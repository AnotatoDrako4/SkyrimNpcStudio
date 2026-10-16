using System.Text.Json;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Strings;
using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>Turns the records of one plugin into index rows. Malformed records are reported, not fatal.</summary>
internal static class RecordExtractor
{
    /// <summary>Bump whenever extraction output changes, so cached plugin rows are rebuilt.</summary>
    public const int Version = 7;

    /// <summary>
    /// Localized names live in separate .STRINGS files (loose or in BSAs). When they cannot be read
    /// the record is still indexed without a name, and the plugin reports one summary issue.
    /// </summary>
    private sealed class NameReader
    {
        public int Failures { get; private set; }
        public string? FirstError { get; private set; }

        public string? Read(ITranslatedStringGetter? text)
        {
            try
            {
                // Some mods ship names that are just whitespace (seen in the wild: " "): treat as no name.
                var value = text?.String;
                return string.IsNullOrWhiteSpace(value) ? null : value.Trim();
            }
            catch (Exception ex)
            {
                Failures++;
                FirstError ??= ex.Message;
                return null;
            }
        }
    }

    /// <summary>NPC fields tracked across overrides. The first block affects how the NPC looks.</summary>
    public static readonly IReadOnlyList<string> VisualFields =
    [
        "Race", "Sex", "HeadParts", "HairColor", "HeadTexture", "FaceMorph", "FaceParts",
        "TintLayers", "TextureLighting", "Height", "Weight", "WornArmor", "DefaultOutfit", "SleepingOutfit",
    ];

    public static readonly IReadOnlyList<string> OtherTrackedFields =
    [
        "Name", "Template", "TemplateFlags", "Flags", "Class", "Voice", "Factions", "Inventory",
        "Spells", "Perks", "Packages", "Keywords", "AIData", "Scripts",
    ];

    internal static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    public static PluginExtract Extract(ISkyrimModGetter mod, CancellationToken ct)
    {
        var issues = new List<Issue>();
        var records = new List<GenericRow>();
        var npcs = new List<NpcRow>();
        var armors = new List<ArmorRow>();
        var plugin = mod.ModKey.FileName.String;
        var names = new NameReader();

        void Guarded<T>(IEnumerable<T> source, Action<T> action) where T : IMajorRecordGetter
        {
            using var e = source.GetEnumerator();
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                T current;
                try
                {
                    if (!e.MoveNext())
                    {
                        break;
                    }

                    current = e.Current;
                }
                catch (Exception ex)
                {
                    issues.Add(Issue.Error("GroupUnreadable", $"Stopped reading a record group: {ex.Message}", plugin));
                    break;
                }

                try
                {
                    action(current);
                }
                catch (Exception ex)
                {
                    issues.Add(Issue.Warning("RecordUnreadable", $"Skipped a malformed record: {ex.Message}", SafeFormKey(current)));
                }
            }
        }

        Guarded(mod.Npcs, npc =>
        {
            var row = ExtractNpc(npc, names);
            npcs.Add(row);
            records.Add(new GenericRow(row.FormKey, "NPC_", row.EditorId, row.Name, null));
        });

        Guarded(mod.Armors, armor =>
        {
            var row = ExtractArmor(armor, names);
            armors.Add(row);
            records.Add(new GenericRow(row.FormKey, "ARMO", row.EditorId, row.Name, null));
        });

        var outfitCount = 0;
        Guarded(mod.Outfits, outfit =>
        {
            var items = outfit.Items?.Select(i => i.FormKey.ToString()).ToArray() ?? [];
            records.Add(new GenericRow(outfit.FormKey.ToString(), "OTFT", outfit.EditorID, null, Serialize(new { items })));
            outfitCount++;
        });

        Guarded(mod.ArmorAddons, arma =>
        {
            var data = new
            {
                race = arma.Race.FormKeyNullable?.ToString(),
                additionalRaces = arma.AdditionalRaces.Select(r => r.FormKey.ToString()).ToArray(),
                slotMask = (uint)(arma.BodyTemplate?.FirstPersonFlags ?? 0),
                maleModel = arma.WorldModel?.Male?.File.GivenPath,
                femaleModel = arma.WorldModel?.Female?.File.GivenPath,
            };
            records.Add(new GenericRow(arma.FormKey.ToString(), "ARMA", arma.EditorID, null, Serialize(data)));
        });

        Guarded(mod.Races, race => records.Add(new GenericRow(
            race.FormKey.ToString(), "RACE", race.EditorID, names.Read(race.Name),
            Serialize(new { armorRace = race.ArmorRace.FormKeyNullable?.ToString() }))));

        Guarded(mod.HeadParts, part => records.Add(new GenericRow(
            part.FormKey.ToString(), "HDPT", part.EditorID, names.Read(part.Name),
            Serialize(new { type = part.Type?.ToString(), model = part.Model?.File.GivenPath }))));

        Guarded(mod.Factions, x => records.Add(new GenericRow(x.FormKey.ToString(), "FACT", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.Classes, x => records.Add(new GenericRow(x.FormKey.ToString(), "CLAS", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.VoiceTypes, x => records.Add(new GenericRow(x.FormKey.ToString(), "VTYP", x.EditorID, null, null)));
        Guarded(mod.Colors, x => records.Add(new GenericRow(x.FormKey.ToString(), "CLFM", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.TextureSets, x => records.Add(new GenericRow(x.FormKey.ToString(), "TXST", x.EditorID, null, null)));
        Guarded(mod.LeveledNpcs, x => records.Add(new GenericRow(x.FormKey.ToString(), "LVLN", x.EditorID, null, null)));
        Guarded(mod.LeveledItems, x => records.Add(new GenericRow(x.FormKey.ToString(), "LVLI", x.EditorID, null, null)));
        Guarded(mod.Weapons, x => records.Add(new GenericRow(x.FormKey.ToString(), "WEAP", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.Ammunitions, x => records.Add(new GenericRow(x.FormKey.ToString(), "AMMO", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.MiscItems, x => records.Add(new GenericRow(x.FormKey.ToString(), "MISC", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.Keys, x => records.Add(new GenericRow(x.FormKey.ToString(), "KEYM", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.Books, x => records.Add(new GenericRow(x.FormKey.ToString(), "BOOK", x.EditorID, names.Read(x.Name), null)));
        Guarded(mod.Ingestibles, x => records.Add(new GenericRow(x.FormKey.ToString(), "ALCH", x.EditorID, names.Read(x.Name), null)));

        var locations = new List<PlacedNpcRow>();
        Guarded(mod.EnumerateMajorRecords<ICellGetter>(), cell =>
        {
            var cellName = names.Read(cell.Name);
            var cellId = cell.EditorID;
            var cellKey = cell.FormKey.ToString();
            if (cellId is not null || cellName is not null)
            {
                records.Add(new GenericRow(cellKey, "CELL", cellId, cellName, null));
            }

            void CheckPlaced(IReadOnlyList<IPlacedGetter>? placedList)
            {
                if (placedList is null) return;
                for (var i = 0; i < placedList.Count; i++)
                {
                    if (placedList[i] is IPlacedNpcGetter achr && achr.Base.FormKeyNullable is { } baseKey)
                    {
                        locations.Add(new PlacedNpcRow(
                            FormKey: achr.FormKey.ToString(),
                            NpcFormKey: baseKey.ToString(),
                            EditorId: achr.EditorID,
                            CellFormKey: cellKey,
                            CellName: cellName,
                            CellEditorId: cellId));
                    }
                }
            }

            CheckPlaced(cell.Persistent);
            CheckPlaced(cell.Temporary);
        });

        if (names.Failures > 0)
        {
            issues.Add(Issue.Warning(
                "StringsUnavailable",
                $"{names.Failures} localized names could not be read and were indexed without a name ({names.FirstError}). Check the plugin's .STRINGS files or its BSA.",
                plugin));
        }

        return new PluginExtract(records, npcs, armors, outfitCount, locations, issues);
    }

    private static NpcRow ExtractNpc(INpcGetter npc, NameReader names)
    {
        var name = names.Read(npc.Name);
        var config = npc.Configuration;
        var flags = config.Flags;
        var data = new NpcData
        {
            Class = npc.Class.FormKeyNullable?.ToString(),
            Voice = npc.Voice.FormKeyNullable?.ToString(),
            WornArmor = npc.WornArmor.FormKeyNullable?.ToString(),
            HairColor = npc.HairColor.FormKeyNullable?.ToString(),
            HeadTexture = npc.HeadTexture.FormKeyNullable?.ToString(),
            Height = npc.Height,
            Weight = npc.Weight,
            Flags = (int)flags,
            HeadParts = npc.HeadParts.Select(h => h.FormKey.ToString()).ToArray(),
            Items = npc.Items?.Select(i => new ItemCount(i.Item.Item.FormKey.ToString(), i.Item.Count)).ToArray() ?? [],
        };

        return new NpcRow(
            FormKey: npc.FormKey.ToString(),
            EditorId: npc.EditorID,
            Name: name,
            Race: npc.Race.FormKeyNullable?.ToString(),
            Female: flags.HasFlag(NpcConfiguration.Flag.Female),
            Unique: flags.HasFlag(NpcConfiguration.Flag.Unique),
            Preset: flags.HasFlag(NpcConfiguration.Flag.IsCharGenFacePreset),
            Template: npc.Template.FormKeyNullable?.ToString(),
            TemplateFlags: (int)config.TemplateFlags,
            DefaultOutfit: npc.DefaultOutfit.FormKeyNullable?.ToString(),
            SleepingOutfit: npc.SleepingOutfit.FormKeyNullable?.ToString(),
            Factions: npc.Factions.Select(f => (f.Faction.FormKey.ToString(), (int)f.Rank)).ToArray(),
            Data: data,
            Fingerprints: Fingerprints(npc, name));
    }

    /// <summary>
    /// Fingerprint of every tracked field (VisualFields + OtherTrackedFields), as stored in the catalog.
    /// <paramref name="name"/> is the name as the game shows it (localized string tables resolved).
    /// </summary>
    internal static Dictionary<string, string> Fingerprints(INpcGetter npc, string? name)
    {
        var config = npc.Configuration;
        var flags = config.Flags;
        return new Dictionary<string, string>
        {
            ["Race"] = Fingerprint.Of(npc.Race.FormKey),
            ["Sex"] = Fingerprint.Of(flags.HasFlag(NpcConfiguration.Flag.Female)),
            ["HeadParts"] = Fingerprint.Of(npc.HeadParts),
            ["HairColor"] = Fingerprint.Of(npc.HairColor),
            ["HeadTexture"] = Fingerprint.Of(npc.HeadTexture),
            ["FaceMorph"] = Fingerprint.Of(npc.FaceMorph),
            ["FaceParts"] = Fingerprint.Of(npc.FaceParts),
            ["TintLayers"] = Fingerprint.Of(npc.TintLayers),
            ["TextureLighting"] = Fingerprint.Of(npc.TextureLighting),
            ["Height"] = Fingerprint.Of(npc.Height),
            ["Weight"] = Fingerprint.Of(npc.Weight),
            ["WornArmor"] = Fingerprint.Of(npc.WornArmor),
            ["DefaultOutfit"] = Fingerprint.Of(npc.DefaultOutfit),
            ["SleepingOutfit"] = Fingerprint.Of(npc.SleepingOutfit),
            ["Name"] = Fingerprint.Of(name),
            ["Template"] = Fingerprint.Of(npc.Template),
            ["TemplateFlags"] = Fingerprint.Of(config.TemplateFlags),
            ["Flags"] = Fingerprint.Of(flags),
            ["Class"] = Fingerprint.Of(npc.Class),
            ["Voice"] = Fingerprint.Of(npc.Voice),
            ["Factions"] = Fingerprint.Of(npc.Factions),
            ["Inventory"] = Fingerprint.Of(npc.Items),
            ["Spells"] = Fingerprint.Of(npc.ActorEffect),
            ["Perks"] = Fingerprint.Of(npc.Perks),
            ["Packages"] = Fingerprint.Of(npc.Packages),
            ["Keywords"] = Fingerprint.Of(npc.Keywords),
            ["AIData"] = Fingerprint.Of(npc.AIData),
            ["Scripts"] = Fingerprint.Of(npc.VirtualMachineAdapter),
        };
    }

    private static ArmorRow ExtractArmor(IArmorGetter armor, NameReader names)
    {
        var body = armor.BodyTemplate;
        var nonPlayable = armor.MajorFlags.HasFlag(Armor.MajorFlag.NonPlayable)
                          || (body?.Flags.HasFlag(BodyTemplate.Flag.NonPlayable) ?? false);
        return new ArmorRow(
            FormKey: armor.FormKey.ToString(),
            EditorId: armor.EditorID,
            Name: names.Read(armor.Name),
            SlotMask: (uint)(body?.FirstPersonFlags ?? 0),
            ArmorType: body?.ArmorType.ToString() ?? "Unknown",
            NonPlayable: nonPlayable,
            MaleModel: armor.WorldModel?.Male?.Model?.File.GivenPath,
            FemaleModel: armor.WorldModel?.Female?.Model?.File.GivenPath,
            Addons: armor.Armature.Select(a => a.FormKey.ToString()).ToArray());
    }

    private static string Serialize<T>(T value) => JsonSerializer.Serialize(value, Json);

    private static string? SafeFormKey(IMajorRecordGetter record)
    {
        try
        {
            return record.FormKey.ToString();
        }
        catch (Exception)
        {
            return null;
        }
    }
}
