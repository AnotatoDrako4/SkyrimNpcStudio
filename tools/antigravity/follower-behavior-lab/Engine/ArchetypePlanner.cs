using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

/// <summary>
/// Provides six original, configurable follower behavior archetypes and dynamically validates all record links
/// (CLAS, CSTY, ARMO, ITEM, SPEL, PERK, FACT, PACK) against the active Mutagen load order.
/// </summary>
public static class ArchetypePlanner
{
    private static readonly IReadOnlyList<ArchetypeTemplate> BuiltInArchetypes =
    [
        new ArchetypeTemplate
        {
            Id = "defensive-warrior",
            DisplayName = "Guerrero Defensivo",
            RoleSummary = "Frontline heavy-armor tank specializing in One-Handed weapons, shield blocking, and threat absorption.",
            ClassFormKey = LabSyntheticFixture.ClassDefensiveWarrior.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleDefensiveWarrior.ToString(),
            Health = 240,
            Magicka = 80,
            Stamina = 180,
            Skills = new Dictionary<string, int>
            {
                ["OneHanded"] = 65,
                ["Block"] = 65,
                ["HeavyArmor"] = 65,
                ["Smithing"] = 40,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorSteelCuirass.ToString(),
                LabSyntheticFixture.ArmorSteelBoots.ToString(),
                LabSyntheticFixture.ArmorSteelGauntlets.ToString(),
                LabSyntheticFixture.ArmorSteelShield.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponSteelSword.ToString(), 1),
            ],
            SpellFormKeys = [],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkShieldWall.ToString(),
                LabSyntheticFixture.PerkJuggernaut.ToString(),
                LabSyntheticFixture.PerkArmsMan.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
        new ArchetypeTemplate
        {
            Id = "archer",
            DisplayName = "Arquero",
            RoleSummary = "Long-range marksman with high ranged equipment score multiplier, threat avoidance, and light armor mobility.",
            ClassFormKey = LabSyntheticFixture.ClassArcher.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleArcher.ToString(),
            Health = 180,
            Magicka = 90,
            Stamina = 230,
            Skills = new Dictionary<string, int>
            {
                ["Archery"] = 70,
                ["LightArmor"] = 60,
                ["Sneak"] = 55,
                ["OneHanded"] = 45,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorScoutCuirass.ToString(),
                LabSyntheticFixture.ArmorScoutBoots.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponHuntingBow.ToString(), 1),
                new InventoryItemDto(LabSyntheticFixture.AmmoSteelArrow.ToString(), 100),
            ],
            SpellFormKeys = [],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkOverdraw.ToString(),
                LabSyntheticFixture.PerkDeadlyAim.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
        new ArchetypeTemplate
        {
            Id = "assassin",
            DisplayName = "Asesino",
            RoleSummary = "Aggressive stealth flanker using dual daggers, invisibility magic, and critical backstab perks.",
            ClassFormKey = LabSyntheticFixture.ClassAssassin.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleAssassin.ToString(),
            Health = 170,
            Magicka = 130,
            Stamina = 220,
            Skills = new Dictionary<string, int>
            {
                ["Sneak"] = 75,
                ["OneHanded"] = 65,
                ["LightArmor"] = 60,
                ["Illusion"] = 50,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorShroudedArmor.ToString(),
                LabSyntheticFixture.ArmorShroudedBoots.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponEbonyDagger.ToString(), 2),
            ],
            SpellFormKeys =
            [
                LabSyntheticFixture.SpellInvisibility.ToString(),
            ],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkBackstab.ToString(),
                LabSyntheticFixture.PerkArmsMan.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
        new ArchetypeTemplate
        {
            Id = "destruction-mage",
            DisplayName = "Mago Destructor",
            RoleSummary = "Elemental artillery caster prioritizing Firebolt/Ice Spike ranged magic and flesh wards.",
            ClassFormKey = LabSyntheticFixture.ClassDestructionMage.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleDestructionMage.ToString(),
            Health = 150,
            Magicka = 260,
            Stamina = 100,
            Skills = new Dictionary<string, int>
            {
                ["Destruction"] = 75,
                ["Alteration"] = 60,
                ["Conjuration"] = 50,
                ["Restoration"] = 45,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorMageRobes.ToString(),
                LabSyntheticFixture.ArmorMageBoots.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponStaffFireball.ToString(), 1),
            ],
            SpellFormKeys =
            [
                LabSyntheticFixture.SpellFirebolt.ToString(),
                LabSyntheticFixture.SpellIceSpike.ToString(),
                LabSyntheticFixture.SpellOakflesh.ToString(),
            ],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkAugmentedFlames.ToString(),
                LabSyntheticFixture.PerkRecovery.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
        new ArchetypeTemplate
        {
            Id = "healer",
            DisplayName = "Sanador",
            RoleSummary = "Support sustain caster configured with Restoration spells, defensive wards, and high defensive combat multiplier.",
            ClassFormKey = LabSyntheticFixture.ClassHealer.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleHealer.ToString(),
            Health = 190,
            Magicka = 240,
            Stamina = 110,
            Skills = new Dictionary<string, int>
            {
                ["Restoration"] = 75,
                ["Alteration"] = 65,
                ["LightArmor"] = 50,
                ["Block"] = 45,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorHealerRobes.ToString(),
                LabSyntheticFixture.ArmorMageBoots.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponSteelSword.ToString(), 1),
            ],
            SpellFormKeys =
            [
                LabSyntheticFixture.SpellFastHealing.ToString(),
                LabSyntheticFixture.SpellCloseWounds.ToString(),
                LabSyntheticFixture.SpellSteadfastWard.ToString(),
                LabSyntheticFixture.SpellOakflesh.ToString(),
            ],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkRecovery.ToString(),
                LabSyntheticFixture.PerkRegeneration.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
        new ArchetypeTemplate
        {
            Id = "spellsword",
            DisplayName = "Guerrero-Mago",
            RoleSummary = "Hybrid battlemage combining one-handed melee steel with offensive Destruction and defensive Alteration spells.",
            ClassFormKey = LabSyntheticFixture.ClassSpellsword.ToString(),
            CombatStyleFormKey = LabSyntheticFixture.StyleSpellsword.ToString(),
            Health = 200,
            Magicka = 200,
            Stamina = 160,
            Skills = new Dictionary<string, int>
            {
                ["OneHanded"] = 65,
                ["Destruction"] = 65,
                ["HeavyArmor"] = 55,
                ["Alteration"] = 55,
            },
            OutfitArmorFormKeys =
            [
                LabSyntheticFixture.ArmorSpellswordCuirass.ToString(),
                LabSyntheticFixture.ArmorSteelBoots.ToString(),
                LabSyntheticFixture.ArmorSteelGauntlets.ToString(),
            ],
            Inventory =
            [
                new InventoryItemDto(LabSyntheticFixture.WeaponSteelSword.ToString(), 1),
            ],
            SpellFormKeys =
            [
                LabSyntheticFixture.SpellFirebolt.ToString(),
                LabSyntheticFixture.SpellOakflesh.ToString(),
                LabSyntheticFixture.SpellFastHealing.ToString(),
            ],
            PerkFormKeys =
            [
                LabSyntheticFixture.PerkArmsMan.ToString(),
                LabSyntheticFixture.PerkAugmentedFlames.ToString(),
                LabSyntheticFixture.PerkRecovery.ToString(),
            ],
            Factions =
            [
                new FactionMembershipDto(LabSyntheticFixture.PotentialFollowerFaction.ToString(), 0),
                new FactionMembershipDto(LabSyntheticFixture.CurrentFollowerFaction.ToString(), -1),
            ],
            PackageFormKeys =
            [
                LabSyntheticFixture.PackageDefaultFollower.ToString(),
                LabSyntheticFixture.PackageDefaultSandbox.ToString(),
            ],
        },
    ];

    public static IReadOnlyList<ArchetypeTemplate> GetAllArchetypes() => BuiltInArchetypes;

    public static ArchetypeTemplate GetById(string archetypeId)
    {
        if (string.IsNullOrWhiteSpace(archetypeId))
        {
            throw new LabUsageException("ArchetypeNotFound", "Archetype ID cannot be empty.", 2);
        }

        var normalized = archetypeId.Trim().ToLowerInvariant() switch
        {
            "warrior" or "defensive-warrior" or "guerrero" or "guerrero-defensivo" => "defensive-warrior",
            "archer" or "arquero" or "ranger" => "archer",
            "assassin" or "asesino" => "assassin",
            "mage" or "destruction-mage" or "mago" or "mago-destructor" => "destruction-mage",
            "healer" or "sanador" or "cleric" => "healer",
            "spellsword" or "battlemage" or "guerrero-mago" => "spellsword",
            var other => other,
        };

        var match = BuiltInArchetypes.FirstOrDefault(a => a.Id.Equals(normalized, StringComparison.OrdinalIgnoreCase));
        if (match is null)
        {
            var available = string.Join(", ", BuiltInArchetypes.Select(a => a.Id));
            throw new LabUsageException("ArchetypeNotFound", $"Unknown archetype '{archetypeId}'. Available: {available}.", 2);
        }
        return match;
    }

    public static ArchetypeValidationReport ValidateArchetype(PluginContext context, ArchetypeTemplate template)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(template);

        var issues = new List<BehaviorIssue>();
        var masters = new HashSet<ModKey>();
        var links = new List<ResolvedLinkEvidence>();

        var resolvedClass = CombatClassCatalog.ResolveClass(context, template.ClassFormKey, issues, masters);
        links.Add(new ResolvedLinkEvidence(
            "Class",
            template.ClassFormKey,
            "CLAS",
            resolvedClass.Resolved,
            resolvedClass.Resolved ? resolvedClass.EditorId : null,
            resolvedClass.Resolved ? resolvedClass.DefiningPlugin : null,
            resolvedClass.Resolved ? resolvedClass.WinningPlugin : null));

        var resolvedStyle = CombatClassCatalog.ResolveCombatStyle(context, template.CombatStyleFormKey, issues, masters, required: true);
        if (resolvedStyle is not null)
        {
            links.Add(new ResolvedLinkEvidence(
                "CombatStyle",
                template.CombatStyleFormKey,
                "CSTY",
                resolvedStyle.Resolved,
                resolvedStyle.Resolved ? resolvedStyle.EditorId : null,
                resolvedStyle.Resolved ? resolvedStyle.DefiningPlugin : null,
                resolvedStyle.Resolved ? resolvedStyle.WinningPlugin : null));
        }

        foreach (var armorKey in template.OutfitArmorFormKeys)
        {
            ResolveArchetypeLink<IArmorGetter>(context, template.Id, "OutfitArmor", armorKey, "ARMO", links, issues, masters);
        }

        foreach (var item in template.Inventory)
        {
            ResolveArchetypeLink<IItemGetter>(context, template.Id, "Inventory", item.FormKey, "ITEM", links, issues, masters);
        }

        foreach (var spellKey in template.SpellFormKeys)
        {
            ResolveArchetypeLink<ISpellGetter>(context, template.Id, "Spell", spellKey, "SPEL", links, issues, masters);
        }

        foreach (var perkKey in template.PerkFormKeys)
        {
            ResolveArchetypeLink<IPerkGetter>(context, template.Id, "Perk", perkKey, "PERK", links, issues, masters);
        }

        foreach (var fac in template.Factions)
        {
            ResolveArchetypeLink<IFactionGetter>(context, template.Id, "Faction", fac.FormKey, "FACT", links, issues, masters);
        }

        foreach (var pkgKey in template.PackageFormKeys)
        {
            ResolveArchetypeLink<IPackageGetter>(context, template.Id, "Package", pkgKey, "PACK", links, issues, masters);
        }

        bool allResolved = links.All(l => l.Resolved) && !issues.Any(i => i.Severity == ValidationSeverity.ERROR);
        var orderedMasters = context.EnabledPlugins
            .Where(p => masters.Contains(p.ModKey))
            .Select(p => p.ModKey.FileName.String)
            .ToArray();

        return new ArchetypeValidationReport
        {
            ArchetypeId = template.Id,
            DisplayName = template.DisplayName,
            AllLinksResolved = allResolved,
            ResolvedClass = resolvedClass,
            ResolvedCombatStyle = resolvedStyle,
            Links = links,
            RequiredMasters = orderedMasters,
            Issues = issues,
        };
    }

    private static void ResolveArchetypeLink<T>(
        PluginContext context,
        string archetypeId,
        string category,
        string formKeyText,
        string expectedType,
        List<ResolvedLinkEvidence> links,
        List<BehaviorIssue> issues,
        HashSet<ModKey> masters)
        where T : class, ISkyrimMajorRecordGetter
    {
        if (!PluginContext.TryParseFormKey(formKeyText, out var key))
        {
            issues.Add(new BehaviorIssue(
                "ArchetypeBrokenLink",
                ValidationSeverity.ERROR,
                $"Archetype '{archetypeId}' has invalid {category} ({expectedType}) FormKey syntax: '{formKeyText}'.",
                TargetFormKey: formKeyText));
            links.Add(new ResolvedLinkEvidence(category, formKeyText, expectedType, false, null, null, null, "Malformed FormKey"));
            return;
        }

        if (context.IsModDisabled(key.ModKey))
        {
            issues.Add(new BehaviorIssue(
                "ArchetypeBrokenLink",
                ValidationSeverity.ERROR,
                $"Archetype '{archetypeId}' references {category} ({expectedType}) '{key}' from disabled plugin '{key.ModKey.FileName}'.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            links.Add(new ResolvedLinkEvidence(category, key.ToString(), expectedType, false, null, key.ModKey.FileName.String, null, "Disabled plugin"));
            return;
        }

        if (!context.LinkCache.TryResolveSimpleContext<T>(key, out var ctx) || ctx.Record.IsDeleted)
        {
            issues.Add(new BehaviorIssue(
                "ArchetypeBrokenLink",
                ValidationSeverity.ERROR,
                $"Archetype '{archetypeId}' has unresolved {category} ({expectedType}) reference: '{key}'.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            links.Add(new ResolvedLinkEvidence(category, key.ToString(), expectedType, false, null, key.ModKey.FileName.String, null, "Missing record"));
            return;
        }

        masters.Add(key.ModKey);
        masters.Add(ctx.ModKey);
        links.Add(new ResolvedLinkEvidence(
            category,
            key.ToString(),
            expectedType,
            true,
            ctx.Record.EditorID ?? key.ToString(),
            key.ModKey.FileName.String,
            ctx.ModKey.FileName.String));
    }
}
