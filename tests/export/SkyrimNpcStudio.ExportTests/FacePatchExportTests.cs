using System.IO.Compression;
using System.Text.Json;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

public sealed class FacePatchExportTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public FacePatchExportTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-face-export-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "export-face-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private ISkyrimModDisposableGetter ReadBack(string path) =>
        SkyrimMod.CreateFromBinaryOverlay(
            new ModPath(ModKey.FromFileName(Path.GetFileName(path)), path),
            SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });

    [Fact]
    public void ExportFaceOverhaul_WritesPluginWithCustomizedFaceRecordAndCopiesFaceGen()
    {
        // Create fake donor FaceGen files in temp
        var donorNif = Path.Combine(_out, "donor_face.nif");
        var donorDds = Path.Combine(_out, "donor_face.dds");
        File.WriteAllBytes(donorNif, [0x4E, 0x49, 0x46, 0x00]); // fake NIF
        File.WriteAllBytes(donorDds, [0x44, 0x44, 0x53, 0x20]); // fake DDS

        var request = new ExportFaceOverhaulRequest
        {
            PluginName = "SkyrimNpcStudio_Lydia_Face.esp",
            OutputDirectory = _out,
            EslFlag = true,
            Description = "Lydia Face Overhaul",
            Assignments =
            [
                new FaceAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    DonorFaceGeometryPath = donorNif,
                    DonorFaceTintPath = donorDds,
                    SkinTone = new AppearanceColor(240, 200, 180),
                    FaceMorphs = new FaceMorphsDto(
                        NoseLongVsShort: 0.4f, NoseUpVsDown: 0.1f,
                        JawUpVsDown: -0.2f, JawNarrowVsWide: 0.3f, JawForwardVsBack: 0.0f,
                        CheeksUpVsDown: 0.2f, CheeksForwardVsBack: 0.1f,
                        EyesUpVsDown: 0.0f, EyesInVsOut: -0.1f, EyesForwardVsBack: 0.0f,
                        BrowsUpVsDown: 0.2f, BrowsInVsOut: 0.0f, BrowsForwardVsBack: -0.1f,
                        LipsUpVsDown: -0.2f, LipsInVsOut: 0.1f,
                        ChinNarrowVsWide: 0.2f, ChinUpVsDown: -0.1f, ChinUnderbiteVsOverbite: 0.0f),
                    Weight = 65f,
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        Assert.True(File.Exists(response.PluginPath));
        Assert.True(response.EslFlagged);

        // Verify copied FaceGen files inside mod directory
        var expectedFaceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);
        var targetNif = Path.Combine(response.ModDirectory, expectedFaceGen.GeometryPath);
        var targetDds = Path.Combine(response.ModDirectory, expectedFaceGen.TintPath);
        Assert.True(File.Exists(targetNif), $"Target FaceGen NIF must exist in staged mod at '{targetNif}'");
        Assert.True(File.Exists(targetDds), $"Target FaceGen DDS must exist in staged mod at '{targetDds}'");

        // Verify manifest
        var manifestText = File.ReadAllText(response.ManifestPath);
        using var manifest = JsonDocument.Parse(manifestText);
        Assert.Equal("face-overhaul", manifest.RootElement.GetProperty("mode").GetString());
        Assert.Equal("hybrid", manifest.RootElement.GetProperty("dependencyMode").GetString());
        Assert.True(manifest.RootElement.GetProperty("bundledAssets").GetArrayLength() >= 2);

        // Verify binary plugin contents
        using var mod = ReadBack(response.PluginPath);
        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal(SyntheticLoadOrder.Lydia, lydia.FormKey);
        Assert.Equal(65f, lydia.Weight);
        Assert.NotNull(lydia.TextureLighting);
        Assert.Equal(240, lydia.TextureLighting.Value.R);
        Assert.Equal(200, lydia.TextureLighting.Value.G);
        Assert.Equal(180, lydia.TextureLighting.Value.B);
        Assert.NotNull(lydia.FaceMorph);
        Assert.Equal(0.4f, lydia.FaceMorph.NoseLongVsShort);
        Assert.Equal(0.3f, lydia.FaceMorph.JawNarrowVsWide);
    }

    [Fact]
    public void ExportCompleteOverhaul_CombinesFaceAndOutfitInSingleMod()
    {
        var request = new ExportCompleteOverhaulRequest
        {
            PluginName = "SkyrimNpcStudio_Lydia_Complete.esp",
            OutputDirectory = _out,
            EslFlag = false,
            Description = "Lydia Complete Overhaul (Outfit + Face)",
            Assignments =
            [
                new CompleteNpcAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    Outfit = new OutfitAssignment
                    {
                        NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                        ArmorFormKeys =
                        [
                            _lo.CrimsonCuirass.ToString(),
                            SyntheticLoadOrder.SteelBoots.ToString(),
                        ],
                    },
                    Face = new FaceAssignment
                    {
                        NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                        SkinTone = new AppearanceColor(255, 220, 200),
                        Weight = 75f,
                    },
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportCompleteOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        Assert.True(File.Exists(response.PluginPath));
        var manifestText = File.ReadAllText(response.ManifestPath);
        using var manifest = JsonDocument.Parse(manifestText);
        Assert.Equal("complete-overhaul", manifest.RootElement.GetProperty("mode").GetString());

        using var mod = ReadBack(response.PluginPath);
        var outfit = Assert.Single(mod.Outfits);
        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal(outfit.FormKey, lydia.DefaultOutfit.FormKey);
        Assert.Equal(75f, lydia.Weight);
        Assert.NotNull(lydia.TextureLighting);
        Assert.Equal(255, lydia.TextureLighting.Value.R);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
