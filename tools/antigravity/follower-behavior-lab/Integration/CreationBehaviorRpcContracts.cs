using System.Text.Json;
using System.Text.Json.Serialization;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;

/// <summary>
/// Wire-compatible JSON-RPC 2.0 constants and DTOs matching services/BethesdaWorker/Contracts/Protocol.cs
/// and CreationDtos.cs, bumping Protocol.Minor from 5 to 6 (additive).
/// Reuses FollowerDraftDto (1:1 wire match with NativeNpcDraft) so no second NPC configuration system is created.
/// </summary>
public static class CreationBehaviorRpcMethods
{
    public const int ProtocolMajor = 1;
    public const int ProtocolMinor = 6; // Additive bump from 1.5

    public const string AnalyzeVoice = "creation.analyzeVoice";
    public const string GetBehaviorCatalog = "creation.getBehaviorCatalog";
    public const string ApplyArchetype = "creation.applyArchetype";
    public const string UndoArchetype = "creation.undoArchetype";
    public const string ValidateBehavior = "creation.validateBehavior";
}

public static class WorkerCompatibleErrorCodes
{
    public const int ProtocolMismatch = 1001;
    public const int NotIndexed = 1002;
    public const int RecordNotFound = 1003;
    public const int ValidationFailed = 1004;
    public const int InvalidLoadOrder = 1005;
    public const int IoError = 1006;
    public const int InvalidArgument = 1009;
}

/// <summary>
/// Wire-compatible with SkyrimNpcStudio.Worker.Contracts.Issue (code, severity, message, target).
/// </summary>
public sealed record WorkerCompatibleIssue(
    string Code,
    string Severity,
    string Message,
    string? Target = null);

public sealed record JsonRpcRequestEnvelope
{
    [JsonPropertyName("jsonrpc")]
    public string JsonRpc { get; init; } = "2.0";

    [JsonPropertyName("id")]
    public object? Id { get; init; } = 1;

    [JsonPropertyName("method")]
    public required string Method { get; init; }

    [JsonPropertyName("params")]
    public JsonElement Params { get; init; }
}

public sealed record JsonRpcErrorObject(
    int Code,
    string Message,
    object? Data = null);

public sealed record JsonRpcResponseEnvelope
{
    [JsonPropertyName("jsonrpc")]
    public string JsonRpc { get; init; } = "2.0";

    [JsonPropertyName("id")]
    public object? Id { get; init; }

    [JsonPropertyName("result")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public object? Result { get; init; }

    [JsonPropertyName("error")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public JsonRpcErrorObject? Error { get; init; }
}

// 1. creation.analyzeVoice
public sealed record CreationAnalyzeVoiceRequest
{
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public required string VoiceFormKey { get; init; }
    public bool? Female { get; init; }
    public IReadOnlyList<FactionMembershipDto>? Factions { get; init; }
}

public sealed record CreationAnalyzeVoiceResponse
{
    public required string LoadOrderFingerprint { get; init; }
    public required VoiceDialogueAnalysisResult VoiceAnalysis { get; init; }
    public required IReadOnlyList<WorkerCompatibleIssue> WorkerIssues { get; init; }
    public bool RecruitableGuaranteed => false;
    public bool MarriageGuaranteed => false;
    public string RuntimeVerification => "REQUIRES_IN_GAME_TEST";
}

// 2. creation.getBehaviorCatalog
public sealed record CreationBehaviorCatalogRequest
{
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public string FilterText { get; init; } = "";
    public int Limit { get; init; } = 200;
}

public sealed record ArchetypeCatalogDescriptor(
    string Id,
    string DisplayName,
    string RoleSummary,
    string ClassFormKey,
    string CombatStyleFormKey,
    bool AllLinksResolved,
    IReadOnlyList<string> RequiredMasters,
    IReadOnlyList<BehaviorIssue> Issues);

public sealed record CreationBehaviorCatalogResponse
{
    public required string LoadOrderFingerprint { get; init; }
    public required IReadOnlyList<ClassCatalogEntry> Classes { get; init; }
    public required IReadOnlyList<CombatStyleCatalogEntry> CombatStyles { get; init; }
    public required IReadOnlyList<VoiceCatalogEntry> Voices { get; init; }
    public required IReadOnlyList<ArchetypeCatalogDescriptor> Archetypes { get; init; }
}

// 3. creation.applyArchetype & creation.undoArchetype
public sealed record CreationApplyArchetypeRequest
{
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public required FollowerDraftDto Npc { get; init; }
    public required string ArchetypeId { get; init; }
    public IReadOnlyList<string>? Categories { get; init; }
    public bool DryRun { get; init; } = true;
}

public sealed record CreationApplyArchetypeResponse
{
    public required bool DryRun { get; init; }
    public required string ArchetypeId { get; init; }
    public required string LoadOrderFingerprint { get; init; }
    public required string SourceSha256Before { get; init; }
    public required string SourceSha256After { get; init; }
    public required bool SourceUntouched { get; init; }
    public required string ProposedSha256 { get; init; }
    public required IReadOnlyList<string> AppliedCategories { get; init; }
    public required IReadOnlyList<FieldChangeDto> Changes { get; init; }
    public required FollowerDraftDto ProposedNpc { get; init; }
    public required FollowerDraftDto UndoRestoreToken { get; init; }
    public required ArchetypeValidationReport ArchetypeValidation { get; init; }
}

public sealed record CreationUndoArchetypeRequest
{
    public required string ExpectedOriginalSha256 { get; init; }
    public required FollowerDraftDto UndoRestoreToken { get; init; }
}

public sealed record CreationUndoArchetypeResponse
{
    public required bool Restored { get; init; }
    public required string RestoredSha256 { get; init; }
    public required FollowerDraftDto RestoredNpc { get; init; }
}

// 4. creation.validateBehavior (extends CreationValidateResponse without breaking existing fields)
public sealed record CreationValidateBehaviorRequest
{
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public required FollowerDraftDto Npc { get; init; }
    public string PluginName { get; init; } = "CharacterForge.esp";
}

public sealed record CreationValidateBehaviorResponse
{
    /// <summary>Always false while native FaceGen is pending.</summary>
    public bool Exportable => false;
    public required bool TechnicallyExportable { get; init; }
    public string FaceGenStatus => "pending";
    public string RuntimeStatus => "REQUIRES_IN_GAME_TEST";
    public required string LoadOrderFingerprint { get; init; }
    public required string DraftSha256 { get; init; }
    public required IReadOnlyList<WorkerCompatibleIssue> Issues { get; init; }
    public required IReadOnlyList<BehaviorIssue> BehaviorIssues { get; init; }
    public required IReadOnlyList<string> MissingReferences { get; init; }
    public required IReadOnlyList<string> Masters { get; init; }
    public required VoiceDialogueAnalysisResult VoiceAnalysis { get; init; }
    public ClassCatalogEntry? ResolvedClass { get; init; }
    public CombatStyleCatalogEntry? ResolvedCombatStyle { get; init; }
    public required IReadOnlyList<ResolvedLinkEvidence> ResolvedLinks { get; init; }
    public IReadOnlyList<string> Limitations { get; init; } =
    [
        "Native FaceGen geometry and tint generation are not verified; final export is blocked.",
        "Static voice/FLST and AI package checks do not guarantee in-game recruitment or marriage; verify in Skyrim SE 1.6.1170."
    ];
}
