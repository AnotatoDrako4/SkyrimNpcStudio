<#
.SYNOPSIS
  Configures, builds and tests Skyrim NPC Studio (C++ engines/app via CMake presets, .NET worker via dotnet).

.EXAMPLE
  .\scripts\build.ps1                 # debug preset: app + worker + all tests
  .\scripts\build.ps1 -Preset engines # no Qt needed
  .\scripts\build.ps1 -SkipTests
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'engines')]
    [string]$Preset = 'debug',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'dev-env.ps1')
. (Join-Path $PSScriptRoot 'build-toolchain.ps1')

Push-Location $root
try {
    $ninja = (Get-Command ninja -ErrorAction Stop).Source
    $toolchain = Resolve-CMakeNinjaCache -BuildDirectory (Join-Path $root "build\$Preset") -ActiveNinja $ninja
    if ($toolchain.CacheRemoved) {
        Write-Warning "Removed stale CMake cache ($($toolchain.Reason)): $($toolchain.CachedNinja)"
    }

    cmake --preset $Preset "-DCMAKE_MAKE_PROGRAM=$ninja"
    if ($LASTEXITCODE) { throw "cmake configure failed ($LASTEXITCODE)" }
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw "cmake build failed ($LASTEXITCODE)" }

    if (-not $SkipTests) {
        dotnet test --solution SkyrimNpcStudio.slnx
        if ($LASTEXITCODE) { throw ".NET tests failed ($LASTEXITCODE)" }
        $testPreset = if ($Preset -eq 'engines') { 'engines' } else { 'debug' }
        ctest --preset $testPreset -j 4
        if ($LASTEXITCODE) { throw "C++ tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
