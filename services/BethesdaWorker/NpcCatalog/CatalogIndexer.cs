using System.Diagnostics;
using Mutagen.Bethesda.Plugins;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>Builds (incrementally) the catalog of a load order.</summary>
internal static class CatalogIndexer
{
    public sealed record Result(IndexResponse Response, LoadOrderSession Session, CatalogStore Store);

    public static Result Run(IndexRequest request, Action<IndexProgress> progress, CancellationToken ct)
    {
        ValidateRequest(request);
        var clock = Stopwatch.StartNew();
        var issues = new List<Issue>();

        progress(new IndexProgress { Stage = "open", Current = 0, Total = request.Plugins.Count });
        var language = LoadOrderSession.ParseLanguage(request.Language, issues);
        var session = LoadOrderSession.Open(request.Plugins, language, issues, ct);
        CatalogStore? store = null;
        try
        {
            var dbPath = Path.Combine(request.CacheDirectory, SanitizeKey(request.ProfileKey), "catalog.sqlite");
            store = CatalogStore.Open(dbPath);

            var rows = new List<LoadOrderRow>(session.Plugins.Count);
            int parsed = 0, cached = 0;
            foreach (var plugin in session.Plugins)
            {
                ct.ThrowIfCancellationRequested();
                progress(new IndexProgress
                {
                    Stage = "plugins", Current = plugin.LoadIndex + 1, Total = session.Plugins.Count, Plugin = plugin.ModKey.FileName,
                });

                // Names come from localized strings, so the language is part of the cache identity.
                var masterStyles = plugin.Masters.Select(m => $"{m.FileName}:{session.Find(m)?.Style.ToString() ?? "missing"}")
                    .Append($"lang:{language}");
                var signature = CatalogStore.ComputeSignature(plugin.Path, plugin.Size, plugin.LastWriteUtc, masterStyles);
                long pluginId;
                if (store.FindBySignature(signature) is { } hit)
                {
                    pluginId = hit.Id;
                    issues.AddRange(store.LoadIssues(hit.Id));
                    cached++;
                }
                else
                {
                    var extract = RecordExtractor.Extract(plugin.Mod, ct);
                    var pluginIssues = extract.Issues.Select(i => i with { Subject = i.Subject ?? plugin.ModKey.FileName }).ToList();
                    issues.AddRange(pluginIssues);
                    pluginId = store.InsertPlugin(plugin.ModKey.FileName, signature, extract with { Issues = pluginIssues });
                    parsed++;
                }

                rows.Add(new LoadOrderRow(
                    plugin.LoadIndex, pluginId, plugin.ModKey.FileName, plugin.IsLight, plugin.IsMasterFlagged,
                    plugin.FormIdPrefix, plugin.Path, plugin.Masters.Select(m => m.FileName.String).ToArray()));
            }

            progress(new IndexProgress { Stage = "winners", Current = 0, Total = 1 });
            store.ActivateLoadOrder(rows);
            PurgeCache(store, issues);

            var counts = CatalogQueries.Counts(store);
            var response = new IndexResponse
            {
                PluginCount = session.Plugins.Count,
                ParsedPlugins = parsed,
                CachedPlugins = cached,
                NpcCount = counts.Npcs,
                ArmorCount = counts.Armors,
                OutfitCount = counts.Outfits,
                ElapsedMs = clock.ElapsedMilliseconds,
                Issues = issues,
            };
            progress(new IndexProgress { Stage = "done", Current = 1, Total = 1 });
            return new Result(response, session, store);
        }
        catch
        {
            store?.Dispose();
            session.Dispose();
            throw;
        }
    }

    /// <summary>
    /// Housekeeping after the load order is active (audit v2 F-17): a purge that fails (e.g. another studio
    /// process holds the cache) only leaves old rows for the next index, so it never fails this one.
    /// </summary>
    private static void PurgeCache(CatalogStore store, List<Issue> issues)
    {
        try
        {
            var purged = store.PurgeUnusedPlugins();
            if (purged > 0)
            {
                Rpc.Log.Info($"index cache: purged {purged} plugin version(s) unused for {CatalogStore.UnusedPluginRetention.TotalDays:0} days");
            }
        }
        catch (Microsoft.Data.Sqlite.SqliteException ex)
        {
            issues.Add(Issue.Warning("CachePurgeFailed", $"Old plugin versions could not be removed from the index cache: {ex.Message}"));
        }
    }

    private static void ValidateRequest(IndexRequest request)
    {
        if (string.IsNullOrWhiteSpace(request.ProfileKey))
        {
            throw new ArgumentException("profileKey is required.");
        }

        if (string.IsNullOrWhiteSpace(request.CacheDirectory) || !Path.IsPathFullyQualified(request.CacheDirectory))
        {
            throw new ArgumentException("cacheDirectory must be an absolute path.");
        }

        if (request.Plugins.Count == 0)
        {
            throw new ArgumentException("The load order is empty.");
        }

        if (!request.Plugins.Any(p => p.Enabled && ModKey.TryFromFileName(p.FileName, out var key) && key == "Skyrim.esm"))
        {
            throw new ArgumentException("Skyrim.esm must be part of the enabled load order.");
        }
    }

    /// <summary>Profile keys become directory names: keep them to a safe, bounded character set.</summary>
    internal static string SanitizeKey(string key)
    {
        var chars = key.Select(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_' ? c : '_').ToArray();
        var safe = new string(chars);
        return safe.Length > 64 ? safe[..64] : safe;
    }
}
