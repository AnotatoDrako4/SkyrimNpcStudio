using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace SkyrimNpcStudio.TestFixtures;

/// <summary>
/// Builds a small but structurally realistic Skyrim SE load order on disk with Mutagen.
/// Contains no game data: every record is synthetic. Some FormIDs mirror vanilla ones
/// (Lydia = 0A2C8E) so tests read like real-world scenarios.
///
///   0 Skyrim.esm     master: races, ARMA/ARMO, an outfit, Lydia, a templated guard
///   1 FaceMod.esp    overrides Lydia: Height 1.05
///   2 ArmorPack.esp  ESL-flagged: two new armors for DefaultRace
///   3 LateTweak.esp  overrides Lydia from the ORIGINAL record: Weight 80 (loses FaceMod's height)
/// </summary>
public sealed class SyntheticLoadOrder : IDisposable
{
    public static readonly ModKey Skyrim = ModKey.FromFileName("Skyrim.esm");
    public static readonly ModKey FaceMod = ModKey.FromFileName("FaceMod.esp");
    public static readonly ModKey ArmorPack = ModKey.FromFileName("ArmorPack.esp");
    public static readonly ModKey LateTweak = ModKey.FromFileName("LateTweak.esp");

    public static readonly FormKey Lydia = new(Skyrim, 0x0A2C8E);
    public static readonly FormKey TemplatedGuard = new(Skyrim, 0x0A0001);
    public static readonly FormKey GuardTemplate = new(Skyrim, 0x0A0002);
    public static readonly FormKey NordRace = new(Skyrim, 0x013746);
    public static readonly FormKey DefaultRace = new(Skyrim, 0x000019);
    public static readonly FormKey KhajiitRace = new(Skyrim, 0x013745);
    public static readonly FormKey HousecarlOutfit = new(Skyrim, 0x0A0010);
    public static readonly FormKey SteelCuirass = new(Skyrim, 0x013952);
    public static readonly FormKey SteelBoots = new(Skyrim, 0x013951);
    public static readonly FormKey SteelGauntlets = new(Skyrim, 0x013953);
    public static readonly FormKey ElvenCuirass = new(Skyrim, 0x0896A3);
    public static readonly FormKey KhajiitOnlyHood = new(Skyrim, 0x0A0020);
    public static readonly FormKey HousecarlFaction = new(Skyrim, 0x0A0030);
    public static readonly FormKey HairNord = new(Skyrim, 0x0A0040);
    public static readonly FormKey DragonsreachCell = new(Skyrim, 0x01691D);
    public static readonly FormKey LydiaRef = new(Skyrim, 0x0A2C94);

    public FormKey CrimsonCuirass { get; private set; }
    public FormKey CrimsonCloak { get; private set; }

    public string DataDirectory { get; }

    private SyntheticLoadOrder(string dataDirectory) => DataDirectory = dataDirectory;

    public string PathOf(ModKey key) => Path.Combine(DataDirectory, key.FileName);

    /// <summary>Load order entries as the studio's MO2 engine would send them.</summary>
    public IReadOnlyList<(string FileName, string Path)> LoadOrder =>
        new[] { Skyrim, FaceMod, ArmorPack, LateTweak }.Select(k => (k.FileName.String, PathOf(k))).ToArray();

    public static SyntheticLoadOrder Create(string? directory = null)
    {
        var dir = directory ?? Path.Combine(Path.GetTempPath(), "sns-fixture-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        var fixture = new SyntheticLoadOrder(dir);
        fixture.Build();
        return fixture;
    }

    private void Build()
    {
        var master = new SkyrimMod(Skyrim, SkyrimRelease.SkyrimSE);
        master.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Master;

        var defaultRace = AddRace(master, DefaultRace, "DefaultRace", "Default", armorRace: null);
        AddRace(master, NordRace, "NordRace", "Nord", armorRace: defaultRace);
        AddRace(master, KhajiitRace, "KhajiitRace", "Khajiit", armorRace: null);

        AddArmor(master, SteelCuirass, "ArmorSteelCuirass", "Steel Armor", BipedObjectFlag.Body, DefaultRace, "Steel\\Cuirass");
        AddArmor(master, SteelBoots, "ArmorSteelBoots", "Steel Boots", BipedObjectFlag.Feet, DefaultRace, "Steel\\Boots");
        AddArmor(master, SteelGauntlets, "ArmorSteelGauntlets", "Steel Gauntlets", BipedObjectFlag.Hands, DefaultRace, "Steel\\Gauntlets");
        AddArmor(master, ElvenCuirass, "ArmorElvenCuirass", "Elven Armor", BipedObjectFlag.Body, DefaultRace, "Elven\\Cuirass");
        AddArmor(master, KhajiitOnlyHood, "ClothesKhajiitHood", "Khajiit Hood", BipedObjectFlag.Hair, KhajiitRace, "Clothes\\KhajiitHood");

        var outfit = new Outfit(HousecarlOutfit, SkyrimRelease.SkyrimSE)
        {
            EditorID = "HousecarlOutfit",
            Items = new ExtendedList<IFormLinkGetter<IOutfitTargetGetter>>
            {
                new FormLink<IOutfitTargetGetter>(SteelCuirass),
                new FormLink<IOutfitTargetGetter>(SteelBoots),
            },
        };
        master.Outfits.Add(outfit);

        master.Factions.Add(new Faction(HousecarlFaction, SkyrimRelease.SkyrimSE) { EditorID = "HousecarlWhiterunFaction", Name = "Housecarl" });
        master.HeadParts.Add(new HeadPart(HairNord, SkyrimRelease.SkyrimSE)
        {
            EditorID = "HairFemaleNord01",
            Type = HeadPart.TypeEnum.Hair,
            Model = new Model { File = "Actors\\Character\\Character Assets\\Hair\\FemaleHair01.nif" },
        });

        var lydia = new Npc(Lydia, SkyrimRelease.SkyrimSE)
        {
            EditorID = "HousecarlWhiterun",
            Name = "Lydia",
            Height = 1.0f,
            Weight = 50f,
        };
        lydia.Race.SetTo(NordRace);
        lydia.Configuration.Flags = NpcConfiguration.Flag.Female | NpcConfiguration.Flag.Unique | NpcConfiguration.Flag.Essential;
        lydia.DefaultOutfit.SetTo(HousecarlOutfit);
        lydia.HeadParts.Add(new FormLink<IHeadPartGetter>(HairNord));
        lydia.Factions.Add(new RankPlacement { Faction = new FormLink<IFactionGetter>(HousecarlFaction), Rank = 0 });
        master.Npcs.Add(lydia);

        var template = new Npc(GuardTemplate, SkyrimRelease.SkyrimSE) { EditorID = "GuardTemplate", Name = "Guard" };
        template.Race.SetTo(NordRace);
        template.DefaultOutfit.SetTo(HousecarlOutfit);
        master.Npcs.Add(template);

        var guard = new Npc(TemplatedGuard, SkyrimRelease.SkyrimSE) { EditorID = "TemplatedGuard", Name = "Whiterun Guard" };
        guard.Race.SetTo(NordRace);
        guard.Template.SetTo(GuardTemplate);
        guard.Configuration.TemplateFlags = NpcConfiguration.TemplateFlag.Inventory | NpcConfiguration.TemplateFlag.Traits;
        master.Npcs.Add(guard);

        var dragonsreach = new Cell(DragonsreachCell, SkyrimRelease.SkyrimSE)
        {
            EditorID = "WhiterunDragonsreach",
            Name = "Dragonsreach",
        };
        var placedLydia = new PlacedNpc(LydiaRef, SkyrimRelease.SkyrimSE)
        {
            EditorID = "LydiaRef",
        };
        placedLydia.Base.SetTo(lydia);
        dragonsreach.Persistent.Add(placedLydia);
        master.Cells.Add(new CellBlock
        {
            SubBlocks = { new CellSubBlock { Cells = { dragonsreach } } }
        });

        // Vanilla Skyrim.esm defines records below 0x800 (DefaultRace is 000019); only the
        // synthetic master may do that, so the lower-range check is disabled for it alone.
        master.WriteToBinary(PathOf(Skyrim), new BinaryWriteParameters
        {
            LowerRangeDisallowedHandler = new NoCheckIfLowerRangeDisallowed(),
        });

        // FaceMod: a typical appearance overhaul edit.
        var faceMod = new SkyrimMod(FaceMod, SkyrimRelease.SkyrimSE);
        var faceLydia = faceMod.Npcs.GetOrAddAsOverride(lydia);
        faceLydia.Height = 1.05f;
        faceMod.WriteToBinary(PathOf(FaceMod));

        // ArmorPack: ESL-flagged plugin adding armors (records allocated in the light range).
        var pack = new SkyrimMod(ArmorPack, SkyrimRelease.SkyrimSE);
        pack.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Small;
        CrimsonCuirass = AddNewArmor(pack, "CrimsonCuirass", "Crimson Corset", BipedObjectFlag.Body, "Crimson\\Torso");
        CrimsonCloak = AddNewArmor(pack, "CrimsonCloak", "Crimson Cloak", (BipedObjectFlag)(1u << (46 - 30)), "Crimson\\Cloak");
        pack.WriteToBinary(PathOf(ArmorPack));

        // LateTweak: built from the ORIGINAL record, so FaceMod's height edit is lost when it wins.
        var tweak = new SkyrimMod(LateTweak, SkyrimRelease.SkyrimSE);
        var tweakLydia = tweak.Npcs.GetOrAddAsOverride(lydia);
        tweakLydia.Weight = 80f;
        tweak.WriteToBinary(PathOf(LateTweak));
    }

    private static Race AddRace(SkyrimMod mod, FormKey key, string editorId, string name, Race? armorRace)
    {
        var race = new Race(key, SkyrimRelease.SkyrimSE) { EditorID = editorId, Name = name };
        if (armorRace is not null)
        {
            race.ArmorRace.SetTo(armorRace);
        }

        mod.Races.Add(race);
        return race;
    }

    private static void AddArmor(SkyrimMod mod, FormKey key, string editorId, string name, BipedObjectFlag slots, FormKey race, string mesh)
    {
        var arma = new ArmorAddon(new FormKey(key.ModKey, key.ID + 0x100000), SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId + "AA",
            BodyTemplate = new BodyTemplate { FirstPersonFlags = slots },
            WorldModel = new GenderedItem<Model?>(
                new Model { File = $"Armor\\{mesh}_1.nif" },
                new Model { File = $"Armor\\{mesh}F_1.nif" }),
        };
        arma.Race.SetTo(race);
        mod.ArmorAddons.Add(arma);

        var armor = new Armor(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            Name = name,
            BodyTemplate = new BodyTemplate { FirstPersonFlags = slots, ArmorType = ArmorType.HeavyArmor },
        };
        armor.Armature.Add(arma);
        mod.Armors.Add(armor);
    }

    private static FormKey AddNewArmor(SkyrimMod mod, string editorId, string name, BipedObjectFlag slots, string mesh)
    {
        var arma = mod.ArmorAddons.AddNew(editorId + "AA");
        arma.BodyTemplate = new BodyTemplate { FirstPersonFlags = slots };
        arma.Race.SetTo(DefaultRace);
        arma.WorldModel = new GenderedItem<Model?>(
            new Model { File = $"Armor\\{mesh}_1.nif" },
            new Model { File = $"Armor\\{mesh}F_1.nif" });

        var armor = mod.Armors.AddNew(editorId);
        armor.Name = name;
        armor.BodyTemplate = new BodyTemplate { FirstPersonFlags = slots, ArmorType = ArmorType.Clothing };
        armor.Armature.Add(arma);
        return armor.FormKey;
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(DataDirectory, recursive: true);
        }
        catch (IOException)
        {
            // Overlays may still hold handles on slow CI machines; temp is cleaned eventually.
        }
    }
}
