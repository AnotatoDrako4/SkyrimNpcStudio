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
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// body.inspectNpcs on synthetic data: the synthetic load order plus BodyLab.esp, a plugin built here with the
/// body-relevant cases (own skin, race skin fallback, a Traits template chain, a cycle, a leveled template,
/// a child race, keywords/factions/class/combat style). Every record is synthetic.
/// </summary>
public sealed class BodyQueriesTests : IDisposable
{
    private static readonly ModKey BodyLab = ModKey.FromFileName("BodyLab.esp");
    private const string FemaleBody = @"Actors\Character\Character Assets\FemaleBody_1.nif";
    private const string MaleBody = @"Actors\Character\Character Assets\MaleBody_1.nif";
    private const string CustomFemaleBody = @"Actors\Character\BodyLab\FemaleBodyCustom_1.nif";

    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _cache = Path.Combine(Path.GetTempPath(), "sns-body-" + Guid.NewGuid().ToString("N"));

    private FormKey _eira, _bjorn, _child, _traitsBase, _traitsMid, _templated, _cycleA, _cycleB, _leveledUser, _leveledList;
    private FormKey _bodyRace, _childRace, _skinNaked, _skinCustom, _nakedTorso, _nakedTorsoKhajiit, _customTorso;
    private FormKey _actorTypeNpc, _raceKeyword, _combatStyle;

    public BodyQueriesTests()
    {
        WriteBodyLab();
        _state.Index(new IndexRequest
        {
            ProfileKey = "body-tests-" + Guid.NewGuid().ToString("N"),
            CacheDirectory = _cache,
            Plugins = _lo.LoadOrder.Append((BodyLab.FileName.String, Path.Combine(_lo.DataDirectory, BodyLab.FileName)))
                .Select(p => new LoadOrderEntry { FileName = p.Item1, Path = p.Item2 }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private void WriteBodyLab()
    {
        var mod = new SkyrimMod(BodyLab, SkyrimRelease.SkyrimSE);

        var actorType = mod.Keywords.AddNew("ActorTypeNPC");
        _actorTypeNpc = actorType.FormKey;
        var raceKeyword = mod.Keywords.AddNew("BodyLabRaceKeyword");
        _raceKeyword = raceKeyword.FormKey;
        var style = mod.CombatStyles.AddNew("csHumanMeleeLvl1");
        _combatStyle = style.FormKey;

        var bodyRace = mod.Races.AddNew("BodyLabNordRace");
        bodyRace.Name = "Body Nord";
        bodyRace.Flags = Race.Flag.Playable;
        bodyRace.ArmorRace.SetTo(SyntheticLoadOrder.DefaultRace);
        bodyRace.Keywords = new ExtendedList<IFormLinkGetter<IKeywordGetter>> { raceKeyword.ToLink() };
        _bodyRace = bodyRace.FormKey;

        var childRace = mod.Races.AddNew("BodyLabChildRace");
        childRace.Flags = Race.Flag.Child;
        _childRace = childRace.FormKey;

        var torso = Addon(mod, "NakedTorso", SyntheticLoadOrder.DefaultRace, [bodyRace.FormKey, childRace.FormKey], MaleBody, FemaleBody);
        _nakedTorso = torso.FormKey;
        var khajiit = Addon(mod, "NakedTorsoKhajiit", SyntheticLoadOrder.KhajiitRace, [], @"Actors\Character\Character Assets\MaleBodyKhajiit.nif", null);
        _nakedTorsoKhajiit = khajiit.FormKey;
        var custom = Addon(mod, "BodyLabCustomTorso", bodyRace.FormKey, [], null, CustomFemaleBody);
        _customTorso = custom.FormKey;

        var naked = mod.Armors.AddNew("SkinNaked");
        naked.BodyTemplate = new BodyTemplate { FirstPersonFlags = BipedObjectFlag.Body };
        naked.Armature.Add(torso);
        naked.Armature.Add(khajiit);
        _skinNaked = naked.FormKey;
        var customSkin = mod.Armors.AddNew("SkinBodyLabCustom");
        customSkin.BodyTemplate = new BodyTemplate { FirstPersonFlags = BipedObjectFlag.Body };
        customSkin.Armature.Add(custom);
        _skinCustom = customSkin.FormKey;

        bodyRace.Skin.SetTo(naked);
        childRace.Skin.SetTo(naked);

        var eira = Npc(mod, "BodyLabEira", "Eira", bodyRace.FormKey, female: true);
        eira.Configuration.Flags |= NpcConfiguration.Flag.Unique;
        eira.WornArmor.SetTo(customSkin);
        eira.Keywords = new ExtendedList<IFormLinkGetter<IKeywordGetter>> { actorType.ToLink() };
        eira.Class.SetTo(SyntheticLoadOrder.WarriorClass);
        eira.CombatStyle.SetTo(style);
        eira.Factions.Add(new RankPlacement { Faction = new FormLink<IFactionGetter>(SyntheticLoadOrder.HousecarlFaction), Rank = 2 });
        eira.HeadParts.Add(new FormLink<IHeadPartGetter>(SyntheticLoadOrder.HairNord));
        _eira = eira.FormKey;

        _bjorn = Npc(mod, "BodyLabBjorn", "Bjorn", bodyRace.FormKey, female: false).FormKey;
        _child = Npc(mod, "BodyLabChild", "Child", childRace.FormKey, female: true).FormKey;

        // Templated -> TraitsMid -> TraitsBase: race, sex and skin come from TraitsBase.
        var traitsBase = Npc(mod, "BodyLabTraitsBase", "Base", bodyRace.FormKey, female: true);
        traitsBase.WornArmor.SetTo(customSkin);
        _traitsBase = traitsBase.FormKey;
        var traitsMid = Npc(mod, "BodyLabTraitsMid", "Mid", SyntheticLoadOrder.KhajiitRace, female: false);
        traitsMid.Template.SetTo(traitsBase.FormKey);
        traitsMid.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits;
        _traitsMid = traitsMid.FormKey;
        var templated = Npc(mod, "BodyLabTemplated", "Templated", SyntheticLoadOrder.KhajiitRace, female: false);
        templated.Template.SetTo(traitsMid.FormKey);
        templated.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits | NpcConfiguration.TemplateFlag.Inventory;
        _templated = templated.FormKey;

        var cycleA = Npc(mod, "BodyLabCycleA", "A", bodyRace.FormKey, female: false);
        var cycleB = Npc(mod, "BodyLabCycleB", "B", bodyRace.FormKey, female: false);
        cycleA.Template.SetTo(cycleB.FormKey);
        cycleB.Template.SetTo(cycleA.FormKey);
        cycleA.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits;
        cycleB.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits;
        _cycleA = cycleA.FormKey;
        _cycleB = cycleB.FormKey;

        var list = mod.LeveledNpcs.AddNew("BodyLabLeveledBandit");
        list.Entries = [new LeveledNpcEntry { Data = new LeveledNpcEntryData { Level = 1, Count = 1, Reference = new FormLink<INpcSpawnGetter>(traitsBase.FormKey) } }];
        _leveledList = list.FormKey;
        var leveledUser = Npc(mod, "BodyLabLeveledUser", "Bandit", bodyRace.FormKey, female: false);
        leveledUser.Template.SetTo(list.FormKey);
        leveledUser.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Traits;
        _leveledUser = leveledUser.FormKey;

        mod.WriteToBinary(Path.Combine(_lo.DataDirectory, BodyLab.FileName));
    }

    private static ArmorAddon Addon(SkyrimMod mod, string editorId, FormKey race, FormKey[] additional, string? male, string? female)
    {
        var addon = mod.ArmorAddons.AddNew(editorId);
        addon.BodyTemplate = new BodyTemplate { FirstPersonFlags = BipedObjectFlag.Body | BipedObjectFlag.Hands };
        addon.Race.SetTo(race);
        foreach (var r in additional) addon.AdditionalRaces.Add(new FormLink<IRaceGetter>(r));
        addon.WorldModel = new GenderedItem<Model?>(
            male is null ? null : new Model { File = male },
            female is null ? null : new Model { File = female });
        return addon;
    }

    private static Npc Npc(SkyrimMod mod, string editorId, string name, FormKey race, bool female)
    {
        var npc = mod.Npcs.AddNew(editorId);
        npc.Name = name;
        npc.Race.SetTo(race);
        npc.Configuration.Flags = female ? NpcConfiguration.Flag.Female : 0;
        return npc;
    }

    private BodyInspectResponse Inspect(BodyInspectRequest request) =>
        _state.Read(c => BodyQueries.Inspect(c, request, TestContext.Current.CancellationToken));

    private BodyNpcInfo One(FormKey key)
    {
        var response = Inspect(new BodyInspectRequest { FormKeys = [key.ToString()] });
        Assert.Empty(response.Missing);
        return Assert.Single(response.Items);
    }

    [Fact]
    public void AFemaleNpcWithItsOwnSkinReportsTheNpcSkinAndTheFemaleBodyModel()
    {
        var eira = One(_eira);

        Assert.Equal(_eira.ToString(), eira.FormKey);
        Assert.Equal("female", eira.Sex);
        Assert.Equal("female", eira.EffectiveSex);
        Assert.Equal("BodyLabEira", eira.EditorId);
        Assert.Equal("Eira", eira.Name);
        Assert.Equal("BodyLab.esp", eira.OriginPlugin);
        Assert.Equal("BodyLab.esp", eira.WinningPlugin);
        Assert.True(eira.IsUnique);
        Assert.False(eira.UsesTemplate);
        Assert.Equal("npc", eira.SkinSource);
        Assert.Equal(_skinCustom.ToString(), eira.Skin!.FormKey);
        Assert.Equal("ARMO", eira.Skin.RecordType);
        Assert.Equal("SkinBodyLabCustom", eira.Skin.EditorId);
        Assert.Equal(_skinCustom.ToString(), eira.EffectiveSkin!.FormKey);
        Assert.Equal(_eira.ToString(), eira.TraitsFrom);
        Assert.False(eira.TemplateUnresolved);

        var addon = Assert.Single(eira.SkinAddons);
        Assert.Equal(_customTorso.ToString(), addon.FormKey);
        Assert.Equal(CustomFemaleBody, addon.FemaleModel);
        Assert.Null(addon.MaleModel);
        Assert.Equal([32, 33], addon.BodySlots);
        Assert.Equal(_bodyRace.ToString(), addon.Race);
        Assert.True(addon.AppliesToRace);

        Assert.Equal(_bodyRace.ToString(), eira.Race!.FormKey);
        Assert.Equal("BodyLabNordRace", eira.Race.EditorId);
        Assert.Equal("Body Nord", eira.Race.Name);
        Assert.True(eira.IsPlayable);
        Assert.False(eira.IsChild);
        Assert.Equal(["BodyLabRaceKeyword"], eira.RaceKeywords);
        Assert.Equal(_skinNaked.ToString(), eira.RaceSkin!.FormKey);

        var hair = Assert.Single(eira.HeadParts);
        Assert.Equal(SyntheticLoadOrder.HairNord.ToString(), hair.FormKey);
        Assert.Equal("Hair", hair.PartType);
        Assert.Equal(@"Actors\Character\Character Assets\Hair\FemaleHair01.nif", hair.ModelPath);
        Assert.Equal(CatalogQueries.FaceGenFor(_eira).GeometryPath, eira.FaceGen.GeometryPath);
        Assert.Empty(eira.Notes);
    }

    [Fact]
    public void KeywordsFactionsClassAndCombatStyleRoundTrip()
    {
        var eira = One(_eira);

        var keyword = Assert.Single(eira.Keywords);
        Assert.Equal(_actorTypeNpc.ToString(), keyword.FormKey);
        Assert.Equal("ActorTypeNPC", keyword.EditorId);

        var faction = Assert.Single(eira.Factions);
        Assert.Equal(SyntheticLoadOrder.HousecarlFaction.ToString(), faction.FormKey);
        Assert.Equal("HousecarlWhiterunFaction", faction.EditorId);
        Assert.Equal(2, faction.Rank);

        Assert.Equal(SyntheticLoadOrder.WarriorClass.ToString(), eira.Class!.FormKey);
        Assert.Equal("Warrior", eira.Class.EditorId);
        Assert.Equal("CLAS", eira.Class.RecordType);
        Assert.Equal("Skyrim.esm", eira.Class.WinningPlugin);
        Assert.Equal(_combatStyle.ToString(), eira.CombatStyle!.FormKey);
        Assert.Equal("csHumanMeleeLvl1", eira.CombatStyle.EditorId);
        Assert.Equal("CSTY", eira.CombatStyle.RecordType);
    }

    [Fact]
    public void AnNpcWithoutWnamFallsBackToItsRaceSkin()
    {
        var bjorn = One(_bjorn);

        Assert.Equal("male", bjorn.Sex);
        Assert.Equal("race", bjorn.SkinSource);
        Assert.Equal(_skinNaked.ToString(), bjorn.Skin!.FormKey);
        Assert.Equal(_skinNaked.ToString(), bjorn.EffectiveSkin!.FormKey);
        Assert.Equal(2, bjorn.SkinAddons.Count);
        var torso = Assert.Single(bjorn.SkinAddons, a => a.FormKey == _nakedTorso.ToString());
        Assert.Equal(MaleBody, torso.MaleModel);
        Assert.Equal(FemaleBody, torso.FemaleModel);
        Assert.Equal(SyntheticLoadOrder.DefaultRace.ToString(), torso.Race);
        Assert.Contains(_bodyRace.ToString(), torso.AdditionalRaces);
        Assert.True(torso.AppliesToRace);
        var khajiit = Assert.Single(bjorn.SkinAddons, a => a.FormKey == _nakedTorsoKhajiit.ToString());
        Assert.False(khajiit.AppliesToRace);
        Assert.Null(khajiit.FemaleModel);
    }

    [Fact]
    public void ANpcWhoseRaceHasNoSkinReportsNoneWithANote()
    {
        // The synthetic master's NordRace defines no skin, and Lydia no WNAM.
        var lydia = One(SyntheticLoadOrder.Lydia);
        Assert.Equal("none", lydia.SkinSource);
        Assert.Null(lydia.Skin);
        Assert.Empty(lydia.SkinAddons);
        Assert.Contains(lydia.Notes, n => n.Contains("No skin", StringComparison.Ordinal));
        Assert.Equal("LateTweak.esp", lydia.WinningPlugin);
        Assert.Equal("Skyrim.esm", lydia.OriginPlugin);
        Assert.Equal("000A2C8E", lydia.FormId);
        Assert.Equal("0A2C8E:Skyrim.esm", lydia.FormKey);
    }

    [Fact]
    public void ATraitsTemplateChainSuppliesTheEffectiveRaceSexAndSkin()
    {
        var templated = One(_templated);

        Assert.True(templated.UsesTemplate);
        Assert.Contains("Traits", templated.TemplateFlags);
        Assert.Contains("Inventory", templated.TemplateFlags);
        Assert.Equal(_traitsMid.ToString(), templated.Template!.FormKey);
        Assert.Equal("NPC_", templated.Template.RecordType);

        // Own record: Khajiit, male, no skin of its own (Khajiit race has none either).
        Assert.Equal(SyntheticLoadOrder.KhajiitRace.ToString(), templated.Race!.FormKey);
        Assert.Equal("male", templated.Sex);
        Assert.Equal("none", templated.SkinSource);

        Assert.False(templated.TemplateUnresolved);
        Assert.Equal(_traitsBase.ToString(), templated.TraitsFrom);
        Assert.Equal(_bodyRace.ToString(), templated.EffectiveRace!.FormKey);
        Assert.Equal("female", templated.EffectiveSex);
        Assert.Equal(_skinCustom.ToString(), templated.EffectiveSkin!.FormKey);
        Assert.Equal(CustomFemaleBody, Assert.Single(templated.SkinAddons).FemaleModel);
        Assert.True(templated.IsPlayable);
        Assert.Equal(["BodyLabRaceKeyword"], templated.RaceKeywords);
    }

    [Fact]
    public void ATemplateCycleOrALeveledTemplateIsReportedUnresolvedWithoutLooping()
    {
        var response = Inspect(new BodyInspectRequest { FormKeys = [_cycleA.ToString(), _cycleB.ToString(), _leveledUser.ToString()] });
        Assert.Equal(3, response.Items.Count);
        foreach (var item in response.Items)
        {
            Assert.True(item.TemplateUnresolved);
            Assert.Null(item.TraitsFrom);
            Assert.Null(item.EffectiveRace);
            Assert.Null(item.EffectiveSkin);
            Assert.Null(item.EffectiveSex);
            // The own record's facts stay available.
            Assert.Equal(_bodyRace.ToString(), item.Race!.FormKey);
            Assert.Equal("race", item.SkinSource);
            Assert.Equal(2, item.SkinAddons.Count);
        }

        Assert.Contains(response.Items[0].Notes, n => n.Contains("loops back", StringComparison.Ordinal));
        var leveled = response.Items.Single(i => i.FormKey == _leveledUser.ToString());
        Assert.Equal("LVLN", leveled.Template!.RecordType);
        Assert.Equal(_leveledList.ToString(), leveled.Template.FormKey);
        Assert.Contains(leveled.Notes, n => n.Contains("leveled NPC list", StringComparison.Ordinal));
    }

    [Fact]
    public void AChildRaceIsFlagged()
    {
        var child = One(_child);
        Assert.True(child.IsChild);
        Assert.False(child.IsPlayable);
        Assert.Equal(_childRace.ToString(), child.Race!.FormKey);
        Assert.Equal("race", child.SkinSource);
        Assert.True(child.SkinAddons.Single(a => a.FormKey == _nakedTorso.ToString()).AppliesToRace);
    }

    [Fact]
    public void UnknownOrInvalidFormKeysGoToMissingAndDuplicatesAreReportedOnce()
    {
        var unknown = new FormKey(BodyLab, 0xABCDEF).ToString();
        var response = Inspect(new BodyInspectRequest
        {
            FormKeys = [_eira.ToString(), unknown, "not a key", _eira.ToString().ToUpperInvariant(), _leveledList.ToString()],
        });

        Assert.Equal(1, response.Total);
        Assert.Equal(_eira.ToString(), Assert.Single(response.Items).FormKey);
        Assert.Equal([unknown, "not a key", _leveledList.ToString()], response.Missing);
    }

    [Fact]
    public void TheWholePopulationIsPagedWithoutOverlapAndCanBeFilteredBySex()
    {
        var all = Inspect(new BodyInspectRequest { Limit = 2000 });
        // 4 synthetic master NPCs + 9 BodyLab NPCs.
        Assert.Equal(13, all.Total);
        Assert.Equal(13, all.Items.Count);
        Assert.Empty(all.Missing);

        var paged = new List<string>();
        for (var offset = 0; offset < all.Total; offset += 4)
        {
            var page = Inspect(new BodyInspectRequest { Offset = offset, Limit = 4 });
            Assert.Equal(13, page.Total);
            Assert.True(page.Items.Count <= 4);
            paged.AddRange(page.Items.Select(i => i.FormKey));
        }

        Assert.Equal(all.Items.Select(i => i.FormKey), paged);
        Assert.Equal(paged.Count, paged.Distinct(StringComparer.OrdinalIgnoreCase).Count());
        Assert.Empty(Inspect(new BodyInspectRequest { Offset = 13, Limit = 4 }).Items);

        var female = Inspect(new BodyInspectRequest { Sex = "female", Limit = 2000 });
        Assert.All(female.Items, i => Assert.Equal("female", i.Sex));
        Assert.Equal(all.Items.Count(i => i.Sex == "female"), female.Total);
        Assert.Contains(female.Items, i => i.FormKey == SyntheticLoadOrder.Lydia.ToString());

        var males = Inspect(new BodyInspectRequest { Sex = "male", FormKeys = [_eira.ToString(), _bjorn.ToString()] });
        Assert.Equal(_bjorn.ToString(), Assert.Single(males.Items).FormKey);
        Assert.Empty(males.Missing);
    }

    [Fact]
    public void BadArgumentsAreRefused()
    {
        Assert.Throws<ArgumentException>(() => Inspect(new BodyInspectRequest { Sex = "other" }));
        Assert.Throws<ArgumentException>(() => Inspect(new BodyInspectRequest { FormKeys = Enumerable.Repeat(_eira.ToString(), 2001).ToArray() }));
    }

    [Fact]
    public async Task TheRpcMethodAnswersOnTheWireInCamelCase()
    {
        var rpc = new WorkerRpcTarget(_state, () => { }, (_, _) => { });
        var response = await rpc.InspectBodyNpcs(new BodyInspectRequest { FormKeys = [_eira.ToString()] }, CancellationToken.None);
        Assert.Single(response.Items);

        var options = new JsonSerializerOptions();
        RpcHost.ConfigureSerializer(options);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(response, options));
        var item = json.RootElement.GetProperty("items")[0];
        Assert.Equal("npc", item.GetProperty("skinSource").GetString());
        Assert.Equal(CustomFemaleBody, item.GetProperty("skinAddons")[0].GetProperty("femaleModel").GetString());
        Assert.True(item.GetProperty("skinAddons")[0].GetProperty("appliesToRace").GetBoolean());
        Assert.False(item.GetProperty("isChild").GetBoolean());
        Assert.Equal("0", json.RootElement.GetProperty("missing").GetArrayLength().ToString(System.Globalization.CultureInfo.InvariantCulture));

        var hello = rpc.Handshake(new HandshakeRequest { ClientName = "body-test", ClientVersion = "0", ProtocolMajor = 1, ProtocolMinor = 7 });
        Assert.Contains(Methods.BodyInspectNpcs, hello.Capabilities);
        Assert.Contains(Methods.ExportBodyDistribution, hello.Capabilities);

        var bad = await Assert.ThrowsAsync<LocalRpcException>(() => rpc.InspectBodyNpcs(new BodyInspectRequest { Sex = "x" }, CancellationToken.None));
        Assert.Equal(ErrorCodes.InvalidArgument, bad.ErrorCode);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try
        {
            Directory.Delete(_cache, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
