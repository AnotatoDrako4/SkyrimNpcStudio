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
    # Character Forge 3D preview check: models loaded in order (validated ones must render, rejected
    # ones must show an error). Captures go next to the screenshot.
    [string[]]$ForgePreview,
    [string]$CaptureSet,
    # Show the window instead of starting it hidden. A hidden window is never exposed, so it only
    # renders through grabWindow(); picking needs the real, exposed render path.
    [switch]$ShowWindow,
    [string]$Binary,
    [string]$Screenshot = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Screenshot) { $Screenshot = Join-Path $root 'out\smoke.png' }
$exe = if ($Binary) { (Resolve-Path $Binary).Path } else { Join-Path $root "build\$Preset\bin\SkyrimNpcStudio.exe" }
if (-not (Test-Path $exe)) { throw "Binary not found: $exe" }
. (Join-Path $PSScriptRoot 'dev-env.ps1') | Out-Null

New-Item -ItemType Directory -Force (Split-Path -Parent $Screenshot) | Out-Null
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QUICK_BACKEND = 'rhi'
$env:QSG_RHI_BACKEND = 'd3d11'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$env:QT_FORCE_STDERR_LOGGING = '1'
# Stress runs never read or write the user's review decisions (D1): the studio keeps them in a new,
# temporary book next to the stress output unless SNS_REVIEW_DIR is already set.
$temporaryReviewDir = $false
if ($StressAppearance -and -not $env:SNS_REVIEW_DIR) {
    $temporaryReviewDir = $true
    $reviewBase = if ($StressWork) { $StressWork } else { Split-Path -Parent $Screenshot }
    $env:SNS_REVIEW_DIR = Join-Path $reviewBase ('review-book-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmssfff'))
    New-Item -ItemType Directory -Force $env:SNS_REVIEW_DIR | Out-Null
    Write-Host "Review decisions of this run: $env:SNS_REVIEW_DIR"
}
# Gallery portraits of stress runs go to a new folder next to the stress output (never the user's cache),
# unless SNS_PORTRAIT_CACHE_DIR is already set (a second run reuses a first run's folder on purpose).
$temporaryPortraitDir = $false
if ($StressAppearance -and -not $env:SNS_PORTRAIT_CACHE_DIR) {
    $temporaryPortraitDir = $true
    $portraitBase = if ($StressWork) { $StressWork } else { Split-Path -Parent $Screenshot }
    $env:SNS_PORTRAIT_CACHE_DIR = Join-Path $portraitBase ('portraits-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmssfff'))
    New-Item -ItemType Directory -Force $env:SNS_PORTRAIT_CACHE_DIR | Out-Null
    Write-Host "Portraits of this run: $env:SNS_PORTRAIT_CACHE_DIR"
}

$arguments = @('--smoke-test', '--screenshot', "`"$Screenshot`"")
if ($Instance) { $arguments += @('--instance', "`"$Instance`"") }
if ($MO2Profile) { $arguments += @('--profile', "`"$MO2Profile`"") }
if ($Npc) { $arguments += @('--npc', "`"$Npc`"") }
if ($Game) { $arguments += @('--game', "`"$Game`"") }
if ($null -ne $WorkspaceIndex) { $arguments += @('--smoke-workspace', [string]$WorkspaceIndex) }
if ($Size) { $arguments += @('--smoke-size', $Size) }
if ($StressAppearance) { $arguments += @('--stress-appearance', $StressAppearance) }
if ($SecondNpc) { $arguments += @('--second-npc', "`"$SecondNpc`"") }
if ($StressWork) { $arguments += @('--stress-work', "`"$StressWork`"") }
if ($VerifyViewport) { $arguments += '--verify-viewport' }
if ($ForgePreview) {
    $models = ($ForgePreview | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }) -join ';'
    $forgeCaptures = Join-Path (Split-Path -Parent $Screenshot) 'forge-preview'
    $arguments += @('--forge-preview', "`"$models`"", '--forge-capture', "`"$forgeCaptures`"")
}
if ($CaptureSet) {
    New-Item -ItemType Directory -Force $CaptureSet | Out-Null
    $arguments += @('--capture-set', "`"$((Resolve-Path $CaptureSet).Path)`"")
}

$log = [IO.Path]::ChangeExtension($Screenshot, '.log')
$process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle $(if ($ShowWindow) { 'Normal' } else { 'Hidden' }) -PassThru -RedirectStandardError $log
# The studio inherited the variable; a later run in this shell must not reuse this run's decisions.
if ($temporaryReviewDir) { $env:SNS_REVIEW_DIR = $null }
if ($temporaryPortraitDir) { $env:SNS_PORTRAIT_CACHE_DIR = $null }
# Touch the handle now: without it .NET may not keep the exit code and ExitCode reads as $null.
$null = $process.Handle
if (-not $process.WaitForExit(15 * 60 * 1000)) { $process.Kill(); throw 'Smoke test timed out.' }
$process.WaitForExit()

# QML warnings and runtime shader compilation errors (custom materials compile at run time).
$warnings = Select-String -Path $log -Pattern '\.qml:\d+|Failed to compile|QSpirvCompiler|Failed to build graphics pipeline' |
    Where-Object { $_.Line -notmatch 'propertyCache' }
Get-Content $log | Select-String 'smoke:|screenshot|viewport:|capture:|forge:'
if ($warnings) {
    $warnings | Select-Object -First 20 | ForEach-Object { Write-Warning $_.Line }
    throw "QML or shader warnings found ($(@($warnings).Count)); see $log"
}
if ($null -eq $process.ExitCode) { throw "Studio exit code unavailable; see $log" }
if ($process.ExitCode -ne 0) { throw "Studio exited with $($process.ExitCode); see $log" }
Write-Host "OK - screenshot: $Screenshot"
