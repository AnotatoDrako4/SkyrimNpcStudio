using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// An NPC dressed in meshes converted to another body family: new ARMA/ARMO for her race only, the base records and every
/// other NPC_ field untouched, read back from the written plugin.
/// </summary>
public sealed class ConvertedOutfitExportTests : IDisposable
{
    private const string Mesh = "!UBE\\Test\\Cuirass_1.nif";
    private static readonly string[] s_outfitAndItems = ["DefaultOutfit", "Items"];
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public ConvertedOutfitExportTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-converted-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "converted-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private ExportConvertedOutfitRequest Request(string mesh = Mesh, string? armor = null, string? npc = null) => new()
    {
        PluginName = "SNS_Converted.esp",
        OutputDirectory = _out,
        EslFlag = true,
        CreateZip = false,
        NpcFormKey = npc ?? SyntheticLoadOrder.Lydia.ToString(),
        Pieces = [new ConvertedOutfitPiece { BaseArmorFormKey = armor ?? _lo.CrimsonCuirass.ToString(), FemaleMesh = mesh }],
    };

    private ExportResponse Export(ExportConvertedOutfitRequest request) =>
        _state.Read(c => ModPackager.ExportConvertedOutfit(c.Session, c.Queries, request, CancellationToken.None));

    [Fact]
    public void New_addon_is_for_her_race_only_with_the_converted_mesh_and_the_override_only_changes_the_outfit()
    {
        var response = Export(Request());

        var key = ModKey.FromFileName("SNS_Converted.esp");
        using var patch = SkyrimMod.CreateFromBinaryOverlay(new ModPath(key, response.PluginPath), SkyrimRelease.SkyrimSE);
        var addon = Assert.Single(patch.ArmorAddons);
        var armor = Assert.Single(patch.Armors);
        var outfit = Assert.Single(patch.Outfits);
        var npc = Assert.Single(patch.Npcs);

        Assert.Equal(SyntheticLoadOrder.Lydia, npc.FormKey);
        Assert.Equal(outfit.FormKey, npc.DefaultOutfit.FormKeyNullable);
        Assert.Equal(armor.FormKey, Assert.Single(outfit.Items!).FormKey);
        Assert.Equal(addon.FormKey, Assert.Single(armor.Armature).FormKey);
        Assert.Equal(Mesh, addon.WorldModel?.Female?.File.GivenPath);
        Assert.Equal(npc.Race.FormKeyNullable, addon.Race.FormKeyNullable);
        Assert.Empty(addon.AdditionalRaces);
        Assert.True(patch.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
        Assert.All(new[] { addon.FormKey, armor.FormKey, outfit.FormKey }, k => Assert.Equal(key, k.ModKey));
        // The base armor is not overridden, so no other actor changes.
        Assert.DoesNotContain(patch.Armors, a => a.FormKey == _lo.CrimsonCuirass);

        var diff = Assert.Single(response.SemanticDiff);
        Assert.Equal("DefaultOutfit", Assert.Single(diff.Changes).Field);
    }

    [Fact]
    public void AlsoAddToInventory_lists_the_pieces_in_the_override_and_keeps_her_items()
    {
        var before = _state.Read(c => c.Session.LinkCache.Resolve<INpcGetter>(SyntheticLoadOrder.Lydia).Items?.Count ?? 0);
        var response = _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, new ExportOutfitPatchRequest
        {
            PluginName = "SNS_Inventory.esp",
            OutputDirectory = _out,
            EslFlag = true,
            CreateZip = false,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [_lo.CrimsonCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
                    AlsoAddToInventory = true,
                },
            ],
        }, CancellationToken.None));

        var key = ModKey.FromFileName("SNS_Inventory.esp");
        using var patch = SkyrimMod.CreateFromBinaryOverlay(new ModPath(key, response.PluginPath), SkyrimRelease.SkyrimSE);
        var npc = Assert.Single(patch.Npcs);
        var items = npc.Items!.Select(e => e.Item.Item.FormKey).ToArray();
        Assert.Contains(_lo.CrimsonCuirass, items);
        Assert.Contains(SyntheticLoadOrder.SteelBoots, items);
        Assert.True(items.Length >= before);
        Assert.Equal(items.Length, items.Distinct().Count());
        // Only the two fields asked for changed: Safe Patch read the plugin back.
        var changes = Assert.Single(response.SemanticDiff).Changes.Select(c => c.Field).OrderBy(f => f, StringComparer.Ordinal).ToArray();
        Assert.True(changes.SequenceEqual(s_outfitAndItems), string.Join(",", changes));
    }

    [Theory]
    [InlineData("C:\\Skyrim\\x.nif")]
    [InlineData("meshes\\x.nif")]
    [InlineData("..\\x.nif")]
    [InlineData("x.dds")]
    public void Bad_mesh_paths_are_refused_and_nothing_is_written(string mesh)
    {
        var e = Assert.Throws<WorkerException>(() => Export(Request(mesh)));
        Assert.Equal(ErrorCodes.ValidationFailed, e.Code);
        Assert.False(Directory.Exists(Path.Combine(_out, "SNS_Converted")));
    }

    [Fact]
    public void An_armor_outside_the_load_order_is_refused()
    {
        var e = Assert.Throws<WorkerException>(() => Export(Request(armor: "ABCDEF:Skyrim.esm")));
        Assert.Equal(ErrorCodes.ValidationFailed, e.Code);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, true); } catch (IOException) { }
    }
}
