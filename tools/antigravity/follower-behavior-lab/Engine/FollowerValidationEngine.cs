using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

/// <summary>
/// Validates follower behavior plans and drafts against real Mutagen records while preserving Character Forge's FaceGen export block.
/// </summary>
public static class FollowerValidationEngine
{
    public static CatalogInspectReport InspectCatalog(PluginContext context)
    {
        var issues = new List<BehaviorIssue>(context.LoadIssues);
        var classes = CombatClassCatalog.EnumerateActiveClasses(context);
        var styles = CombatClassCatalog.EnumerateActiveCombatStyles(context);
        var voices = VoiceDialogueAnalyzer.EnumerateActiveVoices(context);

        var enabledDtos = context.EnabledPlugins
            .Select(p => new PluginSummaryDto(
                p.LoadIndex,
                p.ModKey.FileName.String,
                p.IsLight,
                p.IsMasterFlagged,
                p.SizeBytes,
                p.Sha256,
                p.Masters.Select(m => m.FileName.String).ToArray()))
            .ToArray();

        return new CatalogInspectReport
        {
            GeneratedAtUtc = DateTime.UtcNow.ToString("o"),
            FixtureDirectory = context.FixtureDirectory,
            LoadOrderFingerprint = context.LoadOrderFingerprint,
            EnabledPlugins = enabledDtos,
            DisabledPlugins = context.DisabledPluginNames,
            Classes = classes,
            CombatStyles = styles,
            Voices = voices,
            Issues = issues,
        };
    }

    public static BehaviorVerdictReport ValidatePlan(PluginContext context, FollowerDraftDto draft)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(draft);

        var issues = new List<BehaviorIssue>(context.LoadIssues);
        var requiredMasters = new HashSet<ModKey>();
        var resolvedLinks = new List<ResolvedLinkEvidence>();

        if (string.IsNullOrWhiteSpace(draft.EditorId))
        {
            issues.Add(new BehaviorIssue("EditorIdEmpty", ValidationSeverity.ERROR, "Draft EditorID is required."));
        }
        if (string.IsNullOrWhiteSpace(draft.Name))
        {
            issues.Add(new BehaviorIssue("NameEmpty", ValidationSeverity.ERROR, "Draft Name is required."));
        }

        // 1. Race
        ResolveLink<IRaceGetter>(context, "Race", draft.RaceFormKey, "RACE", required: true, resolvedLinks, issues, requiredMasters);

        // 2. Voice & Dialogue Evidence
        var voiceResult = VoiceDialogueAnalyzer.AnalyzeVoice(context, draft.VoiceFormKey, draft.Female, draft.Factions);
        issues.AddRange(voiceResult.Issues);
        if (voiceResult.VoiceResolved && PluginContext.TryParseFormKey(voiceResult.VoiceFormKey, out var vKey))
        {
            requiredMasters.Add(vKey.ModKey);
            resolvedLinks.Add(new ResolvedLinkEvidence(
                "Voice",
                voiceResult.VoiceFormKey,
                "VTYP",
                true,
                voiceResult.VoiceEditorId,
                voiceResult.DefiningPlugin,
                voiceResult.DefiningPlugin,
                $"Follower={voiceResult.FollowerDialogueClassification}, Marriage={voiceResult.MarriageDialogueClassification}"));
        }
        else
        {
            resolvedLinks.Add(new ResolvedLinkEvidence("Voice", draft.VoiceFormKey, "VTYP", false, null, null, null, "Unresolved voice"));
        }

        // 3. Class & CombatStyle
        var resolvedClass = CombatClassCatalog.ResolveClass(context, draft.ClassFormKey, issues, requiredMasters);
        resolvedLinks.Add(new ResolvedLinkEvidence(
            "Class",
            draft.ClassFormKey,
            "CLAS",
            resolvedClass.Resolved,
            resolvedClass.Resolved ? resolvedClass.EditorId : null,
            resolvedClass.Resolved ? resolvedClass.DefiningPlugin : null,
            resolvedClass.Resolved ? resolvedClass.WinningPlugin : null));

        var resolvedStyle = CombatClassCatalog.ResolveCombatStyle(context, draft.CombatStyleFormKey, issues, requiredMasters, required: false);
        if (resolvedStyle is not null)
        {
            resolvedLinks.Add(new ResolvedLinkEvidence(
                "CombatStyle",
                draft.CombatStyleFormKey,
                "CSTY",
                resolvedStyle.Resolved,
                resolvedStyle.Resolved ? resolvedStyle.EditorId : null,
                resolvedStyle.Resolved ? resolvedStyle.DefiningPlugin : null,
                resolvedStyle.Resolved ? resolvedStyle.WinningPlugin : null));
        }

        // 4. Equipment (Outfit Armors & Inventory Weapons/Items)
        var seenArmors = new HashSet<FormKey>();
        foreach (var armorKey in draft.OutfitArmorFormKeys)
        {
            var armor = ResolveLink<IArmorGetter>(context, "OutfitArmor", armorKey, "ARMO", required: true, resolvedLinks, issues, requiredMasters);
            if (armor is not null && !seenArmors.Add(armor.FormKey))
            {
                issues.Add(new BehaviorIssue("DuplicateOutfitArmor", ValidationSeverity.ERROR, $"Duplicate armor in outfit: '{armorKey}'.", TargetFormKey: armorKey));
            }
        }

        var seenItems = new HashSet<FormKey>();
        foreach (var item in draft.Inventory)
        {
            var resolvedItem = ResolveLink<IItemGetter>(context, "Inventory", item.FormKey, "ITEM", required: true, resolvedLinks, issues, requiredMasters);
            if (item.Count <= 0 || item.Count > 100000)
            {
                issues.Add(new BehaviorIssue("InventoryCountInvalid", ValidationSeverity.ERROR, $"Inventory count {item.Count} for '{item.FormKey}' is out of bounds (1..100000).", TargetFormKey: item.FormKey));
            }
            if (resolvedItem is not null && !seenItems.Add(resolvedItem.FormKey))
            {
                issues.Add(new BehaviorIssue("DuplicateInventoryItem", ValidationSeverity.ERROR, $"Duplicate inventory item: '{item.FormKey}'.", TargetFormKey: item.FormKey));
            }
        }

        // 5. Spells & Perks
        foreach (var spellKey in draft.SpellFormKeys)
        {
            ResolveLink<ISpellGetter>(context, "Spell", spellKey, "SPEL", required: true, resolvedLinks, issues, requiredMasters);
        }

        foreach (var perkKey in draft.PerkFormKeys)
        {
            ResolveLink<IPerkGetter>(context, "Perk", perkKey, "PERK", required: true, resolvedLinks, issues, requiredMasters);
        }

        // 6. Factions & AI Packages
        var seenFactions = new HashSet<FormKey>();
        foreach (var fac in draft.Factions)
        {
            var resolvedFaction = ResolveLink<IFactionGetter>(context, "Faction", fac.FormKey, "FACT", required: true, resolvedLinks, issues, requiredMasters);
            if (fac.Rank is < -1 or > 127)
            {
                issues.Add(new BehaviorIssue("FactionRankInvalid", ValidationSeverity.ERROR, $"Faction rank {fac.Rank} for '{fac.FormKey}' must be between -1 and 127.", TargetFormKey: fac.FormKey));
            }
            if (resolvedFaction is not null && !seenFactions.Add(resolvedFaction.FormKey))
            {
                issues.Add(new BehaviorIssue("DuplicateFaction", ValidationSeverity.ERROR, $"Duplicate faction membership: '{fac.FormKey}'.", TargetFormKey: fac.FormKey));
            }
        }

        foreach (var pkgKey in draft.PackageFormKeys)
        {
            ResolveLink<IPackageGetter>(context, "Package", pkgKey, "PACK", required: true, resolvedLinks, issues, requiredMasters);
        }
        if (draft.PackageFormKeys.Count > 0)
        {
            issues.Add(new BehaviorIssue(
                "PackageBehaviorRequiresInGameTest",
                ValidationSeverity.REQUIRES_IN_GAME_TEST,
                "AI Package references resolve statically; runtime schedule evaluation, quest alias overrides, and navmesh execution require in-game verification in Skyrim SE.",
                Evidence: $"Packages={draft.PackageFormKeys.Count}"));
        }

        // Mandatory runtime & FaceGen export guardrails
        issues.Add(new BehaviorIssue(
            "FollowerRuntimeUnverified",
            ValidationSeverity.REQUIRES_IN_GAME_TEST,
            "Static record and FLST analysis does not guarantee in-game recruitment or dialogue execution; DialogueFollower/RelationshipMarriage quest conditions, aliases, and SEQ state must be tested in Skyrim.",
            TargetFormKey: draft.VoiceFormKey));

        issues.Add(new BehaviorIssue(
            "NativeFaceGenUnavailable",
            ValidationSeverity.INFO,
            "Final playable NPC export remains locked in Skyrim NPC Studio until native FaceGen NIF and FaceTint DDS are verified."));

        bool structurallyValid = !issues.Any(i => i.Severity == ValidationSeverity.ERROR)
                                 && voiceResult.FollowerDialogueClassification != EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE;

        var orderedMasters = context.EnabledPlugins
            .Where(p => requiredMasters.Contains(p.ModKey))
            .Select(p => p.ModKey.FileName.String)
            .ToArray();

        return new BehaviorVerdictReport
        {
            GeneratedAtUtc = DateTime.UtcNow.ToString("o"),
            LoadOrderFingerprint = context.LoadOrderFingerprint,
            DraftEditorId = draft.EditorId,
            DraftName = draft.Name,
            DraftSha256 = SandboxGuard.ComputeObjectCanonicalSha256(draft),
            PlayableExportUnlocked = false,
            RuntimeStatus = "REQUIRES_IN_GAME_TEST",
            StructurallyValid = structurallyValid,
            VoiceAnalysis = voiceResult,
            ResolvedClass = resolvedClass,
            ResolvedCombatStyle = resolvedStyle,
            ResolvedLinks = resolvedLinks,
            RequiredMasters = orderedMasters,
            Issues = issues,
        };
    }

    private static T? ResolveLink<T>(
        PluginContext context,
        string category,
        string formKeyText,
        string expectedType,
        bool required,
        List<ResolvedLinkEvidence> links,
        List<BehaviorIssue> issues,
        HashSet<ModKey> requiredMasters)
        where T : class, ISkyrimMajorRecordGetter
    {
        if (string.IsNullOrWhiteSpace(formKeyText))
        {
            if (required)
            {
                issues.Add(new BehaviorIssue(
                    $"{category}Missing",
                    ValidationSeverity.ERROR,
                    $"Required {category} ({expectedType}) FormKey is empty."));
                links.Add(new ResolvedLinkEvidence(category, formKeyText, expectedType, false, null, null, null, "Empty FormKey"));
            }
            return null;
        }

        if (!PluginContext.TryParseFormKey(formKeyText, out var key))
        {
            issues.Add(new BehaviorIssue(
                $"{category}FormKeyInvalid",
                ValidationSeverity.ERROR,
                $"Invalid FormKey syntax for {category} ({expectedType}): '{formKeyText}'.",
                TargetFormKey: formKeyText));
            links.Add(new ResolvedLinkEvidence(category, formKeyText, expectedType, false, null, null, null, "Invalid FormKey syntax"));
            return null;
        }

        if (context.IsModDisabled(key.ModKey))
        {
            issues.Add(new BehaviorIssue(
                $"{category}FromDisabledPlugin",
                ValidationSeverity.ERROR,
                $"{category} ({expectedType}) '{key}' belongs to disabled plugin '{key.ModKey.FileName}'.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            links.Add(new ResolvedLinkEvidence(category, key.ToString(), expectedType, false, null, key.ModKey.FileName.String, null, "Plugin disabled"));
            return null;
        }

        if (!context.LinkCache.TryResolveSimpleContext<T>(key, out var ctx) || ctx.Record.IsDeleted)
        {
            issues.Add(new BehaviorIssue(
                "ReferenceInvalid",
                ValidationSeverity.ERROR,
                $"Missing or wrong-type {expectedType} ({typeof(T).Name}) for {category}: '{key}'.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            links.Add(new ResolvedLinkEvidence(category, key.ToString(), expectedType, false, null, key.ModKey.FileName.String, null, "Unresolved in active load order"));
            return null;
        }

        requiredMasters.Add(key.ModKey);
        requiredMasters.Add(ctx.ModKey);
        links.Add(new ResolvedLinkEvidence(
            category,
            key.ToString(),
            expectedType,
            true,
            ctx.Record.EditorID ?? key.ToString(),
            key.ModKey.FileName.String,
            ctx.ModKey.FileName.String));
        return ctx.Record;
    }
}
