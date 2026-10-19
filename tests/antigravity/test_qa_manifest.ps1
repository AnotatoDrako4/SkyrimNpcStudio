<#
.SYNOPSIS
  Self-test suite for Antigravity QA Runner and Manifest Generation.
  Verifies that PASS, intentional FAIL, and SKIP are properly detected, recorded,
  and validated in manifest JSON and summary reports.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runnerScript = Join-Path $repoRoot 'scripts\antigravity\Invoke-SnsQaBaseline.ps1'

$testRunId = "self-test-manifest-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
$testOutRoot = Join-Path $repoRoot 'out\antigravity\runs'

Write-Host "=========================================================="
Write-Host " Running Antigravity QA Lab Self-Test: Manifest & Runner"
Write-Host "=========================================================="

$definitions = @(
    [PSCustomObject]@{
        Name             = 'T01_Legitimate_Passing_Case'
        Type             = 'ScriptBlock'
        Action           = { return 0 }
        ExpectedExitCode = 0
    },
    [PSCustomObject]@{
        Name             = 'T02_Controlled_Intentional_Failure'
        Type             = 'ScriptBlock'
        Action           = { return 42 }
        ExpectedExitCode = 0  # Expected 0, but returned 42 -> Must be marked FAIL
    },
    [PSCustomObject]@{
        Name             = 'T03_Explicit_Skipped_Case'
        Status           = 'SKIP'
        Reason           = 'Integration requires physical hardware'
    }
)

$manifest = & $runnerScript -Suite 'antigravity-manifest-self-test' -TestDefinitions $definitions -RunId $testRunId -OutRoot $testOutRoot

# Assertions
$assertionsFailed = 0

function Assert-Condition([bool]$cond, [string]$msg) {
    if ($cond) {
        Write-Host "  [OK] $msg"
    } else {
        Write-Error "  [ASSERTION FAILED] $msg"
        $script:assertionsFailed++
    }
}

$runDir = Join-Path $testOutRoot $testRunId
$manifestFile = Join-Path $runDir 'manifest.json'
$summaryJsonFile = Join-Path $runDir 'summary.json'
$summaryMdFile = Join-Path $runDir 'summary.md'

Assert-Condition (Test-Path -LiteralPath $manifestFile) "manifest.json exists on disk"
Assert-Condition (Test-Path -LiteralPath $summaryJsonFile) "summary.json exists on disk"
Assert-Condition (Test-Path -LiteralPath $summaryMdFile) "summary.md exists on disk"

$t01 = $manifest.TestCases | Where-Object { $_.Name -eq 'T01_Legitimate_Passing_Case' }
Assert-Condition ($t01.Status -eq 'PASS') "T01 is recorded as PASS"
Assert-Condition ($t01.ExitCode -eq 0) "T01 ExitCode is 0"

$t02 = $manifest.TestCases | Where-Object { $_.Name -eq 'T02_Controlled_Intentional_Failure' }
Assert-Condition ($t02.Status -eq 'FAIL') "T02 intentional fail is correctly recorded as FAIL"
Assert-Condition ($t02.ExitCode -ne 0) "T02 ExitCode is non-zero ($($t02.ExitCode))"

$t03 = $manifest.TestCases | Where-Object { $_.Name -eq 'T03_Explicit_Skipped_Case' }
Assert-Condition ($t03.Status -eq 'SKIP') "T03 is recorded as SKIP"

Assert-Condition ($manifest.OverallStatus -eq 'FAIL') "Overall status reflects presence of failure (FAIL)"

if ($assertionsFailed -eq 0) {
    Write-Host "`nAll Manifest Self-Test Assertions PASSED successfully."
    exit 0
} else {
    Write-Error "`nManifest Self-Test had $assertionsFailed failed assertions."
    exit 1
}
