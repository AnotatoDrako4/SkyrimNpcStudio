<#
.SYNOPSIS
  Releases the atomic build slot acquired by Antigravity.
  Ensures that handles are disposed and file is deleted safely.
#>
[CmdletBinding()]
param(
    [string]$LockFile
)

if (-not $LockFile) {
    $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Definition }
    $LockFile = Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) '.ai-coordination\locks\heavy-build.lock'
}

if ($null -ne $global:SNS_BUILD_LOCK_STREAM) {
    try {
        $global:SNS_BUILD_LOCK_STREAM.Dispose()
    } catch {}
    $global:SNS_BUILD_LOCK_STREAM = $null
}

if (Test-Path -LiteralPath $LockFile) {
    try {
        # Check if the lock belongs to Antigravity before deleting
        $content = Get-Content -LiteralPath $LockFile -Raw -ErrorAction SilentlyContinue
        if ($content -match '"Agent"\s*:\s*"Antigravity"') {
            Remove-Item -LiteralPath $LockFile -Force -ErrorAction Stop
            Write-Host "Lock released successfully by Antigravity: $LockFile"
            exit 0
        } else {
            Write-Warning "Lock file was not created by Antigravity; refusing to delete: $content"
            exit 1
        }
    } catch {
        Write-Error "Error releasing lock file: $_"
        exit 1
    }
} else {
    Write-Host "No lock file to release at: $LockFile"
    exit 0
}
