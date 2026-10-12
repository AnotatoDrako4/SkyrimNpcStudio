namespace SkyrimNpcStudio.Worker.Contracts;

/// <summary>
/// Everything needed to rebuild an NPC's FaceGen exactly as the Creation Kit would: the effective
/// head parts with their meshes and TRI morph files, texture sets, race data and the record's face
/// values. The studio regenerates the FaceGen NIF/DDS from it (engines/face) for both the preview
/// and the export. Field names mirror engines/face/src/face_recipe.cpp (recipe_from_json).
/// </summary>
public sealed record FaceRecipeRequest
{
    public required string NpcFormKey { get; init; }

    /// <summary>Read this plugin's version of the record instead of the winner (e.g. "Skyrim.esm").</summary>
    public string? SourcePlugin { get; init; }

    /// <summary>Replacement PNAM list (head part FormKeys) for a draft; null keeps the record's list.</summary>
    public IReadOnlyList<string>? HeadPartFormKeys { get; init; }

    /// <summary>
    /// Resolve races, head parts and texture sets from the five base-game masters only (what
    /// vanilla FaceGen was built from), ignoring mod and Creation Club overrides of those records.
    /// </summary>
    public bool OfficialOnly { get; init; }

    /// <summary>Replacement face texture set (FTST): a TXST FormKey, "" for none (race default), null keeps the record's.</summary>
    public string? HeadTextureFormKey { get; init; }
}

public sealed record FaceRecipeRace(string FormKey, string EditorId, string MorphRaceEditorId, bool Vampire);

public sealed record FaceRecipeTri(int Kind, string Path);

public sealed record FaceRecipeHeadPart
{
    public required string FormKey { get; init; }
    public required string EditorId { get; init; }
    public required string Type { get; init; }

    /// <summary>Data-relative mesh ("meshes\..."); empty when the part has no geometry.</summary>
    public required string Model { get; init; }

    public required IReadOnlyList<FaceRecipeTri> Tris { get; init; }

    /// <summary>HDPT texture set mapped to NIF texture slots 0..8 (null: keep the mesh's own).</summary>
    public IReadOnlyList<string>? TextureSet { get; init; }

    public bool Extra { get; init; }
    public required string Plugin { get; init; }

    /// <summary>MO2 / plugin facts used for redistribution checks: true for Bethesda masters and CC.</summary>
    public bool Official { get; init; }

    /// <summary>"male", "female" or "any" from the HDPT flags.</summary>
    public string ValidSex { get; init; } = "any";

    /// <summary>False when the HDPT has a valid-races list that does not contain the NPC's race.</summary>
    public bool ValidForRace { get; init; } = true;
}

public sealed record FaceRecipeTintMask(int Index, string MaskPath, int MaskType);

public sealed record FaceRecipeTint(int Index, AppearanceColor Color, float Value, int Preset);

public sealed record FaceRecipeValues
{
    public required IReadOnlyList<float> Sliders { get; init; }
    public required IReadOnlyList<int> Presets { get; init; }
    public required float Weight { get; init; }
    public required AppearanceColor SkinTone { get; init; }
    public AppearanceColor? HairColor { get; init; }
    public string? HairColorFormKey { get; init; }
    public required IReadOnlyList<FaceRecipeTint> Tints { get; init; }
}

public sealed record FaceRecipeResponse
{
    public required string NpcFormKey { get; init; }
    public required string SourcePlugin { get; init; }
    public required string FaceGenMeshPath { get; init; }
    public required string FaceGenTintPath { get; init; }
    public required bool Female { get; init; }

    /// <summary>NPC height multiplier (NAM6); not part of FaceGen, edited with the body.</summary>
    public float Height { get; init; } = 1.0f;
    public required FaceRecipeRace Race { get; init; }
    public required IReadOnlyList<FaceRecipeHeadPart> HeadParts { get; init; }
    public required IReadOnlyList<string> NpcHeadParts { get; init; }
    public IReadOnlyList<string>? FaceTextureSet { get; init; }
    public string? FaceTextureSetFormKey { get; init; }

    /// <summary>The record's own FTST ("" when it uses the race default).</summary>
    public string NpcHeadTexture { get; init; } = "";
    public required IReadOnlyList<FaceRecipeTintMask> TintMasks { get; init; }
    public required FaceRecipeValues Values { get; init; }
    public required IReadOnlyList<Issue> Issues { get; init; }
}
