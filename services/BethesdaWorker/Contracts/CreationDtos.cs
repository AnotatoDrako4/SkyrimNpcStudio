namespace SkyrimNpcStudio.Worker.Contracts;

/// <summary>A creation-only draft; deliberately has no source NPC FormKey.</summary>
public sealed record NativeNpcDraft
{
    public string Name { get; init; } = "";
    public string EditorId { get; init; } = "";
    public string Description { get; init; } = "";
    public string RaceFormKey { get; init; } = "";
    public bool Female { get; init; }
    public string VoiceFormKey { get; init; } = "";
    public string ClassFormKey { get; init; } = "";
    public string CombatStyleFormKey { get; init; } = "";
    public float Height { get; init; } = 1;
    public float Weight { get; init; } = 50;
    public int Level { get; init; } = 1;
    public float? LevelMultiplier { get; init; }
    public int MinLevel { get; init; } = 1;
    public int MaxLevel { get; init; } = 100;
    public int Health { get; init; } = 100;
    public int Magicka { get; init; } = 100;
    public int Stamina { get; init; } = 100;
    public IReadOnlyDictionary<string, int> Skills { get; init; } = new Dictionary<string, int>();
    public IReadOnlyList<string> HeadPartFormKeys { get; init; } = [];
    public string HairColorFormKey { get; init; } = "";
    public string HeadTextureFormKey { get; init; } = "";
    public FaceValuesDto? Face { get; init; }
    public IReadOnlyList<string> OutfitArmorFormKeys { get; init; } = [];
    public IReadOnlyList<NativeInventoryItem> Inventory { get; init; } = [];
    public IReadOnlyList<NativeFaction> Factions { get; init; } = [];
    public IReadOnlyList<string> PackageFormKeys { get; init; } = [];
    public IReadOnlyList<string> SpellFormKeys { get; init; } = [];
    public IReadOnlyList<string> PerkFormKeys { get; init; } = [];
    public NativePlacement? Placement { get; init; }
}

public sealed record NativeInventoryItem(string FormKey, int Count);
public sealed record NativeFaction(string FormKey, int Rank);
public sealed record NativePlacement
{
    public string CellFormKey { get; init; } = "";
    public float X { get; init; }
    public float Y { get; init; }
    public float Z { get; init; }
    public float RotationZ { get; init; }
}

public sealed record CreationValidateRequest
{
    public required NativeNpcDraft Npc { get; init; }
    public string PluginName { get; init; } = "CharacterForge.esp";
}
public sealed record CreationValidateResponse
{
    public required bool Exportable { get; init; }
    public required bool TechnicallyExportable { get; init; }
    public string FaceGenStatus { get; init; } = "pending";
    public required IReadOnlyList<Issue> Issues { get; init; }
    public required IReadOnlyList<string> MissingReferences { get; init; }
    public required IReadOnlyList<string> Masters { get; init; }
    public OutfitValidateResponse? Outfit { get; init; }
    public IReadOnlyList<string> Limitations { get; init; } = ["Native FaceGen geometry and tint generation are not verified; final export is blocked.", "Asset availability and behavior must be verified in Windows/MO2 and Skyrim."];
}
public sealed record CreationExportRequest
{
    public required NativeNpcDraft Npc { get; init; }
    public required string PluginName { get; init; }
    public required string OutputDirectory { get; init; }
    /// <summary>Explicit opt-in to a structural test artifact, never a ready-to-play mod.</summary>
    public bool TechnicalOnly { get; init; }
    public bool EslFlag { get; init; }
    public bool CreateZip { get; init; } = true;
    public string Author { get; init; } = "Skyrim NPC Studio";
    public IReadOnlyDictionary<string, string>? PluginProviders { get; init; }
}
public sealed record CreationExportResponse
{
    public required ExportResponse Package { get; init; }
    public required CreationValidateResponse Validation { get; init; }
    public required bool ReadBackVerified { get; init; }
    public bool TechnicalOnly { get; init; } = true;
}
public sealed record CreationSearchRequest
{
    public string Kind { get; init; } = "race";
    public string Text { get; init; } = "";
    public int Limit { get; init; } = 100;
}
public sealed record CreationRecord(string FormKey, string EditorId, string Kind);
