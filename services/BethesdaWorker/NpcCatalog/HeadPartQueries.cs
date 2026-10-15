using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>
/// Head parts an NPC can use, from the winning HDPT records of the active load order: the NPC's race
/// (or its morph race) must be in the part's valid-races list and its sex must be allowed, the same
/// rule FaceRecipeQueries reports as ValidForRace/ValidSex. Extra parts are omitted: they come with
/// the part that lists them.
/// </summary>
internal static class HeadPartQueries
{
    public static SearchHeadPartsResponse ForNpc(LoadOrderSession session, CatalogQueries queries, SearchHeadPartsRequest request)
    {
        if (!queries.TryParseAnyFormKey(request.NpcFormKey ?? "", out var npcKey)
            || !session.LinkCache.TryResolve<INpcGetter>(npcKey, out var npc))
        {
            throw new WorkerException(ErrorCodes.RecordNotFound, $"NPC '{request.NpcFormKey}' is not in the active load order.");
        }

        if (!session.LinkCache.TryResolve<IRaceGetter>(npc.Race.FormKey, out var race))
        {
            return new SearchHeadPartsResponse { Items = [], Total = 0 };
        }

        var female = npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var text = request.Text?.Trim();
        var matches = new List<HeadPartSummary>();
        foreach (var part in session.LinkCache.PriorityOrder.WinningOverrides<IHeadPartGetter>())
        {
            if (part.Flags.HasFlag(HeadPart.Flag.IsExtraPart)) continue;
            var type = part.Type?.ToString() ?? "Misc";
            if (!string.IsNullOrWhiteSpace(request.Type) && !string.Equals(type, request.Type, StringComparison.OrdinalIgnoreCase)) continue;
            var male = part.Flags.HasFlag(HeadPart.Flag.Male);
            var femaleFlag = part.Flags.HasFlag(HeadPart.Flag.Female);
            if (male != femaleFlag && (female ? !femaleFlag : !male)) continue;
            if (part.ValidRaces.FormKeyNullable is { } listKey && session.LinkCache.TryResolve<IFormListGetter>(listKey, out var list)
                && !list.Items.Any(i => i.FormKey == race.FormKey || i.FormKey == race.MorphRace.FormKeyNullable))
            {
                continue;
            }

            if (!string.IsNullOrEmpty(text)
                && (part.EditorID?.Contains(text, StringComparison.OrdinalIgnoreCase) ?? false) == false
                && (part.Name?.String?.Contains(text, StringComparison.OrdinalIgnoreCase) ?? false) == false)
            {
                continue;
            }

            matches.Add(new HeadPartSummary
            {
                FormKey = part.FormKey.ToString(),
                EditorId = part.EditorID,
                Name = part.Name?.String,
                Type = type,
                ModelPath = part.Model?.File.GivenPath,
                Plugin = part.FormKey.ModKey.FileName,
            });
        }

        var items = matches.OrderBy(m => m.EditorId ?? m.FormKey, StringComparer.OrdinalIgnoreCase)
            .Take(Math.Clamp(request.Limit, 1, 1000)).ToArray();
        return new SearchHeadPartsResponse { Items = items, Total = matches.Count };
    }
}
