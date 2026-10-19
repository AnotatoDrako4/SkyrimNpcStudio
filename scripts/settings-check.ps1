<#
.SYNOPSIS
  Settings Center check on the real window (real keyboard/mouse input), then a restart that must find the stored
  settings again. Preferences live in an INI folder of the run and portraits in a cache folder of the run: the
  user's registry, preferences and portrait cache are never touched. No Git.

.EXAMPLE
  .\scripts\settings-check.ps1 -Size 1366x768
#>
[CmdletBinding()]
param(
    [string]$Size = '1366x768',
    [string]$Instance = 'D:\Apps\ModOrganizer.ini',
    [string]$MO2Profile = 'Default',
    [string]$Npc = '002B6C:Dawnguard.esm',   # Serana
    [string]$Preset = 'debug',
    [string]$OutRoot,
    [string]$Label = "settings-$Size"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'dev-env.ps1') | Out-Null   # Qt runtime on PATH
$exe = Join-Path $root "build\$Preset\bin\SkyrimNpcStudio.exe"
if (-not $OutRoot) { $OutRoot = Join-Path $root 'out\ui' }
$out = Join-Path $OutRoot $Label
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
$settings = Join-Path $out 'settings'
New-Item -ItemType Directory -Force $settings | Out-Null
$env:SNS_SETTINGS_DIR = $settings
$env:SNS_PORTRAIT_CACHE_DIR = Join-Path $out 'portraits'

function Invoke-Run([string]$name, [string[]]$extra) {
    $log = Join-Path $out "$name.log"
    $arguments = @('--instance', "`"$Instance`"", '--profile', "`"$MO2Profile`"", '--npc', "`"$Npc`"",
                   '--smoke-size', $Size, '--screenshot', "`"$(Join-Path $out "$name-final.png")`"") + $extra
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Normal -PassThru -RedirectStandardError $log
    $p.WaitForExit()
    Get-Content $log | Where-Object { $_ -match '^(settings|smoke|screenshot):' } | Out-Host
    Write-Host "== $name exit code $($p.ExitCode)"
    return $p.ExitCode
}

try {
    $first = Invoke-Run 'run1' @('--settings-check', "`"$(Join-Path $out 'run1')`"")
    $second = Invoke-Run 'restart' @('--settings-check', "`"$(Join-Path $out 'restart')`"",
                                     '--settings-expect', "`"$(Join-Path $out 'run1\expect.json')`"")
} finally {
    Remove-Item Env:\SNS_SETTINGS_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\SNS_PORTRAIT_CACHE_DIR -ErrorAction SilentlyContinue
}
Get-ChildItem -Recurse $settings -Filter *.ini | ForEach-Object { "---- $($_.Name)"; Get-Content $_.FullName } | Out-File -Encoding utf8 (Join-Path $out 'settings-ini.txt')
if ($first -ne 0 -or $second -ne 0) { throw "settings check failed (run1=$first, restart=$second)" }
Write-Host "Settings check passed; captures in $out"
