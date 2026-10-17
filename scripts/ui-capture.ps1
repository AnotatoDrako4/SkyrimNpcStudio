<#
.SYNOPSIS
  Captures the three workspaces at several window sizes (hidden window, real profile) with the
  preferences isolated in an INI file, so the user's registry is never touched.

.EXAMPLE
  .\scripts\ui-capture.ps1 -Label before
  .\scripts\ui-capture.ps1 -Label after -Sizes 1100x700,1920x1080 -Workspaces 0 -Seed out\ui\seed-large.ini
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Label,
    [string[]]$Sizes = @('1100x700', '1366x768', '1600x900', '1920x1080', '2560x1440'),
    [int[]]$Workspaces = @(0, 1, 2),
    [string]$Instance = 'D:\Apps\ModOrganizer.ini',
    [string]$MO2Profile = 'Default',
    [string]$Npc = '0A2C8E:Skyrim.esm',
    # INI copied into the isolated settings folder before each run (to start from a given layout/scale).
    [string]$Seed,
    # Keep the settings folder of the run (to be reused by a second run: restore after restart).
    [string]$SettingsDir,
    [string]$Preset = 'debug',
    # Run this executable instead of build\<preset>\bin (e.g. a copy of the build to compare against).
    [string]$Binary
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "out\ui\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$names = @('creator', 'outfit', 'forge')

foreach ($size in $Sizes) {
    foreach ($ws in $Workspaces) {
        $settings = if ($SettingsDir) { $SettingsDir } else { Join-Path $out "settings-$($names[$ws])-$size" }
        if (-not $SettingsDir -and (Test-Path $settings)) { Remove-Item -Recurse -Force $settings }
        New-Item -ItemType Directory -Force $settings | Out-Null
        if ($Seed) {
            $target = Join-Path $settings 'SkyrimNpcStudio\SkyrimNpcStudio.ini'
            New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $Seed -Destination $target -Force
        }
        $env:SNS_SETTINGS_DIR = $settings
        $png = Join-Path $out "$($names[$ws])-$size.png"
        Write-Host "== $($names[$ws]) $size"
        try {
            $extra = @{}
            if ($Binary) { $extra.Binary = $Binary }
            & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Preset $Preset -Instance $Instance -MO2Profile $MO2Profile -Npc $Npc `
                -WorkspaceIndex $ws -Size $size -Screenshot $png @extra
        } catch {
            Write-Warning "$($names[$ws]) $size failed: $($_.Exception.Message)"
        }
    }
}
Remove-Item Env:\SNS_SETTINGS_DIR -ErrorAction SilentlyContinue
Write-Host "Captures in $out"
