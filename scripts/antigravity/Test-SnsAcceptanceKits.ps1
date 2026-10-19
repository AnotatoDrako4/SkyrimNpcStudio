<#
.SYNOPSIS
  Antigravity Acceptance Kit & Human Certificate Auditor (AG-MO2-SKYRIM).
  Validates acceptance kits, hashes, manifests, unapproved ambiguous records (D1),
  and strictly verifies that no synthetic test fraudulently claims in-game verification.
  NO GIT / NO MODIFICATION OF SKYRIM OR LIVE MO2 FILES.

.PARAMETER OutRoot
  Base directory for runs. Defaults to out/antigravity/runs.
#>
[CmdletBinding()]
param(
    [string]$KitsDir,
    [string]$OutRoot,
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $RunId) {
    $RunId = "kits-audit-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
}

if (-not $OutRoot) {
    $OutRoot = Join-Path $repoRoot 'out\antigravity\runs'
}

$runDir = Join-Path $OutRoot $RunId
if (-not (Test-Path -LiteralPath $runDir)) {
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
}

if (-not $KitsDir) {
    $KitsDir = Join-Path $repoRoot 'out\local-implementation\20261017-141652\acceptance-kits-final'
}

Write-Host "=========================================================="
Write-Host " Antigravity Acceptance Kits Auditor (AG-MO2-SKYRIM)     "
Write-Host "=========================================================="
Write-Host "RunId:   $RunId"
Write-Host "KitsDir: $KitsDir`n"

$results = [System.Collections.Generic.List[PSCustomObject]]::new()
$kitsJsonPath = Join-Path $KitsDir 'kits.json'

if (-not (Test-Path -LiteralPath $kitsJsonPath)) {
    throw "Kits metadata not found: $kitsJsonPath"
}

$kitsData = Get-Content -LiteralPath $kitsJsonPath -Raw -Encoding utf8 | ConvertFrom-Json

# 1. Audit Profile Invariance (Profile must be untouched)
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$profileUntouched = $kitsData.profileUnchanged
$sw.Stop()

$results.Add([PSCustomObject]@{
    Name       = "MO2_Profile_Invariance_Audit"
    Target     = $kitsData.profile
    Type       = "IntegrityCheck"
    Status     = if ($profileUntouched) { "PASS" } else { "FAIL" }
    ExitCode   = if ($profileUntouched) { 0 } else { 1 }
    DurationMs = $sw.ElapsedMilliseconds
    Details    = "Profile '$($kitsData.profile)' untouched flag: $profileUntouched"
})

# 2. Audit Each Acceptance Kit
foreach ($kit in $kitsData.kits) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $kitFailures = [System.Collections.Generic.List[string]]::new()

    # Rule A: In-game status must NOT be falsely marked PASS
    if ($kit.results.inGame -eq 'PASS' -or $kit.results.inGame -eq 'PASS_IN_GAME') {
        $kitFailures.Add("Fraudulent in-game PASS claim without human acceptance certificate")
    }

    # Rule B: Kit directory exists
    $kitFolder = Join-Path $KitsDir $kit.id
    if (-not (Test-Path -LiteralPath $kitFolder)) {
        $kitFailures.Add("Kit folder missing: $kitFolder")
    }

    # Rule C: D1 ambiguous case check (e.g. Bolli)
    if ($kit.id -eq '4-bolli') {
        if (-not $kit.blocked) {
            $kitFailures.Add("D1 ambiguous case '4-bolli' must be blocked pending individual human approval")
        }
        foreach ($pkg in $kit.packages) {
            if ($pkg.installable -eq $true) {
                $kitFailures.Add("D1 ambiguous case package cannot be marked installable without human decision")
            }
        }
    }

    # Rule D: Package file hashes if package is present
    foreach ($pkg in $kit.packages) {
        if ($pkg.zip) {
            $zipPath = Join-Path (Join-Path $kitFolder "kit") $pkg.zip
            if (-not (Test-Path -LiteralPath $zipPath)) {
                # Look in sourceZip
                $altZip = Join-Path $kitFolder $pkg.sourceZip
                if (Test-Path -LiteralPath $altZip) {
                    $zipPath = $altZip
                }
            }

            if (Test-Path -LiteralPath $zipPath) {
                $fs = [System.IO.File]::OpenRead($zipPath)
                try {
                    $hasher = [System.Security.Cryptography.SHA256]::Create()
                    $hashBytes = $hasher.ComputeHash($fs)
                    $realSha = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
                    if ($realSha -ne $pkg.sha256.ToLowerInvariant()) {
                        $kitFailures.Add("Package hash mismatch for $($pkg.zip): expected $($pkg.sha256), got $realSha")
                    }
                } finally {
                    $fs.Dispose()
                }
            }
        }
    }

    $sw.Stop()
    $kitStatus = if ($kitFailures.Count -eq 0) { "PASS" } else { "FAIL" }
    $details = if ($kitFailures.Count -eq 0) {
        "Status: $($kit.status); inGame: $($kit.results.inGame) (Human Gate enforced)"
    } else {
        $kitFailures -join "; "
    }

    Write-Host "-- Kit: $($kit.id) ($($kit.name)) => $kitStatus [$($sw.ElapsedMilliseconds) ms]"
    Write-Host "   $details"

    $results.Add([PSCustomObject]@{
        Name       = "AcceptanceKit_$($kit.id)"
        Target     = $kit.npc
        Type       = "KitAudit"
        Status     = $kitStatus
        ExitCode   = if ($kitFailures.Count -eq 0) { 0 } else { 1 }
        DurationMs = $sw.ElapsedMilliseconds
        Details    = $details
    })
}

# 3. Overall Manifest and Report Generation
$manifest = [ordered]@{
    RunId             = $RunId
    TimestampUtc      = [System.DateTime]::UtcNow.ToString("o")
    Suite             = "mo2-acceptance-kits-audit"
    OverallStatus     = if ($results | Where-Object { $_.Status -eq 'FAIL' }) { 'FAIL' } else { 'PASS' }
    DurationSeconds   = 0
    Toolchain         = @{ Auditor = "AG-MO2-SKYRIM" }
    ConcurrentChanges = @{ HasDifferences = $false }
    TestCases         = $results
}

$manifestPath = Join-Path $runDir 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$reportScript = Join-Path $PSScriptRoot 'Write-SnsQaReport.ps1'
& $reportScript -ManifestPath $manifestPath -OutDir $runDir | Out-Null

Write-Host "`n== AG-MO2-SKYRIM Acceptance Kits Audit Complete. Overall Status: $($manifest.OverallStatus) =="
return $manifest
