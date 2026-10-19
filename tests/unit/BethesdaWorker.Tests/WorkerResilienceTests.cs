using System.Runtime.CompilerServices;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>Tests that replace the process-wide Console.Error run alone.</summary>
[CollectionDefinition(nameof(ConsoleErrorIsolation), DisableParallelization = true)]
public sealed class ConsoleErrorIsolation;

/// <summary>
/// Audit v2 F-18 (1/2): Guard mapped the expected failures and let any other exception reach StreamJsonRpc
/// unlogged, so the studio's console showed a bare -32000 with no trace of where it came from.
/// </summary>
[Collection(nameof(ConsoleErrorIsolation))]
public sealed class GuardLoggingTests
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Explode() => throw new InvalidOperationException("simulated worker bug");

    [Fact]
    public void AnUnexpectedException_IsLoggedWithItsStackTrace_AndStillFailsTheCall()
    {
        var original = Console.Error;
        using var captured = new StringWriter();
        Console.SetError(captured);
        try
        {
            var thrown = Assert.Throws<InvalidOperationException>(() => WorkerRpcTarget.Guard(Explode));
            Assert.Equal("simulated worker bug", thrown.Message);
        }
        finally
        {
            Console.SetError(original);
        }

        var log = captured.ToString();
        Assert.Contains("[ERROR]", log, StringComparison.Ordinal);
        Assert.Contains("System.InvalidOperationException: simulated worker bug", log, StringComparison.Ordinal);
        Assert.Contains(nameof(Explode), log, StringComparison.Ordinal);
    }

    [Fact]
    public void AnExpectedFailure_IsNotLoggedAsAnError()
    {
        var original = Console.Error;
        using var captured = new StringWriter();
        Console.SetError(captured);
        try
        {
            Assert.Throws<StreamJsonRpc.LocalRpcException>(() =>
                WorkerRpcTarget.Guard<int>(() => throw new WorkerException(ErrorCodes.RecordNotFound, "no such NPC")));
        }
        finally
        {
            Console.SetError(original);
        }

        Assert.DoesNotContain("[ERROR]", captured.ToString(), StringComparison.Ordinal);
    }
}

/// <summary>
/// Audit v2 F-18 (2/2): a re-index disposed the catalog before it started, so a re-index that failed
/// (cancelled, refused load order, an I/O error half-way) left the studio with no catalog at all. The
/// previous catalog must keep answering until a new one is complete.
/// </summary>
public sealed class ReindexFailureTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();

    private IndexRequest Request() => new()
    {
        ProfileKey = "reindex-failure",
        CacheDirectory = Path.Combine(_lo.DataDirectory, "_cache"),
        Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
    };

    private static readonly NpcSearchRequest Query = new() { Text = "Lydia" };

    public static TheoryData<string> Failures => ["empty load order", "cancelled", "io error while reading plugins", "io error after activation"];

    [Theory]
    [MemberData(nameof(Failures))]
    public void AFailedReindex_KeepsThePreviousCatalog(string failure)
    {
        _state.Index(Request(), _ => { }, CancellationToken.None);
        var before = _state.Read(c => c.Queries.SearchNpcs(Query)).Items.Select(i => i.FormKey).ToArray();
        Assert.NotEmpty(before);

        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Action reindex = failure switch
        {
            "empty load order" => () => _state.Index(Request() with { Plugins = [] }, _ => { }, CancellationToken.None),
            "cancelled" => () => _state.Index(Request(), _ => { }, cancelled.Token),
            "io error while reading plugins" => () => _state.Index(Request(), p =>
            {
                if (p.Stage == "plugins") throw new IOException("simulated read failure");
            }, CancellationToken.None),
            // Same load order, so the winner tables it activated are the previous catalog's too.
            "io error after activation" => () => _state.Index(Request(), p =>
            {
                if (p.Stage == "done") throw new IOException("simulated failure after activation");
            }, CancellationToken.None),
            _ => throw new ArgumentException(failure),
        };
        Assert.ThrowsAny<Exception>(reindex);

        var status = _state.Status();
        Assert.True(status.Indexed, "The failed re-index discarded the previous catalog.");
        Assert.False(status.Busy);
        Assert.Equal(before, _state.Read(c => c.Queries.SearchNpcs(Query)).Items.Select(i => i.FormKey).ToArray());
    }

    [Fact]
    public void ASuccessfulReindex_ReplacesTheCatalog()
    {
        _state.Index(Request(), _ => { }, CancellationToken.None);
        var skyrimOnly = Request() with { Plugins = [Request().Plugins[0]] };
        var response = _state.Index(skyrimOnly, _ => { }, CancellationToken.None);

        Assert.Equal(1, response.PluginCount);
        Assert.Equal(1, _state.Status().PluginCount);
        Assert.Single(_state.Read(c => c.Queries.ListPlugins()).Plugins);
    }

    public void Dispose()
    {
        _state.Dispose();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        _lo.Dispose();
    }
}
