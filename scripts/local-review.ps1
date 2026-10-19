<#
.SYNOPSIS
  Rebuilds and checks the local Windows app, saving evidence under out/local-review/.
.EXAMPLE
  .\scripts\local-review.ps1 -Instance 'D:\Apps\ModOrganizer.ini' -Profile Default -Npc '0A2C8E:Skyrim.esm'
#>
[CmdletBinding()]
param(
    [string]$Instance,
    [string]$Profile,
    [string]$Npc,
    [string]$Game,
    [string]$Blender
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $commit = (git rev-parse --short=7 HEAD).Trim()
    $runDir = Join-Path $root ('out/local-review/{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $commit)
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    # The app runs below (smoke, forge preview): its preferences (window, panel layout, last workspace)
    # go to an INI file of this run, never to the user's own settings.
    $previousSettingsDir = $env:SNS_SETTINGS_DIR
    $env:SNS_SETTINGS_DIR = Join-Path $runDir 'settings'
    New-Item -ItemType Directory -Force -Path $env:SNS_SETTINGS_DIR | Out-Null
    $results = [ordered]@{}
    $summary = [ordered]@{
        sourceCommit = (git rev-parse HEAD).Trim()
        branch = (git branch --show-current).Trim()
        startedAt = (Get-Date).ToString('o')
        gitStatus = @(git status --porcelain)
        msvc = $null
        qt = $null
        dotnet = $null
        results = $results
    }

    function Invoke-ReviewStep {
        param([string]$Name, [scriptblock]$Action)
        Start-Transcript -Path (Join-Path $runDir "$Name-transcript.log") -Force | Out-Null
        try {
            & $Action
            $results[$Name] = 'passed'
            return $true
        } catch {
            $results[$Name] = 'failed: ' + $_.Exception.Message
            Write-Error -ErrorAction Continue $_
            return $false
        } finally {
            Stop-Transcript | Out-Null
        }
    }

    . (Join-Path $PSScriptRoot 'dev-env.ps1')
    $summary.msvc = $env:VCToolsVersion
    $summary.qt = $env:SNS_QT_DIR
    $summary.dotnet = (& dotnet --version).Trim()

    $buildOk = Invoke-ReviewStep 'build' {
        & (Join-Path $PSScriptRoot 'build.ps1') -SkipTests
        if ($LASTEXITCODE -ne 0) { throw "build exited $LASTEXITCODE" }
    }
    if ($buildOk) {
        [void](Invoke-ReviewStep 'dotnet-test' {
            dotnet test --solution SkyrimNpcStudio.slnx
            if ($LASTEXITCODE -ne 0) { throw "dotnet test exited $LASTEXITCODE" }
        })
        [void](Invoke-ReviewStep 'ctest' {
            ctest --preset debug -j 4 --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw "ctest exited $LASTEXITCODE" }
        })
        if ($Instance) {
            if (-not (Test-Path -LiteralPath $Instance)) { throw "Instance not found: $Instance" }
            $env:SNS_LOCAL_MO2_INI = (Resolve-Path -LiteralPath $Instance).Path
            $env:SNS_LOCAL_CACHE_DIR = Join-Path $runDir 'cache'
            if ($Profile) { $env:SNS_LOCAL_PROFILE = $Profile }
            if ($Game) { $env:SNS_LOCAL_GAME_DIR = $Game }
            [void](Invoke-ReviewStep 'ctest-local' {
                ctest --preset debug -R 'integration:\[local\]' -j 1 --output-on-failure
                if ($LASTEXITCODE -ne 0) { throw "local ctest exited $LASTEXITCODE" }
            })
        }
        # Opt-in real Blender bridge regression: generate owned FBX/OBJ/BLEND fixtures, then
        # run the full conversion + managed .sncf import checks. This must never be marked
        # successful when Blender is absent or the tests were merely skipped.
        if ($Blender) {
            [void](Invoke-ReviewStep 'blender-bridge' {
                if (-not (Test-Path -LiteralPath $Blender -PathType Leaf)) { throw "Blender executable missing: $Blender" }
                $env:SNS_LOCAL_BLENDER = (Resolve-Path -LiteralPath $Blender).Path
                $inputs = Join-Path $runDir 'blender-inputs'
                New-Item -ItemType Directory -Force -Path $inputs | Out-Null
                & $env:SNS_LOCAL_BLENDER --background --factory-startup --disable-autoexec --python-exit-code 17 --python (Join-Path $root 'tools/blender/make_test_models.py') -- $inputs bridge_sources
                if ($LASTEXITCODE -ne 0) { throw "Blender fixture generation exited $LASTEXITCODE" }
                $env:SNS_LOCAL_BLENDER_FIXTURES = $inputs
                $testExe = Join-Path $root 'build/debug/bin/sns_forge_asset_tests.exe'
                if (-not (Test-Path -LiteralPath $testExe)) { throw "Build the Forge test executable first: $testExe" }
                & $testExe '[blender][local]'
                if ($LASTEXITCODE -ne 0) { throw "Real Blender bridge regression exited $LASTEXITCODE" }
            })
        }
        [void](Invoke-ReviewStep 'smoke' {
            $smokeArgs = @{ Screenshot = (Join-Path $runDir 'smoke.png') }
            if ($Instance) { $smokeArgs.Instance = $Instance }
            if ($Profile) { $smokeArgs.MO2Profile = $Profile }
            if ($Game) { $smokeArgs.Game = $Game }
            if ($Npc) {
                $smokeArgs.Npc = $Npc
                $smokeArgs.VerifyViewport = $true
                $smokeArgs.CaptureSet = Join-Path $runDir 'captures'
                $smokeArgs.ShowWindow = $true
            }
            & (Join-Path $PSScriptRoot 'smoke-test.ps1') @smokeArgs
            if ($LASTEXITCODE -ne 0) { throw "smoke exited $LASTEXITCODE" }
        })
        # Character Forge: real GLB/glTF (tools/blender/make_test_models.py) rendered in the window,
        # parts selected by clicking and hidden/shown, a truncated GLB rejected with a visible
        # error, and a repeated load.
        [void](Invoke-ReviewStep 'forge-preview' {
            $inputs = Join-Path $runDir 'forge-inputs'
            New-Item -ItemType Directory -Force -Path $inputs | Out-Null
            $models = Join-Path $root 'tests/fixtures/models'
            $bytes = [IO.File]::ReadAllBytes((Join-Path $models 'suzanne.glb'))
            [IO.File]::WriteAllBytes((Join-Path $inputs 'truncated.glb'), $bytes[0..($bytes.Length - 4001)])
            $files = @((Join-Path $models 'suzanne.glb'), (Join-Path $models 'rigged_figure.glb'),
                       (Join-Path $models 'rigged_figure_split/rigged_figure.gltf'), (Join-Path $models 'parts_set.glb'),
                       (Join-Path $inputs 'truncated.glb'), (Join-Path $models 'suzanne.glb'))
            & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Screenshot (Join-Path $runDir 'forge.png') -ForgePreview $files -Size 1600x900 -ShowWindow
            if ($LASTEXITCODE -ne 0) { throw "forge preview exited $LASTEXITCODE" }
        })
    }
    $summary.finishedAt = (Get-Date).ToString('o')
    $summary.finalGitStatus = @(git status --porcelain)
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDir 'summary.json') -Encoding utf8
    Write-Host "Review evidence: $runDir"
    if (@($results.Values | Where-Object { $_ -ne 'passed' }).Count -gt 0) { exit 1 }
} finally {
    $env:SNS_SETTINGS_DIR = $previousSettingsDir
    Pop-Location
}
