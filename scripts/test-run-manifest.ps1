<#
.SYNOPSIS
  Test run manifest (D3/D22): records which build, MO2 profile, Skyrim version and artifacts a test run
  used and what its automatic checks found, or verifies such a manifest later.

  The in-game result stays WAITING_HUMAN: only a person records it, with evidence files, through
  Add-SnsHumanEvidence (scripts\TestRunManifest.psm1). Git is not queried. The MO2 instance, the profile
  and Skyrim are only read.

.EXAMPLE
  .\scripts\test-run-manifest.ps1 -RunDir D:\runs\bolli -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default `
      -SmokeLog D:\runs\bolli\smoke.log -SmokeExitCode 0

.EXAMPLE
  # Is this evidence still about the current build, profile and files? Exit code 1 when it is not.
  .\scripts\test-run-manifest.ps1 -Verify D:\runs\bolli\test-run-manifest.json
#>
[CmdletBinding(DefaultParameterSetName = 'Create')]
param(
    [Parameter(ParameterSetName = 'Create', Mandatory = $true)][string]$RunDir,
    [Parameter(ParameterSetName = 'Create')][string]$Instance,
    [Parameter(ParameterSetName = 'Create')][string]$MO2Profile,
    [Parameter(ParameterSetName = 'Create')][string[]]$Npc = @(),
    [Parameter(ParameterSetName = 'Create')][string[]]$Scenario = @(),
    [Parameter(ParameterSetName = 'Create')][string]$SmokeLog,
    [Parameter(ParameterSetName = 'Create')][Nullable[int]]$SmokeExitCode,
    [Parameter(ParameterSetName = 'Create')][string]$Notes,
    [Parameter(ParameterSetName = 'Create')][string]$Preset = 'debug',
    [Parameter(ParameterSetName = 'Verify', Mandatory = $true)][string]$Verify,
    [Parameter(ParameterSetName = 'Verify')][string[]]$CurrentBuildFiles = @()
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TestRunManifest.psm1') -Force

if ($PSCmdlet.ParameterSetName -eq 'Verify') {
    $result = Test-SnsTestRunManifest -Path $Verify -CurrentBuildFiles $CurrentBuildFiles
    if ($result.Valid) {
        Write-Host "VALID: $Verify"
        exit 0
    }
    Write-Host "INVALID: $Verify"
    $result.Problems | ForEach-Object { Write-Host "  $_" }
    exit 1
}

$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\$Preset\bin"
$buildFiles = @((Join-Path $bin 'SkyrimNpcStudio.exe'), (Join-Path $bin 'worker\SkyrimNpcStudio.BethesdaWorker.dll'))
$created = New-SnsTestRunManifest -RunDir $RunDir -MO2Instance $Instance -MO2Profile $MO2Profile -Npc $Npc -Scenario $Scenario `
    -SmokeLog $SmokeLog -SmokeExitCode $SmokeExitCode -BuildFiles $buildFiles -Notes $Notes
$m = $created.Manifest
Write-Host "test run manifest: $($created.Path)"
Write-Host "sha256: $($created.Sha256)"
Write-Host ("results: static={0} windows={1} inGame={2}; {3} artifact(s), {4} export(s); Skyrim {5}" -f
    $m.results.static, $m.results.windows, $m.results.inGame, @($m.artifacts).Count, @($m.exports).Count,
    $(if ($m.skyrim.detected) { $m.skyrim.fileVersion } else { 'not detected' }))
if (@($m.problems).Count -gt 0) {
    Write-Host 'problems:'
    $m.problems | ForEach-Object { Write-Host "  $_" }
    exit 1
}
exit 0
