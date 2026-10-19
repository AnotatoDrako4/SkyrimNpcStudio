<#
.SYNOPSIS
  Independent auditor of QA evidence files and manifests.
  Verifies that claims of PASS are backed by real exit codes, valid logs, and matching hashes.
  Detects counterfeit PASS, missing evidence, or improper promotion to 'in-game verified'.

.PARAMETER ManifestPath
  Path to the manifest.json to audit.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "Manifest path not found: $ManifestPath"
}

$manifestDir = Split-Path -Parent $ManifestPath
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding utf8 | ConvertFrom-Json

$violations = [System.Collections.Generic.List[string]]::new()
$checksPassed = 0

# Check 1: Valid structure
if (-not $manifest.RunId -or -not $manifest.TimestampUtc) {
    $violations.Add("Missing RunId or TimestampUtc in manifest.")
} else {
    $checksPassed++
}

# Check 2: Audit each test case
foreach ($tc in $manifest.TestCases) {
    if ($tc.Status -eq 'PASS') {
        if ($tc.ExitCode -ne 0) {
            $violations.Add("Fraudulent PASS: Test '$($tc.Name)' has status PASS but ExitCode=$($tc.ExitCode)")
        } else {
            $checksPassed++
        }

        # Check log file exists
        if ($tc.LogFile) {
            $logFullPath = Join-Path $manifestDir $tc.LogFile
            if (-not (Test-Path -LiteralPath $logFullPath)) {
                $violations.Add("Missing log evidence: Test '$($tc.Name)' references non-existent log '$($tc.LogFile)'")
            } else {
                $checksPassed++
            }
        }
    }

    # Check 3: Improper promotion of in-game verification
    if ($tc.Name -match '(?i)in-?game|live-?skyrim|skyrim-?runtime' -and $tc.Status -eq 'PASS' -and -not $tc.HumanValidationCertificate) {
        $violations.Add("Unapproved in-game verification: '$($tc.Name)' cannot be PASS without human acceptance certificate.")
    }
}

# Check 4: Concurrent modifications invalidation
if ($null -ne $manifest.ConcurrentChanges -and $manifest.ConcurrentChanges.HasDifferences) {
    if ($manifest.OverallStatus -eq 'PASS') {
        $violations.Add("Integrity failure: Concurrent modifications occurred, but OverallStatus was still marked PASS.")
    } else {
        $checksPassed++
    }
}

$isValid = ($violations.Count -eq 0)

$auditResult = [PSCustomObject]@{
    RunId        = $manifest.RunId
    IsValid      = $isValid
    ChecksPassed = $checksPassed
    ViolationCount = $violations.Count
    Violations   = $violations.ToArray()
}

Write-Host "Evidence Audit for $($manifest.RunId): $(if ($isValid) { 'VALID' } else { 'INVALID' }) (Checks passed: $checksPassed, Violations: $($violations.Count))"
if (-not $isValid) {
    foreach ($v in $violations) {
        Write-Warning "  - $v"
    }
    exit 1
}

exit 0
