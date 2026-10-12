<#
.SYNOPSIS
  Loads the MSVC x64 developer environment, .NET 10 SDK and Qt into the current PowerShell session.

.DESCRIPTION
  Dot-source it:  . .\scripts\dev-env.ps1
  Locations can be overridden with SNS_DOTNET_ROOT and SNS_QT_DIR; defaults match scripts/bootstrap.ps1.
#>
[CmdletBinding()]
param(
    [string]$DotnetRoot = $(if ($env:SNS_DOTNET_ROOT) { $env:SNS_DOTNET_ROOT } else { 'D:\DevTools\dotnet' }),
    [string]$QtDir = $(if ($env:SNS_QT_DIR) { $env:SNS_QT_DIR } else { 'D:\DevTools\Qt\6.11.2\msvc2022_64' })
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2022 or Build Tools with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the MSVC x64 toolset was found.' }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

if (-not (Test-Path (Join-Path $DotnetRoot 'dotnet.exe'))) { throw ".NET SDK not found at $DotnetRoot (run scripts/bootstrap.ps1)." }
$env:DOTNET_ROOT = $DotnetRoot
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:PATH = "$DotnetRoot;$env:PATH"

if (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake')) {
    $env:SNS_QT_DIR = $QtDir
    $env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"
} else {
    Write-Warning "Qt not found at ${QtDir}; only the 'engines' preset will configure."
}

Write-Host "MSVC: $env:VCToolsVersion | .NET: $(& dotnet --version) | Qt: $env:SNS_QT_DIR"
