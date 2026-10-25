function Resolve-CMakeNinjaCache {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$BuildDirectory,

        [Parameter(Mandatory)]
        [string]$ActiveNinja
    )

    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    $result = [ordered]@{
        CacheRemoved = $false
        CachedNinja = $null
        ActiveNinja = [IO.Path]::GetFullPath($ActiveNinja)
        Reason = 'cache-missing'
    }

    if (-not (Test-Path -LiteralPath $cache)) {
        return [pscustomobject]$result
    }

    $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$' |
        Select-Object -First 1
    if (-not $line) {
        $result.Reason = 'cache-has-no-make-program'
        return [pscustomobject]$result
    }

    $cached = $line.Matches[0].Groups[1].Value -replace '/', '\'
    $result.CachedNinja = $cached
    $active = [IO.Path]::GetFullPath($ActiveNinja)

    $same = $false
    try {
        $same = [IO.Path]::GetFullPath($cached).Equals($active, [StringComparison]::OrdinalIgnoreCase)
    }
    catch {
        $same = $false
    }

    if ($same -and (Test-Path -LiteralPath $cached)) {
        $result.Reason = 'cache-matches-active-ninja'
        return [pscustomobject]$result
    }

    Remove-Item -LiteralPath $cache -Force
    $result.CacheRemoved = $true
    $result.Reason = if (Test-Path -LiteralPath $cached) { 'cached-ninja-differs' } else { 'cached-ninja-missing' }
    return [pscustomobject]$result
}
