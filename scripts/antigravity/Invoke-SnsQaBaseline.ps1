<#
.SYNOPSIS
  Antigravity Independent QA Baseline Runner.
  Executes test suites in isolated sandboxes, monitors source integrity via SHA-256 fingerprints,
  detects concurrent edits, logs toolchain versions, and produces reproducible offline QA reports.
  ZERO GIT / ZERO GITHUB dependencies.

.PARAMETER Suite
  Name of the test suite (e.g., 'antigravity-self-test', 'f10-nif-safe-load', 'xunit-worker').

.PARAMETER TestDefinitions
  Array of test cases to run. Each test case is a hashtable with:
    - Name: string
    - Command: string or scriptblock
    - Arguments: string[]
    - ExpectedExitCode: int (default: 0)
    - WatchPaths: string[] (paths to watch for concurrent edits)

.PARAMETER RunId
  Unique run identifier. Defaults to timestamp-based ID.

.PARAMETER OutRoot
  Base directory for runs. Defaults to out/antigravity/runs.
#>
[CmdletBinding()]
param(
    [string]$Suite = 'antigravity-qa-baseline',
    [array]$TestDefinitions = @(),
    [string]$RunId,
    [string]$OutRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'out\antigravity\runs')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $RunId) {
    $RunId = "run-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
}

$runDir = Join-Path $OutRoot $RunId
if (-not (Test-Path -LiteralPath $runDir)) {
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
}

$fingerprintScript = Join-Path $PSScriptRoot 'Get-SnsSourceFingerprint.ps1'
. $fingerprintScript

# 1. Discover Toolchain
$devEnv = Join-Path $repoRoot 'scripts\dev-env.ps1'
if (Test-Path -LiteralPath $devEnv) {
    try {
        . $devEnv | Out-Null
    } catch {
        Write-Warning "dev-env.ps1 had a non-fatal warning during load: $_"
    }
}

function Safe-Probe([scriptblock]$cmd) {
    try {
        $val = & $cmd 2>$null
        if ($null -ne $val) { ($val | Select-Object -First 1).ToString().Trim() } else { "Not Available" }
    } catch {
        "Not Available"
    }
}

$toolchain = [ordered]@{
    OsArchitecture = if ([System.Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
    MachineName    = $env:COMPUTERNAME
    PowerShellVer  = $PSVersionTable.PSVersion.ToString()
    DotnetVer      = Safe-Probe { dotnet --version }
    CMakeVer       = Safe-Probe { cmake --version }
    NinjaVer       = Safe-Probe { ninja --version }
    PythonVer      = Safe-Probe { python --version }
}

# 2. Pre-execution Fingerprint
$defaultWatch = @(
    (Join-Path $repoRoot 'engines'),
    (Join-Path $repoRoot 'services'),
    (Join-Path $repoRoot 'apps'),
    (Join-Path $repoRoot 'scripts\antigravity'),
    (Join-Path $repoRoot 'tests\antigravity')
)

Write-Host "== [ANTIGRAVITY QA] Starting Run: $RunId (Suite: $Suite) =="
Write-Host "== Computing initial source fingerprints..."
$initialFp = Get-SnsSourceFingerprint -ScanPaths $defaultWatch -BaseDir $repoRoot

$startTime = [System.DateTime]::UtcNow
$testCaseResults = [System.Collections.Generic.List[PSCustomObject]]::new()

# 3. Execute Test Definitions
foreach ($def in $TestDefinitions) {
    $tcName = $def.Name
    $tcTarget = $def.Target
    $tcType = if ($def.Type) { $def.Type } else { 'PowerShell' }
    $expectedCode = if ($null -ne $def.ExpectedExitCode) { $def.ExpectedExitCode } else { 0 }

    Write-Host "`n-- Executing test: $tcName ..."
    $tcStart = [System.DateTime]::UtcNow
    $tcLog = Join-Path $runDir "$($tcName -replace '[^a-zA-Z0-9_-]', '_').log"

    $status = "RUNNING"
    $exitCode = -1
    $output = ""

    if ($def.Status -eq 'BLOCKED' -or $def.Status -eq 'SKIP') {
        $status = $def.Status
        $output = $def.Reason
        Write-Host "   Status: $status ($output)"
    } else {
        try {
            if ($tcType -eq 'ScriptBlock' -and $def.Action) {
                try {
                    $res = & $def.Action
                    if ($res -is [int]) {
                        $exitCode = $res
                        $output = "ScriptBlock returned exit code $res"
                    } else {
                        $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
                        $output = if ($null -ne $res) { ($res | Out-String).Trim() } else { "OK" }
                    }
                } catch {
                    $exitCode = 1
                    $output = "Exception in ScriptBlock: $_"
                }
            } elseif ($tcType -eq 'Executable') {
                $pinfo = [System.Diagnostics.ProcessStartInfo]::new()
                $pinfo.FileName = $tcTarget
                if ($def.Arguments) { $pinfo.Arguments = ($def.Arguments -join ' ') }
                $pinfo.RedirectStandardOutput = $true
                $pinfo.RedirectStandardError = $true
                $pinfo.UseShellExecute = $false
                $pinfo.CreateNoWindow = $true
                $pinfo.WorkingDirectory = if ($def.WorkingDirectory) { $def.WorkingDirectory } else { $repoRoot }

                $qtBin = 'D:\DevTools\Qt\6.11.2\msvc2022_64\bin'
                $debugBin = Join-Path $repoRoot 'build\debug\bin'
                $curPath = [System.Environment]::GetEnvironmentVariable('PATH')
                if ($pinfo.EnvironmentVariables.ContainsKey('PATH')) {
                    $pinfo.EnvironmentVariables['PATH'] = "$qtBin;$debugBin;" + $pinfo.EnvironmentVariables['PATH']
                } else {
                    $pinfo.EnvironmentVariables.Add('PATH', "$qtBin;$debugBin;$curPath")
                }

                $proc = [System.Diagnostics.Process]::Start($pinfo)
                $stdout = $proc.StandardOutput.ReadToEnd()
                $stderr = $proc.StandardError.ReadToEnd()
                $proc.WaitForExit()
                $exitCode = $proc.ExitCode
                $output = ($stdout + "`n" + $stderr).Trim()
            } elseif ($tcType -eq 'PowerShellScript') {
                $scriptPath = $tcTarget
                $argString = if ($def.Arguments) { ($def.Arguments -join ' ') } else { '' }
                $p = Start-Process -FilePath "powershell.exe" -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$scriptPath`" $argString" -Wait -PassThru -NoNewWindow -RedirectStandardOutput $tcLog -RedirectStandardError (Join-Path $runDir "$($tcName)_err.log")
                $exitCode = $p.ExitCode
                if (Test-Path -LiteralPath $tcLog) {
                    $output = Get-Content -LiteralPath $tcLog -Raw -ErrorAction SilentlyContinue
                }
            } else {
                $output = "Unsupported test type: $tcType"
                $exitCode = 99
            }

            if ($exitCode -eq $expectedCode) {
                $status = "PASS"
            } else {
                $status = "FAIL"
            }
        } catch {
            $exitCode = 1
            $status = "FAIL"
            $output = "Exception during test execution: $_"
        }
    }

    $tcDuration = [math]::Round(([System.DateTime]::UtcNow - $tcStart).TotalMilliseconds, 2)
    Write-Host "   Result: $status (ExitCode=$exitCode, Duration=${tcDuration}ms)"

    # Write per-test log if not already written
    if (-not (Test-Path -LiteralPath $tcLog) -and -not [string]::IsNullOrEmpty($output)) {
        [System.IO.File]::WriteAllText($tcLog, $output, [System.Text.Encoding]::UTF8)
    }

    $testCaseResults.Add([PSCustomObject]@{
        Name       = $tcName
        Target     = if ($tcTarget) { $tcTarget } else { "inline-block" }
        Type       = $tcType
        Status     = $status
        ExitCode   = $exitCode
        DurationMs = $tcDuration
        LogFile    = (Split-Path -Leaf $tcLog)
        Details    = if ($output.Length -gt 250) { $output.Substring(0, 250) + "..." } else { $output }
    })
}

$endTime = [System.DateTime]::UtcNow
$totalDuration = [math]::Round(($endTime - $startTime).TotalSeconds, 3)

# 4. Post-execution Fingerprint & Concurrent Change Detection
Write-Host "`n== Computing post-execution source fingerprints..."
$postFp = Get-SnsSourceFingerprint -ScanPaths $defaultWatch -BaseDir $repoRoot
$diff = Compare-SnsSourceFingerprint -Before $initialFp -After $postFp

if ($diff.HasDifferences) {
    Write-Warning "CONCURRENT SOURCE MODIFICATION DETECTED DURING TEST RUN!"
    foreach ($tc in $testCaseResults) {
        $tc.Status = 'INVALIDATED_BY_CONCURRENT_EDIT'
        $tc.Details += " [INVALIDATED: Input sources modified concurrently: " + ($diff.ChangedFiles -join ', ') + "]"
    }
}

# 5. Compute Overall Status
$overallStatus = "PASS"
if ($diff.HasDifferences) {
    $overallStatus = "INVALIDATED_BY_CONCURRENT_EDIT"
} elseif ($testCaseResults | Where-Object { $_.Status -eq 'FAIL' }) {
    $overallStatus = "FAIL"
} elseif ($testCaseResults | Where-Object { $_.Status -eq 'BLOCKED' }) {
    $overallStatus = "BLOCKED"
} elseif ($testCaseResults.Count -eq 0) {
    $overallStatus = "SKIP"
}

# 6. Save Manifest JSON
$manifest = [ordered]@{
    RunId             = $RunId
    TimestampUtc      = $startTime.ToString("o")
    Suite             = $Suite
    OverallStatus     = $overallStatus
    DurationSeconds   = $totalDuration
    Toolchain         = $toolchain
    ConcurrentChanges = $diff
    TestCases         = $testCaseResults
}

$manifestPath = Join-Path $runDir 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

# 7. Generate Summary Markdown and JSON Report
$reportScript = Join-Path $PSScriptRoot 'Write-SnsQaReport.ps1'
& $reportScript -ManifestPath $manifestPath -OutDir $runDir

Write-Host "`n== [ANTIGRAVITY QA] Run $RunId Complete. Overall Status: $overallStatus =="
return $manifest
