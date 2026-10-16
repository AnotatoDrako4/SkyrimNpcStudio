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
    [string]$Destination = "",
    # Close a studio running from dist\ instead of stopping. Never the default: the open studio
    # may hold unsaved edits, and being killed looks like a crash (no error report is left).
    [switch]$CloseRunning
)

$ErrorActionPreference = 'Stop'
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$root = Split-Path -Parent $scriptDir
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $root 'dist\SkyrimNpcStudio'
}
. (Join-Path $scriptDir 'dev-env.ps1') | Out-Null

Push-Location $root
try {
    $distRoot = [IO.Path]::GetFullPath((Join-Path $root 'dist')).TrimEnd('\') + '\'
    $running = @(Get-Process -Name SkyrimNpcStudio -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -and $_.Path.StartsWith($distRoot, [StringComparison]::OrdinalIgnoreCase) } catch { $false }
    })
    if ($running.Count -gt 0 -and -not $CloseRunning) {
        Write-Host ''
        Write-Host "Skyrim NPC Studio esta abierto desde dist\ (PID $($running.Id -join ', '))."
        Write-Host 'No se cierra automaticamente: podria tener cambios sin guardar. Cierralo y vuelve a ejecutar,'
        Write-Host 'o usa -CloseRunning para cerrarlo a la fuerza.'
        exit 3
    }

    # Evitar bloqueos de DLLs por demonios en segundo plano de .NET
    dotnet build-server shutdown | Out-Null
    Stop-Process -Name VBCSCompiler -Force -ErrorAction SilentlyContinue

    Write-Host '[1/4] Compilando (Release)...'
    cmake --preset package | Out-Null
    if ($LASTEXITCODE) { throw "cmake configure failed ($LASTEXITCODE)" }
    # Builds launched from a CP850 console recorded no header dependencies (see dev-env.ps1), so the
    # objects of earlier packages may be stale: the first package after that fix is built clean.
    $packageDir = Join-Path $root 'build\package'
    $marker = Join-Path $packageDir '.sns-header-deps-v1'
    $clean = @()
    if (-not (Test-Path $marker)) {
        Write-Host '      Reconstruccion completa (una sola vez): objetos de paquetes anteriores no fiables.'
        $clean = @('--clean-first')
    }
    cmake --build --preset package @clean
    if ($LASTEXITCODE) { throw "cmake build failed ($LASTEXITCODE)" }
    # Every first-party object must know its headers, or a later header change would leave it stale.
    cmake "-DNINJA=$((Get-Command ninja).Source)" "-DBUILD_DIR=$packageDir" -P (Join-Path $root 'cmake\CheckHeaderDeps.cmake')
    if ($LASTEXITCODE) { throw 'Hay objetos sin dependencias de cabeceras registradas: el paquete no seria fiable.' }
    Set-Content -Path $marker -Value "header dependencies verified $(Get-Date -Format s)"

    # dist/ is a build output owned by this script: it is recreated from scratch every time.
    $full = [IO.Path]::GetFullPath($Destination)
    $dist = [IO.Path]::GetFullPath((Join-Path $root 'dist')).TrimEnd('\') + '\'
    if (-not $full.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Destination must be inside $root\dist"
    }

    # Only with -CloseRunning: the studio itself. Workers whose studio is gone (they normally exit
    # with it) are always cleaned up, since they would lock files in dist\.
    $studios = @(Get-Process -Name SkyrimNpcStudio -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -and $_.Path.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase) } catch { $false }
    })
    if ($studios.Count -gt 0) {
        if (-not $CloseRunning) { throw 'Skyrim NPC Studio se abrio desde dist\ durante la compilacion; cierralo y vuelve a empaquetar.' }
        Write-Host "Cerrando Skyrim NPC Studio (PID $($studios.Id -join ', ')) por -CloseRunning..."
        $studios | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Get-Process -Name SkyrimNpcStudio.BethesdaWorker -ErrorAction SilentlyContinue |
        Where-Object { try { $_.Path -and $_.Path.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase) } catch { $false } } |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    if (Test-Path $full) {
        $attempts = 0
        while ((Test-Path $full) -and ($attempts -lt 5)) {
            try {
                Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
                break
            } catch {
                $attempts++
                Start-Sleep -Milliseconds 800
            }
        }
    }
    New-Item -ItemType Directory -Force $full | Out-Null

    Write-Host '[2/4] Copiando el ejecutable y el runtime de Qt...'
    Copy-Item (Join-Path $root 'build\package\bin\SkyrimNpcStudio.exe') $full
    windeployqt --release --qmldir (Join-Path $root 'apps\desktop\qml') --no-translations `
        --no-system-d3d-compiler --compiler-runtime (Join-Path $full 'SkyrimNpcStudio.exe') | Out-Null
    if ($LASTEXITCODE) { throw "windeployqt failed ($LASTEXITCODE)" }
    # windeployqt only ships the "windows" platform; "offscreen" lets the package run headless smoke tests.
    Copy-Item (Join-Path $env:SNS_QT_DIR 'plugins\platforms\qoffscreen.dll') (Join-Path $full 'platforms')
    # RuntimeLoader (QtQuick3D.AssetUtils) requires the assetimporters plugin (assimp) to load GLB/glTF files:
    $assetImporters = Join-Path $full 'assetimporters'
    if (-not (Test-Path $assetImporters)) {
        New-Item -ItemType Directory -Force $assetImporters | Out-Null
    }
    Copy-Item (Join-Path $env:SNS_QT_DIR 'plugins\assetimporters\assimp.dll') $assetImporters
    # Ensure AssetUtils and AssetImport runtime DLLs are present:
    foreach ($dll in 'Qt6Quick3DAssetUtils.dll', 'Qt6Quick3DAssetImport.dll') {
        $src = Join-Path $env:SNS_QT_DIR "bin\$dll"
        if (Test-Path $src) { Copy-Item $src $full -Force }
    }

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
    # What produced this package, so a release can be rebuilt from the same sources and toolchain.
    $projectVersion = if ((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw) -match 'project\(SkyrimNpcStudio\s+VERSION\s+([0-9.]+)') { $Matches[1] } else { '' }
    $dirty = @(git -C $root status --porcelain --untracked-files=no 2>$null).Count -gt 0
    [ordered]@{
        product     = 'Skyrim NPC Studio'
        version     = $projectVersion
        commit      = (git -C $root rev-parse HEAD 2>$null)
        dirty       = $dirty
        builtUtc    = (Get-Date).ToUniversalTime().ToString('o')
        msvc        = $env:VCToolsVersion
        qt          = (Split-Path -Leaf (Split-Path -Parent $env:SNS_QT_DIR))
        dotnetSdk   = (dotnet --version)
        buildType   = 'Release'
    } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $full 'BUILDINFO.json')

    $size = [math]::Round(((Get-ChildItem $full -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host "Listo: $full ($size MB)"
}
finally {
    Pop-Location
}
exit 0
