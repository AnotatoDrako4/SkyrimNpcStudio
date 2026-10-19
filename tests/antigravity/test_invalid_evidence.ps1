<#
.SYNOPSIS
  Self-test suite for evidence auditor (Test-SnsEvidence.ps1).
  Verifies that fraudulent manifests, missing logs, and unauthorized in-game promotions
  are rejected with exit code 1.
#>
[CmdletBinding()]
param()

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$evidenceAuditor = Join-Path $repoRoot 'scripts\antigravity\Test-SnsEvidence.ps1'

$fixtureDir = Join-Path $repoRoot 'out\antigravity\fixtures\evidence_tests'
if (-not (Test-Path -LiteralPath $fixtureDir)) {
    New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
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
Write-Host " Running Antigravity QA Lab Self-Test: Evidence Auditor"
Write-Host "=========================================================="

# Scenario 1: Valid Manifest
$validManifestFile = Join-Path $fixtureDir 'valid_manifest.json'
$dummyLog = Join-Path $fixtureDir 't1.log'
[System.IO.File]::WriteAllText($dummyLog, "Sample log content", [System.Text.Encoding]::UTF8)

$validObj = [ordered]@{
    RunId = "audit-valid-01"
    TimestampUtc = [System.DateTime]::UtcNow.ToString("o")
    OverallStatus = "PASS"
    TestCases = @(
        @{
            Name = "Case1"
            Status = "PASS"
            ExitCode = 0
            LogFile = "t1.log"
        }
    )
}
$validObj | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $validManifestFile -Encoding utf8

$p1 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$evidenceAuditor`" -ManifestPath `"$validManifestFile`"" -Wait -PassThru -NoNewWindow
Assert-Condition ($p1.ExitCode -eq 0) "Evidence auditor accepts valid manifest (ExitCode=0)"

# Scenario 2: Fraudulent PASS with non-zero exit code
$fraudManifestFile = Join-Path $fixtureDir 'fraud_manifest.json'
$fraudObj = [ordered]@{
    RunId = "audit-fraud-01"
    TimestampUtc = [System.DateTime]::UtcNow.ToString("o")
    OverallStatus = "PASS"
    TestCases = @(
        @{
            Name = "FakePassCase"
            Status = "PASS"
            ExitCode = 127
            LogFile = "t1.log"
        }
    )
}
$fraudObj | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $fraudManifestFile -Encoding utf8

$p2 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$evidenceAuditor`" -ManifestPath `"$fraudManifestFile`"" -Wait -PassThru -NoNewWindow
Assert-Condition ($p2.ExitCode -ne 0) "Evidence auditor rejects fraudulent PASS (ExitCode!=0)"

# Scenario 3: Unauthorized in-game claim
$inGameManifestFile = Join-Path $fixtureDir 'ingame_fraud_manifest.json'
$inGameObj = [ordered]@{
    RunId = "audit-ingame-fraud-01"
    TimestampUtc = [System.DateTime]::UtcNow.ToString("o")
    OverallStatus = "PASS"
    TestCases = @(
        @{
            Name = "Skyrim-InGame-Appearance-Verified"
            Status = "PASS"
            ExitCode = 0
            LogFile = "t1.log"
        }
    )
}
$inGameObj | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $inGameManifestFile -Encoding utf8

$p3 = Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$evidenceAuditor`" -ManifestPath `"$inGameManifestFile`"" -Wait -PassThru -NoNewWindow
Assert-Condition ($p3.ExitCode -ne 0) "Evidence auditor rejects uncertified in-game verification (ExitCode!=0)"

# Cleanup
Remove-Item -LiteralPath $fixtureDir -Recurse -Force -ErrorAction SilentlyContinue

if ($assertionsFailed -eq 0) {
    Write-Host "`nAll Evidence Auditor Assertions PASSED successfully."
    exit 0
} else {
    Write-Error "`nEvidence Auditor Test had $assertionsFailed failed assertions."
    exit 1
}
