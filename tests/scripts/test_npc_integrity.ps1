[CmdletBinding()]
param([Parameter(Mandatory)][string]$Scanner, [Parameter(Mandatory)][string]$Worker,
      [Parameter(Mandatory)][string]$FixtureData, [Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Work) { throw 'Use a new isolated Work directory.' }
$fixture = Get-Content -LiteralPath (Join-Path $FixtureData 'fixture.json') -Raw | ConvertFrom-Json
$mo = Join-Path $Work 'mo2'
$profile = Join-Path $mo 'profiles/IntegrityTest'
$output = Join-Path $Work 'evidence'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$game = (Split-Path -Parent $FixtureData).Replace('\', '/')
Set-Content -LiteralPath (Join-Path $mo 'ModOrganizer.ini') -Value "[General]`ngameName=Skyrim Special Edition`ngamePath=$game`nselected_profile=IntegrityTest" -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $profile 'settings.ini') -Value "[General]`nLocalSettings=true" -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $profile 'Skyrim.ini') -Value "[Archive]`nsResourceArchiveList=`nsResourceArchiveList2=`n[General]`nsLanguage=ENGLISH" -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $profile 'plugins.txt') -Value (($fixture.plugins | ForEach-Object { '*' + $_.fileName }) -join "`n") -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $profile 'loadorder.txt') -Value (($fixture.plugins | ForEach-Object { $_.fileName }) -join "`n") -Encoding utf8NoBOM
$face = 'meshes/actors/character/facegendata/facegeom/Skyrim.esm/000A2C8E.nif'
foreach ($provider in @('FaceA','FaceB')) {
    $target = Join-Path $mo "mods/$provider/$face"
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Set-Content -LiteralPath $target -Value 'synthetic provider bytes; not a renderable NIF' -Encoding utf8NoBOM
}
$job = @{ instance=$mo; profile='IntegrityTest'; output=$output; worker=$Worker; npcs=@($fixture.lydia, '0A0001:Skyrim.esm') }
$jobPath = Join-Path $Work 'job.json'
$job | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jobPath -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $profile 'modlist.txt') -Value "+FaceB`n+FaceA" -Encoding utf8NoBOM
& $Scanner $jobPath > (Join-Path $Work 'before.json')
if ($LASTEXITCODE) { throw "Baseline failed: $LASTEXITCODE" }
Set-Content -LiteralPath (Join-Path $profile 'modlist.txt') -Value "+FaceA`n+FaceB" -Encoding utf8NoBOM
& $Scanner $jobPath > (Join-Path $Work 'after.json')
if ($LASTEXITCODE) { throw "Incremental scan failed: $LASTEXITCODE" }
$report = Get-Content -LiteralPath (Join-Path $Work 'after.json') -Raw | ConvertFrom-Json
if ($report.checkedNpcs -ne 1) { throw "Expected exactly one affected NPC; got $($report.checkedNpcs)" }
if (-not $report.catalog.skipped) { throw 'Asset-only scan rebuilt the worker catalogue.' }
if (-not ($report.findings | Where-Object { $_.rule -eq 'EffectiveProviderChanged' -and $_.npc -eq $fixture.lydia.ToLowerInvariant() -and -not $_.confirmed })) {
    throw 'Missing explainable provider-change finding.'
}
& $Scanner $jobPath > (Join-Path $Work 'noop.json')
if ($LASTEXITCODE) { throw "No-op scan failed: $LASTEXITCODE" }
$noop = Get-Content -LiteralPath (Join-Path $Work 'noop.json') -Raw | ConvertFrom-Json
if ($noop.checkedNpcs -ne 0) { throw 'Unchanged scan re-read NPCs.' }
Write-Output 'PASS: real VFS + worker; provider change checks 1/2 NPCs; subsequent no-op checks 0; Skyrim validation pending.'
