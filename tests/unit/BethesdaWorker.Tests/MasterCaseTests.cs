using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// Plugin names are case-insensitive in the game, but an override's master list keeps whatever spelling its
/// author used (USSEP lists "ccbgssse025-advdsgs.esm", the file is "ccBGSSSE025-AdvDSGS.esm"), so Mutagen's
/// FormKey.ToString() of the override differs in case from the original's. The catalog must still see one
/// record: one row, both versions in its chain, the origin's spelling, no field reported as changed only
/// because a link spells its master differently.
/// </summary>
public sealed class MasterCaseTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-mastercase-" + Guid.NewGuid().ToString("N"));
    private static readonly ModKey Origin = ModKey.FromFileName("ccBGSSSE025-AdvDSGS.esm");
    private static readonly ModKey OriginLower = ModKey.FromFileName("ccbgssse025-advdsgs.esm");
    private static readonly ModKey Tweak = ModKey.FromFileName("LowerTweak.esp");
    private static readonly ModKey Skyrim = ModKey.FromFileName("Skyrim.esm");

    private WorkerState IndexTwoSpellings()
    {
        Directory.CreateDirectory(_dir);
        // The indexer requires Skyrim.esm in the load order; an empty one is enough here.
        var skyrim = new SkyrimMod(Skyrim, SkyrimRelease.SkyrimSE);
        skyrim.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Master;
        skyrim.WriteToBinary(Path.Combine(_dir, Skyrim.FileName));
        var race = new FormKey(Origin, 0x000801);
        var master = new SkyrimMod(Origin, SkyrimRelease.SkyrimSE);
        master.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Master;
        master.Races.Add(new Race(race, SkyrimRelease.SkyrimSE) { EditorID = "DsgsRace" });
        var npc = new Npc(new FormKey(Origin, 0x0009B8), SkyrimRelease.SkyrimSE) { EditorID = "DsgsBandit", Name = "Bandido dorado", Height = 1f };
        npc.Race.SetTo(race);
        master.Npcs.Add(npc);
        master.Armors.Add(new Armor(new FormKey(Origin, 0x000AE4), SkyrimRelease.SkyrimSE) { EditorID = "DsgsGoldenCuirass" });
        master.WriteToBinary(Path.Combine(_dir, Origin.FileName));

        // The override names the master (and the race it links) in lower case: its MAST entry is written so.
        var tweak = new SkyrimMod(Tweak, SkyrimRelease.SkyrimSE);
        var over = new Npc(new FormKey(OriginLower, 0x0009B8), SkyrimRelease.SkyrimSE) { EditorID = "DsgsBandit", Name = "Bandido dorado", Height = 1.2f };
        over.Race.SetTo(new FormKey(OriginLower, 0x000801));
        tweak.Npcs.Add(over);
        tweak.Armors.Add(new Armor(new FormKey(OriginLower, 0x000AE4), SkyrimRelease.SkyrimSE) { EditorID = "DsgsGoldenCuirass", Value = 9 });
        tweak.WriteToBinary(Path.Combine(_dir, Tweak.FileName));

        var state = new WorkerState();
        var response = state.Index(new IndexRequest
        {
            ProfileKey = "master-case",
            CacheDirectory = Path.Combine(_dir, "cache"),
            Plugins = new[] { Skyrim, Origin, Tweak }.Select(k => new LoadOrderEntry { FileName = k.FileName, Path = Path.Combine(_dir, k.FileName) }).ToArray(),
        }, _ => { }, CancellationToken.None);
        Assert.Equal(1, response.NpcCount);
        return state;
    }

    [Fact]
    public void SearchNpcs_OverrideSpellingItsMasterInLowerCase_IsOneRowWithTheOriginSpelling()
    {
        using var state = IndexTwoSpellings();
        var row = Assert.Single(state.Read(c => c.Queries.SearchNpcs(new NpcSearchRequest { Text = "DsgsBandit", Limit = 50 })).Items);
        Assert.Equal("0009B8:ccBGSSSE025-AdvDSGS.esm", row.FormKey);
        Assert.Equal("ccBGSSSE025-AdvDSGS.esm", row.OriginPlugin);
        Assert.Equal("LowerTweak.esp", row.WinningPlugin);
        Assert.Equal(2, row.OverrideCount);
    }

    [Theory]
    [InlineData("0009B8:ccBGSSSE025-AdvDSGS.esm")]
    [InlineData("0009B8:ccbgssse025-advdsgs.esm")]
    public void GetNpc_EitherSpelling_FindsBothVersionsAndOnlyTheRealChange(string formKey)
    {
        using var state = IndexTwoSpellings();
        var details = state.Read(c => c.Queries.GetNpc(formKey));
        Assert.NotNull(details);
        Assert.Equal("0009B8:ccBGSSSE025-AdvDSGS.esm", details!.Summary.FormKey);
        Assert.Equal(2, details.Overrides.Count);
        Assert.True(details.Overrides[0].IsOrigin);
        var winner = details.Overrides[1];
        Assert.True(winner.IsWinner);
        Assert.Equal("LowerTweak.esp", winner.Plugin);
        // Height changed; the race link only spells its master differently: not a change.
        Assert.Contains("Height", winner.ChangedFields);
        Assert.DoesNotContain("Race", winner.ChangedFields);
        // Linked records are named with the catalog's (origin's) spelling too, not the override's.
        Assert.Equal("000801:ccBGSSSE025-AdvDSGS.esm", details.Race?.FormKey);
        Assert.True(details.Race?.Resolved);
    }

    [Fact]
    public void SearchArmors_OverrideSpellingItsMasterInLowerCase_IsOneRowWithTheOriginSpelling()
    {
        using var state = IndexTwoSpellings();
        var row = Assert.Single(state.Read(c => c.Queries.SearchArmors(new ArmorSearchRequest { Text = "DsgsGoldenCuirass", Limit = 50 })).Items);
        Assert.Equal("000AE4:ccBGSSSE025-AdvDSGS.esm", row.FormKey);
        Assert.Equal("ccBGSSSE025-AdvDSGS.esm", row.OriginPlugin);
        Assert.Equal("LowerTweak.esp", row.WinningPlugin);
    }

    [Fact]
    public void Open_ACatalogOfTheOldSchema_IsRebuiltWithCaseInsensitiveKeys()
    {
        Directory.CreateDirectory(_dir);
        var db = Path.Combine(_dir, "old-catalog.sqlite");
        // A schema-3 cache as an older studio left it: binary FormKey columns.
        using (var old = new Microsoft.Data.Sqlite.SqliteConnection($"Data Source={db};Pooling=False"))
        {
            old.Open();
            using var cmd = old.CreateCommand();
            cmd.CommandText = """
                CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT); INSERT INTO meta VALUES ('schema', '3');
                CREATE TABLE npc_winner(form_key TEXT PRIMARY KEY, plugin_id INTEGER NOT NULL, load_index INTEGER NOT NULL,
                    winning_plugin TEXT NOT NULL, origin_plugin TEXT NOT NULL, override_count INTEGER NOT NULL) WITHOUT ROWID;
                INSERT INTO npc_winner VALUES ('0009B8:ccBGSSSE025-AdvDSGS.esm', 1, 8, 'a', 'a', 1), ('0009B8:ccbgssse025-advdsgs.esm', 2, 17, 'b', 'b', 1);
                """;
            cmd.ExecuteNonQuery();
        }

        using var store = Worker.NpcCatalog.CatalogStore.Open(db);
        using var check = store.Connection.CreateCommand();
        check.CommandText = "SELECT (SELECT value FROM meta WHERE key = 'schema'), (SELECT COUNT(*) FROM npc_winner), (SELECT sql FROM sqlite_master WHERE name = 'npc_winner')";
        using var r = check.ExecuteReader();
        Assert.True(r.Read());
        Assert.Equal(Worker.NpcCatalog.CatalogStore.SchemaVersion.ToString(System.Globalization.CultureInfo.InvariantCulture), r.GetString(0));
        Assert.Equal(0L, r.GetInt64(1));  // the old rows are gone: the next index rebuilds them
        Assert.Contains("COLLATE NOCASE", r.GetString(2), StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void SearchNpcs_PluginFilterWithTheOtherSpelling_FindsTheNpc()
    {
        using var state = IndexTwoSpellings();
        var items = state.Read(c => c.Queries.SearchNpcs(new NpcSearchRequest { Plugin = "ccbgssse025-advdsgs.esm", Limit = 50 })).Items;
        Assert.Contains(items, i => i.FormKey == "0009B8:ccBGSSSE025-AdvDSGS.esm");
    }

    [Fact]
    public void IsOfficial_CreationClubPluginInAnyLetterCase()
    {
        Assert.True(FaceRecipeQueries.IsOfficial(Origin));
        Assert.True(FaceRecipeQueries.IsOfficial(OriginLower));
        Assert.False(FaceRecipeQueries.IsOfficial(Tweak));
    }

    [Fact]
    public void SafePatchFingerprint_SameLinkInBothSpellings_IsEqual()
    {
        var upper = new FormLink<IRaceGetter>(new FormKey(Origin, 0x000801));
        var lower = new FormLink<IRaceGetter>(new FormKey(OriginLower, 0x000801));
        Assert.Equal(PatchBuilder.NpcSafePatch.Fingerprint(upper), PatchBuilder.NpcSafePatch.Fingerprint(lower));
        Assert.NotEqual(PatchBuilder.NpcSafePatch.Fingerprint(upper), PatchBuilder.NpcSafePatch.Fingerprint(new FormLink<IRaceGetter>(new FormKey(Origin, 0x000802))));
    }

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
