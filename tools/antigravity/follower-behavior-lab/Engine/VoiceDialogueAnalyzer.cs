using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

/// <summary>
/// Analyzes VTYP records and their provenance across VoicesFollower and VoicesMarriage FormLists (FLST).
/// Strictly separates static record evidence from in-game runtime recruitability (DialogueFollower / RelationshipMarriage quests).
/// </summary>
public static class VoiceDialogueAnalyzer
{
    private static readonly string[] FollowerListFamily = ["VoicesFollower", "VoicesFollowerAll", "VoicesFollowerNeutral", "VoicesFollowerRough", "VoicesFollowerOrc"];
    private static readonly string[] MarriageListFamily = ["VoicesMarriage", "VoicesMarriageAll", "VoicesMarriageNeutral", "VoicesMarriageRough", "VoicesMarriageArrogant", "VoicesMarriageNord", "VoicesMarriageOrc", "VoicesMarriageArgonian"];

    public static readonly FormKey VanillaVoicesFollowerKey = LabSyntheticFixture.VoicesFollowerList;
    public static readonly FormKey VanillaVoicesMarriageKey = LabSyntheticFixture.VoicesMarriageList;
    public static readonly FormKey VanillaPotentialFollowerFactionKey = LabSyntheticFixture.PotentialFollowerFaction;
    public static readonly FormKey VanillaPotentialMarriageFactionKey = LabSyntheticFixture.PotentialMarriageFaction;

    public static VoiceDialogueAnalysisResult AnalyzeVoice(
        PluginContext context,
        string voiceFormKeyText,
        bool? draftIsFemale = null,
        IReadOnlyList<FactionMembershipDto>? draftFactions = null)
    {
        ArgumentNullException.ThrowIfNull(context);
        var issues = new List<BehaviorIssue>();

        if (!PluginContext.TryParseFormKey(voiceFormKeyText, out var voiceKey))
        {
            issues.Add(new BehaviorIssue(
                "VoiceFormKeyInvalid",
                ValidationSeverity.ERROR,
                $"Voice FormKey '{voiceFormKeyText}' is not a valid FormKey.",
                TargetFormKey: voiceFormKeyText));

            return BuildUnresolvedResult(voiceFormKeyText, issues, "Invalid VTYP FormKey syntax.");
        }

        if (context.IsModDisabled(voiceKey.ModKey))
        {
            issues.Add(new BehaviorIssue(
                "VoiceFromDisabledPlugin",
                ValidationSeverity.ERROR,
                $"VoiceType '{voiceKey}' belongs to disabled plugin '{voiceKey.ModKey.FileName}'.",
                TargetFormKey: voiceKey.ToString(),
                SourcePlugin: voiceKey.ModKey.FileName.String));

            return BuildUnresolvedResult(voiceKey.ToString(), issues, $"Voice belongs to disabled plugin '{voiceKey.ModKey.FileName}'.");
        }

        if (!context.LinkCache.TryResolveSimpleContext<IVoiceTypeGetter>(voiceKey, out var voiceCtx) || voiceCtx.Record.IsDeleted)
        {
            issues.Add(new BehaviorIssue(
                "VoiceNotFound",
                ValidationSeverity.ERROR,
                $"VoiceType (VTYP) '{voiceKey}' could not be resolved in the active load order.",
                TargetFormKey: voiceKey.ToString(),
                SourcePlugin: voiceKey.ModKey.FileName.String));

            return BuildUnresolvedResult(voiceKey.ToString(), issues, "VoiceType record does not exist in active plugins.");
        }

        var voiceRecord = voiceCtx.Record;
        var editorId = voiceRecord.EditorID ?? voiceRecord.FormKey.ToString();
        bool isFemaleVoice = voiceRecord.Flags.HasFlag(VoiceType.Flag.Female);
        bool isCreatureVoice = editorId.StartsWith("Cr", StringComparison.OrdinalIgnoreCase);

        if (draftIsFemale is { } expectedFemale && expectedFemale != isFemaleVoice && !isCreatureVoice)
        {
            issues.Add(new BehaviorIssue(
                "VoiceSexMismatch",
                ValidationSeverity.WARNING,
                $"NPC draft sex (female={expectedFemale}) differs from VoiceType '{editorId}' sex flag (female={isFemaleVoice}).",
                TargetFormKey: voiceRecord.FormKey.ToString(),
                SourcePlugin: voiceRecord.FormKey.ModKey.FileName.String,
                WinningPlugin: voiceCtx.ModKey.FileName.String));
        }

        var followerProv = InspectFormListProvenance(context, VanillaVoicesFollowerKey, "VoicesFollower", voiceRecord.FormKey);
        var marriageProv = InspectFormListProvenance(context, VanillaVoicesMarriageKey, "VoicesMarriage", voiceRecord.FormKey);

        EvidenceClassification followerClass;
        if (isCreatureVoice)
        {
            followerClass = EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE;
            issues.Add(new BehaviorIssue(
                "CreatureVoiceIncompatible",
                ValidationSeverity.ERROR,
                $"VoiceType '{editorId}' ({voiceRecord.FormKey}) is a creature voice and is incompatible with standard humanoid follower dialogue.",
                TargetFormKey: voiceRecord.FormKey.ToString(),
                SourcePlugin: voiceRecord.FormKey.ModKey.FileName.String));
        }
        else
        {
            followerClass = ClassifyFromProvenance(
                voiceRecord,
                followerProv,
                isMarriage: false,
                issues);
        }

        EvidenceClassification marriageClass;
        if (isCreatureVoice)
        {
            marriageClass = EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE;
        }
        else
        {
            marriageClass = ClassifyFromProvenance(
                voiceRecord,
                marriageProv,
                isMarriage: true,
                issues);
        }

        // V07: Having PotentialMarriageFaction without a compatible marriage voice NEVER makes marriage work
        if (draftFactions is not null && HasFaction(draftFactions, VanillaPotentialMarriageFactionKey))
        {
            if (marriageClass is not (EvidenceClassification.STATIC_EVIDENCE or EvidenceClassification.MOD_DEPENDENT))
            {
                issues.Add(new BehaviorIssue(
                    "MarriageFactionWithoutCompatibleVoice",
                    ValidationSeverity.WARNING,
                    $"NPC belongs to PotentialMarriageFaction ({VanillaPotentialMarriageFactionKey}), but voice '{editorId}' has no marriage dialogue list evidence (status={marriageClass}). Faction membership alone does NOT enable marriage in Skyrim.",
                    TargetFormKey: voiceRecord.FormKey.ToString(),
                    SourcePlugin: voiceRecord.FormKey.ModKey.FileName.String,
                    Evidence: $"Faction={VanillaPotentialMarriageFactionKey}; MarriageClassification={marriageClass}"));
            }
        }

        // Also warn if PotentialFollowerFaction is present but follower voice evidence is missing
        if (draftFactions is not null && HasFaction(draftFactions, VanillaPotentialFollowerFactionKey))
        {
            if (followerClass is not (EvidenceClassification.STATIC_EVIDENCE or EvidenceClassification.MOD_DEPENDENT))
            {
                issues.Add(new BehaviorIssue(
                    "FollowerFactionWithoutDialogueEvidence",
                    ValidationSeverity.WARNING,
                    $"NPC belongs to PotentialFollowerFaction ({VanillaPotentialFollowerFactionKey}), but voice '{editorId}' is not present in the winning VoicesFollower list (status={followerClass}). Faction membership alone does NOT make an NPC recruitable.",
                    TargetFormKey: voiceRecord.FormKey.ToString(),
                    SourcePlugin: voiceRecord.FormKey.ModKey.FileName.String,
                    Evidence: $"Faction={VanillaPotentialFollowerFactionKey}; FollowerClassification={followerClass}"));
            }
        }

        bool hasStaticFollower = followerClass is EvidenceClassification.STATIC_EVIDENCE or EvidenceClassification.MOD_DEPENDENT;
        bool hasStaticMarriage = marriageClass is EvidenceClassification.STATIC_EVIDENCE or EvidenceClassification.MOD_DEPENDENT;

        var rationale = BuildRationale(editorId, voiceRecord.FormKey, followerClass, followerProv, marriageClass, marriageProv);

        return new VoiceDialogueAnalysisResult
        {
            VoiceFormKey = voiceRecord.FormKey.ToString(),
            VoiceEditorId = editorId,
            VoiceResolved = true,
            DefiningPlugin = voiceRecord.FormKey.ModKey.FileName.String,
            IsFemaleVoice = isFemaleVoice,
            FollowerDialogueClassification = followerClass,
            MarriageDialogueClassification = marriageClass,
            HasStaticFollowerListEvidence = hasStaticFollower,
            HasStaticMarriageListEvidence = hasStaticMarriage,
            RuntimeVerificationState = "REQUIRES_IN_GAME_TEST",
            FollowerListProvenance = followerProv,
            MarriageListProvenance = marriageProv,
            Rationale = rationale,
            Issues = issues,
        };
    }

    public static IReadOnlyList<VoiceCatalogEntry> EnumerateActiveVoices(PluginContext context)
    {
        var entries = new List<VoiceCatalogEntry>();
        var winningVoices = context.EnabledPlugins
            .Reverse()
            .SelectMany(p => p.Mod.VoiceTypes.Select(v => (Record: (IVoiceTypeGetter)v, WinningMod: p.ModKey)))
            .DistinctBy(x => x.Record.FormKey)
            .Where(x => !x.Record.IsDeleted)
            .OrderBy(x => x.Record.EditorID, StringComparer.OrdinalIgnoreCase);

        foreach (var (record, winningMod) in winningVoices)
        {
            var analysis = AnalyzeVoice(context, record.FormKey.ToString());
            entries.Add(new VoiceCatalogEntry(
                FormKey: record.FormKey.ToString(),
                EditorId: record.EditorID ?? record.FormKey.ToString(),
                DefiningPlugin: record.FormKey.ModKey.FileName.String,
                WinningPlugin: winningMod.FileName.String,
                IsFemale: record.Flags.HasFlag(VoiceType.Flag.Female),
                FollowerEvidence: analysis.FollowerDialogueClassification,
                MarriageEvidence: analysis.MarriageDialogueClassification));
        }

        return entries;
    }

    private static FormListProvenance InspectFormListProvenance(
        PluginContext context,
        FormKey targetListKey,
        string expectedEditorId,
        FormKey voiceKey)
    {
        var chain = new List<(LoadedPluginInfo Plugin, IFormListGetter Record, bool HasVoice)>();

        // The real game has no FLST named "VoicesFollower" / "VoicesMarriage": it has VoicesFollowerAll / VoicesFollowerNeutral / ...Rough / ...Orc
        // and VoicesMarriageAll / ...Neutral / ...Rough / ...Arrogant / ...Nord / ...Orc / ...Argonian (the ...Adoption* lists are for adoption
        // dialogue, not marriage). A voice counts when any list of the family has it; the bare name stays for profiles that define it.
        var family = string.Equals(expectedEditorId, "VoicesMarriage", StringComparison.OrdinalIgnoreCase) ? MarriageListFamily : FollowerListFamily;
        foreach (var plugin in context.EnabledPlugins)
        {
            var matches = plugin.Mod.FormLists
                .Where(fl => !fl.IsDeleted && (fl.FormKey == targetListKey || (fl.EditorID is not null && family.Contains(fl.EditorID, StringComparer.OrdinalIgnoreCase))))
                .ToArray();
            if (matches.Length == 0) continue;
            // The list reported as "the" list is the canonical ...All one (or the one that was asked for by key), the rest only add evidence.
            var representative = matches.FirstOrDefault(fl => fl.FormKey == targetListKey) ?? matches[0];
            bool contains = matches.Any(fl => fl.Items.Any(i => i.FormKey == voiceKey));
            chain.Add((plugin, representative, contains));
        }

        if (chain.Count == 0)
        {
            return new FormListProvenance
            {
                ListFormKey = targetListKey.ToString(),
                ListEditorId = expectedEditorId,
                ListFound = false,
                PresentInVanilla = false,
                PresentInWinner = false,
                OverrideChain = [],
            };
        }

        var source = chain[0];
        var winner = chain[^1];
        bool presentInVanilla = chain
            .Where(c => PluginContext.IsVanillaBaseMaster(c.Plugin.ModKey))
            .Any(c => c.HasVoice);

        string? addedBy = null;
        string? removedBy = null;

        for (int i = 0; i < chain.Count; i++)
        {
            var current = chain[i];
            bool prevHas = i > 0 && chain[i - 1].HasVoice;
            if (current.HasVoice && (i == 0 ? !PluginContext.IsVanillaBaseMaster(current.Plugin.ModKey) : !prevHas))
            {
                addedBy ??= current.Plugin.ModKey.FileName.String;
            }
            if (i > 0 && prevHas && !current.HasVoice)
            {
                removedBy = current.Plugin.ModKey.FileName.String;
            }
        }

        // Also check if an earlier plugin in the chain had the voice and the final winner does not
        var firstHolder = chain.FirstOrDefault(c => c.HasVoice);
        if (firstHolder.Plugin is not null && !winner.HasVoice)
        {
            removedBy ??= winner.Plugin.ModKey.FileName.String;
        }

        return new FormListProvenance
        {
            ListFormKey = winner.Record.FormKey.ToString(),
            ListEditorId = winner.Record.EditorID ?? expectedEditorId,
            ListFound = true,
            SourcePlugin = source.Plugin.ModKey.FileName.String,
            WinningPlugin = winner.Plugin.ModKey.FileName.String,
            PresentInVanilla = presentInVanilla,
            PresentInWinner = winner.HasVoice,
            AddedByPlugin = addedBy,
            RemovedByPlugin = removedBy,
            OverrideChain = chain.Select(c => $"{c.Plugin.ModKey.FileName}:{(c.HasVoice ? "INCLUDED" : "ABSENT")}").ToArray(),
        };
    }

    private static EvidenceClassification ClassifyFromProvenance(
        IVoiceTypeGetter voice,
        FormListProvenance prov,
        bool isMarriage,
        List<BehaviorIssue> issues)
    {
        var domain = isMarriage ? "Marriage" : "Follower";
        var editorId = voice.EditorID ?? voice.FormKey.ToString();

        if (!prov.ListFound)
        {
            issues.Add(new BehaviorIssue(
                $"{domain}VoiceListMissing",
                ValidationSeverity.UNKNOWN,
                $"FormList '{prov.ListEditorId}' ({prov.ListFormKey}) was not found in the active load order; {domain.ToLowerInvariant()} voice compatibility is UNKNOWN.",
                TargetFormKey: voice.FormKey.ToString(),
                Evidence: $"Missing FLST {prov.ListFormKey}"));
            return EvidenceClassification.UNKNOWN;
        }

        if (prov.PresentInWinner)
        {
            bool voiceIsVanilla = PluginContext.IsVanillaBaseMaster(voice.FormKey.ModKey);
            if (prov.PresentInVanilla && voiceIsVanilla && prov.AddedByPlugin is null)
            {
                return EvidenceClassification.STATIC_EVIDENCE;
            }

            var providerPlugin = prov.AddedByPlugin ?? prov.WinningPlugin ?? voice.FormKey.ModKey.FileName.String;
            issues.Add(new BehaviorIssue(
                $"Voice{domain}ModDependent",
                ValidationSeverity.INFO,
                $"Voice '{editorId}' is included in '{prov.ListEditorId}' via mod '{providerPlugin}' (MOD_DEPENDENT). Requires that plugin and its dialogue/SEQ assets at runtime.",
                TargetFormKey: voice.FormKey.ToString(),
                SourcePlugin: voice.FormKey.ModKey.FileName.String,
                WinningPlugin: prov.WinningPlugin,
                Evidence: string.Join(" -> ", prov.OverrideChain)));
            return EvidenceClassification.MOD_DEPENDENT;
        }

        if (prov.RemovedByPlugin is not null)
        {
            var earlierProvider = prov.PresentInVanilla ? prov.SourcePlugin : (prov.AddedByPlugin ?? prov.SourcePlugin);
            issues.Add(new BehaviorIssue(
                $"Voice{domain}RemovedByOverride",
                ValidationSeverity.ERROR,
                $"Conflict detected on '{prov.ListEditorId}': voice '{editorId}' was provided by '{earlierProvider}' but removed by winning override '{prov.RemovedByPlugin}'.",
                TargetFormKey: voice.FormKey.ToString(),
                SourcePlugin: earlierProvider,
                WinningPlugin: prov.RemovedByPlugin,
                Evidence: string.Join(" -> ", prov.OverrideChain)));
            return EvidenceClassification.INCOMPATIBLE_BY_KNOWN_RULE;
        }

        // Valid VTYP, list exists, but voice is not in the list (V01)
        issues.Add(new BehaviorIssue(
            $"VoiceNotIn{domain}List",
            ValidationSeverity.WARNING,
            $"VoiceType '{editorId}' ({voice.FormKey}) is a valid VTYP record, but is NOT present in '{prov.ListEditorId}' ({prov.ListFormKey}). Do not assume standard {domain.ToLowerInvariant()} dialogue will appear without custom quest conditions.",
            TargetFormKey: voice.FormKey.ToString(),
            SourcePlugin: voice.FormKey.ModKey.FileName.String,
            WinningPlugin: prov.WinningPlugin,
            Evidence: string.Join(" -> ", prov.OverrideChain)));

        return EvidenceClassification.UNKNOWN;
    }

    private static bool HasFaction(IReadOnlyList<FactionMembershipDto> factions, FormKey targetFaction)
    {
        foreach (var f in factions)
        {
            if (PluginContext.TryParseFormKey(f.FormKey, out var parsed) && parsed == targetFaction && f.Rank >= 0)
            {
                return true;
            }
        }
        return false;
    }

    private static string BuildRationale(
        string editorId,
        FormKey voiceKey,
        EvidenceClassification followerClass,
        FormListProvenance followerProv,
        EvidenceClassification marriageClass,
        FormListProvenance marriageProv)
    {
        return $"Voice '{editorId}' ({voiceKey}): FollowerDialogue={followerClass} (listFound={followerProv.ListFound}, inVanilla={followerProv.PresentInVanilla}, inWinner={followerProv.PresentInWinner}, winner={followerProv.WinningPlugin ?? "none"}); " +
               $"MarriageDialogue={marriageClass} (listFound={marriageProv.ListFound}, inVanilla={marriageProv.PresentInVanilla}, inWinner={marriageProv.PresentInWinner}, winner={marriageProv.WinningPlugin ?? "none"}). " +
               "Static record analysis only; actual recruitment and marriage dialogue require in-game verification (REQUIRES_IN_GAME_TEST).";
    }

    private static VoiceDialogueAnalysisResult BuildUnresolvedResult(
        string voiceFormKeyText,
        IReadOnlyList<BehaviorIssue> issues,
        string rationale) =>
        new()
        {
            VoiceFormKey = voiceFormKeyText,
            VoiceResolved = false,
            FollowerDialogueClassification = EvidenceClassification.UNKNOWN,
            MarriageDialogueClassification = EvidenceClassification.UNKNOWN,
            HasStaticFollowerListEvidence = false,
            HasStaticMarriageListEvidence = false,
            RuntimeVerificationState = "REQUIRES_IN_GAME_TEST",
            Rationale = rationale,
            Issues = issues,
        };
}
