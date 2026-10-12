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
    public void DonorAppearance_ExtractsWinningFaceGenAndHeadParts()
    {
        var donor = _fixture.State.Read(c => AppearanceQueries.GetDonor(c.Session, c.Queries, new DonorAppearanceRequest
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
        }));

        Assert.Equal(SyntheticLoadOrder.Lydia.ToString(), donor.NpcFormKey);
        Assert.Equal("female", donor.Sex);
        Assert.Contains("Skyrim.esm\\000A2C8E.nif", donor.FaceGeometryPath);
        Assert.Contains("Skyrim.esm\\000A2C8E.dds", donor.FaceTintPath);
        Assert.NotEmpty(donor.HeadParts);
        Assert.Contains(donor.HeadParts, hp => hp.EditorId == "HairFemaleNord01");
    }

    [Fact]
    public void DonorAppearance_OverridesEffectiveAppearance()
    {
        var overrideData = new DonorAppearanceOverride(
            DonorNpcFormKey: "000800:FaceMod.esp",
            DonorFaceGeometryPath: "meshes\\actors\\character\\facegendata\\facegeom\\FaceMod.esp\\00000800.nif",
            DonorFaceTintPath: "textures\\actors\\character\\facegendata\\facetint\\FaceMod.esp\\00000800.dds",
            HeadParts: [new AppearanceHeadPart("CustomHair", "Hair", "000801:FaceMod.esp", "meshes\\hair.nif")],
            HairColor: new AppearanceColor(200, 50, 50),
            SkinTone: new AppearanceColor(220, 210, 200),
            Weight: 95.0f);

        var result = Get(new AppearanceRequest
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
            DonorOverride = overrideData,
        });

        Assert.Equal("meshes\\actors\\character\\facegendata\\facegeom\\FaceMod.esp\\00000800.nif", result.FaceGeometryPath);
        Assert.Equal("textures\\actors\\character\\facegendata\\facetint\\FaceMod.esp\\00000800.dds", result.FaceTintPath);
        Assert.Equal(95.0f, result.Weight);
        Assert.NotNull(result.HairColor);
        Assert.Equal(200, result.HairColor.R);
        Assert.Single(result.HeadParts);
        Assert.Equal("CustomHair", result.HeadParts[0].EditorId);
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

    private AppearanceResponse Get(AppearanceRequest request) => _fixture.State.Read(c => AppearanceQueries.Get(c.Session, c.Queries, request));
}
