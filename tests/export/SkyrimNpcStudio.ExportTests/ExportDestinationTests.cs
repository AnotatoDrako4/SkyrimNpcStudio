using System.Diagnostics;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.Validation;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// Audit F-13: every export (Modes A/B/C and native NPC) validates its output folder with the same policy,
/// which refuses the game, an active mod, MO2's mods/overwrite and the MO2 instance, subfolders and links
/// included, and writes nothing when it refuses. Synthetic folders under %TEMP% only.
/// </summary>
public sealed class ExportDestinationTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "sns-destination-" + Guid.NewGuid().ToString("N"));
    private readonly List<IDisposable> _owned = [];
    private readonly List<string> _junctions = [];

    public ExportDestinationTests() => Directory.CreateDirectory(_root);

    /// <summary>A synthetic load order whose plugins live in <paramref name="pluginFolder"/>, indexed.</summary>
    private (SyntheticLoadOrder Lo, WorkerState State) Profile(string pluginFolder)
    {
        var lo = SyntheticLoadOrder.Create(pluginFolder);
        var state = new WorkerState();
        state.Index(new IndexRequest
        {
            ProfileKey = "destination-tests-" + Guid.NewGuid().ToString("N"),
            CacheDirectory = Path.Combine(_root, "_cache-" + Guid.NewGuid().ToString("N")),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
        _owned.Add(lo);
        _owned.Add(state);
        return (lo, state);
    }

    private static string Dir(params string[] parts)
    {
        var path = Path.Combine(parts);
        Directory.CreateDirectory(path);
        return path;
    }

    private static ExportOutfitPatchRequest Request(string output) => new()
    {
        PluginName = "SkyrimNpcStudio_Destination.esp",
        OutputDirectory = output,
        Assignments = [new OutfitAssignment { NpcFormKey = SyntheticLoadOrder.Lydia.ToString(), ArmorFormKeys = [SyntheticLoadOrder.SteelBoots.ToString()] }],
    };

    /// <summary>The export is refused as InvalidArgument and the folder is left exactly as it was.</summary>
    private static void AssertRefused(WorkerState state, string output)
    {
        var before = Directory.GetFileSystemEntries(output, "*", SearchOption.AllDirectories).Order().ToArray();
        var ex = Assert.Throws<WorkerException>(() =>
            state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, Request(output), CancellationToken.None)));
        Assert.Equal(ErrorCodes.InvalidArgument, ex.Code);
        Assert.Equal(before, Directory.GetFileSystemEntries(output, "*", SearchOption.AllDirectories).Order().ToArray());
    }

    private bool Junction(string link, string target)
    {
        using var p = Process.Start(new ProcessStartInfo("cmd.exe", $"/c mklink /J \"{link}\" \"{target}\"")
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
        })!;
        p.WaitForExit();
        if (p.ExitCode != 0 || !Directory.Exists(link))
        {
            return false;
        }

        _junctions.Add(link);
        return true;
    }

    [Fact]
    public void Export_RefusesTheDataFolderAndItsSubfolders()
    {
        var (lo, state) = Profile(Path.Combine(_root, "Data"));
        AssertRefused(state, lo.DataDirectory);
        AssertRefused(state, Dir(lo.DataDirectory, "Exports"));
        AssertRefused(state, Dir(lo.DataDirectory, "meshes", "deep"));
    }

    [Fact]
    public void Export_RefusesMo2ModsOverwriteAndTheInstance()
    {
        // A portable MO2 instance: <root>/MO2/ModOrganizer.ini, the profile's plugins in mods/Synthetic (with meta.ini).
        var instance = Dir(_root, "MO2");
        File.WriteAllText(Path.Combine(instance, "ModOrganizer.ini"), "[General]\n");
        var modFolder = Dir(instance, "mods", "Synthetic");
        File.WriteAllText(Path.Combine(modFolder, "meta.ini"), "[General]\n");
        var (_, state) = Profile(modFolder);

        AssertRefused(state, Dir(instance, "mods", "NewMod"));  // a sibling mod MO2 would pick up
        AssertRefused(state, Dir(instance, "overwrite", "Exports"));
        AssertRefused(state, Dir(instance, "profiles", "Default"));
        AssertRefused(state, instance);
    }

    [Fact]
    public void Export_RefusesAModsFolderOutsideTheInstanceByItsMetaIni()
    {
        // A global instance keeps its ModOrganizer.ini elsewhere; mods/ is recognised by the mod's meta.ini.
        var modFolder = Dir(_root, "Games", "mods", "Synthetic");
        File.WriteAllText(Path.Combine(modFolder, "meta.ini"), "[General]\n");
        var (_, state) = Profile(modFolder);
        AssertRefused(state, Dir(_root, "Games", "mods", "NewMod"));
        var exports = Dir(_root, "Games", "Exports");
        Assert.Equal(ExportDestinationPolicy.RealDirectory(exports), state.Read(c => ExportDestinationPolicy.Validate(c.Session, exports)));
    }

    [Fact]
    public void Export_RefusesTheGameFolder()
    {
        var game = Dir(_root, "Skyrim Special Edition");
        File.WriteAllText(Path.Combine(game, "SkyrimSE.exe"), "not a game");
        var (_, state) = Profile(Path.Combine(game, "Data"));
        AssertRefused(state, Dir(game, "Exports"));
        AssertRefused(state, game);
        // A folder next to the game (sharing its name as a prefix) is not inside it.
        var beside = Dir(_root, "Skyrim Special Edition Exports");
        Assert.Equal(ExportDestinationPolicy.RealDirectory(beside), state.Read(c => ExportDestinationPolicy.Validate(c.Session, beside)));
    }

    [Fact]
    public void Export_RefusesAJunctionThatLeadsIntoData()
    {
        var (lo, state) = Profile(Path.Combine(_root, "Data"));
        Dir(lo.DataDirectory, "Exports");
        var link = Path.Combine(_root, "LooksHarmless");
        Assert.True(Junction(link, lo.DataDirectory), "mklink /J failed");

        AssertRefused(state, link);
        AssertRefused(state, Path.Combine(link, "Exports"));
        Assert.True(File.Exists(lo.PathOf(SyntheticLoadOrder.Skyrim)));
    }

    [Fact]
    public void Export_WritesIntoTheResolvedFolderOfAnAllowedJunction()
    {
        var (_, state) = Profile(Path.Combine(_root, "Data"));
        var real = Dir(_root, "RealExports");
        var link = Path.Combine(_root, "ExportsLink");
        Assert.True(Junction(link, real), "mklink /J failed");

        var response = state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, Request(link), CancellationToken.None));
        Assert.StartsWith(ExportDestinationPolicy.RealDirectory(real) + Path.DirectorySeparatorChar, response.ModDirectory, StringComparison.OrdinalIgnoreCase);
        Assert.True(File.Exists(response.PluginPath));
    }

    [Fact]
    public void NativeNpcExport_UsesTheSamePolicy()
    {
        var (lo, state) = Profile(Path.Combine(_root, "Data"));
        var output = Dir(lo.DataDirectory, "Exports");
        var request = new CreationExportRequest
        {
            PluginName = "SkyrimNpcStudio_NewNpc.esp",
            OutputDirectory = output,
            TechnicalOnly = true,
            Npc = new NativeNpcDraft { EditorId = "SnsDestinationTest", Name = "Test", RaceFormKey = SyntheticLoadOrder.NordRace.ToString() },
        };
        var ex = Assert.Throws<WorkerException>(() => state.Read(c => NativeNpcBuilder.Export(c.Session, c.Queries, request, CancellationToken.None)));
        Assert.Equal(ErrorCodes.InvalidArgument, ex.Code);
        Assert.Empty(Directory.GetFileSystemEntries(output));
    }

    public void Dispose()
    {
        foreach (var link in _junctions)
        {
            try { Directory.Delete(link); } catch (IOException) { } // the junction only, never its target
        }

        foreach (var owned in Enumerable.Reverse(_owned))
        {
            owned.Dispose();
        }

        try { Directory.Delete(_root, recursive: true); } catch (IOException) { }
    }
}
