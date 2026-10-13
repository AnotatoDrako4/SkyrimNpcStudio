using System.Text.RegularExpressions;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.Validation;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>What the builder put into the plugin, for the manifest and the response.</summary>
internal sealed record BuiltAssignment(
    FormKey Npc, string? NpcEditorId, string? NpcName, FormKey Outfit, string OutfitEditorId,
    IReadOnlyList<ArmorSummary> Armors);

internal sealed record BuiltGeneratedAsset(string Npc, string DataPath, string Sha256, string Kind);

internal sealed record BuiltFace(string Npc, string? NpcEditorId, FaceGenProvenance Provenance, IReadOnlyList<string> ChangedFields);
internal sealed record BuiltStandaloneOutfit(FormKey Outfit, string EditorId, IReadOnlyList<ArmorSummary> Armors);

internal sealed record BuiltPatch(
    string PluginPath, IReadOnlyList<ModKey> Masters, bool EslFlagged,
    IReadOnlyList<ExportedRecord> Records, IReadOnlyList<BuiltAssignment> Assignments, IReadOnlyList<Issue> Issues,
    IReadOnlyList<BuiltGeneratedAsset>? Generated = null, IReadOnlyList<BuiltFace>? Faces = null,
    IReadOnlyList<BuiltStandaloneOutfit>? StandaloneOutfits = null);

/// <summary>
/// Mode A (Outfit Patch): one new OTFT per NPC plus a minimal NPC override that only changes the
/// outfit. The override is a copy of the WINNING record, so edits made by other mods survive.
/// Never modifies existing plugins; writes a new file and verifies it by reading it back.
/// </summary>
internal static partial class OutfitPatchBuilder
{
    /// <summary>ESL-flagged plugins may only define new records in this local FormID range.</summary>
    public const uint LightRangeMin = 0x800;
    public const uint LightRangeMax = 0xFFF;

    [GeneratedRegex(@"^[A-Za-z0-9][A-Za-z0-9 _\-\.\(\)\[\]']{0,95}\.esp$")]
    private static partial Regex PluginNamePattern();

    [GeneratedRegex(@"[^A-Za-z0-9_]")]
    private static partial Regex NonEditorIdChars();

    public static void ValidatePluginName(string pluginName, LoadOrderSession session)
    {
        if (!PluginNamePattern().IsMatch(pluginName))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument,
                $"'{pluginName}' is not an allowed plugin name (letters, digits, spaces and _-.()[]' only, ending in .esp).");
        }

        if (session.Find(ModKey.FromFileName(pluginName)) is not null)
        {
            throw new WorkerException(ErrorCodes.ValidationFailed,
                $"A plugin named '{pluginName}' is already part of the active load order. Choose another name or disable it first.");
        }
    }

    public static BuiltPatch Build(
        LoadOrderSession session, CatalogQueries queries, ExportOutfitPatchRequest request, string pluginPath, CancellationToken ct)
    {
        ValidatePluginName(request.PluginName, session);
        if (request.Assignments.Count == 0 && request.StandaloneOutfits.Count == 0)
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "At least one outfit assignment or standalone outfit is required.");
        }

        var issues = new List<Issue>();
        var resolved = new List<(INpcGetter Npc, OutfitAssignment Assignment, OutfitValidateResponse Validation)>();
        var npcsSeen = new HashSet<FormKey>();
        foreach (var assignment in request.Assignments)
        {
            ct.ThrowIfCancellationRequested();
            var validation = OutfitValidator.Validate(session, queries, new OutfitValidateRequest
            {
                NpcFormKey = assignment.NpcFormKey,
                ArmorFormKeys = assignment.ArmorFormKeys,
            });
            issues.AddRange(validation.Issues.Where(i => i.Code != "SaveGamePersistence"));
            if (!validation.Exportable)
            {
                continue;
            }

            if (!queries.TryParseAnyFormKey(assignment.NpcFormKey, out var npcKey))
            {
                continue; // unreachable: Validate already rejected unparsable keys
            }

            if (!npcsSeen.Add(npcKey))
            {
                issues.Add(Issue.Error("DuplicateNpc", "The same NPC has two outfit assignments in one patch.", npcKey.ToString()));
                continue;
            }

            resolved.Add((session.LinkCache.Resolve<INpcGetter>(npcKey), assignment, validation));
        }

        var standaloneResolved = new List<(StandaloneOutfit Request, IReadOnlyList<OutfitPiece> Pieces)>();
        foreach (var standalone in request.StandaloneOutfits)
        {
            ct.ThrowIfCancellationRequested();
            if (string.IsNullOrWhiteSpace(standalone.Name) || standalone.ArmorFormKeys.Count == 0)
            {
                issues.Add(Issue.Error("EmptyStandaloneOutfit", "A standalone outfit needs a name and at least one armor.", standalone.Name));
                continue;
            }
            var pieces = new List<OutfitPiece>();
            var seen = new HashSet<FormKey>();
            uint occupied = 0;
            foreach (var text in standalone.ArmorFormKeys)
            {
                if (!queries.TryParseAnyFormKey(text, out var key) ||
                    !session.LinkCache.TryResolve<IArmorGetter>(key, out _) || queries.GetArmor(key.ToString()) is not { } armor)
                {
                    issues.Add(Issue.Error("ArmorNotFound", $"Armor '{text}' is not in the active load order.", text));
                    continue;
                }
                if (!seen.Add(key))
                {
                    issues.Add(Issue.Error("DuplicatePiece", $"Armor '{text}' appears twice.", text));
                    continue;
                }
                if ((occupied & armor.SlotMask) != 0)
                    issues.Add(Issue.Error("SlotConflict", $"Armor '{text}' conflicts with another biped slot in this outfit.", text));
                occupied |= armor.SlotMask;
                pieces.Add(new OutfitPiece { Armor = armor, AddonsForRace = 0, Meshes = [] });
            }
            standaloneResolved.Add((standalone, pieces));
        }

        if (issues.Any(i => i.Severity == "error"))
        {
            throw new WorkerException(ErrorCodes.ValidationFailed, "Validation failed; nothing was written.", issues);
        }

        if (request.Assignments.Count > 0)
        {
            issues.Add(Issue.Info("SaveGamePersistence",
                "NPCs already loaded in an existing save can keep their current equipment until the game re-initializes them; immediate application in every save is not guaranteed."));
        }

        var patchKey = ModKey.FromFileName(request.PluginName);
        var patch = new SkyrimMod(patchKey, SkyrimRelease.SkyrimSE);
        patch.ModHeader.Author = Truncate(request.Author, 255);
        patch.ModHeader.Description = Truncate(request.Description ?? "Generated by Skyrim NPC Studio", 511);
        if (request.EslFlag)
        {
            patch.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Small;
        }

        var built = new List<BuiltAssignment>();
        var standaloneBuilt = new List<BuiltStandaloneOutfit>();
        var records = new List<ExportedRecord>();
        var usedEditorIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (npc, assignment, validation) in resolved)
        {
            var editorId = UniqueEditorId(assignment.OutfitEditorId ?? $"SNS_{npc.EditorID ?? npc.FormKey.ID.ToString("X6")}Outfit", usedEditorIds);
            var outfit = patch.Outfits.AddNew(editorId);

            var (itemKeys, outfitPieces) = ResolveOutfitItems(validation.Pieces, assignment.MaterialOverrides);
            outfit.Items = new ExtendedList<IFormLinkGetter<IOutfitTargetGetter>>(
                itemKeys.Select(k => (IFormLinkGetter<IOutfitTargetGetter>)new FormLink<IOutfitTargetGetter>(k)));

            var npcOverride = patch.Npcs.GetOrAddAsOverride(npc);
            npcOverride.DefaultOutfit.SetTo(outfit.FormKey);
            if (assignment.AlsoSetSleepingOutfit)
            {
                npcOverride.SleepingOutfit.SetTo(outfit.FormKey);
            }

            records.Add(new ExportedRecord { FormKey = outfit.FormKey.ToString(), RecordType = "OTFT", EditorId = editorId, Kind = "new" });
            records.Add(new ExportedRecord { FormKey = npc.FormKey.ToString(), RecordType = "NPC_", EditorId = npc.EditorID, Kind = "override" });
            built.Add(new BuiltAssignment(npc.FormKey, npc.EditorID, npc.Name?.String, outfit.FormKey, editorId, outfitPieces));
        }

        foreach (var (standalone, pieces) in standaloneResolved)
        {
            var editorId = UniqueEditorId($"SNS_{standalone.Name}_Outfit", usedEditorIds);
            var outfit = patch.Outfits.AddNew(editorId);
            var (itemKeys, outfitPieces) = ResolveOutfitItems(pieces, standalone.MaterialOverrides);
            outfit.Items = new ExtendedList<IFormLinkGetter<IOutfitTargetGetter>>(
                itemKeys.Select(k => (IFormLinkGetter<IOutfitTargetGetter>)new FormLink<IOutfitTargetGetter>(k)));
            records.Add(new ExportedRecord { FormKey = outfit.FormKey.ToString(), RecordType = "OTFT", EditorId = editorId, Kind = "new" });
            standaloneBuilt.Add(new BuiltStandaloneOutfit(outfit.FormKey, editorId, outfitPieces));
        }

        if (request.EslFlag)
        {
            foreach (var record in patch.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == patchKey))
            {
                if (record.FormKey.ID is < LightRangeMin or > LightRangeMax)
                {
                    throw new WorkerException(ErrorCodes.ValidationFailed,
                        $"ESL flag rejected: new record {record.FormKey} is outside the light-plugin range 0x800–0xFFF.");
                }
            }
        }

        var loadOrderKeys = session.Plugins.Select(p => p.ModKey).ToArray();
        patch.WriteToBinary(pluginPath, new BinaryWriteParameters
        {
            MastersListOrdering = new MastersListOrderingByLoadOrder(loadOrderKeys),
            MasterFlagsLookup = session.MasterStyles,
        });

        var masters = Verify(session, pluginPath, patchKey, built, standaloneBuilt, request.EslFlag, issues);
        return new BuiltPatch(pluginPath, masters, request.EslFlag, records, built, issues, StandaloneOutfits: standaloneBuilt);
    }

    /// <summary>
    /// Re-reads the written plugin and checks it independently of the in-memory model:
    /// masters present and in load order, overrides point at the new outfits, items match,
    /// ESL range respected and every reference resolvable.
    /// </summary>
    private static IReadOnlyList<ModKey> Verify(
        LoadOrderSession session, string pluginPath, ModKey patchKey, IReadOnlyList<BuiltAssignment> built,
        IReadOnlyList<BuiltStandaloneOutfit> standalone, bool esl, List<Issue> issues)
    {
        using var written = SkyrimMod.CreateFromBinaryOverlay(
            new ModPath(patchKey, pluginPath), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = session.MasterStyles });

        var masters = written.ModHeader.MasterReferences.Select(m => m.Master).ToList();
        var lastIndex = -1;
        foreach (var master in masters)
        {
            var plugin = session.Find(master)
                ?? throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: master '{master}' is not in the load order.");
            if (plugin.LoadIndex < lastIndex)
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, "Verification failed: masters are not in load order.");
            }

            lastIndex = plugin.LoadIndex;
        }

        if (esl != written.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small))
        {
            throw new WorkerException(ErrorCodes.ValidationFailed, "Verification failed: ESL flag mismatch after writing.");
        }

        var npcs = written.Npcs.ToDictionary(n => n.FormKey);
        var outfits = written.Outfits.ToDictionary(o => o.FormKey);
        if (npcs.Count != built.Count || outfits.Count != built.Count + standalone.Count)
        {
            throw new WorkerException(ErrorCodes.ValidationFailed,
                $"Verification failed: expected {built.Count} NPC overrides and {built.Count + standalone.Count} outfits, found {npcs.Count} and {outfits.Count}.");
        }

        foreach (var assignment in built)
        {
            if (!npcs.TryGetValue(assignment.Npc, out var npc) || npc.DefaultOutfit.FormKeyNullable != assignment.Outfit)
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: override of {assignment.Npc} does not use the new outfit.");
            }

            var items = outfits[assignment.Outfit].Items?.Select(i => i.FormKey.ToString()).ToArray() ?? [];
            if (!items.SequenceEqual(assignment.Armors.Select(a => a.FormKey)))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: outfit {assignment.Outfit} items differ from the request.");
            }

            if (esl && assignment.Outfit.ID is < LightRangeMin or > LightRangeMax)
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: {assignment.Outfit} is outside the ESL range.");
            }
        }

        foreach (var item in standalone)
        {
            if (!outfits.TryGetValue(item.Outfit, out var outfit))
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: standalone outfit {item.Outfit} is absent.");
            var items = outfit.Items?.Select(i => i.FormKey.ToString()).ToArray() ?? [];
            if (!items.SequenceEqual(item.Armors.Select(a => a.FormKey)))
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: standalone outfit {item.Outfit} items differ from the request.");
            if (esl && item.Outfit.ID is < LightRangeMin or > LightRangeMax)
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: {item.Outfit} is outside the ESL range.");
        }

        foreach (var record in written.EnumerateMajorRecords())
        {
            foreach (var link in record.EnumerateFormLinks())
            {
                if (link.IsNull || link.FormKey.ModKey == patchKey)
                {
                    continue;
                }

                if (!session.LinkCache.TryResolve(link.FormKey, link.Type, out _))
                {
                    issues.Add(Issue.Warning("UnresolvedReference",
                        $"{record.FormKey} references {link.FormKey}, which does not resolve in the active load order (inherited from the winning record).",
                        record.FormKey.ToString()));
                }
            }
        }

        return masters;
    }

    internal static (IReadOnlyList<FormKey> ItemKeys, IReadOnlyList<ArmorSummary> Pieces) ResolveOutfitItems(
        IReadOnlyList<OutfitPiece> pieces, IReadOnlyList<PieceMaterialDto>? overrides)
    {
        // A TXST record by itself does not recolor a Skyrim mesh. The former path accepted
        // arbitrary DDS names without generating or staging those files, and wrote an alternate
        // texture whose Name was the ARMA EditorID rather than the NIF shape name. That could
        // produce a readable ESP which rendered the original outfit in game. Fail closed until
        // the exporter receives verified generated DDS assets and a validated shape mapping.
        if (overrides is { Count: > 0 })
        {
            throw new WorkerException(ErrorCodes.ValidationFailed,
                "Material variants are preview-only: no generated DDS and verified NIF shape mapping were supplied.",
                [Issue.Error("MaterialVariantPreviewOnly",
                    "This material edit has no verified Skyrim texture resources and shape mapping. Clear it before export.")]);
        }
        return (pieces.Select(piece => FormKey.Factory(piece.Armor.FormKey)).ToArray(),
            pieces.Select(piece => piece.Armor).ToArray());
    }

    internal static string UniqueEditorId(string candidate, HashSet<string> used)
    {
        var baseId = NonEditorIdChars().Replace(candidate, "_");
        if (baseId.Length > 90)
        {
            baseId = baseId[..90];
        }

        var id = baseId;
        for (var n = 2; !used.Add(id); n++)
        {
            id = $"{baseId}{n}";
        }

        return id;
    }

    private static string Truncate(string value, int max) => value.Length <= max ? value : value[..max];
}
