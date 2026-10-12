using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

internal static class AppearanceQueries
{
    public static AppearanceResponse Get(LoadOrderSession session, CatalogQueries queries, AppearanceRequest request)
    {
        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var key)
            || !session.LinkCache.TryResolve<INpcGetter>(key, out var npc))
            throw new WorkerException(ErrorCodes.RecordNotFound, "NPC not found: " + request.NpcFormKey);
        if (npc.Template.FormKeyNullable is not null && npc.Configuration.TemplateFlags != 0)
            throw new WorkerException(ErrorCodes.ValidationFailed, "Template inheritance requires a dedicated appearance adapter for this NPC.");
        if (!session.LinkCache.TryResolve<IRaceGetter>(npc.Race.FormKey, out var race))
            throw new WorkerException(ErrorCodes.RecordNotFound, "NPC race is unresolved.");

        var female = npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var races = new HashSet<FormKey> { race.FormKey };
        if (race.ArmorRace.FormKeyNullable is { } armorRace) races.Add(armorRace);
        var pieces = new List<AppearancePiece>();
        void AddArmor(FormKey armorKey, string source)
        {
            if (!session.LinkCache.TryResolve<IArmorGetter>(armorKey, out var armor))
                throw new WorkerException(ErrorCodes.RecordNotFound, "Unresolved armor: " + armorKey);
            var addons = new List<AppearanceAddon>();
            foreach (var link in armor.Armature)
            {
                if (!session.LinkCache.TryResolve<IArmorAddonGetter>(link.FormKey, out var addon)) continue;
                if (!(addon.Race.FormKeyNullable is { } r && races.Contains(r))
                    && !addon.AdditionalRaces.Any(r => races.Contains(r.FormKey))) continue;
                var path = (female ? addon.WorldModel?.Female : addon.WorldModel?.Male)?.File.GivenPath;
                if (string.IsNullOrEmpty(path)) continue;
                var slider = female ? addon.WeightSliderEnabled.Female : addon.WeightSliderEnabled.Male;
                addons.Add(new AppearanceAddon(addon.FormKey.ToString(), MeshPath(path), slider,
                    (uint)(addon.BodyTemplate?.FirstPersonFlags ?? 0)));
            }
            pieces.Add(new AppearancePiece(armorKey.ToString(), armor.EditorID ?? armorKey.ToString(), source,
                (uint)(armor.BodyTemplate?.FirstPersonFlags ?? 0), addons));
        }

        if ((npc.WornArmor.FormKeyNullable ?? race.Skin.FormKeyNullable) is { } skin) AddArmor(skin, "skin");
        if (request.ArmorFormKeys is { } requested)
        {
            foreach (var armor in requested.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                if (!queries.TryParseAnyFormKey(armor, out var armorKey))
                    throw new WorkerException(ErrorCodes.InvalidArgument, "Invalid armor FormKey: " + armor);
                AddArmor(armorKey, "outfit");
            }
        }
        else if (npc.DefaultOutfit.FormKeyNullable is { } outfitKey
            && session.LinkCache.TryResolve<IOutfitGetter>(outfitKey, out var outfit))
        {
            foreach (var item in outfit.Items ?? [])
                if (session.LinkCache.TryResolve<IArmorGetter>(item.FormKey, out _)) AddArmor(item.FormKey, "outfit");
        }

        var head = new List<AppearanceHeadPart>();
        foreach (var link in npc.HeadParts)
        {
            if (session.LinkCache.TryResolve<IHeadPartGetter>(link.FormKey, out var part))
            {
                head.Add(new AppearanceHeadPart(
                    part.EditorID ?? "",
                    part.Type?.ToString() ?? "",
                    part.FormKey.ToString(),
                    MeshPath(part.Model?.File.GivenPath ?? "")));
            }
        }

        var face = CatalogQueries.FaceGenFor(key);
        var faceGeomPath = face.GeometryPath;
        var faceTintPath = face.TintPath;

        AppearanceColor? hairColor = null;
        if (npc.HairColor.FormKeyNullable is { } colorKey
            && session.LinkCache.TryResolve<IColorRecordGetter>(colorKey, out var color))
            hairColor = new AppearanceColor(color.Color.R, color.Color.G, color.Color.B);

        var skinTone = npc.TextureLighting is { } lighting
            ? new AppearanceColor(lighting.R, lighting.G, lighting.B) : null;

        var weight = npc.Weight;
        if (request.DonorOverride is { } donor)
        {
            if (!string.IsNullOrEmpty(donor.DonorFaceGeometryPath)) faceGeomPath = donor.DonorFaceGeometryPath;
            if (!string.IsNullOrEmpty(donor.DonorFaceTintPath)) faceTintPath = donor.DonorFaceTintPath;
            if (donor.HairColor is not null) hairColor = donor.HairColor;
            if (donor.SkinTone is not null) skinTone = donor.SkinTone;
            if (donor.HeadParts is { Count: > 0 }) head = donor.HeadParts.ToList();
            if (donor.Weight.HasValue) weight = donor.Weight.Value;
        }

        FaceMorphsDto? morphs = null;
        if (npc.FaceMorph is { } fm)
        {
            morphs = new FaceMorphsDto(
                fm.NoseLongVsShort, fm.NoseUpVsDown,
                fm.JawUpVsDown, fm.JawNarrowVsWide, fm.JawForwardVsBack,
                fm.CheeksUpVsDown, fm.CheeksForwardVsBack,
                fm.EyesUpVsDown, fm.EyesInVsOut,
                fm.BrowsUpVsDown, fm.BrowsInVsOut, fm.BrowsForwardVsBack,
                fm.LipsUpVsDown, fm.LipsInVsOut,
                fm.ChinNarrowVsWide, fm.ChinUpVsDown, fm.ChinUnderbiteVsOverbite,
                fm.EyesForwardVsBack);
        }

        FacePartsDto? parts = null;
        if (npc.FaceParts is { } fp)
        {
            parts = new FacePartsDto(fp.Nose, fp.Eyes, fp.Mouth);
        }

        var tints = new List<TintLayerDto>();
        if (npc.TintLayers is { } tl)
        {
            foreach (var t in tl)
            {
                var c = t.Color;
                var col = c.HasValue ? new AppearanceColor(c.Value.R, c.Value.G, c.Value.B) : new AppearanceColor(255, 255, 255);
                tints.Add(new TintLayerDto(t.Index ?? 0, col, t.InterpolationValue ?? 1.0f, (uint)(t.Preset ?? 0)));
            }
        }

        return new AppearanceResponse(key.ToString(), female ? "female" : "male", weight, npc.Height,
            female ? race.Height.Female : race.Height.Male,
            MeshPath((female ? race.SkeletalModel?.Female : race.SkeletalModel?.Male)?.File.GivenPath ?? ""),
            faceGeomPath, faceTintPath, head, pieces, hairColor, skinTone,
            morphs, parts, tints);
    }

    public static DonorAppearanceResponse GetDonor(LoadOrderSession session, CatalogQueries queries, DonorAppearanceRequest request)
    {
        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var key)
            || !session.LinkCache.TryResolve<INpcGetter>(key, out var npc))
            throw new WorkerException(ErrorCodes.RecordNotFound, "Donor NPC not found: " + request.NpcFormKey);
        if (!session.LinkCache.TryResolve<IRaceGetter>(npc.Race.FormKey, out var race))
            throw new WorkerException(ErrorCodes.RecordNotFound, "Donor NPC race is unresolved.");

        var female = npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var head = new List<AppearanceHeadPart>();
        foreach (var link in npc.HeadParts)
        {
            if (session.LinkCache.TryResolve<IHeadPartGetter>(link.FormKey, out var part))
            {
                head.Add(new AppearanceHeadPart(
                    part.EditorID ?? "",
                    part.Type?.ToString() ?? "",
                    part.FormKey.ToString(),
                    MeshPath(part.Model?.File.GivenPath ?? "")));
            }
        }

        var face = CatalogQueries.FaceGenFor(key);
        AppearanceColor? hairColor = null;
        if (npc.HairColor.FormKeyNullable is { } colorKey
            && session.LinkCache.TryResolve<IColorRecordGetter>(colorKey, out var color))
            hairColor = new AppearanceColor(color.Color.R, color.Color.G, color.Color.B);

        var skinTone = npc.TextureLighting is { } lighting
            ? new AppearanceColor(lighting.R, lighting.G, lighting.B) : null;

        FaceMorphsDto? morphs = null;
        if (npc.FaceMorph is { } fm)
        {
            morphs = new FaceMorphsDto(
                fm.NoseLongVsShort, fm.NoseUpVsDown,
                fm.JawUpVsDown, fm.JawNarrowVsWide, fm.JawForwardVsBack,
                fm.CheeksUpVsDown, fm.CheeksForwardVsBack,
                fm.EyesUpVsDown, fm.EyesInVsOut,
                fm.BrowsUpVsDown, fm.BrowsInVsOut, fm.BrowsForwardVsBack,
                fm.LipsUpVsDown, fm.LipsInVsOut,
                fm.ChinNarrowVsWide, fm.ChinUpVsDown, fm.ChinUnderbiteVsOverbite,
                fm.EyesForwardVsBack);
        }

        FacePartsDto? parts = null;
        if (npc.FaceParts is { } fp)
        {
            parts = new FacePartsDto(fp.Nose, fp.Eyes, fp.Mouth);
        }

        var tints = new List<TintLayerDto>();
        if (npc.TintLayers is { } tl)
        {
            foreach (var t in tl)
            {
                var c = t.Color;
                var col = c.HasValue ? new AppearanceColor(c.Value.R, c.Value.G, c.Value.B) : new AppearanceColor(255, 255, 255);
                tints.Add(new TintLayerDto(t.Index ?? 0, col, t.InterpolationValue ?? 1.0f, (uint)(t.Preset ?? 0)));
            }
        }

        var winningPlugin = queries.WinningPluginOf(key);

        return new DonorAppearanceResponse(
            key.ToString(),
            npc.EditorID ?? key.ToString(),
            winningPlugin,
            female ? "female" : "male",
            npc.Weight,
            npc.Height,
            female ? race.Height.Female : race.Height.Male,
            MeshPath((female ? race.SkeletalModel?.Female : race.SkeletalModel?.Male)?.File.GivenPath ?? ""),
            face.GeometryPath,
            face.TintPath,
            head,
            hairColor,
            skinTone,
            morphs,
            parts,
            tints);
    }

    private static string MeshPath(string path) => string.IsNullOrEmpty(path) ? "" :
        path.StartsWith("meshes\\", StringComparison.OrdinalIgnoreCase) ? path : "meshes\\" + path;
}
