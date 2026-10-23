using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// AG04 phase 1: the five behavior endpoints answer from the profile the worker has INDEXED, never from the laboratory fixture.
/// The lab's synthetic plugins are used here only as the data of two test profiles that are indexed through the real worker
/// (WorkerState.Index -> LoadOrderSession), exactly as the studio's MO2 profile is.
/// </summary>
public sealed class BehaviorIntegrationTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-ag04-" + Guid.NewGuid().ToString("N"));
    private readonly List<WorkerState> _states = [];

    public BehaviorIntegrationTests()
    {
        Directory.CreateDirectory(_dir);
        LabSyntheticFixture.EnsureBasicFixture(_dir);
    }

    private static string FixtureDirectoryOfTheRepo()
    {
        var root = SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine.SandboxGuard.FindRepoRoot() ?? Directory.GetCurrentDirectory();
        return Path.Combine(root, "tests", "antigravity", "follower-behavior-lab", "fixtures", "basic");
    }

    /// <summary>A worker whose active catalog is the lab's plugins in the order and with the enabled flags of the given profile.</summary>
    private WorkerRpcTarget IndexedProfile(string profileKey, bool brokenOverrideEnabled)
    {
        var state = new WorkerState();
        _states.Add(state);
        string[] names = [LabSyntheticFixture.Skyrim.FileName, LabSyntheticFixture.FollowerVoiceAddon.FileName,
                          LabSyntheticFixture.BrokenVoiceOverride.FileName, LabSyntheticFixture.DisabledCombatMod.FileName];
        state.Index(new IndexRequest
        {
            ProfileKey = profileKey,
            CacheDirectory = Path.Combine(_dir, "_cache_" + profileKey),
            Plugins = names.Select(n => new LoadOrderEntry
            {
                FileName = n,
                Path = Path.Combine(_dir, n),
                Enabled = n != LabSyntheticFixture.DisabledCombatMod.FileName && (n != LabSyntheticFixture.BrokenVoiceOverride.FileName || brokenOverrideEnabled),
            }).ToArray(),
        }, _ => { }, CancellationToken.None);
        return new WorkerRpcTarget(state, () => { }, (_, _) => { });
    }

    private static int? ErrorCodeOf(Action call)
    {
        try { call(); return null; }
        catch (LocalRpcException ex) { return ex.ErrorCode; }
    }

    private static string FixtureStamp(string dir) =>
        !Directory.Exists(dir) ? "absent" : string.Join("|", Directory.GetFiles(dir).OrderBy(f => f, StringComparer.Ordinal)
            .Select(f => Path.GetFileName(f) + ":" + new FileInfo(f).Length + ":" + File.GetLastWriteTimeUtc(f).Ticks));

    // T01
    [Fact]
    public void NotIndexed_AnswersNotIndexed_ForAllFiveEndpoints_AndNeverTouchesTheLabFixture()
    {
        var rpc = new WorkerRpcTarget(new WorkerState(), () => { }, (_, _) => { });
        var fixtures = FixtureDirectoryOfTheRepo();
        var before = FixtureStamp(fixtures);
        var draft = LabSyntheticFixture.CreateSampleDraft();

        Assert.Equal(ErrorCodes.NotIndexed, ErrorCodeOf(() => rpc.AnalyzeVoice(new() { VoiceFormKey = LabSyntheticFixture.VoiceFemaleCommander.ToString() })));
        Assert.Equal(ErrorCodes.NotIndexed, ErrorCodeOf(() => rpc.GetBehaviorCatalog(new())));
        Assert.Equal(ErrorCodes.NotIndexed, ErrorCodeOf(() => rpc.ApplyArchetype(new() { Npc = draft, ArchetypeId = "defensive-warrior" })));
        Assert.Equal(ErrorCodes.NotIndexed, ErrorCodeOf(() => rpc.ValidateBehavior(new() { Npc = draft })));
        // The fifth, undo, needs no profile: it only checks the restore token against its hash (see the undo tests).
        Assert.Equal(before, FixtureStamp(fixtures));
    }

    // T02 + T03
    [Fact]
    public void TheEvidenceComesFromTheIndexedProfile_AndDiffersBetweenProfileAAndB()
    {
        var a = IndexedProfile("ag04-a", brokenOverrideEnabled: false);
        var b = IndexedProfile("ag04-b", brokenOverrideEnabled: true);
        var request = new CreationAnalyzeVoiceRequest { VoiceFormKey = LabSyntheticFixture.VoiceFemaleCommander.ToString(), Female = true };

        var ra = a.AnalyzeVoice(request);
        var rb = b.AnalyzeVoice(request);

        Assert.NotEqual(ra.LoadOrderFingerprint, rb.LoadOrderFingerprint);
        Assert.True(ra.VoiceAnalysis.HasStaticFollowerListEvidence);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, ra.VoiceAnalysis.FollowerDialogueClassification);
        // Profile B enables the override that removes the voice from VoicesFollower: the same call must say so.
        Assert.False(rb.VoiceAnalysis.HasStaticFollowerListEvidence);
        Assert.NotEqual(ra.VoiceAnalysis.FollowerListProvenance?.WinningPlugin, rb.VoiceAnalysis.FollowerListProvenance?.WinningPlugin);
        // Never a guarantee, whatever the evidence says.
        Assert.False(ra.RecruitableGuaranteed);
        Assert.Equal("REQUIRES_IN_GAME_TEST", ra.RuntimeVerification);
    }

    public void Dispose()
    {
        foreach (var s in _states) s.Dispose();
        try { Directory.Delete(_dir, recursive: true); } catch (IOException) { }
    }
}
