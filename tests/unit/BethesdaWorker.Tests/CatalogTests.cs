using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

public sealed class CatalogTests(CatalogFixture fixture) : IClassFixture<CatalogFixture>
{
    private static readonly string LydiaKey = SyntheticLoadOrder.Lydia.ToString();

    [Fact]
    public void Fingerprint_DistinguishesColors()
    {
        // Skin tone (QNAM) and tint colors (TINC) are System.Drawing.Color, not Mutagen getters.
        var a = System.Drawing.Color.FromArgb(255, 206, 205, 204);
        var b = System.Drawing.Color.FromArgb(255, 180, 150, 140);
        Assert.NotEqual(NpcCatalog.Fingerprint.Of(a), NpcCatalog.Fingerprint.Of(b));
        Assert.Equal(NpcCatalog.Fingerprint.Of(a), NpcCatalog.Fingerprint.Of(System.Drawing.Color.FromArgb(255, 206, 205, 204)));
        Assert.NotEqual(NpcCatalog.Fingerprint.Of((System.Drawing.Color?)a), NpcCatalog.Fingerprint.Of((System.Drawing.Color?)null));
        var tint = new Mutagen.Bethesda.Skyrim.TintLayer { Index = 24, Color = a, InterpolationValue = 1f };
        var other = new Mutagen.Bethesda.Skyrim.TintLayer { Index = 24, Color = b, InterpolationValue = 1f };
        Assert.NotEqual(NpcCatalog.Fingerprint.Of(new[] { tint }), NpcCatalog.Fingerprint.Of(new[] { other }));
    }

    [Fact]
    public void Index_ReportsCountsAndNoErrors()
    {
        var r = fixture.IndexResponse;
        Assert.Equal(4, r.PluginCount);
        Assert.Equal(4, r.NpcCount);          // Lydia, Hert (vampire), guard template, templated guard
        Assert.Equal(7, r.ArmorCount);        // 5 vanilla-like + 2 from the ESL pack
        Assert.Equal(2, r.OutfitCount);       // HousecarlOutfit, GuardConflictOutfit
        Assert.DoesNotContain(r.Issues, i => i.Severity == "error");
    }

    /// <summary>
    /// Audit v2 F-20: the studio rebuilt the FaceGen file names from the FormKey with its own copy of
    /// FaceGenFor's rule. Every search result now carries the paths, computed here once.
    /// </summary>
    [Fact]
    public void SearchResults_CarryTheFaceGenPathsOfTheOriginPlugin()
    {
        var items = fixture.State.Read(c => c.Queries.SearchNpcs(new NpcSearchRequest { Limit = 200 })).Items;
        Assert.NotEmpty(items);
        foreach (var item in items)
        {
            Assert.Equal(NpcCatalog.CatalogQueries.FaceGenFor(Mutagen.Bethesda.Plugins.FormKey.Factory(item.FormKey)), item.FaceGen);
        }

        var lydia = Assert.Single(items, i => i.FormKey == LydiaKey);
        Assert.Equal(@"meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif", lydia.FaceGen!.GeometryPath);
        Assert.Equal(@"textures\actors\character\facegendata\facetint\Skyrim.esm\000A2C8E.dds", lydia.FaceGen.TintPath);
    }

    [Theory]
    [InlineData("lydia")]
    [InlineData("HousecarlWhiterun")]
    [InlineData("0A2C8E:Skyrim.esm")]
    [InlineData("000A2C8E")]                  // xEdit load-order FormID, Skyrim.esm is index 00
    public void SearchNpcs_FindsLydiaByNameEditorIdFormKeyOrFormId(string text)
    {
        var result = fixture.State.Read(c => c.Queries.SearchNpcs(new NpcSearchRequest { Text = text }));
        var lydia = Assert.Single(result.Items);
        Assert.Equal(LydiaKey, lydia.FormKey);
        Assert.Equal("000A2C8E", lydia.FormId);
        Assert.Equal("female", lydia.Sex);
        Assert.Equal("NordRace", lydia.RaceEditorId);
        Assert.Equal("Skyrim.esm", lydia.OriginPlugin);
        Assert.Equal("LateTweak.esp", lydia.WinningPlugin);
        Assert.Equal(3, lydia.OverrideCount);
    }

    [Fact]
    public void SearchNpcs_FiltersBySexKindPluginAndFaction()
    {
        NpcSearchResponse Search(NpcSearchRequest r) => fixture.State.Read(c => c.Queries.SearchNpcs(r));

        Assert.Equal(2, Search(new() { Sex = "female" }).Total);
        Assert.Equal(2, Search(new() { Sex = "male" }).Total);
        Assert.Equal(1, Search(new() { Kind = "templated" }).Total);
        Assert.Equal(1, Search(new() { Kind = "unique" }).Total);
        Assert.Equal(1, Search(new() { Plugin = "FaceMod.esp" }).Total);
        Assert.Equal(3, Search(new() { Race = "NordRace" }).Total);
        Assert.Equal(1, Search(new() { Faction = "HousecarlWhiterunFaction" }).Total);
        Assert.Throws<ArgumentException>(() => Search(new() { Sex = "other" }));
    }

    [Fact]
    public void SearchNpcs_ByFormKeys_ReturnsExactlyThoseNpcs_IgnoresCaseAndCannotInjectSql()
    {
        NpcSearchResponse Search(NpcSearchRequest r) => fixture.State.Read(c => c.Queries.SearchNpcs(r));

        Assert.Equal(LydiaKey, Assert.Single(Search(new() { FormKeys = [LydiaKey] }).Items).FormKey);
        var lower = Search(new() { FormKeys = [LydiaKey.ToLowerInvariant(), "FFFFFF:Nothing.esm"] });
        Assert.Equal(LydiaKey, Assert.Single(lower.Items).FormKey);
        Assert.Equal(1, lower.Total);
        Assert.Empty(Search(new() { FormKeys = ["x') OR 1=1 --"] }).Items);

        var some = Search(new() { Limit = 200 }).Items.Take(2).Select(i => i.FormKey).ToList();
        Assert.Equal(2, Search(new() { FormKeys = some }).Total);

        // It narrows like any other filter, it does not replace them.
        Assert.Empty(Search(new() { FormKeys = [LydiaKey], Sex = "male" }).Items);
        // Without keys nothing changes.
        Assert.Equal(4, Search(new() { FormKeys = [] }).Total);
    }

    [Fact]
    public void GetNpc_ShowsOverrideChainAndTheLostHeightEdit()
    {
        var npc = fixture.State.Read(c => c.Queries.GetNpc(LydiaKey))!;

        Assert.Equal(["Skyrim.esm", "FaceMod.esp", "LateTweak.esp"], npc.Overrides.Select(o => o.Plugin));
        Assert.True(npc.Overrides[0].IsOrigin);
        Assert.True(npc.Overrides[^1].IsWinner);
        Assert.Equal(["Height"], npc.Overrides[1].ChangedFields);
        Assert.Contains("Weight", npc.Overrides[2].ChangedFields);

        // LateTweak was made from the original record: FaceMod's height change does not survive.
        // Its height is the origin's value again, so it is a copy of Skyrim.esm's; its weight is its own.
        Assert.Equal("Skyrim.esm", npc.Overrides[2].CopiedFrom["Height"]);
        Assert.False(npc.Overrides[2].CopiedFrom.ContainsKey("Weight"));
        Assert.Empty(npc.Overrides[1].CopiedFrom);
        var conflict = Assert.Single(npc.Conflicts);
        Assert.Equal("Height", conflict.Field);
        Assert.Equal(["FaceMod.esp"], conflict.LosingPlugins);

        Assert.Equal(80f, npc.Weight);
        Assert.Equal(1.0f, npc.Height);
        Assert.Equal("HousecarlOutfit", npc.DefaultOutfit!.Outfit.EditorId);
        Assert.Equal(["Steel Armor", "Steel Boots"], npc.DefaultOutfit.Armors.Select(a => a.Name));
        Assert.Equal(@"meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif", npc.FaceGen.GeometryPath);
        Assert.Equal("Hair", Assert.Single(npc.HeadParts).PartType);
        Assert.Contains("Female", npc.Flags);

        var loc = Assert.Single(npc.Locations);
        Assert.Equal(SyntheticLoadOrder.LydiaRef.ToString(), loc.FormKey);
        Assert.Equal("WhiterunDragonsreach", loc.CellEditorId);
        Assert.Equal("Dragonsreach", loc.CellName);
        Assert.Equal("Skyrim.esm", loc.Plugin);
    }

    [Fact]
    public void GetNpc_TemplatedGuard_WarnsThatOutfitComesFromTemplate()
    {
        var npc = fixture.State.Read(c => c.Queries.GetNpc(SyntheticLoadOrder.TemplatedGuard.ToString()))!;
        Assert.Contains(npc.Notes, n => n.Code == "OutfitFromTemplate");
        Assert.Contains(npc.Notes, n => n.Code == "AppearanceFromTemplate");
        Assert.Contains("Inventory", npc.TemplateFlags);
    }

    [Fact]
    public void SearchArmors_BySlotAndLightPluginFormIds()
    {
        var body = fixture.State.Read(c => c.Queries.SearchArmors(new ArmorSearchRequest { SlotMask = 1u << (32 - 30) }));
        Assert.Equal(["Crimson Corset", "Elven Armor", "Steel Armor"], body.Items.Select(i => i.Name));

        var crimson = body.Items.Single(i => i.Name == "Crimson Corset");
        Assert.Equal("ArmorPack.esp", crimson.OriginPlugin);
        Assert.StartsWith("FE000", crimson.FormId, StringComparison.Ordinal); // first light plugin
        Assert.Equal([32], crimson.Slots);

        var cloak = fixture.State.Read(c => c.Queries.SearchArmors(new ArmorSearchRequest { Text = "cloak" }));
        Assert.Equal([46], Assert.Single(cloak.Items).Slots);

        // Audit & Catalog fix: Armors without ARMO.WorldModel resolve wearable models from winning ARMA addons
        Assert.NotNull(crimson.FemaleModel);
        Assert.Contains("Crimson", crimson.FemaleModel);
        var steel = body.Items.Single(i => i.Name == "Steel Armor");
        Assert.NotNull(steel.MaleModel);
        Assert.Contains("Steel", steel.MaleModel);
    }

    [Fact]
    public void ListPlugins_ReportsPrefixesAndMasters()
    {
        var plugins = fixture.State.Read(c => c.Queries.ListPlugins()).Plugins;
        Assert.Equal(["00", "01", "FE000", "02"], plugins.Select(p => p.FormIdPrefix));
        Assert.True(plugins[0].IsMaster);
        Assert.True(plugins[2].IsLight);
        Assert.Equal(["Skyrim.esm"], plugins[3].Masters);
    }
}

public sealed class IncrementalIndexTests : IDisposable
{
    private readonly CatalogFixture _fixture = new();

    [Fact]
    public void Reindex_ReusesCacheAndOnlyReparsesChangedPlugins()
    {
        Assert.Equal(4, _fixture.IndexResponse.ParsedPlugins);

        var again = _fixture.State.Index(_fixture.Request(), _ => { }, CancellationToken.None);
        Assert.Equal(0, again.ParsedPlugins);
        Assert.Equal(4, again.CachedPlugins);

        File.SetLastWriteTimeUtc(_fixture.LoadOrder.PathOf(SyntheticLoadOrder.LateTweak), DateTime.UtcNow.AddMinutes(1));
        var touched = _fixture.State.Index(_fixture.Request(), _ => { }, CancellationToken.None);
        Assert.Equal(1, touched.ParsedPlugins);
        Assert.Equal(4, touched.NpcCount);
    }

    [Fact]
    public void Reindex_WithoutLateTweak_FaceModBecomesWinner()
    {
        var request = _fixture.Request();
        request = request with { Plugins = request.Plugins.Where(p => p.FileName != "LateTweak.esp").ToArray() };
        _fixture.State.Index(request, _ => { }, CancellationToken.None);

        var npc = _fixture.State.Read(c => c.Queries.GetNpc(SyntheticLoadOrder.Lydia.ToString()))!;
        Assert.Equal("FaceMod.esp", npc.Summary.WinningPlugin);
        Assert.Equal(1.05f, npc.Height);
        Assert.Empty(npc.Conflicts);
    }

    [Fact]
    public void Index_ReportsMissingPluginsAndMasters()
    {
        var request = _fixture.Request();
        request = request with
        {
            Plugins = request.Plugins.Where(p => p.FileName != "Skyrim.esm")
                .Prepend(new LoadOrderEntry { FileName = "Skyrim.esm", Path = Path.Combine(_fixture.LoadOrder.DataDirectory, "missing.esm") })
                .ToArray(),
        };
        var response = _fixture.State.Index(request, _ => { }, CancellationToken.None);
        Assert.Contains(response.Issues, i => i.Code == "PluginNotFound" && i.Subject == "Skyrim.esm");
        Assert.Contains(response.Issues, i => i.Code == "MissingMaster" && i.Subject == "FaceMod.esp");
    }

    [Fact]
    public void Read_BeforeIndex_FailsWithNotIndexed()
    {
        using var empty = new WorkerState();
        var ex = Assert.Throws<WorkerException>(() => empty.Read(c => c.Queries.ListPlugins()));
        Assert.Equal(ErrorCodes.NotIndexed, ex.Code);
    }

    public void Dispose() => _fixture.Dispose();
}
