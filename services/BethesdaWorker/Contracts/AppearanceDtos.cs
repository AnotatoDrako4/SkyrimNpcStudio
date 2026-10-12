namespace SkyrimNpcStudio.Worker.Contracts;

public sealed record DonorAppearanceOverride(
    string? DonorNpcFormKey,
    string? DonorFaceGeometryPath,
    string? DonorFaceTintPath,
    IReadOnlyList<AppearanceHeadPart>? HeadParts,
    AppearanceColor? HairColor,
    AppearanceColor? SkinTone,
    float? Weight);

public sealed record AppearanceRequest
{
    public required string NpcFormKey { get; init; }
    // Null requests the record's default outfit; an empty list requests skin only.
    public IReadOnlyList<string>? ArmorFormKeys { get; init; }
    public DonorAppearanceOverride? DonorOverride { get; init; }
}

public sealed record AppearanceAddon(string FormKey, string MeshPath, bool WeightSlider, uint SlotMask);
public sealed record AppearancePiece(string ArmorFormKey, string Name, string Source, uint SlotMask,
    IReadOnlyList<AppearanceAddon> Addons);
public sealed record AppearanceHeadPart(string EditorId, string Type, string? FormKey = null, string? MeshPath = null);
public sealed record AppearanceColor(byte R, byte G, byte B);

public sealed record FaceMorphsDto(
    float NoseLongVsShort, float NoseUpVsDown,
    float JawUpVsDown, float JawNarrowVsWide, float JawForwardVsBack,
    float CheeksUpVsDown, float CheeksForwardVsBack,
    float EyesUpVsDown, float EyesInVsOut,
    float BrowsUpVsDown, float BrowsInVsOut, float BrowsForwardVsBack,
    float LipsUpVsDown, float LipsInVsOut,
    float ChinNarrowVsWide, float ChinUpVsDown, float ChinUnderbiteVsOverbite,
    float EyesForwardVsBack);

public sealed record FacePartsDto(uint Nose, uint Eyes, uint Mouth);

public sealed record TintLayerDto(int Index, AppearanceColor Color, float Alpha, uint Preset);

public sealed record AppearanceResponse(string NpcFormKey, string Sex, float Weight, float Height, float RaceHeight,
    string SkeletonPath, string FaceGeometryPath, string FaceTintPath,
    IReadOnlyList<AppearanceHeadPart> HeadParts, IReadOnlyList<AppearancePiece> Pieces,
    AppearanceColor? HairColor = null, AppearanceColor? SkinTone = null,
    FaceMorphsDto? FaceMorphs = null, FacePartsDto? FaceParts = null,
    IReadOnlyList<TintLayerDto>? TintLayers = null);

public sealed record DonorAppearanceRequest
{
    public required string NpcFormKey { get; init; }
}

public sealed record DonorAppearanceResponse(
    string NpcFormKey, string EditorId, string WinningPlugin, string Sex,
    float Weight, float Height, float RaceHeight,
    string SkeletonPath, string FaceGeometryPath, string FaceTintPath,
    IReadOnlyList<AppearanceHeadPart> HeadParts,
    AppearanceColor? HairColor, AppearanceColor? SkinTone,
    FaceMorphsDto? FaceMorphs, FacePartsDto? FaceParts,
    IReadOnlyList<TintLayerDto> TintLayers);
