using System.IO.Compression;
using System.Security.Cryptography;
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

    private static string Sha256Of(string path) => Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));

    [Fact]
    public void ExportFaceOverhaul_WritesPluginWithCustomizedFaceRecordAndCopiesFaceGen()
    {
        // Create fake donor FaceGen files in temp
        var donorNif = Path.Combine(_out, "donor_face.nif");
        var donorDds = Path.Combine(_out, "donor_face.dds");
        File.WriteAllBytes(donorNif, [0x4E, 0x49, 0x46, 0x00]); // fake NIF
        File.WriteAllBytes(donorDds, [0x44, 0x44, 0x53, 0x20]); // fake DDS

        var expectedFaceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);

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
                    Values = new FaceValuesDto
                    {
                        Sliders = [0.4f, 0.1f, -0.2f, 0.3f, 0.0f, 0.2f, 0.1f, 0.0f, -0.1f, 0.0f, 0.2f, 0.0f, -0.1f, -0.2f, 0.1f, 0.2f, -0.1f, 0.0f],
                        Presets = [0, 0, 0, 0],
                        Weight = 65f,
                        SkinTone = new AppearanceColor(240, 200, 180),
                        Tints = [],
                    },
                    HeadParts = [SyntheticLoadOrder.FemaleHeadNord.ToString(), SyntheticLoadOrder.HairNord.ToString()],
                    FaceGen = new FaceGenProvenance
                    {
                        Mode = "regenerate",
                        Parts = ["FemaleHeadNord", "HairNord"],
                    },
                    GeneratedAssets =
                    [
                        new GeneratedAsset
                        {
                            SourcePath = donorNif,
                            DataPath = expectedFaceGen.GeometryPath,
                            Sha256 = Sha256Of(donorNif),
                            Kind = "facegen-geometry",
                        },
                        new GeneratedAsset
                        {
                            SourcePath = donorDds,
                            DataPath = expectedFaceGen.TintPath,
                            Sha256 = Sha256Of(donorDds),
                            Kind = "facegen-tint",
                        },
                    ],
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        Assert.True(File.Exists(response.PluginPath));
        Assert.True(response.EslFlagged);

        // Verify copied FaceGen files inside mod directory
        var targetNif = Path.Combine(response.ModDirectory, expectedFaceGen.GeometryPath);
        var targetDds = Path.Combine(response.ModDirectory, expectedFaceGen.TintPath);
        Assert.True(File.Exists(targetNif), $"Target FaceGen NIF must exist in staged mod at '{targetNif}'");
        Assert.True(File.Exists(targetDds), $"Target FaceGen DDS must exist in staged mod at '{targetDds}'");

        // Verify manifest
        var manifestText = File.ReadAllText(response.ManifestPath);
        using var manifest = JsonDocument.Parse(manifestText);
        Assert.Equal("face-overhaul", manifest.RootElement.GetProperty("mode").GetString());
        Assert.Equal("generated", manifest.RootElement.GetProperty("dependencyMode").GetString());
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
        var donorNif = Path.Combine(_out, "donor_complete.nif");
        var donorDds = Path.Combine(_out, "donor_complete.dds");
        File.WriteAllBytes(donorNif, [0x4E, 0x49, 0x46, 0x00]);
        File.WriteAllBytes(donorDds, [0x44, 0x44, 0x53, 0x20]);

        var expectedFaceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);

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
                        Values = new FaceValuesDto
                        {
                            Sliders = [0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f],
                            Presets = [0, 0, 0, 0],
                            Weight = 75f,
                            SkinTone = new AppearanceColor(255, 220, 200),
                            Tints = [],
                        },
                        GeneratedAssets =
                        [
                            new GeneratedAsset
                            {
                                SourcePath = donorNif,
                                DataPath = expectedFaceGen.GeometryPath,
                                Sha256 = Sha256Of(donorNif),
                                Kind = "facegen-geometry",
                            },
                            new GeneratedAsset
                            {
                                SourcePath = donorDds,
                                DataPath = expectedFaceGen.TintPath,
                                Sha256 = Sha256Of(donorDds),
                                Kind = "facegen-tint",
                            },
                        ],
                    },
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportCompleteOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        Assert.True(File.Exists(response.PluginPath));
        var manifestText = File.ReadAllText(response.ManifestPath);
        using var manifest = JsonDocument.Parse(manifestText);
        Assert.Equal("complete-overhaul", manifest.RootElement.GetProperty("mode").GetString());
        // The outfit half carries the same save-game warning as an outfit-only patch.
        Assert.Contains(response.Issues, i => i.Code == "SaveGamePersistence");

        using var mod = ReadBack(response.PluginPath);
        var outfit = Assert.Single(mod.Outfits);
        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal(outfit.FormKey, lydia.DefaultOutfit.FormKey);
        Assert.Equal(75f, lydia.Weight);
        Assert.NotNull(lydia.TextureLighting);
        Assert.Equal(255, lydia.TextureLighting.Value.R);
    }

    [Fact]
    public void ExportCompleteOverhaul_RejectsPreviewOnlyMaterialInsteadOfDroppingIt()
    {
        var request = new ExportCompleteOverhaulRequest
        {
            PluginName = "SkyrimNpcStudio_Lydia_UnverifiedMaterial.esp",
            OutputDirectory = _out,
            Assignments =
            [
                new CompleteNpcAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    Outfit = new OutfitAssignment
                    {
                        NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                        ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString()],
                        MaterialOverrides =
                        [
                            new PieceMaterialDto
                            {
                                ArmorFormKey = SyntheticLoadOrder.SteelCuirass.ToString(),
                                CustomTintColor = new AppearanceColor(64, 32, 16),
                            },
                        ],
                    },
                },
            ],
        };

        var error = Assert.Throws<WorkerException>(() => _state.Read(c =>
            ModPackager.ExportCompleteOverhaul(c.Session, c.Queries, request, CancellationToken.None)));
        Assert.Equal(ErrorCodes.ValidationFailed, error.Code);
        Assert.Contains(error.Issues, issue => issue.Code == "MaterialVariantPreviewOnly");
        Assert.False(File.Exists(Path.Combine(_out, request.PluginName)));
    }

    [Fact]
    public void ExportFaceOverhaul_HairColorOnlyChangesHclfAndNeedsOnlyTheFaceGenNif()
    {
        // The studio sends the complete face read with face.getRecipe; only the hair color differs.
        // Regression: QNAM was rewritten with another alpha and counted as a skin tone change, so
        // the export demanded a FaceTint and refused a pure hair color edit.
        var recipe = _state.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries,
            new FaceRecipeRequest { NpcFormKey = SyntheticLoadOrder.Lydia.ToString() }));
        var nif = Path.Combine(_out, "hair_only.nif");
        File.WriteAllBytes(nif, [0x4E, 0x49, 0x46, 0x01]);
        var expectedFaceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);
        var request = new ExportFaceOverhaulRequest
        {
            PluginName = "SNS_Lydia_HairOnly.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Assignments =
            [
                new FaceAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    Values = new FaceValuesDto
                    {
                        Sliders = recipe.Values.Sliders,
                        Presets = recipe.Values.Presets,
                        Weight = recipe.Values.Weight,
                        SkinTone = recipe.Values.SkinTone,
                        HairColor = new AppearanceColor(165, 58, 30),
                        Tints = recipe.Values.Tints,
                    },
                    HeadParts = recipe.NpcHeadParts,
                    HeadTexture = recipe.NpcHeadTexture,
                    GeneratedAssets =
                    [
                        new GeneratedAsset { SourcePath = nif, DataPath = expectedFaceGen.GeometryPath, Sha256 = Sha256Of(nif), Kind = "facegen-geometry" },
                    ],
                    FaceGen = new FaceGenProvenance { Mode = "hair-color-safe", InstallAbove = ["Helen Lydia Replacer", "NITHI Whiterun"] },
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        var changed = manifest.RootElement.GetProperty("faces")[0].GetProperty("changedFields").EnumerateArray().Select(e => e.GetString()).ToList();
        Assert.Equal(["HCLF"], changed);
        // The mods that also carry Lydia's FaceGen: named in the manifest, the README and the pending checks.
        var above = manifest.RootElement.GetProperty("faces")[0].GetProperty("installAbove").EnumerateArray().Select(e => e.GetString()).ToList();
        Assert.Equal(["Helen Lydia Replacer", "NITHI Whiterun"], above);
        var readme = File.ReadAllText(Path.Combine(response.ModDirectory, "README.md"));
        Assert.Contains("Prioridad en MO2", readme, StringComparison.Ordinal);
        Assert.Contains("«NITHI Whiterun»", readme, StringComparison.Ordinal);
        Assert.Contains(response.PendingVerification, p => p.Contains("por encima de «Helen Lydia Replacer»", StringComparison.Ordinal));
        Assert.True(File.Exists(Path.Combine(response.ModDirectory, expectedFaceGen.GeometryPath)));
        Assert.False(File.Exists(Path.Combine(response.ModDirectory, expectedFaceGen.TintPath)));

        using var mod = ReadBack(response.PluginPath);
        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal((206, 205, 204), (lydia.TextureLighting!.Value.R, lydia.TextureLighting.Value.G, lydia.TextureLighting.Value.B));
        var color = Assert.Single(mod.Colors);
        Assert.Equal(color.FormKey, lydia.HairColor.FormKey);
        Assert.Equal((165, 58, 30), (color.Color.R, color.Color.G, color.Color.B));
    }

    [Fact]
    public void ExportFaceOverhaul_OnlyFieldsHclfKeepsTheWinningRecordExceptTheHairColor()
    {
        // Hair Color Safe Mode: the studio asks for HCLF only. Even if the values it sends do not
        // round-trip exactly (here every slider is off), nothing but HCLF may reach the record.
        var winner = _state.Read(c => c.Session.LinkCache.Resolve<INpcGetter>(SyntheticLoadOrder.Lydia));
        var winnerSliders = FaceRecipeQueries.Sliders(winner.FaceMorph);
        var winnerParts = winner.HeadParts.Select(h => h.FormKey).ToList();
        var nif = Path.Combine(_out, "hair_safe.nif");
        File.WriteAllBytes(nif, [0x4E, 0x49, 0x46, 0x02]);
        var expectedFaceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);
        var request = new ExportFaceOverhaulRequest
        {
            PluginName = "SNS_Lydia_HairSafe.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Assignments =
            [
                new FaceAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    OnlyFields = ["HCLF"],
                    Values = new FaceValuesDto
                    {
                        Sliders = Enumerable.Repeat(0.9f, 18).ToList(),
                        Presets = [5, 5, 5, 5],
                        Weight = 1f,
                        SkinTone = new AppearanceColor(1, 2, 3),
                        HairColor = new AppearanceColor(12, 200, 34),
                        Tints = [],
                    },
                    GeneratedAssets =
                    [
                        new GeneratedAsset { SourcePath = nif, DataPath = expectedFaceGen.GeometryPath, Sha256 = Sha256Of(nif), Kind = "facegen-geometry" },
                    ],
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));

        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        var changed = manifest.RootElement.GetProperty("faces")[0].GetProperty("changedFields").EnumerateArray().Select(e => e.GetString()).ToList();
        Assert.Equal(["HCLF"], changed);
        using var mod = ReadBack(response.PluginPath);
        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal(winnerSliders, FaceRecipeQueries.Sliders(lydia.FaceMorph));
        Assert.Equal(winner.Weight, lydia.Weight);
        Assert.Equal(winnerParts, lydia.HeadParts.Select(h => h.FormKey).ToList());
        Assert.Equal(winner.TintLayers.Count, lydia.TintLayers.Count);
        var color = Assert.Single(mod.Colors);
        Assert.Equal(color.FormKey, lydia.HairColor.FormKey);
        Assert.Equal((12, 200, 34), (color.Color.R, color.Color.G, color.Color.B));
    }

    [Fact]
    public void ExportFaceOverhaul_OnlyFieldsRejectsFieldsItCannotWriteMinimally()
    {
        var request = new ExportFaceOverhaulRequest
        {
            PluginName = "SNS_Lydia_OnlyNam9.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Assignments =
            [
                new FaceAssignment { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), OnlyFields = ["NAM9"] },
            ],
        };

        var error = Assert.Throws<WorkerException>(() => _state.Read(c =>
            ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None)));
        Assert.Equal(ErrorCodes.ValidationFailed, error.Code);
        Assert.Contains(error.Issues, issue => issue.Code == "OnlyFieldsUnsupported");
        Assert.False(File.Exists(Path.Combine(_out, request.PluginName)));
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
