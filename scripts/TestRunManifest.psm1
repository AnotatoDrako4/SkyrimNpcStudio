# TestRunManifest (D3/D22): which build, MO2 profile, Skyrim version and artifacts a test run used, what the
# automatic checks found, and a verifier that refuses evidence from another build, a changed profile,
# changed or missing artifacts, or an altered manifest.
#
# Nothing here decides an in-game result: results.inGame stays WAITING_HUMAN until a person records
# evidence with Add-SnsHumanEvidence (screenshots or notes, hashed and bound to the build). Git is not
# queried; the build is identified by the SHA-256 of its binaries. The sidecar hash protects the
# manifest's integrity, not its authorship: it detects edits made without the tool.

Set-StrictMode -Version 3.0
# A module does not inherit the caller's preference: every failure here must stop the run, never yield a
# manifest built from a file that could not be read.
$ErrorActionPreference = 'Stop'

$script:Format = 'skyrim-npc-studio/test-run'
$script:Schema = 1
$script:ManifestName = 'test-run-manifest.json'
$script:InGameStates = @('NOT_RUN', 'WAITING_HUMAN', 'PASS_IN_GAME', 'FAIL_IN_GAME')

# Paths are resolved against the PowerShell location; .NET file APIs would use the process directory.
function Resolve-SnsPath([string]$Path) {
    if (-not $Path) { return $Path }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Get-SnsSha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Property of a JSON object, or $null when it is absent (StrictMode refuses missing properties).
function Get-SnsProperty($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $p = $Object.PSObject.Properties[$Name]
    if ($p) { return $p.Value }
    return $null
}

function Write-SnsJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
}

# ModOrganizer.ini is only read. Values may be wrapped in @ByteArray(...) with doubled backslashes.
function Read-SnsMO2Instance([string]$Instance, [string]$ProfileName) {
    $Instance = Resolve-SnsPath $Instance
    $values = @{}
    foreach ($line in [IO.File]::ReadAllLines($Instance)) {
        if ($line -match '^([A-Za-z_]+)=(.*)$') {
            $key = $Matches[1]
            $value = $Matches[2]
            if ($value -match '^@ByteArray\((.*)\)$') { $value = $Matches[1] }   # (overwrites $Matches)
            $values[$key] = $value.Replace('\\', '\')
        }
    }
    $base = if ($values.ContainsKey('base_directory')) { $values['base_directory'] } else { Split-Path -Parent $Instance }
    $profiles = if ($values.ContainsKey('profiles_directory')) { $values['profiles_directory'].Replace('%BASE_DIR%', $base) } else { Join-Path $base 'profiles' }
    $name = if ($ProfileName) { $ProfileName } elseif ($values.ContainsKey('selected_profile')) { $values['selected_profile'] } else { 'Default' }
    [pscustomobject]@{
        Profile    = $name
        ProfileDir = [IO.Path]::GetFullPath((Join-Path $profiles $name))
        GameDir    = if ($values.ContainsKey('gamePath')) { $values['gamePath'] } else { $null }
    }
}

function Get-SnsProfileFacts([string]$ProfileDir) {
    $ProfileDir = Resolve-SnsPath $ProfileDir
    if (-not $ProfileDir -or -not (Test-Path -LiteralPath $ProfileDir)) {
        return [ordered]@{ profileDir = $ProfileDir; found = $false; files = @(); enabledMods = @(); activePlugins = @() }
    }
    $files = foreach ($name in 'modlist.txt', 'plugins.txt', 'loadorder.txt', 'archives.txt') {
        $path = Join-Path $ProfileDir $name
        if (Test-Path -LiteralPath $path) { [ordered]@{ name = $name; sha256 = Get-SnsSha256 $path } }
    }
    $lines = {
        param([string]$Name, [string]$Prefix)
        $path = Join-Path $ProfileDir $Name
        if (-not (Test-Path -LiteralPath $path)) { return @() }
        @([IO.File]::ReadAllLines($path) | Where-Object { $_.StartsWith($Prefix) } | ForEach-Object { $_.Substring(1) })
    }
    [ordered]@{
        profileDir    = [IO.Path]::GetFullPath($ProfileDir)
        found         = $true
        files         = @($files)
        enabledMods   = @(& $lines 'modlist.txt' '+')   # MO2 order: highest priority first
        activePlugins = @(& $lines 'plugins.txt' '*')
    }
}

function Get-SnsSkyrimFacts([string]$GameDir) {
    if (-not $GameDir) { return [ordered]@{ detected = $false; reason = 'no game folder given' } }
    $GameDir = Resolve-SnsPath $GameDir
    $exe = Join-Path $GameDir 'SkyrimSE.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return [ordered]@{ detected = $false; gameDir = $GameDir; reason = 'SkyrimSE.exe not found' } }
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    [ordered]@{
        detected       = $true
        exe            = $exe
        fileVersion    = $info.FileVersion
        productVersion = $info.ProductVersion
        sha256         = Get-SnsSha256 $exe
    }
}

# stress lines of smoke-test.ps1 logs: "stress: [   12 ms] PASS what", "... FAIL what", "... summary: PASSED (n scenario(s))".
function Read-SnsSmokeLog([string]$Path) {
    $out = [ordered]@{ found = $false; pass = 0; fail = 0; summary = $null; failures = @(); npc = $null; scenarios = @() }
    $Path = Resolve-SnsPath $Path
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $out }
    $out.found = $true
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^stress: \[\s*\d+ ms\] PASS ') { $out.pass++ }
        elseif ($line -match '^stress: \[\s*\d+ ms\] FAIL (.*)$') { $out.fail++; $failures.Add($Matches[1]) }
        elseif ($line -match '^stress: \[\s*\d+ ms\] summary: (PASSED|FAILED)') { $out.summary = $Matches[1] }
        elseif ($line -match '^stress: \[\s*\d+ ms\] start on (.+?) \(.*\), scenarios: (.*)$') {
            $out.npc = $Matches[1]
            $out.scenarios = @($Matches[2].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        }
    }
    $out.failures = @($failures)
    $out
}

function Get-SnsRelativePath([string]$Root, [string]$Path) {
    [IO.Path]::GetRelativePath($Root, $Path).Replace('\', '/')
}

# Mods written by the studio (manifest.json with format skyrim-npc-studio/export) and their ZIPs.
function Get-SnsExports([string]$Root) {
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Filter 'manifest.json') {
        $relative = Get-SnsRelativePath $Root $file.FullName
        try {
            $m = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        } catch {
            [ordered]@{ manifest = $relative; error = "unreadable manifest: $($_.Exception.Message)" }
            continue
        }
        if ((Get-SnsProperty $m 'format') -ne 'skyrim-npc-studio/export') { continue }
        $plugin = Get-SnsProperty $m 'plugin'
        $pluginName = Get-SnsProperty $plugin 'fileName'
        $pluginPath = if ($pluginName) { Join-Path $file.DirectoryName $pluginName } else { $null }
        $zip = Join-Path $file.Directory.Parent.FullName ($file.Directory.Name + '.zip')
        $faces = @(Get-SnsProperty $m 'faces')
        $npcs = @($faces | ForEach-Object { Get-SnsProperty $_ 'npc' }) + @(@(Get-SnsProperty $m 'assignments') | ForEach-Object { Get-SnsProperty $_ 'npc' })
        [ordered]@{
            manifest       = $relative
            mode           = Get-SnsProperty $m 'mode'
            plugin         = $pluginName
            pluginSha256   = "$(Get-SnsProperty $plugin 'sha256')".ToLowerInvariant()
            pluginOnDisk   = if ($pluginPath -and (Test-Path -LiteralPath $pluginPath)) { Get-SnsSha256 $pluginPath } else { $null }
            npcs           = @($npcs | Where-Object { $_ } | Select-Object -Unique)
            loadAfter      = @(Get-SnsProperty $m 'loadAfter')
            installAbove   = @($faces | ForEach-Object { @(Get-SnsProperty $_ 'installAbove') } | Where-Object { $_ } | Select-Object -Unique)
            inGameVerified = [bool](Get-SnsProperty $m 'inGameVerified')
            readyForGame   = [bool](Get-SnsProperty $m 'readyForGame')
            zip            = if (Test-Path -LiteralPath $zip) { Get-SnsRelativePath $Root $zip } else { $null }
            zipSha256      = if (Test-Path -LiteralPath $zip) { Get-SnsSha256 $zip } else { $null }
        }
    }
}

function Get-SnsArtifacts([string]$Root) {
    Get-ChildItem -LiteralPath $Root -Recurse -File |
        Where-Object { $_.Name -ne $script:ManifestName -and $_.Name -ne "$($script:ManifestName).sha256" } |
        Sort-Object FullName |
        ForEach-Object { [ordered]@{ path = Get-SnsRelativePath $Root $_.FullName; bytes = $_.Length; sha256 = Get-SnsSha256 $_.FullName } }
}

function Save-SnsManifest([string]$Path, $Manifest) {
    Write-SnsJson $Path $Manifest
    $hash = Get-SnsSha256 $Path
    [IO.File]::WriteAllText("$Path.sha256", "$hash  $(Split-Path -Leaf $Path)`n", [Text.UTF8Encoding]::new($false))
    $hash
}

function New-SnsTestRunManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RunDir,
        [string]$MO2Instance,
        [string]$MO2Profile,
        [string]$ProfileDir,
        [string]$GameDir,
        [string[]]$Npc = @(),
        [string[]]$Scenario = @(),
        [string]$SmokeLog,
        [Nullable[int]]$SmokeExitCode,
        [string[]]$BuildFiles = @(),
        [string]$Notes
    )
    $root = (Resolve-Path -LiteralPath $RunDir).Path
    $SmokeLog = Resolve-SnsPath $SmokeLog
    $BuildFiles = @($BuildFiles | ForEach-Object { Resolve-SnsPath $_ })
    $profileName = $MO2Profile
    if ($MO2Instance) {
        $mo2 = Read-SnsMO2Instance $MO2Instance $MO2Profile
        if (-not $ProfileDir) { $ProfileDir = $mo2.ProfileDir }
        if (-not $GameDir) { $GameDir = $mo2.GameDir }
        $profileName = $mo2.Profile
    }
    $smoke = Read-SnsSmokeLog $SmokeLog
    $exports = @(Get-SnsExports $root)
    $problems = [Collections.Generic.List[string]]::new()
    foreach ($e in $exports) {
        if ($e.Contains('error')) { $problems.Add("ExportManifestUnreadable: $($e.manifest)"); continue }
        if ($e.inGameVerified -or $e.readyForGame) { $problems.Add("InGameClaimWithoutEvidence: $($e.manifest)") }
        if (-not $e.pluginOnDisk -or $e.pluginOnDisk -ne $e.pluginSha256) { $problems.Add("PluginHashMismatch: $($e.manifest)") }
        if (-not $e.zip) { $problems.Add("ZipMissing: $($e.manifest)") }
    }
    $build = foreach ($f in $BuildFiles) {
        if (Test-Path -LiteralPath $f) {
            $full = (Resolve-Path -LiteralPath $f).Path
            [ordered]@{ name = Split-Path -Leaf $full; path = $full; sha256 = Get-SnsSha256 $full }
        } else {
            $problems.Add("BuildFileMissing: $f")
        }
    }
    $windows = if (-not $smoke.found) { 'NOT_RUN' }
               elseif ($smoke.fail -gt 0 -or $smoke.summary -ne 'PASSED' -or ($null -ne $SmokeExitCode -and $SmokeExitCode -ne 0)) { 'FAIL' }
               else { 'PASS_WINDOWS' }
    $static = if ($exports.Count -eq 0) { 'NOT_RUN' } elseif (@($problems | Where-Object { $_ -notlike 'BuildFileMissing*' }).Count -gt 0) { 'FAIL' } else { 'PASS_STATIC' }
    $profile = Get-SnsProfileFacts $ProfileDir
    $manifest = [ordered]@{
        format        = $script:Format
        schema        = $script:Schema
        createdAt     = [DateTimeOffset]::Now.ToString('o')
        runDir        = $root
        build         = [ordered]@{
            files = @($build)
            git   = [ordered]@{ sha = $null; note = 'Not collected: Git work is on hold by explicit order; the build is identified by the SHA-256 of its binaries.' }
        }
        skyrim        = Get-SnsSkyrimFacts $GameDir
        mo2           = [ordered]@{
            instance      = $MO2Instance
            profile       = $profileName
            profileDir    = $profile.profileDir
            found         = $profile.found
            files         = $profile.files
            enabledMods   = $profile.enabledMods
            activePlugins = $profile.activePlugins
        }
        npcs          = @(if ($Npc.Count) { $Npc } elseif ($smoke.npc) { $smoke.npc })
        scenarios     = @(if ($Scenario.Count) { $Scenario } else { $smoke.scenarios })
        smoke         = [ordered]@{
            log      = if ($SmokeLog -and (Test-Path -LiteralPath $SmokeLog)) { (Resolve-Path -LiteralPath $SmokeLog).Path } else { $SmokeLog }
            exitCode = $SmokeExitCode
            pass     = $smoke.pass
            fail     = $smoke.fail
            summary  = $smoke.summary
            failures = $smoke.failures
        }
        exports       = $exports
        artifacts     = @(Get-SnsArtifacts $root)
        results       = [ordered]@{ static = $static; windows = $windows; inGame = 'WAITING_HUMAN' }
        humanEvidence = @()
        problems      = @($problems)
        notes         = $Notes
    }
    $path = Join-Path $root $script:ManifestName
    $hash = Save-SnsManifest $path $manifest
    [pscustomobject]@{ Path = $path; Sha256 = $hash; Manifest = $manifest }
}

# A person records what they saw in Skyrim: files (screenshots, notes) are hashed and bound to the build of
# the manifest. Only PASS_IN_GAME / FAIL_IN_GAME are accepted, and only with at least one evidence file.
function Add-SnsHumanEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$File,
        [Parameter(Mandatory = $true)][string]$Observer,
        [Parameter(Mandatory = $true)][ValidateSet('PASS_IN_GAME', 'FAIL_IN_GAME')][string]$Result,
        [string]$Notes
    )
    $check = Test-SnsTestRunManifest -Path $Path
    if (-not $check.Valid) { throw "The manifest does not match its build, profile or files: $($check.Problems -join '; ')" }
    $full = (Resolve-Path -LiteralPath $Path).Path
    $root = Split-Path -Parent $full
    $m = Get-Content -LiteralPath $full -Raw -Encoding utf8 | ConvertFrom-Json
    $files = foreach ($f in $File) {
        $evidence = (Resolve-Path -LiteralPath $f).Path
        [ordered]@{ path = Get-SnsRelativePath $root $evidence; sha256 = Get-SnsSha256 $evidence }
    }
    $entry = [ordered]@{
        recordedAt  = [DateTimeOffset]::Now.ToString('o')
        observer    = $Observer
        result      = $Result
        notes       = $Notes
        buildSha256 = @($m.build.files | ForEach-Object { $_.sha256 })
        files       = @($files)
    }
    $m.humanEvidence = @(@($m.humanEvidence) + $entry)
    $m.results.inGame = $Result
    Save-SnsManifest $full $m | Out-Null
}

function Test-SnsTestRunManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        # Binaries to compare with (default: the paths recorded in the manifest, as they are now).
        [string[]]$CurrentBuildFiles = @()
    )
    $problems = [Collections.Generic.List[string]]::new()
    $full = (Resolve-Path -LiteralPath $Path).Path
    $root = Split-Path -Parent $full
    $sidecar = "$full.sha256"
    if (-not (Test-Path -LiteralPath $sidecar)) {
        $problems.Add('ManifestHashMissing')
    } elseif (([IO.File]::ReadAllText($sidecar).Trim() -split '\s+')[0] -ne (Get-SnsSha256 $full)) {
        $problems.Add('ManifestAltered')
    }
    $m = Get-Content -LiteralPath $full -Raw -Encoding utf8 | ConvertFrom-Json
    if ((Get-SnsProperty $m 'format') -ne $script:Format -or (Get-SnsProperty $m 'schema') -ne $script:Schema) {
        $problems.Add('UnknownFormat')
        return [pscustomobject]@{ Valid = $false; Problems = @($problems) }
    }

    # Evidence of one build never certifies another.
    $current = @{}
    foreach ($f in $CurrentBuildFiles) { $current[(Split-Path -Leaf $f).ToLowerInvariant()] = Resolve-SnsPath $f }
    $recordedBuild = @($m.build.files)
    if ($recordedBuild.Count -eq 0) { $problems.Add('BuildNotRecorded') }
    foreach ($b in $recordedBuild) {
        $now = if ($current.ContainsKey($b.name.ToLowerInvariant())) { $current[$b.name.ToLowerInvariant()] } else { $b.path }
        if (-not (Test-Path -LiteralPath $now)) { $problems.Add("BuildFileMissing: $($b.name)") }
        elseif ((Get-SnsSha256 $now) -ne $b.sha256) { $problems.Add("OtherBuild: $($b.name)") }
    }

    # The MO2 profile the run used.
    if ($m.mo2.profileDir) {
        if (-not (Test-Path -LiteralPath $m.mo2.profileDir)) { $problems.Add('ProfileMissing') }
        foreach ($f in @($m.mo2.files)) {
            $p = Join-Path $m.mo2.profileDir $f.name
            if (-not (Test-Path -LiteralPath $p)) { $problems.Add("ProfileChanged: $($f.name) missing") }
            elseif ((Get-SnsSha256 $p) -ne $f.sha256) { $problems.Add("ProfileChanged: $($f.name)") }
        }
    }

    foreach ($a in @($m.artifacts)) {
        $p = Join-Path $root $a.path
        if (-not (Test-Path -LiteralPath $p)) { $problems.Add("ArtifactMissing: $($a.path)") }
        elseif ((Get-SnsSha256 $p) -ne $a.sha256) { $problems.Add("ArtifactChanged: $($a.path)") }
    }
    foreach ($e in @($m.exports)) {
        if ((Get-SnsProperty $e 'inGameVerified') -or (Get-SnsProperty $e 'readyForGame')) { $problems.Add("InGameClaimWithoutEvidence: $($e.manifest)") }
    }

    # In-game results only through recorded human evidence for this very build.
    $inGame = $m.results.inGame
    $evidence = @($m.humanEvidence)
    if ($inGame -notin $script:InGameStates) { $problems.Add("UnknownInGameState: $inGame") }
    if ($inGame -in 'PASS_IN_GAME', 'FAIL_IN_GAME' -and $evidence.Count -eq 0) { $problems.Add('InGameResultWithoutEvidence') }
    $buildHashes = @($recordedBuild | ForEach-Object { $_.sha256 }) -join ','
    foreach ($h in $evidence) {
        if ((@($h.buildSha256) -join ',') -ne $buildHashes) { $problems.Add("EvidenceFromAnotherBuild: $($h.recordedAt)") }
        if (@($h.files).Count -eq 0) { $problems.Add("EvidenceWithoutFiles: $($h.recordedAt)") }
        foreach ($f in @($h.files)) {
            $p = Join-Path $root $f.path
            if (-not (Test-Path -LiteralPath $p)) { $problems.Add("EvidenceMissing: $($f.path)") }
            elseif ((Get-SnsSha256 $p) -ne $f.sha256) { $problems.Add("EvidenceChanged: $($f.path)") }
        }
    }
    [pscustomobject]@{ Valid = ($problems.Count -eq 0); Problems = @($problems) }
}

Export-ModuleMember -Function New-SnsTestRunManifest, Test-SnsTestRunManifest, Add-SnsHumanEvidence, Read-SnsMO2Instance, Read-SnsSmokeLog
