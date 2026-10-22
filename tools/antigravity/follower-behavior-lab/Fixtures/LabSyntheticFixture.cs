using System.Text;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;

/// <summary>
/// Builds deterministic, realistic binary Mutagen Skyrim SE plugins for the Follower Behavior Lab.
/// Zero external game files are touched; all records are written to the specified sandbox/fixture folder.
/// </summary>
public static class LabSyntheticFixture
{
    public static readonly ModKey Skyrim = ModKey.FromFileName("Skyrim.esm");
    public static readonly ModKey FollowerVoiceAddon = ModKey.FromFileName("FollowerVoiceAddon.esp");
    public static readonly ModKey BrokenVoiceOverride = ModKey.FromFileName("BrokenVoiceOverride.esp");
    public static readonly ModKey DisabledCombatMod = ModKey.FromFileName("DisabledCombatMod.esp");
    public static readonly ModKey NoMarriageListMod = ModKey.FromFileName("NoMarriageList.esm");

    // Races
    public static readonly FormKey DefaultRace = new(Skyrim, 0x000019);
    public static readonly FormKey NordRace = new(Skyrim, 0x013746);

    // Voice Types (VTYP)
    public static readonly FormKey VoiceFemaleEvenToned = new(Skyrim, 0x013AD2);
    public static readonly FormKey VoiceMaleEvenToned = new(Skyrim, 0x013AD1);
    public static readonly FormKey VoiceFemaleCommander = new(Skyrim, 0x013AE3);
    public static readonly FormKey VoiceFemaleUniqueSilent = new(Skyrim, 0x0A0100); // V01: Valid VTYP, NOT in VoicesFollower or VoicesMarriage
    public static readonly FormKey VoiceCreatureDragon = new(Skyrim, 0x01F1D2);     // Non-dialogue creature voice
    public static readonly FormKey VoiceModCustomFollower = new(FollowerVoiceAddon, 0x000800); // V03: Added by mod

    // FormLists (FLST)
    public static readonly FormKey VoicesFollowerList = new(Skyrim, 0x09B1F5);
    public static readonly FormKey VoicesMarriageList = new(Skyrim, 0x0C790E);

    // Factions (FACT)
    public static readonly FormKey PotentialFollowerFaction = new(Skyrim, 0x05C84D);
    public static readonly FormKey CurrentFollowerFaction = new(Skyrim, 0x05C84E);
    public static readonly FormKey PotentialMarriageFaction = new(Skyrim, 0x019809);

    // Classes (CLAS)
    public static readonly FormKey ClassDefensiveWarrior = new(Skyrim, 0x013176);
    public static readonly FormKey ClassArcher = new(Skyrim, 0x013181);
    public static readonly FormKey ClassAssassin = new(Skyrim, 0x01317E);
    public static readonly FormKey ClassDestructionMage = new(Skyrim, 0x013179);
    public static readonly FormKey ClassHealer = new(Skyrim, 0x013182);
    public static readonly FormKey ClassSpellsword = new(Skyrim, 0x01317B);
    public static readonly FormKey ClassDisabledBerserker = new(DisabledCombatMod, 0x000801);

    // Combat Styles (CSTY)
    public static readonly FormKey StyleDefensiveWarrior = new(Skyrim, 0x03BE1C);
    public static readonly FormKey StyleArcher = new(Skyrim, 0x03BE1D);
    public static readonly FormKey StyleAssassin = new(Skyrim, 0x03BE1E);
    public static readonly FormKey StyleDestructionMage = new(Skyrim, 0x03DECE);
    public static readonly FormKey StyleHealer = new(Skyrim, 0x03DECF);
    public static readonly FormKey StyleSpellsword = new(Skyrim, 0x03DED0);
    public static readonly FormKey StyleDisabledBerserker = new(DisabledCombatMod, 0x000802);

    // Armors (ARMO)
    public static readonly FormKey ArmorSteelCuirass = new(Skyrim, 0x013952);
    public static readonly FormKey ArmorSteelBoots = new(Skyrim, 0x013951);
    public static readonly FormKey ArmorSteelGauntlets = new(Skyrim, 0x013953);
    public static readonly FormKey ArmorSteelShield = new(Skyrim, 0x013955);
    public static readonly FormKey ArmorScoutCuirass = new(Skyrim, 0x03619E);
    public static readonly FormKey ArmorScoutBoots = new(Skyrim, 0x03619D);
    public static readonly FormKey ArmorShroudedArmor = new(Skyrim, 0x0D2844);
    public static readonly FormKey ArmorShroudedBoots = new(Skyrim, 0x0D2845);
    public static readonly FormKey ArmorMageRobes = new(Skyrim, 0x07C932);
    public static readonly FormKey ArmorMageBoots = new(Skyrim, 0x0166FC);
    public static readonly FormKey ArmorHealerRobes = new(Skyrim, 0x0A0201);
    public static readonly FormKey ArmorSpellswordCuirass = new(Skyrim, 0x0A0202);

    // Weapons & Ammo (WEAP / AMMO)
    public static readonly FormKey WeaponSteelSword = new(Skyrim, 0x013989);
    public static readonly FormKey WeaponHuntingBow = new(Skyrim, 0x013985);
    public static readonly FormKey AmmoSteelArrow = new(Skyrim, 0x01397F);
    public static readonly FormKey WeaponEbonyDagger = new(Skyrim, 0x013996);
    public static readonly FormKey WeaponStaffFireball = new(Skyrim, 0x029B7B);

    // Spells (SPEL)
    public static readonly FormKey SpellFirebolt = new(Skyrim, 0x012FD0);
    public static readonly FormKey SpellIceSpike = new(Skyrim, 0x02B96C);
    public static readonly FormKey SpellOakflesh = new(Skyrim, 0x05AD5C);
    public static readonly FormKey SpellFastHealing = new(Skyrim, 0x02F3B8);
    public static readonly FormKey SpellCloseWounds = new(Skyrim, 0x0B62EF);
    public static readonly FormKey SpellSteadfastWard = new(Skyrim, 0x0211EE);
    public static readonly FormKey SpellInvisibility = new(Skyrim, 0x027EB6);

    // Perks (PERK)
    public static readonly FormKey PerkShieldWall = new(Skyrim, 0x058F68);
    public static readonly FormKey PerkJuggernaut = new(Skyrim, 0x0BCD2A);
    public static readonly FormKey PerkOverdraw = new(Skyrim, 0x0BABED);
    public static readonly FormKey PerkDeadlyAim = new(Skyrim, 0x0103ADA);
    public static readonly FormKey PerkBackstab = new(Skyrim, 0x058210);
    public static readonly FormKey PerkAugmentedFlames = new(Skyrim, 0x0581EA);
    public static readonly FormKey PerkRecovery = new(Skyrim, 0x0581F8);
    public static readonly FormKey PerkRegeneration = new(Skyrim, 0x0581F4);
    public static readonly FormKey PerkArmsMan = new(Skyrim, 0x0BABE4);

    // AI Packages (PACK)
    public static readonly FormKey PackageDefaultFollower = new(Skyrim, 0x0A0301);
    public static readonly FormKey PackageDefaultSandbox = new(Skyrim, 0x0A0302);

    /// <summary>
    /// Ensures the basic fixture directory contains all synthetic plugins, profiles, and sample JSON inputs.
    /// </summary>
    public static void EnsureBasicFixture(string directory, int extraSyntheticRecords = 0)
    {
        Directory.CreateDirectory(directory);

        var skyrimPath = Path.Combine(directory, Skyrim.FileName);
        var addonPath = Path.Combine(directory, FollowerVoiceAddon.FileName);
        var brokenOverridePath = Path.Combine(directory, BrokenVoiceOverride.FileName);
        var disabledModPath = Path.Combine(directory, DisabledCombatMod.FileName);

        BuildSkyrimMaster(skyrimPath, extraSyntheticRecords, includeMarriageList: true);
        BuildFollowerVoiceAddon(addonPath, skyrimPath);
        BuildBrokenVoiceOverride(brokenOverridePath, skyrimPath);
        BuildDisabledCombatMod(disabledModPath);

        // Default profile: Skyrim.esm + FollowerVoiceAddon.esp enabled; BrokenVoiceOverride.esp and DisabledCombatMod.esp disabled
        var defaultPluginsTxt = new StringBuilder();
        defaultPluginsTxt.AppendLine("# Antigravity Follower Behavior Lab - Default Profile");
        defaultPluginsTxt.AppendLine($"*{Skyrim.FileName}");
        defaultPluginsTxt.AppendLine($"*{FollowerVoiceAddon.FileName}");
        defaultPluginsTxt.AppendLine($"-{BrokenVoiceOverride.FileName}");
        defaultPluginsTxt.AppendLine($"-{DisabledCombatMod.FileName}");
        File.WriteAllText(Path.Combine(directory, "plugins.txt"), defaultPluginsTxt.ToString(), Encoding.UTF8);

        // Alternate profile ("override-conflict"): enables BrokenVoiceOverride.esp which removes VoiceFemaleCommander from VoicesFollower
        var conflictPluginsTxt = new StringBuilder();
        conflictPluginsTxt.AppendLine("# Antigravity Follower Behavior Lab - Override Conflict Profile");
        conflictPluginsTxt.AppendLine($"*{Skyrim.FileName}");
        conflictPluginsTxt.AppendLine($"*{FollowerVoiceAddon.FileName}");
        conflictPluginsTxt.AppendLine($"*{BrokenVoiceOverride.FileName}");
        conflictPluginsTxt.AppendLine($"-{DisabledCombatMod.FileName}");
        File.WriteAllText(Path.Combine(directory, "plugins.override-conflict.txt"), conflictPluginsTxt.ToString(), Encoding.UTF8);
    }

    public static void BuildNoMarriageListFixture(string directory)
    {
        Directory.CreateDirectory(directory);
        var skyrimPath = Path.Combine(directory, Skyrim.FileName);
        BuildSkyrimMaster(skyrimPath, extraSyntheticRecords: 0, includeMarriageList: false);
        File.WriteAllText(Path.Combine(directory, "plugins.txt"), $"*{Skyrim.FileName}\n", Encoding.UTF8);
    }

    private static void BuildSkyrimMaster(string path, int extraSyntheticRecords, bool includeMarriageList)
    {
        var master = new SkyrimMod(Skyrim, SkyrimRelease.SkyrimSE);
        master.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Master;

        var defaultRace = new Race(DefaultRace, SkyrimRelease.SkyrimSE)
        {
            EditorID = "DefaultRace",
            Name = "Default",
            Height = new GenderedItem<float>(1.0f, 1.0f),
        };
        master.Races.Add(defaultRace);

        var nordRace = new Race(NordRace, SkyrimRelease.SkyrimSE)
        {
            EditorID = "NordRace",
            Name = "Nord",
            Height = new GenderedItem<float>(1.0f, 1.0f),
        };
        nordRace.ArmorRace.SetTo(defaultRace);
        master.Races.Add(nordRace);

        // Voice Types
        master.VoiceTypes.Add(new VoiceType(VoiceFemaleEvenToned, SkyrimRelease.SkyrimSE)
        {
            EditorID = "FemaleEvenToned",
            Flags = VoiceType.Flag.AllowDefaultDialog | VoiceType.Flag.Female,
        });
        master.VoiceTypes.Add(new VoiceType(VoiceMaleEvenToned, SkyrimRelease.SkyrimSE)
        {
            EditorID = "MaleEvenToned",
            Flags = VoiceType.Flag.AllowDefaultDialog,
        });
        master.VoiceTypes.Add(new VoiceType(VoiceFemaleCommander, SkyrimRelease.SkyrimSE)
        {
            EditorID = "FemaleCommander",
            Flags = VoiceType.Flag.AllowDefaultDialog | VoiceType.Flag.Female,
        });
        master.VoiceTypes.Add(new VoiceType(VoiceFemaleUniqueSilent, SkyrimRelease.SkyrimSE)
        {
            EditorID = "FemaleUniqueSilent",
            Flags = VoiceType.Flag.Female, // Valid VTYP, not in any follower/marriage FLST
        });
        master.VoiceTypes.Add(new VoiceType(VoiceCreatureDragon, SkyrimRelease.SkyrimSE)
        {
            EditorID = "CrDragonVoice",
            Flags = 0, // Creature voice
        });

        // FormLists for follower & marriage dialogue
        var followerList = new FormList(VoicesFollowerList, SkyrimRelease.SkyrimSE)
        {
            EditorID = "VoicesFollower",
            Items = new ExtendedList<IFormLinkGetter<ISkyrimMajorRecordGetter>>
            {
                new FormLink<ISkyrimMajorRecordGetter>(VoiceFemaleEvenToned),
                new FormLink<ISkyrimMajorRecordGetter>(VoiceMaleEvenToned),
                new FormLink<ISkyrimMajorRecordGetter>(VoiceFemaleCommander),
            },
        };
        master.FormLists.Add(followerList);

        if (includeMarriageList)
        {
            var marriageList = new FormList(VoicesMarriageList, SkyrimRelease.SkyrimSE)
            {
                EditorID = "VoicesMarriage",
                Items = new ExtendedList<IFormLinkGetter<ISkyrimMajorRecordGetter>>
                {
                    new FormLink<ISkyrimMajorRecordGetter>(VoiceFemaleEvenToned),
                    new FormLink<ISkyrimMajorRecordGetter>(VoiceMaleEvenToned),
                },
            };
            master.FormLists.Add(marriageList);
        }

        // Factions
        master.Factions.Add(new Faction(PotentialFollowerFaction, SkyrimRelease.SkyrimSE)
        {
            EditorID = "PotentialFollowerFaction",
            Name = "Potential Follower Faction",
        });
        master.Factions.Add(new Faction(CurrentFollowerFaction, SkyrimRelease.SkyrimSE)
        {
            EditorID = "CurrentFollowerFaction",
            Name = "Current Follower Faction",
        });
        master.Factions.Add(new Faction(PotentialMarriageFaction, SkyrimRelease.SkyrimSE)
        {
            EditorID = "PotentialMarriageFaction",
            Name = "Potential Marriage Faction",
        });

        // Classes (CLAS)
        AddClass(master, ClassDefensiveWarrior, "CombatWarrior1H", "Defensive Warrior", Skill.OneHanded, 90,
            new() { [Skill.OneHanded] = 3, [Skill.Block] = 3, [Skill.HeavyArmor] = 3, [Skill.Smithing] = 1 },
            health: 3, magicka: 0, stamina: 2);
        AddClass(master, ClassArcher, "CombatRanger", "Scout Archer", Skill.Archery, 90,
            new() { [Skill.Archery] = 4, [Skill.LightArmor] = 3, [Skill.Sneak] = 2, [Skill.OneHanded] = 1 },
            health: 2, magicka: 0, stamina: 3);
        AddClass(master, ClassAssassin, "CombatAssassin", "Nightblade Assassin", Skill.Sneak, 90,
            new() { [Skill.Sneak] = 4, [Skill.OneHanded] = 3, [Skill.LightArmor] = 2, [Skill.Illusion] = 1 },
            health: 2, magicka: 1, stamina: 3);
        AddClass(master, ClassDestructionMage, "CombatMageDestruction", "Destruction Mage", Skill.Destruction, 90,
            new() { [Skill.Destruction] = 4, [Skill.Alteration] = 3, [Skill.Conjuration] = 2, [Skill.Restoration] = 1 },
            health: 1, magicka: 4, stamina: 0);
        AddClass(master, ClassHealer, "CombatHealer", "Field Healer", Skill.Restoration, 90,
            new() { [Skill.Restoration] = 4, [Skill.Alteration] = 3, [Skill.Block] = 1, [Skill.LightArmor] = 2 },
            health: 2, magicka: 3, stamina: 1);
        AddClass(master, ClassSpellsword, "CombatSpellsword", "Spellsword Battlemage", Skill.OneHanded, 90,
            new() { [Skill.OneHanded] = 3, [Skill.Destruction] = 3, [Skill.HeavyArmor] = 2, [Skill.Alteration] = 2 },
            health: 2, magicka: 2, stamina: 1);

        // Combat Styles (CSTY)
        AddCombatStyle(master, StyleDefensiveWarrior, "csHumanMelee1H", off: 0.45f, def: 0.85f, grp: 0.5f, melee: 1.0f, magic: 0.0f, ranged: 0.2f, avoid: 0.2f);
        AddCombatStyle(master, StyleArcher, "csHumanMissile", off: 0.65f, def: 0.50f, grp: 0.4f, melee: 0.2f, magic: 0.0f, ranged: 1.0f, avoid: 0.8f);
        AddCombatStyle(master, StyleAssassin, "csHumanAssassin", off: 0.90f, def: 0.40f, grp: 0.3f, melee: 1.0f, magic: 0.3f, ranged: 0.4f, avoid: 0.6f);
        AddCombatStyle(master, StyleDestructionMage, "csHumanMagic", off: 0.80f, def: 0.50f, grp: 0.4f, melee: 0.1f, magic: 1.0f, ranged: 0.0f, avoid: 0.9f);
        AddCombatStyle(master, StyleHealer, "csHumanHealer", off: 0.25f, def: 0.90f, grp: 0.2f, melee: 0.2f, magic: 1.0f, ranged: 0.0f, avoid: 0.85f);
        AddCombatStyle(master, StyleSpellsword, "csHumanSpellsword", off: 0.70f, def: 0.60f, grp: 0.5f, melee: 0.8f, magic: 0.8f, ranged: 0.1f, avoid: 0.4f);

        // Armors
        AddArmor(master, ArmorSteelCuirass, "ArmorSteelCuirass", "Steel Armor", BipedObjectFlag.Body, ArmorType.HeavyArmor);
        AddArmor(master, ArmorSteelBoots, "ArmorSteelBoots", "Steel Cuffed Boots", BipedObjectFlag.Feet, ArmorType.HeavyArmor);
        AddArmor(master, ArmorSteelGauntlets, "ArmorSteelGauntlets", "Steel Nordic Gauntlets", BipedObjectFlag.Hands, ArmorType.HeavyArmor);
        AddArmor(master, ArmorSteelShield, "ArmorSteelShield", "Steel Shield", BipedObjectFlag.Shield, ArmorType.HeavyArmor);
        AddArmor(master, ArmorScoutCuirass, "ArmorScaledCuirass", "Scaled Armor", BipedObjectFlag.Body, ArmorType.LightArmor);
        AddArmor(master, ArmorScoutBoots, "ArmorScaledBoots", "Scaled Boots", BipedObjectFlag.Feet, ArmorType.LightArmor);
        AddArmor(master, ArmorShroudedArmor, "DBArmor", "Shrouded Armor", BipedObjectFlag.Body, ArmorType.LightArmor);
        AddArmor(master, ArmorShroudedBoots, "DBBoots", "Shrouded Boots", BipedObjectFlag.Feet, ArmorType.LightArmor);
        AddArmor(master, ArmorMageRobes, "ClothesCollegeRobesDestruction", "Master Robes of Destruction", BipedObjectFlag.Body, ArmorType.Clothing);
        AddArmor(master, ArmorMageBoots, "ClothesBoots01", "College Boots", BipedObjectFlag.Feet, ArmorType.Clothing);
        AddArmor(master, ArmorHealerRobes, "ClothesRobesRestoration", "Adept Robes of Restoration", BipedObjectFlag.Body, ArmorType.Clothing);
        AddArmor(master, ArmorSpellswordCuirass, "ArmorSpellswordPlate", "Spellsword Battlemage Armor", BipedObjectFlag.Body, ArmorType.HeavyArmor);

        // Weapons & Ammo
        master.Weapons.Add(new Weapon(WeaponSteelSword, SkyrimRelease.SkyrimSE) { EditorID = "WeapSteelSword", Name = "Steel Sword" });
        master.Weapons.Add(new Weapon(WeaponHuntingBow, SkyrimRelease.SkyrimSE) { EditorID = "WeapHuntingBow", Name = "Hunting Bow" });
        master.Ammunitions.Add(new Ammunition(AmmoSteelArrow, SkyrimRelease.SkyrimSE) { EditorID = "SteelArrow", Name = "Steel Arrow" });
        master.Weapons.Add(new Weapon(WeaponEbonyDagger, SkyrimRelease.SkyrimSE) { EditorID = "WeapEbonyDagger", Name = "Ebony Dagger" });
        master.Weapons.Add(new Weapon(WeaponStaffFireball, SkyrimRelease.SkyrimSE) { EditorID = "StaffFireball", Name = "Staff of Fireballs" });

        // Spells
        AddSpell(master, SpellFirebolt, "Firebolt", "Firebolt");
        AddSpell(master, SpellIceSpike, "IceSpike", "Ice Spike");
        AddSpell(master, SpellOakflesh, "Oakflesh", "Oakflesh");
        AddSpell(master, SpellFastHealing, "FastHealing", "Fast Healing");
        AddSpell(master, SpellCloseWounds, "CloseWounds", "Close Wounds");
        AddSpell(master, SpellSteadfastWard, "SteadfastWard", "Steadfast Ward");
        AddSpell(master, SpellInvisibility, "Invisibility", "Invisibility");

        // Perks
        AddPerk(master, PerkShieldWall, "ShieldWall00", "Shield Wall");
        AddPerk(master, PerkJuggernaut, "Juggernaut00", "Juggernaut");
        AddPerk(master, PerkOverdraw, "Overdraw00", "Overdraw");
        AddPerk(master, PerkDeadlyAim, "DeadlyAim", "Deadly Aim");
        AddPerk(master, PerkBackstab, "Backstab", "Backstab");
        AddPerk(master, PerkAugmentedFlames, "AugmentedFlames00", "Augmented Flames");
        AddPerk(master, PerkRecovery, "Recovery00", "Recovery");
        AddPerk(master, PerkRegeneration, "Regeneration", "Regeneration");
        AddPerk(master, PerkArmsMan, "Armsman00", "Armsman");

        // AI Packages
        master.Packages.Add(new Package(PackageDefaultFollower, SkyrimRelease.SkyrimSE) { EditorID = "DefaultFollowerPackage" });
        master.Packages.Add(new Package(PackageDefaultSandbox, SkyrimRelease.SkyrimSE) { EditorID = "DefaultSandboxEditorLocation512" });

        // Optional synthetic stress records (for the 1,000-record performance benchmark)
        for (int i = 0; i < extraSyntheticRecords; i++)
        {
            uint id = (uint)(0x0B0000 + i);
            int mod3 = i % 3;
            if (mod3 == 0)
            {
                AddClass(master, new FormKey(Skyrim, id), $"SynthClass_{i:D4}", $"Synthetic Class {i}", Skill.OneHanded, 50,
                    new() { [Skill.OneHanded] = 3 }, health: 2, magicka: 1, stamina: 2);
            }
            else if (mod3 == 1)
            {
                AddCombatStyle(master, new FormKey(Skyrim, id), $"SynthStyle_{i:D4}", 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.2f);
            }
            else
            {
                var v = new VoiceType(new FormKey(Skyrim, id), SkyrimRelease.SkyrimSE)
                {
                    EditorID = $"SynthVoice_{i:D4}",
                    Flags = (i % 2 == 0) ? VoiceType.Flag.Female | VoiceType.Flag.AllowDefaultDialog : VoiceType.Flag.AllowDefaultDialog,
                };
                master.VoiceTypes.Add(v);
                if (i % 6 == 2)
                {
                    followerList.Items.Add(new FormLink<ISkyrimMajorRecordGetter>(v.FormKey));
                }
            }
        }

        master.WriteToBinary(path, new BinaryWriteParameters
        {
            LowerRangeDisallowedHandler = new NoCheckIfLowerRangeDisallowed(),
        });
    }

    private static void BuildFollowerVoiceAddon(string addonPath, string skyrimPath)
    {
        using var skyrimOverlay = SkyrimMod.CreateFromBinaryOverlay(new ModPath(Skyrim, skyrimPath), SkyrimRelease.SkyrimSE);
        var addon = new SkyrimMod(FollowerVoiceAddon, SkyrimRelease.SkyrimSE);
        addon.VoiceTypes.Add(new VoiceType(VoiceModCustomFollower, SkyrimRelease.SkyrimSE)
        {
            EditorID = "CustomFollowerVoiceFemale",
            Flags = VoiceType.Flag.Female | VoiceType.Flag.AllowDefaultDialog,
        });

        var baseFollowerList = skyrimOverlay.FormLists.First(f => f.FormKey == VoicesFollowerList);
        var overrideList = addon.FormLists.GetOrAddAsOverride(baseFollowerList);
        overrideList.Items.Add(new FormLink<ISkyrimMajorRecordGetter>(VoiceModCustomFollower));

        addon.WriteToBinary(addonPath, new BinaryWriteParameters
        {
            MastersListOrdering = new MastersListOrderingByLoadOrder([Skyrim]),
        });
    }

    private static void BuildBrokenVoiceOverride(string brokenPath, string skyrimPath)
    {
        using var skyrimOverlay = SkyrimMod.CreateFromBinaryOverlay(new ModPath(Skyrim, skyrimPath), SkyrimRelease.SkyrimSE);
        var broken = new SkyrimMod(BrokenVoiceOverride, SkyrimRelease.SkyrimSE);

        var baseFollowerList = skyrimOverlay.FormLists.First(f => f.FormKey == VoicesFollowerList);
        var overrideList = broken.FormLists.GetOrAddAsOverride(baseFollowerList);
        // Removes VoiceFemaleCommander (V04 conflict scenario) and also loses FollowerVoiceAddon's addition
        overrideList.Items.Clear();
        overrideList.Items.Add(new FormLink<ISkyrimMajorRecordGetter>(VoiceFemaleEvenToned));
        overrideList.Items.Add(new FormLink<ISkyrimMajorRecordGetter>(VoiceMaleEvenToned));

        broken.WriteToBinary(brokenPath, new BinaryWriteParameters
        {
            MastersListOrdering = new MastersListOrderingByLoadOrder([Skyrim]),
        });
    }

    private static void BuildDisabledCombatMod(string disabledModPath)
    {
        var mod = new SkyrimMod(DisabledCombatMod, SkyrimRelease.SkyrimSE);
        AddClass(mod, ClassDisabledBerserker, "DisabledBerserkerClass", "Berserker (Disabled Mod)", Skill.TwoHanded, 90,
            new() { [Skill.TwoHanded] = 5 }, health: 4, magicka: 0, stamina: 2);
        AddCombatStyle(mod, StyleDisabledBerserker, "csDisabledBerserker", 1.0f, 0.1f, 0.8f, 1.0f, 0.0f, 0.0f, 0.0f);
        mod.WriteToBinary(disabledModPath);
    }

    private static void AddClass(
        SkyrimMod mod,
        FormKey key,
        string editorId,
        string name,
        Skill trainingSkill,
        byte maxTraining,
        Dictionary<Skill, byte> weights,
        byte health,
        byte magicka,
        byte stamina)
    {
        var cls = new Class(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            Name = name,
            Teaches = trainingSkill,
            MaxTrainingLevel = maxTraining,
        };
        foreach (var (s, w) in weights)
        {
            cls.SkillWeights[s] = w;
        }
        cls.StatWeights[BasicStat.Health] = health;
        cls.StatWeights[BasicStat.Magicka] = magicka;
        cls.StatWeights[BasicStat.Stamina] = stamina;
        mod.Classes.Add(cls);
    }

    private static void AddCombatStyle(
        SkyrimMod mod,
        FormKey key,
        string editorId,
        float off,
        float def,
        float grp,
        float melee,
        float magic,
        float ranged,
        float avoid)
    {
        var style = new CombatStyle(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            OffensiveMult = off,
            DefensiveMult = def,
            GroupOffensiveMult = grp,
            EquipmentScoreMultMelee = melee,
            EquipmentScoreMultMagic = magic,
            EquipmentScoreMultRanged = ranged,
            EquipmentScoreMultShout = 0.0f,
            EquipmentScoreMultUnarmed = 0.1f,
            AvoidThreatChance = avoid,
        };
        mod.CombatStyles.Add(style);
    }

    private static void AddArmor(SkyrimMod mod, FormKey key, string editorId, string name, BipedObjectFlag slot, ArmorType type)
    {
        var arma = new ArmorAddon(new FormKey(key.ModKey, key.ID + 0x100000), SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId + "AA",
            BodyTemplate = new BodyTemplate { FirstPersonFlags = slot, ArmorType = type },
            WorldModel = new GenderedItem<Model?>(
                new Model { File = $"Armor\\{editorId}_M.nif" },
                new Model { File = $"Armor\\{editorId}_F.nif" }),
        };
        arma.Race.SetTo(DefaultRace);
        mod.ArmorAddons.Add(arma);

        var armor = new Armor(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            Name = name,
            BodyTemplate = new BodyTemplate { FirstPersonFlags = slot, ArmorType = type },
        };
        armor.Armature.Add(arma);
        mod.Armors.Add(armor);
    }

    private static void AddSpell(SkyrimMod mod, FormKey key, string editorId, string name)
    {
        mod.Spells.Add(new Spell(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            Name = name,
        });
    }

    private static void AddPerk(SkyrimMod mod, FormKey key, string editorId, string name)
    {
        mod.Perks.Add(new Perk(key, SkyrimRelease.SkyrimSE)
        {
            EditorID = editorId,
            Name = name,
            Playable = true,
        });
    }

    public static FollowerDraftDto CreateSampleDraft(
        string editorId = "SNS_AstridFollower",
        string name = "Astrid Shield-Maiden",
        FormKey? voiceKey = null,
        FormKey? classKey = null,
        FormKey? styleKey = null,
        bool includeMarriageFaction = false)
    {
        var factions = new List<FactionMembershipDto>
        {
            new(PotentialFollowerFaction.ToString(), 0),
            new(CurrentFollowerFaction.ToString(), -1),
        };
        if (includeMarriageFaction)
        {
            factions.Add(new(PotentialMarriageFaction.ToString(), 0));
        }

        return new FollowerDraftDto
        {
            Name = name,
            EditorId = editorId,
            Description = "Synthetic follower draft for Follower Behavior Studio validation.",
            RaceFormKey = NordRace.ToString(),
            Female = true,
            VoiceFormKey = (voiceKey ?? VoiceFemaleEvenToned).ToString(),
            ClassFormKey = (classKey ?? ClassDefensiveWarrior).ToString(),
            CombatStyleFormKey = (styleKey ?? StyleDefensiveWarrior).ToString(),
            Height = 1.0f,
            Weight = 50.0f,
            Level = 15,
            MinLevel = 10,
            MaxLevel = 50,
            Health = 220,
            Magicka = 100,
            Stamina = 180,
            Skills = new Dictionary<string, int>
            {
                ["OneHanded"] = 60,
                ["Block"] = 55,
                ["HeavyArmor"] = 60,
            },
            OutfitArmorFormKeys = [ArmorSteelCuirass.ToString(), ArmorSteelBoots.ToString(), ArmorSteelGauntlets.ToString()],
            Inventory = [new InventoryItemDto(WeaponSteelSword.ToString(), 1)],
            Factions = factions,
            PackageFormKeys = [PackageDefaultFollower.ToString(), PackageDefaultSandbox.ToString()],
            SpellFormKeys = [],
            PerkFormKeys = [PerkShieldWall.ToString(), PerkJuggernaut.ToString()],
        };
    }
}
