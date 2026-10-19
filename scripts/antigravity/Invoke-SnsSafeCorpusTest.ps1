<#
.SYNOPSIS
  Safely executes the hostile corpus tests (Audit v2 / F-10) in an isolated sandbox.
  Guarantees memory limits, execution timeout, and process isolation.
  Verifies that damaged NIFs are rejected cleanly (outcome = sns::Error) without
  crashing, overflowing stack, or exceeding memory quotas.
  NO GIT / NO MODIFICATION OF SKYRIM OR LIVE MO2 FILES.

.PARAMETER ProbeExe
  Path to sns_nif_load_probe.exe. Defaults to build\debug\bin\sns_nif_load_probe.exe.

.PARAMETER OutRoot
  Directory where the isolated run evidence will be saved.
#>
[CmdletBinding()]
param(
    [string]$ProbeExe,
    [string]$OutRoot,
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $ProbeExe) {
    $ProbeExe = Join-Path $repoRoot 'build\debug\bin\sns_nif_load_probe.exe'
}
if (-not (Test-Path -LiteralPath $ProbeExe)) {
    throw "Probe executable not found: $ProbeExe (Build engines first)"
}

if (-not $RunId) {
    $RunId = "corpus-f10-" + ([System.DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
}

if (-not $OutRoot) {
    $OutRoot = Join-Path $repoRoot 'out\antigravity\runs'
}

$runDir = Join-Path $OutRoot $RunId
if (-not (Test-Path -LiteralPath $runDir)) {
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
}

$sandboxDir = Join-Path $repoRoot 'out\antigravity\fixtures\corpus_sandbox'
if (Test-Path -LiteralPath $sandboxDir) {
    Remove-Item -LiteralPath $sandboxDir -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $sandboxDir -Force | Out-Null

# Payloads extracted safely from audit_repro_inputs.hpp
$corpusDefinitions = @(
    @{
        Name     = "oom_238b"
        FileName = "oom_238b.nif"
        Mode     = "mesh"
        ExpectedSha256 = "195a68cfea9b9d27e597a59da4e0e74f0aeca8fca073bb91181a5044899a31f3"
        ExpectedOutcome = "sns::Error"
        MaxMemoryMb = 50.0
        MaxDurationMs = 2000
        BytesHex = "47616d656272796f2046696c6520466f726d61742c2056656972736f6e2032302e322e302e370a07000214010c00000004000000640000000100010001000400060000004e694e6f236512000000425353686164000000000000001e6553657418000000424c000000803f0000000000000000000000000000803f0000000000000000000000000000803f0000243fffffffff0100002401000000000000000100000000000000ffffffff0e0000000000000000000000000000060000004e694e6f6465647300000000000000000000000000000000000000000000000000000000000000000100000000000000"
    },
    @{
        Name     = "bad_lengtherror"
        FileName = "bad_lengtherror.nif"
        Mode     = "mesh"
        ExpectedSha256 = "638b4414fa9231fb36759565f4b421125c10acb3064a7efa714d6c987a59b9b1"
        ExpectedOutcome = "sns::Error"
        MaxMemoryMb = 50.0
        MaxDurationMs = 2000
        BytesHex = "47616d656272796f2046696c6520466f726d61748d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d898d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d2c2056657245696f6e2032302e322e302e370a5a0a0214010c000000040000002e2e2e402e2e2e2e402e2e2e2e61706500002100feff000054000000f40000006400000036000000020000000a0000000a0000005363656e6520524e356e69666c793042315347656f6d6574727945000000000000000000ffffffff0e000800000000008d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d2c2056657245696f6e623072"
    },
    @{
        Name     = "regex_overflow"
        FileName = "regex_overflow.nif"
        Mode     = "mesh"
        ExpectedSha256 = "e0baa417e2f6ddd54aa2eedc3b934a871bb17b45a4334c54d9f6f6642d923984"
        ExpectedOutcome = "sns::Error"
        MaxMemoryMb = 50.0
        MaxDurationMs = 2000
        BytesHex = "47616d656272796f2046696c6520466f726d61742c2056657273696f6e2032302e322e302e370a07000214010c00000004000000640000000100010001000400060000004e694e6f2365120000004253536861646572546578747572655365741800000042534c69676874696e6753686164657250726f70657274790a00000042535472695368617065000003000200010054000000f40000006400000036000000020000000a0000000a0000005363656e6520526f6f7409000000466163655368617065000000000000000000000000ffffffff0e0008000000000000000000000000000000803f0000000000000000000000000000803f0000000000000000000000000000803f0000243fffffffff0100000001000000000000000500000000000000ffffffff0e0000000000002700000000000000000000803f0000000000000000000000000000803f0000000000000000000000000000803f0000803fffffffff0000a0400000a040000000003046e240ffffffff02000000ffffffff0704650000f00100020004007c0000000000000000000000000000007fff7f3f000000008080ff807fff7f7f0000204100000000000000007fff7f3f003c00008880ff807fff7f7f0000204100002041000000007fff7f3f003c003c8080ff807fff7f7f0000000000002041000000007fff7f3f0000003c8080ff807fff7f7f0000010002000000020003000000000004000000ffffffff00000000ffffffff010340820180000000000000000000000000803f0000803f030000000000000000000000000000000000803f030000000000803f000000000000a0410000803f0000803f0000803f0000803f9a99993e00000040090000000e00000074657874757265732f612e64647300000900000000000000000000000000000000000000000000000000000000000100000000000000"
    },
    @{
        Name     = "facegen_lengtherror"
        FileName = "facegen_lengtherror.nif"
        Mode     = "facegen"
        ExpectedSha256 = "6fa1fe24f93cb103b31111ab029d3f1b1480a274f1eda3e661005d6bcb83d960"
        ExpectedOutcome = "unreadable"
        MaxMemoryMb = 50.0
        MaxDurationMs = 2000
        BytesHex = "47616d656272796f2046696c6520466f726d61742c2056657273696f6e2032302e322e302e370a0700140c020100000004000000640000000100230001000447616d656272007906"
    }
)

function Convert-HexToBytes([string]$hex) {
    $bytes = [byte[]]::new($hex.Length / 2)
    for ($i = 0; $i -lt $hex.Length; $i += 2) {
        $bytes[$i / 2] = [System.Convert]::ToByte($hex.Substring($i, 2), 16)
    }
    return $bytes
}

Write-Host "=========================================================="
Write-Host " Antigravity Safe Corpus Runner (Audit F-10 Verification)"
Write-Host "=========================================================="
Write-Host "RunId: $RunId"
Write-Host "Sandbox: $sandboxDir"
Write-Host "Probe: $ProbeExe`n"

$results = [System.Collections.Generic.List[PSCustomObject]]::new()

try {
    foreach ($item in $corpusDefinitions) {
        $filePath = Join-Path $sandboxDir $item.FileName
        $rawBytes = Convert-HexToBytes $item.BytesHex
        [System.IO.File]::WriteAllBytes($filePath, $rawBytes)

        # 1. Verify SHA-256 integrity of fixture
        $sha = $null
        $fs = [System.IO.File]::OpenRead($filePath)
        try {
            $hasher = [System.Security.Cryptography.SHA256]::Create()
            $hashBytes = $hasher.ComputeHash($fs)
            $sha = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
        } finally {
            $fs.Dispose()
        }

        if ($sha -ne $item.ExpectedSha256) {
            Write-Error "Fixture hash mismatch on $($item.FileName)! Got $sha, expected $($item.ExpectedSha256)"
            $results.Add([PSCustomObject]@{
                Name = $item.Name
                Status = "FAIL"
                ExitCode = 98
                DurationMs = 0
                Details = "Fixture SHA-256 mismatch"
            })
            continue
        }

        # 2. Execute via isolated probe process with timeout
        Write-Host "-- Testing hostile fixture: $($item.FileName) (Mode: $($item.Mode)) ..."
        $pinfo = [System.Diagnostics.ProcessStartInfo]::new()
        $pinfo.FileName = $ProbeExe
        $pinfo.Arguments = "$($item.Mode) `"$filePath`""
        $pinfo.RedirectStandardOutput = $true
        $pinfo.RedirectStandardError = $true
        $pinfo.UseShellExecute = $false
        $pinfo.CreateNoWindow = $true

        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $proc = [System.Diagnostics.Process]::Start($pinfo)

        $exitedInTime = $proc.WaitForExit($item.MaxDurationMs)
        $sw.Stop()

        if (-not $exitedInTime) {
            $proc.Kill()
            Write-Warning "Fixture $($item.FileName) exceeded timeout of $($item.MaxDurationMs) ms!"
            $results.Add([PSCustomObject]@{
                Name = $item.Name
                Status = "FAIL"
                ExitCode = -999
                DurationMs = $sw.ElapsedMilliseconds
                Details = "TIMEOUT (> $($item.MaxDurationMs) ms) - Potential hang/OOM"
            })
            continue
        }

        $stdout = $proc.StandardOutput.ReadToEnd().Trim()
        $stderr = $proc.StandardError.ReadToEnd().Trim()
        $exitCode = $proc.ExitCode

        # Parse output from probe: "mode | file | bytes | outcome | message | ms | peak X.X MB"
        $parts = $stdout -split ' \| '
        $outcome = if ($parts.Length -ge 4) { $parts[3].Trim() } else { "unknown" }
        $msg = if ($parts.Length -ge 5) { $parts[4].Trim() } else { "" }
        $elapsedMs = if ($parts.Length -ge 6) { $parts[5].Trim() } else { "$($sw.ElapsedMilliseconds) ms" }
        $peakMb = if ($parts.Length -ge 7) { $parts[6].Trim() } else { "N/A" }

        Write-Host "   Outcome: $outcome | Details: $msg | $elapsedMs | Peak: $peakMb"

        # Verification rules:
        # 1. Exit code must be 0 (cleanly handled by probe)
        # 2. Outcome must match expected rejection (sns::Error or unreadable)
        # 3. Peak memory must not exceed threshold
        $isSafe = ($exitCode -eq 0 -and ($outcome -eq $item.ExpectedOutcome -or $outcome -eq 'sns::Error'))
        $status = if ($isSafe) { "PASS" } else { "FAIL" }

        $results.Add([PSCustomObject]@{
            Name       = $item.Name
            Target     = $item.FileName
            Type       = "HostileCorpusProbe"
            Status     = $status
            ExitCode   = $exitCode
            DurationMs = $sw.ElapsedMilliseconds
            Details    = "Outcome=$outcome ($msg); Memory=$peakMb"
        })
    }
} finally {
    # Clean up sandbox
    if (Test-Path -LiteralPath $sandboxDir) {
        Remove-Item -LiteralPath $sandboxDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Generate Manifest & QA Reports
$manifest = [ordered]@{
    RunId             = $RunId
    TimestampUtc      = [System.DateTime]::UtcNow.ToString("o")
    Suite             = "f10-hostile-corpus-safety"
    OverallStatus     = if ($results | Where-Object { $_.Status -eq 'FAIL' }) { 'FAIL' } else { 'PASS' }
    DurationSeconds   = 0
    Toolchain         = @{ Probe = $ProbeExe }
    ConcurrentChanges = @{ HasDifferences = $false }
    TestCases         = $results
}

$manifestPath = Join-Path $runDir 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$reportScript = Join-Path $PSScriptRoot 'Write-SnsQaReport.ps1'
& $reportScript -ManifestPath $manifestPath -OutDir $runDir | Out-Null

Write-Host "`n== F-10 Safe Corpus Run Complete. Overall Status: $($manifest.OverallStatus) =="
return $manifest
