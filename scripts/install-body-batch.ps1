<#
.SYNOPSIS
  Installs a Compilar-Cuerpos.bat batch into MO2 in a reversible way. Without -Apply it only reports what it would do.

  - New mod with the batch's meshes, new test profile (a copy of -SourceProfile) with that mod on top.
  - Meshes of the UBE folder in Overwrite (which hide any mod) move to a second new mod, enabled in the source profile and the test profile.
    The source profile keeps the same look; its only change is that line in modlist.txt.
  - Meshes in Overwrite outside the UBE folder stay there, and are left out of the mod (other profiles can use them).
  Refuses to run while MO2 or Skyrim are open. revert-body-batch.ps1 undoes it from the manifest.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Batch,
    [string]$Base = 'F:\Mods Skyrim',
    [string]$SourceProfile = 'CommunityShaders-AB-Test',
    [string]$TestProfile = 'SNS-Cuerpos-Test',
    [string]$BodyMod = 'SNS_Cuerpos_Compilados',
    [string]$MovedMod = 'SNS_Overwrite_UBE_Movido',
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$overwrite = Join-Path $Base 'overwrite'
$mods = Join-Path $Base 'mods'
$profiles = Join-Path $Base 'profiles'
$source = Join-Path $profiles $SourceProfile
$test = Join-Path $profiles $TestProfile
$batchMod = Join-Path $Batch 'mod'
$bodyDir = Join-Path $mods $BodyMod
$movedDir = Join-Path $mods $MovedMod

foreach ($path in @($bodyDir, $movedDir, $test)) { if (Test-Path -LiteralPath $path) { throw "Ya existe: $path" } }
if (-not (Test-Path -LiteralPath (Join-Path $source 'modlist.txt'))) { throw "Perfil no encontrado: $source" }

function Get-Relative([string]$Root, [string]$Full) { $Full.Substring($Root.TrimEnd('\').Length + 1) }
$ubePrefix = 'meshes\!ube\'
$copy = [Collections.Generic.List[string]]::new()
$kept = [Collections.Generic.List[string]]::new()
$move = [Collections.Generic.List[string]]::new()
foreach ($file in Get-ChildItem -LiteralPath $batchMod -Recurse -File) {
    $rel = Get-Relative $batchMod $file.FullName
    $inOverwrite = Test-Path -LiteralPath (Join-Path $overwrite $rel)
    if ($inOverwrite -and -not $rel.ToLowerInvariant().StartsWith($ubePrefix)) { $kept.Add($rel); continue }
    $copy.Add($rel)
    if ($inOverwrite) { $move.Add($rel) }
}
# The .tri next to a moved body goes with it: its mesh's BODYTRI points there.
$moveAll = [Collections.Generic.List[string]]::new($move)
foreach ($rel in @($move)) {
    $stem = $rel -replace '_[01]\.nif$', '.tri'
    if ($stem -ne $rel -and -not $moveAll.Contains($stem) -and (Test-Path -LiteralPath (Join-Path $overwrite $stem))) { $moveAll.Add($stem) }
}
"Mallas a instalar en '$BodyMod': $($copy.Count)"
"Archivos que salen de Overwrite hacia '$MovedMod': $($moveAll.Count)"
"Archivos de Overwrite que no se tocan y quedan fuera del mod: $($kept.Count)"
"Perfil nuevo: $TestProfile. Cambio en '$SourceProfile': solo +$MovedMod en modlist.txt"
$moveAll | ForEach-Object { "  mover: $_" }
$kept | ForEach-Object { "  no se toca: $_" }
if (-not $Apply) { 'SIMULACION: no se ha cambiado nada. Usa -Apply para instalar.'; return }

$running = Get-Process -Name ModOrganizer, SkyrimSE, skse64_loader -ErrorAction SilentlyContinue
if ($running) { throw "Cierra antes: $(($running.Name | Sort-Object -Unique) -join ', ')" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $Batch "respaldo-$stamp"
New-Item -ItemType Directory -Force -Path $backup | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'modlist.txt') -Destination (Join-Path $backup 'modlist.txt')
$moved = foreach ($rel in $moveAll) {
    $from = Join-Path $overwrite $rel
    $to = Join-Path $movedDir $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    $hash = (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $from -Destination $to
    if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $hash) { throw "Copia distinta: $rel" }
    Remove-Item -LiteralPath $from
    [ordered]@{ path = $rel; sha256 = $hash }
}
foreach ($rel in $copy) {
    $to = Join-Path $bodyDir $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath (Join-Path $batchMod $rel) -Destination $to
}
foreach ($dir in @($bodyDir, $movedDir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'meta.ini'), "[General]`r`nmodid=0`r`nversion=1.0`r`ncomments=Skyrim NPC Studio: $stamp`r`n")
}
function Add-ModLines([string]$List, [string[]]$Names) {
    $lines = [Collections.Generic.List[string]]::new([IO.File]::ReadAllLines($List))
    $at = 0
    while ($at -lt $lines.Count -and $lines[$at].StartsWith('#')) { $at++ }  # top of the list = highest priority
    foreach ($n in $Names) { $lines.Insert($at, "+$n"); $at++ }
    [IO.File]::WriteAllLines($List, $lines)
}
Copy-Item -LiteralPath $source -Destination $test -Recurse
Add-ModLines (Join-Path $test 'modlist.txt') @($BodyMod, $MovedMod)
Add-ModLines (Join-Path $source 'modlist.txt') @($MovedMod)
$manifest = [ordered]@{
    createdUtc = (Get-Date).ToUniversalTime().ToString('o'); base = $Base; sourceProfile = $SourceProfile; testProfile = $TestProfile
    bodyMod = $BodyMod; movedMod = $MovedMod; backup = $backup; moved = @($moved); installed = @($copy)
}
$manifestPath = Join-Path $Batch 'instalacion.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
"INSTALADO. Elige el perfil '$TestProfile' en MO2. Para deshacer: scripts\revert-body-batch.ps1 -Manifest `"$manifestPath`""
