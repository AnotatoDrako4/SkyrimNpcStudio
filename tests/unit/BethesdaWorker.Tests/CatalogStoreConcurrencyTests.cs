using Mutagen.Bethesda.Plugins;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// Every studio build shares one index cache per profile. Two processes indexing the same profile can
/// both miss a plugin's signature and then both insert it: the second insert used to fail the UNIQUE
/// constraint and abort the whole indexing. Two stores on one database file reproduce that order exactly.
/// </summary>
public sealed class CatalogStoreConcurrencyTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-catalog-race-" + Guid.NewGuid().ToString("N"));

    private static PluginExtract Empty() => new([], [], [], 0, [], []);

    [Fact]
    public void InsertPlugin_ReusesTheRowAnotherProcessCommittedForTheSameSignature()
    {
        var db = Path.Combine(_dir, "catalog.sqlite");
        using var first = CatalogStore.Open(db);
        using var second = CatalogStore.Open(db);
        const string signature = "ABCDEF";

        // Both processes look the plugin up before either has written it.
        Assert.Null(first.FindBySignature(signature));
        Assert.Null(second.FindBySignature(signature));

        var id = second.InsertPlugin("Mod.esp", signature, Empty());
        // The first process inserts the same content afterwards: same row, no exception.
        var again = first.InsertPlugin("Mod.esp", signature, Empty());

        Assert.Equal(id, again);
        Assert.Equal(id, first.FindBySignature(signature)!.Id);
    }

    [Fact]
    public void ActivateLoadOrder_DoesNotDeleteAPluginAnotherIndexerIsAboutToActivate()
    {
        var db = Path.Combine(_dir, "catalog.sqlite");
        using var first = CatalogStore.Open(db);
        using var second = CatalogStore.Open(db);
        var firstId = first.InsertPlugin("First.esp", "FIRST", Empty());
        var secondId = second.InsertPlugin("Second.esp", "SECOND", Empty());

        first.ActivateLoadOrder([Row(firstId, "First.esp")]);
        Assert.NotNull(second.FindBySignature("SECOND"));
        second.ActivateLoadOrder([Row(secondId, "Second.esp")]);
    }

    [Fact]
    public void ActivateLoadOrder_ByAnotherProcessWithAnotherLoadOrder_FailsReadsInsteadOfServingItsData()
    {
        var db = Path.Combine(_dir, "catalog.sqlite");
        using var first = CatalogStore.Open(db);
        using var second = CatalogStore.Open(db);
        var firstId = first.InsertPlugin("First.esp", "FIRST", Empty());
        var secondId = second.InsertPlugin("Second.esp", "SECOND", Empty());

        first.ActivateLoadOrder([Row(firstId, "First.esp")]);
        first.EnsureActiveLoadOrder();
        // Another process activates a different load order in the same cache: the first store's winner
        // tables are now someone else's, so its reads must fail instead of answering with them.
        second.ActivateLoadOrder([Row(secondId, "Second.esp")]);
        var error = Assert.Throws<WorkerException>(first.EnsureActiveLoadOrder);
        Assert.Equal(ErrorCodes.IndexReplaced, error.Code);
        second.EnsureActiveLoadOrder();
    }

    [Fact]
    public void ActivateLoadOrder_TheSameLoadOrderInTwoProcesses_IsNotAConflict()
    {
        var db = Path.Combine(_dir, "catalog.sqlite");
        using var first = CatalogStore.Open(db);
        using var second = CatalogStore.Open(db);
        // Same plugin content -> same signature -> the same cached row in both processes.
        var id = first.InsertPlugin("Mod.esp", "SAME", Empty());
        Assert.Equal(id, second.InsertPlugin("Mod.esp", "SAME", Empty()));

        first.ActivateLoadOrder([Row(id, "Mod.esp")]);
        second.ActivateLoadOrder([Row(id, "Mod.esp")]);
        first.EnsureActiveLoadOrder();
        second.EnsureActiveLoadOrder();
    }

    [Fact]
    public void WorkerState_ReadAfterAnotherProcessIndexedAnotherLoadOrder_FailsUntilReindexed()
    {
        using var lo = SyntheticLoadOrder.Create();
        using var first = new WorkerState();
        using var second = new WorkerState();
        var lydia = SyntheticLoadOrder.Lydia.ToString();

        Index(first, lo, SyntheticLoadOrder.Skyrim);
        Assert.NotNull(first.Read(c => c.Queries.GetNpc(lydia)));

        // A second studio indexes the same profile after the user enabled FaceMod.esp in MO2.
        Index(second, lo, SyntheticLoadOrder.Skyrim, SyntheticLoadOrder.FaceMod);
        var error = Assert.Throws<WorkerException>(() => first.Read(c => c.Queries.GetNpc(lydia)));
        Assert.Equal(ErrorCodes.IndexReplaced, error.Code);
        Assert.NotNull(second.Read(c => c.Queries.GetNpc(lydia)));

        // Re-indexing makes the first process consistent with the cache again.
        Index(first, lo, SyntheticLoadOrder.Skyrim);
        Assert.NotNull(first.Read(c => c.Queries.GetNpc(lydia)));
    }

    private void Index(WorkerState state, SyntheticLoadOrder lo, params ModKey[] plugins) =>
        state.Index(new IndexRequest
        {
            ProfileKey = "shared-profile",
            CacheDirectory = Path.Combine(_dir, "cache"),
            Plugins = plugins.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = Path.Combine(lo.DataDirectory, p.FileName) }).ToArray(),
        }, _ => { }, CancellationToken.None);

    private static LoadOrderRow Row(long id, string fileName) =>
        new(0, id, fileName, false, false, "00", fileName, []);

    public void Dispose()
    {
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
