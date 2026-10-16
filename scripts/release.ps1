<#
.SYNOPSIS
  Makes a release ZIP from dist\SkyrimNpcStudio and verifies it (scripts\verify-release.ps1).
  1. Refuses a working tree with uncommitted changes to tracked files (-AllowDirty marks it -dirty).
  2. Runs package.ps1 (Release build, Qt runtime, self-contained worker) unless -SkipPackage.
  3. Writes release-manifest.json (every file with size and SHA-256) into the package.
  4. Zips it deterministically: entries sorted, all dated with the commit time, so the same dist
     always gives the same ZIP bytes. Writes <zip>.sha256 next to it.
  5. Verifies the ZIP and runs the extracted copy with a fresh environment.

.EXAMPLE
  .\scripts\release.ps1
.EXAMPLE
  .\scripts\release.ps1 -SkipPackage -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 002B6C:Dawnguard.esm -Scenarios hair-safe
#>
[CmdletBinding()]
param(
    [switch]$AllowDirty,
    [switch]$SkipPackage,
    [string]$Instance,
    [string]$MO2Profile,
    [string]$Npc,
    [string]$Scenarios
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'dist\SkyrimNpcStudio'

$changes = @(git -C $root status --porcelain --untracked-files=no)
if ($changes.Count -gt 0 -and -not $AllowDirty) {
    throw "Hay cambios sin commit en archivos versionados ($($changes.Count)); una release se hace desde un commit. Usa -AllowDirty para una prueba."
}
$commit = (git -C $root rev-parse HEAD).Trim()
$short = $commit.Substring(0, 7)
$commitTime = [DateTimeOffset]::Parse((git -C $root log -1 --format=%cI).Trim())
$version = if ((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw) -match 'project\(SkyrimNpcStudio\s+VERSION\s+([0-9.]+)') { $Matches[1] } else { '0.0.0' }

if (-not $SkipPackage) {
    & (Join-Path $PSScriptRoot 'package.ps1')
    if ($LASTEXITCODE) { throw "package.ps1 failed ($LASTEXITCODE)" }
}
if (-not (Test-Path (Join-Path $package 'SkyrimNpcStudio.exe'))) { throw "No package in $package; run without -SkipPackage." }
$built = Get-Content (Join-Path $package 'BUILDINFO.json') -Raw | ConvertFrom-Json
if ($built.commit -ne $commit) { throw "dist\ was built from $($built.commit), HEAD is $commit; package again." }

# ---- manifest (sorted, forward slashes: the paths as they appear in the ZIP)
$manifestPath = Join-Path $package 'release-manifest.json'
if (Test-Path $manifestPath) { [IO.File]::Delete($manifestPath) }
$base = [IO.Path]::GetFullPath($package).TrimEnd('\') + '\'
$files = Get-ChildItem $package -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName.Substring($base.Length).Replace('\', '/'); file = $_.FullName; bytes = $_.Length }
} | Sort-Object { $_.path } -CaseSensitive
$list = foreach ($f in $files) {
    [ordered]@{ path = $f.path; bytes = $f.bytes; sha256 = (Get-FileHash -Algorithm SHA256 $f.file).Hash.ToLowerInvariant() }
}
[ordered]@{
    product = 'Skyrim NPC Studio'; version = $version; commit = $commit; dirty = ($changes.Count -gt 0)
    files   = @($list)
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $manifestPath

# ---- deterministic ZIP
$suffix = if ($changes.Count -gt 0) { '-dirty' } else { '' }
$zipPath = Join-Path $root "dist\SkyrimNpcStudio-$version-$short$suffix-win64.zip"
if (Test-Path $zipPath) { [IO.File]::Delete($zipPath) }
$entries = @($files | ForEach-Object { $_ }) + [pscustomobject]@{ path = 'release-manifest.json'; file = $manifestPath }
$entries = $entries | Sort-Object { $_.path } -CaseSensitive
$stream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
$zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($e in $entries) {
        $entry = $zip.CreateEntry("SkyrimNpcStudio/$($e.path)", [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $commitTime
        $out = $entry.Open()
        $in = [IO.File]::OpenRead($e.file)
        try { $in.CopyTo($out) } finally { $in.Dispose(); $out.Dispose() }
    }
} finally { $zip.Dispose(); $stream.Dispose() }
$hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
"$hash *$(Split-Path -Leaf $zipPath)" | Set-Content -Encoding ascii "$zipPath.sha256"
$mb = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Host "ZIP: $zipPath ($mb MB, $($entries.Count) files)  sha256 $hash"

# ---- verify
$verify = @{ Zip = $zipPath }
foreach ($k in 'Instance', 'MO2Profile', 'Npc', 'Scenarios') { if ($PSBoundParameters[$k]) { $verify[$k] = $PSBoundParameters[$k] } }
& (Join-Path $PSScriptRoot 'verify-release.ps1') @verify
exit $LASTEXITCODE
