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

public sealed record PieceMaterialDto
{
    /// <summary>
    /// Legacy draft material request. The exporter rejects this until generated DDS assets and
    /// verified NIF shape mappings are part of the contract. Paths alone are not a game variant.
    /// </summary>
    public required string ArmorFormKey { get; init; }
    public AppearanceColor? CustomTintColor { get; init; }
    public string? DiffusePath { get; init; }
    public string? NormalPath { get; init; }
    public string? SpecularPath { get; init; }
    public string? MaterialName { get; init; }
}

public sealed record OutfitAssignment
{
    public required string NpcFormKey { get; init; }
    public required IReadOnlyList<string> ArmorFormKeys { get; init; }

    /// <summary>EditorID for the new OTFT. Generated from the NPC EditorID when omitted.</summary>
    public string? OutfitEditorId { get; init; }

    public bool AlsoSetSleepingOutfit { get; init; }

    /// <summary>Saved material requests; currently rejected with MaterialVariantPreviewOnly.</summary>
    public IReadOnlyList<PieceMaterialDto>? MaterialOverrides { get; init; }
}

public sealed record StandaloneOutfit
{
    public required string Name { get; init; }
    public required IReadOnlyList<string> ArmorFormKeys { get; init; }
    public IReadOnlyList<PieceMaterialDto>? MaterialOverrides { get; init; }
}

public sealed record ExportOutfitPatchRequest
{
    /// <summary>Plugin file name, e.g. "SkyrimNpcStudio_Lydia.esp". Also used as the mod folder name.</summary>
    public required string PluginName { get; init; }

    /// <summary>Directory that will receive the mod folder (and the ZIP). Must already exist.</summary>
    public required string OutputDirectory { get; init; }

    public required IReadOnlyList<OutfitAssignment> Assignments { get; init; }
    public IReadOnlyList<StandaloneOutfit> StandaloneOutfits { get; init; } = [];

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

/// <summary>A file of the exported mod (everything except the manifest, README and meta.ini).</summary>
public sealed record ExportedFile
{
    /// <summary>Path relative to the mod folder, with forward slashes (as inside the ZIP).</summary>
    public required string Path { get; init; }
    public required string Sha256 { get; init; }
    public required long Bytes { get; init; }
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

    /// <summary>
    /// Per NPC override, every field that differs from the winning record (re-read from the written
    /// plugin) with its original and exported value and why. Fields outside the requested operation
    /// make the export fail instead of appearing here.
    /// </summary>
    public IReadOnlyList<NpcSemanticDiff> SemanticDiff { get; init; } = [];

    /// <summary>Files created, with hash and size (what to expect in the mod folder and the ZIP).</summary>
    public IReadOnlyList<ExportedFile> Files { get; init; } = [];

    /// <summary>Checks the worker ran on this export, in plain words (including the ZIP, which the manifest cannot mention).</summary>
    public IReadOnlyList<string> Verification { get; init; } = [];

    /// <summary>What the worker cannot check: still to do by hand inside the game.</summary>
    public IReadOnlyList<string> PendingVerification { get; init; } = [];

    /// <summary>
    /// Face resolutions (FaceAssignment.CopyFaceFrom): per NPC, the override chain as it will be with the
    /// written plugin loaded last, computed from the re-read plugin. The studio feeds it to the control map.
    /// </summary>
    public IReadOnlyList<ProjectedChain> ProjectedChains { get; init; } = [];
}

/// <summary>An NPC's override chain with an exported plugin appended as its winning version.</summary>
public sealed record ProjectedChain
{
    public required string Npc { get; init; }
    public required IReadOnlyList<OverrideEntry> Overrides { get; init; }
    public required IReadOnlyList<FieldConflict> Conflicts { get; init; }
}

/// <summary>The complete face of an NPC as the export writes it (NAM9, NAMA, NAM7, QNAM, HCLF, tints).</summary>
public sealed record FaceValuesDto
{
    /// <summary>The 18 NAM9 sliders in record order.</summary>
    public required IReadOnlyList<float> Sliders { get; init; }

    /// <summary>NAMA: nose, unused, eyes, mouth (-1 = none).</summary>
    public required IReadOnlyList<int> Presets { get; init; }

    public required float Weight { get; init; }
    public required AppearanceColor SkinTone { get; init; }

    /// <summary>Null: no hair color record (HCLF cleared).</summary>
    public AppearanceColor? HairColor { get; init; }

    public required IReadOnlyList<FaceRecipeTint> Tints { get; init; }
}

/// <summary>A file the studio generated (never a copy of another mod's file), staged for the mod.</summary>
public sealed record GeneratedAsset
{
    /// <summary>Absolute path of the generated file in the studio's temporary staging folder.</summary>
    public required string SourcePath { get; init; }

    /// <summary>Data-relative destination; only the NPC's own FaceGen paths are accepted.</summary>
    public required string DataPath { get; init; }

    public required string Sha256 { get; init; }

    /// <summary>"facegen-geometry" or "facegen-tint".</summary>
    public required string Kind { get; init; }
}

/// <summary>How the FaceGen was produced, for the manifest and README.</summary>
public sealed record FaceGenProvenance
{
    /// <summary>"regenerate" (head parts + morphs, as the Creation Kit) or "preserve" (existing FaceGen + edit delta).</summary>
    public string Mode { get; init; } = "regenerate";
    public IReadOnlyList<string> Parts { get; init; } = [];

    /// <summary>Non-Bethesda sources whose geometry the FaceGen contains (personal use unless permitted).</summary>
    public IReadOnlyList<string> DerivedFrom { get; init; } = [];
    public int CustomMorphs { get; init; }
    public int SculptTargets { get; init; }

    /// <summary>
    /// MO2 mods with loose FaceGen/FaceTint files of this NPC, ascending priority. The exported mod must be
    /// installed above all of them in MO2, or the game shows their head with the new record.
    /// </summary>
    public IReadOnlyList<string> InstallAbove { get; init; } = [];

    /// <summary>
    /// Mode "source-facegen" (FaceAssignment.CopyFaceFrom): the MO2 mod whose FaceGen the game already
    /// uses for this NPC, as the studio located it. It must be the mod that provides the source plugin.
    /// </summary>
    public string? EffectiveFaceGenMod { get; init; }
}

public sealed record FaceAssignment
{
    public required string NpcFormKey { get; init; }
    public FaceValuesDto? Values { get; init; }

    /// <summary>The complete PNAM list (head part FormKeys).</summary>
    public IReadOnlyList<string>? HeadParts { get; init; }

    /// <summary>FTST face texture set FormKey; "" clears it (race default); null keeps the winner's.</summary>
    public string? HeadTexture { get; init; }

    public float? Height { get; init; }
    public IReadOnlyList<GeneratedAsset>? GeneratedAssets { get; init; }
    public FaceGenProvenance? FaceGen { get; init; }

    /// <summary>
    /// Minimal change: when set, only these NPC_ fields are written on the winning record ("HCLF",
    /// "NAM6"); every other face field must stay exactly as the winner has it, or the export fails
    /// with MinimalChangeViolated. Null applies the complete face (Values, HeadParts, HeadTexture, Height).
    /// </summary>
    public IReadOnlyList<string>? OnlyFields { get; init; }

    /// <summary>
    /// For a complete face (no OnlyFields): the NPC_ properties the user changed in the studio
    /// ("FaceMorph", "FaceParts", "Weight", "TextureLighting", "TintLayers", "HairColor", "HeadParts",
    /// "HeadTexture", "Height"). Only these are written and only these may differ from the winning
    /// record (UnauthorizedNpcChange otherwise). Null keeps the previous behavior (the whole face).
    /// </summary>
    public IReadOnlyList<string>? RequestedFields { get; init; }

    /// <summary>
    /// Assisted resolution («Usar el rostro de X»): copy the face fields listed in OnlyFields ("PNAM",
    /// "NAM9", "NAMA", "TINT", "HCLF", "FTST", "QNAM", "NAM7") from this plugin's version of the record onto
    /// the winning record, which keeps every other field. No FaceGen is generated or copied (FaceGen.Mode
    /// "source-facegen": the source mod's FaceGen already wins in MO2), and the plugin declares the
    /// source and the winning plugins as masters so it loads after both.
    /// </summary>
    public string? CopyFaceFrom { get; init; }
}

public sealed record ExportFaceOverhaulRequest
{
    public required string PluginName { get; init; }
    public required string OutputDirectory { get; init; }
    public required IReadOnlyList<FaceAssignment> Assignments { get; init; }
    public bool EslFlag { get; init; }
    public bool CreateZip { get; init; } = true;
    public string Author { get; init; } = "Skyrim NPC Studio";
    public string? Description { get; init; }
    public IReadOnlyDictionary<string, string>? PluginProviders { get; init; }
}

public sealed record CompleteNpcAssignment
{
    public required string NpcFormKey { get; init; }
    public FaceAssignment? Face { get; init; }
    public OutfitAssignment? Outfit { get; init; }
}

public sealed record ExportCompleteOverhaulRequest
{
    public required string PluginName { get; init; }
    public required string OutputDirectory { get; init; }
    public required IReadOnlyList<CompleteNpcAssignment> Assignments { get; init; }
    public bool EslFlag { get; init; }
    public bool CreateZip { get; init; } = true;
    public string Author { get; init; } = "Skyrim NPC Studio";
    public string? Description { get; init; }
    public IReadOnlyDictionary<string, string>? PluginProviders { get; init; }
}

