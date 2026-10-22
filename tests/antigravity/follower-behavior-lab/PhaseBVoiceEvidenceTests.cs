using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using Xunit;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Tests;

public sealed class PhaseBVoiceEvidenceTests : IDisposable
{
    private readonly string _basicDir;
    private readonly string _noMarriageDir;

    public PhaseBVoiceEvidenceTests()
    {
        _basicDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-b-basic-" + Guid.NewGuid().ToString("N"));
        _noMarriageDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-b-nomar-" + Guid.NewGuid().ToString("N"));
        LabSyntheticFixture.EnsureBasicFixture(_basicDir);
        LabSyntheticFixture.BuildNoMarriageListFixture(_noMarriageDir);
    }

    [Fact]
    public void V02_VanillaVoiceInFollowerAndMarriageLists_HasStaticEvidenceAndRequiresInGameTest()
    {
        using var context = PluginContext.OpenDirectory(_basicDir, "default");
        var result = VoiceDialogueAnalyzer.AnalyzeVoice(
            context,
            LabSyntheticFixture.VoiceFemaleEvenToned.ToString(),
            draftIsFemale: true);

        Assert.True(result.VoiceResolved);
        Assert.Equal("FemaleEvenToned", result.VoiceEditorId);
        Assert.Equal("Skyrim.esm", result.DefiningPlugin);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, result.FollowerDialogueClassification);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, result.MarriageDialogueClassification);
        Assert.True(result.HasStaticFollowerListEvidence);
        Assert.True(result.HasStaticMarriageListEvidence);
        Assert.Equal("REQUIRES_IN_GAME_TEST", result.RuntimeVerificationState);

        Assert.NotNull(result.FollowerListProvenance);
        Assert.True(result.FollowerListProvenance!.PresentInVanilla);
        Assert.True(result.FollowerListProvenance.PresentInWinner);
        Assert.Equal("Skyrim.esm", result.FollowerListProvenance.SourcePlugin);
        Assert.Equal("FollowerVoiceAddon.esp", result.FollowerListProvenance.WinningPlugin);
    }

    [Fact]
    public void V03_VoiceAddedByModPlugin_IsClassifiedAsModDependent()
    {
        using var context = PluginContext.OpenDirectory(_basicDir, "default");
        var result = VoiceDialogueAnalyzer.AnalyzeVoice(
            context,
            LabSyntheticFixture.VoiceModCustomFollower.ToString(),
            draftIsFemale: true);

        Assert.True(result.VoiceResolved);
        Assert.Equal("FollowerVoiceAddon.esp", result.DefiningPlugin);
        Assert.Equal(EvidenceClassification.MOD_DEPENDENT, result.FollowerDialogueClassification);
        Assert.True(result.HasStaticFollowerListEvidence);
        Assert.NotNull(result.FollowerListProvenance);
        Assert.False(result.FollowerListProvenance!.PresentInVanilla);
        Assert.True(result.FollowerListProvenance.PresentInWinner);
        Assert.Equal("FollowerVoiceAddon.esp", result.FollowerListProvenance.AddedByPlugin);
        Assert.Contains(result.Issues, i => i.Code == "VoiceFollowerModDependent");
    }

    [Fact]
    public void V04_VoiceRemovedByWinningOverride_DetectsConflictAndIdentifiesBothPlugins()
    {
        using var context = PluginContext.OpenDirectory(_basicDir, "override-conflict");
        var result = VoiceDialogueAnalyzer.AnalyzeVoice(
            context,
            LabSyntheticFixture.VoiceFemaleCommander.ToString(),
            draftIsFemale: true);

        Assert.True(result.VoiceResolved);
        Assert.Equal(EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE, result.FollowerDialogueClassification);
        Assert.False(result.HasStaticFollowerListEvidence);
        Assert.NotNull(result.FollowerListProvenance);
        Assert.True(result.FollowerListProvenance!.PresentInVanilla);
        Assert.False(result.FollowerListProvenance.PresentInWinner);
        Assert.Equal("Skyrim.esm", result.FollowerListProvenance.SourcePlugin);
        Assert.Equal("BrokenVoiceOverride.esp", result.FollowerListProvenance.RemovedByPlugin);

        var conflictIssue = Assert.Single(result.Issues, i => i.Code == "VoiceFollowerRemovedByOverride");
        Assert.Equal(ValidationSeverity.ERROR, conflictIssue.Severity);
        Assert.Equal("Skyrim.esm", conflictIssue.SourcePlugin);
        Assert.Equal("BrokenVoiceOverride.esp", conflictIssue.WinningPlugin);
    }

    [Fact]
    public void V05_VoiceNotFoundOrMalformed_ReturnsTypedErrorIssue()
    {
        using var context = PluginContext.OpenDirectory(_basicDir, "default");
        var missingKey = "099999:Skyrim.esm";
        var result = VoiceDialogueAnalyzer.AnalyzeVoice(context, missingKey, draftIsFemale: true);

        Assert.False(result.VoiceResolved);
        Assert.Equal(EvidenceClassification.UNKNOWN, result.FollowerDialogueClassification);
        Assert.Equal(EvidenceClassification.UNKNOWN, result.MarriageDialogueClassification);
        Assert.False(result.HasStaticFollowerListEvidence);
        var issue = Assert.Single(result.Issues, i => i.Code == "VoiceNotFound");
        Assert.Equal(ValidationSeverity.ERROR, issue.Severity);
        Assert.Equal(missingKey, issue.TargetFormKey);
    }

    [Fact]
    public void V06_MarriageListAbsentFromLoadOrder_ReturnsUnknownClassification()
    {
        using var context = PluginContext.OpenDirectory(_noMarriageDir, "default");
        var result = VoiceDialogueAnalyzer.AnalyzeVoice(
            context,
            LabSyntheticFixture.VoiceFemaleEvenToned.ToString(),
            draftIsFemale: true);

        Assert.True(result.VoiceResolved);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, result.FollowerDialogueClassification);
        Assert.Equal(EvidenceClassification.UNKNOWN, result.MarriageDialogueClassification);
        Assert.False(result.HasStaticMarriageListEvidence);
        Assert.NotNull(result.MarriageListProvenance);
        Assert.False(result.MarriageListProvenance!.ListFound);
        Assert.Contains(result.Issues, i => i.Code == "MarriageVoiceListMissing" && i.Severity == ValidationSeverity.UNKNOWN);
    }

    [Fact]
    public void V07_MarriageFactionWithoutCompatibleMarriageVoice_DoesNotClaimMarriageWorks()
    {
        using var context = PluginContext.OpenDirectory(_basicDir, "default");
        // FemaleCommander is in VoicesFollower, but NOT in VoicesMarriage
        var draft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_CommanderCandidate",
            name: "Commander Candidate",
            voiceKey: LabSyntheticFixture.VoiceFemaleCommander,
            includeMarriageFaction: true);

        var verdict = FollowerValidationEngine.ValidatePlan(context, draft);

        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, verdict.VoiceAnalysis.FollowerDialogueClassification);
        Assert.Equal(EvidenceClassification.UNKNOWN, verdict.VoiceAnalysis.MarriageDialogueClassification);
        Assert.False(verdict.VoiceAnalysis.HasStaticMarriageListEvidence);
        Assert.Contains(verdict.Issues, i => i.Code == "MarriageFactionWithoutCompatibleVoice" && i.Severity == ValidationSeverity.WARNING);
    }

    [Fact]
    public void V08_DifferentProfileModifiesVoiceLists_InvalidatesFingerprintAndRecalculatesVerdict()
    {
        using var defaultCtx = PluginContext.OpenDirectory(_basicDir, "default");
        using var conflictCtx = PluginContext.OpenDirectory(_basicDir, "override-conflict");

        Assert.NotEqual(defaultCtx.LoadOrderFingerprint, conflictCtx.LoadOrderFingerprint);

        var draft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_ProfileSwitchCandidate",
            name: "Profile Switch Candidate",
            voiceKey: LabSyntheticFixture.VoiceFemaleCommander);

        var verdictDefault = FollowerValidationEngine.ValidatePlan(defaultCtx, draft);
        var verdictConflict = FollowerValidationEngine.ValidatePlan(conflictCtx, draft);

        Assert.NotEqual(verdictDefault.LoadOrderFingerprint, verdictConflict.LoadOrderFingerprint);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, verdictDefault.VoiceAnalysis.FollowerDialogueClassification);
        Assert.True(verdictDefault.StructurallyValid);

        Assert.Equal(EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE, verdictConflict.VoiceAnalysis.FollowerDialogueClassification);
        Assert.False(verdictConflict.StructurallyValid);
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_basicDir)) Directory.Delete(_basicDir, recursive: true);
            if (Directory.Exists(_noMarriageDir)) Directory.Delete(_noMarriageDir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
