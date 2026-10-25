using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.RecordEditor;

namespace SkyrimNpcStudio.Worker.Validation;

/// <summary>
/// Record-level validation of an outfit for a given NPC, against the winning records of the
/// active load order. Asset existence (meshes/textures in the MO2 virtual file system) is
/// validated by the studio, which owns the VFS.
/// </summary>
internal static class OutfitValidator
{
    public static OutfitValidateResponse Validate(LoadOrderSession session, CatalogQueries queries, OutfitValidateRequest request)
    {
        var issues = new List<Issue>();
        var pieces = new List<OutfitPiece>();

        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var npcKey)
            || !session.LinkCache.TryResolve<INpcGetter>(npcKey, out var npc))
        {
            issues.Add(Issue.Error("NpcNotFound", $"NPC '{request.NpcFormKey}' is not in the active load order.", request.NpcFormKey));
            return new OutfitValidateResponse { Exportable = false, CombinedSlotMask = 0, Pieces = pieces, Issues = issues };
        }

        return ValidateForNpc(session, queries, npc, request.ArmorFormKeys);
    }

    // Creation uses the same rules with a transient new NPC, never an existing NPC's draft.
    internal static OutfitValidateResponse ValidateForNpc(LoadOrderSession session, CatalogQueries queries,
        INpcGetter npc, IReadOnlyList<string> armorFormKeys)
    {
        var issues = new List<Issue>();
        var pieces = new List<OutfitPiece>();
        var female = npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var raceKeys = RaceAndArmorRace(session, npc);

        if (npc.Template.FormKeyNullable is { } template
            && npc.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Inventory))
        {
            issues.Add(Issue.Error(
                "OutfitFromTemplate",
                "This NPC inherits its inventory (and outfits) from its template because the 'Inventory' template flag is set. A new default outfit on this record would be ignored by the game.",
                template.ToString()));
        }

        if (armorFormKeys.Count == 0)
        {
            issues.Add(Issue.Warning("EmptyOutfit", "The outfit has no pieces: the NPC will appear without clothes."));
        }

        // Pieces already in the NPC's current default outfit (winning record). Two of them sharing a slot
        // is how that outfit already is in game (one of them is equipped): not introduced by this edit,
        // so it does not block it; a conflict involving a piece the edit adds still does.
        var inherited = new HashSet<FormKey>();
        if (npc.DefaultOutfit.FormKeyNullable is { } currentOutfit && session.LinkCache.TryResolve<IOutfitGetter>(currentOutfit, out var outfit))
        {
            foreach (var item in outfit.Items ?? [])
            {
                inherited.Add(item.FormKey);
            }
        }

        uint combined = 0;
        var owners = new Dictionary<int, string>();
        var ownerKeys = new Dictionary<int, FormKey>();
        var seen = new HashSet<FormKey>();
        foreach (var text in armorFormKeys)
        {
            if (!queries.TryParseAnyFormKey(text, out var armorKey)
                || !session.LinkCache.TryResolve<IArmorGetter>(armorKey, out var armor))
            {
                issues.Add(Issue.Error("ArmorNotFound", $"Armor '{text}' is not in the active load order.", text));
                continue;
            }

            if (!seen.Add(armorKey))
            {
                issues.Add(Issue.Warning("DuplicatePiece", "The same piece is listed twice; only one copy is kept.", armorKey.ToString()));
                continue;
            }

            var label = armor.Name?.String ?? armor.EditorID ?? armorKey.ToString();
            var mask = (uint)(armor.BodyTemplate?.FirstPersonFlags ?? 0);
            if (mask == 0)
            {
                issues.Add(Issue.Warning("NoBipedSlots", $"'{label}' does not occupy any biped slot.", armorKey.ToString()));
            }

            foreach (var slot in BipedSlots.ToSlots(mask & combined))
            {
                var message = $"'{label}' and '{owners[slot]}' both use slot {BipedSlots.Describe(slot)}; the game would equip only one of them.";
                issues.Add(inherited.Contains(armorKey) && inherited.Contains(ownerKeys[slot])
                    ? Issue.Warning("SlotConflictInherited", message + " Both are already in the NPC's current outfit.", armorKey.ToString())
                    : Issue.Error("SlotConflict", message, armorKey.ToString()));
            }

            foreach (var slot in BipedSlots.ToSlots(mask & ~combined))
            {
                owners[slot] = label;
                ownerKeys[slot] = armorKey;
            }

            combined |= mask;

            var matching = 0;
            var meshes = new List<string>();
            var missingModel = false;
            foreach (var link in armor.Armature)
            {
                if (!session.LinkCache.TryResolve<IArmorAddonGetter>(link.FormKey, out var arma))
                {
                    issues.Add(Issue.Warning("AddonNotFound", $"'{label}' references a missing ARMA {link.FormKey}.", armorKey.ToString()));
                    continue;
                }

                var covers = (arma.Race.FormKeyNullable is { } r && raceKeys.Contains(r))
                             || arma.AdditionalRaces.Any(a => raceKeys.Contains(a.FormKey));
                if (!covers)
                {
                    continue;
                }

                matching++;
                var model = female ? arma.WorldModel?.Female : arma.WorldModel?.Male;
                if (model?.File.GivenPath is { Length: > 0 } path)
                {
                    meshes.Add(path);
                }
                else
                {
                    missingModel = true;
                }
            }

            if (matching == 0)
            {
                issues.Add(Issue.Error(
                    "NoAddonForRace",
                    $"No ARMA of '{label}' supports the NPC's race; the piece would be invisible in game. A race compatibility patch is required.",
                    armorKey.ToString()));
            }
            else if (missingModel)
            {
                issues.Add(Issue.Warning(
                    "NoModelForSex",
                    $"'{label}' has an addon for this race without a {(female ? "female" : "male")} model.",
                    armorKey.ToString()));
            }

            if (armor.MajorFlags.HasFlag(Armor.MajorFlag.NonPlayable))
            {
                issues.Add(Issue.Info("NonPlayable", $"'{label}' is non-playable: NPCs can wear it, the player cannot take it.", armorKey.ToString()));
            }

            var summary = queries.GetArmor(armorKey.ToString());
            if (summary is null)
            {
                issues.Add(Issue.Error("ArmorNotIndexed", $"'{label}' is not in the catalog index; re-index the profile.", armorKey.ToString()));
                continue;
            }

            pieces.Add(new OutfitPiece { Armor = summary, AddonsForRace = matching, Meshes = meshes });
        }

        issues.Add(Issue.Info(
            "SaveGamePersistence",
            "NPCs already loaded in an existing save can keep their current equipment until the game re-initializes them; immediate application in every save is not guaranteed."));

        return new OutfitValidateResponse
        {
            Exportable = issues.All(i => i.Severity != "error"),
            CombinedSlotMask = combined,
            Pieces = pieces,
            Issues = issues,
        };
    }

    /// <summary>The NPC's race plus its armor race (e.g. NordRace → DefaultRace), which ARMA lists target.</summary>
    private static HashSet<FormKey> RaceAndArmorRace(LoadOrderSession session, INpcGetter npc) =>
        npc.Race.FormKeyNullable is { } raceKey ? RaceAncestry.CollectRaces(session, raceKey) : [];
}
