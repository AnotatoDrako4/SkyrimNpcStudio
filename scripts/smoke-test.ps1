<#
.SYNOPSIS
  Runs the built studio headless (offscreen), optionally against a real MO2 profile, and saves a
  screenshot. Exit code 0 means: QML loaded, the profile was scanned and indexed, the NPC loaded.

.EXAMPLE
  .\scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 0A2C8E:Skyrim.esm
#>
[CmdletBinding()]
param(
    [string]$Preset = 'debug',
    [string]$Instance,
    [string]$MO2Profile,
    [string]$Npc,
    [string]$Game,
    [switch]$VerifyViewport,
    [string]$Screenshot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'out\smoke.png')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
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
if ($VerifyViewport) { $arguments += '--verify-viewport' }

$log = [IO.Path]::ChangeExtension($Screenshot, '.log')
$process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardError $log
if (-not $process.WaitForExit(15 * 60 * 1000)) { $process.Kill(); throw 'Smoke test timed out.' }

$warnings = Select-String -Path $log -Pattern '\.qml:\d+' | Where-Object { $_.Line -notmatch 'propertyCache' }
Get-Content $log | Select-String 'smoke:|screenshot'
if ($warnings) {
    $warnings | Select-Object -First 20 | ForEach-Object { Write-Warning $_.Line }
    throw "QML warnings found ($(@($warnings).Count)); see $log"
}
if ($process.ExitCode -ne 0) { throw "Studio exited with $($process.ExitCode); see $log" }
Write-Host "OK - screenshot: $Screenshot"
