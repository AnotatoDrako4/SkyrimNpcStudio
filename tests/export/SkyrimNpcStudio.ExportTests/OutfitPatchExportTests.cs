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
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// End-to-end export of the first mandatory use case on synthetic data: change Lydia's outfit
/// with pieces from different plugins and package it as an MO2-installable mod.
/// </summary>
public sealed class OutfitPatchExportTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public OutfitPatchExportTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-export-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "export-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private ExportOutfitPatchRequest LydiaRequest(bool esl = false, string name = "SkyrimNpcStudio_Lydia.esp") => new()
    {
        PluginName = name,
        OutputDirectory = _out,
        EslFlag = esl,
        Description = "Lydia con corsé carmesí",
        PluginProviders = new Dictionary<string, string> { ["ArmorPack.esp"] = "Crimson Armor Pack" },
        Assignments =
        [
            new OutfitAssignment
            {
                NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                ArmorFormKeys =
                [
                    _lo.CrimsonCuirass.ToString(),
                    SyntheticLoadOrder.SteelBoots.ToString(),
                    SyntheticLoadOrder.SteelGauntlets.ToString(),
                    _lo.CrimsonCloak.ToString(),
                ],
            },
        ],
    };

    private ExportResponse Export(ExportOutfitPatchRequest request) =>
        _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, request, CancellationToken.None));

    private ISkyrimModDisposableGetter ReadBack(string path) =>
        SkyrimMod.CreateFromBinaryOverlay(
            new ModPath(ModKey.FromFileName(Path.GetFileName(path)), path),
            SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });

    [Fact]
    public void Export_WritesAPluginWithANewOutfitAndAMinimalLydiaOverride()
    {
        var response = Export(LydiaRequest());

        Assert.True(File.Exists(response.PluginPath));
        Assert.Equal(["Skyrim.esm", "ArmorPack.esp"], response.Masters);
        Assert.Contains(response.Records, r => r is { RecordType: "OTFT", Kind: "new" });
        Assert.Contains(response.Records, r => r is { RecordType: "NPC_", Kind: "override" } && r.FormKey == SyntheticLoadOrder.Lydia.ToString());

        using var mod = ReadBack(response.PluginPath);
        var outfit = Assert.Single(mod.Outfits);
        Assert.Equal(
            [_lo.CrimsonCuirass, SyntheticLoadOrder.SteelBoots, SyntheticLoadOrder.SteelGauntlets, _lo.CrimsonCloak],
            outfit.Items!.Select(i => i.FormKey));

        var lydia = Assert.Single(mod.Npcs);
        Assert.Equal(SyntheticLoadOrder.Lydia, lydia.FormKey);
        Assert.Equal(outfit.FormKey, lydia.DefaultOutfit.FormKey);
        Assert.Empty(mod.Armors);   // dependent mode: armors are referenced, never copied
    }

    [Fact]
    public void Export_OverrideIsCopiedFromTheWinningRecord()
    {
        var response = Export(LydiaRequest());
        using var mod = ReadBack(response.PluginPath);
        var lydia = Assert.Single(mod.Npcs);

        // LateTweak.esp (the winner) set Weight 80: an outfit patch must not revert it.
        Assert.Equal(80f, lydia.Weight);
        Assert.Equal("Lydia", lydia.Name?.String);
        Assert.Equal(SyntheticLoadOrder.NordRace, lydia.Race.FormKey);
        Assert.Equal(SyntheticLoadOrder.HairNord, Assert.Single(lydia.HeadParts).FormKey);
        Assert.True(lydia.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Essential));
    }

    [Fact]
    public void Export_MultipleNpcAssignments_WritesOutfitsAndOverridesForAllNpcs()
    {
        var request = new ExportOutfitPatchRequest
        {
            PluginName = "SkyrimNpcStudio_MultiNpc.esp",
            OutputDirectory = _out,
            EslFlag = true,
            Description = "Parche multi-NPC",
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [_lo.CrimsonCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
                },
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.GuardTemplate.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.SteelGauntlets.ToString()],
                    AlsoSetSleepingOutfit = true,
                },
            ],
        };

        var response = Export(request);

        Assert.True(File.Exists(response.PluginPath));
        Assert.Equal(4, response.Records.Count);
        Assert.Contains(response.Records, r => r.RecordType == "NPC_" && r.FormKey == SyntheticLoadOrder.Lydia.ToString());
        Assert.Contains(response.Records, r => r.RecordType == "NPC_" && r.FormKey == SyntheticLoadOrder.GuardTemplate.ToString());

        using var mod = ReadBack(response.PluginPath);
        Assert.Equal(2, mod.Outfits.Count);
        Assert.Equal(2, mod.Npcs.Count);

        var lydia = mod.Npcs.Single(n => n.FormKey == SyntheticLoadOrder.Lydia);
        var guard = mod.Npcs.Single(n => n.FormKey == SyntheticLoadOrder.GuardTemplate);

        Assert.NotNull(lydia.DefaultOutfit.FormKeyNullable);
        Assert.NotNull(guard.DefaultOutfit.FormKeyNullable);
        Assert.Equal(guard.DefaultOutfit.FormKey, guard.SleepingOutfit.FormKey);
    }

    [Fact]
    public void Export_EslFlagged_PutsNewRecordsInTheLightRange()
    {
        var response = Export(LydiaRequest(esl: true));
        Assert.True(response.EslFlagged);

        using var mod = ReadBack(response.PluginPath);
        Assert.True(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
        var outfit = Assert.Single(mod.Outfits);
        Assert.InRange(outfit.FormKey.ID, OutfitPatchBuilder.LightRangeMin, OutfitPatchBuilder.LightRangeMax);
    }

    [Fact]
    public void Export_PackagesAnMo2InstallableFolderAndZip()
    {
        var response = Export(LydiaRequest());

        Assert.Equal(Path.Combine(_out, "SkyrimNpcStudio_Lydia"), response.ModDirectory);
        Assert.True(File.Exists(Path.Combine(response.ModDirectory, "meta.ini")));
        Assert.Contains("gameName=SkyrimSE", File.ReadAllText(Path.Combine(response.ModDirectory, "meta.ini")), StringComparison.Ordinal);
        var readme = File.ReadAllText(Path.Combine(response.ModDirectory, "README.md"));
        Assert.Contains("ArmorPack.esp — mod de MO2 \"Crimson Armor Pack\"", readme, StringComparison.Ordinal);

        using (var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath)))
        {
            Assert.Equal(ModPackager.ManifestFormat, manifest.RootElement.GetProperty("format").GetString());
            Assert.Equal(response.PluginSha256, manifest.RootElement.GetProperty("plugin").GetProperty("sha256").GetString());
            Assert.False(manifest.RootElement.GetProperty("inGameVerified").GetBoolean());
        }

        using var zip = ZipFile.OpenRead(response.ZipPath!);
        Assert.Equal(["README.md", "SkyrimNpcStudio_Lydia.esp", "manifest.json"], zip.Entries.Select(e => e.FullName).Order(StringComparer.Ordinal));
        Assert.Empty(Directory.GetDirectories(_out, ".sns-staging-*"));
    }

    [Fact]
    public void Export_NeverOverwritesAnExistingMod()
    {
        Export(LydiaRequest());
        var before = File.GetLastWriteTimeUtc(Path.Combine(_out, "SkyrimNpcStudio_Lydia", "SkyrimNpcStudio_Lydia.esp"));

        var ex = Assert.Throws<WorkerException>(() => Export(LydiaRequest()));
        Assert.Equal(ErrorCodes.OutputExists, ex.Code);
        Assert.Equal(before, File.GetLastWriteTimeUtc(Path.Combine(_out, "SkyrimNpcStudio_Lydia", "SkyrimNpcStudio_Lydia.esp")));
    }

    [Fact]
    public void Export_ValidationFailure_WritesNothing()
    {
        var request = LydiaRequest() with
        {
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.ElvenCuirass.ToString()],
                },
            ],
        };

        var ex = Assert.Throws<WorkerException>(() => Export(request));
        Assert.Equal(ErrorCodes.ValidationFailed, ex.Code);
        Assert.Contains(ex.Issues, i => i.Code == "SlotConflict");
        Assert.DoesNotContain(Directory.GetFileSystemEntries(_out), p => !p.EndsWith("_cache", StringComparison.Ordinal));
    }

    [Fact]
    public void Export_RefusesUnsafeTargets()
    {
        var intoData = Assert.Throws<WorkerException>(() => Export(LydiaRequest() with { OutputDirectory = _lo.DataDirectory }));
        Assert.Equal(ErrorCodes.InvalidArgument, intoData.Code);

        var clash = Assert.Throws<WorkerException>(() => Export(LydiaRequest(name: "FaceMod.esp")));
        Assert.Equal(ErrorCodes.ValidationFailed, clash.Code);

        var traversal = Assert.Throws<WorkerException>(() => Export(LydiaRequest(name: "..\\evil.esp")));
        Assert.Equal(ErrorCodes.InvalidArgument, traversal.Code);
    }

    [Fact]
    public void Export_WithMaterialOverride_GeneratesTxstArmaAndArmo()
    {
        var request = new ExportOutfitPatchRequest
        {
            PluginName = "SkyrimNpcStudio_Lydia_Recolor.esp",
            OutputDirectory = _out,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString()],
                    MaterialOverrides =
                    [
                        new PieceMaterialDto
                        {
                            ArmorFormKey = SyntheticLoadOrder.SteelCuirass.ToString(),
                            DiffusePath = "textures\\armor\\steel\\cuirass_recolor_d.dds",
                            NormalPath = "textures\\armor\\steel\\cuirass_n.dds",
                            MaterialName = "EbonyTint",
                        },
                    ],
                },
            ],
        };

        var response = Export(request);
        using var mod = ReadBack(response.PluginPath);

        // Verify TXST was generated
        var txst = Assert.Single(mod.TextureSets);
        Assert.Equal("textures\\armor\\steel\\cuirass_recolor_d.dds", txst.Diffuse);
        Assert.Equal("textures\\armor\\steel\\cuirass_n.dds", txst.NormalOrGloss);

        // Verify ARMA was generated and references TXST
        var arma = Assert.Single(mod.ArmorAddons);
        Assert.NotNull(arma.WorldModel?.Female?.AlternateTextures);
        Assert.Contains(arma.WorldModel.Female.AlternateTextures, at => at.NewTexture.FormKey == txst.FormKey);

        // Verify ARMO was generated and references the new ARMA
        var armo = Assert.Single(mod.Armors);
        Assert.Contains(armo.Armature, a => a.FormKey == arma.FormKey);

        // Verify OTFT contains the cloned ARMO instead of the original SteelCuirass
        var outfit = Assert.Single(mod.Outfits);
        Assert.Contains(outfit.Items!, item => item.FormKey == armo.FormKey);
        Assert.DoesNotContain(outfit.Items!, item => item.FormKey == SyntheticLoadOrder.SteelCuirass);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try
        {
            Directory.Delete(_out, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
