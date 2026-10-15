using System.Text.Json;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
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

/// <summary>
/// NPC Safe Patch Engine: an export may only change the NPC_ fields of the operation that was
/// requested, proved by re-reading the written plugin and comparing EVERY NPC_ property with the
/// winning record, and the allowed differences come back as a readable semantic diff.
/// </summary>
public sealed class NpcSafePatchTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public NpcSafePatchTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-safepatch-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "safe-patch-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private static readonly ModKey TestPatch = new("SNS_Test", ModType.Plugin);

    /// <summary>A form no fixture record uses, so setting it on Lydia is always a real change.</summary>
    private static readonly FormKey OtherForm = new(SyntheticLoadOrder.Skyrim, 0x0A0777);

    private INpcGetter LydiaWinner() => _state.Read(c => c.Session.LinkCache.Resolve<INpcGetter>(SyntheticLoadOrder.Lydia));

    private static string Sha256Of(string path) => Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path)));

    private ExportResponse ExportHairOnly(string pluginName)
    {
        var nif = Path.Combine(_out, pluginName + ".nif");
        File.WriteAllBytes(nif, [0x4E, 0x49, 0x46, 0x02]);
        var faceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);
        var request = new ExportFaceOverhaulRequest
        {
            PluginName = pluginName,
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
                        new GeneratedAsset { SourcePath = nif, DataPath = faceGen.GeometryPath, Sha256 = Sha256Of(nif), Kind = "facegen-geometry" },
                    ],
                },
            ],
        };
        return _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));
    }

    [Fact]
    public void HairColorOnlyExport_DiffNamesExactlyTheHairColor()
    {
        var response = ExportHairOnly("SNS_SafePatch_Hair.esp");

        var diff = Assert.Single(response.SemanticDiff);
        Assert.Equal(SyntheticLoadOrder.Lydia.ToString(), diff.Npc);
        var change = Assert.Single(diff.Changes);
        Assert.Equal("HairColor", change.Field);
        Assert.Equal("HCLF", change.Subrecord);
        Assert.False(string.IsNullOrWhiteSpace(change.Reason));
        Assert.NotEqual(change.Original, change.Exported);
        // The color 12,200,34 did not exist: the patch creates the CLFM and says so.
        Assert.Contains(change.Dependencies, d => d.EndsWith("(nuevo en este mod)", StringComparison.Ordinal));

        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        var fromManifest = manifest.RootElement.GetProperty("semanticDiff");
        Assert.Equal(1, fromManifest.GetArrayLength());
        Assert.Equal("HairColor", fromManifest[0].GetProperty("changes")[0].GetProperty("field").GetString());

        var readme = File.ReadAllText(Path.Combine(response.ModDirectory, "README.md"));
        Assert.Contains("Cambios respecto al registro ganador", readme, StringComparison.Ordinal);
        Assert.Contains("HairColor", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void OutfitExport_DiffNamesExactlyTheDefaultOutfit()
    {
        var request = new ExportOutfitPatchRequest
        {
            PluginName = "SNS_SafePatch_Outfit.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, request, CancellationToken.None));

        var change = Assert.Single(Assert.Single(response.SemanticDiff).Changes);
        Assert.Equal("DefaultOutfit", change.Field);
        Assert.Equal("DOFT", change.Subrecord);
        Assert.Contains(change.Dependencies, d => d.Contains("(nuevo en este mod)", StringComparison.Ordinal));
    }

    public static TheoryData<string> UnrelatedChanges() => new()
    {
        "Race", "Configuration", "Voice", "Class", "Factions", "Keywords", "Items", "Packages", "ActorEffect", "Name", "AIData",
        "Template", "Perks", "DeathItem",
    };

    /// <summary>Applies an unrelated edit to a copy of Lydia, as a buggy builder might.</summary>
    private static void Tamper(Npc npc, string field)
    {
        var other = new FormKey(SyntheticLoadOrder.Skyrim, 0x0A0099);
        switch (field)
        {
            case "Race": npc.Race.SetTo(SyntheticLoadOrder.KhajiitRace); break;
            case "Configuration": npc.Configuration.MagickaOffset = (short)(npc.Configuration.MagickaOffset + 123); break;
            case "Voice": npc.Voice.SetTo(SyntheticLoadOrder.FemaleVoice); break;
            case "Class": npc.Class.SetTo(other); break;
            case "Factions": npc.Factions.Add(new RankPlacement { Faction = new FormLink<IFactionGetter>(other), Rank = 3 }); break;
            case "Keywords": npc.Keywords ??= []; npc.Keywords.Add(new FormLink<IKeywordGetter>(SyntheticLoadOrder.VampireKeyword)); break;
            case "Items":
                npc.Items ??= [];
                npc.Items.Add(new ContainerEntry { Item = new ContainerItem { Item = new FormLink<IItemGetter>(SyntheticLoadOrder.SteelCuirass), Count = 4 } });
                break;
            case "Packages": npc.Packages.Add(new FormLink<IPackageGetter>(other)); break;
            case "ActorEffect": npc.ActorEffect ??= []; npc.ActorEffect.Add(new FormLink<ISpellRecordGetter>(other)); break;
            case "Name": npc.Name = "Not Lydia"; break;
            case "AIData": npc.AIData.Aggression = Aggression.Frenzied; break;
            case "Template": npc.Template.SetTo(SyntheticLoadOrder.GuardTemplate); break;
            case "Perks": npc.Perks ??= []; npc.Perks.Add(new PerkPlacement { Perk = new FormLink<IPerkGetter>(other), Rank = 1 }); break;
            case "DeathItem": npc.DeathItem.SetTo(SyntheticLoadOrder.HousecarlOutfit); break;
            default: throw new ArgumentException(field);
        }
    }

    [Theory]
    [MemberData(nameof(UnrelatedChanges))]
    public void AnUnrelatedFieldChange_IsBlockedWhenOnlyTheHairColorWasRequested(string field)
    {
        var winner = LydiaWinner();
        var tampered = winner.DeepCopy();
        Tamper(tampered, field);

        var violations = new List<Issue>();
        var diff = NpcSafePatch.Check(winner, tampered, TestPatch,
            NpcSafePatch.AllowedForFace(new FaceAssignment { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), OnlyFields = ["HCLF"] }),
            "a hair color change", violations);

        var error = Assert.Single(violations, v => v.Code == "UnauthorizedNpcChange");
        Assert.Equal("error", error.Severity);
        Assert.Empty(diff.Changes);
    }

    [Fact]
    public void AnAllowedAndAForbiddenChangeTogether_ReportBoth()
    {
        var winner = LydiaWinner();
        var edited = winner.DeepCopy();
        Assert.NotEqual(OtherForm, winner.HairColor.FormKeyNullable);
        edited.HairColor.SetTo(OtherForm);
        edited.Race.SetTo(SyntheticLoadOrder.KhajiitRace);

        var violations = new List<Issue>();
        var diff = NpcSafePatch.Check(winner, edited, TestPatch,
            NpcSafePatch.AllowedForFace(new FaceAssignment { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), OnlyFields = ["HCLF"] }),
            "a hair color change", violations);

        Assert.Equal(["HairColor"], diff.Changes.Select(c => c.Field).ToArray());
        Assert.Single(violations);
        Assert.Contains("Race", violations[0].Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ALocalizedNameWrittenAsTheSameText_IsNotAChange()
    {
        // Skyrim.esm keeps names in string tables; a non-localized patch writes the resolved text.
        var winner = LydiaWinner();
        var original = winner.DeepCopy();
        original.Name = new Mutagen.Bethesda.Strings.TranslatedString(Mutagen.Bethesda.Strings.Language.Spanish, "Lydia");
        var exported = winner.DeepCopy();
        exported.Name = "Lydia";

        var violations = new List<Issue>();
        var diff = NpcSafePatch.Check(original, exported, TestPatch, new HashSet<string>(), "nothing", violations);
        Assert.Empty(violations);
        Assert.Empty(diff.Changes);

        exported.Name = "Otra";
        NpcSafePatch.Check(original, exported, TestPatch, new HashSet<string>(), "nothing", violations);
        Assert.Contains(violations, v => v.Message.Contains("Name", StringComparison.Ordinal));
    }

    [Fact]
    public void AnUnchangedCopy_HasNoDifferencesAtAll()
    {
        var winner = LydiaWinner();
        var violations = new List<Issue>();
        var diff = NpcSafePatch.Check(winner, winner.DeepCopy(), TestPatch, new HashSet<string>(), "nothing", violations);
        Assert.Empty(violations);
        Assert.Empty(diff.Changes);
    }

    [Fact]
    public void ACompleteFaceAllowsFaceFieldsButStillBlocksTheOutfit()
    {
        var winner = LydiaWinner();
        var edited = winner.DeepCopy();
        edited.Weight = winner.Weight + 5;
        Assert.NotEqual(OtherForm, winner.DefaultOutfit.FormKeyNullable);
        edited.DefaultOutfit.SetTo(OtherForm);

        var violations = new List<Issue>();
        var diff = NpcSafePatch.Check(winner, edited, TestPatch, NpcSafePatch.FaceFields, "a face overhaul", violations);

        Assert.Equal(["Weight"], diff.Changes.Select(c => c.Field).ToArray());
        Assert.Contains(violations, v => v.Code == "UnauthorizedNpcChange" && v.Message.Contains("DefaultOutfit", StringComparison.Ordinal));
    }

    [Fact]
    public void AnUnreadableMaskVerdict_IsADifference_NeverEqual()
    {
        Assert.Null(NpcSafePatch.MaskVerdict(null));
        Assert.True(NpcSafePatch.MaskVerdict(true));
        Assert.False(NpcSafePatch.MaskVerdict(false));
        Assert.False(NpcSafePatch.MaskVerdict(new { Something = 1 }));
        Assert.False(NpcSafePatch.MaskVerdict(new { Overall = "not a bool" }));
        Assert.True(NpcSafePatch.MaskVerdict(new { Overall = true }));
    }

    [Fact]
    public void RequestedFields_NarrowTheFaceFields()
    {
        var allowed = NpcSafePatch.AllowedForFace(new FaceAssignment
        {
            NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
            RequestedFields = ["FaceMorph", "Height", "Race"],
        });
        Assert.Equal(2, allowed.Count);
        Assert.Contains("FaceMorph", allowed);
        Assert.Contains("Height", allowed);
    }

    [Fact]
    public void ACompleteFaceWithRequestedFields_WritesOnlyWhatWasRequested()
    {
        // The studio sends the complete face; the user only moved sliders. Another weight and skin
        // tone in the values (round-trip drift) must not reach the record.
        var winner = LydiaWinner();
        var nif = Path.Combine(_out, "requested.nif");
        File.WriteAllBytes(nif, [0x4E, 0x49, 0x46, 0x03]);
        var faceGen = CatalogQueries.FaceGenFor(SyntheticLoadOrder.Lydia);
        var request = new ExportFaceOverhaulRequest
        {
            PluginName = "SNS_Requested.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Assignments =
            [
                new FaceAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    RequestedFields = ["FaceMorph"],
                    Values = new FaceValuesDto
                    {
                        Sliders = Enumerable.Repeat(0.3f, 18).ToList(),
                        Presets = [0, 0, 0, 0],
                        Weight = winner.Weight + 25f,
                        SkinTone = new AppearanceColor(10, 20, 30),
                        HairColor = new AppearanceColor(1, 2, 3),
                        Tints = [],
                    },
                    HeadParts = winner.HeadParts.Select(h => h.FormKey.ToString()).ToList(),
                    GeneratedAssets =
                    [
                        new GeneratedAsset { SourcePath = nif, DataPath = faceGen.GeometryPath, Sha256 = Sha256Of(nif), Kind = "facegen-geometry" },
                    ],
                },
            ],
        };

        var response = _state.Read(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, CancellationToken.None));
        var change = Assert.Single(Assert.Single(response.SemanticDiff).Changes);
        Assert.Equal("FaceMorph", change.Field);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
