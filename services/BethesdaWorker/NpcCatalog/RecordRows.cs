namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>Index rows extracted from ONE plugin. A FormKey may appear in many plugins (overrides).</summary>
internal sealed record GenericRow(string FormKey, string RecordType, string? EditorId, string? Name, string? DataJson);

internal sealed record NpcRow(
    string FormKey,
    string? EditorId,
    string? Name,
    string? Race,
    bool Female,
    bool Unique,
    bool Preset,
    string? Template,
    int TemplateFlags,
    string? DefaultOutfit,
    string? SleepingOutfit,
    IReadOnlyList<(string Faction, int Rank)> Factions,
    NpcData Data,
    IReadOnlyDictionary<string, string> Fingerprints);

/// <summary>Detail fields of an NPC version, serialized as JSON in the index.</summary>
internal sealed record NpcData
{
    public string? Class { get; init; }
    public string? Voice { get; init; }
    public string? WornArmor { get; init; }
    public string? HairColor { get; init; }
    public string? HeadTexture { get; init; }
    public float Height { get; init; }
    public float Weight { get; init; }
    public int Flags { get; init; }
    public IReadOnlyList<string> HeadParts { get; init; } = [];
    public IReadOnlyList<ItemCount> Items { get; init; } = [];
}

internal sealed record ItemCount(string Item, int Count);

internal sealed record ArmorRow(
    string FormKey,
    string? EditorId,
    string? Name,
    uint SlotMask,
    string ArmorType,
    bool NonPlayable,
    string? MaleModel,
    string? FemaleModel,
    IReadOnlyList<string> Addons);

internal sealed record PlacedNpcRow(
    string FormKey,
    string NpcFormKey,
    string? EditorId,
    string? CellFormKey,
    string? CellName,
    string? CellEditorId);

internal sealed record PluginExtract(
    IReadOnlyList<GenericRow> Records,
    IReadOnlyList<NpcRow> Npcs,
    IReadOnlyList<ArmorRow> Armors,
    int OutfitCount,
    IReadOnlyList<PlacedNpcRow> Locations,
    IReadOnlyList<Contracts.Issue> Issues);
