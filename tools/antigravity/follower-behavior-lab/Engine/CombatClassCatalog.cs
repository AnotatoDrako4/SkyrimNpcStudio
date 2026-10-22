using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

/// <summary>
/// Inspects and resolves CLAS (IClassGetter) and CSTY (ICombatStyleGetter) records across the active Mutagen load order.
/// Never substitutes a missing or disabled-plugin record with a synthetic/default record.
/// </summary>
public static class CombatClassCatalog
{
    public static IReadOnlyList<ClassCatalogEntry> EnumerateActiveClasses(PluginContext context)
    {
        return context.EnabledPlugins
            .Reverse()
            .SelectMany(p => p.Mod.Classes.Select(r => (Record: (IClassGetter)r, ModKey: p.ModKey)))
            .DistinctBy(x => x.Record.FormKey)
            .Where(x => !x.Record.IsDeleted)
            .Select(x => MapClass(x.Record, x.ModKey))
            .OrderBy(c => c.EditorId, StringComparer.OrdinalIgnoreCase)
            .ThenBy(c => c.FormKey, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public static IReadOnlyList<CombatStyleCatalogEntry> EnumerateActiveCombatStyles(PluginContext context)
    {
        return context.EnabledPlugins
            .Reverse()
            .SelectMany(p => p.Mod.CombatStyles.Select(r => (Record: (ICombatStyleGetter)r, ModKey: p.ModKey)))
            .DistinctBy(x => x.Record.FormKey)
            .Where(x => !x.Record.IsDeleted)
            .Select(x => MapCombatStyle(x.Record, x.ModKey))
            .OrderBy(c => c.EditorId, StringComparer.OrdinalIgnoreCase)
            .ThenBy(c => c.FormKey, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public static ClassCatalogEntry ResolveClass(
        PluginContext context,
        string formKeyText,
        List<BehaviorIssue> issues,
        HashSet<ModKey> requiredMasters)
    {
        if (!PluginContext.TryParseFormKey(formKeyText, out var key))
        {
            issues.Add(new BehaviorIssue(
                "ClassFormKeyInvalid",
                ValidationSeverity.ERROR,
                $"Invalid CLAS FormKey syntax: '{formKeyText}'.",
                TargetFormKey: formKeyText));
            return UnresolvedClass(formKeyText);
        }

        if (context.IsModDisabled(key.ModKey))
        {
            issues.Add(new BehaviorIssue(
                "ClassFromDisabledPlugin",
                ValidationSeverity.ERROR,
                $"Class '{key}' belongs to disabled plugin '{key.ModKey.FileName}' and cannot be used as active.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            return UnresolvedClass(key.ToString(), key.ModKey.FileName.String);
        }

        if (!context.LinkCache.TryResolveSimpleContext<IClassGetter>(key, out var resolvedCtx) || resolvedCtx.Record.IsDeleted)
        {
            issues.Add(new BehaviorIssue(
                "ClassNotFound",
                ValidationSeverity.ERROR,
                $"Class record '{key}' could not be resolved in the active load order.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            return UnresolvedClass(key.ToString(), key.ModKey.FileName.String);
        }

        requiredMasters.Add(key.ModKey);
        requiredMasters.Add(resolvedCtx.ModKey);
        return MapClass(resolvedCtx.Record, resolvedCtx.ModKey);
    }

    public static CombatStyleCatalogEntry? ResolveCombatStyle(
        PluginContext context,
        string formKeyText,
        List<BehaviorIssue> issues,
        HashSet<ModKey> requiredMasters,
        bool required = false)
    {
        if (string.IsNullOrWhiteSpace(formKeyText))
        {
            if (required)
            {
                issues.Add(new BehaviorIssue(
                    "CombatStyleRequired",
                    ValidationSeverity.ERROR,
                    "A CombatStyle (CSTY) FormKey is required for this operation."));
            }
            return null;
        }

        if (!PluginContext.TryParseFormKey(formKeyText, out var key))
        {
            issues.Add(new BehaviorIssue(
                "CombatStyleFormKeyInvalid",
                ValidationSeverity.ERROR,
                $"Invalid CSTY FormKey syntax: '{formKeyText}'.",
                TargetFormKey: formKeyText));
            return UnresolvedCombatStyle(formKeyText);
        }

        if (context.IsModDisabled(key.ModKey))
        {
            issues.Add(new BehaviorIssue(
                "CombatStyleFromDisabledPlugin",
                ValidationSeverity.ERROR,
                $"CombatStyle '{key}' belongs to disabled plugin '{key.ModKey.FileName}' and cannot be offered as active.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            return UnresolvedCombatStyle(key.ToString(), key.ModKey.FileName.String);
        }

        if (!context.LinkCache.TryResolveSimpleContext<ICombatStyleGetter>(key, out var resolvedCtx) || resolvedCtx.Record.IsDeleted)
        {
            issues.Add(new BehaviorIssue(
                "CombatStyleNotFound",
                ValidationSeverity.ERROR,
                $"CombatStyle record '{key}' could not be resolved in the active load order.",
                TargetFormKey: key.ToString(),
                SourcePlugin: key.ModKey.FileName.String));
            return UnresolvedCombatStyle(key.ToString(), key.ModKey.FileName.String);
        }

        requiredMasters.Add(key.ModKey);
        requiredMasters.Add(resolvedCtx.ModKey);
        return MapCombatStyle(resolvedCtx.Record, resolvedCtx.ModKey);
    }

    private static ClassCatalogEntry MapClass(IClassGetter record, ModKey winningMod)
    {
        var skillWeights = record.SkillWeights
            .Where(kv => kv.Value > 0)
            .OrderByDescending(kv => kv.Value)
            .ThenBy(kv => kv.Key.ToString(), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(kv => kv.Key.ToString(), kv => kv.Value, StringComparer.OrdinalIgnoreCase);

        var statWeights = record.StatWeights
            .ToDictionary(kv => kv.Key.ToString(), kv => kv.Value, StringComparer.OrdinalIgnoreCase);

        return new ClassCatalogEntry(
            FormKey: record.FormKey.ToString(),
            EditorId: record.EditorID ?? record.FormKey.ToString(),
            Name: record.Name?.String ?? record.EditorID ?? record.FormKey.ToString(),
            DefiningPlugin: record.FormKey.ModKey.FileName.String,
            WinningPlugin: winningMod.FileName.String,
            Resolved: true,
            PrimaryTrainingSkill: record.Teaches?.ToString() ?? "None",
            MaxTrainingLevel: record.MaxTrainingLevel,
            SkillWeights: skillWeights,
            StatWeights: statWeights);
    }

    private static CombatStyleCatalogEntry MapCombatStyle(ICombatStyleGetter record, ModKey winningMod)
    {
        return new CombatStyleCatalogEntry(
            FormKey: record.FormKey.ToString(),
            EditorId: record.EditorID ?? record.FormKey.ToString(),
            DefiningPlugin: record.FormKey.ModKey.FileName.String,
            WinningPlugin: winningMod.FileName.String,
            Resolved: true,
            OffensiveMult: record.OffensiveMult,
            DefensiveMult: record.DefensiveMult,
            GroupOffensiveMult: record.GroupOffensiveMult,
            MeleeScoreMult: record.EquipmentScoreMultMelee,
            MagicScoreMult: record.EquipmentScoreMultMagic,
            RangedScoreMult: record.EquipmentScoreMultRanged,
            ShoutScoreMult: record.EquipmentScoreMultShout,
            UnarmedScoreMult: record.EquipmentScoreMultUnarmed,
            AvoidThreatChance: record.AvoidThreatChance);
    }

    private static ClassCatalogEntry UnresolvedClass(string formKey, string definingPlugin = "unresolved") =>
        new(
            FormKey: formKey,
            EditorId: "<unresolved>",
            Name: "<unresolved>",
            DefiningPlugin: definingPlugin,
            WinningPlugin: "<none>",
            Resolved: false,
            PrimaryTrainingSkill: "None",
            MaxTrainingLevel: 0,
            SkillWeights: new Dictionary<string, byte>(),
            StatWeights: new Dictionary<string, byte>());

    private static CombatStyleCatalogEntry UnresolvedCombatStyle(string formKey, string definingPlugin = "unresolved") =>
        new(
            FormKey: formKey,
            EditorId: "<unresolved>",
            DefiningPlugin: definingPlugin,
            WinningPlugin: "<none>",
            Resolved: false,
            OffensiveMult: 0f,
            DefensiveMult: 0f,
            GroupOffensiveMult: 0f,
            MeleeScoreMult: 0f,
            MagicScoreMult: 0f,
            RangedScoreMult: 0f,
            ShoutScoreMult: 0f,
            UnarmedScoreMult: 0f,
            AvoidThreatChance: 0f);
}
