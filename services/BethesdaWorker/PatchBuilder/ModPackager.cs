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
    public const int ManifestVersion = 1;

    public static ExportResponse ExportOutfitPatch(
        LoadOrderSession session, CatalogQueries queries, ExportOutfitPatchRequest request, CancellationToken ct)
    {
        var outputDir = ValidateOutputDirectory(request.OutputDirectory, session);
        OutfitPatchBuilder.ValidatePluginName(request.PluginName, session);

        var modName = Path.GetFileNameWithoutExtension(request.PluginName);
        var finalModDir = Path.Combine(outputDir, modName);
        var finalZip = Path.Combine(outputDir, modName + ".zip");
        if (Directory.Exists(finalModDir) || File.Exists(finalModDir))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalModDir}' already exists; choose another name or folder.");
        }

        if (request.CreateZip && File.Exists(finalZip))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalZip}' already exists; choose another name or folder.");
        }

        var staging = Path.Combine(outputDir, $".sns-staging-{Guid.NewGuid():N}");
        var stagedModDir = Path.Combine(staging, modName);
        var movedModDir = false;
        try
        {
            Directory.CreateDirectory(stagedModDir);
            var stagedPlugin = Path.Combine(stagedModDir, request.PluginName);
            var built = OutfitPatchBuilder.Build(session, queries, request, stagedPlugin, ct);
            ct.ThrowIfCancellationRequested();

            var sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(stagedPlugin)));
            var manifest = BuildManifest(session, request, built, sha256);
            File.WriteAllText(Path.Combine(stagedModDir, "manifest.json"), JsonSerializer.Serialize(manifest, ManifestJson), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "README.md"), BuildReadme(session, request, built), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "meta.ini"), BuildMetaIni(request, modName), Encoding.UTF8);

            string? stagedZip = null;
            if (request.CreateZip)
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
                PluginPath = Path.Combine(finalModDir, request.PluginName),
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
                // Roll back the first rename if the second one failed.
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

    private static object BuildManifest(LoadOrderSession session, ExportOutfitPatchRequest request, BuiltPatch built, string sha256) => new
    {
        format = ManifestFormat,
        formatVersion = ManifestVersion,
        tool = new { name = "Skyrim NPC Studio", workerVersion = WorkerVersion, protocol = $"{Protocol.Major}.{Protocol.Minor}" },
        createdUtc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
        mode = "outfit-patch",
        plugin = new { fileName = request.PluginName, sha256, eslFlagged = built.EslFlagged },
        masters = built.Masters.Select(m => new
        {
            plugin = m.FileName.String,
            providedBy = request.PluginProviders?.GetValueOrDefault(m.FileName.String),
            loadIndex = session.Find(m)?.LoadIndex,
        }),
        records = built.Records,
        assignments = built.Assignments.Select(a => new
        {
            npc = a.Npc.ToString(),
            npcEditorId = a.NpcEditorId,
            npcName = a.NpcName,
            outfit = a.Outfit.ToString(),
            outfitEditorId = a.OutfitEditorId,
            armors = a.Armors.Select(x => new { formKey = x.FormKey, editorId = x.EditorId, name = x.Name, plugin = x.WinningPlugin, slots = x.Slots }),
        }),
        dependencyMode = "dependent",
        bundledAssets = Array.Empty<string>(),
        issues = built.Issues,
        inGameVerified = false,
    };

    private static string BuildReadme(LoadOrderSession session, ExportOutfitPatchRequest request, BuiltPatch built)
    {
        var sb = new StringBuilder();
        var modName = Path.GetFileNameWithoutExtension(request.PluginName);
        sb.AppendLine(CultureInfo.InvariantCulture, $"# {modName}");
        sb.AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"Mod generado por Skyrim NPC Studio (worker {WorkerVersion}) el {DateTime.UtcNow:yyyy-MM-dd HH:mm} UTC.");
        if (!string.IsNullOrWhiteSpace(request.Description))
        {
            sb.AppendLine().AppendLine(request.Description);
        }

        sb.AppendLine().AppendLine("## Contenido").AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"- `{request.PluginName}` — {(built.EslFlagged ? "ESP con flag ESL (no ocupa índice de carga completo)" : "ESP")}");
        sb.AppendLine(CultureInfo.InvariantCulture, $"- {built.Assignments.Count} atuendo(s) nuevo(s) (OTFT) y {built.Assignments.Count} override(s) de NPC.");
        sb.AppendLine("- No incluye mallas ni texturas: usa los recursos de los mods que proporcionan cada prenda (mod dependiente).");

        sb.AppendLine().AppendLine("## Cambios").AppendLine();
        foreach (var a in built.Assignments)
        {
            sb.AppendLine(CultureInfo.InvariantCulture, $"### {a.NpcName ?? a.NpcEditorId ?? a.Npc.ToString()} (`{session.FormatFormId(a.Npc)}` · `{a.Npc}`)");
            sb.AppendLine();
            sb.AppendLine(CultureInfo.InvariantCulture, $"Nuevo atuendo predeterminado `{a.OutfitEditorId}` (`{a.Outfit}`):");
            foreach (var armor in a.Armors)
            {
                sb.AppendLine(CultureInfo.InvariantCulture, $"- {armor.Name ?? armor.EditorId} (`{armor.FormKey}`, slots {string.Join(", ", armor.Slots)}) — {armor.WinningPlugin}");
            }

            sb.AppendLine();
        }

        sb.AppendLine("## Requisitos (masters)").AppendLine();
        sb.AppendLine("Deben estar activos y cargarse ANTES de este plugin:").AppendLine();
        foreach (var master in built.Masters)
        {
            var provider = request.PluginProviders?.GetValueOrDefault(master.FileName.String);
            sb.AppendLine(provider is null ? $"- {master.FileName}" : $"- {master.FileName} — mod de MO2 \"{provider}\"");
        }

        sb.AppendLine().AppendLine("## Instalación con Mod Organizer 2").AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"1. En MO2, *Instalar mod desde archivo* y elige `{modName}.zip`.");
        sb.AppendLine(CultureInfo.InvariantCulture, $"2. Activa el mod y el plugin `{request.PluginName}`.");
        sb.AppendLine("3. Coloca el plugin después de sus masters y de cualquier otro mod que modifique a estos NPC.");

        sb.AppendLine().AppendLine("## Limitaciones conocidas").AppendLine();
        sb.AppendLine("- En partidas existentes, un NPC ya cargado puede conservar su equipamiento actual hasta que el juego lo reinicialice; no se garantiza la aplicación inmediata en todas las partidas.");
        sb.AppendLine("- El override copia el registro ganador del perfil usado al exportar. Si después cambias mods que editan a estos NPC, vuelve a exportar.");
        sb.AppendLine("- Validado estructuralmente (masters, referencias, slots, razas). La comprobación dentro del juego debe hacerla el usuario.");
        return sb.ToString();
    }

    private static string BuildMetaIni(ExportOutfitPatchRequest request, string modName)
    {
        // Same keys MO2 writes for Skyrim SE mods; "installationFile" links the folder to its archive.
        var sb = new StringBuilder();
        sb.AppendLine("[General]");
        sb.AppendLine("gameName=SkyrimSE");
        sb.AppendLine("modid=0");
        sb.AppendLine("version=1.0.0");
        sb.AppendLine(CultureInfo.InvariantCulture, $"installationFile={modName}.zip");
        sb.AppendLine("repository=");
        sb.AppendLine("comments=Generado por Skyrim NPC Studio");
        sb.AppendLine(CultureInfo.InvariantCulture, $"notes=\"{(request.Description ?? string.Empty).Replace("\"", "'", StringComparison.Ordinal)}\"");
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
            // Best effort: a leftover hidden ".sns-staging-*" folder is harmless and reported in logs.
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    public static string WorkerVersion { get; } =
        typeof(ModPackager).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "0.0.0";
}
