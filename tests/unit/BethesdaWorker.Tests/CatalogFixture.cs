using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>A synthetic load order indexed into a fresh worker state, shared by a test class.</summary>
public sealed class CatalogFixture : IDisposable
{
    public CatalogFixture()
    {
        LoadOrder = SyntheticLoadOrder.Create();
        CacheDirectory = Path.Combine(LoadOrder.DataDirectory, "_cache");
        State = new WorkerState();
        IndexResponse = State.Index(Request(), _ => { }, CancellationToken.None);
    }

    public SyntheticLoadOrder LoadOrder { get; }

    public string CacheDirectory { get; }

    internal WorkerState State { get; }

    public IndexResponse IndexResponse { get; }

    public IndexRequest Request() => new()
    {
        ProfileKey = "test-profile",
        CacheDirectory = CacheDirectory,
        Plugins = LoadOrder.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
    };

    public void Dispose()
    {
        State.Dispose();
        LoadOrder.Dispose();
    }
}
