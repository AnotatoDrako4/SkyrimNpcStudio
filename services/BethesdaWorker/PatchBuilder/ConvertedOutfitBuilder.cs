using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>
/// Dresses one NPC in meshes converted to another body family. A new ARMA (the base addon, for the NPC's race only, with the
/// converted female mesh) and a new ARMO (the base armor, with only that addon) per piece, a new OTFT and a minimal NPC override
/// that only changes DefaultOutfit. The base records stay untouched, so nobody else changes and no runtime patcher is needed.
/// The caller (who owns the MO2 VFS) checks that the meshes exist; here they are only checked to be well-formed.
/// </summary>
internal static class ConvertedOutfitBuilder
{
    public static BuiltPatch Build(
        LoadOrderSession session, CatalogQueries queries, ExportConvertedOutfitRequest request, string pluginPath, CancellationToken ct)
    {
        OutfitPatchBuilder.ValidatePluginName(request.PluginName, session);
        if (request.Pieces.Count == 0)
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "At least one converted piece is required.");
        }

        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var npcKey) || !session.LinkCache.TryResolve<INpcGetter>(npcKey, out var npc))
        {
            throw new WorkerException(ErrorCodes.RecordNotFound, $"NPC '{request.NpcFormKey}' is not in the active load order.");
        }

        var issues = new List<Issue>();
        if (!npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female))
        {
            issues.Add(Issue.Error("NotFemale", "Converted pieces carry a female mesh only; this NPC is male.", npcKey.ToString()));
        }

        if (npc.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits) || npc.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Inventory))
        {
            issues.Add(Issue.Error("TemplateControlled", "A template controls this NPC's race or outfit: an override of DefaultOutfit would not reach the game.", npcKey.ToString()));
        }

        if (npc.Race.FormKeyNullable is not { } raceKey || !session.LinkCache.TryResolve<IRaceGetter>(raceKey, out _))
        {
            throw new WorkerException(ErrorCodes.RecordNotFound, "NPC race is unresolved.");
        }

        var sources = new List<(IArmorGetter Armor, IArmorAddonGetter Addon, ArmorSummary Summary, string Mesh)>();
        var seen = new HashSet<FormKey>();
        uint occupied = 0;
        foreach (var piece in request.Pieces)
        {
            ct.ThrowIfCancellationRequested();
            if (!queries.TryParseAnyFormKey(piece.BaseArmorFormKey, out var key) ||
                !session.LinkCache.TryResolve<IArmorGetter>(key, out var armor) || queries.GetArmor(key.ToString()) is not { } summary)
            {
                issues.Add(Issue.Error("ArmorNotFound", $"Armor '{piece.BaseArmorFormKey}' is not in the active load order.", piece.BaseArmorFormKey));
                continue;
            }

            if (!seen.Add(key))
            {
                issues.Add(Issue.Error("DuplicatePiece", $"Armor '{piece.BaseArmorFormKey}' appears twice.", piece.BaseArmorFormKey));
                continue;
            }

            if ((occupied & summary.SlotMask) != 0)
            {
                issues.Add(Issue.Error("SlotConflict", $"Armor '{piece.BaseArmorFormKey}' conflicts with another biped slot in this outfit.", piece.BaseArmorFormKey));
            }

            occupied |= summary.SlotMask;
            if (!IsMeshPath(piece.FemaleMesh))
            {
                issues.Add(Issue.Error("InvalidMeshPath", $"'{piece.FemaleMesh}' is not a NIF path relative to meshes\\.", piece.BaseArmorFormKey));
                continue;
            }

            // One addon with a female model: several would need a choice this request does not express.
            var addons = armor.Armature.Select(a => a.TryResolve(session.LinkCache)).Where(a => !string.IsNullOrEmpty(a?.WorldModel?.Female?.File.GivenPath)).ToList();
            if (addons.Count != 1)
            {
                issues.Add(Issue.Error("AmbiguousBaseAddon", $"Armor '{piece.BaseArmorFormKey}' has {addons.Count} addons with a female model; exactly one is needed.", piece.BaseArmorFormKey));
                continue;
            }

            sources.Add((armor, addons[0]!, summary, piece.FemaleMesh));
        }

        if (issues.Any(i => i.Severity == "error"))
        {
            throw new WorkerException(ErrorCodes.ValidationFailed, "Validation failed; nothing was written.", issues);
        }

        issues.Add(Issue.Info("SaveGamePersistence",
            "NPCs already loaded in an existing save can keep their current equipment until the game re-initializes them."));

        var patchKey = ModKey.FromFileName(request.PluginName);
        var patch = new SkyrimMod(patchKey, SkyrimRelease.SkyrimSE);
        patch.ModHeader.Author = request.Author;
        patch.ModHeader.Description = request.Description ?? "Generated by Skyrim NPC Studio";
        if (request.EslFlag)
        {
            patch.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Small;
        }

        var records = new List<ExportedRecord>();
        var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var armors = new List<ArmorSummary>();
        var expected = new List<(FormKey Armor, FormKey Addon, string Mesh)>();
        foreach (var (armor, baseAddon, summary, mesh) in sources)
        {
            var name = armor.EditorID ?? armor.FormKey.ID.ToString("X6");
            var addon = patch.ArmorAddons.DuplicateInAsNewRecord(baseAddon);
            addon.EditorID = OutfitPatchBuilder.UniqueEditorId($"SNS_Conv_{name}_AA", used);
            addon.WorldModel = new GenderedItem<Model?>(addon.WorldModel?.Male, new Model { File = mesh });
            addon.Race.SetTo(raceKey);
            addon.AdditionalRaces.Clear();

            var copy = patch.Armors.DuplicateInAsNewRecord(armor);
            copy.EditorID = OutfitPatchBuilder.UniqueEditorId($"SNS_Conv_{name}", used);
            copy.TemplateArmor.Clear();
            copy.Armature.Clear();
            copy.Armature.Add(addon.FormKey);

            records.Add(new ExportedRecord { FormKey = addon.FormKey.ToString(), RecordType = "ARMA", EditorId = addon.EditorID, Kind = "new" });
            records.Add(new ExportedRecord { FormKey = copy.FormKey.ToString(), RecordType = "ARMO", EditorId = copy.EditorID, Kind = "new" });
            armors.Add(summary with
            {
                FormKey = copy.FormKey.ToString(),
                FormId = copy.FormKey.ID.ToString("X8"),
                EditorId = copy.EditorID,
                OriginPlugin = request.PluginName,
                WinningPlugin = request.PluginName,
                AddonCount = 1,
            });
            expected.Add((copy.FormKey, addon.FormKey, mesh));
        }

        var outfitId = OutfitPatchBuilder.UniqueEditorId(request.OutfitEditorId ?? $"SNS_{npc.EditorID ?? npc.FormKey.ID.ToString("X6")}ConvertedOutfit", used);
        var outfit = patch.Outfits.AddNew(outfitId);
        outfit.Items = new ExtendedList<IFormLinkGetter<IOutfitTargetGetter>>(
            expected.Select(e => (IFormLinkGetter<IOutfitTargetGetter>)new FormLink<IOutfitTargetGetter>(e.Armor)));
        var npcOverride = patch.Npcs.GetOrAddAsOverride(npc);
        npcOverride.DefaultOutfit.SetTo(outfit.FormKey);
        records.Add(new ExportedRecord { FormKey = outfit.FormKey.ToString(), RecordType = "OTFT", EditorId = outfitId, Kind = "new" });
        records.Add(new ExportedRecord { FormKey = npc.FormKey.ToString(), RecordType = "NPC_", EditorId = npc.EditorID, Kind = "override" });

        if (request.EslFlag)
        {
            foreach (var record in patch.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == patchKey))
            {
                if (record.FormKey.ID is < OutfitPatchBuilder.LightRangeMin or > OutfitPatchBuilder.LightRangeMax)
                {
                    throw new WorkerException(ErrorCodes.ValidationFailed, $"ESL flag rejected: new record {record.FormKey} is outside the light-plugin range 0x800–0xFFF.");
                }
            }
        }

        patch.WriteToBinary(pluginPath, new BinaryWriteParameters
        {
            MastersListOrdering = new MastersListOrderingByLoadOrder(session.Plugins.Select(p => p.ModKey).ToArray()),
            MasterFlagsLookup = session.MasterStyles,
        });

        var built = new[] { new BuiltAssignment(npc.FormKey, npc.EditorID, npc.Name?.String, outfit.FormKey, outfitId, armors) };
        var masters = OutfitPatchBuilder.Verify(session, pluginPath, patchKey, built, [], request.EslFlag,
            new Dictionary<FormKey, IReadOnlySet<string>> { [npc.FormKey] = NpcSafePatch.AllowedForOutfit(false) }, out var diffs, issues);
        VerifyPieces(session, pluginPath, patchKey, raceKey, expected);
        return new BuiltPatch(pluginPath, masters, request.EslFlag, records, built, issues, SemanticDiffs: diffs);
    }

    /// <summary>The written plugin, read back: each new armor holds exactly its new addon, for the NPC's race, with the requested mesh.</summary>
    private static void VerifyPieces(LoadOrderSession session, string pluginPath, ModKey patchKey, FormKey raceKey,
        IReadOnlyList<(FormKey Armor, FormKey Addon, string Mesh)> expected)
    {
        using var written = SkyrimMod.CreateFromBinaryOverlay(new ModPath(patchKey, pluginPath), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = session.MasterStyles });
        var armors = written.Armors.ToDictionary(a => a.FormKey);
        var addons = written.ArmorAddons.ToDictionary(a => a.FormKey);
        foreach (var (armor, addon, mesh) in expected)
        {
            if (!armors.TryGetValue(armor, out var a) || a.Armature.Count != 1 || a.Armature[0].FormKey != addon ||
                !addons.TryGetValue(addon, out var aa) || aa.Race.FormKeyNullable != raceKey || aa.AdditionalRaces.Count != 0 ||
                !string.Equals(aa.WorldModel?.Female?.File.GivenPath, mesh, StringComparison.OrdinalIgnoreCase))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: converted piece {armor} does not match the request after writing.");
            }
        }
    }

    private static bool IsMeshPath(string path) =>
        path.EndsWith(".nif", StringComparison.OrdinalIgnoreCase) && !Path.IsPathRooted(path) && !path.StartsWith("meshes\\", StringComparison.OrdinalIgnoreCase) &&
        !path.Split('\\', '/').Contains("..");
}
