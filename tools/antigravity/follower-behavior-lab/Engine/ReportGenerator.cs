using System.Net;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

public sealed record ArtifactHashEntry(string Path, long SizeBytes, string Sha256, string Kind);

public sealed record Sha256ManifestDto(
    string SchemaVersion,
    string GeneratedAtUtc,
    IReadOnlyList<ArtifactHashEntry> Artifacts);

/// <summary>
/// Generates navigable static HTML reports and SHA-256 cryptographic manifests from real analysis JSON files.
/// Strictly escapes all user/record strings via WebUtility.HtmlEncode (S04 XSS defense).
/// </summary>
public static class ReportGenerator
{
    public static string GenerateHtmlFromVerdict(BehaviorVerdictReport verdict, string? sourceJsonPath = null)
    {
        ArgumentNullException.ThrowIfNull(verdict);
        var sb = new StringBuilder(16384);

        sb.AppendLine("<!DOCTYPE html>");
        sb.AppendLine("<html lang=\"es\">");
        sb.AppendLine("<head>");
        sb.AppendLine("  <meta charset=\"utf-8\" />");
        sb.AppendLine("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />");
        sb.AppendLine($"  <title>Follower Behavior Studio — Verdict: {Esc(verdict.DraftEditorId)}</title>");
        sb.AppendLine("  <style>");
        sb.AppendLine("    :root { --bg: #0f141c; --panel: #171f2c; --border: #2a364b; --text: #e6edf3; --muted: #94a3b8; --ok: #22c55e; --warn: #f59e0b; --err: #ef4444; --info: #38bdf8; }");
        sb.AppendLine("    body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 24px; line-height: 1.5; }");
        sb.AppendLine("    h1, h2, h3 { margin-top: 0; }");
        sb.AppendLine("    .banner-lock { background: #3b181e; border: 1px solid var(--err); padding: 12px 16px; border-radius: 8px; margin-bottom: 20px; }");
        sb.AppendLine("    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; margin-bottom: 20px; }");
        sb.AppendLine("    .card { background: var(--panel); border: 1px solid var(--border); border-radius: 8px; padding: 16px; }");
        sb.AppendLine("    .badge { display: inline-block; padding: 2px 8px; border-radius: 4px; font-weight: 600; font-size: 0.82rem; }");
        sb.AppendLine("    .badge-ERROR, .badge-INCOMPATIBLE_BY_KNOWN_RULE { background: rgba(239,68,68,0.2); color: #fca5a5; border: 1px solid var(--err); }");
        sb.AppendLine("    .badge-WARNING, .badge-UNKNOWN, .badge-REQUIRES_IN_GAME_TEST { background: rgba(245,158,11,0.2); color: #fcd34d; border: 1px solid var(--warn); }");
        sb.AppendLine("    .badge-STATIC_EVIDENCE, .badge-OK { background: rgba(34,197,94,0.2); color: #86efac; border: 1px solid var(--ok); }");
        sb.AppendLine("    .badge-MOD_DEPENDENT, .badge-INFO { background: rgba(56,189,248,0.2); color: #7dd3fc; border: 1px solid var(--info); }");
        sb.AppendLine("    table { width: 100%; border-collapse: collapse; margin-top: 8px; font-size: 0.9rem; }");
        sb.AppendLine("    th, td { border-bottom: 1px solid var(--border); padding: 8px 10px; text-align: left; vertical-align: top; }");
        sb.AppendLine("    th { color: var(--muted); font-weight: 600; }");
        sb.AppendLine("    code { font-family: 'Cascadia Code', Consolas, monospace; font-size: 0.85rem; background: rgba(255,255,255,0.06); padding: 1px 5px; border-radius: 4px; }");
        sb.AppendLine("    nav { margin-bottom: 20px; display: flex; gap: 12px; flex-wrap: wrap; }");
        sb.AppendLine("    nav a { color: var(--info); text-decoration: none; font-weight: 600; }");
        sb.AppendLine("  </style>");
        sb.AppendLine("</head>");
        sb.AppendLine("<body>");
        sb.AppendLine("  <h1>Skyrim NPC Studio — Follower Behavior &amp; Voice Intelligence Report</h1>");
        sb.AppendLine("  <nav>");
        sb.AppendLine("    <a href=\"#summary\">Resumen</a>");
        sb.AppendLine("    <a href=\"#voice\">Voz y Diálogo (VTYP/FLST)</a>");
        sb.AppendLine("    <a href=\"#combat\">Clase y Combate (CLAS/CSTY)</a>");
        sb.AppendLine("    <a href=\"#links\">Dependencias y Enlaces</a>");
        sb.AppendLine("    <a href=\"#issues\">Diagnósticos e Issues</a>");
        sb.AppendLine("  </nav>");

        sb.AppendLine("  <div class=\"banner-lock\">");
        sb.AppendLine($"    <strong>Bloqueo de Exportación Final Activo:</strong> <code>PlayableExportUnlocked = {Esc(verdict.PlayableExportUnlocked.ToString().ToLowerInvariant())}</code> — {Esc(verdict.ExportLockReason)}<br/>");
        sb.AppendLine($"    <strong>Estado Runtime en Skyrim SE:</strong> <span class=\"badge badge-REQUIRES_IN_GAME_TEST\">{Esc(verdict.RuntimeStatus)}</span> (Ninguna validación estática promueve un NPC a <code>VERIFICADO_EN_SKYRIM</code>).");
        sb.AppendLine("  </div>");

        // Summary section
        sb.AppendLine("  <div id=\"summary\" class=\"grid\">");
        sb.AppendLine("    <div class=\"card\">");
        sb.AppendLine("      <h2>Identidad del Borrador</h2>");
        sb.AppendLine($"      <p><strong>Nombre:</strong> {Esc(verdict.DraftName)}</p>");
        sb.AppendLine($"      <p><strong>EditorID:</strong> <code>{Esc(verdict.DraftEditorId)}</code></p>");
        sb.AppendLine($"      <p><strong>SHA-256 Borrador:</strong> <code>{Esc(verdict.DraftSha256)}</code></p>");
        sb.AppendLine($"      <p><strong> Validez Estructural:</strong> <span class=\"badge badge-{(verdict.StructurallyValid ? "OK" : "ERROR")}\">{Esc(verdict.StructurallyValid.ToString())}</span></p>");
        sb.AppendLine("    </div>");

        sb.AppendLine("    <div class=\"card\">");
        sb.AppendLine("      <h2>Contexto de Load Order y Masters</h2>");
        sb.AppendLine($"      <p><strong>Huella Load Order:</strong> <code>{Esc(verdict.LoadOrderFingerprint)}</code></p>");
        sb.AppendLine($"      <p><strong>Generado (UTC):</strong> <code>{Esc(verdict.GeneratedAtUtc)}</code></p>");
        sb.AppendLine($"      <p><strong>Masters Requeridos ({verdict.RequiredMasters.Count}):</strong> {Esc(string.Join(", ", verdict.RequiredMasters))}</p>");
        if (!string.IsNullOrWhiteSpace(sourceJsonPath))
        {
            sb.AppendLine($"      <p><strong>Fuente JSON:</strong> <code>{Esc(sourceJsonPath)}</code></p>");
        }
        sb.AppendLine("    </div>");
        sb.AppendLine("  </div>");

        // Voice section
        var v = verdict.VoiceAnalysis;
        sb.AppendLine("  <div id=\"voice\" class=\"card\" style=\"margin-bottom:20px;\">");
        sb.AppendLine("    <h2>Evidencia de Voz (VTYP) y Listas de Diálogo (FLST)</h2>");
        sb.AppendLine($"    <p><strong>Voz Seleccionada:</strong> <code>{Esc(v.VoiceFormKey)}</code> ({Esc(v.VoiceEditorId ?? "<sin EditorID>")}) — Definida en: <code>{Esc(v.DefiningPlugin ?? "N/A")}</code></p>");
        sb.AppendLine($"    <p><strong>Clasificación Seguidor (VoicesFollower):</strong> <span class=\"badge badge-{Esc(v.FollowerDialogueClassification.ToString())}\">{Esc(v.FollowerDialogueClassification.ToString())}</span></p>");
        sb.AppendLine($"    <p><strong>Clasificación Matrimonio (VoicesMarriage):</strong> <span class=\"badge badge-{Esc(v.MarriageDialogueClassification.ToString())}\">{Esc(v.MarriageDialogueClassification.ToString())}</span></p>");
        sb.AppendLine($"    <p><strong>Verificación en Juego:</strong> <span class=\"badge badge-REQUIRES_IN_GAME_TEST\">{Esc(v.RuntimeVerificationState)}</span></p>");
        sb.AppendLine($"    <p><strong>Justificación Técnica:</strong> {Esc(v.Rationale)}</p>");

        if (v.FollowerListProvenance is { } fp)
        {
            sb.AppendLine("    <h3>Trazabilidad de Override — VoicesFollower</h3>");
            AppendProvenanceTable(sb, fp);
        }
        if (v.MarriageListProvenance is { } mp)
        {
            sb.AppendLine("    <h3>Trazabilidad de Override — VoicesMarriage</h3>");
            AppendProvenanceTable(sb, mp);
        }
        sb.AppendLine("  </div>");

        // Combat & Class section
        sb.AppendLine("  <div id=\"combat\" class=\"grid\">");
        sb.AppendLine("    <div class=\"card\">");
        sb.AppendLine("      <h2>Clase Resuelta (CLAS)</h2>");
        if (verdict.ResolvedClass is { } cls)
        {
            sb.AppendLine($"      <p><strong>FormKey:</strong> <code>{Esc(cls.FormKey)}</code> (Resuelta: <code>{Esc(cls.Resolved.ToString())}</code>)</p>");
            sb.AppendLine($"      <p><strong>EditorID / Nombre:</strong> <code>{Esc(cls.EditorId)}</code> — {Esc(cls.Name)}</p>");
            sb.AppendLine($"      <p><strong>Fuente / Ganador:</strong> <code>{Esc(cls.DefiningPlugin)}</code> → <code>{Esc(cls.WinningPlugin)}</code></p>");
            sb.AppendLine($"      <p><strong>Pesos de Habilidad:</strong> <code>{Esc(string.Join(", ", cls.SkillWeights.Select(kv => $"{kv.Key}:{kv.Value}")))}</code></p>");
        }
        sb.AppendLine("    </div>");

        sb.AppendLine("    <div class=\"card\">");
        sb.AppendLine("      <h2>Estilo de Combate Resuelto (CSTY)</h2>");
        if (verdict.ResolvedCombatStyle is { } style)
        {
            sb.AppendLine($"      <p><strong>FormKey:</strong> <code>{Esc(style.FormKey)}</code> (Resuelto: <code>{Esc(style.Resolved.ToString())}</code>)</p>");
            sb.AppendLine($"      <p><strong>EditorID:</strong> <code>{Esc(style.EditorId)}</code></p>");
            sb.AppendLine($"      <p><strong>Multiplicadores IA:</strong> Ofensivo=<code>{style.OffensiveMult:F2}</code>, Defensivo=<code>{style.DefensiveMult:F2}</code>, Evitar Amenaza=<code>{style.AvoidThreatChance:F2}</code></p>");
            sb.AppendLine($"      <p><strong>Puntuaciones de Equipo:</strong> Melee=<code>{style.MeleeScoreMult:F2}</code>, Magia=<code>{style.MagicScoreMult:F2}</code>, Rango=<code>{style.RangedScoreMult:F2}</code></p>");
        }
        else
        {
            sb.AppendLine("      <p>Sin estilo de combate explícito asignado.</p>");
        }
        sb.AppendLine("    </div>");
        sb.AppendLine("  </div>");

        // Resolved Links table
        sb.AppendLine("  <div id=\"links\" class=\"card\" style=\"margin-bottom:20px;\">");
        sb.AppendLine($"    <h2>Enlaces y Dependencias Verificadas ({verdict.ResolvedLinks.Count})</h2>");
        sb.AppendLine("    <table>");
        sb.AppendLine("      <thead><tr><th>Categoría</th><th>FormKey</th><th>Tipo</th><th>Estado</th><th>EditorID</th><th>Plugin Fuente</th><th>Plugin Ganador</th><th>Notas</th></tr></thead>");
        sb.AppendLine("      <tbody>");
        foreach (var link in verdict.ResolvedLinks)
        {
            sb.AppendLine("        <tr>");
            sb.AppendLine($"          <td>{Esc(link.Category)}</td>");
            sb.AppendLine($"          <td><code>{Esc(link.FormKey)}</code></td>");
            sb.AppendLine($"          <td><code>{Esc(link.ExpectedRecordType)}</code></td>");
            sb.AppendLine($"          <td><span class=\"badge badge-{(link.Resolved ? "OK" : "ERROR")}\">{(link.Resolved ? "RESOLVED" : "MISSING")}</span></td>");
            sb.AppendLine($"          <td><code>{Esc(link.EditorId ?? "-")}</code></td>");
            sb.AppendLine($"          <td><code>{Esc(link.DefiningPlugin ?? "-")}</code></td>");
            sb.AppendLine($"          <td><code>{Esc(link.WinningPlugin ?? "-")}</code></td>");
            sb.AppendLine($"          <td>{Esc(link.Note ?? "")}</td>");
            sb.AppendLine("        </tr>");
        }
        sb.AppendLine("      </tbody>");
        sb.AppendLine("    </table>");
        sb.AppendLine("  </div>");

        // Issues table
        sb.AppendLine("  <div id=\"issues\" class=\"card\">");
        sb.AppendLine($"    <h2>Diagnósticos, Conflictos y Advertencias ({verdict.Issues.Count})</h2>");
        sb.AppendLine("    <table>");
        sb.AppendLine("      <thead><tr><th>Severidad</th><th>Código</th><th>Mensaje</th><th>FormKey</th><th>Fuente → Ganador</th><th>Evidencia</th></tr></thead>");
        sb.AppendLine("      <tbody>");
        foreach (var issue in verdict.Issues)
        {
            var srcWin = (issue.SourcePlugin is not null || issue.WinningPlugin is not null)
                ? $"{issue.SourcePlugin ?? "?"} → {issue.WinningPlugin ?? "?"}"
                : "-";
            sb.AppendLine("        <tr>");
            sb.AppendLine($"          <td><span class=\"badge badge-{Esc(issue.Severity.ToString())}\">{Esc(issue.Severity.ToString())}</span></td>");
            sb.AppendLine($"          <td><code>{Esc(issue.Code)}</code></td>");
            sb.AppendLine($"          <td>{Esc(issue.Message)}</td>");
            sb.AppendLine($"          <td><code>{Esc(issue.TargetFormKey ?? "-")}</code></td>");
            sb.AppendLine($"          <td><code>{Esc(srcWin)}</code></td>");
            sb.AppendLine($"          <td><code>{Esc(issue.Evidence ?? "-")}</code></td>");
            sb.AppendLine("        </tr>");
        }
        sb.AppendLine("      </tbody>");
        sb.AppendLine("    </table>");
        sb.AppendLine("  </div>");

        sb.AppendLine("</body>");
        sb.AppendLine("</html>");
        return sb.ToString();
    }

    public static void WriteSha256Manifest(string manifestPath, IEnumerable<(string FilePath, string Kind)> files)
    {
        var entries = new List<ArtifactHashEntry>();
        foreach (var (filePath, kind) in files)
        {
            if (!File.Exists(filePath))
            {
                continue;
            }
            var info = new FileInfo(filePath);
            entries.Add(new ArtifactHashEntry(
                Path.GetFullPath(filePath),
                info.Length,
                SandboxGuard.ComputeFileSha256Hex(filePath),
                kind));
        }

        var manifest = new Sha256ManifestDto(
            SchemaVersion: "1.0.0",
            GeneratedAtUtc: DateTime.UtcNow.ToString("o"),
            Artifacts: entries);

        var json = JsonSerializer.Serialize(manifest, SandboxGuard.SafeJsonOptions);
        File.WriteAllText(manifestPath, json, Encoding.UTF8);
    }

    private static void AppendProvenanceTable(StringBuilder sb, FormListProvenance p)
    {
        sb.AppendLine("    <table>");
        sb.AppendLine("      <thead><tr><th>FLST FormKey</th><th>EditorID</th><th>Encontrada</th><th>En Vanilla</th><th>En Ganador</th><th>Plugin Fuente</th><th>Plugin Ganador</th><th>Cadena de Overrides</th></tr></thead>");
        sb.AppendLine("      <tbody><tr>");
        sb.AppendLine($"        <td><code>{Esc(p.ListFormKey)}</code></td>");
        sb.AppendLine($"        <td><code>{Esc(p.ListEditorId)}</code></td>");
        sb.AppendLine($"        <td>{Esc(p.ListFound.ToString())}</td>");
        sb.AppendLine($"        <td>{Esc(p.PresentInVanilla.ToString())}</td>");
        sb.AppendLine($"        <td>{Esc(p.PresentInWinner.ToString())}</td>");
        sb.AppendLine($"        <td><code>{Esc(p.SourcePlugin ?? "-")}</code></td>");
        sb.AppendLine($"        <td><code>{Esc(p.WinningPlugin ?? "-")}</code></td>");
        sb.AppendLine($"        <td><code>{Esc(string.Join(" -> ", p.OverrideChain))}</code></td>");
        sb.AppendLine("      </tr></tbody>");
        sb.AppendLine("    </table>");
    }

    private static string Esc(string? value) => WebUtility.HtmlEncode(value ?? string.Empty);
}
