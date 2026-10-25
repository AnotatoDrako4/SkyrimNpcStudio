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
    public void UnresolvedAndTemplatedAppearancesFailExplicitly()
    {
        Assert.Throws<WorkerException>(() => Get(new AppearanceRequest { NpcFormKey = "FFFFFF:Skyrim.esm" }));
        Assert.Throws<WorkerException>(() => Get(new AppearanceRequest { NpcFormKey = SyntheticLoadOrder.TemplatedGuard.ToString() }));
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
