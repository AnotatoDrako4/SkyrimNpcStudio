namespace SkyrimNpcStudio.Worker.Contracts;

public sealed record IntegrityRecordRequest(string NpcFormKey);
public sealed record IntegrityRecordResponse(string FormKey, string DefiningPlugin, string Winner,
    IReadOnlyList<string> Overrides, string AppearanceSignature, IReadOnlyList<string> HeadParts,
    IReadOnlyList<string> Assets, IReadOnlyList<string> Dependencies, IReadOnlyList<string> Unresolved,
    string? TraitsSource);
