using Mutagen.Bethesda.Plugins;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// NPC Health & Beauty Center, fixture 11: FaceGen files are found through the plugin that DEFINES the NPC and
/// its plugin-local FormID, never through the load-order index (which changes with every load order) nor the
/// plugin that wins the record. ESL-flagged plugins use the compact local ID (0x800-0xFFF) as is.
/// </summary>
public sealed class FaceGenPathTests
{
    [Fact]
    public void EslNpc_UsesDefiningPluginAndCompactLocalId()
    {
        var paths = CatalogQueries.FaceGenFor(FormKey.Factory("000801:MyFollower.esl"));
        Assert.Equal(@"meshes\actors\character\facegendata\facegeom\MyFollower.esl\00000801.nif", paths.GeometryPath);
        Assert.Equal(@"textures\actors\character\facegendata\facetint\MyFollower.esl\00000801.dds", paths.TintPath);
    }

    [Fact]
    public void FullPluginNpc_UsesSixHexDigitLocalIdPaddedToEight()
    {
        var paths = CatalogQueries.FaceGenFor(FormKey.Factory("0A2C8E:Skyrim.esm"));
        Assert.Equal(@"meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif", paths.GeometryPath);
        Assert.Equal(@"textures\actors\character\facegendata\facetint\Skyrim.esm\000A2C8E.dds", paths.TintPath);
    }

    [Fact]
    public void EslFlaggedEspNpc_KeepsTheEspFolderName()
    {
        // ESL-flagged .esp ("ESPFE"): the folder is still the plugin's file name, the ID its compact local ID.
        var paths = CatalogQueries.FaceGenFor(FormKey.Factory("000FFF:Light Patch.esp"));
        Assert.Equal(@"meshes\actors\character\facegendata\facegeom\Light Patch.esp\00000FFF.nif", paths.GeometryPath);
    }
}
