using System.Text.RegularExpressions;
using Nerdbank.Streams;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// AG04 T05 and friends: the five behavior methods over a real StreamJsonRpc server (the way the studio's WorkerClient talks to the
/// worker), indexed with the lab's synthetic plugins as the data of a test profile. Typed errors, stale-profile refusal,
/// apply -> undo hash round trip, a profile without the follower/marriage lists, and a source scan that keeps fixtures out of production.
/// </summary>
public sealed class BehaviorRpcTransportTests : IAsyncDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-ag04-rpc-" + Guid.NewGuid().ToString("N"));
    private readonly WorkerState _state = new();
    private readonly JsonRpc _server;
    private readonly JsonRpc _client;
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    public BehaviorRpcTransportTests()
    {
        Directory.CreateDirectory(_dir);
        LabSyntheticFixture.EnsureBasicFixture(_dir);
        var (serverStream, clientStream) = FullDuplexStream.CreatePair();
        _server = RpcHost.Attach(serverStream, _state, () => { });
        var formatter = new SystemTextJsonFormatter();
        RpcHost.ConfigureSerializer(formatter.JsonSerializerOptions);
        _client = new JsonRpc(new HeaderDelimitedMessageHandler(clientStream, clientStream, formatter));
        _client.StartListening();
    }

    private Task<IndexResponse> Index(string key, bool brokenOverride, string? dir = null, string[]? names = null)
    {
        dir ??= _dir;
        names ??= [LabSyntheticFixture.Skyrim.FileName, LabSyntheticFixture.FollowerVoiceAddon.FileName,
                   LabSyntheticFixture.BrokenVoiceOverride.FileName, LabSyntheticFixture.DisabledCombatMod.FileName];
        return _client.InvokeWithParameterObjectAsync<IndexResponse>(Methods.CatalogIndex, new IndexRequest
        {
            ProfileKey = key,
            CacheDirectory = Path.Combine(dir, "_cache_" + key),
            Plugins = names.Select(n => new LoadOrderEntry
            {
                FileName = n,
                Path = Path.Combine(dir, n),
                Enabled = n != LabSyntheticFixture.DisabledCombatMod.FileName && (n != LabSyntheticFixture.BrokenVoiceOverride.FileName || brokenOverride),
            }).ToArray(),
        }, Ct);
    }

    private Task<T> Call<T>(string method, object request) => _client.InvokeWithParameterObjectAsync<T>(method, request, Ct);

    private static readonly string Commander = LabSyntheticFixture.VoiceFemaleCommander.ToString();

    [Fact]
    public async Task EveryBehaviorMethod_BeforeIndex_AnswersTheTypedNotIndexedError()
    {
        var draft = LabSyntheticFixture.CreateSampleDraft();
        var calls = new (string Method, object Request)[]
        {
            (CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander }),
            (CreationBehaviorRpcMethods.GetBehaviorCatalog, new CreationBehaviorCatalogRequest()),
            (CreationBehaviorRpcMethods.ApplyArchetype, new CreationApplyArchetypeRequest { Npc = draft, ArchetypeId = "archer" }),
            (CreationBehaviorRpcMethods.ValidateBehavior, new CreationValidateBehaviorRequest { Npc = draft }),
        };
        foreach (var (method, request) in calls)
        {
            var ex = await Assert.ThrowsAsync<RemoteInvocationException>(() => Call<object>(method, request));
            Assert.Equal(ErrorCodes.NotIndexed, ex.ErrorCode);
        }
    }

    [Fact]
    public async Task AnalyzeVoice_OverTheWire_ReturnsTheEvidenceOfTheIndexedProfile_AndAReindexChangesIt()
    {
        await Index("wire-a", brokenOverride: false);
        var a = await Call<CreationAnalyzeVoiceResponse>(CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, Female = true });
        Assert.True(a.VoiceAnalysis.VoiceResolved);
        Assert.True(a.VoiceAnalysis.HasStaticFollowerListEvidence);
        Assert.Equal("REQUIRES_IN_GAME_TEST", a.RuntimeVerification);
        Assert.False(a.RecruitableGuaranteed);
        Assert.Equal(64, a.LoadOrderFingerprint.Length);

        // The studio activates another profile (same worker, new index): the context of the first one is gone, answers follow the second.
        await Index("wire-b", brokenOverride: true);
        var b = await Call<CreationAnalyzeVoiceResponse>(CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, Female = true });
        Assert.NotEqual(a.LoadOrderFingerprint, b.LoadOrderFingerprint);
        Assert.False(b.VoiceAnalysis.HasStaticFollowerListEvidence);

        // And back: the same load order gives back the same fingerprint (it identifies the profile and its files, not the call).
        await Index("wire-a", brokenOverride: false);
        var again = await Call<CreationAnalyzeVoiceResponse>(CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, Female = true });
        Assert.Equal(a.LoadOrderFingerprint, again.LoadOrderFingerprint);
    }

    [Fact]
    public async Task ARequestMadeForAnotherProfile_IsRefused_NotAnsweredWithThisProfilesEvidence()
    {
        await Index("wire-current", brokenOverride: false);
        var ex = await Assert.ThrowsAsync<RemoteInvocationException>(() => Call<CreationAnalyzeVoiceResponse>(
            CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, ProfileName = "the-previous-profile" }));
        Assert.Equal(ErrorCodes.InvalidLoadOrder, ex.ErrorCode);
        // The same request naming the indexed profile is answered.
        var ok = await Call<CreationAnalyzeVoiceResponse>(CreationBehaviorRpcMethods.AnalyzeVoice,
            new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, ProfileName = "wire-current" });
        Assert.True(ok.VoiceAnalysis.VoiceResolved);
    }

    [Fact]
    public async Task AFixtureDirectoryInARequest_IsRefused_ForEveryMethod()
    {
        await Index("wire-fixture", brokenOverride: false);
        var draft = LabSyntheticFixture.CreateSampleDraft();
        var calls = new (string Method, object Request)[]
        {
            (CreationBehaviorRpcMethods.AnalyzeVoice, new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, FixtureDirectory = _dir }),
            (CreationBehaviorRpcMethods.GetBehaviorCatalog, new CreationBehaviorCatalogRequest { FixtureDirectory = _dir }),
            (CreationBehaviorRpcMethods.ApplyArchetype, new CreationApplyArchetypeRequest { Npc = draft, ArchetypeId = "archer", FixtureDirectory = _dir }),
            (CreationBehaviorRpcMethods.ValidateBehavior, new CreationValidateBehaviorRequest { Npc = draft, FixtureDirectory = _dir }),
        };
        foreach (var (method, request) in calls)
        {
            var ex = await Assert.ThrowsAsync<RemoteInvocationException>(() => Call<object>(method, request));
            Assert.Equal(ErrorCodes.InvalidArgument, ex.ErrorCode);
        }
    }

    [Fact]
    public async Task TheCatalog_ListsTheArchetypesAndVoicesOfTheIndexedProfile()
    {
        await Index("wire-catalog", brokenOverride: false);
        var catalog = await Call<CreationBehaviorCatalogResponse>(CreationBehaviorRpcMethods.GetBehaviorCatalog, new CreationBehaviorCatalogRequest());
        Assert.Contains(catalog.Archetypes, a => a.Id == "defensive-warrior");
        Assert.Contains(catalog.Voices, v => v.FormKey == Commander);
        Assert.NotEmpty(catalog.Classes);
        Assert.Equal(64, catalog.LoadOrderFingerprint.Length);
        // The class that lives only in the DISABLED plugin is not offered as available.
        Assert.DoesNotContain(catalog.Classes, c => c.FormKey == LabSyntheticFixture.ClassDisabledBerserker.ToString());
    }

    [Fact]
    public async Task ApplyThenUndo_RestoresTheExactDraft_ByHash_AndNeverClaimsAPlayableExport()
    {
        await Index("wire-apply", brokenOverride: false);
        var draft = LabSyntheticFixture.CreateSampleDraft();
        var applied = await Call<CreationApplyArchetypeResponse>(CreationBehaviorRpcMethods.ApplyArchetype,
            new CreationApplyArchetypeRequest { Npc = draft, ArchetypeId = "archer", DryRun = true });
        Assert.True(applied.SourceUntouched);
        Assert.NotEmpty(applied.Changes);
        Assert.NotEqual(applied.SourceSha256Before, applied.ProposedSha256);
        // Identity and appearance fields are never touched by an archetype.
        Assert.Equal(draft.Name, applied.ProposedNpc.Name);
        Assert.Equal(draft.EditorId, applied.ProposedNpc.EditorId);

        var undone = await Call<CreationUndoArchetypeResponse>(CreationBehaviorRpcMethods.UndoArchetype,
            new CreationUndoArchetypeRequest { ExpectedOriginalSha256 = applied.SourceSha256Before, UndoRestoreToken = applied.UndoRestoreToken });
        Assert.True(undone.Restored);
        Assert.Equal(applied.SourceSha256Before, undone.RestoredSha256);

        // A wrong hash is refused (the token does not belong to what the studio holds).
        var bad = await Assert.ThrowsAsync<RemoteInvocationException>(() => Call<CreationUndoArchetypeResponse>(CreationBehaviorRpcMethods.UndoArchetype,
            new CreationUndoArchetypeRequest { ExpectedOriginalSha256 = new string('0', 64), UndoRestoreToken = applied.UndoRestoreToken }));
        Assert.Equal(ErrorCodes.ValidationFailed, bad.ErrorCode);

        // An unknown archetype is a typed error, not a crash.
        var unknown = await Assert.ThrowsAsync<RemoteInvocationException>(() => Call<CreationApplyArchetypeResponse>(CreationBehaviorRpcMethods.ApplyArchetype,
            new CreationApplyArchetypeRequest { Npc = draft, ArchetypeId = "no-such-archetype" }));
        Assert.Equal(ErrorCodes.RecordNotFound, unknown.ErrorCode);
    }

    [Fact]
    public async Task ValidateBehavior_KeepsNativeFaceGenBlocked_AndNeverPromisesRecruitmentOrMarriage()
    {
        await Index("wire-validate", brokenOverride: false);
        var v = await Call<CreationValidateBehaviorResponse>(CreationBehaviorRpcMethods.ValidateBehavior,
            new CreationValidateBehaviorRequest { Npc = LabSyntheticFixture.CreateSampleDraft() });
        Assert.False(v.Exportable);
        Assert.Equal("pending", v.FaceGenStatus);
        Assert.Contains(v.Issues, i => i.Code == "NativeFaceGenUnavailable" && i.Severity == "error");
        Assert.Equal("REQUIRES_IN_GAME_TEST", v.RuntimeStatus);
        Assert.NotNull(v.VoiceAnalysis);
        Assert.Equal(64, v.LoadOrderFingerprint.Length);
    }

    [Fact]
    public async Task AProfileWithoutTheFollowerAndMarriageLists_IsUnknown_NotAFalseCompatible()
    {
        var bare = Path.Combine(_dir, "no-marriage");
        LabSyntheticFixture.BuildNoMarriageListFixture(bare);
        await Index("wire-bare", brokenOverride: false, dir: bare, names: [LabSyntheticFixture.Skyrim.FileName]);
        var r = await Call<CreationAnalyzeVoiceResponse>(CreationBehaviorRpcMethods.AnalyzeVoice,
            new CreationAnalyzeVoiceRequest { VoiceFormKey = Commander, Female = true });
        Assert.False(r.VoiceAnalysis.HasStaticMarriageListEvidence);
        Assert.NotEqual(EvidenceClassification.STATIC_EVIDENCE, r.VoiceAnalysis.MarriageDialogueClassification);
        Assert.False(r.MarriageGuaranteed);
    }

    /// <summary>Production source never opens, builds or falls back to a laboratory fixture (the AG04 defect, kept from coming back).</summary>
    [Fact]
    public void TheWorkersProductionSource_NeverTouchesTheLaboratoryFixtures()
    {
        var root = SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine.SandboxGuard.FindRepoRoot() ?? Directory.GetCurrentDirectory();
        var worker = Path.Combine(root, "services", "BethesdaWorker");
        var offenders = new List<string>();
        var forbidden = new Regex(@"OpenDirectory\s*\(|EnsureBasicFixture|LabSyntheticFixture|BuildNoMarriageListFixture|FindRepoRoot|HandleAnalyzeVoice\(\s*request|HandleGetCatalog\(\s*request|HandleApplyArchetype\(\s*request|HandleValidateBehavior\(\s*request|DispatchJson", RegexOptions.Compiled);
        foreach (var file in Directory.EnumerateFiles(worker, "*.cs", SearchOption.AllDirectories).Where(f => !f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}") && !f.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}")))
        {
            foreach (var (line, index) in File.ReadAllLines(file).Select((l, i) => (l, i)))
            {
                if (!line.TrimStart().StartsWith("//", StringComparison.Ordinal) && forbidden.IsMatch(line)) offenders.Add($"{Path.GetRelativePath(root, file)}:{index + 1}: {line.Trim()}");
            }
        }

        Assert.Empty(offenders);
    }

    public async ValueTask DisposeAsync()
    {
        _client.Dispose();
        _server.Dispose();
        await Task.CompletedTask;
        _state.Dispose();
        try { Directory.Delete(_dir, recursive: true); } catch (IOException) { }
    }
}
