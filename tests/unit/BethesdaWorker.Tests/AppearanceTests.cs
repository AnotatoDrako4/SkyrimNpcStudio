using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

public sealed class AppearanceTests : IClassFixture<CatalogFixture>
{
    private readonly CatalogFixture _fixture;
    public AppearanceTests(CatalogFixture fixture) => _fixture = fixture;

    [Fact]
    public void EffectiveAppearance_SelectsFemaleRaceAddonAndOriginFaceGen()
    {
        var result = Get(new AppearanceRequest { NpcFormKey = SyntheticLoadOrder.Lydia.ToString() });
        Assert.Equal("female", result.Sex);
        Assert.Equal(80, result.Weight);
        Assert.Equal(2, result.Pieces.Count);
        Assert.All(result.Pieces, p => Assert.All(p.Addons, a => Assert.StartsWith("meshes\\", a.MeshPath)));
        Assert.Contains("Skyrim.esm\\000A2C8E.nif", result.FaceGeometryPath);
    }

    [Fact]
    public void DraftAppearance_UsesExplicitArmorSelectionIncludingEmptyOutfit()
    {
        var result = Get(new AppearanceRequest { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), ArmorFormKeys = [SyntheticLoadOrder.ElvenCuirass.ToString()] });
        Assert.Single(result.Pieces);
        Assert.Equal(SyntheticLoadOrder.ElvenCuirass.ToString(), result.Pieces[0].ArmorFormKey);
        Assert.Empty(Get(new AppearanceRequest { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), ArmorFormKeys = [] }).Pieces);
    }

    [Fact]
    public void UnresolvedNpcFailsExplicitly()
    {
        var ex = Assert.Throws<WorkerException>(() => Get(new AppearanceRequest { NpcFormKey = "FFFFFF:Skyrim.esm" }));
        Assert.Equal(ErrorCodes.RecordNotFound, ex.Code);
    }

    // Since f5b87b0 a Traits template that resolves to an NPC_ is followed (it used to be refused outright): the guard takes
    // race and sex from GuardTemplate, and its outfit from the template because the guard has none of its own.
    [Fact]
    public void ValidTraitsTemplate_ResolvesAppearanceFromTemplate()
    {
        var result = Get(new AppearanceRequest { NpcFormKey = SyntheticLoadOrder.TemplatedGuard.ToString() });
        Assert.Equal(SyntheticLoadOrder.TemplatedGuard.ToString(), result.NpcFormKey);
        Assert.Equal("male", result.Sex);
        Assert.Contains(result.Pieces, p => p.ArmorFormKey == SyntheticLoadOrder.SteelCuirass.ToString() && p.Source == "outfit");
        Assert.Contains(result.Pieces, p => p.ArmorFormKey == SyntheticLoadOrder.ElvenCuirass.ToString() && p.Source == "outfit");
    }

    private static readonly ModKey TemplateMod = ModKey.FromFileName("TemplateChain.esp");

    private static Npc TraitsNpc(uint id, FormKey? template, bool female = false)
    {
        var npc = new Npc(new FormKey(TemplateMod, id), SkyrimRelease.SkyrimSE);
        if (template is { } t) npc.Template.SetTo(t);
        npc.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits;
        if (female) npc.Configuration.Flags |= NpcConfiguration.Flag.Female;
        return npc;
    }

    private static INpcGetter Resolve(INpcGetter start, params Npc[] records)
    {
        var byKey = records.ToDictionary(n => n.FormKey, n => (INpcGetter)n);
        return AppearanceQueries.ResolveTraitsSource(start, k => byKey.GetValueOrDefault(k), _ => false);
    }

    [Fact]
    public void TraitsChain_FollowsEveryLinkToTheLastNpc()
    {
        var root = TraitsNpc(0x900, null, female: true);
        root.Configuration.TemplateFlags = 0;  // root has its own traits: the chain stops here
        var middle = TraitsNpc(0x901, root.FormKey);
        var leaf = TraitsNpc(0x902, middle.FormKey);
        Assert.Same(root, Resolve(leaf, root, middle));
    }

    [Fact]
    public void TraitsFlagWithoutTemplate_KeepsTheNpcItself()
    {
        var npc = TraitsNpc(0x903, null);
        Assert.Same(npc, Resolve(npc));
    }

    [Fact]
    public void TraitsTemplate_MissingRecordFailsInsteadOfFallingBackToOwnAppearance()
    {
        var npc = TraitsNpc(0x904, new FormKey(TemplateMod, 0xFFF));
        var ex = Assert.Throws<WorkerException>(() => Resolve(npc));
        Assert.Equal(ErrorCodes.RecordNotFound, ex.Code);
        Assert.Contains("not in the active load order", ex.Message);
    }

    [Fact]
    public void TraitsTemplate_LeveledListFailsWithItsOwnReason()
    {
        var lvln = new FormKey(TemplateMod, 0x9F0);
        var npc = TraitsNpc(0x905, lvln);
        var ex = Assert.Throws<WorkerException>(() =>
            AppearanceQueries.ResolveTraitsSource(npc, _ => null, k => k == lvln));
        Assert.Equal(ErrorCodes.ValidationFailed, ex.Code);
        Assert.Contains("leveled NPC list", ex.Message);
    }

    [Fact]
    public void TraitsTemplate_CycleFailsExplicitly()
    {
        var a = TraitsNpc(0x906, new FormKey(TemplateMod, 0x907));
        var b = TraitsNpc(0x907, a.FormKey);
        var ex = Assert.Throws<WorkerException>(() => Resolve(a, a, b));
        Assert.Equal(ErrorCodes.ValidationFailed, ex.Code);
        Assert.Contains("loops back", ex.Message);

        var self = TraitsNpc(0x908, new FormKey(TemplateMod, 0x908));
        Assert.Contains("loops back", Assert.Throws<WorkerException>(() => Resolve(self, self)).Message);
    }

    [Fact]
    public void TraitsTemplate_ChainLongerThanTheLimitFails()
    {
        var chain = new List<Npc> { TraitsNpc(0xA00, null) };
        chain[0].Configuration.TemplateFlags = 0;
        for (uint i = 1; i <= AppearanceQueries.MaxTemplateDepth + 1; i++) chain.Add(TraitsNpc(0xA00 + i, chain[^1].FormKey));
        var ex = Assert.Throws<WorkerException>(() => Resolve(chain[^1], [.. chain]));
        Assert.Contains("longer than", ex.Message);
    }

    [Fact]
    public void FaceRecipe_ResolvesHeadPartsAndRace()
    {
        var recipe = _fixture.State.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries, new FaceRecipeRequest
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
        }));

        Assert.Equal(SyntheticLoadOrder.Lydia.ToString(), recipe.NpcFormKey);
        Assert.True(recipe.Female);
        Assert.Contains("Skyrim.esm\\000A2C8E.nif", recipe.FaceGenMeshPath);
        Assert.Contains("Skyrim.esm\\000A2C8E.dds", recipe.FaceGenTintPath);
        Assert.NotEmpty(recipe.HeadParts);
        Assert.Contains(recipe.HeadParts, hp => hp.EditorId == "HairFemaleNord01");
        Assert.Equal("NordRace", recipe.Race.EditorId);
    }

    [Fact]
    public void FaceRecipe_HeadPartOverride_ReplacesEffectiveHeadParts()
    {
        var recipe = _fixture.State.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries, new FaceRecipeRequest
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
            HeadPartFormKeys = [SyntheticLoadOrder.FemaleHeadNord.ToString()],
        }));

        Assert.Equal([SyntheticLoadOrder.FemaleHeadNord.ToString()], recipe.NpcHeadParts);
        Assert.Contains(recipe.HeadParts, hp => hp.EditorId == "FemaleHeadNord");
    }

    [Fact]
    public void FaceRecipe_UsesRaceDefaultsExtraPartsAndRecordFaceValues()
    {
        var recipe = _fixture.State.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries, new FaceRecipeRequest
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
            SourcePlugin = "Skyrim.esm",
            OfficialOnly = true,
        }));

        Assert.Equal("Skyrim.esm", recipe.SourcePlugin);
        Assert.Equal(50f, recipe.Values.Weight);
        Assert.Equal([16, -1, 9, 16], recipe.Values.Presets);
        Assert.Equal(0.1f, recipe.Values.Sliders[0]);
        Assert.Equal(-0.3f, recipe.Values.Sliders[2]);
        Assert.Equal(18, recipe.Values.Sliders.Count);
        Assert.Equal(4, recipe.HeadParts.Count);
        Assert.Contains(recipe.HeadParts, p => p.FormKey == SyntheticLoadOrder.FemaleHeadNord.ToString()
            && p.Tris.Count == 3 && p.ValidSex == "female");
        Assert.Contains(recipe.HeadParts, p => p.FormKey == SyntheticLoadOrder.HairLineNord.ToString() && p.Extra);
        Assert.Contains(recipe.TintMasks, mask => mask.Index == 24 && mask.MaskType == 6);
        Assert.Contains(recipe.Values.Tints, tint => tint.Index == 31 && Math.Abs(tint.Value - 0.61f) < 0.0001f);
        Assert.Equal(SyntheticLoadOrder.SkinHeadFemaleNord.ToString(), recipe.FaceTextureSetFormKey);
        Assert.Equal(206, recipe.Values.SkinTone.R);
        Assert.NotNull(recipe.Values.HairColor);
        Assert.Equal((byte)26, recipe.Values.HairColor.R);
    }

    [Fact]
    public void FaceRecipe_VampireRaceUsesMorphRaceAndVampireMorph()
    {
        var recipe = _fixture.State.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries, new FaceRecipeRequest
        {
            NpcFormKey = SyntheticLoadOrder.VampireNpc.ToString(),
            OfficialOnly = true,
        }));

        Assert.Equal("NordRaceVampire", recipe.Race.EditorId);
        Assert.Equal("NordRace", recipe.Race.MorphRaceEditorId);
        Assert.True(recipe.Race.Vampire);
        Assert.Equal(20f, recipe.Values.Weight);
    }

    [Fact]
    public void HeadParts_SearchableFromCatalog()
    {
        var search = _fixture.State.Read(c => c.Queries.SearchHeadParts(new SearchHeadPartsRequest
        {
            Type = "Hair",
        }));

        Assert.True(search.Total >= 1);
        Assert.Contains(search.Items, hp => hp.EditorId == "HairFemaleNord01");
    }

    [Fact]
    public void VampireNpc_EquippingDefaultRaceArmor_ResolvesArmatureAndAddons()
    {
        var result = Get(new AppearanceRequest
        {
            NpcFormKey = SyntheticLoadOrder.VampireNpc.ToString(),
            ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString()]
        });
        Assert.Single(result.Pieces);
        Assert.Single(result.Pieces[0].Addons);
        Assert.StartsWith("meshes\\", result.Pieces[0].Addons[0].MeshPath);
    }

    private AppearanceResponse Get(AppearanceRequest request) => _fixture.State.Read(c => AppearanceQueries.Get(c.Session, c.Queries, request));
}
