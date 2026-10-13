<#
.SYNOPSIS
  Registers Windows Error Reporting "LocalDumps" for SkyrimNpcStudio.exe so that crashes no
  in-process handler can see (heap corruption 0xC0000374, fail-fast 0xC0000409, stack overflow)
  also leave a minidump. Needs an elevated PowerShell (HKLM). The studio's own handler already
  writes a report + minidump for access violations, std::terminate, abort() and CRT errors.

.EXAMPLE
  # From an administrator PowerShell:
  .\scripts\enable-crash-dumps.ps1
  .\scripts\enable-crash-dumps.ps1 -Full       # full memory dumps (large)
  .\scripts\enable-crash-dumps.ps1 -Disable    # remove the setting again
#>
[CmdletBinding()]
param(
    [switch]$Full,
    [switch]$Disable,
    [int]$Count = 10
)

$ErrorActionPreference = 'Stop'
$key = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\SkyrimNpcStudio.exe'
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an administrator PowerShell (it writes under HKLM).'
}

if ($Disable) {
    if (Test-Path $key) { Remove-Item $key -Recurse }
    Write-Host 'WER LocalDumps for SkyrimNpcStudio.exe removed.'
    return
}

# %LOCALAPPDATA% is expanded by WER for the user whose process crashed.
$folder = '%LOCALAPPDATA%\SkyrimNpcStudio\SkyrimNpcStudio\diagnostics\wer-dumps'
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name DumpFolder -PropertyType ExpandString -Value $folder -Force | Out-Null
New-ItemProperty -Path $key -Name DumpType -PropertyType DWord -Value ($(if ($Full) { 2 } else { 1 })) -Force | Out-Null
New-ItemProperty -Path $key -Name DumpCount -PropertyType DWord -Value $Count -Force | Out-Null
Write-Host "WER LocalDumps enabled for SkyrimNpcStudio.exe -> $folder ($(if ($Full) { 'full' } else { 'mini' }) dumps, keeps $Count)."
