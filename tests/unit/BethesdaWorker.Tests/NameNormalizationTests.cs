using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.NpcCatalog;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

public sealed class NameNormalizationTests
{
    [Fact]
    public void WhitespaceOnlyNames_AreIndexedAsNoName()
    {
        // Seen in a real load order: 35 NPCs of a popular mod are named " ".
        var mod = new SkyrimMod(ModKey.FromFileName("Blank.esp"), SkyrimRelease.SkyrimSE);
        var npc = mod.Npcs.AddNew("BlankNameGuard");
        npc.Name = " ";
        var named = mod.Npcs.AddNew("NamedGuard");
        named.Name = "  Guardia  ";

        var extract = RecordExtractor.Extract(mod, CancellationToken.None);

        Assert.Null(extract.Npcs.Single(n => n.EditorId == "BlankNameGuard").Name);
        Assert.Equal("Guardia", extract.Npcs.Single(n => n.EditorId == "NamedGuard").Name);
        Assert.DoesNotContain(extract.Issues, i => i.Code == "StringsUnavailable");
    }
}
