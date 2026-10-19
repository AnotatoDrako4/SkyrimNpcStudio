<#
.SYNOPSIS
  Workbench layout check on the real window (real mouse/keyboard input), then a restart that must find
  the stored layout again. Preferences live in an INI folder of the run, never in the user's registry.

.EXAMPLE
  .\scripts\workbench-check.ps1 -Size 1366x768
#>
[CmdletBinding()]
param(
    [string]$Size = '1366x768',
    [string]$Instance = 'D:\Apps\ModOrganizer.ini',
    [string]$MO2Profile = 'Default',
    [string]$Npc = '002B6C:Dawnguard.esm',   # Serana
    [string]$Preset = 'debug',
    # Windows display scale to emulate for this app only (Qt's own High-DPI path): 1.25, 1.5, 2.
    [string]$DisplayScale,
    [string]$Label = "workbench-$Size$(if ($DisplayScale) { "-dpi$DisplayScale" })"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'dev-env.ps1') | Out-Null   # Qt runtime on PATH
$exe = Join-Path $root "build\$Preset\bin\SkyrimNpcStudio.exe"
$out = Join-Path $root "out\ui\$Label"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
$settings = Join-Path $out 'settings'
New-Item -ItemType Directory -Force $settings | Out-Null
$env:SNS_SETTINGS_DIR = $settings
$previousScale = $env:QT_SCALE_FACTOR
if ($DisplayScale) { $env:QT_SCALE_FACTOR = $DisplayScale }

function Invoke-Run([string]$name, [string[]]$extra) {
    $log = Join-Path $out "$name.log"
    $arguments = @('--instance', "`"$Instance`"", '--profile', "`"$MO2Profile`"", '--npc', "`"$Npc`"",
                   '--smoke-size', $Size, '--screenshot', "`"$(Join-Path $out "$name-final.png")`"") + $extra
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Normal -PassThru -RedirectStandardError $log
    $p.WaitForExit()
    Get-Content $log | Where-Object { $_ -match '^(workbench|smoke|screenshot):' } | Out-Host
    Write-Host "== $name exit code $($p.ExitCode)"
    return $p.ExitCode
}

try {
    $first = Invoke-Run 'run1' @('--workbench-check', "`"$(Join-Path $out 'run1')`"")
    $second = Invoke-Run 'restart' @('--workbench-check', "`"$(Join-Path $out 'restart')`"",
                                     '--workbench-expect', "`"$(Join-Path $out 'run1\expect.json')`"")
} finally {
    Remove-Item Env:\SNS_SETTINGS_DIR -ErrorAction SilentlyContinue
    $env:QT_SCALE_FACTOR = $previousScale
}
if ($first -ne 0 -or $second -ne 0) { throw "workbench check failed (run1=$first, restart=$second)" }
Write-Host "Workbench check passed; captures in $out"
