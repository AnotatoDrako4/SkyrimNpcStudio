<#
.SYNOPSIS
  Self-test suite for atomic build slot locking (Request-SnsBuildSlot.ps1 and Release-SnsBuildSlot.ps1).
  Verifies race-free lock acquisition, rejection on contention, and proper release.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$reqScript = Join-Path $repoRoot 'scripts\antigravity\Request-SnsBuildSlot.ps1'
$relScript = Join-Path $repoRoot 'scripts\antigravity\Release-SnsBuildSlot.ps1'
$testLockFile = Join-Path $repoRoot '.ai-coordination\locks\test-slot.lock'

# Ensure clean starting state
if (Test-Path -LiteralPath $testLockFile) {
    Remove-Item -LiteralPath $testLockFile -Force -ErrorAction SilentlyContinue
}

$assertionsFailed = 0
function Assert-Condition([bool]$cond, [string]$msg) {
    if ($cond) {
        Write-Host "  [OK] $msg"
    } else {
        Write-Error "  [ASSERTION FAILED] $msg"
        $script:assertionsFailed++
    }
}

Write-Host "=========================================================="
Write-Host " Running Antigravity QA Lab Self-Test: Build Slot Locking"
Write-Host "=========================================================="

try {
    # 1. Acquire initial lock
    $p1 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$reqScript`" -LockFile `"$testLockFile`" -Reason `"Test slot initial`" -TimeoutSec 0" -Wait -PassThru -NoNewWindow
    Assert-Condition ($p1.ExitCode -eq 0) "Initial lock acquisition succeeds (ExitCode=0)"
    Assert-Condition (Test-Path -LiteralPath $testLockFile) "Lock file was created on disk"

    # 2. Contention: Attempt to acquire while locked
    $p2 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$reqScript`" -LockFile `"$testLockFile`" -Reason `"Test slot concurrent`" -TimeoutSec 0" -Wait -PassThru -NoNewWindow
    Assert-Condition ($p2.ExitCode -ne 0) "Contention detected: second acquisition rejected (ExitCode!=0)"

    # 3. Release lock
    $p3 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$relScript`" -LockFile `"$testLockFile`"" -Wait -PassThru -NoNewWindow
    Assert-Condition ($p3.ExitCode -eq 0) "Lock release succeeds (ExitCode=0)"
    Assert-Condition (-not (Test-Path -LiteralPath $testLockFile)) "Lock file deleted from disk"

    # 4. Re-acquire lock after release
    $p4 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$reqScript`" -LockFile `"$testLockFile`" -Reason `"Test slot reacquire`" -TimeoutSec 0" -Wait -PassThru -NoNewWindow
    Assert-Condition ($p4.ExitCode -eq 0) "Re-acquisition after release succeeds (ExitCode=0)"

    # Final cleanup
    & $relScript -LockFile $testLockFile | Out-Null
} finally {
    if (Test-Path -LiteralPath $testLockFile) {
        Remove-Item -LiteralPath $testLockFile -Force -ErrorAction SilentlyContinue
    }
}

if ($assertionsFailed -eq 0) {
    Write-Host "`nAll Build Slot Locking Assertions PASSED successfully."
    exit 0
} else {
    Write-Error "`nBuild Slot Locking Test had $assertionsFailed failed assertions."
    exit 1
}
