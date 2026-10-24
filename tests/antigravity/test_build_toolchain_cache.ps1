[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$helper = Join-Path $root 'scripts\build-toolchain.ps1'

if (-not (Test-Path -LiteralPath $helper)) {
    throw "Missing build toolchain helper: $helper"
}

. $helper

$scratch = Join-Path ([IO.Path]::GetTempPath()) ("sns-build-toolchain-" + [Guid]::NewGuid().ToString('N'))
$buildDir = Join-Path $scratch 'debug'
$cache = Join-Path $buildDir 'CMakeCache.txt'
$staleNinja = Join-Path $scratch 'stale\ninja.exe'
$activeNinja = Join-Path $scratch 'active\ninja.exe'

try {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $activeNinja) -Force | Out-Null
    Set-Content -LiteralPath $activeNinja -Value 'fixture'
    Set-Content -LiteralPath $cache -Value @(
        "CMAKE_MAKE_PROGRAM:FILEPATH=$($staleNinja -replace '\\','/')"
        'CMAKE_GENERATOR:INTERNAL=Ninja'
    )

    $result = Resolve-CMakeNinjaCache -BuildDirectory $buildDir -ActiveNinja $activeNinja

    if (-not $result.CacheRemoved) { throw 'Expected stale CMake cache to be removed.' }
    if (Test-Path -LiteralPath $cache) { throw 'Stale CMakeCache.txt still exists.' }

    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    Set-Content -LiteralPath $cache -Value @(
        "CMAKE_MAKE_PROGRAM:FILEPATH=$($activeNinja -replace '\\','/')"
        'CMAKE_GENERATOR:INTERNAL=Ninja'
    )

    $result = Resolve-CMakeNinjaCache -BuildDirectory $buildDir -ActiveNinja $activeNinja
    if ($result.CacheRemoved) { throw 'Matching CMake cache must be preserved.' }
    if (-not (Test-Path -LiteralPath $cache)) { throw 'Matching CMake cache was unexpectedly removed.' }

    Write-Host 'PASS: stale Ninja cache is removed and matching cache is preserved.'
}
finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
