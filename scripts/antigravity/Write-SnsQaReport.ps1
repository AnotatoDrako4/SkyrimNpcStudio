<#
.SYNOPSIS
  Generates offline, structured QA summary reports (JSON and Markdown) for an Antigravity QA run.
  Never fabricates results; reports exact exit codes, hashes, statuses and concurrent change invalidations.

.PARAMETER ManifestPath
  Path to the run manifest JSON file.

.PARAMETER OutDir
  Directory where summary.json and summary.md will be written. Defaults to manifest's parent directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "Manifest file not found: $ManifestPath"
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding utf8 | ConvertFrom-Json

if (-not $OutDir) {
    $OutDir = Split-Path -Parent $ManifestPath
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# Redaction helper for user sensitive paths
function Redact-SensitivePath([string]$text) {
    if ([string]::IsNullOrEmpty($text)) { return $text }
    $userProfile = [System.Environment]::GetFolderPath('UserProfile')
    if (-not [string]::IsNullOrEmpty($userProfile)) {
        $text = $text.Replace($userProfile, '%USERPROFILE%')
    }
    return $text
}

$summaryData = [ordered]@{
    RunId             = $manifest.RunId
    TimestampUtc      = $manifest.TimestampUtc
    Suite             = $manifest.Suite
    OverallStatus     = $manifest.OverallStatus
    DurationSeconds   = $manifest.DurationSeconds
    Agent             = "Antigravity"
    GitUsage          = "NO_GIT_PER_OWNER_RULE"
    SkyrimValidation  = "PENDING_SKYRIM"
    Counts            = @{
        Total   = $manifest.TestCases.Count
        Passed  = ($manifest.TestCases | Where-Object { $_.Status -eq 'PASS' }).Count
        Failed  = ($manifest.TestCases | Where-Object { $_.Status -eq 'FAIL' }).Count
        Skipped = ($manifest.TestCases | Where-Object { $_.Status -eq 'SKIP' }).Count
        Blocked = ($manifest.TestCases | Where-Object { $_.Status -eq 'BLOCKED' }).Count
        InvalidatedByConcurrentEdit = ($manifest.TestCases | Where-Object { $_.Status -eq 'INVALIDATED_BY_CONCURRENT_EDIT' }).Count
    }
    Toolchain         = $manifest.Toolchain
    ConcurrentChanges = $manifest.ConcurrentChanges
    TestCases         = $manifest.TestCases
}

$summaryJsonPath = Join-Path $OutDir 'summary.json'
$summaryJsonContent = $summaryData | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText($summaryJsonPath, $summaryJsonContent, [System.Text.Encoding]::UTF8)

# Markdown summary table
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("# Reporte de Laboratorio QA - Antigravity Run: " + $manifest.RunId)
[void]$sb.AppendLine("")
[void]$sb.AppendLine("- **Fecha UTC:** " + $manifest.TimestampUtc)
[void]$sb.AppendLine("- **Suite:** " + $manifest.Suite)
[void]$sb.AppendLine("- **Estado Global:** **" + $manifest.OverallStatus + "**")
[void]$sb.AppendLine("- **Duracion:** " + $manifest.DurationSeconds + " s")
[void]$sb.AppendLine("- **Control de Versiones:** NO UTILIZADO (Orden estricta del propietario)")
[void]$sb.AppendLine("- **Validacion In-Game Skyrim:** PENDIENTE_SKYRIM (Requiere validacion humana)")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Resumen de Casos")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("| Metrica | Cantidad |")
[void]$sb.AppendLine("|---|---|")
[void]$sb.AppendLine("| Total Casos | " + $summaryData.Counts.Total + " |")
[void]$sb.AppendLine("| PASS | " + $summaryData.Counts.Passed + " |")
[void]$sb.AppendLine("| FAIL | " + $summaryData.Counts.Failed + " |")
[void]$sb.AppendLine("| SKIP | " + $summaryData.Counts.Skipped + " |")
[void]$sb.AppendLine("| BLOCKED | " + $summaryData.Counts.Blocked + " |")
[void]$sb.AppendLine("| INVALIDATED_BY_CONCURRENT_EDIT | " + $summaryData.Counts.InvalidatedByConcurrentEdit + " |")
[void]$sb.AppendLine("")

if ($null -ne $manifest.ConcurrentChanges -and $manifest.ConcurrentChanges.HasDifferences) {
    [void]$sb.AppendLine("### [ADVERTENCIA] Modificacion Concurrente Detectada")
    [void]$sb.AppendLine("Se detectaron modificaciones en las fuentes de entrada durante la corrida de prueba:")
    foreach ($f in $manifest.ConcurrentChanges.ChangedFiles) {
        [void]$sb.AppendLine("- Modificado: " + $f)
    }
    foreach ($f in $manifest.ConcurrentChanges.AddedFiles) {
        [void]$sb.AppendLine("- Agregado: " + $f)
    }
    foreach ($f in $manifest.ConcurrentChanges.RemovedFiles) {
        [void]$sb.AppendLine("- Eliminado: " + $f)
    }
    [void]$sb.AppendLine("")
}

[void]$sb.AppendLine("## Detalle de Casos de Prueba")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("| Caso / Test | Estado | Exit Code | Duracion (ms) | Archivo / Comando | Motivo / Evidencia |")
[void]$sb.AppendLine("|---|---|---|---|---|---|")

foreach ($tc in $manifest.TestCases) {
    $redactedCmd = Redact-SensitivePath $tc.Target
    $codeCmd = '`' + $redactedCmd + '`'
    $row = '| ' + $tc.Name + ' | **' + $tc.Status + '** | ' + $tc.ExitCode + ' | ' + $tc.DurationMs + ' | ' + $codeCmd + ' | ' + $redactedDetails + ' |'
    [void]$sb.AppendLine($row)
}

$summaryMdPath = Join-Path $OutDir 'summary.md'
[System.IO.File]::WriteAllText($summaryMdPath, $sb.ToString(), [System.Text.Encoding]::UTF8)

Write-Host "QA Report generated successfully in: $OutDir"
Write-Host "  - JSON: $summaryJsonPath"
Write-Host "  - MD:   $summaryMdPath"

