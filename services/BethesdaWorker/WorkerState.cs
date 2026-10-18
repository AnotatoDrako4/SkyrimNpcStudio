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

    public void Dispose()
    {
        Store.Dispose();
        Session.Dispose();
    }
}

/// <summary>
/// Holds the active catalog. SQLite connections and Mutagen overlays are not thread-safe, so all
/// access is serialized; indexing replaces the catalog and makes reads fail fast with Busy.
/// </summary>
internal sealed class WorkerState : IDisposable
{
    private readonly Lock _gate = new();
    private Catalog? _catalog;
    private bool _indexing;

    public CatalogStatusResponse Status()
    {
        lock (_gate)
        {
            return new CatalogStatusResponse
            {
                Indexed = _catalog is not null,
                Busy = _indexing,
                ProfileKey = _catalog?.ProfileKey,
                PluginCount = _catalog?.Session.Plugins.Count ?? 0,
            };
        }
    }

    public T Read<T>(Func<Catalog, T> query)
    {
        lock (_gate)
        {
            if (_indexing)
            {
                throw new WorkerException(ErrorCodes.Busy, "The catalog is being indexed; retry when indexing finishes.");
            }

            if (_catalog is null)
            {
                throw new WorkerException(ErrorCodes.NotIndexed, "No load order has been indexed yet (call catalog.index).");
            }

            // The winner tables live in a cache shared with other studio processes on the same profile.
            _catalog.Store.EnsureActiveLoadOrder();
            return query(_catalog);
        }
    }

    public IndexResponse Index(IndexRequest request, Action<IndexProgress> progress, CancellationToken ct)
    {
        lock (_gate)
        {
            if (_indexing)
            {
                throw new WorkerException(ErrorCodes.Busy, "An indexing operation is already running.");
            }

            _indexing = true;
            _catalog?.Dispose();
            _catalog = null;
        }

        try
        {
            var result = CatalogIndexer.Run(request, progress, ct);
            lock (_gate)
            {
                _catalog = new Catalog(request.ProfileKey, result.Session, result.Store);
            }

            return result.Response;
        }
        finally
        {
            lock (_gate)
            {
                _indexing = false;
            }
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _catalog?.Dispose();
            _catalog = null;
        }
    }
}
