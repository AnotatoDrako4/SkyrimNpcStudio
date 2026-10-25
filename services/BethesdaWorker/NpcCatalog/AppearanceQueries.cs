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
        var traitsNpc = npc;
        if (npc.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits)
            && npc.Template.FormKeyNullable is not null)
        {
            var visited = new HashSet<FormKey> { npc.FormKey };
            var current = npc;
            while (current.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits)
                   && current.Template.FormKeyNullable is { } next)
            {
                if (!visited.Add(next)) break;
                if (session.LinkCache.TryResolve<INpcGetter>(next, out var tmpl))
                {
                    current = tmpl;
                }
                else
                {
                    break;
                }
            }
            traitsNpc = current;
        }

        var effectiveRaceKey = traitsNpc.Race.FormKeyNullable ?? npc.Race.FormKeyNullable;
        if (effectiveRaceKey is null || !session.LinkCache.TryResolve<IRaceGetter>(effectiveRaceKey.Value, out var race))
            throw new WorkerException(ErrorCodes.RecordNotFound, "NPC race is unresolved.");

        var female = traitsNpc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var races = RaceAncestry.CollectRaces(session, race.FormKey);
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

        var ownSkin = traitsNpc.WornArmor.FormKeyNullable ?? npc.WornArmor.FormKeyNullable;
        if ((ownSkin ?? race.Skin.FormKeyNullable) is { } skin) AddArmor(skin, "skin");
        if (request.ArmorFormKeys is { } requested)
        {
            foreach (var armor in requested.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                if (!queries.TryParseAnyFormKey(armor, out var armorKey))
                    throw new WorkerException(ErrorCodes.InvalidArgument, "Invalid armor FormKey: " + armor);
                AddArmor(armorKey, "outfit");
            }
        }
        else if ((npc.DefaultOutfit.FormKeyNullable ?? traitsNpc.DefaultOutfit.FormKeyNullable) is { } outfitKey
            && session.LinkCache.TryResolve<IOutfitGetter>(outfitKey, out var outfit))
        {
            foreach (var item in outfit.Items ?? [])
                if (session.LinkCache.TryResolve<IArmorGetter>(item.FormKey, out _)) AddArmor(item.FormKey, "outfit");
        }

        var head = new List<AppearanceHeadPart>();
        var headPartsSource = traitsNpc.HeadParts.Count > 0 ? traitsNpc.HeadParts : npc.HeadParts;
        foreach (var link in headPartsSource)
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

        var face = CatalogQueries.FaceGenFor(traitsNpc.FormKey);
        var faceGeomPath = face.GeometryPath;
        var faceTintPath = face.TintPath;

        AppearanceColor? hairColor = null;
        var hairColorKey = traitsNpc.HairColor.FormKeyNullable ?? npc.HairColor.FormKeyNullable;
        if (hairColorKey is { } colorKey
            && session.LinkCache.TryResolve<IColorRecordGetter>(colorKey, out var color))
            hairColor = new AppearanceColor(color.Color.R, color.Color.G, color.Color.B);

        var skinTone = (traitsNpc.TextureLighting ?? npc.TextureLighting) is { } lighting
            ? new AppearanceColor(lighting.R, lighting.G, lighting.B) : null;

        var weight = traitsNpc.Weight != 0 ? traitsNpc.Weight : npc.Weight;
        var height = traitsNpc.Height != 0 ? traitsNpc.Height : npc.Height;

        FaceMorphsDto? morphs = null;
        if ((traitsNpc.FaceMorph ?? npc.FaceMorph) is { } fm)
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
        if ((traitsNpc.FaceParts ?? npc.FaceParts) is { } fp)
        {
            parts = new FacePartsDto(fp.Nose, fp.Eyes, fp.Mouth);
        }

        var tints = new List<TintLayerDto>();
        var tintSource = traitsNpc.TintLayers ?? npc.TintLayers;
        if (tintSource is { } tl)
        {
            foreach (var t in tl)
            {
                var c = t.Color;
                var col = c.HasValue ? new AppearanceColor(c.Value.R, c.Value.G, c.Value.B) : new AppearanceColor(255, 255, 255);
                tints.Add(new TintLayerDto(t.Index ?? 0, col, t.InterpolationValue ?? 1.0f, (uint)(t.Preset ?? 0)));
            }
        }

        return new AppearanceResponse(key.ToString(), female ? "female" : "male", weight, height,
            female ? race.Height.Female : race.Height.Male,
            MeshPath((female ? race.SkeletalModel?.Female : race.SkeletalModel?.Male)?.File.GivenPath ?? ""),
            faceGeomPath, faceTintPath, head, pieces, hairColor, skinTone,
            morphs, parts, tints);
    }

    private static string MeshPath(string path) => string.IsNullOrEmpty(path) ? "" :
        path.StartsWith("meshes\\", StringComparison.OrdinalIgnoreCase) ? path : "meshes\\" + path;
}
