using StreamJsonRpc;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// Audit v2 F-14: an export ran inside the worker's catalog lock, so every search, status and index
/// request queued behind it for as long as the export took (and the studio timed out on long ones).
/// During an export the catalog belongs to the export alone (Mutagen overlays and the SQLite connection
/// are not thread-safe): other requests must answer at once with Busy, never wait and never see a
/// half-done state, and the catalog answers exactly as before once the export ends.
/// </summary>
public sealed class ExportConcurrencyTests : IDisposable
{
    private static readonly TimeSpan Prompt = TimeSpan.FromSeconds(5);

    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly WorkerRpcTarget _rpc;
    private readonly string _out;

    public ExportConcurrencyTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-export-concurrency-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(Request(), _ => { }, CancellationToken.None);
        _rpc = new WorkerRpcTarget(_state, () => { }, (_, _) => { });
    }

    private IndexRequest Request() => new()
    {
        ProfileKey = "export-concurrency-tests",
        CacheDirectory = Path.Combine(_out, "_cache"),
        Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
    };

    private ExportOutfitPatchRequest Outfit(string pluginName) => new()
    {
        PluginName = pluginName,
        OutputDirectory = _out,
        CreateZip = true,
        Assignments =
        [
            new OutfitAssignment
            {
                NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
            },
        ],
    };

    private static readonly NpcSearchRequest Query = new() { Text = "Lydia" };

    private static string[] Keys(NpcSearchResponse r) => r.Items.Select(i => i.FormKey).ToArray();

    /// <summary>The protocol error code of a call, or null when it answered normally.</summary>
    private static int? ErrorCodeOf(Action call)
    {
        try
        {
            call();
            return null;
        }
        catch (LocalRpcException ex)
        {
            return ex.ErrorCode;
        }
    }

    [Fact]
    public async Task ASearchDuringAnExport_AnswersAtOnceWithBusy_AndTheSameResultsAfterwards()
    {
        var ct = TestContext.Current.CancellationToken;
        var before = _rpc.SearchNpcs(Query);
        Assert.NotEmpty(before.Items);

        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        _state.ExportStarted = () =>
        {
            started.Set();
            release.Wait(TimeSpan.FromSeconds(30), ct);
        };
        var export = _rpc.ExportOutfitPatch(Outfit("SNS_Concurrent.esp"), ct);
        try
        {
            Assert.True(started.Wait(Prompt, ct), "The export never started.");

            var search = Task.Run(() => ErrorCodeOf(() => _rpc.SearchNpcs(Query)), ct);
            Assert.True(await Task.WhenAny(search, Task.Delay(Prompt, ct)) == search,
                "A search waited for the running export instead of answering.");
            Assert.Equal(ErrorCodes.Busy, await search);

            var status = Task.Run(_rpc.Status, ct);
            Assert.True(await Task.WhenAny(status, Task.Delay(Prompt, ct)) == status, "catalog.status waited for the export.");
            Assert.True((await status).Busy);
            Assert.True((await status).Indexed);

            // Nothing else may use (or replace) the catalog the export is reading.
            var second = await Assert.ThrowsAsync<LocalRpcException>(() => _rpc.ExportOutfitPatch(Outfit("SNS_Second.esp"), ct));
            Assert.Equal(ErrorCodes.Busy, second.ErrorCode);
            var reindex = Assert.Throws<WorkerException>(() => _state.Index(Request(), _ => { }, ct));
            Assert.Equal(ErrorCodes.Busy, reindex.Code);
        }
        finally
        {
            release.Set();
        }

        var response = await export;
        Assert.True(File.Exists(response.PluginPath));
        Assert.False(Directory.Exists(Path.Combine(_out, "SNS_Second")));
        Assert.False(_rpc.Status().Busy);
        Assert.Equal(Keys(before), Keys(_rpc.SearchNpcs(Query)));
        Assert.Equal(before.Total, _rpc.SearchNpcs(Query).Total);
    }

    [Fact]
    public async Task AFailedExport_GivesTheCatalogBack()
    {
        var ct = TestContext.Current.CancellationToken;
        var before = _rpc.SearchNpcs(Query);

        // The plugin name is refused inside the export, after it took the catalog.
        var failed = await Assert.ThrowsAsync<LocalRpcException>(() => _rpc.ExportOutfitPatch(Outfit("Skyrim.esm"), ct));
        Assert.NotEqual(ErrorCodes.Busy, failed.ErrorCode);

        Assert.False(_rpc.Status().Busy);
        Assert.Equal(Keys(before), Keys(_rpc.SearchNpcs(Query)));
        var response = await _rpc.ExportOutfitPatch(Outfit("SNS_AfterFailure.esp"), ct);
        Assert.True(File.Exists(response.PluginPath));
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_out, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
