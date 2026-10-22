<#
.SYNOPSIS
  Antigravity Regression Runner.
  Executes verified test suites non-destructively against existing builds.
  Fingerprints inputs before and after, logs exit codes, and generates full QA reports.
  NO GIT / NO GITHUB.

.PARAMETER Suite
  Suite to run: 'worker-unit', 'export-tests', 'all-worker', 'antigravity-self-tests'.

.PARAMETER RunId
  Unique identifier for the run.
#>
[CmdletBinding()]
param(
    [ValidateSet('worker-unit', 'export-tests', 'all-worker', 'follower-behavior', 'reference-integration', 'engine-security', 'export-destinations', 'workbench', 'settings', 'portrait-cache', 'concurrency', 'hostile-corpus', 'mock-destinations', 'acceptance-kits', 'ui-workbench', 'ui-settings', 'antigravity-self-tests', 'full-qa-gate')]
    [string]$Suite = 'worker-unit',
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$baselineRunner = Join-Path $PSScriptRoot 'Invoke-SnsQaBaseline.ps1'

if (-not $RunId) {
    $RunId = "regression-" + $Suite + "-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
}

$testDefs = [System.Collections.Generic.List[PSCustomObject]]::new()

# 1. .NET BethesdaWorker Unit Tests
if ($Suite -eq 'worker-unit' -or $Suite -eq 'all-worker' -or $Suite -eq 'full-qa-gate') {
    $testDefs.Add([PSCustomObject]@{
        Name             = 'BethesdaWorker_Unit_Tests'
        Type             = 'Executable'
        Target           = 'dotnet'
        Arguments        = @('test', 'tests\unit\BethesdaWorker.Tests\BethesdaWorker.Tests.csproj', '--no-build')
        ExpectedExitCode = 0
    })
}

# 2. .NET Export Tests (F-13, F-14, F-16, F-17, F-18)
if ($Suite -eq 'export-tests' -or $Suite -eq 'all-worker' -or $Suite -eq 'full-qa-gate') {
    $testDefs.Add([PSCustomObject]@{
        Name             = 'SkyrimNpcStudio_Export_Tests'
        Type             = 'Executable'
        Target           = 'dotnet'
        Arguments        = @('test', 'tests\export\SkyrimNpcStudio.ExportTests\SkyrimNpcStudio.ExportTests.csproj', '--no-build')
        ExpectedExitCode = 0
    })
}

# 2b. .NET Follower Behavior Lab Tests (AG-FOLLOWER-BEHAVIOR-01 & 02: 28 tests)
if ($Suite -eq 'follower-behavior' -or $Suite -eq 'all-worker' -or $Suite -eq 'full-qa-gate') {
    $testDefs.Add([PSCustomObject]@{
        Name             = 'FollowerBehaviorLab_Unit_And_Rpc_Tests'
        Type             = 'Executable'
        Target           = 'dotnet'
        Arguments        = @('test', 'tests\antigravity\follower-behavior-lab\FollowerBehaviorLab.Tests.csproj', '--no-build')
        ExpectedExitCode = 0
    })
}

# 3. C++ Catch2 Engine Security & Audit Tests (F-10, F-14 busy-retry)
if ($Suite -eq 'engine-security' -or $Suite -eq 'full-qa-gate') {
    $engineExe = Join-Path $repoRoot 'build\debug\bin\sns_engine_tests.exe'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Catch2_Engine_Security_Audit_Tests'
        Type             = 'Executable'
        Target           = $engineExe
        Arguments        = @('[audit],[busy],[safe-load],[security]')
        ExpectedExitCode = 0
    })
}

# 4. C++ Catch2 Export Destination Refusal Tests (F-13)
if ($Suite -eq 'export-destinations' -or $Suite -eq 'full-qa-gate') {
    $destExe = Join-Path $repoRoot 'build\debug\bin\sns_export_destination_tests.exe'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Catch2_Export_Destination_Refusal_Tests'
        Type             = 'Executable'
        Target           = $destExe
        Arguments        = @()
        ExpectedExitCode = 0
    })
}

# 5. C++ Catch2 Workbench State Tests (10 cases / 150 assertions)
if ($Suite -eq 'workbench' -or $Suite -eq 'full-qa-gate') {
    $wbExe = Join-Path $repoRoot 'build\debug\bin\sns_workbench_tests.exe'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Catch2_Workbench_State_Tests'
        Type             = 'Executable'
        Target           = $wbExe
        Arguments        = @()
        ExpectedExitCode = 0
    })
}

# 6. C++ Catch2 Settings Center Tests (9 cases / 743 assertions)
if ($Suite -eq 'settings' -or $Suite -eq 'full-qa-gate') {
    $settingsExe = Join-Path $repoRoot 'build\debug\bin\sns_settings_tests.exe'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Catch2_Settings_Center_Tests'
        Type             = 'Executable'
        Target           = $settingsExe
        Arguments        = @()
        ExpectedExitCode = 0
    })
}

# 7. C++ Catch2 Portrait Cache Tests (9 cases / 128 assertions)
if ($Suite -eq 'portrait-cache' -or $Suite -eq 'full-qa-gate') {
    $portraitExe = Join-Path $repoRoot 'build\debug\bin\sns_portrait_cache_tests.exe'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Catch2_Portrait_Cache_Tests'
        Type             = 'Executable'
        Target           = $portraitExe
        Arguments        = @()
        ExpectedExitCode = 0
    })
}

# 7b. C++ Catch2 Reference Integration Tests (Claude A1-A3: 60 cases / 335 assertions)
if ($Suite -eq 'reference-integration' -or $Suite -eq 'full-qa-gate') {
    $refExe = Join-Path $repoRoot 'build\refint\bin\sns_reference_tests.exe'
    if (-not (Test-Path -LiteralPath $refExe)) {
        $refExe = Join-Path $repoRoot 'build\refint-app\bin\sns_reference_tests.exe'
    }
    if (Test-Path -LiteralPath $refExe) {
        $testDefs.Add([PSCustomObject]@{
            Name             = 'Catch2_Reference_Integration_Tests'
            Type             = 'Executable'
            Target           = $refExe
            Arguments        = @()
            ExpectedExitCode = 0
        })
    }
}

# 8. Antigravity Concurrency & Busy-Lock Safety Auditor (AG-F14-LOCK: 6/6 contracts)
if ($Suite -eq 'concurrency' -or $Suite -eq 'full-qa-gate') {
    $concurrencyScript = Join-Path $PSScriptRoot 'Test-SnsBusyConcurrency.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_Concurrency_Lock_Auditor'
        Type             = 'PowerShellScript'
        Target           = $concurrencyScript
        ExpectedExitCode = 0
    })
}

# 9. Hostile NIF Corpus Safety Sandbox (F-10: 238b, bad_length, regex_overflow, facegen)
if ($Suite -eq 'hostile-corpus' -or $Suite -eq 'full-qa-gate') {
    $corpusScript = Join-Path $PSScriptRoot 'Invoke-SnsSafeCorpusTest.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Hostile_Nif_Corpus_Safety_Sandbox'
        Type             = 'PowerShellScript'
        Target           = $corpusScript
        ExpectedExitCode = 0
    })
}

# 10. Synthetic Mock Game & MO2 Destination Auditor (AG-P0-INSTALL / AG-F13-PATHS)
if ($Suite -eq 'mock-destinations' -or $Suite -eq 'full-qa-gate') {
    $mockDestScript = Join-Path $PSScriptRoot 'Test-SnsExportDestinations.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Mock_Game_Destination_Safety_Auditor'
        Type             = 'PowerShellScript'
        Target           = $mockDestScript
        ExpectedExitCode = 0
    })
}

# 11. Acceptance Kits & Human Gate Auditor (AG-MO2-SKYRIM)
if ($Suite -eq 'acceptance-kits' -or $Suite -eq 'full-qa-gate') {
    $kitsScript = Join-Path $PSScriptRoot 'Test-SnsAcceptanceKits.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Acceptance_Kits_Human_Gate_Auditor'
        Type             = 'PowerShellScript'
        Target           = $kitsScript
        ExpectedExitCode = 0
    })
}

# 12. Optional Real-Window UI Automation Suites
if ($Suite -eq 'ui-workbench') {
    $uiWbScript = Join-Path $PSScriptRoot 'Test-SnsUiWorkbench.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_UI_Workbench_Real_Window'
        Type             = 'PowerShellScript'
        Target           = $uiWbScript
        Arguments        = @('-Size', '1366x768')
        ExpectedExitCode = 0
    })
}

if ($Suite -eq 'ui-settings') {
    $uiSetScript = Join-Path $PSScriptRoot 'Test-SnsUiSettings.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_UI_Settings_Real_Window'
        Type             = 'PowerShellScript'
        Target           = $uiSetScript
        Arguments        = @('-Size', '1366x768')
        ExpectedExitCode = 0
    })
}

# 13. Antigravity QA Lab Self-Tests (Manifest, Concurrent Changes, Evidence Auditor, Lock)
if ($Suite -eq 'antigravity-self-tests' -or $Suite -eq 'full-qa-gate') {
    $manifestTest = Join-Path $repoRoot 'tests\antigravity\test_qa_manifest.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_QA_Manifest_Self_Test'
        Type             = 'PowerShellScript'
        Target           = $manifestTest
        ExpectedExitCode = 0
    })

    $concurrentTest = Join-Path $repoRoot 'tests\antigravity\test_concurrent_change.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_QA_Concurrent_Change_Detection'
        Type             = 'PowerShellScript'
        Target           = $concurrentTest
        ExpectedExitCode = 0
    })

    $evidenceTest = Join-Path $repoRoot 'tests\antigravity\test_invalid_evidence.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_QA_Evidence_Auditor'
        Type             = 'PowerShellScript'
        Target           = $evidenceTest
        ExpectedExitCode = 0
    })

    $lockTest = Join-Path $repoRoot 'tests\antigravity\test_build_slot_lock.ps1'
    $testDefs.Add([PSCustomObject]@{
        Name             = 'Antigravity_QA_Build_Slot_Lock'
        Type             = 'PowerShellScript'
        Target           = $lockTest
        ExpectedExitCode = 0
    })
}

$manifest = & $baselineRunner -Suite $Suite -TestDefinitions $testDefs.ToArray() -RunId $RunId
return $manifest
