using Microsoft.Data.Sqlite;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// Audit v2 F-17: every edit of a plugin (new mtime, new signature) added a full copy of its rows to the
/// shared index cache and nothing ever removed the old ones. Indexing now purges the plugin rows that the
/// active load order does not use and nobody has used for <see cref="CatalogStore.UnusedPluginRetention"/>;
/// rows of the active load order, and rows another indexer has just looked up, are never purged.
/// </summary>
public sealed class CatalogPurgeTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-catalog-purge-" + Guid.NewGuid().ToString("N"));

    private sealed class ManualClock(DateTimeOffset now) : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = now;

        public override DateTimeOffset GetUtcNow() => Now;
    }

    private static readonly DateTimeOffset Start = new(2026, 10, 1, 12, 0, 0, TimeSpan.Zero);
    private static readonly TimeSpan PastRetention = CatalogStore.UnusedPluginRetention + TimeSpan.FromDays(1);

    private static PluginExtract OneRecord(string formKey) =>
        new([new GenericRow(formKey, "KYWD", "SNS_Keyword", null, null)], [], [], 0, [], []);

    private static LoadOrderRow Row(int index, long id, string fileName) =>
        new(index, id, fileName, false, false, index.ToString("X2"), fileName, []);

    private static long Count(CatalogStore store, string sql)
    {
        using var cmd = store.Connection.CreateCommand();
        cmd.CommandText = sql;
        return (long)cmd.ExecuteScalar()!;
    }

    private string Db => Path.Combine(_dir, "catalog.sqlite");

    [Fact]
    public void ARowNoLoadOrderUses_IsPurgedOnceTheRetentionHasPassed()
    {
        var clock = new ManualClock(Start);
        using var store = CatalogStore.Open(Db, clock);
        var active = store.InsertPlugin("Active.esp", "ACTIVE", OneRecord("000801:Active.esp"));
        var old = store.InsertPlugin("Old.esp", "OLD", OneRecord("000802:Old.esp"));
        store.ActivateLoadOrder([Row(0, active, "Active.esp")]);

        // Within the retention the old row stays: switching back to that load order costs no re-parse.
        clock.Now = Start + TimeSpan.FromDays(1);
        Assert.Equal(0, store.PurgeUnusedPlugins());
        Assert.NotNull(store.FindBySignature("OLD"));

        clock.Now = Start + TimeSpan.FromDays(1) + PastRetention;
        Assert.Equal(1, store.PurgeUnusedPlugins());
        Assert.Null(store.FindBySignature("OLD"));
        Assert.Equal(0, Count(store, $"SELECT COUNT(*) FROM records WHERE plugin_id = {old}"));
        Assert.Equal(0, Count(store, $"SELECT COUNT(*) FROM plugin_usage WHERE plugin_id = {old}"));

        Assert.NotNull(store.FindBySignature("ACTIVE"));
        Assert.Equal(1, Count(store, $"SELECT COUNT(*) FROM records WHERE plugin_id = {active}"));
        Assert.Equal(1, Count(store, "SELECT COUNT(*) FROM record_winner"));
        store.EnsureActiveLoadOrder();
    }

    [Fact]
    public void ThePluginsOfTheActiveLoadOrder_AreNeverPurged_HoweverOld()
    {
        var clock = new ManualClock(Start);
        using var store = CatalogStore.Open(Db, clock);
        var a = store.InsertPlugin("A.esp", "A", OneRecord("000801:A.esp"));
        var b = store.InsertPlugin("B.esp", "B", OneRecord("000801:B.esp"));
        store.ActivateLoadOrder([Row(0, a, "A.esp"), Row(1, b, "B.esp")]);

        clock.Now = Start + TimeSpan.FromDays(3650);
        Assert.Equal(0, store.PurgeUnusedPlugins());
        Assert.Equal(2, Count(store, "SELECT COUNT(*) FROM plugin_files"));
        Assert.Equal(2, Count(store, "SELECT COUNT(*) FROM record_winner"));
    }

    [Fact]
    public void ARowAnotherIndexerJustLookedUp_IsNotPurgedFromUnderIt()
    {
        var clock = new ManualClock(Start);
        using var first = CatalogStore.Open(Db, clock);
        using var second = CatalogStore.Open(Db, clock);
        var current = first.InsertPlugin("Current.esp", "CURRENT", OneRecord("000801:Current.esp"));
        var old = first.InsertPlugin("Old.esp", "OLD", OneRecord("000802:Old.esp"));
        first.ActivateLoadOrder([Row(0, current, "Current.esp")]);

        // Months later another studio process indexes the old load order: it finds the row by signature
        // and has not activated it yet when the first process finishes indexing and purges.
        clock.Now = Start + PastRetention + TimeSpan.FromDays(60);
        Assert.Equal(old, second.FindBySignature("OLD")!.Id);
        Assert.Equal(0, first.PurgeUnusedPlugins());

        second.ActivateLoadOrder([Row(0, old, "Old.esp")]);
        second.EnsureActiveLoadOrder();
        Assert.Equal(1, Count(second, "SELECT COUNT(*) FROM record_winner"));
    }

    [Fact]
    public void ARowWrittenBeforeUsageWasTracked_GetsTheFullRetention()
    {
        var clock = new ManualClock(Start);
        using (var store = CatalogStore.Open(Db, clock))
        {
            var active = store.InsertPlugin("Active.esp", "ACTIVE", OneRecord("000801:Active.esp"));
            store.InsertPlugin("Legacy.esp", "LEGACY", OneRecord("000802:Legacy.esp"));
            store.ActivateLoadOrder([Row(0, active, "Active.esp")]);
            // A build without usage tracking wrote this row: no stamp at all.
            CatalogStore.Exec(store.Connection, "DELETE FROM plugin_usage WHERE plugin_id <> " + active);
        }

        clock.Now = Start + TimeSpan.FromDays(400);
        using var reopened = CatalogStore.Open(Db, clock);
        Assert.Equal(0, reopened.PurgeUnusedPlugins());
        clock.Now += PastRetention;
        Assert.Equal(1, reopened.PurgeUnusedPlugins());
        Assert.Null(reopened.FindBySignature("LEGACY"));
    }

    [Fact]
    public void Indexing_KeepsTheRowsOfAPluginThatJustLeftTheLoadOrder()
    {
        using var lo = SyntheticLoadOrder.Create();
        using var state = new WorkerState();
        var full = new IndexRequest
        {
            ProfileKey = "purge-profile",
            CacheDirectory = Path.Combine(_dir, "cache"),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        };

        state.Index(full, _ => { }, CancellationToken.None);
        state.Index(full with { Plugins = full.Plugins.Take(1).ToArray() }, _ => { }, CancellationToken.None);
        // Re-enabling the plugins the same day reuses every cached row.
        var again = state.Index(full, _ => { }, CancellationToken.None);

        Assert.Equal(0, again.ParsedPlugins);
        Assert.Equal(full.Plugins.Count, again.CachedPlugins);
    }

    public void Dispose()
    {
        SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
