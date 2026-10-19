<#
.SYNOPSIS
  Audit F-11 on a profile: builds a SYNTHETIC portable MO2 instance whose only defect is a damaged FaceGen
  (tests/tools/synthetic_mo2.cpp), runs the studio's `damaged-facegen-scan` scenario on it and deletes the
  instance. It never reads or writes a real game folder, MO2 instance or profile.

.DESCRIPTION
  The instance lives in %TEMP% (never in the source tree) and holds the synthetic load order of FixtureGen
  plus Lydia's FaceGen replaced by the audit's facegen_lengtherror.nif (72 bytes, SHA-256 checked below).
  Its file inventory with hashes is kept in -Out before it is deleted. Exit code: the smoke test's (0 = passed).

.EXAMPLE
  .\scripts\damaged-facegen-check.ps1 -Out out\local-implementation\20261019-f11-harness\scenario
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$Preset = 'debug',
    # The studio opens this NPC first; the damaged one (Lydia) is only read by the scan.
    [string]$StartNpc = '01367C:Skyrim.esm',
    [switch]$KeepInstance
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# FixtureGen (run by sns_synthetic_mo2) is framework-dependent: it needs the .NET 10 SDK from dev-env.
. (Join-Path $PSScriptRoot 'dev-env.ps1') *> $null
$tool = Join-Path $root "build\$Preset\bin\sns_synthetic_mo2.exe"
if (-not (Test-Path $tool)) { throw "Build it first: cmake --build build\$Preset --target sns_synthetic_mo2 SkyrimNpcStudio" }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

$instance = Join-Path ([IO.Path]::GetTempPath()) ('sns-synthetic-mo2-' + [guid]::NewGuid().ToString('N'))
$code = 1
try {
    $summary = & $tool $instance
    if ($LASTEXITCODE -ne 0) { throw "sns_synthetic_mo2 failed (exit $LASTEXITCODE)" }
    $summary | Set-Content (Join-Path $Out 'synthetic-instance.json') -Encoding utf8
    $info = ($summary | Out-String) | ConvertFrom-Json
    # The damaged FaceGen must be exactly the audit's file (SHA-256 recorded in tests/support/audit_repro_inputs.hpp).
    $expected = '6FA1FE24F93CB103B31111AB029D3F1B1480A274F1EDA3E661005D6BCB83D960'
    $actual = (Get-FileHash $info.faceGen -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "unexpected FaceGen bytes: $actual" }
    Get-ChildItem $instance -Recurse -File |
        ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.FullName.Substring($instance.Length + 1) } |
        Set-Content (Join-Path $Out 'synthetic-instance-files.txt') -Encoding utf8

    $env:SNS_DAMAGED_FACEGEN_NPC = $info.damagedNpc
    & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Preset $Preset -Instance $info.instance -MO2Profile $info.profile -Npc $StartNpc `
        -StressAppearance damaged-facegen-scan -StressWork $Out -Screenshot (Join-Path $Out 'smoke.png')
    $code = $LASTEXITCODE
} finally {
    Remove-Item Env:\SNS_DAMAGED_FACEGEN_NPC -ErrorAction SilentlyContinue
    if ($KeepInstance) { Write-Host "Synthetic instance kept: $instance" }
    elseif ($instance.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path $instance)) { Remove-Item -Recurse -Force $instance }
}
exit $code
