using System.Globalization;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.Validation;

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

    internal static ExportResponse ExportNativeNpc(LoadOrderSession session, CatalogQueries queries,
        CreationExportRequest request, CreationValidateResponse validation, CancellationToken ct) =>
        ExportCore(session, request.PluginName, request.OutputDirectory, request.CreateZip, request.Author,
            request.Npc.Description, request.PluginProviders, "native-npc-technical",
            (path, _) => NativeNpcBuilder.Build(session, queries, request, validation, path, ct), ct);

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
        // The resolved folder (links and junctions followed): the export writes there, never through a link.
        var outputDir = ExportDestinationPolicy.Validate(session, outputDirectory);
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

            var files = ListFiles(stagedModDir);
            var verification = DescribeVerification(built, files);
            var pending = DescribePendingVerification(built, bundledAssets);
            var manifest = BuildManifest(session, pluginName, author, description, mode, pluginProviders, built, sha256, bundledAssets,
                files, verification, pending);
            File.WriteAllText(Path.Combine(stagedModDir, "manifest.json"), JsonSerializer.Serialize(manifest, ManifestJson), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "README.md"), BuildReadme(session, pluginName, description, mode, pluginProviders, built, bundledAssets), Encoding.UTF8);
            File.WriteAllText(Path.Combine(stagedModDir, "meta.ini"), BuildMetaIni(description, modName), Encoding.UTF8);

            string? stagedZip = null;
            if (createZip)
            {
                stagedZip = Path.Combine(staging, modName + ".zip");
                CreateZip(stagedModDir, stagedZip);
                VerifyZip(stagedZip, stagedModDir);
                verification = [.. verification, $"ZIP releído: {files.Count + 2} entrada(s) con rutas relativas seguras y contenido idéntico al de la carpeta del mod (SHA-256)."];
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
                SemanticDiff = built.SemanticDiffs ?? [],
                Files = files,
                Verification = verification,
                PendingVerification = pending,
                ProjectedChains = built.ProjectedChains ?? [],
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

    private static readonly JsonSerializerOptions ManifestJson = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    private static object BuildManifest(
        LoadOrderSession session, string pluginName, string author, string? description,
        string mode, IReadOnlyDictionary<string, string>? pluginProviders,
        BuiltPatch built, string sha256, IReadOnlyList<string> bundledAssets,
        IReadOnlyList<ExportedFile> files, IReadOnlyList<string> verification, IReadOnlyList<string> pendingVerification) => new
    {
        format = ManifestFormat,
        formatVersion = ManifestVersion,
        tool = new { name = "Skyrim NPC Studio", workerVersion = WorkerVersion, protocol = $"{Protocol.Major}.{Protocol.Minor}" },
        createdUtc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
        mode,
        // A face copied from another plugin is only checked in files (record vs FaceGen): never "ready".
        readyForGame = mode == "native-npc-technical" || built.Faces?.Any(f => f.Provenance.Mode == "source-facegen") == true ? (bool?)false : null,
        faceGenStatus = mode == "native-npc-technical" ? "pending" : null,
        validationLevel = mode == "native-npc-technical" ? "structural-readback-only" : null,
        plugin = new { fileName = pluginName, sha256, eslFlagged = built.EslFlagged },
        masters = built.Masters.Select(m => new
        {
            plugin = m.FileName.String,
            providedBy = ProviderOf(pluginProviders, m.FileName.String),
            loadIndex = session.Find(m)?.LoadIndex,
        }),
        // Plugins this one must load after (declared as masters although no record links to them).
        loadAfter = built.LoadAfter ?? [],
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
            installAbove = f.Provenance.InstallAbove,
            effectiveFaceGenMod = f.Provenance.EffectiveFaceGenMod,
        }),
        personalUseOnly = (built.Faces ?? []).Any(f => f.Provenance.DerivedFrom.Count > 0),
        // Every NPC_ field that differs from the winning record, re-read from the written plugin.
        semanticDiff = built.SemanticDiffs ?? [],
        files,
        // What the worker verified before writing this manifest, and what only the game can confirm.
        verification,
        pendingVerification,
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
        if (mode == "native-npc-technical")
        {
            sb.AppendLine("NO INSTALAR EN SKYRIM: artefacto técnico de pruebas. FaceGen NIF y FaceTint DDS pendientes; no es un mod listo para jugar.");
            sb.AppendLine(CultureInfo.InvariantCulture, $"NPC nuevos: {built.Records.Count(r => r.RecordType == "NPC_" && r.Kind == "new")}. Solo los registros estructurales y sus referencias fueron verificados.");
        }
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

            if (face.Provenance.Mode == "source-facegen")
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"  - Rostro copiado del plugin indicado en los cambios: el mod no trae FaceGen; usa el de «{face.Provenance.EffectiveFaceGenMod}», que ya es el que carga el juego. Si ese mod deja de ganar en MO2, el registro y la cabeza dejarán de coincidir.");
            }

            if (face.Provenance.InstallAbove.Count > 0)
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"  - **Prioridad en MO2:** coloca este mod por encima de {string.Join(", ", face.Provenance.InstallAbove.Select(m => $"«{m}»"))}. También contienen el FaceGen de este NPC; si alguno queda por encima, Skyrim usará su cabeza con este plugin (cara oscura o distinta).");
            }
        }

        if (built.SemanticDiffs is { Count: > 0 } diffs)
        {
            sb.AppendLine().AppendLine("## Cambios respecto al registro ganador").AppendLine();
            sb.AppendLine("Comparación del plugin escrito, releído, con el registro del NPC que ganaba en el orden de carga. Cualquier otro campo del NPC es idéntico (si no, la exportación se habría bloqueado).").AppendLine();
            foreach (var diff in diffs)
            {
                sb.AppendLine(CultureInfo.InvariantCulture, $"### {diff.NpcEditorId ?? diff.Npc} (`{diff.Npc}`)").AppendLine();
                if (diff.Changes.Count == 0)
                {
                    sb.AppendLine("- Sin cambios en el registro del NPC.");
                }

                foreach (var change in diff.Changes)
                {
                    sb.AppendLine(CultureInfo.InvariantCulture, $"- **{change.Field}** ({change.Subrecord}): {change.Reason}.");
                    sb.AppendLine(CultureInfo.InvariantCulture, $"  - Antes: `{change.Original}`");
                    sb.AppendLine(CultureInfo.InvariantCulture, $"  - Ahora: `{change.Exported}`");
                    if (change.Dependencies.Count > 0)
                    {
                        sb.AppendLine(CultureInfo.InvariantCulture, $"  - Depende de: {string.Join(", ", change.Dependencies)}");
                    }
                }

                sb.AppendLine();
            }
        }

        sb.AppendLine().AppendLine("## Requisitos (masters)").AppendLine();
        sb.AppendLine("Deben estar activos y cargarse ANTES de este plugin:").AppendLine();
        foreach (var master in built.Masters)
        {
            var provider = ProviderOf(pluginProviders, master.FileName.String);
            sb.AppendLine(provider is null ? $"- {master.FileName}" : $"- {master.FileName} — mod de MO2 \"{provider}\"");
        }

        sb.AppendLine().AppendLine("## Instalación con Mod Organizer 2").AppendLine();
        sb.AppendLine(CultureInfo.InvariantCulture, $"1. En MO2, *Instalar mod desde archivo* y elige `{modName}.zip`.");
        sb.AppendLine(CultureInfo.InvariantCulture, $"2. Activa el mod y el plugin `{pluginName}`.");
        sb.AppendLine("3. Coloca el plugin después de sus masters y de cualquier otro mod que modifique a estos NPC.");
        if (built.LoadAfter is { Count: > 0 } after)
        {
            sb.AppendLine(CultureInfo.InvariantCulture,
                $"   Debe cargarse después de {string.Join(" y ", after.Select(p => $"`{p}`"))}: por eso son masters del plugin aunque ningún registro apunte a ellos. Si otro plugin posterior vuelve a cambiar estos NPC, gana él.");
        }
        if (bundledAssets.Count > 0)
        {
            sb.AppendLine("4. En el panel izquierdo de MO2, coloca este mod DEBAJO (prioridad mayor) de cualquier mod que aporte FaceGen para los mismos NPC; si no, el juego usará la cabeza del otro mod y puede verse la cara oscura.");
        }

        sb.AppendLine().AppendLine("Estado: generado y verificado por el estudio; pendiente de comprobar dentro del juego.");
        return sb.ToString();
    }

    internal static string BuildMetaIni(string? description, string modName)
    {
        var sb = new StringBuilder();
        sb.AppendLine("[General]");
        sb.AppendLine("gameName=SkyrimSE");
        sb.AppendLine("modid=0");
        sb.AppendLine("version=1.0.0");
        sb.AppendLine(CultureInfo.InvariantCulture, $"installationFile={modName}.zip");
        sb.AppendLine("repository=");
        sb.AppendLine("comments=Generado por Skyrim NPC Studio");
        sb.AppendLine(CultureInfo.InvariantCulture, $"notes=\"{IniValue(description ?? string.Empty)}\"");
        return sb.ToString();
    }

    /// <summary>
    /// Text for one quoted meta.ini value as QSettings (how MO2 reads the file) unescapes it: backslash, quote,
    /// CR, LF and tab are escaped and any other control character becomes a space, so a description can never
    /// start a new line, section or key (audit v2 F-19: "[Plugins]" in a description used to become one).
    /// </summary>
    internal static string IniValue(string text)
    {
        var sb = new StringBuilder(text.Length);
        foreach (var c in text)
        {
            sb.Append(c switch
            {
                '\\' => @"\\",
                '"' => "\\\"",
                '\n' => @"\n",
                '\r' => @"\r",
                '\t' => @"\t",
                _ when char.IsControl(c) || c is '\u2028' or '\u2029' => " ",
                _ => c.ToString(),
            });
        }

        return sb.ToString();
    }

    /// <summary>Plugin, generated assets and every other file of the mod folder, except what the packager writes itself.</summary>
    private static IReadOnlyList<ExportedFile> ListFiles(string modDir) =>
        Directory.EnumerateFiles(modDir, "*", SearchOption.AllDirectories)
            .Select(f => (Full: f, Relative: Path.GetRelativePath(modDir, f).Replace('\\', '/')))
            .Where(f => f.Relative is not ("manifest.json" or "README.md" or "meta.ini"))
            .OrderBy(f => f.Relative, StringComparer.OrdinalIgnoreCase)
            .Select(f => new ExportedFile
            {
                Path = f.Relative,
                Sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f.Full))),
                Bytes = new FileInfo(f.Full).Length,
            })
            .ToArray();

    private static IReadOnlyList<string> DescribeVerification(BuiltPatch built, IReadOnlyList<ExportedFile> files)
    {
        var checks = new List<string>
        {
            "Plugin releído con Mutagen tras escribirlo: masters presentes y en orden de carga, flag ESL coherente, rango ESL, referencias resueltas.",
        };
        if (built.SemanticDiffs is { Count: > 0 } diffs)
        {
            checks.Add($"Cada NPC_ releído se comparó campo a campo con su registro ganador: solo cambian los campos pedidos ({diffs.Sum(d => d.Changes.Count)} cambio(s) en {diffs.Count} NPC).");
        }

        if (built.Generated is { Count: > 0 } generated)
        {
            checks.Add($"FaceGen: {generated.Count} archivo(s) con SHA-256 comprobado contra el que envió el estudio y copiados a la ruta propia del NPC.");
        }

        if (built.Faces?.Where(f => f.Provenance.Mode == "source-facegen").ToList() is { Count: > 0 } copied)
        {
            checks.Add($"Rostro copiado: {copied.Count} NPC releídos con sus campos faciales idénticos a la versión de origen; sin FaceGen en el mod (se usa el de «{string.Join("», «", copied.Select(f => f.Provenance.EffectiveFaceGenMod).Distinct())}»).");
        }

        if (built.LoadAfter is { Count: > 0 } after)
        {
            checks.Add($"Se carga después de {string.Join(", ", after)}: declarados como masters y presentes en el plugin releído.");
        }

        checks.Add($"{files.Count} archivo(s) del mod con SHA-256 y tamaño registrados en el manifiesto.");
        return checks;
    }

    private static IReadOnlyList<string> DescribePendingVerification(BuiltPatch built, IReadOnlyList<string> bundledAssets)
    {
        var pending = new List<string>
        {
            "Probar el mod dentro de Skyrim, en un perfil de MO2 de prueba: que el NPC se ve y se equipa como en la vista previa.",
        };
        if (bundledAssets.Count > 0)
        {
            var above = (built.Faces ?? []).SelectMany(f => f.Provenance.InstallAbove).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            pending.Add(above.Count > 0
                ? $"Comprobar en MO2 que este mod queda por encima de {string.Join(", ", above.Select(m => $"«{m}»"))} (también contienen el FaceGen de estos NPC)."
                : "Comprobar en MO2 que ningún otro mod con FaceGen de estos NPC queda con mayor prioridad que este.");
        }

        if (built.Faces?.Any(f => f.Provenance.Mode == "source-facegen") == true)
        {
            pending.Add("Comprobar en el juego que la cara ya no sale oscura ni distinta: el registro y el FaceGen ganador vienen ahora del mismo mod, pero no se ha ejecutado Skyrim.");
        }
        else if (built.Faces is { Count: > 0 })
        {
            pending.Add("Revisar la cabeza y el color de cabello en el juego (la vista previa y el FaceGen exportado coinciden, pero no se ha ejecutado Skyrim).");
        }

        return pending;
    }

    /// <summary>
    /// Re-opens the ZIP that will be delivered: no absolute or parent-relative entry names, nothing the
    /// mod folder does not contain, nothing missing, and every entry byte-identical to its file.
    /// </summary>
    internal static void VerifyZip(string zipPath, string modDir)
    {
        var expected = Directory.EnumerateFiles(modDir, "*", SearchOption.AllDirectories)
            .Select(f => Path.GetRelativePath(modDir, f).Replace('\\', '/'))
            .Where(f => !string.Equals(f, "meta.ini", StringComparison.OrdinalIgnoreCase))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using var zip = ZipFile.OpenRead(zipPath);
        foreach (var entry in zip.Entries)
        {
            var name = entry.FullName;
            var segments = name.Split('/');
            if (name.Length == 0 || name.StartsWith('/') || name.Contains('\\') || name.Contains(':')
                || segments.Any(s => s is ".." or "." || s.Length == 0 && !name.EndsWith('/')))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"ZIP verification failed: unsafe entry name '{name}'.");
            }

            if (name.EndsWith('/'))
            {
                continue;
            }

            if (!expected.Contains(name) || !seen.Add(name))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"ZIP verification failed: unexpected or duplicate entry '{name}'.");
            }

            using var stream = entry.Open();
            var zipped = Convert.ToHexString(SHA256.HashData(stream));
            var onDisk = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(modDir, name.Replace('/', Path.DirectorySeparatorChar)))));
            if (!string.Equals(zipped, onDisk, StringComparison.Ordinal))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"ZIP verification failed: '{name}' differs from the staged file.");
            }
        }

        var missing = expected.Except(seen, StringComparer.OrdinalIgnoreCase).ToList();
        if (missing.Count > 0)
        {
            throw new WorkerException(ErrorCodes.ValidationFailed, $"ZIP verification failed: missing {string.Join(", ", missing)}.");
        }
    }

    internal static void CreateZip(string modDir, string zipPath)
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

    // The MO2 mod of a plugin; a master may be written with another letter case than the provider map's key.
    private static string? ProviderOf(IReadOnlyDictionary<string, string>? providers, string plugin)
    {
        if (providers is null)
        {
            return null;
        }

        return providers.TryGetValue(plugin, out var exact)
            ? exact
            : providers.FirstOrDefault(p => string.Equals(p.Key, plugin, StringComparison.OrdinalIgnoreCase)).Value;
    }
}
