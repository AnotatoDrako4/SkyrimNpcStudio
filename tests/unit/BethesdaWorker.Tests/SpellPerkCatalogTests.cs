using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// AG04 phase 3 (T11, T12, T14): the creation record picker offers real SPEL and PERK records of the indexed profile, typed,
/// bounded and by FormKey; and the structural validation of an NPC says so when a spell or perk is wrong, duplicated or of another type.
/// </summary>
public sealed class SpellPerkCatalogTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "sns-ag04-spells-" + Guid.NewGuid().ToString("N"));
    private readonly WorkerState _state = new();
    private readonly WorkerRpcTarget _rpc;

    private static readonly string[] Spells =
    [
        LabSyntheticFixture.SpellFirebolt.ToString(), LabSyntheticFixture.SpellIceSpike.ToString(), LabSyntheticFixture.SpellOakflesh.ToString(),
        LabSyntheticFixture.SpellFastHealing.ToString(), LabSyntheticFixture.SpellCloseWounds.ToString(), LabSyntheticFixture.SpellSteadfastWard.ToString(),
        LabSyntheticFixture.SpellInvisibility.ToString(),
    ];

    private static readonly string[] Perks =
    [
        LabSyntheticFixture.PerkShieldWall.ToString(), LabSyntheticFixture.PerkJuggernaut.ToString(), LabSyntheticFixture.PerkOverdraw.ToString(),
        LabSyntheticFixture.PerkDeadlyAim.ToString(), LabSyntheticFixture.PerkBackstab.ToString(), LabSyntheticFixture.PerkAugmentedFlames.ToString(),
        LabSyntheticFixture.PerkRecovery.ToString(), LabSyntheticFixture.PerkRegeneration.ToString(), LabSyntheticFixture.PerkArmsMan.ToString(),
    ];

    public SpellPerkCatalogTests()
    {
        Directory.CreateDirectory(_dir);
        LabSyntheticFixture.EnsureBasicFixture(_dir);
        _state.Index(new IndexRequest
        {
            ProfileKey = "spell-perk-tests",
            CacheDirectory = Path.Combine(_dir, "_cache"),
            Plugins = new[] { LabSyntheticFixture.Skyrim.FileName, LabSyntheticFixture.FollowerVoiceAddon.FileName }
                .Select(n => new LoadOrderEntry { FileName = n, Path = Path.Combine(_dir, n) }).ToArray(),
        }, _ => { }, CancellationToken.None);
        _rpc = new WorkerRpcTarget(_state, () => { }, (_, _) => { });
    }

    private static readonly JsonSerializerOptions Web = new(JsonSerializerDefaults.Web);

    private IReadOnlyList<CreationRecord> Search(string kind, string text = "", int limit = 100) =>
        _rpc.SearchCreationRecords(new CreationSearchRequest { Kind = kind, Text = text, Limit = limit });

    // T11
    [Fact]
    public void SearchSpell_ReturnsOnlySpellRecords_ByFormKey()
    {
        var found = Search("spell");
        Assert.NotEmpty(found);
        Assert.All(found, r =>
        {
            Assert.Contains(r.FormKey, Spells);
            Assert.Equal("spell", r.Kind);
        });
        Assert.Equal(Spells.Length, found.Count);
        Assert.Equal(found.Count, found.Select(r => r.FormKey).Distinct().Count());
        // Never a perk, armor, class or anything else.
        Assert.DoesNotContain(found, r => Perks.Contains(r.FormKey));
    }

    // T12
    [Fact]
    public void SearchPerk_ReturnsOnlyPerkRecords_ByFormKey()
    {
        var found = Search("perk");
        Assert.NotEmpty(found);
        Assert.All(found, r => Assert.Contains(r.FormKey, Perks));
        Assert.Equal(Perks.Length, found.Count);
        Assert.DoesNotContain(found, r => Spells.Contains(r.FormKey));
    }

    [Fact]
    public void TheSearchFiltersByEditorId_IgnoringCase_AndHonorsTheLimit()
    {
        var all = Search("spell");
        var one = all[0];
        var byText = Search("spell", one.EditorId.ToUpperInvariant());
        Assert.Contains(byText, r => r.FormKey == one.FormKey);
        Assert.All(byText, r => Assert.Contains(one.EditorId, r.EditorId, StringComparison.OrdinalIgnoreCase));
        Assert.Empty(Search("spell", "no-such-spell-xyz"));
        Assert.Single(Search("spell", limit: 1));
        Assert.Single(Search("perk", limit: 0));                  // the lower bound is 1, not "everything"
        Assert.True(Search("perk", limit: 100000).Count <= 200);  // and the upper bound is 200
    }

    [Fact]
    public void AnUnknownKind_OrTooLongText_IsRefused_NotAnEmptyAnswer()
    {
        Assert.Equal(ErrorCodes.InvalidArgument, ErrorCodeOf(() => Search("spel")));
        Assert.Equal(ErrorCodes.InvalidArgument, ErrorCodeOf(() => Search("perk", new string('x', 161))));
    }

    [Fact]
    public void BeforeIndexing_TheSearchAnswersNotIndexed()
    {
        var bare = new WorkerRpcTarget(new WorkerState(), () => { }, (_, _) => { });
        Assert.Equal(ErrorCodes.NotIndexed, ErrorCodeOf(() => bare.SearchCreationRecords(new CreationSearchRequest { Kind = "perk" })));
    }

    private static int? ErrorCodeOf(Action call)
    {
        try { call(); return null; }
        catch (LocalRpcException ex) { return ex.ErrorCode; }
    }

    /// <summary>The lab's sample NPC, as the worker's own draft type (the two are 1:1 on the wire), with the given spells and perks.</summary>
    private static NativeNpcDraft Draft(string[] spells, string[] perks)
    {
        var json = JsonSerializer.Serialize(LabSyntheticFixture.CreateSampleDraft(), Web);
        var draft = JsonSerializer.Deserialize<NativeNpcDraft>(json, Web)!;
        return draft with { SpellFormKeys = spells, PerkFormKeys = perks, Face = null, Placement = null, OutfitArmorFormKeys = [], Inventory = [], HeadPartFormKeys = [], PackageFormKeys = [] };
    }

    private CreationValidateResponse Validate(NativeNpcDraft draft) =>
        _rpc.ValidateNativeNpc(new CreationValidateRequest { Npc = draft }, CancellationToken.None).GetAwaiter().GetResult();

    // T14 (structural half: the spells and perks of the draft are checked against the profile)
    [Fact]
    public void Validation_AcceptsRealSpellsAndPerks_AndFlagsDuplicatesMissingAndWrongTypeRecords()
    {
        var ok = Validate(Draft([Spells[0], Spells[1]], [Perks[0]]));
        Assert.DoesNotContain(ok.Issues, i => i.Severity == "error" && (i.Code.Contains("Spell", StringComparison.Ordinal) || i.Code.Contains("Perk", StringComparison.Ordinal)
            || i.Subject == Spells[0] || i.Subject == Perks[0]));

        var duplicated = Validate(Draft([Spells[0], Spells[0]], [Perks[0], Perks[0]]));
        Assert.Contains(duplicated.Issues, i => i.Code == "SpellDuplicate" && i.Severity == "error");
        Assert.Contains(duplicated.Issues, i => i.Code == "PerkDuplicate" && i.Severity == "error");
        Assert.False(duplicated.TechnicallyExportable);

        var wrongType = Validate(Draft([Perks[0]], [Spells[0]]));   // a perk where a spell goes, and the other way round
        Assert.Contains(wrongType.Issues, i => i.Severity == "error" && i.Subject == Perks[0]);
        Assert.Contains(wrongType.Issues, i => i.Severity == "error" && i.Subject == Spells[0]);
        Assert.False(wrongType.TechnicallyExportable);

        var missing = Validate(Draft(["0FFFFF:Skyrim.esm"], []));
        Assert.Contains(missing.Issues, i => i.Severity == "error" && i.Subject == "0FFFFF:Skyrim.esm");
        Assert.Contains("0FFFFF:Skyrim.esm", missing.MissingReferences);

        // Whatever the spells and perks say, the playable export stays blocked while native FaceGen is pending.
        Assert.False(ok.Exportable);
        Assert.Contains(ok.Issues, i => i.Code == "NativeFaceGenUnavailable");
    }

    public void Dispose()
    {
        _state.Dispose();
        try { Directory.Delete(_dir, recursive: true); } catch (IOException) { }
    }
}
