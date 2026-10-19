using System.Text.Json;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// Assisted resolution («Usar el rostro de X»): the dark-face case of the test profile rebuilt with
/// synthetic plugins. FaceReplacer.esp (mod «Face Replacer», which also ships the loose FaceGen) gives
/// Lydia a new face; LateAI.esp, made from the ORIGINAL record, changes her AI and so sets the vanilla
/// face fields back. The patch copies FaceReplacer's face fields onto LateAI's record.
/// </summary>
public sealed class FaceResolutionExportTests : IDisposable
{
    private static readonly ModKey FaceReplacer = ModKey.FromFileName("FaceReplacer.esp");
    private static readonly ModKey LateAi = ModKey.FromFileName("LateAI.esp");
    private static readonly string[] CopiedFields = ["NAM9", "NAMA", "PNAM", "TINT", "QNAM", "NAM7"];

    private static readonly Dictionary<string, string> Providers = new()
    {
        ["Skyrim.esm"] = "<Game Data>",
        ["FaceReplacer.esp"] = "Face Replacer",
        ["LateAI.esp"] = "AI Mod",
    };

    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public FaceResolutionExportTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-face-resolution-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        using (var master = SkyrimMod.CreateFromBinaryOverlay(_lo.PathOf(SyntheticLoadOrder.Skyrim), SkyrimRelease.SkyrimSE))
        {
            var lydia = master.Npcs.First(n => n.FormKey == SyntheticLoadOrder.Lydia);

            var replacer = new SkyrimMod(FaceReplacer, SkyrimRelease.SkyrimSE);
            var face = replacer.Npcs.GetOrAddAsOverride(lydia);
            face.FaceMorph!.NoseLongVsShort = 0.9f;
            face.FaceMorph.JawNarrowVsWide = -0.7f;
            face.FaceParts = new NpcFaceParts { Nose = 3, Unknown = 0xFFFFFFFF, Eyes = 2, Mouth = 5 };
            face.HeadParts.Add(new FormLink<IHeadPartGetter>(SyntheticLoadOrder.FemaleHeadNord));
            face.HeadParts.Add(new FormLink<IHeadPartGetter>(SyntheticLoadOrder.EyesGreen));
            face.TintLayers[0].Color = System.Drawing.Color.FromArgb(0, 180, 150, 140);
            face.TintLayers.Add(new TintLayer { Index = 7, Color = System.Drawing.Color.FromArgb(0, 90, 20, 20), InterpolationValue = 0.4f, Preset = 3 });
            face.TextureLighting = System.Drawing.Color.FromArgb(255, 180, 150, 140);
            face.Weight = 30f;
            replacer.WriteToBinary(Path.Combine(_lo.DataDirectory, FaceReplacer.FileName));

            // Made from the vanilla record: its own change is the AI; the face fields are vanilla's again.
            var ai = new SkyrimMod(LateAi, SkyrimRelease.SkyrimSE);
            var tweak = ai.Npcs.GetOrAddAsOverride(lydia);
            tweak.AIData.Confidence = Confidence.Foolhardy;
            tweak.AIData.Aggression = Aggression.Aggressive;
            ai.WriteToBinary(Path.Combine(_lo.DataDirectory, LateAi.FileName));
        }

        Index(_state, "face-resolution-tests", "_cache", SyntheticLoadOrder.Skyrim.FileName, FaceReplacer.FileName, LateAi.FileName);
    }

    private void Index(WorkerState state, string profile, string cache, params string[] plugins) =>
        state.Index(new IndexRequest
        {
            ProfileKey = profile,
            CacheDirectory = Path.Combine(_out, cache),
            Plugins = plugins.Select(f => new LoadOrderEntry { FileName = f, Path = Path.Combine(_lo.DataDirectory, f) }).ToArray(),
        }, _ => { }, CancellationToken.None);

    private static FaceAssignment Copy(string from = "FaceReplacer.esp", IReadOnlyList<string>? fields = null, string faceGenMod = "Face Replacer") => new()
    {
        NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
        CopyFaceFrom = from,
        OnlyFields = fields ?? CopiedFields,
        FaceGen = new FaceGenProvenance { Mode = "source-facegen", EffectiveFaceGenMod = faceGenMod },
    };

    private ExportResponse Export(FaceAssignment face, string plugin = "SNS_LydiaFace.esp", IReadOnlyDictionary<string, string>? providers = null) =>
        _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, new ExportFaceOverhaulRequest
        {
            PluginName = plugin,
            OutputDirectory = _out,
            EslFlag = true,
            CreateZip = true,
            Assignments = [face],
            PluginProviders = providers ?? Providers,
        }, CancellationToken.None));

    private WorkerException Refused(FaceAssignment face, string plugin)
    {
        var error = Assert.Throws<WorkerException>(() => Export(face, plugin));
        Assert.False(Directory.Exists(Path.Combine(_out, Path.GetFileNameWithoutExtension(plugin))), "nothing is written when the export is refused");
        return error;
    }

    private ISkyrimModDisposableGetter Read(string path) =>
        SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName(Path.GetFileName(path)), path), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });

    [Fact]
    public void Chain_AttributesTheLateRevertToTheVanillaRecord()
    {
        var npc = _state.Read(c => c.Queries.GetNpc(SyntheticLoadOrder.Lydia.ToString()))!;
        Assert.Equal(["Skyrim.esm", "FaceReplacer.esp", "LateAI.esp"], npc.Overrides.Select(o => o.Plugin));
        var late = npc.Overrides[2];
        Assert.Contains("FaceMorph", late.ChangedFields);
        Assert.Contains("AIData", late.ChangedFields);
        // LateAI's face fields are vanilla's again: copies of Skyrim.esm; its AI is its own.
        Assert.Equal("Skyrim.esm", late.CopiedFrom["FaceMorph"]);
        Assert.Equal("Skyrim.esm", late.CopiedFrom["HeadParts"]);
        Assert.False(late.CopiedFrom.ContainsKey("AIData"));
        Assert.Contains(npc.Conflicts, c => c.Field == "FaceMorph" && c.LosingPlugins.SequenceEqual(["FaceReplacer.esp"]));
    }

    [Fact]
    public void Resolution_CopiesOnlyTheSourceFaceOntoTheWinnerAndLoadsAfterBoth()
    {
        var response = Export(Copy());

        using var patch = Read(response.PluginPath);
        var lydia = Assert.Single(patch.Npcs);
        var (source, winner) = _state.Read(c => (
            c.Session.Find(FaceReplacer)!.Mod.Npcs.First(n => n.FormKey == SyntheticLoadOrder.Lydia),
            c.Session.Find(LateAi)!.Mod.Npcs.First(n => n.FormKey == SyntheticLoadOrder.Lydia)));

        // The face is FaceReplacer's, field for field.
        Assert.True(lydia.FaceMorph!.Equals(source.FaceMorph));
        Assert.True(lydia.FaceParts!.Equals(source.FaceParts));
        Assert.Equal(source.HeadParts.Select(h => h.FormKey), lydia.HeadParts.Select(h => h.FormKey));
        Assert.Equal(source.TintLayers.Count, lydia.TintLayers.Count);
        Assert.True(lydia.TintLayers.Zip(source.TintLayers).All(p => p.First.Equals(p.Second)));
        Assert.Equal(source.TextureLighting, lydia.TextureLighting);
        Assert.Equal(30f, lydia.Weight);
        Assert.Equal(source.HairColor.FormKey, lydia.HairColor.FormKey);
        // Everything else is the winner's (LateAI): its AI edit survives.
        Assert.Equal(Aggression.Aggressive, lydia.AIData.Aggression);
        Assert.Equal(Confidence.Foolhardy, lydia.AIData.Confidence);
        Assert.Equal(winner.Height, lydia.Height);
        Assert.Equal(winner.DefaultOutfit.FormKey, lydia.DefaultOutfit.FormKey);

        // Loads after the source and the winner: both are masters, in load order.
        Assert.Equal(["Skyrim.esm", "FaceReplacer.esp", "LateAI.esp"], patch.ModHeader.MasterReferences.Select(m => m.Master.FileName.String));
        Assert.Equal(["Skyrim.esm", "FaceReplacer.esp", "LateAI.esp"], response.Masters);

        // No FaceGen: the plugin is the only file of the mod.
        Assert.Equal(["SNS_LydiaFace.esp"], response.Files.Select(f => f.Path));
        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        var root = manifest.RootElement;
        Assert.Equal("dependent", root.GetProperty("dependencyMode").GetString());
        Assert.Equal(0, root.GetProperty("bundledAssets").GetArrayLength());
        Assert.Equal(["FaceReplacer.esp", "LateAI.esp"], root.GetProperty("loadAfter").EnumerateArray().Select(e => e.GetString()).Order());
        var face = Assert.Single(root.GetProperty("faces").EnumerateArray());
        Assert.Equal("source-facegen", face.GetProperty("faceGenMode").GetString());
        Assert.Equal("Face Replacer", face.GetProperty("effectiveFaceGenMod").GetString());
        Assert.Equal(0, face.GetProperty("installAbove").GetArrayLength());
        Assert.False(root.GetProperty("inGameVerified").GetBoolean());
        Assert.False(root.GetProperty("readyForGame").GetBoolean());
        var readme = File.ReadAllText(Path.Combine(response.ModDirectory, "README.md"));
        Assert.Contains("Debe cargarse después de `FaceReplacer.esp` y `LateAI.esp`", readme);
        Assert.Contains("usa el de «Face Replacer»", readme);

        // The safe patch: only the copied face properties differ from the winner, each with its source.
        var diff = Assert.Single(response.SemanticDiff);
        Assert.Equal(["FaceMorph", "FaceParts", "HeadParts", "TextureLighting", "TintLayers", "Weight"], diff.Changes.Select(c => c.Field).Order());
        Assert.All(diff.Changes, c => Assert.StartsWith("Copiado de FaceReplacer.esp", c.Reason));
        Assert.Contains(response.Verification, v => v.StartsWith("Rostro copiado: 1 NPC", StringComparison.Ordinal));
        Assert.Contains(response.PendingVerification, p => p.Contains("no se ha ejecutado Skyrim", StringComparison.Ordinal));
    }

    [Fact]
    public void Resolution_ProjectsTheChainWithThePatchLoadedLast()
    {
        var response = Export(Copy());
        var chain = Assert.Single(response.ProjectedChains);
        Assert.Equal(SyntheticLoadOrder.Lydia.ToString(), chain.Npc);
        Assert.Equal(["Skyrim.esm", "FaceReplacer.esp", "LateAI.esp", "SNS_LydiaFace.esp"], chain.Overrides.Select(o => o.Plugin));
        var patch = chain.Overrides[^1];
        Assert.True(patch.IsWinner);
        // Re-read from the written file: exactly the copied fields changed, each a copy of FaceReplacer's value.
        Assert.Equal(["FaceMorph", "FaceParts", "HeadParts", "TextureLighting", "TintLayers", "Weight"], patch.ChangedFields);
        Assert.All(patch.ChangedFields, f => Assert.Equal("FaceReplacer.esp", patch.CopiedFrom[f]));
        // LateAI's face (the vanilla values) is the edit that no longer shows.
        Assert.Contains(chain.Conflicts, c => c.Field == "FaceMorph" && c.LosingPlugins.SequenceEqual(["LateAI.esp"]));
        Assert.DoesNotContain(chain.Conflicts, c => c.Field == "AIData");
        // Compared on the re-read records: the whole source face is there.
        Assert.Empty(chain.RemainingFaceDifferences);
    }

    [Fact]
    public void Resolution_CanCopyASubsetAndKeepsTheRestOfTheWinner()
    {
        var response = Export(Copy(fields: ["NAM9"]), "SNS_LydiaMorph.esp");
        using var patch = Read(response.PluginPath);
        var lydia = Assert.Single(patch.Npcs);
        Assert.Equal(0.9f, lydia.FaceMorph!.NoseLongVsShort);
        Assert.DoesNotContain(lydia.HeadParts, h => h.FormKey == SyntheticLoadOrder.EyesGreen);
        Assert.Equal(50f, lydia.Weight);
        Assert.Equal(["FaceMorph"], Assert.Single(response.SemanticDiff).Changes.Select(c => c.Field));
        // The rest of FaceReplacer's face is not there, measured on the records (the studio refuses this).
        Assert.Equal(["NAM7", "NAMA", "PNAM", "QNAM", "TINT"], Assert.Single(response.ProjectedChains).RemainingFaceDifferences.Order());
    }

    [Fact]
    public void Resolution_RefusesWhenTheSourceProviderIsUnknown()
    {
        // Without the source plugin's MO2 mod the FaceGen check cannot be made: refused, not assumed.
        var withoutSource = Providers.Where(p => p.Key != "FaceReplacer.esp").ToDictionary(p => p.Key, p => p.Value);
        var error = Assert.Throws<WorkerException>(() => Export(Copy(), "SNS_NoProvider.esp", withoutSource));
        Assert.Contains(error.Issues, i => i.Code == "FaceSourceProviderUnknown");
        Assert.False(Directory.Exists(Path.Combine(_out, "SNS_NoProvider")));
        var empty = Assert.Throws<WorkerException>(() => Export(Copy(), "SNS_NoProviders.esp", new Dictionary<string, string>()));
        Assert.Contains(empty.Issues, i => i.Code == "FaceSourceProviderUnknown");
    }

    [Fact]
    public void Resolution_MatchesTheSourceProviderWhateverTheCase()
    {
        // Plugin names are case-insensitive: the load order may spell them differently from the chain.
        var lower = Providers.ToDictionary(p => p.Key.ToLowerInvariant(), p => p.Value);
        Assert.Empty(Assert.Single(Export(Copy(), "SNS_Case.esp", lower).ProjectedChains).RemainingFaceDifferences);
        var other = Providers.ToDictionary(p => p.Key.ToUpperInvariant(), p => p.Key == "FaceReplacer.esp" ? "Other Mod" : p.Value);
        Assert.Contains(Assert.Throws<WorkerException>(() => Export(Copy(), "SNS_CaseOther.esp", other)).Issues, i => i.Code == "FaceSourceNotFaceGenMod");
    }

    [Fact]
    public void Resolution_RefusesWhatWouldNotMatchTheEffectiveFaceGen()
    {
        // A FaceGen with a copied face: the source's FaceGen already wins, nothing may be bundled.
        var generated = Path.Combine(_out, "face.nif");
        File.WriteAllBytes(generated, [0x4E, 0x49, 0x46, 0x00]);
        var withAsset = Copy() with
        {
            GeneratedAssets =
            [
                new GeneratedAsset
                {
                    SourcePath = generated,
                    DataPath = @"meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif",
                    Sha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(generated))),
                    Kind = "facegen-geometry",
                },
            ],
        };
        Assert.Contains(Refused(withAsset, "SNS_R1.esp").Issues, i => i.Code == "SourceFaceGenNoAssets");

        // The FaceGen the game uses comes from another mod than the source plugin, or is unknown.
        Assert.Contains(Refused(Copy(faceGenMod: "Other Replacer"), "SNS_R2.esp").Issues, i => i.Code == "FaceSourceNotFaceGenMod");
        Assert.Contains(Refused(Copy(faceGenMod: ""), "SNS_R3.esp").Issues, i => i.Code == "FaceSourceFaceGenUnknown");
        // The source is already the winner or is not loaded.
        Assert.Contains(Refused(Copy(from: "LateAI.esp", faceGenMod: "AI Mod"), "SNS_R4.esp").Issues, i => i.Code == "FaceSourceIsWinner");
        Assert.Contains(Refused(Copy(from: "Missing.esp"), "SNS_R5.esp").Issues, i => i.Code == "FaceSourceNotFound");
        // Height is never copied; a copy needs fields and FaceGen mode source-facegen.
        Assert.Contains(Refused(Copy(fields: ["NAM9", "NAM6"]), "SNS_R6.esp").Issues, i => i.Code == "OnlyFieldsUnsupported");
        Assert.Contains(Refused(Copy(fields: []), "SNS_R7.esp").Issues, i => i.Code == "CopyFaceFieldsRequired");
        Assert.Contains(Refused(Copy() with { FaceGen = new FaceGenProvenance { Mode = "regenerate", EffectiveFaceGenMod = "Face Replacer" } }, "SNS_R8.esp").Issues,
            i => i.Code == "SourceFaceGenModeRequired");
    }

    [Fact]
    public void Resolution_RefusesASourceOfAnotherRace()
    {
        using (var master = SkyrimMod.CreateFromBinaryOverlay(_lo.PathOf(SyntheticLoadOrder.Skyrim), SkyrimRelease.SkyrimSE))
        {
            var lydia = master.Npcs.First(n => n.FormKey == SyntheticLoadOrder.Lydia);
            var khajiit = new SkyrimMod(ModKey.FromFileName("KhajiitLydia.esp"), SkyrimRelease.SkyrimSE);
            var cat = khajiit.Npcs.GetOrAddAsOverride(lydia);
            cat.Race.SetTo(SyntheticLoadOrder.KhajiitRace);
            cat.FaceMorph!.NoseLongVsShort = -0.5f;
            khajiit.WriteToBinary(Path.Combine(_lo.DataDirectory, "KhajiitLydia.esp"));
        }

        using var state = new WorkerState();
        Index(state, "face-resolution-race", "_cache_race", "Skyrim.esm", "KhajiitLydia.esp", "LateAI.esp");
        var error = Assert.Throws<WorkerException>(() => state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, new ExportFaceOverhaulRequest
        {
            PluginName = "SNS_Race.esp",
            OutputDirectory = _out,
            Assignments = [Copy(from: "KhajiitLydia.esp", fields: ["NAM9"], faceGenMod: "Cats")],
            PluginProviders = new Dictionary<string, string> { ["KhajiitLydia.esp"] = "Cats" },
        }, CancellationToken.None)));
        Assert.Contains(error.Issues, i => i.Code == "FaceSourceRaceMismatch");
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
