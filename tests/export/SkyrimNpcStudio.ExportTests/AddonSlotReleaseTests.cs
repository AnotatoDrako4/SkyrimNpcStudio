using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// A corset whose addon also claims the pants' slot: the outfit gets a copy without that slot, the pants stay as they are and the
/// base records are not overridden. Read back from the written plugin.
/// </summary>
public sealed class AddonSlotReleaseTests : IDisposable
{
    private const BipedObjectFlag Pants = (BipedObjectFlag)(1u << (53 - 30));
    private const string CorsetMesh = "!UBE\\Test\\Corset_1.nif";
    private static readonly ModKey Layers = ModKey.FromFileName("Layers.esp");
    private static readonly ModKey Written = ModKey.FromFileName("SNS_Released.esp");
    private static readonly string[] s_outfitAndItems = ["DefaultOutfit", "Items"];
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;
    private readonly FormKey _corset;
    private readonly FormKey _pants;
    private readonly FormKey _claimingAddon;
    private readonly FormKey _otherAddon;

    public AddonSlotReleaseTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-release-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);

        var mod = new SkyrimMod(Layers, SkyrimRelease.SkyrimSE);
        var claiming = Addon(mod, "LayerCorsetAA_UBE", BipedObjectFlag.Body | Pants, CorsetMesh);
        var other = Addon(mod, "LayerCorsetAA", BipedObjectFlag.Body, "Layer\\Corset_1.nif");
        _claimingAddon = claiming.FormKey;
        _otherAddon = other.FormKey;
        _corset = Armor(mod, "LayerCorset", BipedObjectFlag.Body, claiming, other);
        _pants = Armor(mod, "LayerPants", Pants, Addon(mod, "LayerPantsAA", Pants, "Layer\\Pants_1.nif"));
        var path = Path.Combine(_lo.DataDirectory, Layers.FileName);
        mod.WriteToBinary(path);

        _state.Index(new IndexRequest
        {
            ProfileKey = "release-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path })
                .Append(new LoadOrderEntry { FileName = Layers.FileName, Path = path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private static ArmorAddon Addon(SkyrimMod mod, string editorId, BipedObjectFlag slots, string female)
    {
        var addon = mod.ArmorAddons.AddNew(editorId);
        addon.BodyTemplate = new BodyTemplate { FirstPersonFlags = slots };
        addon.Race.SetTo(SyntheticLoadOrder.DefaultRace);
        addon.WorldModel = new GenderedItem<Model?>(null, new Model { File = female });
        return addon;
    }

    private static FormKey Armor(SkyrimMod mod, string editorId, BipedObjectFlag slots, params ArmorAddon[] addons)
    {
        var armor = mod.Armors.AddNew(editorId);
        armor.Name = editorId;
        armor.BodyTemplate = new BodyTemplate { FirstPersonFlags = slots, ArmorType = ArmorType.Clothing };
        foreach (var addon in addons) armor.Armature.Add(addon);
        return armor.FormKey;
    }

    private ExportResponse Export(string armor, uint mask) =>
        _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, new ExportOutfitPatchRequest
        {
            PluginName = Written.FileName,
            OutputDirectory = _out,
            EslFlag = true,
            CreateZip = false,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [_pants.ToString(), _corset.ToString()],
                    AlsoAddToInventory = true,
                    AddonSlotReleases = [new AddonSlotRelease { ArmorFormKey = armor, SlotMask = mask }],
                },
            ],
        }, CancellationToken.None));

    [Fact]
    public void The_outfit_gets_a_copy_of_the_corset_without_the_slot_and_the_base_records_stay()
    {
        var response = Export(_corset.ToString(), (uint)Pants);

        using var patch = SkyrimMod.CreateFromBinaryOverlay(new ModPath(Written, response.PluginPath), SkyrimRelease.SkyrimSE);
        var copy = Assert.Single(patch.Armors);
        var freed = Assert.Single(patch.ArmorAddons);

        // Same mesh and the same body slot: only the claim on the pants' slot is gone.
        Assert.Equal(BipedObjectFlag.Body, freed.BodyTemplate!.FirstPersonFlags);
        Assert.Equal(CorsetMesh, freed.WorldModel?.Female?.File.GivenPath);
        Assert.Equal(new[] { freed.FormKey, _otherAddon }, copy.Armature.Select(a => a.FormKey));

        // The base corset and its addon are not overridden, so no other actor changes.
        Assert.All(new[] { copy.FormKey, freed.FormKey }, k => Assert.Equal(Written, k.ModKey));
        Assert.DoesNotContain(patch.ArmorAddons, a => a.FormKey == _claimingAddon);

        // In the outfit and in the inventory the copy replaces the base corset; the pants are the original ones.
        var outfit = Assert.Single(patch.Outfits);
        Assert.Equal(new[] { _pants, copy.FormKey }, outfit.Items!.Select(i => i.FormKey));
        var items = Assert.Single(patch.Npcs).Items!.Select(e => e.Item.Item.FormKey).ToArray();
        Assert.Contains(copy.FormKey, items);
        Assert.Contains(_pants, items);
        Assert.DoesNotContain(_corset, items);

        var changes = Assert.Single(response.SemanticDiff).Changes.Select(c => c.Field).OrderBy(f => f, StringComparer.Ordinal).ToArray();
        Assert.True(changes.SequenceEqual(s_outfitAndItems), string.Join(",", changes));
    }

    [Theory]
    [InlineData("corset", 1u << (32 - 30), "ReleaseOfOwnSlot")]
    [InlineData("corset", 1u << (58 - 30), "ReleaseWithoutEffect")]
    [InlineData("other", 1u << (53 - 30), "ReleaseNotInOutfit")]
    public void A_release_that_cannot_do_what_it_says_is_refused_and_nothing_is_written(string target, uint mask, string code)
    {
        var armor = target == "corset" ? _corset.ToString() : _lo.CrimsonCuirass.ToString();

        var e = Assert.Throws<WorkerException>(() => Export(armor, mask));

        Assert.Equal(ErrorCodes.ValidationFailed, e.Code);
        Assert.Contains(e.Issues!, i => i.Code == code);
        Assert.False(File.Exists(Path.Combine(_out, "SNS_Released", Written.FileName)));
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, true); } catch (IOException) { }
    }
}
