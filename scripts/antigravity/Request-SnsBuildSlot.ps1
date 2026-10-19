<#
.SYNOPSIS
  Acquires an atomic lock for heavy compilation or testing to prevent resource starvation.
  Uses FileMode.CreateNew to ensure race-free acquisition across processes and agents.

.PARAMETER LockFile
  Path to the lock file. Defaults to .ai-coordination/locks/heavy-build.lock

.PARAMETER Reason
  Short description of the operation requesting the slot.

.PARAMETER TimeoutSec
  Maximum seconds to wait for lock acquisition. Default: 0 (immediate fail if locked).
#>
[CmdletBinding()]
param(
    [string]$LockFile,
    [string]$Reason = 'Antigravity QA Execution',
    [int]$TimeoutSec = 0
)

if (-not $LockFile) {
    $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Definition }
    $LockFile = Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) '.ai-coordination\locks\heavy-build.lock'
}

$lockDir = Split-Path -Parent $LockFile
if (-not (Test-Path -LiteralPath $lockDir)) {
    New-Item -ItemType Directory -Path $lockDir -Force | Out-Null
}

$startTime = [System.DateTime]::UtcNow
$acquired = $false
$fileStream = $null

while ($true) {
    try {
        $fileStream = [System.IO.File]::Open(
            $LockFile,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        $writer = [System.IO.StreamWriter]::new($fileStream, [System.Text.Encoding]::UTF8)
        $payload = @{
            Agent     = 'Antigravity'
            Pid       = $PID
            Machine   = $env:COMPUTERNAME
            AcquiredUtc = [System.DateTime]::UtcNow.ToString("o")
            Reason    = $Reason
        } | ConvertTo-Json -Compress
        $writer.WriteLine($payload)
        $writer.Flush()
        $acquired = $true
        break
    } catch [System.IO.IOException] {
        # File already exists
        if ($TimeoutSec -le 0) {
            break
        }
        $elapsed = ([System.DateTime]::UtcNow - $startTime).TotalSeconds
        if ($elapsed -ge $TimeoutSec) {
            break
        }
        Start-Sleep -Milliseconds 500
    } catch {
        Write-Error "Unexpected error attempting lock: $_"
        break
    }
}

if ($acquired) {
    # Store handle or path in environment/state for current session
    $global:SNS_BUILD_LOCK_STREAM = $fileStream
    Write-Host "Lock acquired successfully by Antigravity: $LockFile"
    exit 0
} else {
    $existingInfo = "unknown"
    if (Test-Path -LiteralPath $LockFile) {
        try {
            $existingInfo = (Get-Content -LiteralPath $LockFile -Raw -ErrorAction SilentlyContinue)
        } catch {}
    }
    Write-Warning "Lock busy. Held by: $existingInfo"
    exit 1
}
