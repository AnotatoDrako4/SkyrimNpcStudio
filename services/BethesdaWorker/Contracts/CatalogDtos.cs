namespace SkyrimNpcStudio.Worker.Contracts;

/// <summary>
/// One plugin of the effective load order, already resolved by the studio's MO2 engine.
/// The worker never interprets MO2 itself: it receives absolute paths of the winning files.
/// </summary>
public sealed record LoadOrderEntry
{
    public required string FileName { get; init; }

    /// <summary>Absolute path of the winning file, or null when the studio could not find it.</summary>
    public string? Path { get; init; }

    public bool Enabled { get; init; } = true;
}

public sealed record IndexRequest
{
    /// <summary>Stable identifier of the MO2 instance + profile; selects the cache database.</summary>
    public required string ProfileKey { get; init; }

    public required string CacheDirectory { get; init; }

    public required IReadOnlyList<LoadOrderEntry> Plugins { get; init; }

    /// <summary>Game language from Skyrim.ini sLanguage (e.g. "SPANISH"); selects localized .STRINGS. Default English.</summary>
    public string? Language { get; init; }
}

public sealed record IndexResponse
{
    public required int PluginCount { get; init; }
    public required int ParsedPlugins { get; init; }
    public required int CachedPlugins { get; init; }
    public required int NpcCount { get; init; }
    public required int ArmorCount { get; init; }
    public required int OutfitCount { get; init; }
    public required long ElapsedMs { get; init; }
    public required IReadOnlyList<Issue> Issues { get; init; }
}

public sealed record IndexProgress
{
    public required string Stage { get; init; }
    public required int Current { get; init; }
    public required int Total { get; init; }
    public string? Plugin { get; init; }
}

public sealed record CatalogStatusResponse
{
    public required bool Indexed { get; init; }
    public required bool Busy { get; init; }
    public string? ProfileKey { get; init; }
    public int PluginCount { get; init; }
}

public sealed record PluginInfo
{
    public required string FileName { get; init; }
    public required int LoadIndex { get; init; }

    /// <summary>xEdit-style prefix: "00".."FD" for full plugins, "FE:000".."FE:FFF" for light plugins.</summary>
    public required string FormIdPrefix { get; init; }

    public required bool IsLight { get; init; }
    public required bool IsMaster { get; init; }
    public required IReadOnlyList<string> Masters { get; init; }
    public required int NpcRecords { get; init; }
    public required int ArmorRecords { get; init; }
    public required int OutfitRecords { get; init; }
    public string? Path { get; init; }
}

public sealed record ListPluginsResponse
{
    public required IReadOnlyList<PluginInfo> Plugins { get; init; }
}

public sealed record NpcSearchRequest
{
    /// <summary>Matches name, EditorID, FormKey ("0A2C8E:Skyrim.esm") or xEdit FormID ("000A2C8E").</summary>
    public string? Text { get; init; }

    /// <summary>"male" or "female".</summary>
    public string? Sex { get; init; }

    /// <summary>Race FormKey or EditorID.</summary>
    public string? Race { get; init; }

    /// <summary>Plugin that defines or overrides the NPC.</summary>
    public string? Plugin { get; init; }

    /// <summary>Faction FormKey or EditorID.</summary>
    public string? Faction { get; init; }

    /// <summary>"unique", "generic", "templated" or "preset".</summary>
    public string? Kind { get; init; }

    public int Offset { get; init; }
    public int Limit { get; init; } = 200;
}

public sealed record NpcSummary
{
    public required string FormKey { get; init; }
    public required string FormId { get; init; }
    public string? EditorId { get; init; }
    public string? Name { get; init; }
    public string? RaceFormKey { get; init; }
    public string? RaceEditorId { get; init; }
    public required string Sex { get; init; }
    public required string OriginPlugin { get; init; }
    public required string WinningPlugin { get; init; }
    public required int OverrideCount { get; init; }
    public required bool IsUnique { get; init; }
    public required bool UsesTemplate { get; init; }
}

public sealed record NpcSearchResponse
{
    public required int Total { get; init; }
    public required IReadOnlyList<NpcSummary> Items { get; init; }
}

public sealed record NpcDetailsRequest
{
    public required string FormKey { get; init; }
}

public sealed record RecordRef
{
    public required string FormKey { get; init; }
    public required string RecordType { get; init; }
    public string? EditorId { get; init; }
    public string? Name { get; init; }
    public string? WinningPlugin { get; init; }

    /// <summary>False when the referenced record is not present in the active load order.</summary>
    public bool Resolved { get; init; } = true;
}

public sealed record HeadPartRef
{
    public required RecordRef Record { get; init; }
    public string? PartType { get; init; }
    public string? ModelPath { get; init; }
}

public sealed record FactionRank
{
    public required RecordRef Faction { get; init; }
    public required int Rank { get; init; }
}

public sealed record InventoryItem
{
    public required RecordRef Item { get; init; }
    public required int Count { get; init; }
}

public sealed record OverrideEntry
{
    public required string Plugin { get; init; }
    public required int LoadIndex { get; init; }
    public required bool IsOrigin { get; init; }
    public required bool IsWinner { get; init; }

    /// <summary>Tracked fields whose value differs from the previous entry of the chain.</summary>
    public required IReadOnlyList<string> ChangedFields { get; init; }
}

/// <summary>
/// A tracked field changed by a non-winning override whose value did not survive into the
/// winning record: the classic "lost edit" that needs a conflict-resolution patch.
/// </summary>
public sealed record FieldConflict
{
    public required string Field { get; init; }
    public required string WinningPlugin { get; init; }
    public required IReadOnlyList<string> LosingPlugins { get; init; }
}

public sealed record FaceGenPaths
{
    /// <summary>Data-relative path, e.g. meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif</summary>
    public required string GeometryPath { get; init; }

    public required string TintPath { get; init; }
}

public sealed record OutfitContents
{
    public required RecordRef Outfit { get; init; }
    public required IReadOnlyList<ArmorSummary> Armors { get; init; }

    /// <summary>Entries that are not ARMO (e.g. leveled items); listed but not expanded.</summary>
    public required IReadOnlyList<RecordRef> OtherEntries { get; init; }
}

public sealed record NpcDetails
{
    public required NpcSummary Summary { get; init; }
    public RecordRef? Race { get; init; }
    public RecordRef? Class { get; init; }
    public RecordRef? Voice { get; init; }
    public RecordRef? Template { get; init; }
    public RecordRef? WornArmor { get; init; }
    public RecordRef? HairColor { get; init; }
    public RecordRef? HeadTexture { get; init; }
    public OutfitContents? DefaultOutfit { get; init; }
    public OutfitContents? SleepingOutfit { get; init; }
    public required IReadOnlyList<string> Flags { get; init; }
    public required IReadOnlyList<string> TemplateFlags { get; init; }
    public required float Height { get; init; }
    public required float Weight { get; init; }
    public required IReadOnlyList<HeadPartRef> HeadParts { get; init; }
    public required IReadOnlyList<FactionRank> Factions { get; init; }
    public required IReadOnlyList<InventoryItem> Inventory { get; init; }
    public required IReadOnlyList<OverrideEntry> Overrides { get; init; }
    public required IReadOnlyList<FieldConflict> Conflicts { get; init; }
    public required FaceGenPaths FaceGen { get; init; }
    public required IReadOnlyList<NpcLocation> Locations { get; init; }
    public required IReadOnlyList<Issue> Notes { get; init; }
}

public sealed record NpcLocation
{
    public required string FormKey { get; init; }
    public string? EditorId { get; init; }
    public string? CellFormKey { get; init; }
    public string? CellName { get; init; }
    public string? CellEditorId { get; init; }
    public required string Plugin { get; init; }
}

public sealed record ArmorSearchRequest
{
    public string? Text { get; init; }
    public string? Plugin { get; init; }

    /// <summary>Only armors that occupy at least one of these biped slots (bit N-30 = slot N).</summary>
    public uint? SlotMask { get; init; }

    public bool IncludeNonPlayable { get; init; } = true;
    public int Offset { get; init; }
    public int Limit { get; init; } = 200;
}

public sealed record ArmorSummary
{
    public required string FormKey { get; init; }
    public required string FormId { get; init; }
    public string? EditorId { get; init; }
    public string? Name { get; init; }
    public required string OriginPlugin { get; init; }
    public required string WinningPlugin { get; init; }
    public required uint SlotMask { get; init; }
    public required IReadOnlyList<int> Slots { get; init; }
    public required string ArmorType { get; init; }
    public required bool NonPlayable { get; init; }
    public string? MaleModel { get; init; }
    public string? FemaleModel { get; init; }
    public required int AddonCount { get; init; }
}

public sealed record ArmorSearchResponse
{
    public required int Total { get; init; }
    public required IReadOnlyList<ArmorSummary> Items { get; init; }
}

public sealed record HeadPartSummary
{
    public required string FormKey { get; init; }
    public string? EditorId { get; init; }
    public string? Name { get; init; }
    public required string Type { get; init; }
    public string? ModelPath { get; init; }
    public required string Plugin { get; init; }
}

public sealed record SearchHeadPartsRequest
{
    public string? Type { get; init; }
    public string? Text { get; init; }
    public string? Plugin { get; init; }
    public int Limit { get; init; } = 200;
}

public sealed record SearchHeadPartsResponse
{
    public required int Total { get; init; }
    public required IReadOnlyList<HeadPartSummary> Items { get; init; }
}
