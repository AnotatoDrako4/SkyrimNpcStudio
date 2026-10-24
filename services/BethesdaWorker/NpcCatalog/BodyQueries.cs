using System.Globalization;
using Microsoft.Data.Sqlite;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Aspects;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.RecordEditor;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>
/// body.inspectNpcs: what decides an NPC's body in game (sex, race and its flags, skin and its ARMA, the Traits
/// template chain, keywords, factions), read from the winning records through the session's link cache. The
/// catalog index only supplies the population, origin/winner plugins and the FormKey spelling. Records shared
/// by many NPCs (races, skins, factions...) are resolved once per call.
/// </summary>
internal static class BodyQueries
{
    public const int MaxItems = 2000;
    private const int MaxTemplateDepth = 16;

    public static BodyInspectResponse Inspect(Catalog catalog, BodyInspectRequest request, CancellationToken ct = default)
    {
        var female = request.Sex?.Trim().ToLowerInvariant() switch
        {
            null or "" => (bool?)null,
            "female" => true,
            "male" => false,
            _ => throw new ArgumentException($"Unknown sex filter '{request.Sex}'."),
        };

        var rows = new List<WinnerRow>();
        var missing = new List<string>();
        int total;
        if (request.FormKeys is { } requested)
        {
            if (requested.Count > MaxItems)
            {
                throw new ArgumentException($"At most {MaxItems} FormKeys per call ({requested.Count} were sent).");
            }

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var text in requested)
            {
                var raw = text ?? "";
                if (!catalog.Queries.TryParseAnyFormKey(raw, out var key))
                {
                    missing.Add(raw);
                    continue;
                }

                if (!seen.Add(key.ToString()))
                {
                    continue;
                }

                var row = Winner(catalog.Store.Connection, key.ToString());
                if (row is null)
                {
                    missing.Add(raw);
                }
                else if (female is null || row.Female == female)
                {
                    rows.Add(row);
                }
            }

            total = rows.Count;
        }
        else
        {
            (total, rows) = Page(catalog.Store.Connection, female, Math.Max(0, request.Offset), Math.Clamp(request.Limit, 1, MaxItems));
        }

        var resolver = new Resolver(catalog.Session);
        var items = new List<BodyNpcInfo>(rows.Count);
        foreach (var row in rows)
        {
            ct.ThrowIfCancellationRequested();
            var key = FormKey.Factory(row.FormKey);
            if (!catalog.Session.LinkCache.TryResolve<INpcGetter>(key, out var npc))
            {
                missing.Add(row.FormKey);
                continue;
            }

            items.Add(resolver.Describe(row, key, npc));
        }

        return new BodyInspectResponse { Total = total, Items = items, Missing = missing };
    }

    private sealed record WinnerRow(string FormKey, string OriginPlugin, string WinningPlugin, bool Female);

    private static WinnerRow? Winner(SqliteConnection db, string key)
    {
        using var cmd = db.CreateCommand();
        cmd.CommandText = """
            SELECT w.form_key, w.origin_plugin, w.winning_plugin, n.female
            FROM npc_winner w JOIN npc_records n ON n.plugin_id = w.plugin_id AND n.form_key = w.form_key
            WHERE w.form_key = $k
            """;
        cmd.Parameters.AddWithValue("$k", key);
        using var r = cmd.ExecuteReader();
        return r.Read() ? new WinnerRow(r.GetString(0), r.GetString(1), r.GetString(2), r.GetInt32(3) != 0) : null;
    }

    private static (int Total, List<WinnerRow> Rows) Page(SqliteConnection db, bool? female, int offset, int limit)
    {
        const string from = """
             FROM npc_winner w JOIN npc_records n ON n.plugin_id = w.plugin_id AND n.form_key = w.form_key
            """;
        var where = female is null ? "" : " WHERE n.female = $female";

        int total;
        using (var count = db.CreateCommand())
        {
            count.CommandText = "SELECT COUNT(*)" + from + where;
            if (female is { } f) count.Parameters.AddWithValue("$female", f ? 1 : 0);
            total = Convert.ToInt32(count.ExecuteScalar(), CultureInfo.InvariantCulture);
        }

        var rows = new List<WinnerRow>();
        using var cmd = db.CreateCommand();
        // A stable order (the FormKey) keeps consecutive pages disjoint.
        cmd.CommandText = "SELECT w.form_key, w.origin_plugin, w.winning_plugin, n.female" + from + where
            + " ORDER BY w.form_key COLLATE NOCASE LIMIT $limit OFFSET $offset";
        if (female is { } g) cmd.Parameters.AddWithValue("$female", g ? 1 : 0);
        cmd.Parameters.AddWithValue("$limit", limit);
        cmd.Parameters.AddWithValue("$offset", offset);
        using var r = cmd.ExecuteReader();
        while (r.Read())
        {
            rows.Add(new WinnerRow(r.GetString(0), r.GetString(1), r.GetString(2), r.GetInt32(3) != 0));
        }

        return (total, rows);
    }

    /// <summary>Resolves and memoizes the records NPCs share, for one call.</summary>
    private sealed class Resolver(LoadOrderSession session)
    {
        private readonly Dictionary<(FormKey, string), RecordRef> _refs = [];
        private readonly Dictionary<FormKey, IRaceGetter?> _races = [];
        private readonly Dictionary<FormKey, string?> _editorIds = [];
        private readonly Dictionary<FormKey, (IArmorAddonGetter? Addon, string? Note)[]> _skins = [];

        public BodyNpcInfo Describe(WinnerRow row, FormKey key, INpcGetter npc)
        {
            var notes = new List<string>();
            var config = npc.Configuration;
            var female = config.Flags.HasFlag(NpcConfiguration.Flag.Female);
            var templateKey = npc.Template.FormKeyNullable;

            var race = npc.Race.FormKeyNullable is { } raceKey ? Race(raceKey) : null;
            if (npc.Race.FormKeyNullable is null)
            {
                notes.Add("The NPC has no race (RNAM).");
            }
            else if (race is null)
            {
                notes.Add($"Race {npc.Race.FormKey} is not in the active load order.");
            }

            var (ownSkinKey, skinSource) = SkinOf(npc, race);
            if (skinSource == "none")
            {
                notes.Add("No skin: neither the NPC (WNAM) nor its race defines one; the game shows no body.");
            }

            // Traits (race, sex, skin, face...) come from the template chain while each link inherits them.
            var traits = TraitsSupplier(npc, notes, out var unresolved);
            INpcGetter? traitsNpc = traits;
            IRaceGetter? effectiveRace = null;
            FormKey? effectiveSkinKey = null;
            if (traitsNpc is not null)
            {
                if (ReferenceEquals(traitsNpc, npc))
                {
                    effectiveRace = race;
                    effectiveSkinKey = ownSkinKey;
                }
                else
                {
                    effectiveRace = traitsNpc.Race.FormKeyNullable is { } r ? Race(r) : null;
                    if (effectiveRace is null)
                    {
                        notes.Add($"The race of the Traits template {traitsNpc.FormKey} is not in the active load order.");
                    }

                    effectiveSkinKey = SkinOf(traitsNpc, effectiveRace).Skin;
                    if (effectiveSkinKey is null)
                    {
                        notes.Add($"The Traits template {traitsNpc.FormKey} has no skin (WNAM) and neither has its race.");
                    }
                }
            }

            var flagsRace = effectiveRace ?? race;
            var raceFlags = flagsRace?.Flags ?? 0;
            var matchRaces = new HashSet<FormKey>();
            if (flagsRace is not null)
            {
                matchRaces.Add(flagsRace.FormKey);
                if (flagsRace.ArmorRace.FormKeyNullable is { } armorRace) matchRaces.Add(armorRace);
            }

            var addonSkin = effectiveSkinKey ?? ownSkinKey;
            var addons = new List<BodySkinAddon>();
            if (addonSkin is { } skinKey)
            {
                foreach (var (addon, note) in SkinAddons(skinKey))
                {
                    if (note is not null)
                    {
                        notes.Add(note);
                    }

                    if (addon is null)
                    {
                        continue;
                    }

                    var additional = addon.AdditionalRaces.Select(a => a.FormKey).ToArray();
                    addons.Add(new BodySkinAddon
                    {
                        FormKey = addon.FormKey.ToString(),
                        EditorId = addon.EditorID,
                        Race = addon.Race.FormKeyNullable?.ToString(),
                        AdditionalRaces = additional.Select(a => a.ToString()).ToArray(),
                        FemaleModel = NullIfEmpty(addon.WorldModel?.Female?.File.GivenPath),
                        MaleModel = NullIfEmpty(addon.WorldModel?.Male?.File.GivenPath),
                        BodySlots = BipedSlots.ToSlots((uint)(addon.BodyTemplate?.FirstPersonFlags ?? 0)),
                        AppliesToRace = (addon.Race.FormKeyNullable is { } ar && matchRaces.Contains(ar))
                                        || additional.Any(matchRaces.Contains),
                    });
                }

                if (addons.Count > 0 && matchRaces.Count > 0 && !addons.Any(a => a.AppliesToRace))
                {
                    notes.Add($"No ARMA of skin {skinKey} applies to race {flagsRace!.EditorID ?? flagsRace.FormKey.ToString()}.");
                }
            }

            var headParts = new List<BodyHeadPart>();
            foreach (var link in npc.HeadParts)
            {
                if (session.LinkCache.TryResolve<IHeadPartGetter>(link.FormKey, out var part))
                {
                    headParts.Add(new BodyHeadPart
                    {
                        FormKey = part.FormKey.ToString(),
                        EditorId = part.EditorID,
                        PartType = part.Type?.ToString(),
                        ModelPath = NullIfEmpty(part.Model?.File.GivenPath),
                    });
                }
                else
                {
                    notes.Add($"Head part {link.FormKey} is not in the active load order.");
                }
            }

            var keywords = new List<BodyKeywordRef>();
            foreach (var link in npc.Keywords ?? [])
            {
                var editorId = EditorIdOf<IKeywordGetter>(link.FormKey);
                if (editorId is null && !session.LinkCache.TryResolve<IKeywordGetter>(link.FormKey, out _))
                {
                    notes.Add($"Keyword {link.FormKey} is not in the active load order.");
                }

                keywords.Add(new BodyKeywordRef { FormKey = link.FormKey.ToString(), EditorId = editorId });
            }

            var factions = new List<BodyFactionRef>();
            foreach (var placement in npc.Factions)
            {
                factions.Add(new BodyFactionRef
                {
                    FormKey = placement.Faction.FormKey.ToString(),
                    EditorId = EditorIdOf<IFactionGetter>(placement.Faction.FormKey),
                    Rank = placement.Rank,
                });
            }

            return new BodyNpcInfo
            {
                FormKey = row.FormKey,
                FormId = session.FormatFormId(key),
                EditorId = npc.EditorID,
                Name = npc.Name?.String,
                Sex = female ? "female" : "male",
                EffectiveSex = traitsNpc is null ? null
                    : traitsNpc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female) ? "female" : "male",
                OriginPlugin = row.OriginPlugin,
                WinningPlugin = row.WinningPlugin,
                IsUnique = config.Flags.HasFlag(NpcConfiguration.Flag.Unique),
                UsesTemplate = templateKey is not null,
                TemplateFlags = FlagNames(config.TemplateFlags),
                Template = templateKey is { } t ? TemplateRef(t) : null,
                Race = npc.Race.FormKeyNullable is { } rk ? Ref<IRaceGetter>(rk, "RACE") : null,
                IsChild = raceFlags.HasFlag(Mutagen.Bethesda.Skyrim.Race.Flag.Child),
                IsPlayable = raceFlags.HasFlag(Mutagen.Bethesda.Skyrim.Race.Flag.Playable),
                RaceKeywords = (flagsRace?.Keywords ?? []).Select(k => EditorIdOf<IKeywordGetter>(k.FormKey) ?? k.FormKey.ToString()).ToArray(),
                RaceSkin = flagsRace?.Skin.FormKeyNullable is { } rs ? Ref<IArmorGetter>(rs, "ARMO") : null,
                EffectiveRace = effectiveRace is null ? null : Ref<IRaceGetter>(effectiveRace.FormKey, "RACE"),
                EffectiveSkin = effectiveSkinKey is { } es ? Ref<IArmorGetter>(es, "ARMO") : null,
                TraitsFrom = traitsNpc?.FormKey.ToString(),
                TemplateUnresolved = unresolved,
                Keywords = keywords,
                Class = npc.Class.FormKeyNullable is { } c ? Ref<IClassGetter>(c, "CLAS") : null,
                CombatStyle = npc.CombatStyle.FormKeyNullable is { } cs ? Ref<ICombatStyleGetter>(cs, "CSTY") : null,
                Factions = factions,
                SkinSource = skinSource,
                Skin = ownSkinKey is { } os ? Ref<IArmorGetter>(os, "ARMO") : null,
                SkinAddons = addons,
                HeadParts = headParts,
                FaceGen = CatalogQueries.FaceGenFor(key),
                Notes = notes,
            };
        }

        /// <summary>
        /// The NPC_ that supplies the traits: follows the template while the current record inherits Traits.
        /// Null (and <paramref name="unresolved"/>) when the chain ends in a leveled list, a missing record,
        /// a cycle or deeper than <see cref="MaxTemplateDepth"/> links: only the game resolves those.
        /// </summary>
        private INpcGetter? TraitsSupplier(INpcGetter npc, List<string> notes, out bool unresolved)
        {
            unresolved = false;
            var visited = new HashSet<FormKey> { npc.FormKey };
            var current = npc;
            for (var depth = 0; ; depth++)
            {
                if (!current.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits)
                    || current.Template.FormKeyNullable is not { } next)
                {
                    return current;
                }

                if (depth >= MaxTemplateDepth)
                {
                    notes.Add($"The Traits template chain is longer than {MaxTemplateDepth} links; race and skin are not resolved.");
                    break;
                }

                if (!visited.Add(next))
                {
                    notes.Add($"The Traits template chain loops back to {next}; race and skin are not resolved.");
                    break;
                }

                if (session.LinkCache.TryResolve<INpcGetter>(next, out var template))
                {
                    current = template;
                    continue;
                }

                notes.Add(session.LinkCache.TryResolve<ILeveledNpcGetter>(next, out _)
                    ? $"Traits come from the leveled NPC list {next}: race and skin are chosen in game."
                    : $"Template {next} is not in the active load order; race and skin are not resolved.");
                break;
            }

            unresolved = true;
            return null;
        }

        private (FormKey? Skin, string Source) SkinOf(INpcGetter npc, IRaceGetter? race)
        {
            if (npc.WornArmor.FormKeyNullable is { } own)
            {
                return (own, "npc");
            }

            return race?.Skin.FormKeyNullable is { } raceSkin ? (raceSkin, "race") : (null, "none");
        }

        private (IArmorAddonGetter? Addon, string? Note)[] SkinAddons(FormKey skinKey)
        {
            if (_skins.TryGetValue(skinKey, out var cached))
            {
                return cached;
            }

            (IArmorAddonGetter?, string?)[] result;
            if (!session.LinkCache.TryResolve<IArmorGetter>(skinKey, out var skin))
            {
                result = [(null, $"Skin {skinKey} is not in the active load order.")];
            }
            else
            {
                result = skin.Armature.Select(link => session.LinkCache.TryResolve<IArmorAddonGetter>(link.FormKey, out var addon)
                    ? ((IArmorAddonGetter?)addon, (string?)null)
                    : (null, $"Skin {skinKey} references a missing ARMA {link.FormKey}.")).ToArray();
            }

            _skins[skinKey] = result;
            return result;
        }

        private IRaceGetter? Race(FormKey key)
        {
            if (!_races.TryGetValue(key, out var race))
            {
                race = session.LinkCache.TryResolve<IRaceGetter>(key, out var resolved) ? resolved : null;
                _races[key] = race;
            }

            return race;
        }

        private RecordRef TemplateRef(FormKey key) =>
            session.LinkCache.TryResolve<INpcGetter>(key, out _) ? Ref<INpcGetter>(key, "NPC_") : Ref<ILeveledNpcGetter>(key, "LVLN");

        private RecordRef Ref<T>(FormKey key, string recordType)
            where T : class, IMajorRecordGetter
        {
            if (_refs.TryGetValue((key, recordType), out var cached))
            {
                return cached;
            }

            var reference = session.LinkCache.TryResolveSimpleContext<T>(key, out var context)
                ? new RecordRef
                {
                    FormKey = key.ToString(),
                    RecordType = recordType,
                    EditorId = context.Record.EditorID,
                    Name = (context.Record as INamedGetter)?.Name,
                    WinningPlugin = context.ModKey.FileName.String,
                }
                : new RecordRef { FormKey = key.ToString(), RecordType = recordType, Resolved = false };
            _refs[(key, recordType)] = reference;
            return reference;
        }

        private string? EditorIdOf<T>(FormKey key)
            where T : class, IMajorRecordGetter
        {
            if (!_editorIds.TryGetValue(key, out var editorId))
            {
                editorId = session.LinkCache.TryResolve<T>(key, out var record) ? record.EditorID : null;
                _editorIds[key] = editorId;
            }

            return editorId;
        }
    }

    private static string? NullIfEmpty(string? text) => string.IsNullOrEmpty(text) ? null : text;

    private static IReadOnlyList<string> FlagNames<TEnum>(TEnum value) where TEnum : struct, Enum =>
        Enum.GetValues<TEnum>().Where(f => Convert.ToUInt64(f, CultureInfo.InvariantCulture) != 0 && value.HasFlag(f)).Select(f => f.ToString()).ToArray();
}
