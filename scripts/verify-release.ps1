<#
.SYNOPSIS
  Checks a release ZIP made by release.ps1 without any development tool: required files present
  (studio, Qt runtime incl. Qt6Test.dll, platform plugins, self-contained worker, licenses), no build
  leftovers, every file equal to release-manifest.json, then extracts it to a new folder and starts
  the studio there with a fresh environment (no Qt, MSVC or .NET SDK on PATH).

.EXAMPLE
  .\scripts\verify-release.ps1 -Zip dist\SkyrimNpcStudio-0.1.0-abc1234-win64.zip
.EXAMPLE
  # Also load a real MO2 profile and NPC from the extracted copy (read-only).
  .\scripts\verify-release.ps1 -Zip <zip> -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 002B6C:Dawnguard.esm -Scenarios hair-safe
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Zip,
    [string]$OutDir = "",
    [string]$Instance,
    [string]$MO2Profile,
    [string]$Npc,
    [string]$Scenarios
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$Zip = (Resolve-Path -LiteralPath $Zip).Path
if (-not $OutDir) { $OutDir = Join-Path $root ("out\release-verify\" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$ok, [string]$what) {
    Write-Host ("{0} {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what)
    if (-not $ok) { $failures.Add($what) }
}

# ---- 1. ZIP contents
$sidecar = "$Zip.sha256"
if (Test-Path $sidecar) {
    $expected = ((Get-Content $sidecar -Raw).Trim() -split '\s+')[0]
    Check ($expected -eq (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()) "ZIP hash equals $(Split-Path -Leaf $sidecar)"
} else {
    Check $false "sidecar $(Split-Path -Leaf $sidecar) present"
}
$archive = [IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $entries = @($archive.Entries | Where-Object { $_.FullName -notmatch '/$' })
    $names = @($entries | ForEach-Object { $_.FullName })
    $prefix = 'SkyrimNpcStudio/'
    Check (@($names | Where-Object { -not $_.StartsWith($prefix) }).Count -eq 0) "every entry under $prefix (one folder to extract)"
    $required = @('SkyrimNpcStudio.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6Quick3D.dll',
                  'Qt6Quick3DAssetUtils.dll', 'Qt6Test.dll', 'platforms/qwindows.dll', 'platforms/qoffscreen.dll',
                  'assetimporters/assimp.dll', 'worker/SkyrimNpcStudio.BethesdaWorker.exe', 'worker/coreclr.dll',
                  'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/nifly-GPL-3.0.txt', 'VERSION.txt',
                  'BUILDINFO.json', 'release-manifest.json')
    foreach ($r in $required) { Check ($names -contains ($prefix + $r)) "contains $r" }
    $leftovers = @($names | Where-Object { $_ -match '\.(pdb|ilk|obj|exp|iobj|ipdb)$|CMakeCache\.txt|/\.git/' })
    Check ($leftovers.Count -eq 0) "no build leftovers ($($leftovers.Count): $($leftovers | Select-Object -First 3))"

    $manifestEntry = $archive.GetEntry($prefix + 'release-manifest.json')
    if ($manifestEntry) {
        $reader = [IO.StreamReader]::new($manifestEntry.Open())
        $manifest = $reader.ReadToEnd() | ConvertFrom-Json
        $reader.Dispose()
        $listed = @{}
        foreach ($f in $manifest.files) { $listed[$prefix + $f.path] = $f }
        $sha = [Security.Cryptography.SHA256]::Create()
        $mismatch = [Collections.Generic.List[string]]::new()
        foreach ($e in $entries) {
            if ($e.FullName -eq $manifestEntry.FullName) { continue }
            $f = $listed[$e.FullName]
            if (-not $f) { $mismatch.Add("unlisted $($e.FullName)"); continue }
            $s = $e.Open()
            $hash = -join ($sha.ComputeHash($s) | ForEach-Object { $_.ToString('x2') })
            $s.Dispose()
            if ($hash -ne $f.sha256 -or $e.Length -ne $f.bytes) { $mismatch.Add("differs $($e.FullName)") }
        }
        foreach ($k in $listed.Keys) { if ($names -notcontains $k) { $mismatch.Add("missing $k") } }
        Check ($mismatch.Count -eq 0) "$($entries.Count - 1) files equal to release-manifest.json ($($mismatch | Select-Object -First 3))"
        Write-Host "     commit $($manifest.commit)  version $($manifest.version)  dirty=$($manifest.dirty)"
    }
} finally { $archive.Dispose() }

# ---- 2. Extract and run with a fresh environment
$extract = Join-Path $OutDir 'extracted'
[IO.Compression.ZipFile]::ExtractToDirectory($Zip, $extract)
$exe = Join-Path $extract 'SkyrimNpcStudio\SkyrimNpcStudio.exe'
$log = Join-Path $OutDir 'release-run.log'
$arguments = @('--smoke-test', '--screenshot', "`"$(Join-Path $OutDir 'release-run.png')`"")
if ($Instance) { $arguments += @('--instance', "`"$Instance`"") }
if ($MO2Profile) { $arguments += @('--profile', "`"$MO2Profile`"") }
if ($Npc) { $arguments += @('--npc', "`"$Npc`"") }
if ($Scenarios) { $arguments += @('--stress-appearance', $Scenarios, '--stress-work', "`"$(Join-Path $OutDir 'work')`"") }
# -UseNewEnvironment: machine + user variables only, so nothing from a dev shell (Qt, MSVC, .NET SDK).
$p = Start-Process -FilePath $exe -ArgumentList $arguments -UseNewEnvironment -WindowStyle Hidden -PassThru -RedirectStandardError $log
$null = $p.Handle
if (-not $p.WaitForExit(20 * 60 * 1000)) { $p.Kill(); Check $false 'extracted studio finished within 20 minutes' }
$p.WaitForExit()
Check ($p.ExitCode -eq 0) "extracted studio exit code $($p.ExitCode) (log: $log)"
$qml = @(Select-String -Path $log -Pattern '\.qml:\d+' | Where-Object { $_.Line -notmatch 'propertyCache' })
Check ($qml.Count -eq 0) "no QML warnings ($($qml.Count))"
Select-String -Path $log -Pattern 'smoke:|summary:' | ForEach-Object { Write-Host "     $($_.Line)" }

if ($failures.Count -gt 0) {
    Write-Host "FAILED: $($failures.Count) check(s)"
    exit 1
}
Write-Host "Release verified: $Zip"
exit 0
