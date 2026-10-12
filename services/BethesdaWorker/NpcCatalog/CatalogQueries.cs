using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.Data.Sqlite;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.RecordEditor;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>Read-only queries over the catalog of the active load order.</summary>
internal sealed class CatalogQueries(CatalogStore store, LoadOrderSession session)
{
    private const int MaxLimit = 1000;
    private readonly SqliteConnection _db = store.Connection;

    public sealed record CountsResult(int Npcs, int Armors, int Outfits);

    public static CountsResult Counts(CatalogStore store)
    {
        static int Count(SqliteConnection db, string sql)
        {
            using var cmd = db.CreateCommand();
            cmd.CommandText = sql;
            return Convert.ToInt32(cmd.ExecuteScalar(), CultureInfo.InvariantCulture);
        }

        return new CountsResult(
            Count(store.Connection, "SELECT COUNT(*) FROM npc_winner"),
            Count(store.Connection, "SELECT COUNT(*) FROM armor_winner"),
            Count(store.Connection, "SELECT COUNT(*) FROM record_winner WHERE record_type = 'OTFT'"));
    }

    public ListPluginsResponse ListPlugins()
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = """
            SELECT lo.load_index, lo.file_name, lo.form_id_prefix, lo.is_light, lo.is_master, lo.masters, lo.path,
                   p.npc_count, p.armor_count, p.outfit_count
            FROM load_order lo JOIN plugin_files p ON p.id = lo.plugin_id ORDER BY lo.load_index
            """;
        using var r = cmd.ExecuteReader();
        var list = new List<PluginInfo>();
        while (r.Read())
        {
            list.Add(new PluginInfo
            {
                LoadIndex = r.GetInt32(0),
                FileName = r.GetString(1),
                FormIdPrefix = r.GetString(2),
                IsLight = r.GetInt32(3) != 0,
                IsMaster = r.GetInt32(4) != 0,
                Masters = JsonSerializer.Deserialize<string[]>(r.GetString(5)) ?? [],
                Path = r.GetString(6),
                NpcRecords = r.GetInt32(7),
                ArmorRecords = r.GetInt32(8),
                OutfitRecords = r.GetInt32(9),
            });
        }

        return new ListPluginsResponse { Plugins = list };
    }

    // ---------------------------------------------------------------- NPC search

    public NpcSearchResponse SearchNpcs(NpcSearchRequest request)
    {
        var where = new StringBuilder(" WHERE 1=1");
        var parameters = new List<(string, object)>();

        if (!string.IsNullOrWhiteSpace(request.Text))
        {
            var text = request.Text.Trim();
            if (TryParseAnyFormKey(text, out var formKey))
            {
                where.Append(" AND w.form_key = $fk");
                parameters.Add(("$fk", formKey.ToString()));
            }
            else
            {
                where.Append(" AND n.search_text LIKE $text ESCAPE '\\'");
                parameters.Add(("$text", "%" + EscapeLike(text.ToLowerInvariant()) + "%"));
            }
        }

        switch (request.Sex?.ToLowerInvariant())
        {
            case null or "": break;
            case "female": where.Append(" AND n.female = 1"); break;
            case "male": where.Append(" AND n.female = 0"); break;
            default: throw new ArgumentException($"Unknown sex filter '{request.Sex}'.");
        }

        if (!string.IsNullOrWhiteSpace(request.Race))
        {
            where.Append(" AND (n.race = $race OR n.race IN (SELECT rw.form_key FROM record_winner rw JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key WHERE rw.record_type = 'RACE' AND r.editor_id = $race COLLATE NOCASE))");
            parameters.Add(("$race", NormalizeKey(request.Race)));
        }

        if (!string.IsNullOrWhiteSpace(request.Plugin))
        {
            where.Append(" AND w.form_key IN (SELECT n2.form_key FROM npc_records n2 JOIN load_order lo2 ON lo2.plugin_id = n2.plugin_id WHERE lo2.file_name = $plugin COLLATE NOCASE)");
            parameters.Add(("$plugin", request.Plugin.Trim()));
        }

        if (!string.IsNullOrWhiteSpace(request.Faction))
        {
            where.Append(" AND EXISTS (SELECT 1 FROM npc_factions f WHERE f.plugin_id = w.plugin_id AND f.form_key = w.form_key AND (f.faction = $faction OR f.faction IN (SELECT rw.form_key FROM record_winner rw JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key WHERE rw.record_type = 'FACT' AND r.editor_id = $faction COLLATE NOCASE)))");
            parameters.Add(("$faction", NormalizeKey(request.Faction)));
        }

        switch (request.Kind?.ToLowerInvariant())
        {
            case null or "": break;
            case "unique": where.Append(" AND n.is_unique = 1 AND n.is_preset = 0"); break;
            case "generic": where.Append(" AND n.is_unique = 0 AND n.is_preset = 0 AND n.template IS NULL"); break;
            case "templated": where.Append(" AND n.template IS NOT NULL"); break;
            case "preset": where.Append(" AND n.is_preset = 1"); break;
            default: throw new ArgumentException($"Unknown kind filter '{request.Kind}'.");
        }

        const string from = """
             FROM npc_winner w
             JOIN npc_records n ON n.plugin_id = w.plugin_id AND n.form_key = w.form_key
            """;

        int total;
        using (var count = _db.CreateCommand())
        {
            count.CommandText = "SELECT COUNT(*)" + from + where;
            foreach (var (name, value) in parameters)
            {
                count.Parameters.AddWithValue(name, value);
            }

            total = Convert.ToInt32(count.ExecuteScalar(), CultureInfo.InvariantCulture);
        }

        using var cmd = _db.CreateCommand();
        cmd.CommandText = $"""
            SELECT w.form_key, n.editor_id, n.name, n.race, n.female, w.origin_plugin, w.winning_plugin,
                   w.override_count, n.is_unique, n.template
            {from}{where}
            ORDER BY COALESCE(n.name, n.editor_id, w.form_key) COLLATE NOCASE, w.form_key
            LIMIT $limit OFFSET $offset
            """;
        foreach (var (name, value) in parameters)
        {
            cmd.Parameters.AddWithValue(name, value);
        }

        cmd.Parameters.AddWithValue("$limit", Math.Clamp(request.Limit, 1, MaxLimit));
        cmd.Parameters.AddWithValue("$offset", Math.Max(0, request.Offset));

        var items = new List<NpcSummary>();
        using (var r = cmd.ExecuteReader())
        {
            while (r.Read())
            {
                items.Add(ReadSummary(r));
            }
        }

        return new NpcSearchResponse { Total = total, Items = items };
    }

    private NpcSummary ReadSummary(SqliteDataReader r)
    {
        var formKey = r.GetString(0);
        var race = Str(r, 3);
        return new NpcSummary
        {
            FormKey = formKey,
            FormId = session.FormatFormId(FormKey.Factory(formKey)),
            EditorId = Str(r, 1),
            Name = Str(r, 2),
            RaceFormKey = race,
            RaceEditorId = race is null ? null : Ref(race).EditorId,
            Sex = r.GetInt32(4) != 0 ? "female" : "male",
            OriginPlugin = r.GetString(5),
            WinningPlugin = r.GetString(6),
            OverrideCount = r.GetInt32(7),
            IsUnique = r.GetInt32(8) != 0,
            UsesTemplate = !r.IsDBNull(9),
        };
    }

    // ---------------------------------------------------------------- NPC details

    public NpcDetails? GetNpc(string formKeyText)
    {
        if (!TryParseAnyFormKey(formKeyText, out var formKey))
        {
            throw new ArgumentException($"'{formKeyText}' is not a FormKey.");
        }

        var key = formKey.ToString();
        NpcSummary summary;
        NpcData data;
        int templateFlags;
        string? template, defaultOutfit, sleepingOutfit;
        long winnerPluginId;
        using (var cmd = _db.CreateCommand())
        {
            cmd.CommandText = """
                SELECT w.form_key, n.editor_id, n.name, n.race, n.female, w.origin_plugin, w.winning_plugin,
                       w.override_count, n.is_unique, n.template, n.template_flags, n.default_outfit,
                       n.sleeping_outfit, n.data, w.plugin_id
                FROM npc_winner w JOIN npc_records n ON n.plugin_id = w.plugin_id AND n.form_key = w.form_key
                WHERE w.form_key = $k
                """;
            cmd.Parameters.AddWithValue("$k", key);
            using var r = cmd.ExecuteReader();
            if (!r.Read())
            {
                return null;
            }

            summary = ReadSummary(r);
            template = Str(r, 9);
            templateFlags = r.GetInt32(10);
            defaultOutfit = Str(r, 11);
            sleepingOutfit = Str(r, 12);
            data = JsonSerializer.Deserialize<NpcData>(r.GetString(13), RecordExtractor.Json)!;
            winnerPluginId = r.GetInt64(14);
        }

        var overrides = OverrideChain(key);
        var flags = (NpcConfiguration.Flag)data.Flags;
        var tflags = (NpcConfiguration.TemplateFlag)templateFlags;
        var notes = new List<Issue>();
        if (template is not null && tflags.HasFlag(NpcConfiguration.TemplateFlag.Traits))
        {
            notes.Add(Issue.Info("AppearanceFromTemplate", "Appearance (race, face, head parts) is inherited from the template; this record's face data is not used in game.", template));
        }

        if (template is not null && tflags.HasFlag(NpcConfiguration.TemplateFlag.Inventory))
        {
            notes.Add(Issue.Warning("OutfitFromTemplate", "Inventory and outfits are inherited from the template; changing this record's outfit has no effect in game.", template));
        }

        if (flags.HasFlag(NpcConfiguration.Flag.IsCharGenFacePreset))
        {
            notes.Add(Issue.Info("CharGenPreset", "This record is a character-creation face preset, not a placed NPC."));
        }

        var locations = Locations(key);
        if (locations.Count == 0 && !flags.HasFlag(NpcConfiguration.Flag.IsCharGenFacePreset))
        {
            notes.Add(Issue.Info("NoPlacedReference", "No placed references (ACHR) found for this NPC in the active load order (may be spawned dynamically or by template)."));
        }

        return new NpcDetails
        {
            Summary = summary,
            Race = summary.RaceFormKey is null ? null : Ref(summary.RaceFormKey),
            Class = RefOrNull(data.Class),
            Voice = RefOrNull(data.Voice),
            Template = RefOrNull(template),
            WornArmor = RefOrNull(data.WornArmor),
            HairColor = RefOrNull(data.HairColor),
            HeadTexture = RefOrNull(data.HeadTexture),
            DefaultOutfit = defaultOutfit is null ? null : Outfit(defaultOutfit),
            SleepingOutfit = sleepingOutfit is null ? null : Outfit(sleepingOutfit),
            Flags = SetFlagNames(flags),
            TemplateFlags = SetFlagNames(tflags),
            Height = data.Height,
            Weight = data.Weight,
            HeadParts = data.HeadParts.Select(HeadPart).ToArray(),
            Factions = Factions(winnerPluginId, key),
            Inventory = data.Items.Select(i => new InventoryItem { Item = Ref(i.Item), Count = i.Count }).ToArray(),
            Overrides = overrides.Entries,
            Conflicts = overrides.Conflicts,
            FaceGen = FaceGenFor(formKey),
            Locations = locations,
            Notes = notes,
        };
    }

    public static FaceGenPaths FaceGenFor(FormKey formKey)
    {
        // FaceGen files are named after the ORIGIN plugin (the one that created the NPC),
        // never after the plugin that overrides it, and use the plugin-local FormID.
        var plugin = formKey.ModKey.FileName.String;
        var id = (formKey.ID & 0xFFFFFF).ToString("X8", CultureInfo.InvariantCulture);
        return new FaceGenPaths
        {
            GeometryPath = $@"meshes\actors\character\facegendata\facegeom\{plugin}\{id}.nif",
            TintPath = $@"textures\actors\character\facegendata\facetint\{plugin}\{id}.dds",
        };
    }

    private sealed record Chain(IReadOnlyList<OverrideEntry> Entries, IReadOnlyList<FieldConflict> Conflicts);

    private Chain OverrideChain(string key)
    {
        var rows = new List<(int Index, string Plugin, Dictionary<string, string> Fp)>();
        using (var cmd = _db.CreateCommand())
        {
            cmd.CommandText = """
                SELECT lo.load_index, lo.file_name, n.fingerprints FROM npc_records n
                JOIN load_order lo ON lo.plugin_id = n.plugin_id WHERE n.form_key = $k ORDER BY lo.load_index
                """;
            cmd.Parameters.AddWithValue("$k", key);
            using var r = cmd.ExecuteReader();
            while (r.Read())
            {
                rows.Add((r.GetInt32(0), r.GetString(1),
                    JsonSerializer.Deserialize<Dictionary<string, string>>(r.GetString(2), RecordExtractor.Json)!));
            }
        }

        var origin = key[(key.IndexOf(':', StringComparison.Ordinal) + 1)..];
        var entries = new List<OverrideEntry>(rows.Count);
        for (var i = 0; i < rows.Count; i++)
        {
            var changed = i == 0
                ? []
                : rows[i].Fp.Where(kv => rows[i - 1].Fp.GetValueOrDefault(kv.Key) != kv.Value).Select(kv => kv.Key).Order(StringComparer.Ordinal).ToList();
            entries.Add(new OverrideEntry
            {
                Plugin = rows[i].Plugin,
                LoadIndex = rows[i].Index,
                IsOrigin = string.Equals(rows[i].Plugin, origin, StringComparison.OrdinalIgnoreCase),
                IsWinner = i == rows.Count - 1,
                ChangedFields = changed,
            });
        }

        return new Chain(entries, DetectLostEdits(rows.Select(r => (r.Plugin, r.Fp)).ToList()));
    }

    /// <summary>
    /// A field is in conflict when some non-winning plugin changed it relative to the previous
    /// version and the winner ends up with a different value: that plugin's edit is lost in game.
    /// </summary>
    internal static IReadOnlyList<FieldConflict> DetectLostEdits(IReadOnlyList<(string Plugin, Dictionary<string, string> Fp)> chain)
    {
        if (chain.Count < 3)
        {
            // origin + a single override: the override wins by definition, nothing is lost.
            return [];
        }

        var winner = chain[^1];
        var conflicts = new List<FieldConflict>();
        foreach (var field in RecordExtractor.VisualFields.Concat(RecordExtractor.OtherTrackedFields))
        {
            var losers = new List<string>();
            for (var i = 1; i < chain.Count - 1; i++)
            {
                var value = chain[i].Fp.GetValueOrDefault(field);
                var changedHere = value != chain[i - 1].Fp.GetValueOrDefault(field);
                if (changedHere && value != winner.Fp.GetValueOrDefault(field))
                {
                    losers.Add(chain[i].Plugin);
                }
            }

            if (losers.Count > 0)
            {
                conflicts.Add(new FieldConflict { Field = field, WinningPlugin = winner.Plugin, LosingPlugins = losers });
            }
        }

        return conflicts;
    }

    private IReadOnlyList<FactionRank> Factions(long pluginId, string key)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = "SELECT faction, rank FROM npc_factions WHERE plugin_id = $p AND form_key = $k";
        cmd.Parameters.AddWithValue("$p", pluginId);
        cmd.Parameters.AddWithValue("$k", key);
        using var r = cmd.ExecuteReader();
        var list = new List<(string Faction, int Rank)>();
        while (r.Read())
        {
            list.Add((r.GetString(0), r.GetInt32(1)));
        }

        return list.Select(f => new FactionRank { Faction = Ref(f.Faction), Rank = f.Rank }).ToArray();
    }

    private IReadOnlyList<NpcLocation> Locations(string npcKey)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = """
            SELECT loc.form_key, loc.editor_id, loc.cell_form_key, loc.cell_name, loc.cell_editor_id, lo.file_name
            FROM npc_locations loc
            JOIN load_order lo ON lo.plugin_id = loc.plugin_id
            WHERE loc.npc_form_key = $k
            ORDER BY lo.load_index ASC
            """;
        cmd.Parameters.AddWithValue("$k", npcKey);
        using var r = cmd.ExecuteReader();
        var list = new List<NpcLocation>();
        while (r.Read())
        {
            list.Add(new NpcLocation
            {
                FormKey = r.GetString(0),
                EditorId = Str(r, 1),
                CellFormKey = Str(r, 2),
                CellName = Str(r, 3),
                CellEditorId = Str(r, 4),
                Plugin = r.GetString(5),
            });
        }

        return list;
    }

    private HeadPartRef HeadPart(string formKey)
    {
        var record = Ref(formKey);
        string? type = null, model = null;
        if (RecordData(formKey) is { } json)
        {
            using var doc = JsonDocument.Parse(json);
            type = doc.RootElement.TryGetProperty("type", out var t) ? t.GetString() : null;
            model = doc.RootElement.TryGetProperty("model", out var m) ? m.GetString() : null;
        }

        return new HeadPartRef { Record = record, PartType = type, ModelPath = model };
    }

    public OutfitContents Outfit(string formKey)
    {
        var outfit = Ref(formKey);
        var armors = new List<ArmorSummary>();
        var others = new List<RecordRef>();
        if (RecordData(formKey) is { } json)
        {
            using var doc = JsonDocument.Parse(json);
            foreach (var item in doc.RootElement.GetProperty("items").EnumerateArray())
            {
                var itemKey = item.GetString()!;
                if (GetArmor(itemKey) is { } armor)
                {
                    armors.Add(armor);
                }
                else
                {
                    others.Add(Ref(itemKey));
                }
            }
        }

        return new OutfitContents { Outfit = outfit, Armors = armors, OtherEntries = others };
    }

    // ---------------------------------------------------------------- armor

    public ArmorSearchResponse SearchArmors(ArmorSearchRequest request)
    {
        var where = new StringBuilder(" WHERE 1=1");
        var parameters = new List<(string, object)>();
        if (!string.IsNullOrWhiteSpace(request.Text))
        {
            var text = request.Text.Trim();
            if (TryParseAnyFormKey(text, out var formKey))
            {
                where.Append(" AND w.form_key = $fk");
                parameters.Add(("$fk", formKey.ToString()));
            }
            else
            {
                where.Append(" AND a.search_text LIKE $text ESCAPE '\\'");
                parameters.Add(("$text", "%" + EscapeLike(text.ToLowerInvariant()) + "%"));
            }
        }

        if (!string.IsNullOrWhiteSpace(request.Plugin))
        {
            where.Append(" AND w.form_key IN (SELECT a2.form_key FROM armor_records a2 JOIN load_order lo2 ON lo2.plugin_id = a2.plugin_id WHERE lo2.file_name = $plugin COLLATE NOCASE)");
            parameters.Add(("$plugin", request.Plugin.Trim()));
        }

        if (request.SlotMask is { } mask and not 0)
        {
            where.Append(" AND (a.slot_mask & $mask) != 0");
            parameters.Add(("$mask", (long)mask));
        }

        if (!request.IncludeNonPlayable)
        {
            where.Append(" AND a.non_playable = 0");
        }

        const string from = " FROM armor_winner w JOIN armor_records a ON a.plugin_id = w.plugin_id AND a.form_key = w.form_key";
        int total;
        using (var count = _db.CreateCommand())
        {
            count.CommandText = "SELECT COUNT(*)" + from + where;
            foreach (var (name, value) in parameters)
            {
                count.Parameters.AddWithValue(name, value);
            }

            total = Convert.ToInt32(count.ExecuteScalar(), CultureInfo.InvariantCulture);
        }

        using var cmd = _db.CreateCommand();
        cmd.CommandText = ArmorSelect + from + where +
                          " ORDER BY COALESCE(a.name, a.editor_id, w.form_key) COLLATE NOCASE, w.form_key LIMIT $limit OFFSET $offset";
        foreach (var (name, value) in parameters)
        {
            cmd.Parameters.AddWithValue(name, value);
        }

        cmd.Parameters.AddWithValue("$limit", Math.Clamp(request.Limit, 1, MaxLimit));
        cmd.Parameters.AddWithValue("$offset", Math.Max(0, request.Offset));
        var items = new List<ArmorSummary>();
        using (var r = cmd.ExecuteReader())
        {
            while (r.Read())
            {
                items.Add(ReadArmor(r));
            }
        }

        return new ArmorSearchResponse { Total = total, Items = items };
    }

    private const string ArmorSelect = """
        SELECT w.form_key, a.editor_id, a.name, w.origin_plugin, w.winning_plugin, a.slot_mask, a.armor_type,
               a.non_playable, a.male_model, a.female_model, a.addons
        """;

    public ArmorSummary? GetArmor(string formKey)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = ArmorSelect + " FROM armor_winner w JOIN armor_records a ON a.plugin_id = w.plugin_id AND a.form_key = w.form_key WHERE w.form_key = $k";
        cmd.Parameters.AddWithValue("$k", formKey);
        using var r = cmd.ExecuteReader();
        return r.Read() ? ReadArmor(r) : null;
    }

    private ArmorSummary ReadArmor(SqliteDataReader r)
    {
        var mask = (uint)r.GetInt64(5);
        var formKey = r.GetString(0);
        return new ArmorSummary
        {
            FormKey = formKey,
            FormId = session.FormatFormId(FormKey.Factory(formKey)),
            EditorId = Str(r, 1),
            Name = Str(r, 2),
            OriginPlugin = r.GetString(3),
            WinningPlugin = r.GetString(4),
            SlotMask = mask,
            Slots = BipedSlots.ToSlots(mask),
            ArmorType = r.GetString(6),
            NonPlayable = r.GetInt32(7) != 0,
            MaleModel = Str(r, 8),
            FemaleModel = Str(r, 9),
            AddonCount = (JsonSerializer.Deserialize<string[]>(r.GetString(10)) ?? []).Length,
        };
    }

    // ---------------------------------------------------------------- records

    public RecordRef Ref(string formKey)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = """
            SELECT rw.record_type, r.editor_id, r.name, rw.winning_plugin FROM record_winner rw
            JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key WHERE rw.form_key = $k
            """;
        cmd.Parameters.AddWithValue("$k", formKey);
        using var r = cmd.ExecuteReader();
        if (!r.Read())
        {
            return new RecordRef { FormKey = formKey, RecordType = "????", Resolved = false };
        }

        return new RecordRef
        {
            FormKey = formKey,
            RecordType = r.GetString(0),
            EditorId = Str(r, 1),
            Name = Str(r, 2),
            WinningPlugin = r.GetString(3),
        };
    }

    public string WinningPluginOf(FormKey formKey)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = "SELECT winning_plugin FROM npc_winner WHERE form_key = $k";
        cmd.Parameters.AddWithValue("$k", formKey.ToString());
        return cmd.ExecuteScalar() as string ?? formKey.ModKey.FileName.String;
    }

    public SearchHeadPartsResponse SearchHeadParts(SearchHeadPartsRequest request)
    {
        var where = new StringBuilder(" WHERE rw.record_type = 'HDPT'");
        var parameters = new List<(string, object)>();
        if (!string.IsNullOrWhiteSpace(request.Text))
        {
            var text = request.Text.Trim();
            where.Append(" AND (r.editor_id LIKE $text ESCAPE '\\' OR r.name LIKE $text ESCAPE '\\')");
            parameters.Add(("$text", "%" + EscapeLike(text.ToLowerInvariant()) + "%"));
        }
        if (!string.IsNullOrWhiteSpace(request.Plugin))
        {
            where.Append(" AND p.file_name = $plugin COLLATE NOCASE");
            parameters.Add(("$plugin", request.Plugin.Trim()));
        }

        var countSql = "SELECT COUNT(*) FROM record_winner rw JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key JOIN plugin_files p ON p.id = rw.plugin_id" + where;
        int total;
        using (var cmd = _db.CreateCommand())
        {
            cmd.CommandText = countSql;
            foreach (var (k, v) in parameters) cmd.Parameters.AddWithValue(k, v);
            total = Convert.ToInt32(cmd.ExecuteScalar(), CultureInfo.InvariantCulture);
        }

        var sql = "SELECT rw.form_key, r.editor_id, r.name, r.data, p.file_name FROM record_winner rw JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key JOIN plugin_files p ON p.id = rw.plugin_id" + where + " ORDER BY r.editor_id LIMIT " + Math.Clamp(request.Limit, 1, MaxLimit);
        var items = new List<HeadPartSummary>();
        using (var cmd = _db.CreateCommand())
        {
            cmd.CommandText = sql;
            foreach (var (k, v) in parameters) cmd.Parameters.AddWithValue(k, v);
            using var reader = cmd.ExecuteReader();
            while (reader.Read())
            {
                var formKey = reader.GetString(0);
                var editorId = reader.IsDBNull(1) ? null : reader.GetString(1);
                var name = reader.IsDBNull(2) ? null : reader.GetString(2);
                var dataJson = reader.IsDBNull(3) ? null : reader.GetString(3);
                var plugin = reader.GetString(4);

                string type = "Unknown";
                string? model = null;
                if (!string.IsNullOrEmpty(dataJson))
                {
                    try
                    {
                        using var doc = JsonDocument.Parse(dataJson);
                        if (doc.RootElement.TryGetProperty("type", out var tProp)) type = tProp.GetString() ?? type;
                        if (doc.RootElement.TryGetProperty("model", out var mProp)) model = mProp.GetString();
                    }
                    catch { }
                }

                if (!string.IsNullOrWhiteSpace(request.Type) && !string.Equals(type, request.Type, StringComparison.OrdinalIgnoreCase))
                    continue;

                items.Add(new HeadPartSummary
                {
                    FormKey = formKey,
                    EditorId = editorId,
                    Name = name,
                    Type = type,
                    ModelPath = model,
                    Plugin = plugin,
                });
            }
        }

        return new SearchHeadPartsResponse { Items = items, Total = total };
    }

    private RecordRef? RefOrNull(string? formKey) => formKey is null ? null : Ref(formKey);

    private string? RecordData(string formKey)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = "SELECT r.data FROM record_winner rw JOIN records r ON r.plugin_id = rw.plugin_id AND r.form_key = rw.form_key WHERE rw.form_key = $k";
        cmd.Parameters.AddWithValue("$k", formKey);
        return cmd.ExecuteScalar() as string;
    }

    /// <summary>Accepts "0A2C8E:Skyrim.esm", "000A2C8E:Skyrim.esm" or a load-order FormID "000A2C8E".</summary>
    public bool TryParseAnyFormKey(string text, out FormKey formKey)
    {
        text = text.Trim();
        var colon = text.IndexOf(':', StringComparison.Ordinal);
        if (colon > 0
            && uint.TryParse(text.AsSpan(0, colon), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var id)
            && ModKey.TryFromFileName(text[(colon + 1)..], out var modKey))
        {
            formKey = new FormKey(modKey, id & 0xFFFFFF);
            return true;
        }

        return session.TryParseFormId(text, out formKey);
    }

    private string NormalizeKey(string text) =>
        TryParseAnyFormKey(text, out var fk) && text.Contains(':', StringComparison.Ordinal) ? fk.ToString() : text.Trim();

    private static string EscapeLike(string text) =>
        text.Replace("\\", "\\\\", StringComparison.Ordinal).Replace("%", "\\%", StringComparison.Ordinal).Replace("_", "\\_", StringComparison.Ordinal);

    private static string? Str(SqliteDataReader r, int i) => r.IsDBNull(i) ? null : r.GetString(i);

    private static IReadOnlyList<string> SetFlagNames<TEnum>(TEnum value) where TEnum : struct, Enum =>
        Enum.GetValues<TEnum>().Where(f => Convert.ToUInt64(f, CultureInfo.InvariantCulture) != 0 && value.HasFlag(f)).Select(f => f.ToString()).ToArray();
}
