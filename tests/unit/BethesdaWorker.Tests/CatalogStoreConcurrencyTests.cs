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
