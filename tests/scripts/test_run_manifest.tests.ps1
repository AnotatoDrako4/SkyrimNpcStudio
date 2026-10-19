<#
  TestRunManifest (D3/D22) on synthetic files: what a run records, and the verifier refusing evidence of
  another build, a changed profile, missing or changed artifacts, an altered manifest and in-game
  results without human evidence. Writes only under -Work (a fresh folder per run).
#>
param([Parameter(Mandatory = $true)][string]$Work)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
Import-Module (Join-Path $PSScriptRoot '..\..\scripts\TestRunManifest.psm1') -Force

$script:checks = 0
$script:failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
    $script:checks++
    if ($Condition) { Write-Host "PASS $What" } else { Write-Host "FAIL $What"; $script:failures.Add($What) }
}
function Problems($Result) { @($Result.Problems) -join '; ' }
function Has($Result, [string]$Prefix) { @($Result.Problems | Where-Object { $_ -like "$Prefix*" }).Count -gt 0 }
function Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }

$root = Join-Path $Work ([DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
$build = Join-Path $root 'build'
$profile = Join-Path $root 'profile'
$run = Join-Path $root 'run'
$mod = Join-Path $run 'export\SNS_Test'
New-Item -ItemType Directory -Force $build, $profile, $mod | Out-Null

$exe = Join-Path $build 'SkyrimNpcStudio.exe'
[IO.File]::WriteAllBytes($exe, [Text.Encoding]::ASCII.GetBytes('build one'))
Utf8 (Join-Path $profile 'modlist.txt') "+Studio Patch`n+NITHI`n-Disabled Mod`n"
Utf8 (Join-Path $profile 'plugins.txt') "*Skyrim.esm`n*NITHI.esp`nInactive.esp`n"
$esp = Join-Path $mod 'SNS_Test.esp'
[IO.File]::WriteAllBytes($esp, [byte[]](0x54, 0x45, 0x53, 0x34, 1, 2, 3))
$espHash = (Get-FileHash -LiteralPath $esp -Algorithm SHA256).Hash
$exportManifest = [ordered]@{
    format = 'skyrim-npc-studio/export'; mode = 'face-overhaul'; inGameVerified = $false; readyForGame = $false
    plugin = [ordered]@{ fileName = 'SNS_Test.esp'; sha256 = $espHash }
    loadAfter = @('NITHI.esp'); faces = @([ordered]@{ npc = '01334E:Skyrim.esm'; installAbove = @('NITHI') })
}
Utf8 (Join-Path $mod 'manifest.json') ($exportManifest | ConvertTo-Json -Depth 5)
[IO.File]::WriteAllBytes((Join-Path $run 'export\SNS_Test.zip'), [byte[]](0x50, 0x4B, 5, 6))
[IO.File]::WriteAllBytes((Join-Path $run 'capture.png'), [byte[]](0x89, 0x50, 0x4E, 0x47))
$log = Join-Path $run 'smoke.log'
Utf8 $log ("stress: [    10 ms] start on 01334E:Skyrim.esm (Bolli), scenarios: face-resolution, appearance-scan`n" +
           "stress: [    20 ms] PASS control map belongs to 01334E:Skyrim.esm`n" +
           "stress: [    30 ms] PASS the export refuses it`n" +
           "stress: [    40 ms] summary: PASSED (2 scenario(s))`n")

# ---- what a run records
$created = New-SnsTestRunManifest -RunDir $run -ProfileDir $profile -SmokeLog $log -SmokeExitCode 0 -BuildFiles $exe -MO2Profile 'Test'
$m = $created.Manifest
Check ($m.results.static -eq 'PASS_STATIC' -and $m.results.windows -eq 'PASS_WINDOWS' -and $m.results.inGame -eq 'WAITING_HUMAN') `
      "results static=$($m.results.static) windows=$($m.results.windows) inGame=$($m.results.inGame)"
Check (@($m.problems).Count -eq 0) "no problems: $(@($m.problems) -join '; ')"
Check (($m.npcs -join ',') -eq '01334E:Skyrim.esm' -and ($m.scenarios -join ',') -eq 'face-resolution,appearance-scan') "NPC and scenarios from the log"
Check ($m.smoke.pass -eq 2 -and $m.smoke.fail -eq 0 -and $m.smoke.summary -eq 'PASSED') "smoke counts"
Check (($m.mo2.enabledMods -join '|') -eq 'Studio Patch|NITHI' -and ($m.mo2.activePlugins -join '|') -eq 'Skyrim.esm|NITHI.esp') "enabled mods and active plugins"
Check (@($m.mo2.files).Count -eq 2) "profile files hashed"
Check ($null -eq $m.build.git.sha -and @($m.build.files).Count -eq 1) "build identified by its binaries, Git not queried"
Check (-not $m.skyrim.detected) "no game folder: Skyrim not detected (never guessed)"
$e = $m.exports[0]
Check ($e.plugin -eq 'SNS_Test.esp' -and ($e.installAbove -join ',') -eq 'NITHI' -and ($e.loadAfter -join ',') -eq 'NITHI.esp' -and $e.zip -eq 'export/SNS_Test.zip') `
      "export: plugin, installAbove, loadAfter, ZIP"
Check (@($m.artifacts).Count -eq 5) "artifacts: $(@($m.artifacts | ForEach-Object { $_.path }) -join ', ')"
Check (Test-Path -LiteralPath "$($created.Path).sha256") "sidecar hash written"

$ok = Test-SnsTestRunManifest -Path $created.Path
Check $ok.Valid "fresh manifest verifies: $(Problems $ok)"

# ---- the verifier
$original = [IO.File]::ReadAllBytes($exe)
[IO.File]::WriteAllBytes($exe, [Text.Encoding]::ASCII.GetBytes('build two'))
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'OtherBuild')) "evidence of another build is refused: $(Problems $r)"
[IO.File]::WriteAllBytes($exe, $original)

$plugins = Join-Path $profile 'plugins.txt'
$before = [IO.File]::ReadAllBytes($plugins)
Utf8 $plugins "*Skyrim.esm`n*NITHI.esp`n*Late Patch.esp`n"
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'ProfileChanged: plugins.txt')) "a changed load order is refused: $(Problems $r)"
[IO.File]::WriteAllBytes($plugins, $before)

Move-Item -LiteralPath (Join-Path $run 'capture.png') -Destination (Join-Path $root 'capture.moved')
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'ArtifactMissing: capture.png')) "a missing screenshot is refused: $(Problems $r)"
Move-Item -LiteralPath (Join-Path $root 'capture.moved') -Destination (Join-Path $run 'capture.png')

$zip = Join-Path $run 'export\SNS_Test.zip'
$zipBytes = [IO.File]::ReadAllBytes($zip)
[IO.File]::WriteAllBytes($zip, [byte[]](0x50, 0x4B, 9, 9))
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'ArtifactChanged: export/SNS_Test.zip')) "a changed ZIP is refused: $(Problems $r)"
[IO.File]::WriteAllBytes($zip, $zipBytes)

$text = [IO.File]::ReadAllText($created.Path)
Utf8 $created.Path ($text.Replace('"inGame": "WAITING_HUMAN"', '"inGame": "PASS_IN_GAME"'))
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'ManifestAltered') -and (Has $r 'InGameResultWithoutEvidence')) "an edited manifest claiming PASS_IN_GAME is refused: $(Problems $r)"
Utf8 $created.Path $text
Check (Test-SnsTestRunManifest -Path $created.Path).Valid "restored manifest verifies again"

# ---- human evidence: only recorded files, bound to the build
$shot = Join-Path $run 'human-skyrim.png'
[IO.File]::WriteAllBytes($shot, [byte[]](1, 2, 3, 4))
$threw = $false
try { Add-SnsHumanEvidence -Path $created.Path -File $shot -Observer 'Tester' -Result 'VERIFIED' } catch { $threw = $true }
Check $threw "only PASS_IN_GAME or FAIL_IN_GAME can be recorded"
Add-SnsHumanEvidence -Path $created.Path -File $shot -Observer 'Tester (synthetic)' -Result 'PASS_IN_GAME' -Notes 'synthetic test evidence'
$r = Test-SnsTestRunManifest -Path $created.Path
$after = Get-Content -LiteralPath $created.Path -Raw | ConvertFrom-Json
Check ($r.Valid -and $after.results.inGame -eq 'PASS_IN_GAME' -and @($after.humanEvidence).Count -eq 1) "recorded human evidence verifies: $(Problems $r)"
[IO.File]::WriteAllBytes($shot, [byte[]](9, 9, 9))
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'EvidenceChanged: human-skyrim.png')) "changed evidence is refused: $(Problems $r)"
[IO.File]::WriteAllBytes($shot, [byte[]](1, 2, 3, 4))
[IO.File]::WriteAllBytes($exe, [Text.Encoding]::ASCII.GetBytes('build three'))
$r = Test-SnsTestRunManifest -Path $created.Path
Check (-not $r.Valid -and (Has $r 'OtherBuild')) "in-game evidence does not certify a rebuilt binary: $(Problems $r)"
$threw = $false
try { Add-SnsHumanEvidence -Path $created.Path -File $shot -Observer 'Tester' -Result 'PASS_IN_GAME' } catch { $threw = $true }
Check $threw "evidence cannot be added to a manifest of another build"
[IO.File]::WriteAllBytes($exe, $original)

# ---- what makes a run fail
$run2 = Join-Path $root 'run-failed'
$mod2 = Join-Path $run2 'export\SNS_Bad'
New-Item -ItemType Directory -Force $mod2 | Out-Null
[IO.File]::WriteAllBytes((Join-Path $mod2 'SNS_Bad.esp'), [byte[]](1, 2))
$bad = $exportManifest
$bad.plugin = [ordered]@{ fileName = 'SNS_Bad.esp'; sha256 = $espHash }   # not the bytes on disk
$bad.inGameVerified = $true
Utf8 (Join-Path $mod2 'manifest.json') ($bad | ConvertTo-Json -Depth 5)
$log2 = Join-Path $run2 'smoke.log'
Utf8 $log2 ("stress: [    10 ms] PASS one`nstress: [    20 ms] FAIL the export refuses it`nstress: [    30 ms] summary: FAILED (1 scenario(s))`n")
$failed = (New-SnsTestRunManifest -RunDir $run2 -SmokeLog $log2 -SmokeExitCode 1 -BuildFiles $exe).Manifest
Check ($failed.results.windows -eq 'FAIL' -and $failed.smoke.fail -eq 1 -and $failed.smoke.failures[0] -eq 'the export refuses it') "a FAIL line fails the Windows result"
$codes = @($failed.problems) -join '; '
Check ($failed.results.static -eq 'FAIL' -and $codes -like '*InGameClaimWithoutEvidence*' -and $codes -like '*PluginHashMismatch*' -and $codes -like '*ZipMissing*') `
      "static checks: $codes"
Check ($failed.results.inGame -eq 'WAITING_HUMAN') "an export flag never sets the in-game result"

# ---- relative paths are resolved against the PowerShell location, not the process directory
Push-Location $root
$processDir = [Environment]::CurrentDirectory
[Environment]::CurrentDirectory = $build
try {
    $relative = (New-SnsTestRunManifest -RunDir 'run' -ProfileDir 'profile' -SmokeLog 'run/smoke.log' -SmokeExitCode 0 -BuildFiles 'build/SkyrimNpcStudio.exe').Manifest
    Check ($relative.smoke.pass -eq 2 -and $relative.results.windows -eq 'PASS_WINDOWS' -and @($relative.mo2.files).Count -eq 2 -and @($relative.build.files).Count -eq 1) `
          "relative paths: smoke $($relative.smoke.pass) PASS, windows $($relative.results.windows)"
} finally {
    [Environment]::CurrentDirectory = $processDir
    Pop-Location
}

# ---- a failure inside the module stops the caller instead of producing a manifest
$threw = $false
try { New-SnsTestRunManifest -RunDir (Join-Path $root 'no-such-run') | Out-Null } catch { $threw = $true }
Check $threw "a missing run folder is an error, not an empty manifest"
$unreadable = Join-Path $root 'locked.log'
Utf8 $unreadable "stress: [    1 ms] summary: PASSED (1 scenario(s))`n"
$lock = [IO.File]::Open($unreadable, 'Open', 'ReadWrite', 'None')
$threw = $false
try { New-SnsTestRunManifest -RunDir $run -SmokeLog $unreadable -BuildFiles $exe | Out-Null } catch { $threw = $true } finally { $lock.Dispose() }
Check $threw "an unreadable smoke log is an error, not a FAIL result"
Check (Test-SnsTestRunManifest -Path $created.Path).Valid "the refused runs left the earlier manifest untouched"

# ---- MO2 instance file (read only)
$ini = Join-Path $root 'ModOrganizer.ini'
Utf8 $ini "[General]`ngameName=Skyrim Special Edition`nselected_profile=@ByteArray(Default)`ngamePath=@ByteArray(D:\\Games\\Skyrim Special Edition)`n[Settings]`nbase_directory=F:/Mods Skyrim`n"
$mo2 = Read-SnsMO2Instance $ini ''
Check ($mo2.Profile -eq 'Default' -and $mo2.GameDir -eq 'D:\Games\Skyrim Special Edition' -and $mo2.ProfileDir -eq [IO.Path]::GetFullPath('F:/Mods Skyrim/profiles/Default')) `
      "ModOrganizer.ini: profile $($mo2.Profile), game $($mo2.GameDir), profile dir $($mo2.ProfileDir)"

Write-Host "$($script:checks) checks, $($script:failures.Count) failed"
if ($script:failures.Count -gt 0) { exit 1 }
exit 0
