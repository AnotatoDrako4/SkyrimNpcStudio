using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using Xunit;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Tests;

public sealed class PhaseAVoiceTests : IDisposable
{
    private readonly string _tempDir;

    public PhaseAVoiceTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-a-" + Guid.NewGuid().ToString("N"));
        LabSyntheticFixture.EnsureBasicFixture(_tempDir);
    }

    [Fact]
    public void V01_ValidVoiceWithoutDialogueLists_DoesNotClaimRecruitableFollower()
    {
        using var context = PluginContext.OpenDirectory(_tempDir);
        var draft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_SilentVoiceTest",
            name: "Silent Voice Candidate",
            voiceKey: LabSyntheticFixture.VoiceFemaleUniqueSilent);

        var verdict = FollowerValidationEngine.ValidatePlan(context, draft);

        Assert.True(verdict.VoiceAnalysis.VoiceResolved);
        // A valid VTYP that is absent from VoicesFollower FLST must NEVER be classified as STATIC_EVIDENCE
        // nor claim static follower list evidence.
        Assert.False(verdict.VoiceAnalysis.HasStaticFollowerListEvidence);
        Assert.Equal(EvidenceClassification.UNKNOWN, verdict.VoiceAnalysis.FollowerDialogueClassification);
        Assert.Equal("REQUIRES_IN_GAME_TEST", verdict.VoiceAnalysis.RuntimeVerificationState);
        Assert.False(verdict.PlayableExportUnlocked);
        Assert.Contains(verdict.Issues, i => i.Code == "VoiceNotInFollowerList");
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_tempDir))
            {
                Directory.Delete(_tempDir, recursive: true);
            }
        }
        catch (IOException)
        {
        }
    }
}
