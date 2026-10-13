using System.Collections.Concurrent;
using Nerdbank.Streams;
using StreamJsonRpc;
using StreamJsonRpc.Protocol;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>Protocol-level tests: a real StreamJsonRpc server over an in-memory duplex stream.</summary>
public sealed class RpcProtocolTests : IAsyncDisposable
{
    private readonly WorkerState _state = new();
    private readonly JsonRpc _server;
    private readonly JsonRpc _client;
    private readonly ConcurrentQueue<IndexProgress> _progress = new();

    public RpcProtocolTests()
    {
        var (serverStream, clientStream) = FullDuplexStream.CreatePair();
        _server = RpcHost.Attach(serverStream, _state, () => { });

        var formatter = new SystemTextJsonFormatter();
        RpcHost.ConfigureSerializer(formatter.JsonSerializerOptions);
        _client = new JsonRpc(new HeaderDelimitedMessageHandler(clientStream, clientStream, formatter));
        Action<IndexProgress> onProgress = p => _progress.Enqueue(p);
        _client.AddLocalRpcMethod(onProgress.Method, onProgress.Target,
            new JsonRpcMethodAttribute(Methods.IndexProgress) { UseSingleObjectParameterDeserialization = true });
        _client.StartListening();
    }

    [Fact]
    public async Task NativeCreation_ValidatesAndExportsThroughTheExistingRpc()
    {
        using var lo = SyntheticLoadOrder.Create();
        var ct = TestContext.Current.CancellationToken;
        await _client.InvokeWithParameterObjectAsync<IndexResponse>(Methods.CatalogIndex, new IndexRequest
        {
            ProfileKey = "forge-rpc", CacheDirectory = Path.Combine(lo.DataDirectory, "_cache"),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, ct);
        var draft = new NativeNpcDraft { Name = "New person", EditorId = "SNS_NewPerson", Female = true,
            RaceFormKey = SyntheticLoadOrder.NordRace.ToString(), VoiceFormKey = SyntheticLoadOrder.FemaleVoice.ToString(),
            ClassFormKey = SyntheticLoadOrder.WarriorClass.ToString() };
        var validation = await _client.InvokeWithParameterObjectAsync<CreationValidateResponse>("creation.validateNpc", new CreationValidateRequest { Npc = draft }, ct);
        Assert.True(validation.TechnicallyExportable);
        Assert.False(validation.Exportable);
        var records = await _client.InvokeWithParameterObjectAsync<IReadOnlyList<CreationRecord>>("creation.searchRecords", new CreationSearchRequest { Kind = "race", Text = "NordRace" }, ct);
        Assert.Contains(records, r => r.FormKey == SyntheticLoadOrder.NordRace.ToString());
        var output = Path.Combine(lo.DataDirectory, "exports"); Directory.CreateDirectory(output);
        var result = await _client.InvokeWithParameterObjectAsync<CreationExportResponse>("creation.exportNpc", new CreationExportRequest
        { Npc = draft, PluginName = "RpcOriginal.esp", OutputDirectory = output, TechnicalOnly = true }, ct);
        Assert.True(result.ReadBackVerified);
        Assert.Equal("new", Assert.Single(result.Package.Records).Kind);
        var rejected = await Assert.ThrowsAsync<RemoteInvocationException>(() => _client.InvokeWithParameterObjectAsync<CreationExportResponse>("creation.exportNpc", new CreationExportRequest
        { Npc = draft, PluginName = "Final.esp", OutputDirectory = output }, ct));
        Assert.Equal(ErrorCodes.ValidationFailed, rejected.ErrorCode);
    }

    [Fact]
    public async Task Handshake_ReturnsVersions()
    {
        var r = await _client.InvokeWithParameterObjectAsync<HandshakeResponse>(Methods.Handshake,
            new HandshakeRequest { ClientName = "tests", ClientVersion = "0", ProtocolMajor = Protocol.Major }, TestContext.Current.CancellationToken);
        Assert.Equal(Protocol.Major, r.ProtocolMajor);
        Assert.Equal("0.54.4", r.MutagenVersion);
        Assert.Contains("export.outfitPatch", r.Capabilities);
    }

    [Fact]
    public async Task Handshake_RejectsOtherMajorVersions()
    {
        var ex = await Assert.ThrowsAsync<RemoteInvocationException>(() => _client.InvokeWithParameterObjectAsync<HandshakeResponse>(
            Methods.Handshake, new HandshakeRequest { ClientName = "tests", ClientVersion = "0", ProtocolMajor = 99 }, TestContext.Current.CancellationToken));
        Assert.Equal(ErrorCodes.ProtocolMismatch, ex.ErrorCode);
    }

    [Fact]
    public async Task Search_BeforeIndex_ReturnsNotIndexed()
    {
        var ex = await Assert.ThrowsAsync<RemoteInvocationException>(() => _client.InvokeWithParameterObjectAsync<NpcSearchResponse>(
            Methods.SearchNpcs, new NpcSearchRequest { Text = "lydia" }, TestContext.Current.CancellationToken));
        Assert.Equal(ErrorCodes.NotIndexed, ex.ErrorCode);
    }

    [Fact]
    public async Task UnknownMethod_IsMethodNotFound()
    {
        var ex = await Assert.ThrowsAsync<RemoteMethodNotFoundException>(() =>
            _client.InvokeAsync<object>("nope.nothing", Array.Empty<object>()));
        Assert.Equal(JsonRpcErrorCode.MethodNotFound, ex.ErrorCode);
    }

    [Fact]
    public async Task IndexThenSearch_OverTheWire_WithProgressNotifications()
    {
        using var lo = SyntheticLoadOrder.Create();
        var ct = TestContext.Current.CancellationToken;
        var index = await _client.InvokeWithParameterObjectAsync<IndexResponse>(Methods.CatalogIndex, new IndexRequest
        {
            ProfileKey = "rpc",
            CacheDirectory = Path.Combine(lo.DataDirectory, "_cache"),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, ct);
        Assert.Equal(4, index.NpcCount);

        var search = await _client.InvokeWithParameterObjectAsync<NpcSearchResponse>(Methods.SearchNpcs, new NpcSearchRequest { Text = "lydia" }, ct);
        Assert.Equal("HousecarlWhiterun", Assert.Single(search.Items).EditorId);

        var details = await _client.InvokeWithParameterObjectAsync<NpcDetails>(Methods.GetNpc, new NpcDetailsRequest { FormKey = "000A2C8E" }, ct);
        Assert.Equal(3, details.Overrides.Count);

        var missing = await Assert.ThrowsAsync<RemoteInvocationException>(() => _client.InvokeWithParameterObjectAsync<NpcDetails>(
            Methods.GetNpc, new NpcDetailsRequest { FormKey = "FFFFFF:Skyrim.esm" }, ct));
        Assert.Equal(ErrorCodes.RecordNotFound, missing.ErrorCode);

        // Notifications are fire-and-forget; give the last one a moment to arrive.
        for (var i = 0; i < 50 && !_progress.Any(p => p.Stage == "done"); i++)
        {
            await Task.Delay(20, ct);
        }

        Assert.Contains(_progress, p => p.Stage == "plugins" && p.Plugin == "Skyrim.esm");
        Assert.Contains(_progress, p => p.Stage == "done");
        _state.Dispose();
    }

    public async ValueTask DisposeAsync()
    {
        _client.Dispose();
        _server.Dispose();
        await _server.Completion.ContinueWith(_ => { }, TaskScheduler.Default);
        _state.Dispose();
    }
}
