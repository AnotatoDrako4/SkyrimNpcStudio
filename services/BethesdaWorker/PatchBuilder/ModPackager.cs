using System.Globalization;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>
/// Produces an MO2-installable mod: everything is built in a hidden staging directory next to the
/// destination, verified, and only then moved into place with a same-volume rename. Existing
/// files are never overwritten; on any failure the staging directory is deleted (rollback).
/// </summary>
internal static class ModPackager
{
    public const string ManifestFormat = "skyrim-npc-studio/export";
    public const int ManifestVersion = 2;

    public static ExportResponse ExportOutfitPatch(
        LoadOrderSession session, CatalogQueries queries, ExportOutfitPatchRequest request, CancellationToken ct)
    {
        return ExportCore(
            session,
            request.PluginName,
            request.OutputDirectory,
            request.CreateZip,
            request.Author,
            request.Description,
            request.PluginProviders,
            "outfit-patch",
            (stagedPlugin, stagedModDir) => OutfitPatchBuilder.Build(session, queries, request, stagedPlugin, ct),
            ct);
    }

    public static ExportResponse ExportFaceOverhaul(
        LoadOrderSession session, CatalogQueries queries, ExportFaceOverhaulRequest request, CancellationToken ct)
    {
        return ExportCore(
            session,
            request.PluginName,
            request.OutputDirectory,
            request.CreateZip,
            request.Author,
            request.Description,
            request.PluginProviders,
            "face-overhaul",
            (stagedPlugin, stagedModDir) => FacePatchBuilder.BuildFaceOverhaul(session, queries, request, stagedPlugin, stagedModDir, ct),
            ct);
    }

    public static ExportResponse ExportCompleteOverhaul(
        LoadOrderSession session, CatalogQueries queries, ExportCompleteOverhaulRequest request, CancellationToken ct)
    {
        return ExportCore(
            session,
            request.PluginName,
            request.OutputDirectory,
            request.CreateZip,
            request.Author,
            request.Description,
            request.PluginProviders,
            "complete-overhaul",
            (stagedPlugin, stagedModDir) => FacePatchBuilder.BuildCompleteOverhaul(session, queries, request, stagedPlugin, stagedModDir, ct),
            ct);
    }

    private static ExportResponse ExportCore(
        LoadOrderSession session,
        string pluginName,
        string outputDirectory,
        bool createZip,
        string author,
        string? description,
        IReadOnlyDictionary<string, string>? pluginProviders,
        string mode,
        Func<string, string, BuiltPatch> buildAction,
        CancellationToken ct)
    {
        var outputDir = ValidateOutputDirectory(outputDirectory, session);
        OutfitPatchBuilder.ValidatePluginName(pluginName, session);

        var modName = Path.GetFileNameWithoutExtension(pluginName);
        var finalModDir = Path.Combine(outputDir, modName);
        var finalZip = Path.Combine(outputDir, modName + ".zip");
        if (Directory.Exists(finalModDir) || File.Exists(finalModDir))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalModDir}' already exists; choose another name or folder.");
        }

        if (createZip && File.Exists(finalZip))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalZip}' already exists; choose another name or folder.");
        }

        var staging = Path.Combine(outputDir, $".sns-staging-{Guid.NewGuid():N}");
        var stagedModDir = Path.Combine(staging, modName);
        var movedModDir = false;
        try
        {
            Directory.CreateDirectory(stagedModDir);
            var stagedPlugin = Path.Combine(stagedModDir, pluginName);
            var built = buildAction(stagedPlugin, stagedModDir);
            ct.ThrowIfCancellationRequested();

            var sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(stagedPlugin)));

            var bundledAssets = Directory.EnumerateFiles(stagedModDir, "*", SearchOption.AllDirectories)
                .Select(f => Path.GetRelativePath(stagedModDir, f))
                .Where(f => !f.EndsWith(".esp", StringComparison.OrdinalIgnoreCase)
                         && !f.Equals("manifest.json", StringComparison.OrdinalIgnoreCase)
                         && !f.Equals("README.md", StringComparison.OrdinalIgnoreCase)
                         && !f.Equals("meta.ini", StringComparison.OrdinalIgnoreCase))
                .ToArray();

            var manifest = BuildManifest(session, pluginName, author, description, mode, pluginProviders, built, sha256, bundledAssets);
            File.WriteAllText(Path.Combine(stagedModDir, "manifest.json"), JsonSerializer.Serialize(manifest, ManifestJson), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "README.md"), BuildReadme(session, pluginName, description, mode, pluginProviders, built, bundledAssets), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "meta.ini"), BuildMetaIni(description, modName), Encoding.UTF8);

            string? stagedZip = null;
            if (createZip)
            {
                stagedZip = Path.Combine(staging, modName + ".zip");
                CreateZip(stagedModDir, stagedZip);
            }

            ct.ThrowIfCancellationRequested();

            // Commit: both renames stay on the output volume, so each one is atomic.
            Directory.Move(stagedModDir, finalModDir);
            movedModDir = true;
            if (stagedZip is not null)
            {
                File.Move(stagedZip, finalZip, overwrite: false);
            }

            Directory.Delete(staging, recursive: true);
            return new ExportResponse
            {
                ModDirectory = finalModDir,
                PluginPath = Path.Combine(finalModDir, pluginName),
                ZipPath = stagedZip is null ? null : finalZip,
                ManifestPath = Path.Combine(finalModDir, "manifest.json"),
                Masters = built.Masters.Select(m => m.FileName.String).ToArray(),
                EslFlagged = built.EslFlagged,
                PluginSha256 = sha256,
                Records = built.Records,
                Issues = built.Issues,
            };
        }
        catch
        {
            if (movedModDir && Directory.Exists(finalModDir))
            {
                Directory.Move(finalModDir, stagedModDir);
            }

            TryDelete(staging);
            throw;
        }
    }

    private static string ValidateOutputDirectory(string path, LoadOrderSession session)
    {
        if (string.IsNullOrWhiteSpace(path) || !Path.IsPathFullyQualified(path))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "outputDirectory must be an absolute path.");
        }

        var full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        if (!Directory.Exists(full))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"Output directory '{full}' does not exist.");
        }

        // Refuse to write next to any active plugin: that is a game Data folder or an existing mod.
        foreach (var plugin in session.Plugins)
        {
            var pluginDir = Path.TrimEndingDirectorySeparator(Path.GetDirectoryName(Path.GetFullPath(plugin.Path))!);
            if (string.Equals(pluginDir, full, StringComparison.OrdinalIgnoreCase))
            {
                throw new WorkerException(ErrorCodes.InvalidArgument,
                    $"'{full}' contains active plugins (game Data folder or an installed mod). Export to a separate folder.");
            }
        }

        return full;
    }

    private static readonly JsonSerializerOptions ManifestJson = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    private static object BuildManifest(
        LoadOrderSession session, string pluginName, string author, string? description,
        string mode, IReadOnlyDictionary<string, string>? pluginProviders,
        BuiltPatch built, string sha256, IReadOnlyList<string> bundledAssets) => new
    {
        format = ManifestFormat,
        formatVersion = ManifestVersion,
        tool = new { name = "Skyrim NPC Studio", workerVersion = WorkerVersion, protocol = $"{Protocol.Major}.{Protocol.Minor}" },
        createdUtc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
        mode,
        plugin = new { fileName = pluginName, sha256, eslFlagged = built.EslFlagged },
        masters = built.Masters.Select(m => new
        {
            plugin = m.FileName.String,
            providedBy = pluginProviders?.GetValueOrDefault(m.FileName.String),
            loadIndex = session.Find(m)?.LoadIndex,
        }),
        records = built.Records,
        assignments = built.Assignments.Select(a => new
        {
            npc = a.Npc.ToString(),
            npcEditorId = a.NpcEditorId,
            npcName = a.NpcName,
            outfit = a.Outfit.IsNull ? null : a.Outfit.ToString(),
            outfitEditorId = string.IsNullOrEmpty(a.OutfitEditorId) ? null : a.OutfitEditorId,
            armors = a.Armors.Select(x => new { formKey = x.FormKey, editorId = x.EditorId, name = x.Name, plugin = x.WinningPlugin, slots = x.Slots }),
        }),
        standaloneOutfits = (built.StandaloneOutfits ?? []).Select(a => new
        {
            outfit = a.Outfit.ToString(),
            outfitEditorId = a.EditorId,
            armors = a.Armors.Select(x => new { formKey = x.FormKey, editorId = x.EditorId, name = x.Name, plugin = x.WinningPlugin, slots = x.Slots }),
        }),
        // "generated": every bundled file was produced by the studio (FaceGen), nothing is copied
        // from other mods; "dependent": the plugin only references assets of its masters.
        dependencyMode = bundledAssets.Count > 0 ? "generated" : "dependent",
        bundledAssets,
        generatedAssets = (built.Generated ?? []).Select(g => new { npc = g.Npc, dataPath = g.DataPath, sha256 = g.Sha256, kind = g.Kind }),
        faces = (built.Faces ?? []).Select(f => new
        {
            npc = f.Npc,
            npcEditorId = f.NpcEditorId,
            changedFields = f.ChangedFields,
            faceGenMode = f.Provenance.Mode,
            parts = f.Provenance.Parts,
            derivedFrom = f.Provenance.DerivedFrom,
            customMorphs = f.Provenance.CustomMorphs,
            sculptTargets = f.Provenance.SculptTargets,
        }),
        personalUseOnly = (built.Faces ?? []).Any(f => f.Provenance.DerivedFrom.Count > 0),
        issues = built.Issues,
        inGameVerified = false,
    };

    private static string BuildReadme(
        LoadOrderSession session, string pluginName, string? description,
        string mode, IReadOnlyDictionary<string, string>? pluginProviders,
        BuiltPatch built, IReadOnlyList<string> bundledAssets)
    {
        var sb = new StringBuilder();
        var modName = Path.GetFileNameWithoutExtension(pluginName);
        sb.AppendLine(CultureInfo.InvariantCulture, $"# {modName}");
        sb.AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"Mod generado por Skyrim NPC Studio (worker {WorkerVersion}) el {DateTime.UtcNow:yyyy-MM-dd HH:mm} UTC.");
        sb.AppendLine(CultureInfo.InvariantCulture, $"Modo de exportación: `{mode}`.");
        if (!string.IsNullOrWhiteSpace(description))
        {
            sb.AppendLine().AppendLine(description);
        }

        sb.AppendLine().AppendLine("## Contenido").AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"- `{pluginName}` — {(built.EslFlagged ? "ESP con flag ESL (no ocupa índice de carga completo)" : "ESP")}");
        sb.AppendLine(CultureInfo.InvariantCulture, $"- {built.Assignments.Count} override(s) de NPC.");
        sb.AppendLine(CultureInfo.InvariantCulture, $"- {(built.StandaloneOutfits ?? []).Count} outfit(s) OTFT independientes.");
        if (bundledAssets.Count > 0)
        {
            sb.AppendLine(CultureInfo.InvariantCulture, $"- {bundledAssets.Count} archivo(s) FaceGen regenerados por Skyrim NPC Studio a partir del registro exportado (no copiados de otros mods).");
        }

        foreach (var face in built.Faces ?? [])
        {
            sb.AppendLine(CultureInfo.InvariantCulture,
                $"- Rostro de {face.NpcEditorId ?? face.Npc}: campos {string.Join(", ", face.ChangedFields)}; FaceGen en modo `{face.Provenance.Mode}`.");
            if (face.Provenance.DerivedFrom.Count > 0)
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"  - Contiene geometría derivada de: {string.Join(", ", face.Provenance.DerivedFrom)}. Uso personal: no lo redistribuyas sin permiso de sus autores.");
            }
        }

        sb.AppendLine().AppendLine("## Requisitos (masters)").AppendLine();
        sb.AppendLine("Deben estar activos y cargarse ANTES de este plugin:").AppendLine();
        foreach (var master in built.Masters)
        {
            var provider = pluginProviders?.GetValueOrDefault(master.FileName.String);
            sb.AppendLine(provider is null ? $"- {master.FileName}" : $"- {master.FileName} — mod de MO2 \"{provider}\"");
        }

        sb.AppendLine().AppendLine("## Instalación con Mod Organizer 2").AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"1. En MO2, *Instalar mod desde archivo* y elige `{modName}.zip`.");
        sb.AppendLine(CultureInfo.InvariantCulture, $"2. Activa el mod y el plugin `{pluginName}`.");
        sb.AppendLine("3. Coloca el plugin después de sus masters y de cualquier otro mod que modifique a estos NPC.");
        if (bundledAssets.Count > 0)
        {
            sb.AppendLine("4. En el panel izquierdo de MO2, coloca este mod DEBAJO (prioridad mayor) de cualquier mod que aporte FaceGen para los mismos NPC; si no, el juego usará la cabeza del otro mod y puede verse la cara oscura.");
        }

        sb.AppendLine().AppendLine("Estado: generado y verificado por el estudio; pendiente de comprobar dentro del juego.");
        return sb.ToString();
    }

    private static string BuildMetaIni(string? description, string modName)
    {
        var sb = new StringBuilder();
        sb.AppendLine("[General]");
        sb.AppendLine("gameName=SkyrimSE");
        sb.AppendLine("modid=0");
        sb.AppendLine("version=1.0.0");
        sb.AppendLine(CultureInfo.InvariantCulture, $"installationFile={modName}.zip");
        sb.AppendLine("repository=");
        sb.AppendLine("comments=Generado por Skyrim NPC Studio");
        sb.AppendLine(CultureInfo.InvariantCulture, $"notes=\"{(description ?? string.Empty).Replace("\"", "'", StringComparison.Ordinal)}\"");
        return sb.ToString();
    }

    private static void CreateZip(string modDir, string zipPath)
    {
        using var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create);
        foreach (var file in Directory.EnumerateFiles(modDir, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(modDir, file);
            if (string.Equals(relative, "meta.ini", StringComparison.OrdinalIgnoreCase))
            {
                continue; // MO2 creates its own meta.ini on install.
            }

            zip.CreateEntryFromFile(file, relative.Replace('\\', '/'), CompressionLevel.Optimal);
        }
    }

    private static void TryDelete(string directory)
    {
        try
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    public static string WorkerVersion { get; } =
        typeof(ModPackager).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "0.0.0";
}
