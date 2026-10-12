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
    private AppearanceResponse Get(AppearanceRequest request) => _fixture.State.Read(c => AppearanceQueries.Get(c.Session, c.Queries, request));
}
