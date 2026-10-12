<#
.SYNOPSIS
  Installs the pinned toolchains into a user folder (no admin, no system-wide changes):
  .NET SDK 10.0.401 (official dotnet-install.ps1) and Qt 6.11.2 msvc2022_64 (scripts/fetch_qt.py).

.PARAMETER Prefix
  Destination root. Defaults to D:\DevTools. Afterwards:  . .\scripts\dev-env.ps1
  (set SNS_DOTNET_ROOT / SNS_QT_DIR if you choose another prefix).

.NOTES
  Requires Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload and
  Python 3.10+ on PATH. Downloads ~300 MB (.NET) + ~1.5 GB (Qt).
#>
[CmdletBinding()]
param(
    [string]$Prefix = 'D:\DevTools',
    [string]$DotnetVersion = '10.0.401',
    [string]$QtVersion = '6.11.2'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Prefix | Out-Null
$dotnetDir = Join-Path $Prefix 'dotnet'
$qtRoot = Join-Path $Prefix 'Qt'

if (-not (Test-Path (Join-Path $dotnetDir "sdk\$DotnetVersion"))) {
    $installer = Join-Path $env:TEMP 'dotnet-install.ps1'
    Invoke-WebRequest -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer -UseBasicParsing
    if ((Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw 'dotnet-install.ps1 signature is not valid.' }
    & $installer -Version $DotnetVersion -InstallDir $dotnetDir -NoPath
} else {
    Write-Host ".NET SDK $DotnetVersion already present."
}

if (-not (Test-Path (Join-Path $qtRoot "$QtVersion\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake"))) {
    $venv = Join-Path $Prefix 'py-venv'
    if (-not (Test-Path $venv)) { python -m venv $venv }
    & (Join-Path $venv 'Scripts\python.exe') -m pip install --quiet 'py7zr==1.1.3'
    & (Join-Path $venv 'Scripts\python.exe') -I (Join-Path $PSScriptRoot 'fetch_qt.py') $qtRoot --version $QtVersion
    if ($LASTEXITCODE) { throw "Qt download failed ($LASTEXITCODE)" }
} else {
    Write-Host "Qt $QtVersion already present."
}

Write-Host "Done. Next:  . .\scripts\dev-env.ps1 ; .\scripts\build.ps1"
