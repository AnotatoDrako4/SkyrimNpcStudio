using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using Xunit;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Tests;

public sealed class PhaseCCombatAndBatchTests : IDisposable
{
    private readonly string _fixtureDir;

    public PhaseCCombatAndBatchTests()
    {
        _fixtureDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-c-" + Guid.NewGuid().ToString("N"));
        LabSyntheticFixture.EnsureBasicFixture(_fixtureDir);
    }

    [Fact]
    public void C01_ClassResolvedByFormKey_ExtractsRealMutagenWeightsAndPluginProvenance()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var issues = new List<BehaviorIssue>();
        var masters = new HashSet<Mutagen.Bethesda.Plugins.ModKey>();

        var cls = CombatClassCatalog.ResolveClass(ctx, LabSyntheticFixture.ClassDefensiveWarrior.ToString(), issues, masters);

        Assert.Empty(issues);
        Assert.True(cls.Resolved);
        Assert.Equal("CombatWarrior1H", cls.EditorId);
        Assert.Equal("Defensive Warrior", cls.Name);
        Assert.Equal("Skyrim.esm", cls.DefiningPlugin);
        Assert.Equal("Skyrim.esm", cls.WinningPlugin);
        Assert.Equal("OneHanded", cls.PrimaryTrainingSkill);
        Assert.Equal(90, cls.MaxTrainingLevel);
        Assert.Equal((byte)3, cls.SkillWeights["OneHanded"]);
        Assert.Equal((byte)3, cls.SkillWeights["Block"]);
        Assert.Equal((byte)3, cls.StatWeights["Health"]);
    }

    [Fact]
    public void C02_CombatStyleResolvedByFormKey_ExtractsRealMultipliers()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var issues = new List<BehaviorIssue>();
        var masters = new HashSet<Mutagen.Bethesda.Plugins.ModKey>();

        var style = CombatClassCatalog.ResolveCombatStyle(ctx, LabSyntheticFixture.StyleArcher.ToString(), issues, masters, required: true);

        Assert.NotNull(style);
        Assert.Empty(issues);
        Assert.True(style!.Resolved);
        Assert.Equal("csHumanMissile", style.EditorId);
        Assert.Equal("Skyrim.esm", style.DefiningPlugin);
        Assert.Equal(0.65f, style.OffensiveMult, 3);
        Assert.Equal(1.0f, style.RangedScoreMult, 3);
        Assert.Equal(0.8f, style.AvoidThreatChance, 3);
    }

    [Fact]
    public void C03_DisabledPluginRecords_AreExcludedFromCatalogAndRejectedOnResolution()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var activeClasses = CombatClassCatalog.EnumerateActiveClasses(ctx);
        var activeStyles = CombatClassCatalog.EnumerateActiveCombatStyles(ctx);

        Assert.DoesNotContain(activeClasses, c => c.FormKey == LabSyntheticFixture.ClassDisabledBerserker.ToString());
        Assert.DoesNotContain(activeStyles, s => s.FormKey == LabSyntheticFixture.StyleDisabledBerserker.ToString());

        var issues = new List<BehaviorIssue>();
        var masters = new HashSet<Mutagen.Bethesda.Plugins.ModKey>();
        var resolvedCls = CombatClassCatalog.ResolveClass(ctx, LabSyntheticFixture.ClassDisabledBerserker.ToString(), issues, masters);

        Assert.False(resolvedCls.Resolved);
        Assert.Contains(issues, i => i.Code == "ClassFromDisabledPlugin" && i.Severity == ValidationSeverity.ERROR);
    }

    [Fact]
    public void C04_WarriorArchetype_ResolvesAllLinksCleanlyAndSixArchetypesAreAvailable()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var all = ArchetypePlanner.GetAllArchetypes();
        Assert.Equal(6, all.Count);

        foreach (var archetype in all)
        {
            var report = ArchetypePlanner.ValidateArchetype(ctx, archetype);
            Assert.True(report.AllLinksResolved, $"Archetype '{archetype.Id}' had unresolved links.");
            Assert.Empty(report.Issues);
        }

        var warrior = ArchetypePlanner.GetById("defensive-warrior");
        var warriorReport = ArchetypePlanner.ValidateArchetype(ctx, warrior);
        Assert.True(warriorReport.AllLinksResolved);
        Assert.True(warriorReport.ResolvedClass.Resolved);
        Assert.NotNull(warriorReport.ResolvedCombatStyle);
        Assert.True(warriorReport.ResolvedCombatStyle!.Resolved);
        Assert.Contains("Skyrim.esm", warriorReport.RequiredMasters);
    }

    [Fact]
    public void C05_HealerArchetype_ResolvesAllSpellsAndPerksCleanly()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var healer = ArchetypePlanner.GetById("healer");
        var report = ArchetypePlanner.ValidateArchetype(ctx, healer);

        Assert.True(report.AllLinksResolved);
        Assert.Empty(report.Issues);

        var spellLinks = report.Links.Where(l => l.Category == "Spell").ToList();
        var perkLinks = report.Links.Where(l => l.Category == "Perk").ToList();
        Assert.Equal(4, spellLinks.Count);
        Assert.All(spellLinks, s => Assert.True(s.Resolved));
        Assert.Equal(2, perkLinks.Count);
        Assert.All(perkLinks, p => Assert.True(p.Resolved));
    }

    [Fact]
    public void C06_ArchetypeWithBrokenLink_GeneratesConcreteTypedError()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var healer = ArchetypePlanner.GetById("healer");
        var brokenArchetype = healer with
        {
            SpellFormKeys = [.. healer.SpellFormKeys, "0DEAD1:Skyrim.esm"],
            PerkFormKeys = [.. healer.PerkFormKeys, LabSyntheticFixture.ClassDisabledBerserker.ToString()]
        };

        var report = ArchetypePlanner.ValidateArchetype(ctx, brokenArchetype);

        Assert.False(report.AllLinksResolved);
        Assert.Contains(report.Issues, i => i.Code == "ArchetypeBrokenLink" && i.TargetFormKey == "0DEAD1:Skyrim.esm");
        Assert.Contains(report.Issues, i => i.Code == "ArchetypeBrokenLink" && i.SourcePlugin == "DisabledCombatMod.esp");
    }

    [Fact]
    public void B01_DryRunWithScopedCategories_ModifiesOnlyRequestedFieldsAndNeverTouchesAppearance()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var source = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_ScopedTest",
            name: "Scoped Candidate") with
        {
            HeadPartFormKeys = ["051623:Skyrim.esm"],
            HairColorFormKey = "0A0434:Skyrim.esm",
            HeadTextureFormKey = "03B522:Skyrim.esm",
            Height = 1.07f,
            Weight = 73.0f,
        };

        var archer = ArchetypePlanner.GetById("archer");
        // Only request 'class' and 'combatStyle' categories
        var report = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, [source], archer, ["class", "combatStyle"]);

        var entry = Assert.Single(report.Entries);
        Assert.All(entry.Changes, c => Assert.True(c.Category is "class" or "combatStyle"));
        Assert.Equal(archer.ClassFormKey, entry.ProposedDraft.ClassFormKey);
        Assert.Equal(archer.CombatStyleFormKey, entry.ProposedDraft.CombatStyleFormKey);

        // Unrequested behavior categories must remain untouched
        Assert.Equal(source.Health, entry.ProposedDraft.Health);
        Assert.Equal(source.OutfitArmorFormKeys, entry.ProposedDraft.OutfitArmorFormKeys);
        Assert.Equal(source.Inventory, entry.ProposedDraft.Inventory);
        Assert.Equal(source.PerkFormKeys, entry.ProposedDraft.PerkFormKeys);

        // Protected appearance/identity fields must NEVER change
        Assert.Equal(source.Name, entry.ProposedDraft.Name);
        Assert.Equal(source.EditorId, entry.ProposedDraft.EditorId);
        Assert.Equal(source.Height, entry.ProposedDraft.Height);
        Assert.Equal(source.Weight, entry.ProposedDraft.Weight);
        Assert.Equal(source.HeadPartFormKeys, entry.ProposedDraft.HeadPartFormKeys);
        Assert.Equal(source.HairColorFormKey, entry.ProposedDraft.HairColorFormKey);
        Assert.Equal(source.HeadTextureFormKey, entry.ProposedDraft.HeadTextureFormKey);
    }

    [Fact]
    public void B02_SourceDraftHash_RemainsUnchangedBeforeAndAfterDryRun()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var source = LabSyntheticFixture.CreateSampleDraft();
        var initialHash = SandboxGuard.ComputeObjectCanonicalSha256(source);

        var mage = ArchetypePlanner.GetById("destruction-mage");
        var report = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, [source], mage);

        var postHash = SandboxGuard.ComputeObjectCanonicalSha256(source);
        Assert.Equal(initialHash, postHash);
        Assert.True(report.AllSourcesUntouched);
        var entry = Assert.Single(report.Entries);
        Assert.True(entry.SourceUntouched);
        Assert.Equal(initialHash, entry.SourceSha256Before);
        Assert.Equal(initialHash, entry.SourceSha256After);
        Assert.NotEqual(initialHash, entry.ProposedSha256);
    }

    [Fact]
    public void B03_BatchOfTenSyntheticDrafts_ProducesTenIndependentDiffs()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var batch = Enumerable.Range(1, 10)
            .Select(i => LabSyntheticFixture.CreateSampleDraft(
                editorId: $"SNS_BatchCandidate_{i:D2}",
                name: $"Batch Candidate {i:D2}") with
            {
                Health = 100 + (i * 10),
                Weight = 10f * i,
            })
            .ToList();

        var spellsword = ArchetypePlanner.GetById("spellsword");
        var report = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, batch, spellsword);

        Assert.Equal(10, report.TotalDrafts);
        Assert.Equal(10, report.Entries.Count);
        Assert.True(report.AllSourcesUntouched);
        Assert.Equal(10, report.Entries.Select(e => e.SourceSha256Before).Distinct().Count());
        Assert.Equal(10, report.Entries.Select(e => e.ProposedSha256).Distinct().Count());

        for (int i = 0; i < 10; i++)
        {
            var e = report.Entries[i];
            Assert.Equal($"SNS_BatchCandidate_{i + 1:D2}", e.EditorId);
            Assert.NotEmpty(e.Changes);
            Assert.Equal(spellsword.ClassFormKey, e.ProposedDraft.ClassFormKey);
            Assert.Equal(10f * (i + 1), e.ProposedDraft.Weight); // Weight preserved per draft
        }
    }

    [Fact]
    public void B04_UndoBatch_RestoresAllTenDraftsWithBitExactSha256Match()
    {
        using var ctx = PluginContext.OpenDirectory(_fixtureDir);
        var batch = Enumerable.Range(1, 10)
            .Select(i => LabSyntheticFixture.CreateSampleDraft(
                editorId: $"SNS_UndoCandidate_{i:D2}",
                name: $"Undo Candidate {i:D2}") with
            {
                Health = 120 + (i * 5),
                Magicka = 90 + (i * 7),
            })
            .ToList();

        var expectedHashes = batch.Select(SandboxGuard.ComputeObjectCanonicalSha256).ToList();

        var assassin = ArchetypePlanner.GetById("assassin");
        var report = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, batch, assassin);
        var restored = BulkArchetypeEngine.UndoBatch(report);

        Assert.Equal(10, restored.Count);
        for (int i = 0; i < 10; i++)
        {
            var actualHash = SandboxGuard.ComputeObjectCanonicalSha256(restored[i]);
            Assert.Equal(expectedHashes[i], actualHash);
            Assert.Equal(batch[i].EditorId, restored[i].EditorId);
            Assert.Equal(batch[i].Health, restored[i].Health);
            Assert.Equal(batch[i].ClassFormKey, restored[i].ClassFormKey);
        }
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_fixtureDir)) Directory.Delete(_fixtureDir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
