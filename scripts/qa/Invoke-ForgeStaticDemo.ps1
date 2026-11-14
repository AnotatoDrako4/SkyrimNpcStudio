<#
.SYNOPSIS
  Reproducible Character Forge demo: Blender model -> Forge (real window) -> verified NIF -> MO2 mod package (ESL, one STAT).

.DESCRIPTION
  Everything is written below -Out, which must be empty or missing:
    model\       a static prop made by tools/blender/make_test_models.py (unless -Model names another .blend/.fbx/.obj)
    mo2\         a SYNTHETIC portable MO2 instance (tests/tools/synthetic_mo2.cpp) the worker indexes, read-only
    settings\    the studio's settings for this run (SNS_SETTINGS_DIR), so the user's are not touched
    captures\    window captures, the .sncf project with exports\skyrim\ and mods\
    studio.log   the harness log (forge_preview_check.cpp): every PASS / FAIL
    summary.json the result, re-checked here: ZIP entries, plugin SHA-256, NIF and DDS headers, source model unchanged
  It never touches the user's MO2 instance, profiles or Skyrim\Data. Best state reachable: PAQUETE_MO2_VERIFICADO;
  behaviour inside Skyrim stays PENDIENTE_SKYRIM until a person installs the ZIP in an isolated profile and checks it.

.EXAMPLE
  .\scripts\qa\Invoke-ForgeStaticDemo.ps1 -Build build\claude-forge-ux -Out out\forge-demo-1
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Out,
  [string]$Build = (Join-Path $PSScriptRoot '..\..\build\debug'),
  [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',
  [string]$Model,
  [int]$TimeoutMinutes = 30
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bin = Join-Path (Resolve-Path $Build).Path 'bin'
foreach ($exe in 'SkyrimNpcStudio.exe', 'sns_synthetic_mo2.exe') {
  if (-not (Test-Path (Join-Path $bin $exe))) { throw "$exe not found in $bin (build the 'all' target first)" }
}
if (-not (Test-Path -LiteralPath $Blender)) { throw "Blender not found: $Blender" }
if (Test-Path $Out) {
  if (@(Get-ChildItem -LiteralPath $Out -Force).Count) { throw "-Out must be empty or missing: $Out" }
} else { New-Item -ItemType Directory $Out | Out-Null }
$Out = (Resolve-Path $Out).Path
$clock = [Diagnostics.Stopwatch]::StartNew()

# 1. The model: a static prop from Blender primitives (versioned generator) unless the caller gives one.
if (-not $Model) {
  & $Blender --background --factory-startup --python (Join-Path $repo 'tools\blender\make_test_models.py') -- (Join-Path $Out 'model') skyrim_prop *>
    (Join-Path $Out 'make-model.log')
  if ($LASTEXITCODE) { throw "make_test_models.py failed with $LASTEXITCODE (see make-model.log)" }
  $Model = Join-Path $Out 'model\skyrim_prop.blend'
}
$Model = (Resolve-Path -LiteralPath $Model).Path
$modelBefore = (Get-FileHash -LiteralPath $Model -Algorithm SHA256).Hash

# 2. A synthetic MO2 instance: the MO2 package step needs a profile indexed by the worker.
$mo2 = Join-Path $Out 'mo2'
& (Join-Path $bin 'sns_synthetic_mo2.exe') $mo2 | Out-File (Join-Path $Out 'mo2.json') -Encoding utf8
if ($LASTEXITCODE) { throw "sns_synthetic_mo2 failed with $LASTEXITCODE" }

# 3. The studio, in a real window, driven by its own --forge-preview harness: ANALIZAR -> EXPORTAR NIF VALIDADO ->
#    EMPAQUETAR MOD MO2 are clicked like a user would.
$captures = Join-Path $Out 'captures'
$saved = @{}
$environment = @{
  SNS_SETTINGS_DIR = (Join-Path $Out 'settings'); SNS_FORGE_SKYRIM_BLENDER = $Blender; SNS_FORGE_SKYRIM_MODEL = $Model
  SNS_FORGE_SKYRIM_MOD = '1'; SNS_FORGE_SKYRIM_PROFILE = 'Synthetic'
}
foreach ($k in $environment.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $environment[$k]) }
try {
  $preview = Join-Path $repo 'tests\fixtures\models\suzanne.glb'
  $arguments = @('--forge-preview', "`"$preview`"", '--forge-capture', "`"$captures`"", '--instance', "`"$(Join-Path $mo2 'ModOrganizer.ini')`"")
  $studio = Start-Process -FilePath (Join-Path $bin 'SkyrimNpcStudio.exe') -ArgumentList $arguments -PassThru `
    -RedirectStandardError (Join-Path $Out 'studio.log') -WindowStyle Normal
  $null = $studio.Handle  # keeps the exit code readable
  if (-not $studio.WaitForExit($TimeoutMinutes * 60 * 1000)) { $studio.Kill(); throw "The studio did not finish in $TimeoutMinutes min" }
  $studio.WaitForExit()
  $studioExit = $studio.ExitCode
} finally {
  foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
}

# 4. Re-check what the studio reports, from the files themselves.
$log = Get-Content -LiteralPath (Join-Path $Out 'studio.log') -Encoding utf8
$fails = @($log | Where-Object { $_ -match '\] FAIL ' })
$passes = @($log | Where-Object { $_ -match '\] PASS ' }).Count
$zip = $null; $pluginSha = $null
foreach ($line in $log) { if ($line -match 'mod zip (.+\.zip) plugin sha256 ([0-9A-Fa-f]{64})') { $zip = $Matches[1]; $pluginSha = $Matches[2].ToUpper() } }
$checks = [ordered]@{}
$entries = @()
if ($zip -and (Test-Path -LiteralPath $zip)) {
  $unpacked = Join-Path $Out 'zip-check'
  Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
  $entries = @(Get-ChildItem $unpacked -Recurse -File | ForEach-Object { $_.FullName.Substring($unpacked.Length + 1) })
  $plugin = Get-ChildItem $unpacked -Recurse -File -Include *.esp, *.esl | Select-Object -First 1
  $checks.pluginInZip = [bool]$plugin
  $checks.pluginSha256Matches = [bool]($plugin -and ((Get-FileHash -LiteralPath $plugin.FullName -Algorithm SHA256).Hash -eq $pluginSha))
  $header = { param($file, $n) $b = [IO.File]::ReadAllBytes($file); [Text.Encoding]::ASCII.GetString($b, 0, [Math]::Min($n, $b.Length)) }
  $nifs = @(Get-ChildItem $unpacked -Recurse -File -Filter *.nif)
  $checks.nifHeaders = $nifs.Count -gt 0 -and -not ($nifs | Where-Object { (& $header $_.FullName 20) -ne 'Gamebryo File Format' })
  $dds = @(Get-ChildItem $unpacked -Recurse -File -Filter *.dds)
  $checks.ddsHeaders = -not ($dds | Where-Object { (& $header $_.FullName 4) -ne 'DDS ' })
  $checks.pluginIsTes4 = [bool]($plugin -and ((& $header $plugin.FullName 4) -eq 'TES4'))
}
$modelAfter = (Get-FileHash -LiteralPath $Model -Algorithm SHA256).Hash
$checks.sourceModelUnchanged = $modelBefore -eq $modelAfter
$packaged = $studioExit -eq 0 -and $fails.Count -eq 0 -and $zip -and -not ($checks.Values -contains $false)
$summary = [ordered]@{
  status = $(if ($packaged) { 'PAQUETE_MO2_VERIFICADO' } else { 'FAIL' }); skyrim = 'PENDIENTE_SKYRIM'
  studioExit = $studioExit; pass = $passes; fail = $fails.Count; failures = $fails
  model = $Model; modelSha256Before = $modelBefore; modelSha256After = $modelAfter
  zip = $zip; zipSha256 = $(if ($zip -and (Test-Path -LiteralPath $zip)) { (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash })
  pluginSha256 = $pluginSha; entries = $entries; checks = $checks; seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 1)
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Out 'summary.json') -Encoding utf8
"{0}: {1} PASS / {2} FAIL, studio exit {3}, {4} s" -f $summary.status, $passes, $fails.Count, $studioExit, $summary.seconds
if ($zip) { "ZIP: $zip" }
if (-not $packaged) { exit 1 }
