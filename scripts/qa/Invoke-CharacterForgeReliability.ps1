[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputRoot,
    [switch]$SkipCtest
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $repo 'build\claude-forge' }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $repo 'out\local-implementation\forge-skyrim-qa' }
$build = (Resolve-Path $BuildDir).Path
$output = if ([IO.Path]::IsPathRooted($OutputRoot)) { [IO.Path]::GetFullPath($OutputRoot) } else { Join-Path $repo $OutputRoot }
New-Item -ItemType Directory -Force -Path $output | Out-Null

function Invoke-Logged([string]$Name, [string]$File, [string[]]$Arguments, [string]$WorkingDirectory) {
    $log = Join-Path $output "$Name.log"
    $stderr = Join-Path $output "$Name.stderr.log"
    $start = Get-Date
    $quotedArguments = @($Arguments | ForEach-Object {
        $argument = [string]$_
        if ($argument -match '[\s"]') { '"' + ($argument -replace '"', '\\"') + '"' } else { $argument }
    })
    $process = Start-Process -FilePath $File -ArgumentList $quotedArguments -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $log -RedirectStandardError $stderr -NoNewWindow -Wait -PassThru
    if (Test-Path $stderr) { Get-Content -Raw $stderr | Add-Content -LiteralPath $log }
    $code = $process.ExitCode
    Get-Content -LiteralPath $log | Write-Host
    $joined = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
    $status = if ($joined -match 'No tests were found') { 'CTEST_NO_TESTS' } elseif ($joined -match 'No test cases matched') { 'FAIL_UNICODE_FILTER' } elseif ($code -eq 0) { 'PASS' } elseif ($code -eq 4) { 'SKIP' } elseif ($code -eq -1073741515) { 'BLOCKED_RUNTIME_DLL' } else { 'FAIL' }
    [pscustomobject]@{ name = $Name; status = $status; exitCode = $code; log = $log; durationSeconds = [math]::Round(((Get-Date) - $start).TotalSeconds, 3) }
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$coveragePath = Join-Path $repo 'tests\qa\character_forge_coverage.json'
$coverage = Get-Content -Raw $coveragePath | ConvertFrom-Json
$fixture = Join-Path $repo 'tests\fixtures\models\skyrim\goblet_report.json'
$fixtureBefore = if (Test-Path $fixture) { Get-Sha256 $fixture } else { $null }

$devEnv = Join-Path $repo 'scripts\dev-env.ps1'
if (Test-Path $devEnv) {
    try { . $devEnv | Out-Null } catch { Write-Warning "dev-env no disponible: $($_.Exception.Message)" }
}

$results = [System.Collections.Generic.List[object]]::new()
$engine = Join-Path $build 'bin\sns_engine_tests.exe'
$forge = Join-Path $build 'bin\sns_forge_asset_tests.exe'
$ctest = Get-Command ctest.exe -ErrorAction SilentlyContinue

if (-not (Test-Path $engine)) { throw "Missing test binary: $engine" }
$results.Add((Invoke-Logged 'direct-skyrim-conversion' $engine @('[forge][skyrim]') $build))

if (Test-Path $forge) {
    $results.Add((Invoke-Logged 'direct-forge-package' $forge @('[forge][skyrim]') $build))
} else {
    $results.Add([pscustomobject]@{ name = 'direct-forge-package'; status = 'SKIP'; exitCode = 4; log = $null; durationSeconds = 0; skipped = $true })
}

# The direct Catch2 filters use ASCII tags/prefixes and avoid CTest's broken
# Windows command-line round-trip for the nine affected Unicode test names.
$results.Add((Invoke-Logged 'direct-unicode-npc' $engine @('NPC-0*') $build))
$results.Add((Invoke-Logged 'direct-unicode-serana' $engine @('SERANA-0*') $build))

if (-not $SkipCtest -and $ctest) {
    $results.Add((Invoke-Logged 'ctest-skyrim-unit' $ctest.Source @('--test-dir', $build, '-R', 'unit:Skyrim conversion.*', '--output-on-failure') $repo))
    $results.Add((Invoke-Logged 'ctest-forge-app' $ctest.Source @('--test-dir', $build, '-R', 'app:.*Forge', '--output-on-failure') $repo))
    $results.Add((Invoke-Logged 'ctest-unicode-regression' $ctest.Source @('--test-dir', $build, '-R', 'unit:(NPC-08|NPC-09|SERANA-0[1-7]).*', '--output-on-failure') $repo))
} else {
    $results.Add([pscustomobject]@{ name = 'ctest'; status = 'SKIP'; exitCode = 4; log = $null; durationSeconds = 0; skipped = $true })
}

$fixtureAfter = if (Test-Path $fixture) { Get-Sha256 $fixture } else { $null }
$integrity = [ordered]@{
    originalFixtureUnchanged = ($fixtureBefore -eq $fixtureAfter -and $null -ne $fixtureBefore)
    outputOutsideForbiddenRoots = ($output -notmatch '(?i)(Skyrim[\\/]Data|MO2[\\/](mods|overwrite))')
    outputRoot = $output
    fixtureBefore = $fixtureBefore
    fixtureAfter = $fixtureAfter
}

$summary = [ordered]@{
    schema = 'sns.character-forge.reliability-run/1'
    timestamp = (Get-Date).ToUniversalTime().ToString('o')
    repository = $repo
    build = $build
    coverage = $coverage.cases
    results = $results
    integrity = $integrity
    unicodeDiagnosis = [ordered]@{
        directFilters = 'PASS is expected when binaries and fixtures are available'
        ctestFilter = 'FAIL is a reproducible infrastructure/encoding symptom if exit code is non-zero'
        affected = @('NPC-08', 'NPC-09', 'SERANA-01', 'SERANA-02', 'SERANA-03', 'SERANA-04', 'SERANA-05', 'SERANA-06', 'SERANA-07')
    }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8

$failed = @($results | Where-Object { $_.status -in @('FAIL', 'FAIL_UNICODE_FILTER', 'CTEST_NO_TESTS') })
$skipped = @($results | Where-Object { $_.status -in @('SKIP', 'BLOCKED_RUNTIME_DLL') -or $_.skipped -eq $true })
Write-Output ("Reliability run: {0} command(s), {1} hard failure(s), {2} skipped/blocked command(s)." -f $results.Count, $failed.Count, $skipped.Count)
Write-Output ("Summary: {0}" -f (Join-Path $output 'summary.json'))
if (-not $integrity.originalFixtureUnchanged) { throw 'Integrity failure: source fixture hash changed during the run.' }
if ($failed.Count -gt 0) { exit 1 }
