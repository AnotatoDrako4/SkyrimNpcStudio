<#
.SYNOPSIS
  Computes SHA-256 cryptographic fingerprints and metadata of specified source files.
  Used to detect concurrent modifications and ensure build/test input integrity. No Git.

.DESCRIPTION
  Scans an array of files or directories, calculates SHA-256 hashes, file sizes in bytes,
  and UTC last write timestamps. Provides comparison helpers to detect concurrent edits.

.PARAMETER Paths
  One or more relative or absolute paths to files or directories to fingerprint.

.PARAMETER RootDir
  Base directory for resolving relative paths. Defaults to repository root.

.EXAMPLE
  $fp = .\scripts\antigravity\Get-SnsSourceFingerprint.ps1 -Paths @('engines/nif', 'services/BethesdaWorker')
#>
[CmdletBinding()]
param(
    [string[]]$Paths = @('engines', 'services', 'apps', 'scripts'),
    [string]$RootDir
)

if (-not $RootDir) {
    $currentDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Definition }
    if ($currentDir) {
        $RootDir = Split-Path -Parent (Split-Path -Parent $currentDir)
    } else {
        $RootDir = (Get-Location).Path
    }
}

function Get-SnsFileEntry([string]$fullPath, [string]$baseDir) {
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return $null
    }

    $item = Get-Item -LiteralPath $fullPath
    $stream = $null
    $sha256 = $null
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        $stream = [System.IO.File]::Open($fullPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $hashBytes = $sha256.ComputeHash($stream)
        $hashHex = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        if ($null -ne $sha256) { $sha256.Dispose() }
    }

    $relPath = if ($fullPath.StartsWith($baseDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        $fullPath.Substring($baseDir.Length).TrimStart('\', '/')
    } else {
        $fullPath
    }
    $relPath = $relPath.Replace('\', '/')

    return [PSCustomObject]@{
        Path      = $relPath
        SizeBytes = $item.Length
        UtcTicks  = $item.LastWriteTimeUtc.Ticks
        UtcIso    = $item.LastWriteTimeUtc.ToString("o")
        Sha256    = $hashHex
    }
}

function Get-SnsSourceFingerprint {
    [CmdletBinding()]
    param(
        [string[]]$ScanPaths,
        [string]$BaseDir = $RootDir
    )

    $entries = [System.Collections.Generic.Dictionary[string, PSCustomObject]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($target in $ScanPaths) {
        $resolved = if ([System.IO.Path]::IsPathRooted($target)) { $target } else { Join-Path $BaseDir $target }
        if (-not (Test-Path -LiteralPath $resolved)) {
            continue
        }

        if (Test-Path -LiteralPath $resolved -PathType Leaf) {
            $entry = Get-SnsFileEntry $resolved $BaseDir
            if ($null -ne $entry -and -not $entries.ContainsKey($entry.Path)) {
                $entries[$entry.Path] = $entry
            }
        } elseif (Test-Path -LiteralPath $resolved -PathType Container) {
            $files = Get-ChildItem -LiteralPath $resolved -File -Recurse -Force -ErrorAction SilentlyContinue |
                Where-Object {
                    $p = $_.FullName.Replace('\', '/')
                    # Exclude build trees, caches, git objects and binary temp dirs
                    -not ($p -match '/(build|bin|obj|dist|out|\.git|\.vs|TestResults)/')
                }
            foreach ($file in $files) {
                $entry = Get-SnsFileEntry $file.FullName $BaseDir
                if ($null -ne $entry -and -not $entries.ContainsKey($entry.Path)) {
                    $entries[$entry.Path] = $entry
                }
            }
        }
    }

    $sortedList = [System.Collections.Generic.List[PSCustomObject]]::new()
    foreach ($key in ($entries.Keys | Sort-Object)) {
        $sortedList.Add($entries[$key])
    }

    return [PSCustomObject]@{
        TimestampUtc = [System.DateTime]::UtcNow.ToString("o")
        BaseDirectory = $BaseDir
        FileCount     = $sortedList.Count
        Files         = $sortedList.ToArray()
    }
}

function Compare-SnsSourceFingerprint {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Before,
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$After
    )

    $beforeMap = @{}
    foreach ($f in $Before.Files) { $beforeMap[$f.Path] = $f }

    $afterMap = @{}
    foreach ($f in $After.Files) { $afterMap[$f.Path] = $f }

    $changed = [System.Collections.Generic.List[string]]::new()
    $added = [System.Collections.Generic.List[string]]::new()
    $removed = [System.Collections.Generic.List[string]]::new()

    foreach ($path in $beforeMap.Keys) {
        if (-not $afterMap.ContainsKey($path)) {
            $removed.Add($path)
        } else {
            $b = $beforeMap[$path]
            $a = $afterMap[$path]
            if ($b.Sha256 -ne $a.Sha256 -or $b.SizeBytes -ne $a.SizeBytes) {
                $changed.Add($path)
            }
        }
    }

    foreach ($path in $afterMap.Keys) {
        if (-not $beforeMap.ContainsKey($path)) {
            $added.Add($path)
        }
    }

    $hasDifferences = ($changed.Count -gt 0 -or $added.Count -gt 0 -or $removed.Count -gt 0)

    return [PSCustomObject]@{
        HasDifferences = $hasDifferences
        ChangedFiles   = $changed.ToArray()
        AddedFiles     = $added.ToArray()
        RemovedFiles   = $removed.ToArray()
    }
}

# If executed directly as a script (not dot-sourced)
if ($MyInvocation.InvocationName -ne '.' -and $MyInvocation.Line -notmatch '^\s*\.\s+') {
    $result = Get-SnsSourceFingerprint -ScanPaths $Paths -BaseDir $RootDir
    $result | ConvertTo-Json -Depth 4
}
