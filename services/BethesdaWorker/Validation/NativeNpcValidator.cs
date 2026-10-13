using System.Text.RegularExpressions;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.Validation;

internal static partial class NativeNpcValidator
{
    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_]{0,127}$")]
    private static partial Regex EditorIdPattern();

    public static CreationValidateResponse Validate(LoadOrderSession session, CatalogQueries queries, CreationValidateRequest request)
    {
        ArgumentNullException.ThrowIfNull(request.Npc);
        var d = request.Npc;
        ArgumentNullException.ThrowIfNull(d.Name);
        ArgumentNullException.ThrowIfNull(d.EditorId);
        ArgumentNullException.ThrowIfNull(d.Description);
        ArgumentNullException.ThrowIfNull(d.RaceFormKey);
        ArgumentNullException.ThrowIfNull(d.VoiceFormKey);
        ArgumentNullException.ThrowIfNull(d.ClassFormKey);
        ArgumentNullException.ThrowIfNull(d.CombatStyleFormKey);
        ArgumentNullException.ThrowIfNull(d.HairColorFormKey);
        ArgumentNullException.ThrowIfNull(d.HeadTextureFormKey);
        ArgumentNullException.ThrowIfNull(d.Skills);
        ArgumentNullException.ThrowIfNull(d.HeadPartFormKeys);
        ArgumentNullException.ThrowIfNull(d.OutfitArmorFormKeys);
        ArgumentNullException.ThrowIfNull(d.Inventory);
        ArgumentNullException.ThrowIfNull(d.Factions);
        ArgumentNullException.ThrowIfNull(d.PackageFormKeys);
        var issues = new List<Issue>();
        var missing = new List<string>();
        var masters = new HashSet<ModKey>();
        try { OutfitPatchBuilder.ValidatePluginName(request.PluginName, session); }
        catch (WorkerException e) { issues.Add(Issue.Error("PluginNameInvalid", e.Message)); }
        if (!EditorIdPattern().IsMatch(d.EditorId)) issues.Add(Issue.Error("EditorIdInvalid", "EditorID must start with an ASCII letter or underscore and contain at most 128 ASCII letters, digits or underscores."));
        if (session.Plugins.SelectMany(p => p.Mod.EnumerateMajorRecords()).Any(r => string.Equals(r.EditorID, d.EditorId, StringComparison.OrdinalIgnoreCase)))
            issues.Add(Issue.Error("EditorIdConflict", "EditorID already exists in the load order."));
        if (string.IsNullOrWhiteSpace(d.Name) || d.Name.Length > 160 || d.Description.Length > 4096)
            issues.Add(Issue.Error("IdentityInvalid", "A name of 1–160 characters and description up to 4096 characters are required."));
        if (!float.IsFinite(d.Height) || d.Height is < 0.5f or > 2 || !float.IsFinite(d.Weight) || d.Weight is < 0 or > 100)
            issues.Add(Issue.Error("BodyInvalid", "Height must be 0.5–2 and weight 0–100, both finite."));
        if (d.Level is < 1 or > short.MaxValue || d.Health is < 1 or > ushort.MaxValue || d.Magicka is < 0 or > ushort.MaxValue || d.Stamina is < 0 or > ushort.MaxValue
            || d.MinLevel < 1 || d.MaxLevel < d.MinLevel || d.MaxLevel > short.MaxValue
            || d.LevelMultiplier is { } mult && (!float.IsFinite(mult) || mult is <= 0 or > 10))
            issues.Add(Issue.Error("StatsInvalid", "Statistics or level scaling are outside supported bounds."));
        foreach (var (skill, value) in d.Skills)
            if (!Enum.TryParse<Skill>(skill, out var parsed) || !Enum.IsDefined(parsed) || value is < 0 or > 100)
                issues.Add(Issue.Error("SkillInvalid", $"Unsupported skill or value: {skill}."));

        T? Resolve<T>(string text, bool required = false) where T : class, ISkyrimMajorRecordGetter
        {
            if (string.IsNullOrEmpty(text) && !required) return null;
            if (!queries.TryParseAnyFormKey(text, out var key) || !session.LinkCache.TryResolve<T>(key, out var record) || record.IsDeleted)
            {
                missing.Add(text); issues.Add(Issue.Error("ReferenceInvalid", $"Missing or wrong-type {typeof(T).Name}: '{text}'.", text)); return null;
            }
            masters.Add(key.ModKey);
            return record;
        }

        var race = Resolve<IRaceGetter>(d.RaceFormKey, true);
        Resolve<IVoiceTypeGetter>(d.VoiceFormKey, true);
        Resolve<IClassGetter>(d.ClassFormKey, true);
        Resolve<ICombatStyleGetter>(d.CombatStyleFormKey);
        Resolve<IColorRecordGetter>(d.HairColorFormKey);
        Resolve<ITextureSetGetter>(d.HeadTextureFormKey);
        if (d.HeadPartFormKeys.Count > 64 || d.OutfitArmorFormKeys.Count > 64 || d.Inventory.Count > 256 || d.Factions.Count > 128 || d.PackageFormKeys.Count > 64)
            issues.Add(Issue.Error("DraftTooLarge", "Draft collection limits exceeded."));
        var headTypes = new HashSet<HeadPart.TypeEnum>();
        var seen = new HashSet<FormKey>();
        foreach (var text in d.HeadPartFormKeys)
        {
            if (Resolve<IHeadPartGetter>(text, true) is not { } part) continue;
            if (!seen.Add(part.FormKey)) issues.Add(Issue.Error("HeadPartDuplicate", "Duplicate head part.", text));
            var male = part.Flags.HasFlag(HeadPart.Flag.Male); var female = part.Flags.HasFlag(HeadPart.Flag.Female);
            if ((male || female) && (d.Female ? !female : !male)) issues.Add(Issue.Error("HeadPartSex", "Head part does not support this sex.", text));
            if (!part.Flags.HasFlag(HeadPart.Flag.IsExtraPart) && part.Type is { } type && type != HeadPart.TypeEnum.Misc && !headTypes.Add(type))
                issues.Add(Issue.Error("HeadPartTypeConflict", "Multiple head parts of the same type.", text));
            if (part.ValidRaces.FormKeyNullable is { } listKey)
            {
                var list = Resolve<IFormListGetter>(listKey.ToString(), true);
                if (list is not null && (race is null || !list.Items.Any(x => x.FormKey == race.FormKey)))
                    issues.Add(Issue.Error("HeadPartRace", "Head part is not valid for this race.", text));
            }
        }
        if (d.HeadPartFormKeys.Count == 0) issues.Add(Issue.Warning("HeadPartsEmpty", "No explicit head parts; native facial resources remain incomplete."));
        seen.Clear();
        foreach (var item in d.Inventory)
        {
            var record = Resolve<IItemGetter>(item.FormKey, true);
            if (item.Count is < 1 or > 100000 || record is not null && !seen.Add(record.FormKey))
                issues.Add(Issue.Error("InventoryInvalid", "Inventory quantities must be 1–100000 with no duplicate entries.", item.FormKey));
        }
        seen.Clear();
        foreach (var f in d.Factions)
        {
            var record = Resolve<IFactionGetter>(f.FormKey, true);
            if (f.Rank is < -1 or > 127 || record is not null && !seen.Add(record.FormKey))
                issues.Add(Issue.Error("FactionInvalid", "Faction rank must be -1–127 and memberships unique.", f.FormKey));
        }
        foreach (var key in d.PackageFormKeys) Resolve<IPackageGetter>(key, true);
        if (d.OutfitArmorFormKeys.Distinct(StringComparer.OrdinalIgnoreCase).Count() != d.OutfitArmorFormKeys.Count)
            issues.Add(Issue.Error("OutfitDuplicate", "Outfit pieces must be unique."));
        if (d.PackageFormKeys.Count > 0) issues.Add(Issue.Warning("PackageBehaviorUnverified", "Package references resolve; quest-specific conditions and behavior require manual validation."));
        if (d.Placement is { } p)
        {
            var cell = Resolve<ICellGetter>(p.CellFormKey, true);
            if (cell is not null && !cell.Flags.HasFlag(Cell.Flag.IsInteriorCell))
                issues.Add(Issue.Error("ExteriorPlacementUnsupported", "Only explicitly selected interior cells are supported; exterior placement needs worldspace context."));
            if (!float.IsFinite(p.X) || !float.IsFinite(p.Y) || !float.IsFinite(p.Z) || !float.IsFinite(p.RotationZ))
                issues.Add(Issue.Error("PlacementInvalid", "Placement coordinates must be finite."));
        }
        if (d.Face is { } face)
        {
            ArgumentNullException.ThrowIfNull(face.Sliders);
            ArgumentNullException.ThrowIfNull(face.Presets);
            ArgumentNullException.ThrowIfNull(face.Tints);
            ArgumentNullException.ThrowIfNull(face.SkinTone);
            if (face.HairColor is not null) issues.Add(Issue.Error("HairColorReferenceRequired", "Use hairColorFormKey to reference a resolved hair color record."));
            if (face.Sliders.Count != 18 || face.Presets.Count != 4 || face.Sliders.Any(v => !float.IsFinite(v) || v is < -1 or > 1)
                || face.Presets.Any(v => v is < -1 or > 65535) || face.Weight != d.Weight || face.Tints.Count > 64)
                issues.Add(Issue.Error("FaceValuesInvalid", "Face needs 18 finite sliders (-1–1), four presets, and weight matching the NPC."));
            foreach (var tint in face.Tints)
                if (tint.Index is < 0 or > ushort.MaxValue || tint.Preset is < short.MinValue or > short.MaxValue || !float.IsFinite(tint.Value) || tint.Value is < 0 or > 1)
                    issues.Add(Issue.Error("TintInvalid", "Tint values are outside the record's bounds."));
        }
        OutfitValidateResponse? outfit = null;
        if (race is not null)
        {
            var npc = new Npc(new FormKey(ModKey.FromFileName("CharacterForgeValidation.esp"), 0x800), SkyrimRelease.SkyrimSE);
            npc.Race.SetTo(race.FormKey);
            if (d.Female) npc.Configuration.Flags |= NpcConfiguration.Flag.Female;
            outfit = OutfitValidator.ValidateForNpc(session, queries, npc, d.OutfitArmorFormKeys);
            issues.AddRange(outfit.Issues.Where(i => i.Code != "SaveGamePersistence"));
            foreach (var key in d.OutfitArmorFormKeys) Resolve<IArmorGetter>(key, true);
            foreach (var piece in outfit.Pieces)
                if (piece.AddonsForRace == 0 || piece.Meshes.Count == 0)
                    issues.Add(Issue.Error("OutfitMeshMissing", "Outfit has no model for the selected race/sex.", piece.Armor.FormKey));
        }
        var technical = !issues.Any(i => i.Severity == "error");
        issues.Add(Issue.Error("NativeFaceGenUnavailable", "Native FaceGen NIF and FaceTint DDS are not generated/verified. Final playable export is blocked; technical plugins must not be installed."));
        return new CreationValidateResponse
        {
            Exportable = false, TechnicallyExportable = technical, Issues = issues, MissingReferences = missing.Distinct().ToArray(),
            Masters = session.Plugins.Where(p => masters.Contains(p.ModKey)).Select(p => p.ModKey.FileName.String).ToArray(), Outfit = outfit,
        };
    }
}
