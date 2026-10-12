namespace SkyrimNpcStudio.Worker.Contracts;

public sealed record AppearanceRequest
{
    public required string NpcFormKey { get; init; }
    // Null requests the record's default outfit; an empty list requests skin only.
    public IReadOnlyList<string>? ArmorFormKeys { get; init; }
}

public sealed record AppearanceAddon(string FormKey, string MeshPath, bool WeightSlider, uint SlotMask);
public sealed record AppearancePiece(string ArmorFormKey, string Name, string Source, uint SlotMask,
    IReadOnlyList<AppearanceAddon> Addons);
public sealed record AppearanceHeadPart(string EditorId, string Type);
public sealed record AppearanceColor(byte R, byte G, byte B);
public sealed record AppearanceResponse(string NpcFormKey, string Sex, float Weight, float Height, float RaceHeight,
    string SkeletonPath, string FaceGeometryPath, string FaceTintPath,
    IReadOnlyList<AppearanceHeadPart> HeadParts, IReadOnlyList<AppearancePiece> Pieces,
    AppearanceColor? HairColor = null, AppearanceColor? SkinTone = null);
