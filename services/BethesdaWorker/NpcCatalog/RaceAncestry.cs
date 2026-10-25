using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>
/// Traverses Skyrim race inheritance chains (ArmorRace and MorphRace)
/// to determine all compatible races for armor addons (ARMA) and skin addons.
/// </summary>
internal static class RaceAncestry
{
    /// <summary>
    /// Collects the race itself and all ancestor armor/morph races transitively.
    /// E.g. NordRaceVampire -> NordRace -> DefaultRace.
    /// Cycle-safe using HashSet.
    /// </summary>
    public static HashSet<FormKey> CollectRaces(LoadOrderSession session, FormKey raceKey)
    {
        var races = new HashSet<FormKey>();
        var queue = new Queue<FormKey>();
        if (races.Add(raceKey)) queue.Enqueue(raceKey);

        while (queue.TryDequeue(out var currentKey))
        {
            if (!session.LinkCache.TryResolve<IRaceGetter>(currentKey, out var currentRace)) continue;
            if (currentRace.ArmorRace.FormKeyNullable is { } armorRace && races.Add(armorRace))
            {
                queue.Enqueue(armorRace);
            }
            if (currentRace.MorphRace.FormKeyNullable is { } morphRace && races.Add(morphRace))
            {
                queue.Enqueue(morphRace);
            }
        }

        return races;
    }
}
