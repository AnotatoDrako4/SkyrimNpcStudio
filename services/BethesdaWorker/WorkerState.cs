using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker;

/// <summary>The indexed catalog of one profile: open plugins, SQLite cache and queries over them.</summary>
internal sealed class Catalog(string profileKey, LoadOrderSession session, CatalogStore store) : IDisposable
{
    public string ProfileKey { get; } = profileKey;
    public LoadOrderSession Session { get; } = session;
    public CatalogStore Store { get; } = store;
    public CatalogQueries Queries { get; } = new(store, session);

    /// <summary>Plugins of the profile that are present but disabled (the session only holds the enabled ones).</summary>
    public IReadOnlyList<string> DisabledPlugins { get; init; } = [];

    private SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine.PluginContext? _behavior;

    /// <summary>
    /// The behavior engines' view of this catalog's session (see Behavior.BehaviorRpc). Built on first use and kept for the life of the
    /// catalog: a re-index makes a new Catalog, so a context never outlives its profile or serves another one. It does not own the
    /// overlays (the session does), and is used under the WorkerState gate like the rest of the catalog.
    /// </summary>
    public SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine.PluginContext BehaviorContext =>
        _behavior ??= Behavior.BehaviorRpc.BuildContext(this);

    public void Dispose()
    {
        _behavior = null;
        Store.Dispose();
        Session.Dispose();
    }
}

/// <summary>
/// Holds the active catalog. SQLite connections and Mutagen overlays are not thread-safe, so the catalog
/// has one user at a time. Short reads use it under the gate. Long operations (indexing, exports) claim it
/// under the gate and run outside it: every other request answers at once with Busy instead of queuing
/// behind them (audit v2 F-14). A re-index keeps the previous catalog until the new one is complete, so a
/// failed or cancelled re-index leaves the studio with the catalog it had (audit v2 F-18).
/// </summary>
internal sealed class WorkerState : IDisposable
{
    private const string Indexing = "indexing";
    private const string Exporting = "export";

    private readonly Lock _gate = new();
    private Catalog? _catalog;

    // The long operation that owns the catalog outside the gate (Indexing, Exporting), or null.
    private string? _claimedBy;
    private bool _disposed;

    public CatalogStatusResponse Status()
    {
        lock (_gate)
        {
            return new CatalogStatusResponse
            {
                Indexed = _catalog is not null,
                Busy = _claimedBy is not null,
                ProfileKey = _catalog?.ProfileKey,
                PluginCount = _catalog?.Session.Plugins.Count ?? 0,
            };
        }
    }

    public T Read<T>(Func<Catalog, T> query)
    {
        lock (_gate)
        {
            return query(Available());
        }
    }

    /// <summary>Test seam: runs on the export's thread once the export owns the catalog.</summary>
    internal Action? ExportStarted { get; set; }

    /// <summary>
    /// Runs an export with the catalog to itself, outside the gate: searches, status and a second export
    /// answer Busy meanwhile instead of waiting for it to write, verify and zip the mod.
    /// </summary>
    public T Export<T>(Func<Catalog, T> export)
    {
        Catalog catalog;
        lock (_gate)
        {
            catalog = Available();
            _claimedBy = Exporting;
        }

        try
        {
            ExportStarted?.Invoke();
            return export(catalog);
        }
        finally
        {
            lock (_gate)
            {
                _claimedBy = null;
                // Dispose() came while the export used the catalog: it left the disposing to the export.
                if (_disposed)
                {
                    _catalog = null;
                    catalog.Dispose();
                }
            }
        }
    }

    public IndexResponse Index(IndexRequest request, Action<IndexProgress> progress, CancellationToken ct)
    {
        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            ThrowIfClaimed(Indexing, "An indexing operation is already running.");
            // Replacing the catalog would dispose it under the running export.
            ThrowIfClaimed(Exporting, "An export is using the catalog; index again when it finishes.");
            _claimedBy = Indexing;
        }

        Catalog? replaced = null;
        try
        {
            // The previous catalog stays in place (reads answer Busy meanwhile) until this one is complete.
            var result = CatalogIndexer.Run(request, progress, ct);
            var fresh = new Catalog(request.ProfileKey, result.Session, result.Store)
            {
                DisabledPlugins = request.Plugins.Where(p => !p.Enabled).Select(p => p.FileName).ToArray(),
            };
            lock (_gate)
            {
                if (_disposed)
                {
                    fresh.Dispose();
                    throw new ObjectDisposedException(nameof(WorkerState));
                }

                replaced = _catalog;
                _catalog = fresh;
            }

            return result.Response;
        }
        finally
        {
            lock (_gate)
            {
                _claimedBy = null;
                if (_disposed)
                {
                    replaced ??= _catalog;
                    _catalog = null;
                }
            }

            replaced?.Dispose();
        }
    }

    /// <summary>The catalog, for the caller holding the gate; Busy while a long operation owns it.</summary>
    private Catalog Available()
    {
        ThrowIfClaimed(Indexing, "The catalog is being indexed; retry when indexing finishes.");
        ThrowIfClaimed(Exporting, "An export is using the catalog; retry when it finishes.");
        if (_catalog is null)
        {
            throw new WorkerException(ErrorCodes.NotIndexed, "No load order has been indexed yet (call catalog.index).");
        }

        // The winner tables live in a cache shared with other studio processes on the same profile.
        _catalog.Store.EnsureActiveLoadOrder();
        return _catalog;
    }

    private void ThrowIfClaimed(string owner, string message)
    {
        if (_claimedBy == owner)
        {
            throw new WorkerException(ErrorCodes.Busy, message);
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _disposed = true;
            // A running index or export disposes what it holds when it ends.
            if (_claimedBy is null)
            {
                _catalog?.Dispose();
                _catalog = null;
            }
        }
    }
}
