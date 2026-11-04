<#
.SYNOPSIS
  Undoes install-body-batch.ps1 from its manifest: files back to Overwrite (hash checked), the source profile's modlist.txt restored,
  and only the two mods and the test profile that install created removed. Refuses to run while MO2 or Skyrim are open.
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Manifest)
$ErrorActionPreference = 'Stop'
$m = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$running = Get-Process -Name ModOrganizer, SkyrimSE, skse64_loader -ErrorAction SilentlyContinue
if ($running) { throw "Cierra antes: $(($running.Name | Sort-Object -Unique) -join ', ')" }
$overwrite = Join-Path $m.base 'overwrite'
$movedDir = Join-Path $m.base "mods\$($m.movedMod)"
foreach ($item in $m.moved) {
    $from = Join-Path $movedDir $item.path
    $to = Join-Path $overwrite $item.path
    if (Test-Path -LiteralPath $to) { throw "Ya existe en Overwrite: $($item.path)" }
    if ((Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -ne $item.sha256) { throw "Cambio desde la instalacion: $($item.path)" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to
}
Copy-Item -LiteralPath (Join-Path $m.backup 'modlist.txt') -Destination (Join-Path $m.base "profiles\$($m.sourceProfile)\modlist.txt") -Force
foreach ($dir in @((Join-Path $m.base "mods\$($m.bodyMod)"), $movedDir, (Join-Path $m.base "profiles\$($m.testProfile)"))) {
    Remove-Item -LiteralPath $dir -Recurse -Force -Confirm:$false
}
'REVERTIDO: Overwrite y el modlist del perfil original estan como antes.'
