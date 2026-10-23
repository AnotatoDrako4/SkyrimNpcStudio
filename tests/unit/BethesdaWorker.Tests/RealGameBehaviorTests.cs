using System.Text;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Worker.Contracts;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// AG04: the follower behavior must work on the real game's records, not only on the lab's synthetic fixture. These tests read the five
/// official masters of the Steam install, read-only (nothing is written there), and return without asserting when the game is not installed
/// (SNS_GAME_DATA overrides the Data folder). SNS_ARCHETYPE_PROBE_REPORT, when set, receives the per-archetype link report.
/// They exist because the first real-profile run showed that no built-in archetype could ever resolve: the templates pointed at ids that
/// are not in Skyrim.esm (or at unrelated records), and the voice analysis looked for FLSTs the real game does not have.
/// </summary>
public sealed class RealGameBehaviorTests : IDisposable
{
    private static readonly string[] Masters = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"];
    private readonly string _cache = Path.Combine(Path.GetTempPath(), "sns-ag04-real-" + Guid.NewGuid().ToString("N"));

    private static string? DataFolder()
    {
        var configured = Environment.GetEnvironmentVariable("SNS_GAME_DATA");
        var candidate = !string.IsNullOrWhiteSpace(configured) ? configured : @"D:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data";
        return Masters.All(m => File.Exists(Path.Combine(candidate, m))) ? candidate : null;
    }

    private WorkerState Index(string data, params (string FileName, string Path)[] extraPlugins)
    {
        var state = new WorkerState();
        state.Index(new IndexRequest
        {
            ProfileKey = "real-masters" + (extraPlugins.Length > 0 ? "+" + string.Join('+', extraPlugins.Select(p => p.FileName)) : ""),
            CacheDirectory = _cache,
            Plugins = Masters.Select(n => new LoadOrderEntry { FileName = n, Path = Path.Combine(data, n) })
                .Concat(extraPlugins.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path })).ToArray(),
        }, _ => { }, CancellationToken.None);
        return state;
    }

    /// <summary>
    /// T03 on real masters: a plugin that overrides the real VoicesFollowerAll FLST and drops a voice changes the evidence for that voice,
    /// and the evidence names the plugin that did it. The plugin is generated in a temp folder; the official masters are only read.
    /// </summary>
    [Fact]
    public void ARealFormListOverride_ChangesTheVoiceEvidence_AndNamesThePluginThatDidIt()
    {
        var data = DataFolder();
        if (data is null) return;
        var dir = Path.Combine(Path.GetTempPath(), "sns-ag05-override-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            var skyrim = ModKey.FromFileName("Skyrim.esm");
            var overrideKey = ModKey.FromFileName("AG05VoiceOverride.esp");
            string voice;
            using (var overlay = SkyrimMod.CreateFromBinaryOverlay(new ModPath(skyrim, Path.Combine(data, "Skyrim.esm")), SkyrimRelease.SkyrimSE))
            {
                var all = overlay.FormLists.First(f => f.EditorID == "VoicesFollowerAll");
                var target = overlay.VoiceTypes.First(v => v.EditorID == "FemaleEvenToned");
                voice = target.FormKey.ToString();
                var esp = new SkyrimMod(overrideKey, SkyrimRelease.SkyrimSE);
                var overridden = esp.FormLists.GetOrAddAsOverride(all);
                overridden.Items.Clear();   // the override keeps nothing: every follower voice of the list is gone
                esp.WriteToBinary(Path.Combine(dir, overrideKey.FileName), new Mutagen.Bethesda.Plugins.Binary.Parameters.BinaryWriteParameters
                {
                    MastersListOrdering = new Mutagen.Bethesda.Plugins.Binary.Parameters.MastersListOrderingByLoadOrder([skyrim]),
                });
            }

            string Analyze(WorkerState state) => state.Read(c =>
            {
                var r = VoiceDialogueAnalyzer.AnalyzeVoice(c.BehaviorContext, voice, draftIsFemale: true);
                return $"{r.FollowerDialogueClassification}|{r.HasStaticFollowerListEvidence}|{r.FollowerListProvenance?.WinningPlugin}|{r.FollowerListProvenance?.RemovedByPlugin}|{r.FollowerListProvenance?.PresentInWinner}";
            });

            string before, after;
            using (var plain = Index(data)) before = Analyze(plain);
            using (var withOverride = Index(data, (overrideKey.FileName, Path.Combine(dir, overrideKey.FileName)))) after = Analyze(withOverride);

            var b = before.Split('|');
            var a = after.Split('|');
            Assert.Equal("True", b[1]);                                   // the real game: a vanilla follower voice has static evidence
            Assert.NotEqual(b[0], a[0]);                                  // with the override its classification is different
            Assert.Equal("False", a[4]);                                  // the winner no longer lists the voice
            Assert.Equal(overrideKey.FileName.String, a[2]);              // and the winner is the override plugin
            Assert.Equal(overrideKey.FileName.String, a[3]);              // which the evidence names as the one that removed it
        }
        finally
        {
            try { Directory.Delete(dir, recursive: true); } catch (IOException) { }
        }
    }

    [Fact]
    public void EveryBuiltInArchetype_ResolvesAgainstTheRealMasters()
    {
        var data = DataFolder();
        if (data is null) return;   // no Skyrim install on this machine: nothing to read
        using var state = Index(data);
        var report = new StringBuilder();
        var unresolved = new List<string>();
        state.Read(c =>
        {
            var ctx = c.BehaviorContext;
            foreach (var template in ArchetypePlanner.GetAllArchetypes())
            {
                var verdict = ArchetypePlanner.ValidateArchetype(ctx, template);
                report.AppendLine($"== {template.Id}: allLinksResolved={verdict.AllLinksResolved}");
                foreach (var link in verdict.Links)
                {
                    report.AppendLine($"   {(link.Resolved ? "ok        " : "UNRESOLVED")} {link.Category} {link.FormKey} {link.ExpectedRecordType} {link.EditorId} {link.WinningPlugin}");
                    if (!link.Resolved) unresolved.Add($"{template.Id}:{link.Category}:{link.FormKey}");
                }
            }
            return 0;
        });
        var path = Environment.GetEnvironmentVariable("SNS_ARCHETYPE_PROBE_REPORT");
        if (!string.IsNullOrEmpty(path)) File.WriteAllText(path, report.ToString());
        Assert.True(unresolved.Count == 0, "Archetype links that do not exist in the real masters:\n" + string.Join("\n", unresolved));
    }

    /// <summary>A template's class must be a class and its combat style a combat style that mean what the archetype says (by EditorID).</summary>
    [Fact]
    public void TheArchetypeLinks_PointAtTheRecordsTheyAreNamedAfter()
    {
        var data = DataFolder();
        if (data is null) return;
        using var state = Index(data);
        state.Read(c =>
        {
            string EditorIdOf(string formKey)
            {
                var key = FormKey.Factory(formKey);
                return c.Session.Plugins.Reverse().SelectMany(p => p.Mod.EnumerateMajorRecords()).First(r => r.FormKey == key).EditorID ?? "";
            }

            var expectedStyles = new Dictionary<string, string>
            {
                ["defensive-warrior"] = "csHumanTankLvl1", ["archer"] = "csHumanMissile", ["assassin"] = "csWEAssassinMelee",
                ["destruction-mage"] = "csHumanMagic", ["healer"] = "csWEMageShieldStaff", ["spellsword"] = "csSpellsword",
            };
            foreach (var template in ArchetypePlanner.GetAllArchetypes())
            {
                Assert.Equal(expectedStyles[template.Id], EditorIdOf(template.CombatStyleFormKey));
                Assert.Contains("Combat", EditorIdOf(template.ClassFormKey), StringComparison.Ordinal);
            }

            var spellsOfMage = ArchetypePlanner.GetById("destruction-mage").SpellFormKeys.Select(EditorIdOf).ToArray();
            Assert.Contains("Firebolt", spellsOfMage);
            Assert.Contains("IceSpike", spellsOfMage);
            var perksOfArcher = ArchetypePlanner.GetById("archer").PerkFormKeys.Select(EditorIdOf).ToArray();
            Assert.Contains("DeadlyAim", perksOfArcher);
            Assert.Contains("Overdraw00", perksOfArcher);
            return 0;
        });
    }

    /// <summary>
    /// The real game has VoicesFollowerAll / VoicesMarriageAll (not "VoicesFollower"), so the analysis must find them. A vanilla follower voice
    /// is static evidence for followers; a creature voice is in neither list and is never reported as recruitable.
    /// </summary>
    [Fact]
    public void VoiceAnalysis_FindsTheRealVoiceLists_AndNeverGuessesForAVoiceThatIsNotInThem()
    {
        var data = DataFolder();
        if (data is null) return;
        using var state = Index(data);
        state.Read(c =>
        {
            var ctx = c.BehaviorContext;
            string Voice(string editorId) => c.Session.Plugins.SelectMany(p => p.Mod.VoiceTypes).First(v => v.EditorID == editorId).FormKey.ToString();

            var follower = VoiceDialogueAnalyzer.AnalyzeVoice(ctx, Voice("FemaleEvenToned"), draftIsFemale: true);
            Assert.True(follower.VoiceResolved);
            Assert.True(follower.FollowerListProvenance?.ListFound, "a real follower list must be found in the real game");
            Assert.True(follower.HasStaticFollowerListEvidence, "FemaleEvenToned is in the vanilla follower voices");
            Assert.True(follower.MarriageListProvenance?.ListFound, "a real marriage list must be found in the real game");
            Assert.Equal("REQUIRES_IN_GAME_TEST", follower.RuntimeVerificationState);

            var creature = VoiceDialogueAnalyzer.AnalyzeVoice(ctx, Voice("CrDragonVoice"));
            Assert.False(creature.HasStaticFollowerListEvidence);
            Assert.False(creature.HasStaticMarriageListEvidence);
            return 0;
        });
    }

    public void Dispose()
    {
        try { Directory.Delete(_cache, recursive: true); } catch (IOException) { }
    }
}
