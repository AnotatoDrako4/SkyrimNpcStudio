using System.Text.Json.Serialization;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

/// <summary>
/// Compatible representation of Character Forge's NativeNpcDraft (engines/creation/include/sns/creation/native_npc_draft.hpp
/// and services/BethesdaWorker/Contracts/CreationDtos.cs), extended with optional follower behavior links (Spells, Perks)
/// without modifying the core NativeNpcDraft.
/// </summary>
public sealed record FollowerDraftDto
{
    public string Name { get; init; } = "";
    public string EditorId { get; init; } = "";
    public string Description { get; init; } = "";
    public string RaceFormKey { get; init; } = "";
    public bool Female { get; init; }
    public string VoiceFormKey { get; init; } = "";
    public string ClassFormKey { get; init; } = "";
    public string CombatStyleFormKey { get; init; } = "";
    public float Height { get; init; } = 1.0f;
    public float Weight { get; init; } = 50.0f;
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
    public object? Face { get; init; }
    public IReadOnlyList<string> OutfitArmorFormKeys { get; init; } = [];
    public IReadOnlyList<InventoryItemDto> Inventory { get; init; } = [];
    public IReadOnlyList<FactionMembershipDto> Factions { get; init; } = [];
    public IReadOnlyList<string> PackageFormKeys { get; init; } = [];
    public IReadOnlyList<string> SpellFormKeys { get; init; } = [];
    public IReadOnlyList<string> PerkFormKeys { get; init; } = [];
    public PlacementDto? Placement { get; init; }
}

public sealed record InventoryItemDto(string FormKey, int Count = 1);
public sealed record FactionMembershipDto(string FormKey, int Rank = 0);

public sealed record PlacementDto
{
    public string CellFormKey { get; init; } = "";
    public float X { get; init; }
    public float Y { get; init; }
    public float Z { get; init; }
    public float RotationZ { get; init; }
}

public sealed record FollowerPlanInput
{
    public string SchemaVersion { get; init; } = "1.0.0";
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public string PluginName { get; init; } = "FollowerBehaviorTest.esp";
    public required FollowerDraftDto Draft { get; init; }
}

[JsonConverter(typeof(JsonStringEnumConverter))]
public enum EvidenceClassification
{
    STATIC_EVIDENCE,
    MOD_DEPENDENT,
    UNKNOWN,
    INCOMPATIBLE_BY_KNOWN_RULE,
    REQUIRES_IN_GAME_TEST
}

[JsonConverter(typeof(JsonStringEnumConverter))]
public enum ValidationSeverity
{
    ERROR,
    WARNING,
    INFO,
    UNKNOWN,
    REQUIRES_IN_GAME_TEST
}

public sealed record BehaviorIssue(
    string Code,
    ValidationSeverity Severity,
    string Message,
    string? TargetFormKey = null,
    string? SourcePlugin = null,
    string? WinningPlugin = null,
    string? Evidence = null);

public sealed record FormListProvenance
{
    public required string ListFormKey { get; init; }
    public required string ListEditorId { get; init; }
    public required bool ListFound { get; init; }
    public string? SourcePlugin { get; init; }
    public string? WinningPlugin { get; init; }
    public bool PresentInVanilla { get; init; }
    public bool PresentInWinner { get; init; }
    public string? AddedByPlugin { get; init; }
    public string? RemovedByPlugin { get; init; }
    public IReadOnlyList<string> OverrideChain { get; init; } = [];
}

public sealed record VoiceDialogueAnalysisResult
{
    public required string VoiceFormKey { get; init; }
    public string? VoiceEditorId { get; init; }
    public bool VoiceResolved { get; init; }
    public string? DefiningPlugin { get; init; }
    public bool? IsFemaleVoice { get; init; }
    public required EvidenceClassification FollowerDialogueClassification { get; init; }
    public required EvidenceClassification MarriageDialogueClassification { get; init; }
    public required bool HasStaticFollowerListEvidence { get; init; }
    public required bool HasStaticMarriageListEvidence { get; init; }
    public required string RuntimeVerificationState { get; init; }
    public FormListProvenance? FollowerListProvenance { get; init; }
    public FormListProvenance? MarriageListProvenance { get; init; }
    public required string Rationale { get; init; }
    public required IReadOnlyList<BehaviorIssue> Issues { get; init; }
}

public sealed record ClassCatalogEntry(
    string FormKey,
    string EditorId,
    string Name,
    string DefiningPlugin,
    string WinningPlugin,
    bool Resolved,
    string PrimaryTrainingSkill,
    byte MaxTrainingLevel,
    IReadOnlyDictionary<string, byte> SkillWeights,
    IReadOnlyDictionary<string, byte> StatWeights);

public sealed record CombatStyleCatalogEntry(
    string FormKey,
    string EditorId,
    string DefiningPlugin,
    string WinningPlugin,
    bool Resolved,
    float OffensiveMult,
    float DefensiveMult,
    float GroupOffensiveMult,
    float MeleeScoreMult,
    float MagicScoreMult,
    float RangedScoreMult,
    float ShoutScoreMult,
    float UnarmedScoreMult,
    float AvoidThreatChance);

public sealed record VoiceCatalogEntry(
    string FormKey,
    string EditorId,
    string DefiningPlugin,
    string WinningPlugin,
    bool IsFemale,
    EvidenceClassification FollowerEvidence,
    EvidenceClassification MarriageEvidence);

public sealed record PluginSummaryDto(
    int LoadIndex,
    string FileName,
    bool IsLight,
    bool IsMaster,
    long SizeBytes,
    string Sha256,
    IReadOnlyList<string> Masters);

public sealed record CatalogInspectReport
{
    public string SchemaVersion { get; init; } = "1.0.0";
    public required string GeneratedAtUtc { get; init; }
    public required string FixtureDirectory { get; init; }
    public required string LoadOrderFingerprint { get; init; }
    public required IReadOnlyList<PluginSummaryDto> EnabledPlugins { get; init; }
    public required IReadOnlyList<string> DisabledPlugins { get; init; }
    public required IReadOnlyList<ClassCatalogEntry> Classes { get; init; }
    public required IReadOnlyList<CombatStyleCatalogEntry> CombatStyles { get; init; }
    public required IReadOnlyList<VoiceCatalogEntry> Voices { get; init; }
    public required IReadOnlyList<BehaviorIssue> Issues { get; init; }
}

public sealed record ResolvedLinkEvidence(
    string Category,
    string FormKey,
    string ExpectedRecordType,
    bool Resolved,
    string? EditorId,
    string? DefiningPlugin,
    string? WinningPlugin,
    string? Note = null);

public sealed record BehaviorVerdictReport
{
    public string SchemaVersion { get; init; } = "1.0.0";
    public required string GeneratedAtUtc { get; init; }
    public required string LoadOrderFingerprint { get; init; }
    public required string DraftEditorId { get; init; }
    public required string DraftName { get; init; }
    public required string DraftSha256 { get; init; }
    /// <summary>
    /// Always false until native FaceGen NIF and FaceTint DDS are generated and verified in core.
    /// </summary>
    public bool PlayableExportUnlocked { get; init; } = false;
    public string ExportLockReason { get; init; } = "NativeFaceGenUnavailable: Native FaceGen NIF and FaceTint DDS are not generated/verified. Final playable export remains blocked.";
    public required string RuntimeStatus { get; init; }
    public required bool StructurallyValid { get; init; }
    public required VoiceDialogueAnalysisResult VoiceAnalysis { get; init; }
    public ClassCatalogEntry? ResolvedClass { get; init; }
    public CombatStyleCatalogEntry? ResolvedCombatStyle { get; init; }
    public required IReadOnlyList<ResolvedLinkEvidence> ResolvedLinks { get; init; }
    public required IReadOnlyList<string> RequiredMasters { get; init; }
    public required IReadOnlyList<BehaviorIssue> Issues { get; init; }
}

public sealed record ArchetypeTemplate
{
    public required string Id { get; init; }
    public required string DisplayName { get; init; }
    public required string RoleSummary { get; init; }
    public required string ClassFormKey { get; init; }
    public required string CombatStyleFormKey { get; init; }
    public required int Health { get; init; }
    public required int Magicka { get; init; }
    public required int Stamina { get; init; }
    public required IReadOnlyDictionary<string, int> Skills { get; init; }
    public required IReadOnlyList<string> OutfitArmorFormKeys { get; init; }
    public required IReadOnlyList<InventoryItemDto> Inventory { get; init; }
    public required IReadOnlyList<string> SpellFormKeys { get; init; }
    public required IReadOnlyList<string> PerkFormKeys { get; init; }
    public required IReadOnlyList<FactionMembershipDto> Factions { get; init; }
    public required IReadOnlyList<string> PackageFormKeys { get; init; }
}

public sealed record ArchetypeValidationReport
{
    public required string ArchetypeId { get; init; }
    public required string DisplayName { get; init; }
    public required bool AllLinksResolved { get; init; }
    public required ClassCatalogEntry ResolvedClass { get; init; }
    public CombatStyleCatalogEntry? ResolvedCombatStyle { get; init; }
    public required IReadOnlyList<ResolvedLinkEvidence> Links { get; init; }
    public required IReadOnlyList<string> RequiredMasters { get; init; }
    public required IReadOnlyList<BehaviorIssue> Issues { get; init; }
}

public sealed record BatchDraftInput
{
    public string SchemaVersion { get; init; } = "1.0.0";
    public string FixtureDirectory { get; init; } = "";
    public string? ProfileName { get; init; }
    public FollowerDraftDto? Draft { get; init; }
    public IReadOnlyList<FollowerDraftDto>? Drafts { get; init; }
}

public sealed record FieldChangeDto(
    string Category,
    string FieldName,
    string BeforeValueJson,
    string AfterValueJson);

public sealed record DraftDryRunEntry
{
    public required int Index { get; init; }
    public required string EditorId { get; init; }
    public required string Name { get; init; }
    public required string SourceSha256Before { get; init; }
    public required string SourceSha256After { get; init; }
    public required bool SourceUntouched { get; init; }
    public required string ProposedSha256 { get; init; }
    public required IReadOnlyList<string> AppliedCategories { get; init; }
    public required IReadOnlyList<FieldChangeDto> Changes { get; init; }
    public required FollowerDraftDto ProposedDraft { get; init; }
    public required FollowerDraftDto UndoRestoreToken { get; init; }
}

public sealed record BulkDryRunReport
{
    public string SchemaVersion { get; init; } = "1.0.0";
    public required string GeneratedAtUtc { get; init; }
    public required bool DryRun { get; init; }
    public required string ArchetypeId { get; init; }
    public required string LoadOrderFingerprint { get; init; }
    public required IReadOnlyList<string> RequestedCategories { get; init; }
    public required IReadOnlyList<string> ProtectedCategories { get; init; }
    public required ArchetypeValidationReport ArchetypeValidation { get; init; }
    public required int TotalDrafts { get; init; }
    public required bool AllSourcesUntouched { get; init; }
    public required IReadOnlyList<DraftDryRunEntry> Entries { get; init; }
    public required IReadOnlyList<BehaviorIssue> Issues { get; init; }
}

