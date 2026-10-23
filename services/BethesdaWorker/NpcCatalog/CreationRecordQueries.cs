using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

internal static class CreationRecordQueries
{
    public static IReadOnlyList<CreationRecord> Search(LoadOrderSession session, CreationSearchRequest request)
    {
        var type = request.Kind switch
        {
            "race" => typeof(IRaceGetter), "voice" => typeof(IVoiceTypeGetter), "class" => typeof(IClassGetter),
            "combatStyle" => typeof(ICombatStyleGetter), "headPart" => typeof(IHeadPartGetter), "armor" => typeof(IArmorGetter),
            "item" => typeof(IItemGetter), "faction" => typeof(IFactionGetter), "package" => typeof(IPackageGetter),
            "cell" => typeof(ICellGetter), "hairColor" => typeof(IColorRecordGetter), "headTexture" => typeof(ITextureSetGetter),
            // The same types NativeNpcValidator resolves for an NPC's spells (SPEL) and perks (PERK): what the picker offers is what validates.
            "spell" => typeof(ISpellGetter), "perk" => typeof(IPerkGetter),
            _ => throw new WorkerException(ErrorCodes.InvalidArgument, "Unsupported creation record kind."),
        };
        if (request.Text.Length > 160) throw new WorkerException(ErrorCodes.InvalidArgument, "Search text too long.");
        return session.Plugins.Reverse().SelectMany(p => p.Mod.EnumerateMajorRecords()).Where(r => type.IsInstanceOfType(r))
            .DistinctBy(r => r.FormKey).Where(r => !r.IsDeleted && (r.EditorID ?? "").Contains(request.Text, StringComparison.OrdinalIgnoreCase))
            .OrderBy(r => r.EditorID, StringComparer.OrdinalIgnoreCase).Take(Math.Clamp(request.Limit, 1, 200))
            .Select(r => new CreationRecord(r.FormKey.ToString(), r.EditorID ?? r.FormKey.ToString(), request.Kind)).ToArray();
    }
}
