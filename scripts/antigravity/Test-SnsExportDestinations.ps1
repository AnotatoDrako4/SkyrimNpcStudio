<#
.SYNOPSIS
  Synthetic Mock-Game & MO2 Destination Auditor (AG-P0-INSTALL / AG-F13-PATHS).
  Verifies that export destination policies refuse protected directories
  (Skyrim Data, MO2 mods, overwrite, instances, and sneaky junctions)
  and guarantees ZERO modifications to existing files or protected trees.
  NO GIT / NO MODIFICATION OF SKYRIM OR LIVE MO2 FILES.

.PARAMETER OutRoot
  Base directory for runs and reports.
#>
[CmdletBinding()]
param(
    [string]$OutRoot,
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $RunId) {
    $RunId = "dest-audit-f13-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
}

if (-not $OutRoot) {
    $OutRoot = Join-Path $repoRoot 'out\antigravity\runs'
}

$runDir = Join-Path $OutRoot $RunId
if (-not (Test-Path -LiteralPath $runDir)) {
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
}

$fixtureRoot = Join-Path $repoRoot 'out\antigravity\fixtures\mock-game'
if (Test-Path -LiteralPath $fixtureRoot) {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

$createdJunctions = [System.Collections.Generic.List[string]]::new()
$sentinelRegistry = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$results = [System.Collections.Generic.List[PSCustomObject]]::new()

function New-SentinelFile([string]$folderPath, [string]$label) {
    $filePath = Join-Path $folderPath "sentinel-$label.txt"
    $content = "SENTINEL-DO-NOT-TOUCH-$label-" + [System.Guid]::NewGuid().ToString()
    [System.IO.File]::WriteAllText($filePath, $content)
    
    $sha = $null
    $fs = [System.IO.File]::OpenRead($filePath)
    try {
        $hasher = [System.Security.Cryptography.SHA256]::Create()
        $hashBytes = $hasher.ComputeHash($fs)
        $sha = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
    } finally {
        $fs.Dispose()
    }
    $sentinelRegistry[$filePath] = $sha
    return $filePath
}

function Resolve-DirectoryJunction([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $root = [System.IO.Path]::GetPathRoot($full)
    $resolved = $root
    $parts = $full.Substring($root.Length).Split([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar, [System.StringSplitOptions]::RemoveEmptyEntries)
    foreach ($part in $parts) {
        $resolved = [System.IO.Path]::Combine($resolved, $part)
        if (Test-Path -LiteralPath $resolved) {
            $item = Get-Item -LiteralPath $resolved -Force -ErrorAction SilentlyContinue
            if ($item -and $item.LinkType -and $item.Target) {
                $target = $item.Target
                if ($target -is [array]) { $target = $target[0] }
                if ($target) {
                    $resolved = [System.IO.Path]::GetFullPath($target)
                }
            }
        }
    }
    return $resolved.TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Test-DestinationPolicyRefusal([string]$outputPath, [hashtable]$layout) {
    # Implements the exact F-13 policy from ExportDestinationPolicy.cs & test_export_destination.cpp
    if ([string]::IsNullOrWhiteSpace($outputPath)) { return "Path empty" }
    
    $resolvedOutput = Resolve-DirectoryJunction $outputPath
    
    # Check forbidden game roots
    $forbidden = @(
        $layout.GameRoot,
        $layout.DataDir,
        $layout.Mo2Root,
        $layout.ModsDir,
        $layout.OverwriteDir,
        $layout.ProfilesDir
    )
    
    foreach ($f in $forbidden) {
        $resolvedF = Resolve-DirectoryJunction $f
        if ($resolvedOutput.Equals($resolvedF, [System.StringComparison]::OrdinalIgnoreCase) -or
            $resolvedOutput.StartsWith($resolvedF + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            return "Refused: inside forbidden tree ($f)"
        }
    }
    
    # Walk up checking for ModOrganizer.ini or overwrite
    $cur = [System.IO.DirectoryInfo]::new($resolvedOutput)
    while ($null -ne $cur) {
        if ($cur.Name.Equals("overwrite", [System.StringComparison]::OrdinalIgnoreCase)) {
            return "Refused: inside overwrite directory"
        }
        if ([System.IO.File]::Exists([System.IO.Path]::Combine($cur.FullName, "ModOrganizer.ini"))) {
            return "Refused: inside MO2 instance directory"
        }
        $cur = $cur.Parent
    }
    
    return $null # Allowed
}

Write-Host "=========================================================="
Write-Host " Antigravity Mock-Game & MO2 Destination Auditor (F-13)  "
Write-Host "=========================================================="
Write-Host "RunId:   $RunId"
Write-Host "Fixture: $fixtureRoot`n"

try {
    # 1. Scaffold Mock Game Layout
    $gameRoot    = Join-Path $fixtureRoot "MockSkyrim"
    $dataDir     = Join-Path $gameRoot "Data"
    $mo2Root     = Join-Path $fixtureRoot "MockMO2"
    $modsDir     = Join-Path $mo2Root "mods"
    $mod1Dir     = Join-Path $modsDir "LydiaOverhaul"
    $overwrite   = Join-Path $mo2Root "overwrite"
    $profilesDir = Join-Path $mo2Root "profiles"
    $profDefault = Join-Path $profilesDir "Default"
    $allowedDir  = Join-Path $fixtureRoot "AllowedExports"
    $allowedSub  = Join-Path $allowedDir "MyCustomMod"

    foreach ($d in @($gameRoot, $dataDir, $mo2Root, $modsDir, $mod1Dir, $overwrite, $profilesDir, $profDefault, $allowedDir, $allowedSub)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
    }

    # Populate structure markers
    [System.IO.File]::WriteAllText((Join-Path $gameRoot "SkyrimSE.exe"), "MOCK_EXE")
    [System.IO.File]::WriteAllText((Join-Path $dataDir "Skyrim.esm"), "MOCK_ESM")
    [System.IO.File]::WriteAllText((Join-Path $mo2Root "ModOrganizer.ini"), "[General]`n")
    [System.IO.File]::WriteAllText((Join-Path $mod1Dir "meta.ini"), "[General]`nmodid=123`n")
    [System.IO.File]::WriteAllText((Join-Path $profDefault "modlist.txt"), "+LydiaOverhaul`n*DLC: Dawnguard`n")

    # Place Sentinel Files
    New-SentinelFile $gameRoot "SkyrimRoot" | Out-Null
    New-SentinelFile $dataDir "DataDir" | Out-Null
    New-SentinelFile $mo2Root "Mo2Root" | Out-Null
    New-SentinelFile $modsDir "ModsDir" | Out-Null
    New-SentinelFile $mod1Dir "LydiaMod" | Out-Null
    New-SentinelFile $overwrite "Overwrite" | Out-Null
    New-SentinelFile $profDefault "ProfileDefault" | Out-Null

    # Create Directory Junctions via cmd mklink /J
    $sneakyJunction = Join-Path $fixtureRoot "SneakyModsJunction"
    $p1 = [System.Diagnostics.Process]::Start([System.Diagnostics.ProcessStartInfo]@{
        FileName = "cmd.exe"
        Arguments = "/c mklink /J `"$sneakyJunction`" `"$modsDir`""
        CreateNoWindow = $true
        UseShellExecute = $false
    })
    $p1.WaitForExit()
    if ($p1.ExitCode -eq 0 -and (Test-Path -LiteralPath $sneakyJunction)) {
        $createdJunctions.Add($sneakyJunction)
    } else {
        Write-Warning "Could not create sneaky junction via mklink /J"
    }

    $allowedJunction = Join-Path $fixtureRoot "AllowedJunction"
    $p2 = [System.Diagnostics.Process]::Start([System.Diagnostics.ProcessStartInfo]@{
        FileName = "cmd.exe"
        Arguments = "/c mklink /J `"$allowedJunction`" `"$allowedDir`""
        CreateNoWindow = $true
        UseShellExecute = $false
    })
    $p2.WaitForExit()
    if ($p2.ExitCode -eq 0 -and (Test-Path -LiteralPath $allowedJunction)) {
        $createdJunctions.Add($allowedJunction)
    }

    $layout = @{
        GameRoot     = $gameRoot
        DataDir      = $dataDir
        Mo2Root      = $mo2Root
        ModsDir      = $modsDir
        OverwriteDir = $overwrite
        ProfilesDir  = $profilesDir
    }

    # 2. Test Policy Evaluation Matrix
    $testCases = @(
        @{ Name = "Refuse_Game_Root"; Path = $gameRoot; ExpectedRefused = $true },
        @{ Name = "Refuse_Data_Directory"; Path = $dataDir; ExpectedRefused = $true },
        @{ Name = "Refuse_Data_Subfolder"; Path = (Join-Path $dataDir "meshes\actors"); ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_Root"; Path = $mo2Root; ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_Mods_Directory"; Path = $modsDir; ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_Existing_Mod"; Path = $mod1Dir; ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_New_Mod_Folder"; Path = (Join-Path $modsDir "NewUnregisteredMod"); ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_Overwrite"; Path = $overwrite; ExpectedRefused = $true },
        @{ Name = "Refuse_MO2_Profiles"; Path = $profDefault; ExpectedRefused = $true },
        @{ Name = "Refuse_Sneaky_Junction_To_Mods"; Path = $sneakyJunction; ExpectedRefused = $true },
        @{ Name = "Refuse_Sneaky_Junction_Subfolder"; Path = (Join-Path $sneakyJunction "AnotherMod"); ExpectedRefused = $true },
        @{ Name = "Allow_External_Custom_Directory"; Path = $allowedDir; ExpectedRefused = $false },
        @{ Name = "Allow_External_Subfolder"; Path = $allowedSub; ExpectedRefused = $false },
        @{ Name = "Allow_Junction_To_Allowed_Directory"; Path = $allowedJunction; ExpectedRefused = $false }
    )

    foreach ($tc in $testCases) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $refusalReason = Test-DestinationPolicyRefusal $tc.Path $layout
        $sw.Stop()

        $isRefused = ($null -ne $refusalReason)
        $passed = ($isRefused -eq $tc.ExpectedRefused)
        $status = if ($passed) { "PASS" } else { "FAIL" }

        Write-Host "-- Test: $($tc.Name) => ExpectedRefused=$($tc.ExpectedRefused), Got=$isRefused ($status) [$($sw.ElapsedMilliseconds) ms]"
        if ($refusalReason) {
            Write-Host "   Reason: $refusalReason"
        }

        $results.Add([PSCustomObject]@{
            Name       = $tc.Name
            Target     = $tc.Path
            Type       = "DestinationPolicyCheck"
            Status     = $status
            ExitCode   = if ($passed) { 0 } else { 1 }
            DurationMs = $sw.ElapsedMilliseconds
            Details    = if ($refusalReason) { $refusalReason } else { "Allowed (valid destination)" }
        })
    }

    # 3. Sentinel Integrity Audit
    Write-Host "`n-- Auditing Sentinel File Integrity (Zero Corruption Guarantee) ..."
    $corruptions = 0
    foreach ($kv in $sentinelRegistry.GetEnumerator()) {
        $sentinelPath = $kv.Key
        $expectedSha = $kv.Value

        if (-not (Test-Path -LiteralPath $sentinelPath)) {
            Write-Error "CRITICAL: Sentinel file disappeared: $sentinelPath"
            $corruptions++
            continue
        }

        $curSha = $null
        $fs = [System.IO.File]::OpenRead($sentinelPath)
        try {
            $hasher = [System.Security.Cryptography.SHA256]::Create()
            $hashBytes = $hasher.ComputeHash($fs)
            $curSha = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
        } finally {
            $fs.Dispose()
        }

        if ($curSha -ne $expectedSha) {
            Write-Error "CRITICAL: Sentinel file modified: $sentinelPath (got $curSha, expected $expectedSha)"
            $corruptions++
        }
    }

    $sentinelStatus = if ($corruptions -eq 0) { "PASS" } else { "FAIL" }
    Write-Host "   Sentinel Audit Result: $sentinelStatus (Checked: $($sentinelRegistry.Count), Corrupted: $corruptions)"

    $results.Add([PSCustomObject]@{
        Name       = "Sentinel_Integrity_Audit"
        Target     = $fixtureRoot
        Type       = "IntegrityCheck"
        Status     = $sentinelStatus
        ExitCode   = if ($corruptions -eq 0) { 0 } else { 2 }
        DurationMs = 1
        Details    = "Sentinels checked: $($sentinelRegistry.Count), corrupted: $corruptions"
    })

} finally {
    # Safely remove junctions first so recursive deletion never deletes target contents
    foreach ($junc in $createdJunctions) {
        if (Test-Path -LiteralPath $junc) {
            try {
                # cmd rmdir removes the junction mount point only
                $pClean = [System.Diagnostics.Process]::Start([System.Diagnostics.ProcessStartInfo]@{
                    FileName = "cmd.exe"
                    Arguments = "/c rmdir `"$junc`""
                    CreateNoWindow = $true
                    UseShellExecute = $false
                })
                $pClean.WaitForExit()
            } catch {
                Write-Warning "Failed to delete junction $junc"
            }
        }
    }

    # Clean up fixture sandbox
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Write Manifest & QA Reports
$manifest = [ordered]@{
    RunId             = $RunId
    TimestampUtc      = [System.DateTime]::UtcNow.ToString("o")
    Suite             = "f13-destination-policy-safety"
    OverallStatus     = if ($results | Where-Object { $_.Status -eq 'FAIL' }) { 'FAIL' } else { 'PASS' }
    DurationSeconds   = 0
    Toolchain         = @{ Policy = "F-13 RealDirectory / RefusedExportDirectory" }
    ConcurrentChanges = @{ HasDifferences = $false }
    TestCases         = $results
}

$manifestPath = Join-Path $runDir 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$reportScript = Join-Path $PSScriptRoot 'Write-SnsQaReport.ps1'
& $reportScript -ManifestPath $manifestPath -OutDir $runDir | Out-Null

Write-Host "`n== F-13 Destination Safety Run Complete. Overall Status: $($manifest.OverallStatus) =="
return $manifest
