using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

/// <summary>
/// Executes category-scoped dry-run archetype applications over 1..N synthetic NPC drafts and provides
/// exact cryptographic undo restoration without touching appearance, identity, FaceGen, or placement fields.
/// </summary>
public static class BulkArchetypeEngine
{
    public static readonly IReadOnlyList<string> DefaultAuthorizedCategories =
    [
        "class",
        "combatStyle",
        "stats",
        "skills",
        "equipment",
        "spells",
        "perks",
        "factions",
        "packages"
    ];

    public static readonly IReadOnlyList<string> ProtectedCategories =
    [
        "identity (name, editorId, description)",
        "raceAndSex (raceFormKey, female)",
        "voice (voiceFormKey)",
        "bodyAndMorphs (height, weight, face)",
        "appearanceAssets (headPartFormKeys, hairColorFormKey, headTextureFormKey)",
        "levelScaling (level, levelMultiplier, minLevel, maxLevel)",
        "worldPlacement (placement)"
    ];

    public static BulkDryRunReport ApplyArchetypeDryRun(
        PluginContext context,
        IReadOnlyList<FollowerDraftDto> sourceDrafts,
        ArchetypeTemplate archetype,
        IReadOnlyCollection<string>? requestedCategories = null)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(sourceDrafts);
        ArgumentNullException.ThrowIfNull(archetype);

        if (sourceDrafts.Count == 0)
        {
            throw new LabUsageException("EmptyBatch", "At least one draft is required for apply-archetype.", 2);
        }
        if (sourceDrafts.Count > SandboxGuard.MaxCollectionItems)
        {
            throw new LabUsageException("BatchTooLarge", $"Batch count ({sourceDrafts.Count}) exceeds limit ({SandboxGuard.MaxCollectionItems}).", 2);
        }

        var activeCategories = NormalizeCategories(requestedCategories);
        var archetypeVal = ArchetypePlanner.ValidateArchetype(context, archetype);
        var entries = new List<DraftDryRunEntry>(sourceDrafts.Count);
        bool allUntouched = true;

        for (int i = 0; i < sourceDrafts.Count; i++)
        {
            var src = sourceDrafts[i];
            var hashBefore = SandboxGuard.ComputeObjectCanonicalSha256(src);
            var undoClone = CloneDraft(src);

            var changes = new List<FieldChangeDto>();

            var newClass = activeCategories.Contains("class") ? archetype.ClassFormKey : src.ClassFormKey;
            RecordChangeIfDifferent(changes, "class", "classFormKey", src.ClassFormKey, newClass);

            var newStyle = activeCategories.Contains("combatStyle") ? archetype.CombatStyleFormKey : src.CombatStyleFormKey;
            RecordChangeIfDifferent(changes, "combatStyle", "combatStyleFormKey", src.CombatStyleFormKey, newStyle);

            int newHealth = activeCategories.Contains("stats") ? archetype.Health : src.Health;
            int newMagicka = activeCategories.Contains("stats") ? archetype.Magicka : src.Magicka;
            int newStamina = activeCategories.Contains("stats") ? archetype.Stamina : src.Stamina;
            RecordChangeIfDifferent(changes, "stats", "health", src.Health, newHealth);
            RecordChangeIfDifferent(changes, "stats", "magicka", src.Magicka, newMagicka);
            RecordChangeIfDifferent(changes, "stats", "stamina", src.Stamina, newStamina);

            IReadOnlyDictionary<string, int> newSkills = activeCategories.Contains("skills")
                ? new Dictionary<string, int>(archetype.Skills, StringComparer.Ordinal)
                : new Dictionary<string, int>(src.Skills, StringComparer.Ordinal);
            RecordChangeIfDifferent(changes, "skills", "skills", src.Skills, newSkills);

            IReadOnlyList<string> newOutfit = activeCategories.Contains("equipment")
                ? archetype.OutfitArmorFormKeys.ToArray()
                : src.OutfitArmorFormKeys.ToArray();
            RecordChangeIfDifferent(changes, "equipment", "outfitArmorFormKeys", src.OutfitArmorFormKeys, newOutfit);

            IReadOnlyList<InventoryItemDto> newInventory = activeCategories.Contains("equipment")
                ? archetype.Inventory.Select(x => new InventoryItemDto(x.FormKey, x.Count)).ToArray()
                : src.Inventory.Select(x => new InventoryItemDto(x.FormKey, x.Count)).ToArray();
            RecordChangeIfDifferent(changes, "equipment", "inventory", src.Inventory, newInventory);

            IReadOnlyList<string> newSpells = activeCategories.Contains("spells")
                ? archetype.SpellFormKeys.ToArray()
                : src.SpellFormKeys.ToArray();
            RecordChangeIfDifferent(changes, "spells", "spellFormKeys", src.SpellFormKeys, newSpells);

            IReadOnlyList<string> newPerks = activeCategories.Contains("perks")
                ? archetype.PerkFormKeys.ToArray()
                : src.PerkFormKeys.ToArray();
            RecordChangeIfDifferent(changes, "perks", "perkFormKeys", src.PerkFormKeys, newPerks);

            IReadOnlyList<FactionMembershipDto> newFactions = activeCategories.Contains("factions")
                ? archetype.Factions.Select(f => new FactionMembershipDto(f.FormKey, f.Rank)).ToArray()
                : src.Factions.Select(f => new FactionMembershipDto(f.FormKey, f.Rank)).ToArray();
            RecordChangeIfDifferent(changes, "factions", "factions", src.Factions, newFactions);

            IReadOnlyList<string> newPackages = activeCategories.Contains("packages")
                ? archetype.PackageFormKeys.ToArray()
                : src.PackageFormKeys.ToArray();
            RecordChangeIfDifferent(changes, "packages", "packageFormKeys", src.PackageFormKeys, newPackages);

            var proposed = src with
            {
                ClassFormKey = newClass,
                CombatStyleFormKey = newStyle,
                Health = newHealth,
                Magicka = newMagicka,
                Stamina = newStamina,
                Skills = newSkills,
                OutfitArmorFormKeys = newOutfit,
                Inventory = newInventory,
                SpellFormKeys = newSpells,
                PerkFormKeys = newPerks,
                Factions = newFactions,
                PackageFormKeys = newPackages,
            };

            var hashAfter = SandboxGuard.ComputeObjectCanonicalSha256(src);
            bool untouched = string.Equals(hashBefore, hashAfter, StringComparison.Ordinal);
            if (!untouched)
            {
                allUntouched = false;
            }

            entries.Add(new DraftDryRunEntry
            {
                Index = i,
                EditorId = src.EditorId,
                Name = src.Name,
                SourceSha256Before = hashBefore,
                SourceSha256After = hashAfter,
                SourceUntouched = untouched,
                ProposedSha256 = SandboxGuard.ComputeObjectCanonicalSha256(proposed),
                AppliedCategories = activeCategories.ToArray(),
                Changes = changes,
                ProposedDraft = proposed,
                UndoRestoreToken = undoClone,
            });
        }

        return new BulkDryRunReport
        {
            GeneratedAtUtc = DateTime.UtcNow.ToString("o"),
            DryRun = true,
            ArchetypeId = archetype.Id,
            LoadOrderFingerprint = context.LoadOrderFingerprint,
            RequestedCategories = activeCategories.ToArray(),
            ProtectedCategories = ProtectedCategories,
            ArchetypeValidation = archetypeVal,
            TotalDrafts = entries.Count,
            AllSourcesUntouched = allUntouched,
            Entries = entries,
            Issues = archetypeVal.Issues,
        };
    }

    /// <summary>
    /// Reverses a batch archetype application using the report's UndoRestoreToken and verifies that each restored
    /// draft matches its original pre-operation SHA-256 hash bit-for-bit.
    /// </summary>
    public static IReadOnlyList<FollowerDraftDto> UndoBatch(BulkDryRunReport report)
    {
        ArgumentNullException.ThrowIfNull(report);
        var restored = new List<FollowerDraftDto>(report.Entries.Count);

        foreach (var entry in report.Entries)
        {
            var candidate = CloneDraft(entry.UndoRestoreToken);
            var actualHash = SandboxGuard.ComputeObjectCanonicalSha256(candidate);
            if (!string.Equals(actualHash, entry.SourceSha256Before, StringComparison.Ordinal))
            {
                throw new LabUsageException(
                    "UndoIntegrityMismatch",
                    $"Undo restoration hash mismatch for draft '{entry.EditorId}' (expected {entry.SourceSha256Before}, got {actualHash}).",
                    4);
            }
            restored.Add(candidate);
        }

        return restored;
    }

    private static HashSet<string> NormalizeCategories(IReadOnlyCollection<string>? requested)
    {
        if (requested is null || requested.Count == 0)
        {
            return new HashSet<string>(DefaultAuthorizedCategories, StringComparer.OrdinalIgnoreCase);
        }

        var allowed = new HashSet<string>(DefaultAuthorizedCategories, StringComparer.OrdinalIgnoreCase);
        var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var raw in requested)
        {
            foreach (var token in raw.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                if (!allowed.Contains(token))
                {
                    throw new LabUsageException(
                        "UnauthorizedCategory",
                        $"Category '{token}' is not an authorized behavior category. Allowed: {string.Join(", ", DefaultAuthorizedCategories)}.",
                        2);
                }
                result.Add(token);
            }
        }
        return result;
    }

    private static void RecordChangeIfDifferent<T>(
        List<FieldChangeDto> changes,
        string category,
        string fieldName,
        T beforeValue,
        T afterValue)
    {
        var beforeJson = JsonSerializer.Serialize(beforeValue, SandboxGuard.CanonicalJsonOptions);
        var afterJson = JsonSerializer.Serialize(afterValue, SandboxGuard.CanonicalJsonOptions);
        if (!string.Equals(beforeJson, afterJson, StringComparison.Ordinal))
        {
            changes.Add(new FieldChangeDto(category, fieldName, beforeJson, afterJson));
        }
    }

    private static FollowerDraftDto CloneDraft(FollowerDraftDto d)
    {
        var json = JsonSerializer.Serialize(d, SandboxGuard.CanonicalJsonOptions);
        return JsonSerializer.Deserialize<FollowerDraftDto>(json, SandboxGuard.SafeJsonOptions)!;
    }
}
