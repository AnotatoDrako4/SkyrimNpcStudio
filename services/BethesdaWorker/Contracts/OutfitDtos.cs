namespace SkyrimNpcStudio.Worker.Contracts;

public sealed record OutfitValidateRequest
{
    public required string NpcFormKey { get; init; }
    public required IReadOnlyList<string> ArmorFormKeys { get; init; }
}

public sealed record OutfitPiece
{
    public required ArmorSummary Armor { get; init; }

    /// <summary>ARMA records whose race list covers the NPC's race (or its armor race).</summary>
    public required int AddonsForRace { get; init; }

    /// <summary>Data-relative meshes used for the NPC's sex by the matching addons.</summary>
    public required IReadOnlyList<string> Meshes { get; init; }
}

public sealed record OutfitValidateResponse
{
    /// <summary>False when at least one issue has severity "error".</summary>
    public required bool Exportable { get; init; }

    public required uint CombinedSlotMask { get; init; }
    public required IReadOnlyList<OutfitPiece> Pieces { get; init; }
    public required IReadOnlyList<Issue> Issues { get; init; }
}

public sealed record OutfitAssignment
{
    public required string NpcFormKey { get; init; }
    public required IReadOnlyList<string> ArmorFormKeys { get; init; }

    /// <summary>EditorID for the new OTFT. Generated from the NPC EditorID when omitted.</summary>
    public string? OutfitEditorId { get; init; }

    public bool AlsoSetSleepingOutfit { get; init; }
}

public sealed record ExportOutfitPatchRequest
{
    /// <summary>Plugin file name, e.g. "SkyrimNpcStudio_Lydia.esp". Also used as the mod folder name.</summary>
    public required string PluginName { get; init; }

    /// <summary>Directory that will receive the mod folder (and the ZIP). Must already exist.</summary>
    public required string OutputDirectory { get; init; }

    public required IReadOnlyList<OutfitAssignment> Assignments { get; init; }

    public bool EslFlag { get; init; }
    public bool CreateZip { get; init; } = true;
    public string Author { get; init; } = "Skyrim NPC Studio";
    public string? Description { get; init; }

    /// <summary>Optional plugin → MO2 mod name map, used to document dependencies.</summary>
    public IReadOnlyDictionary<string, string>? PluginProviders { get; init; }
}

public sealed record ExportedRecord
{
    public required string FormKey { get; init; }
    public required string RecordType { get; init; }
    public string? EditorId { get; init; }

    /// <summary>"new" or "override".</summary>
    public required string Kind { get; init; }
}

public sealed record ExportResponse
{
    public required string ModDirectory { get; init; }
    public required string PluginPath { get; init; }
    public string? ZipPath { get; init; }
    public required string ManifestPath { get; init; }
    public required IReadOnlyList<string> Masters { get; init; }
    public required bool EslFlagged { get; init; }
    public required string PluginSha256 { get; init; }
    public required IReadOnlyList<ExportedRecord> Records { get; init; }
    public required IReadOnlyList<Issue> Issues { get; init; }
}
