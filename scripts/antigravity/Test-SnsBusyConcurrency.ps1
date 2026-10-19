<#
.SYNOPSIS
  Antigravity Concurrency & Lock Safety Auditor (AG-F14-LOCK).
  Validates F-14 concurrency safety: while an export or indexing operation holds the
  catalog gate, reads receive Busy (1007) and retry with exponential backoff without
  deadlocks, while mutating calls (exports/index) are rejected immediately.

.DESCRIPTION
  Executes Catch2 [busy] suite in sns_engine_tests.exe and verifies the retry contract:
  1. Only read methods enter retry on Busy (write/export methods fail fast).
  2. Bounded backoff retry successfully completes when lock clears.
  3. Timeout bounds are strictly enforced (no infinite wait).
  4. Cancellation tokens abort retries cleanly.
  5. Zero deadlocks during concurrent access.
  NO GIT / NO GITHUB.
#>
[CmdletBinding()]
param(
    [string]$Preset = 'debug',
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $OutputDir) {
    $OutputDir = Join-Path $repoRoot "out\antigravity\runs\concurrency-audit-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))"
}
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$devEnv = Join-Path $repoRoot 'scripts\dev-env.ps1'
if (Test-Path $devEnv) {
    . $devEnv | Out-Null
}

$engineExe = Join-Path $repoRoot "build\$Preset\bin\sns_engine_tests.exe"
if (-not (Test-Path $engineExe)) {
    throw "sns_engine_tests.exe not found at $engineExe"
}

Write-Host "=== [AG-F14-LOCK] Antigravity Concurrency & Busy-Lock Safety Auditor ===" -ForegroundColor Cyan
Write-Host "Engine binary: $engineExe" -ForegroundColor Gray
Write-Host "Output directory: $OutputDir" -ForegroundColor Gray

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$results = [System.Collections.Generic.List[PSCustomObject]]::new()

# 1. Run Catch2 [busy] test suite
$logPath = Join-Path $OutputDir 'catch2-busy.log'
& $engineExe @('[busy]', '-r', 'console', '-s') *>&1 | Set-Content -Path $logPath -Encoding utf8
$exitCode = $LASTEXITCODE
$sw.Stop()

$logContent = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { "" }
$passMatch = [regex]::Match($logContent, 'All tests passed \((\d+) assertions in (\d+) test cases\)')
$assertions = if ($passMatch.Success) { [int]$passMatch.Groups[1].Value } else { 0 }
$testCases = if ($passMatch.Success) { [int]$passMatch.Groups[2].Value } else { 0 }

$busyPassed = ($exitCode -eq 0) -and $passMatch.Success

$results.Add([PSCustomObject]@{
    Check = "Catch2_Engine_Busy_Suite"
    Status = if ($busyPassed) { "PASS" } else { "FAIL" }
    ExitCode = $exitCode
    Assertions = $assertions
    TestCases = $testCases
    DurationMs = $sw.ElapsedMilliseconds
    Details = "Verified [busy] contract in C++ RPC client & retry logic"
})

# 2. Verify specific sub-contracts from output
$subContracts = @(
    @{ Name = "ReadOnlyRetryContract"; Pattern = "Only reads are retried after Busy" },
    @{ Name = "ReadRepeatUntilAnswer"; Pattern = "A read answered Busy is repeated until the worker answers it" },
    @{ Name = "ExportNoRetryContract"; Pattern = "An export answered Busy is not repeated" },
    @{ Name = "TimeoutBoundedContract"; Pattern = "Busy retries end at the caller's timeout with the Busy error" },
    @{ Name = "CancellationAbortsContract"; Pattern = "A cancel flag stops the Busy retries" }
)

foreach ($sc in $subContracts) {
    $found = $logContent -match [regex]::Escape($sc.Pattern)
    $results.Add([PSCustomObject]@{
        Check = "Contract_" + $sc.Name
        Status = if ($found -and $busyPassed) { "PASS" } else { "FAIL" }
        Details = $sc.Pattern
    })
}

# 3. Write structured JSON artifact
$summary = [PSCustomObject]@{
    Suite = "AG-F14-LOCK-CONCURRENCY"
    Timestamp = [DateTime]::UtcNow.ToString("o")
    TotalDurationMs = $sw.ElapsedMilliseconds
    AllPassed = ($results | Where-Object { $_.Status -ne "PASS" }).Count -eq 0
    Checks = $results
}

$jsonPath = Join-Path $OutputDir 'concurrency-report.json'
$summary | ConvertTo-Json -Depth 5 | Set-Content -Path $jsonPath -Encoding utf8

Write-Host "`nConcurrency Audit Results:" -ForegroundColor Cyan
foreach ($r in $results) {
    $color = if ($r.Status -eq "PASS") { "Green" } else { "Red" }
    Write-Host "  [$($r.Status)] $($r.Check)" -ForegroundColor $color
}
Write-Host "Duration: $($sw.ElapsedMilliseconds) ms | Assertions: $assertions" -ForegroundColor Gray
Write-Host "Report written to: $jsonPath" -ForegroundColor Gray

if (-not $summary.AllPassed) {
    throw "Concurrency & lock safety audit failed."
}
return $summary
