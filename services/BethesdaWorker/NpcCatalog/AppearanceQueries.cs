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
            if (session.LinkCache.TryResolve<IHeadPartGetter>(link.FormKey, out var part))
                head.Add(new AppearanceHeadPart(part.EditorID ?? "", part.Type.ToString() ?? ""));
        var face = CatalogQueries.FaceGenFor(key);
        AppearanceColor? hairColor = null;
        if (npc.HairColor.FormKeyNullable is { } colorKey
            && session.LinkCache.TryResolve<IColorRecordGetter>(colorKey, out var color))
            hairColor = new AppearanceColor(color.Color.R, color.Color.G, color.Color.B);
        var skinTone = npc.TextureLighting is { } lighting
            ? new AppearanceColor(lighting.R, lighting.G, lighting.B) : null;
        return new AppearanceResponse(key.ToString(), female ? "female" : "male", npc.Weight, npc.Height,
            female ? race.Height.Female : race.Height.Male,
            MeshPath((female ? race.SkeletalModel?.Female : race.SkeletalModel?.Male)?.File.GivenPath ?? ""),
            face.GeometryPath, face.TintPath, head, pieces, hairColor, skinTone);
    }

    private static string MeshPath(string path) => string.IsNullOrEmpty(path) ? "" :
        path.StartsWith("meshes\\", StringComparison.OrdinalIgnoreCase) ? path : "meshes\\" + path;
}
