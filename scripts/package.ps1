<#
.SYNOPSIS
  Builds a Release studio and copies everything it needs to run on its own into dist\SkyrimNpcStudio:
  the Qt runtime (windeployqt, incl. MSVC runtime) and a SELF-CONTAINED .NET worker, so neither the
  dev shell nor a system-wide .NET 10 install is required.

.EXAMPLE
  .\scripts\package.ps1
#>
[CmdletBinding()]
param(
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\SkyrimNpcStudio')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'dev-env.ps1') | Out-Null

Push-Location $root
try {
    Write-Host '[1/4] Compilando (Release)...'
    cmake --preset package | Out-Null
    if ($LASTEXITCODE) { throw "cmake configure failed ($LASTEXITCODE)" }
    cmake --build --preset package
    if ($LASTEXITCODE) { throw "cmake build failed ($LASTEXITCODE)" }

    # dist/ is a build output owned by this script: it is recreated from scratch every time.
    $full = [IO.Path]::GetFullPath($Destination)
    $dist = [IO.Path]::GetFullPath((Join-Path $root 'dist')).TrimEnd('\') + '\'
    if (-not $full.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Destination must be inside $root\dist"
    }
    if (Test-Path $full) { Remove-Item -LiteralPath $full -Recurse -Force }
    New-Item -ItemType Directory -Force $full | Out-Null

    Write-Host '[2/4] Copiando el ejecutable y el runtime de Qt...'
    Copy-Item (Join-Path $root 'build\package\bin\SkyrimNpcStudio.exe') $full
    windeployqt --release --qmldir (Join-Path $root 'apps\desktop\qml') --no-translations `
        --no-system-d3d-compiler --compiler-runtime (Join-Path $full 'SkyrimNpcStudio.exe') | Out-Null
    if ($LASTEXITCODE) { throw "windeployqt failed ($LASTEXITCODE)" }
    # windeployqt only ships the "windows" platform; "offscreen" lets the package run headless smoke tests.
    Copy-Item (Join-Path $env:SNS_QT_DIR 'plugins\platforms\qoffscreen.dll') (Join-Path $full 'platforms')

    Write-Host '[3/4] Publicando el worker .NET (self-contained, no requiere .NET instalado)...'
    dotnet publish (Join-Path $root 'services\BethesdaWorker\BethesdaWorker.csproj') -c Release -r win-x64 `
        --self-contained true -o (Join-Path $full 'worker') --nologo -v quiet
    if ($LASTEXITCODE) { throw "dotnet publish failed ($LASTEXITCODE)" }

    Write-Host '[4/4] Documentacion y licencias...'
    foreach ($f in 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md') { Copy-Item (Join-Path $root $f) $full }
    $licenses = New-Item -ItemType Directory -Force (Join-Path $full 'licenses')
    Copy-Item (Join-Path $root 'build\package\_deps\nifly-src\LICENSE') (Join-Path $licenses 'nifly-GPL-3.0.txt')
    Copy-Item (Join-Path $root 'build\package\_deps\lz4-src\lib\LICENSE') (Join-Path $licenses 'lz4-BSD-2-Clause.txt')
    Copy-Item (Join-Path $root 'build\package\_deps\directxtex-src\LICENSE') (Join-Path $licenses 'DirectXTex-MIT.txt')
    $commit = (git -C $root rev-parse --short HEAD 2>$null)
    if ($LASTEXITCODE) { $commit = 'sin-commit' }  # a repo without commits must not fail the package
    "Skyrim NPC Studio $(Get-Date -Format s) $commit" | Set-Content (Join-Path $full 'VERSION.txt')

    $size = [math]::Round(((Get-ChildItem $full -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host "Listo: $full ($size MB)"
}
finally {
    Pop-Location
}
exit 0
