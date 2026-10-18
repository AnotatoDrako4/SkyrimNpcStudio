using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Data.Sqlite;
using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>A plugin row of the cache, identified by its content signature.</summary>
internal sealed record CachedPlugin(long Id, string FileName, string Signature);

/// <summary>One entry of the current (active) load order as stored in the cache.</summary>
internal sealed record LoadOrderRow(int LoadIndex, long PluginId, string FileName, bool IsLight, bool IsMaster, string FormIdPrefix, string Path, IReadOnlyList<string> Masters);

/// <summary>
/// SQLite cache of per-plugin extracted rows plus the materialized winners of the active load order.
/// Rows are keyed by plugin signature (path, size, mtime, master styles, extractor version), so
/// unchanged plugins are never parsed twice. Winners are recomputed in SQL on every index.
/// </summary>
internal sealed class CatalogStore : IDisposable
{
    // 4: FormKey columns COLLATE NOCASE (a plugin may spell a master with another letter case), winners keyed
    //    by the origin's spelling.
    public const int SchemaVersion = 4;

    private readonly SqliteConnection _db;

    // Digest of the load order this store activated (meta 'active_load_order'); null until then.
    private string? _activeLoadOrder;

    private CatalogStore(SqliteConnection db) => _db = db;

    public SqliteConnection Connection => _db;

    public static CatalogStore Open(string databasePath)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(databasePath)!);
        var db = new SqliteConnection(new SqliteConnectionStringBuilder
        {
            DataSource = databasePath,
            Mode = SqliteOpenMode.ReadWriteCreate,
            Pooling = false,
        }.ToString());
        db.Open();
        Exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY;");

        var current = Scalar<long?>(db, "SELECT CAST(value AS INTEGER) FROM meta WHERE key = 'schema'", ignoreErrors: true);
        if (current != SchemaVersion)
        {
            Exec(db, """
                DROP TABLE IF EXISTS npc_winner; DROP TABLE IF EXISTS armor_winner; DROP TABLE IF EXISTS record_winner;
                DROP TABLE IF EXISTS load_order; DROP TABLE IF EXISTS npc_factions; DROP TABLE IF EXISTS npc_records;
                DROP TABLE IF EXISTS npc_locations;
                DROP TABLE IF EXISTS armor_records; DROP TABLE IF EXISTS records; DROP TABLE IF EXISTS plugin_files;
                DROP TABLE IF EXISTS meta;
                """);
            CreateSchema(db);
        }

        return new CatalogStore(db);
    }

    private static void CreateSchema(SqliteConnection db)
    {
        Exec(db, $"""
            CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
            INSERT INTO meta(key, value) VALUES ('schema', '{SchemaVersion}');

            CREATE TABLE plugin_files(
                id INTEGER PRIMARY KEY,
                file_name TEXT NOT NULL,
                signature TEXT NOT NULL UNIQUE,
                npc_count INTEGER NOT NULL,
                armor_count INTEGER NOT NULL,
                outfit_count INTEGER NOT NULL,
                issues TEXT);

            CREATE TABLE records(
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id) ON DELETE CASCADE,
                form_key TEXT NOT NULL COLLATE NOCASE,
                record_type TEXT NOT NULL,
                editor_id TEXT,
                name TEXT,
                data TEXT,
                PRIMARY KEY(plugin_id, form_key)) WITHOUT ROWID;
            CREATE INDEX records_form_key ON records(form_key);

            CREATE TABLE npc_records(
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id) ON DELETE CASCADE,
                form_key TEXT NOT NULL COLLATE NOCASE,
                editor_id TEXT, name TEXT, search_text TEXT NOT NULL,
                race TEXT COLLATE NOCASE, female INTEGER NOT NULL, is_unique INTEGER NOT NULL, is_preset INTEGER NOT NULL,
                template TEXT COLLATE NOCASE, template_flags INTEGER NOT NULL,
                default_outfit TEXT COLLATE NOCASE, sleeping_outfit TEXT COLLATE NOCASE,
                data TEXT NOT NULL, fingerprints TEXT NOT NULL,
                PRIMARY KEY(plugin_id, form_key)) WITHOUT ROWID;
            CREATE INDEX npc_records_form_key ON npc_records(form_key);

            CREATE TABLE npc_factions(
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id) ON DELETE CASCADE,
                form_key TEXT NOT NULL COLLATE NOCASE, faction TEXT NOT NULL COLLATE NOCASE, rank INTEGER NOT NULL);
            CREATE INDEX npc_factions_key ON npc_factions(plugin_id, form_key);
            CREATE INDEX npc_factions_faction ON npc_factions(faction);

            CREATE TABLE npc_locations(
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id) ON DELETE CASCADE,
                form_key TEXT NOT NULL COLLATE NOCASE,
                npc_form_key TEXT NOT NULL COLLATE NOCASE,
                editor_id TEXT,
                cell_form_key TEXT COLLATE NOCASE,
                cell_name TEXT,
                cell_editor_id TEXT,
                PRIMARY KEY(plugin_id, form_key)) WITHOUT ROWID;
            CREATE INDEX npc_locations_npc ON npc_locations(npc_form_key);

            CREATE TABLE armor_records(
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id) ON DELETE CASCADE,
                form_key TEXT NOT NULL COLLATE NOCASE,
                editor_id TEXT, name TEXT, search_text TEXT NOT NULL,
                slot_mask INTEGER NOT NULL, armor_type TEXT NOT NULL, non_playable INTEGER NOT NULL,
                male_model TEXT, female_model TEXT, addons TEXT NOT NULL,
                PRIMARY KEY(plugin_id, form_key)) WITHOUT ROWID;
            CREATE INDEX armor_records_form_key ON armor_records(form_key);

            CREATE TABLE load_order(
                load_index INTEGER PRIMARY KEY,
                plugin_id INTEGER NOT NULL REFERENCES plugin_files(id),
                file_name TEXT NOT NULL,
                is_light INTEGER NOT NULL,
                is_master INTEGER NOT NULL,
                form_id_prefix TEXT NOT NULL,
                path TEXT NOT NULL,
                masters TEXT NOT NULL);

            CREATE TABLE record_winner(
                form_key TEXT PRIMARY KEY COLLATE NOCASE, record_type TEXT NOT NULL, plugin_id INTEGER NOT NULL,
                load_index INTEGER NOT NULL, winning_plugin TEXT NOT NULL, override_count INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE npc_winner(
                form_key TEXT PRIMARY KEY COLLATE NOCASE, plugin_id INTEGER NOT NULL, load_index INTEGER NOT NULL,
                winning_plugin TEXT NOT NULL, origin_plugin TEXT NOT NULL, override_count INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE armor_winner(
                form_key TEXT PRIMARY KEY COLLATE NOCASE, plugin_id INTEGER NOT NULL, load_index INTEGER NOT NULL,
                winning_plugin TEXT NOT NULL, origin_plugin TEXT NOT NULL, override_count INTEGER NOT NULL) WITHOUT ROWID;
            """);
    }

    public static string ComputeSignature(string path, long size, DateTime lastWriteUtc, IEnumerable<string> masterStyles)
    {
        var text = string.Join('|',
            path.ToUpperInvariant(),
            size.ToString(CultureInfo.InvariantCulture),
            lastWriteUtc.Ticks.ToString(CultureInfo.InvariantCulture),
            string.Join(',', masterStyles),
            RecordExtractor.Version.ToString(CultureInfo.InvariantCulture));
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }

    public CachedPlugin? FindBySignature(string signature)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = "SELECT id, file_name, signature FROM plugin_files WHERE signature = $s";
        cmd.Parameters.AddWithValue("$s", signature);
        using var r = cmd.ExecuteReader();
        return r.Read() ? new CachedPlugin(r.GetInt64(0), r.GetString(1), r.GetString(2)) : null;
    }

    /// <summary>Issues recorded when the plugin was parsed, so cached plugins keep reporting them.</summary>
    public IReadOnlyList<Contracts.Issue> LoadIssues(long pluginId)
    {
        using var cmd = _db.CreateCommand();
        cmd.CommandText = "SELECT issues FROM plugin_files WHERE id = $id";
        cmd.Parameters.AddWithValue("$id", pluginId);
        return cmd.ExecuteScalar() is string json
            ? JsonSerializer.Deserialize<List<Contracts.Issue>>(json, RecordExtractor.Json) ?? []
            : [];
    }

    /// <summary>Stores the rows extracted from one plugin in a single transaction.</summary>
    public long InsertPlugin(string fileName, string signature, PluginExtract extract)
    {
        using var tx = _db.BeginTransaction();
        long id;
        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            // Every studio build shares this cache: another process may have committed the same signature
            // since FindBySignature missed. Same signature, same content, written in one transaction, so its
            // row is complete: reuse it instead of failing the UNIQUE constraint (which aborted indexing).
            cmd.CommandText = """
                INSERT INTO plugin_files(file_name, signature, npc_count, armor_count, outfit_count, issues)
                VALUES ($f, $s, $n, $a, $o, $i) ON CONFLICT(signature) DO NOTHING RETURNING id
                """;
            cmd.Parameters.AddWithValue("$f", fileName);
            cmd.Parameters.AddWithValue("$s", signature);
            cmd.Parameters.AddWithValue("$n", extract.Npcs.Count);
            cmd.Parameters.AddWithValue("$a", extract.Armors.Count);
            cmd.Parameters.AddWithValue("$o", extract.OutfitCount);
            cmd.Parameters.AddWithValue("$i", JsonSerializer.Serialize(extract.Issues, RecordExtractor.Json));
            if (cmd.ExecuteScalar() is not long inserted)
            {
                tx.Rollback();
                return FindBySignature(signature)?.Id
                    ?? throw new InvalidOperationException($"Plugin signature {signature} conflicted but its row cannot be read.");
            }

            id = inserted;
        }

        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = "INSERT OR REPLACE INTO records VALUES ($p, $k, $t, $e, $n, $d)";
            var p = cmd.Parameters.Add("$p", SqliteType.Integer);
            var k = cmd.Parameters.Add("$k", SqliteType.Text);
            var t = cmd.Parameters.Add("$t", SqliteType.Text);
            var e = cmd.Parameters.Add("$e", SqliteType.Text);
            var n = cmd.Parameters.Add("$n", SqliteType.Text);
            var d = cmd.Parameters.Add("$d", SqliteType.Text);
            cmd.Prepare();
            foreach (var row in extract.Records)
            {
                p.Value = id;
                k.Value = row.FormKey;
                t.Value = row.RecordType;
                e.Value = (object?)row.EditorId ?? DBNull.Value;
                n.Value = (object?)row.Name ?? DBNull.Value;
                d.Value = (object?)row.DataJson ?? DBNull.Value;
                cmd.ExecuteNonQuery();
            }
        }

        using (var cmd = _db.CreateCommand())
        using (var fcmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = """
                INSERT OR REPLACE INTO npc_records VALUES
                ($p, $k, $e, $n, $st, $race, $fem, $uni, $pre, $tpl, $tf, $do, $so, $data, $fp)
                """;
            fcmd.Transaction = tx;
            fcmd.CommandText = "INSERT INTO npc_factions VALUES ($p, $k, $f, $r)";
            foreach (var npc in extract.Npcs)
            {
                cmd.Parameters.Clear();
                cmd.Parameters.AddWithValue("$p", id);
                cmd.Parameters.AddWithValue("$k", npc.FormKey);
                cmd.Parameters.AddWithValue("$e", (object?)npc.EditorId ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$n", (object?)npc.Name ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$st", SearchText(npc.EditorId, npc.Name));
                cmd.Parameters.AddWithValue("$race", (object?)npc.Race ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$fem", npc.Female ? 1 : 0);
                cmd.Parameters.AddWithValue("$uni", npc.Unique ? 1 : 0);
                cmd.Parameters.AddWithValue("$pre", npc.Preset ? 1 : 0);
                cmd.Parameters.AddWithValue("$tpl", (object?)npc.Template ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$tf", npc.TemplateFlags);
                cmd.Parameters.AddWithValue("$do", (object?)npc.DefaultOutfit ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$so", (object?)npc.SleepingOutfit ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$data", JsonSerializer.Serialize(npc.Data, RecordExtractor.Json));
                cmd.Parameters.AddWithValue("$fp", JsonSerializer.Serialize(npc.Fingerprints, RecordExtractor.Json));
                cmd.ExecuteNonQuery();

                foreach (var (faction, rank) in npc.Factions)
                {
                    fcmd.Parameters.Clear();
                    fcmd.Parameters.AddWithValue("$p", id);
                    fcmd.Parameters.AddWithValue("$k", npc.FormKey);
                    fcmd.Parameters.AddWithValue("$f", faction);
                    fcmd.Parameters.AddWithValue("$r", rank);
                    fcmd.ExecuteNonQuery();
                }
            }
        }

        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = "INSERT OR REPLACE INTO armor_records VALUES ($p, $k, $e, $n, $st, $sm, $at, $np, $mm, $fm, $ad)";
            foreach (var armor in extract.Armors)
            {
                cmd.Parameters.Clear();
                cmd.Parameters.AddWithValue("$p", id);
                cmd.Parameters.AddWithValue("$k", armor.FormKey);
                cmd.Parameters.AddWithValue("$e", (object?)armor.EditorId ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$n", (object?)armor.Name ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$st", SearchText(armor.EditorId, armor.Name));
                cmd.Parameters.AddWithValue("$sm", (long)armor.SlotMask);
                cmd.Parameters.AddWithValue("$at", armor.ArmorType);
                cmd.Parameters.AddWithValue("$np", armor.NonPlayable ? 1 : 0);
                cmd.Parameters.AddWithValue("$mm", (object?)armor.MaleModel ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$fm", (object?)armor.FemaleModel ?? DBNull.Value);
                cmd.Parameters.AddWithValue("$ad", JsonSerializer.Serialize(armor.Addons, RecordExtractor.Json));
                cmd.ExecuteNonQuery();
            }
        }

        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = "INSERT OR REPLACE INTO npc_locations VALUES ($p, $k, $n, $e, $cf, $cn, $ce)";
            var p = cmd.Parameters.Add("$p", SqliteType.Integer);
            var k = cmd.Parameters.Add("$k", SqliteType.Text);
            var n = cmd.Parameters.Add("$n", SqliteType.Text);
            var e = cmd.Parameters.Add("$e", SqliteType.Text);
            var cf = cmd.Parameters.Add("$cf", SqliteType.Text);
            var cn = cmd.Parameters.Add("$cn", SqliteType.Text);
            var ce = cmd.Parameters.Add("$ce", SqliteType.Text);
            cmd.Prepare();
            foreach (var loc in extract.Locations)
            {
                p.Value = id;
                k.Value = loc.FormKey;
                n.Value = loc.NpcFormKey;
                e.Value = (object?)loc.EditorId ?? DBNull.Value;
                cf.Value = (object?)loc.CellFormKey ?? DBNull.Value;
                cn.Value = (object?)loc.CellName ?? DBNull.Value;
                ce.Value = (object?)loc.CellEditorId ?? DBNull.Value;
                cmd.ExecuteNonQuery();
            }
        }

        tx.Commit();
        return id;
    }

    /// <summary>
    /// Replaces the active load order and recomputes the winner tables (highest load index wins).
    /// Cached plugin rows are retained because another indexer may already have selected them.
    /// </summary>
    public void ActivateLoadOrder(IReadOnlyList<LoadOrderRow> rows)
    {
        using var tx = _db.BeginTransaction();
        Exec(_db, "DELETE FROM load_order; DELETE FROM record_winner; DELETE FROM npc_winner; DELETE FROM armor_winner;", tx);

        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = "INSERT INTO load_order VALUES ($i, $p, $f, $l, $m, $x, $path, $masters)";
            foreach (var row in rows)
            {
                cmd.Parameters.Clear();
                cmd.Parameters.AddWithValue("$i", row.LoadIndex);
                cmd.Parameters.AddWithValue("$p", row.PluginId);
                cmd.Parameters.AddWithValue("$f", row.FileName);
                cmd.Parameters.AddWithValue("$l", row.IsLight ? 1 : 0);
                cmd.Parameters.AddWithValue("$m", row.IsMaster ? 1 : 0);
                cmd.Parameters.AddWithValue("$x", row.FormIdPrefix);
                cmd.Parameters.AddWithValue("$path", row.Path);
                cmd.Parameters.AddWithValue("$masters", JsonSerializer.Serialize(row.Masters, RecordExtractor.Json));
                cmd.ExecuteNonQuery();
            }
        }

        // One winner per record whatever letter case each plugin spells its master with (the form_key columns
        // are NOCASE; the partitions say so too): the winner is the highest load index, the key keeps the
        // origin's spelling (lowest load index) and origin plugin = the ModKey part of that key "ID:Plugin.esp".
        Exec(_db, """
            INSERT INTO record_winner(form_key, record_type, plugin_id, load_index, winning_plugin, override_count)
            SELECT origin_key, record_type, plugin_id, load_index, file_name, cnt FROM (
                SELECT r.record_type, r.plugin_id, lo.load_index, lo.file_name,
                       FIRST_VALUE(r.form_key) OVER (PARTITION BY r.form_key COLLATE NOCASE ORDER BY lo.load_index) AS origin_key,
                       ROW_NUMBER() OVER (PARTITION BY r.form_key COLLATE NOCASE ORDER BY lo.load_index DESC) AS rn,
                       COUNT(*) OVER (PARTITION BY r.form_key COLLATE NOCASE) AS cnt
                FROM records r JOIN load_order lo ON lo.plugin_id = r.plugin_id)
            WHERE rn = 1;

            INSERT INTO npc_winner(form_key, plugin_id, load_index, winning_plugin, origin_plugin, override_count)
            SELECT origin_key, plugin_id, load_index, file_name, substr(origin_key, instr(origin_key, ':') + 1), cnt FROM (
                SELECT n.plugin_id, lo.load_index, lo.file_name,
                       FIRST_VALUE(n.form_key) OVER (PARTITION BY n.form_key COLLATE NOCASE ORDER BY lo.load_index) AS origin_key,
                       ROW_NUMBER() OVER (PARTITION BY n.form_key COLLATE NOCASE ORDER BY lo.load_index DESC) AS rn,
                       COUNT(*) OVER (PARTITION BY n.form_key COLLATE NOCASE) AS cnt
                FROM npc_records n JOIN load_order lo ON lo.plugin_id = n.plugin_id)
            WHERE rn = 1;

            INSERT INTO armor_winner(form_key, plugin_id, load_index, winning_plugin, origin_plugin, override_count)
            SELECT origin_key, plugin_id, load_index, file_name, substr(origin_key, instr(origin_key, ':') + 1), cnt FROM (
                SELECT a.plugin_id, lo.load_index, lo.file_name,
                       FIRST_VALUE(a.form_key) OVER (PARTITION BY a.form_key COLLATE NOCASE ORDER BY lo.load_index) AS origin_key,
                       ROW_NUMBER() OVER (PARTITION BY a.form_key COLLATE NOCASE ORDER BY lo.load_index DESC) AS rn,
                       COUNT(*) OVER (PARTITION BY a.form_key COLLATE NOCASE) AS cnt
                FROM armor_records a JOIN load_order lo ON lo.plugin_id = a.plugin_id)
            WHERE rn = 1;
            """, tx);

        var digest = LoadOrderDigest(rows);
        using (var cmd = _db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = "INSERT OR REPLACE INTO meta(key, value) VALUES ('active_load_order', $d)";
            cmd.Parameters.AddWithValue("$d", digest);
            cmd.ExecuteNonQuery();
        }

        tx.Commit();
        _activeLoadOrder = digest;
        Exec(_db, "PRAGMA optimize;");
    }

    /// <summary>
    /// Fails when another process activated a different load order in this cache after this store did:
    /// the winner tables would then answer with that load order while this worker's plugin session still
    /// reflects its own. A store that activated the same load order shares the digest, so it is not a
    /// conflict. Before ActivateLoadOrder there is nothing of this store's to protect.
    /// </summary>
    public void EnsureActiveLoadOrder()
    {
        if (_activeLoadOrder is null)
        {
            return;
        }

        var current = Scalar<string>(_db, "SELECT value FROM meta WHERE key = 'active_load_order'");
        if (!string.Equals(current, _activeLoadOrder, StringComparison.Ordinal))
        {
            throw new WorkerException(ErrorCodes.IndexReplaced,
                "Another Skyrim NPC Studio process indexed a different load order of this profile in the shared cache; re-index the profile (catalog.index) before reading.");
        }
    }

    /// <summary>Identity of a load order: index, cached plugin row, file name and flags of every entry.</summary>
    internal static string LoadOrderDigest(IReadOnlyList<LoadOrderRow> rows)
    {
        var text = string.Join('\n', rows.OrderBy(r => r.LoadIndex).Select(r => string.Join('|',
            r.LoadIndex.ToString(CultureInfo.InvariantCulture), r.PluginId.ToString(CultureInfo.InvariantCulture),
            r.FileName.ToUpperInvariant(), r.IsLight ? "L" : "-", r.IsMaster ? "M" : "-")));
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }

    public static string SearchText(string? editorId, string? name) =>
        $"{editorId}\u001f{name}".ToLowerInvariant();

    internal static void Exec(SqliteConnection db, string sql, SqliteTransaction? tx = null)
    {
        using var cmd = db.CreateCommand();
        cmd.Transaction = tx;
        cmd.CommandText = sql;
        cmd.ExecuteNonQuery();
    }

    private static T? Scalar<T>(SqliteConnection db, string sql, bool ignoreErrors = false)
    {
        try
        {
            using var cmd = db.CreateCommand();
            cmd.CommandText = sql;
            var value = cmd.ExecuteScalar();
            return value is null or DBNull ? default : (T)Convert.ChangeType(value, Nullable.GetUnderlyingType(typeof(T)) ?? typeof(T), CultureInfo.InvariantCulture);
        }
        catch (SqliteException) when (ignoreErrors)
        {
            return default;
        }
    }

    public void Dispose() => _db.Dispose();
}
