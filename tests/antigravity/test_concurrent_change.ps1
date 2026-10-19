<#
.SYNOPSIS
  Self-test suite for concurrent source modification detection.
  Proves that if an input file is modified while a test runs, the result is
  strictly marked as INVALIDATED_BY_CONCURRENT_EDIT and not a false PASS.
#>
[CmdletBinding()]
param()

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runnerScript = Join-Path $repoRoot 'scripts\antigravity\Invoke-SnsQaBaseline.ps1'
$fingerprintScript = Join-Path $repoRoot 'scripts\antigravity\Get-SnsSourceFingerprint.ps1'
. $fingerprintScript

$testRunId = "self-test-concurrent-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
$testOutRoot = Join-Path $repoRoot 'out\antigravity\runs'

Write-Host "=========================================================="
Write-Host " Running Antigravity QA Lab Self-Test: Concurrent Change"
Write-Host "=========================================================="

# Create temporary fixture
$fixtureDir = Join-Path $repoRoot 'out\antigravity\fixtures'
if (-not (Test-Path -LiteralPath $fixtureDir)) {
    New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
}
$fixtureFile = Join-Path $fixtureDir 'monitored_source_sample.txt'
[System.IO.File]::WriteAllText($fixtureFile, "Initial Content v1", [System.Text.Encoding]::UTF8)

# 1. Test direct comparison function
$fp1 = Get-SnsSourceFingerprint -ScanPaths @($fixtureFile) -BaseDir $repoRoot
Start-Sleep -Milliseconds 100
[System.IO.File]::WriteAllText($fixtureFile, "Modified Content v2 (Concurrent write by external process)", [System.Text.Encoding]::UTF8)
$fp2 = Get-SnsSourceFingerprint -ScanPaths @($fixtureFile) -BaseDir $repoRoot

$diff = Compare-SnsSourceFingerprint -Before $fp1 -After $fp2

$assertionsFailed = 0
function Assert-Condition([bool]$cond, [string]$msg) {
    if ($cond) {
        Write-Host "  [OK] $msg"
    } else {
        Write-Error "  [ASSERTION FAILED] $msg"
        $script:assertionsFailed++
    }
}

Assert-Condition ($diff.HasDifferences -eq $true) "Compare-SnsSourceFingerprint detects that content changed"
Assert-Condition ($diff.ChangedFiles.Count -gt 0) "ChangedFiles contains modified path"

# 2. Test runner behavior when concurrent edit occurs
# Reset file
[System.IO.File]::WriteAllText($fixtureFile, "Initial Content v3", [System.Text.Encoding]::UTF8)

# We will run a test definition where the action modifies an input file in engines/ or scripts/antigravity/
# To be completely safe and not touch Claude's files, let's create a scratch file under tests/antigravity/
$scratchWatched = Join-Path $PSScriptRoot 'temp_watched_source.txt'
[System.IO.File]::WriteAllText($scratchWatched, "State A", [System.Text.Encoding]::UTF8)

try {
    $definitions = @(
        [PSCustomObject]@{
            Name             = 'T01_Running_While_Source_Changes'
            Type             = 'ScriptBlock'
            Action           = {
                # Simulate external agent editing a source file during the test
                Start-Sleep -Milliseconds 100
                [System.IO.File]::WriteAllText($scratchWatched, "State B (concurrent edit!)", [System.Text.Encoding]::UTF8)
                return 0
            }
            ExpectedExitCode = 0
        }
    )

    $manifest = & $runnerScript -Suite 'concurrent-detection-test' -TestDefinitions $definitions -RunId $testRunId -OutRoot $testOutRoot

    $t01 = $manifest.TestCases | Where-Object { $_.Name -eq 'T01_Running_While_Source_Changes' }
    Assert-Condition ($t01.Status -eq 'INVALIDATED_BY_CONCURRENT_EDIT') "Test case status is strictly marked INVALIDATED_BY_CONCURRENT_EDIT"
    Assert-Condition ($manifest.OverallStatus -eq 'INVALIDATED_BY_CONCURRENT_EDIT') "Overall run status is marked INVALIDATED_BY_CONCURRENT_EDIT"

} finally {
    if (Test-Path -LiteralPath $scratchWatched) {
        Remove-Item -LiteralPath $scratchWatched -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $fixtureFile) {
        Remove-Item -LiteralPath $fixtureFile -Force -ErrorAction SilentlyContinue
    }
}

if ($assertionsFailed -eq 0) {
    Write-Host "`nAll Concurrent Change Detection Assertions PASSED successfully."
    exit 0
} else {
    Write-Error "`nConcurrent Change Test had $assertionsFailed failed assertions."
    exit 1
}
