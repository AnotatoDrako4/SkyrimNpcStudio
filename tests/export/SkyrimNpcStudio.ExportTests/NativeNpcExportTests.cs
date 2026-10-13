using System.Security.Cryptography;
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

public sealed class NativeNpcExportTests : IDisposable
{
    private static readonly string[] ExpectedMasters = ["Skyrim.esm", "ArmorPack.esp"];
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out = Path.Combine(Path.GetTempPath(), "sns-native-" + Guid.NewGuid().ToString("N"));

    public NativeNpcExportTests()
    {
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "native-tests", CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private NativeNpcDraft Draft() => new()
    {
        Name = "Aerin", EditorId = "SNS_Aerin", RaceFormKey = SyntheticLoadOrder.NordRace.ToString(), Female = true,
        VoiceFormKey = SyntheticLoadOrder.FemaleVoice.ToString(), ClassFormKey = SyntheticLoadOrder.WarriorClass.ToString(),
        Height = 1.04f, Weight = 35, Level = 12, Health = 180, Magicka = 75, Stamina = 140,
        HeadPartFormKeys = [SyntheticLoadOrder.FemaleHeadNord.ToString(), SyntheticLoadOrder.EyesGreen.ToString()],
        OutfitArmorFormKeys = [_lo.CrimsonCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
        Inventory = [new NativeInventoryItem(SyntheticLoadOrder.SteelGauntlets.ToString(), 2)],
        Factions = [new NativeFaction(SyntheticLoadOrder.HousecarlFaction.ToString(), 0)],
    };
    private CreationExportRequest Request(NativeNpcDraft? draft = null) => new()
    {
        Npc = draft ?? Draft(), PluginName = "SNS_Aerin.esp", OutputDirectory = _out, TechnicalOnly = true,
    };
    private CreationExportResponse Export(CreationExportRequest r) =>
        _state.Read(c => NativeNpcBuilder.Export(c.Session, c.Queries, r, TestContext.Current.CancellationToken));
    private CreationValidateResponse Validate(NativeNpcDraft d) =>
        _state.Read(c => NativeNpcBuilder.Validate(c.Session, c.Queries, new CreationValidateRequest { Npc = d, PluginName = "SNS_Aerin.esp" }));

    [Fact]
    public void TechnicalExport_CreatesOwnedNpcAndOutfit_WithoutOverrides()
    {
        var result = Export(Request());
        Assert.True(result.ReadBackVerified);
        Assert.True(result.TechnicalOnly);
        Assert.All(result.Package.Records, r => { Assert.Equal("new", r.Kind); Assert.Equal("SNS_Aerin.esp", FormKey.Factory(r.FormKey).ModKey.FileName.String); });
        using var mod = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("SNS_Aerin.esp"), result.Package.PluginPath), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });
        var npc = Assert.Single(mod.Npcs);
        Assert.NotEqual(SyntheticLoadOrder.Lydia, npc.FormKey);
        Assert.Equal("Aerin", npc.Name!.String);
        Assert.Equal("SNS_Aerin", npc.EditorID);
        Assert.Equal(SyntheticLoadOrder.NordRace, npc.Race.FormKey);
        Assert.True(npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female));
        Assert.Equal(12, Assert.IsAssignableFrom<INpcLevelGetter>(npc.Configuration.Level).Level);
        Assert.Equal(180, npc.PlayerSkills!.Health);
        Assert.Equal(SyntheticLoadOrder.FemaleVoice, npc.Voice.FormKey);
        Assert.Equal(SyntheticLoadOrder.WarriorClass, npc.Class.FormKey);
        Assert.Equal(2, Assert.Single(npc.Items!).Item.Count);
        Assert.Equal(SyntheticLoadOrder.HousecarlFaction, Assert.Single(npc.Factions).Faction.FormKey);
        Assert.Equal(2, npc.HeadParts.Count);
        Assert.Equal(Assert.Single(mod.Outfits).FormKey, npc.DefaultOutfit.FormKey);
        Assert.Equal(ExpectedMasters, result.Package.Masters);
        Assert.Equal(result.Package.PluginSha256, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(result.Package.PluginPath))));
        using var manifest = JsonDocument.Parse(File.ReadAllText(result.Package.ManifestPath));
        Assert.False(manifest.RootElement.GetProperty("inGameVerified").GetBoolean());
        Assert.False(manifest.RootElement.GetProperty("readyForGame").GetBoolean());
        Assert.Equal("pending", manifest.RootElement.GetProperty("faceGenStatus").GetString());
        Assert.Contains("NO INSTALAR", File.ReadAllText(Path.Combine(result.Package.ModDirectory, "README.md")), StringComparison.Ordinal);
    }

    [Fact]
    public void FinalExport_RejectsMissingFaceGen_AndLeavesNoPartialOutput()
    {
        Assert.False(Validate(Draft()).Exportable);
        Assert.True(Validate(Draft()).TechnicallyExportable);
        var ex = Assert.Throws<WorkerException>(() => Export(Request() with { TechnicalOnly = false }));
        Assert.Equal(ErrorCodes.ValidationFailed, ex.Code);
        Assert.Contains(ex.Issues, x => x.Code == "NativeFaceGenUnavailable");
        Assert.False(Directory.Exists(Path.Combine(_out, "SNS_Aerin")));
        Assert.Empty(Directory.GetDirectories(_out, ".sns-staging-*"));
    }

    [Theory]
    [InlineData("bad id", "EditorIdInvalid")]
    [InlineData("HousecarlWhiterun", "EditorIdConflict")]
    public void InvalidEditorId_IsRejected(string id, string code) => Assert.Contains(Validate(Draft() with { EditorId = id }).Issues, x => x.Code == code);

    [Fact]
    public void WrongTypeOrMissingReferences_AreRejected()
    {
        var d = Draft() with { RaceFormKey = SyntheticLoadOrder.Lydia.ToString(), VoiceFormKey = "FFFFFF:Skyrim.esm" };
        Assert.False(Validate(d).TechnicallyExportable);
        Assert.Contains(Validate(d).MissingReferences, x => x == d.VoiceFormKey);
        Assert.Throws<WorkerException>(() => Export(Request(d)));
    }

    [Fact]
    public void SlotConflictAndWrongRaceArmor_AreRejected()
    {
        var slots = Draft() with { OutfitArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.ElvenCuirass.ToString()] };
        Assert.Contains(Validate(slots).Issues, x => x.Code == "SlotConflict");
        var race = Draft() with { OutfitArmorFormKeys = [SyntheticLoadOrder.KhajiitOnlyHood.ToString()] };
        Assert.Contains(Validate(race).Issues, x => x.Code == "NoAddonForRace");
    }

    [Fact]
    public void HeadPartSexAndInvalidNumbers_AreRejected()
    {
        Assert.Contains(Validate(Draft() with { Female = false }).Issues, x => x.Code == "HeadPartSex");
        Assert.False(Validate(Draft() with { Height = float.NaN, Weight = 101, Level = 0 }).TechnicallyExportable);
        Assert.False(Validate(Draft() with { Inventory = [new NativeInventoryItem(SyntheticLoadOrder.Lydia.ToString(), -1)] }).TechnicallyExportable);
    }

    [Fact]
    public void EslExport_UsesLightRange_AndRereadsFlag()
    {
        var result = Export(Request() with { EslFlag = true });
        Assert.True(result.Package.EslFlagged);
        Assert.All(result.Package.Records, r => Assert.InRange(FormKey.Factory(r.FormKey).ID, 0x800u, 0xFFFu));
    }

    [Fact]
    public void CancelledAndDuplicateExports_DoNotOverwrite()
    {
        using var cts = new CancellationTokenSource(); cts.Cancel();
        Assert.Throws<OperationCanceledException>(() => _state.Read(c => NativeNpcBuilder.Export(c.Session, c.Queries, Request(), cts.Token)));
        var first = Export(Request());
        var bytes = File.ReadAllBytes(first.Package.PluginPath);
        Assert.Equal(ErrorCodes.OutputExists, Assert.Throws<WorkerException>(() => Export(Request())).Code);
        Assert.Equal(bytes, File.ReadAllBytes(first.Package.PluginPath));
        Assert.Empty(Directory.GetDirectories(_out, ".sns-staging-*"));
    }

    [Fact]
    public void Placement_IsOptional_AndOnlyCreatesRequestedInteriorReference()
    {
        var d = Draft() with { Placement = new NativePlacement { CellFormKey = SyntheticLoadOrder.DragonsreachCell.ToString(), X = 10, Y = 20, Z = 30 } };
        var result = Export(Request(d));
        Assert.Single(result.Package.Records, r => r.RecordType == "ACHR");
        using var mod = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("SNS_Aerin.esp"), result.Package.PluginPath), SkyrimRelease.SkyrimSE);
        var placed = Assert.Single(mod.EnumerateMajorRecords<IPlacedNpcGetter>());
        Assert.Equal(Assert.Single(mod.Npcs).FormKey, placed.Base.FormKey);
        Assert.Equal(10, placed.Placement!.Position.X);
        Assert.DoesNotContain(mod.EnumerateMajorRecords(), r => r.FormKey == SyntheticLoadOrder.LydiaRef);
    }

    [Fact]
    public void FaceValuesAndScaling_ArePersistedWithoutClaimingFaceGen()
    {
        var d = Draft() with { LevelMultiplier = 1.5f, MinLevel = 5, MaxLevel = 40,
            Skills = new Dictionary<string, int> { ["OneHanded"] = 70 },
            HairColorFormKey = SyntheticLoadOrder.HairColorBrown.ToString(),
            Face = new FaceValuesDto { Sliders = Enumerable.Repeat(0.2f, 18).ToArray(), Presets = [1, -1, 2, 3], Weight = 35,
                SkinTone = new AppearanceColor(200, 190, 180),
                Tints = [new FaceRecipeTint(24, new AppearanceColor(200, 190, 180), 0.8f, 602)] } };
        var r = Export(Request(d));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("SNS_Aerin.esp"), r.Package.PluginPath), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });
        var npc = Assert.Single(mod.Npcs);
        Assert.Equal(1.5f, Assert.IsAssignableFrom<IPcLevelMultGetter>(npc.Configuration.Level).LevelMult);
        Assert.Equal(70, npc.PlayerSkills!.SkillValues[Skill.OneHanded]);
        Assert.Equal(0.2f, npc.FaceMorph!.NoseLongVsShort);
        Assert.Equal((ushort)24, Assert.Single(npc.TintLayers).Index!.Value);
        Assert.False(r.Validation.Exportable);
    }

    [Fact]
    public void MalformedDraftCollectionsAndFace_AreRejected()
    {
        Assert.Throws<ArgumentNullException>(() => Validate(Draft() with { Inventory = null! }));
        var d = Draft() with { Face = new FaceValuesDto { Sliders = [], Presets = [], Weight = 35, SkinTone = new AppearanceColor(255, 255, 255), Tints = [] } };
        Assert.False(Validate(d).TechnicallyExportable);
        Assert.Throws<WorkerException>(() => Export(Request(d)));
        Assert.Empty(Directory.GetDirectories(_out, ".sns-staging-*"));
    }

    [Fact]
    public void DuplicateOutfitPieces_AreRejected()
    {
        Assert.False(Validate(Draft() with { OutfitArmorFormKeys = [SyntheticLoadOrder.SteelBoots.ToString(), SyntheticLoadOrder.SteelBoots.ToString()] }).TechnicallyExportable);
    }

    [Fact]
    public void OutputAndPlacementMustBeValid()
    {
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request() with { OutputDirectory = "relative" })).Code);
        Assert.False(Validate(Draft() with { Placement = new NativePlacement { CellFormKey = SyntheticLoadOrder.Lydia.ToString() } }).TechnicallyExportable);
    }

    public void Dispose()
    {
        _state.Dispose(); _lo.Dispose(); Directory.Delete(_out, true);
    }
}
