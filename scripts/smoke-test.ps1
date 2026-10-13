<#
.SYNOPSIS
  Runs the built studio headless (offscreen), optionally against a real MO2 profile, and saves a
  screenshot. Exit code 0 means: QML loaded, the profile was scanned and indexed, the NPC loaded.

.EXAMPLE
  .\scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 0A2C8E:Skyrim.esm

.EXAMPLE
  # Viewport checks plus the five fixed-camera captures (face, hair, body, armor, outfit).
  .\scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 0A2C8E:Skyrim.esm -VerifyViewport -CaptureSet out\visual\after
#>
[CmdletBinding()]
param(
    [string]$Preset = 'debug',
    [string]$Instance,
    [string]$MO2Profile,
    [string]$Npc,
    [string]$Game,
    [Nullable[int]]$WorkspaceIndex,
    [string]$Size,
    [string]$StressAppearance,
    [string]$SecondNpc,
    [string]$StressWork,
    [switch]$VerifyViewport,
    [string]$CaptureSet,
    # Show the window instead of starting it hidden. A hidden window is never exposed, so it only
    # renders through grabWindow(); picking needs the real, exposed render path.
    [switch]$ShowWindow,
    [string]$Screenshot = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Screenshot) { $Screenshot = Join-Path $root 'out\smoke.png' }
$exe = Join-Path $root "build\$Preset\bin\SkyrimNpcStudio.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found." }
. (Join-Path $PSScriptRoot 'dev-env.ps1') | Out-Null

New-Item -ItemType Directory -Force (Split-Path -Parent $Screenshot) | Out-Null
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QUICK_BACKEND = 'rhi'
$env:QSG_RHI_BACKEND = 'd3d11'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$env:QT_FORCE_STDERR_LOGGING = '1'

$arguments = @('--smoke-test', '--screenshot', "`"$Screenshot`"")
if ($Instance) { $arguments += @('--instance', "`"$Instance`"") }
if ($MO2Profile) { $arguments += @('--profile', "`"$MO2Profile`"") }
if ($Npc) { $arguments += @('--npc', $Npc) }
if ($Game) { $arguments += @('--game', "`"$Game`"") }
if ($null -ne $WorkspaceIndex) { $arguments += @('--smoke-workspace', [string]$WorkspaceIndex) }
if ($Size) { $arguments += @('--smoke-size', $Size) }
if ($StressAppearance) { $arguments += @('--stress-appearance', $StressAppearance) }
if ($SecondNpc) { $arguments += @('--second-npc', $SecondNpc) }
if ($StressWork) { $arguments += @('--stress-work', "`"$StressWork`"") }
if ($VerifyViewport) { $arguments += '--verify-viewport' }
if ($CaptureSet) {
    New-Item -ItemType Directory -Force $CaptureSet | Out-Null
    $arguments += @('--capture-set', "`"$((Resolve-Path $CaptureSet).Path)`"")
}

$log = [IO.Path]::ChangeExtension($Screenshot, '.log')
$process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle $(if ($ShowWindow) { 'Normal' } else { 'Hidden' }) -PassThru -RedirectStandardError $log
# Touch the handle now: without it .NET may not keep the exit code and ExitCode reads as $null.
$null = $process.Handle
if (-not $process.WaitForExit(15 * 60 * 1000)) { $process.Kill(); throw 'Smoke test timed out.' }
$process.WaitForExit()

# QML warnings and runtime shader compilation errors (custom materials compile at run time).
$warnings = Select-String -Path $log -Pattern '\.qml:\d+|Failed to compile|QSpirvCompiler|Failed to build graphics pipeline' |
    Where-Object { $_.Line -notmatch 'propertyCache' }
Get-Content $log | Select-String 'smoke:|screenshot|viewport:|capture:'
if ($warnings) {
    $warnings | Select-Object -First 20 | ForEach-Object { Write-Warning $_.Line }
    throw "QML or shader warnings found ($(@($warnings).Count)); see $log"
}
if ($null -eq $process.ExitCode) { throw "Studio exit code unavailable; see $log" }
if ($process.ExitCode -ne 0) { throw "Studio exited with $($process.ExitCode); see $log" }
Write-Host "OK - screenshot: $Screenshot"
