using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>Head parts offered for an NPC: valid for its race and sex, without extra parts.</summary>
public sealed class HeadPartQueriesTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out = Path.Combine(Path.GetTempPath(), "sns-headparts-" + Guid.NewGuid().ToString("N"));

    public HeadPartQueriesTests()
    {
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "headpart-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private SearchHeadPartsResponse For(string? type, string? text = null) =>
        _state.Read(c => HeadPartQueries.ForNpc(c.Session, c.Queries,
            new SearchHeadPartsRequest { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), Type = type, Text = text }));

    [Fact]
    public void Eyes_ForAFemaleNord_AreTheFemaleEyes()
    {
        var eyes = For("Eyes").Items.Select(i => i.FormKey).ToHashSet();
        Assert.Contains(SyntheticLoadOrder.EyesBrown.ToString(), eyes);
        Assert.Contains(SyntheticLoadOrder.EyesGreen.ToString(), eyes);
        Assert.All(For("Eyes").Items, i => Assert.Equal("Eyes", i.Type));
    }

    [Fact]
    public void ExtraParts_AreNotOffered()
    {
        Assert.DoesNotContain(For(null).Items, i => i.FormKey == SyntheticLoadOrder.HairLineNord.ToString());
    }

    [Fact]
    public void TextFiltersByEditorId()
    {
        var face = For("Face").Items;
        Assert.Contains(face, i => i.FormKey == SyntheticLoadOrder.FemaleHeadNord.ToString());
        Assert.Empty(For("Face", "NoSuchPartName").Items);
    }

    [Fact]
    public void AnUnknownNpc_IsReported()
    {
        var error = Assert.Throws<WorkerException>(() => _state.Read(c => HeadPartQueries.ForNpc(c.Session, c.Queries,
            new SearchHeadPartsRequest { NpcFormKey = "ABCDEF:Nope.esp" })));
        Assert.Equal(ErrorCodes.RecordNotFound, error.Code);
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
