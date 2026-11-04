using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// export.forgeStaticResource: a Character Forge package (NIF + DDS the studio verified) becomes an MO2 mod with a new ESL plugin
/// holding one STAT. Synthetic files under %TEMP% only; the plugin is read back with Mutagen here as well.
/// </summary>
public sealed class ForgeStaticResourceExportTests : IDisposable
{
    private const string Nif = "meshes/CharacterForge/goblet/goblet.nif";
    private const string Dds = "textures/characterforge/goblet/goblet_d.dds";
    private const string Model = @"CharacterForge\goblet\goblet.nif";

    private static readonly byte[] NifBytes = [.. "Gamebryo File Format, Version 20.2.0.7\n"u8, 1, 2, 3, 4];
    private static readonly byte[] DdsBytes = [.. "DDS "u8, 124, 0, 0, 0, 9, 9];

    private readonly string _root = Path.Combine(Path.GetTempPath(), "sns-forgestat-" + Guid.NewGuid().ToString("N"));
    private readonly List<IDisposable> _owned = [];
    private readonly WorkerState _state;
    private readonly string _out;
    private readonly string _package;
    private readonly string _data;

    public ForgeStaticResourceExportTests()
    {
        Directory.CreateDirectory(_root);
        _out = Dir(_root, "Exports");
        _data = Path.Combine(_root, "Data");
        _package = Dir(_root, "Goblet.sncf", "exports", "skyrim", "goblet-20261103-120000");
        Write(Nif, NifBytes);
        Write(Dds, DdsBytes);

        var lo = SyntheticLoadOrder.Create(_data);
        _state = new WorkerState();
        _state.Index(new IndexRequest
        {
            ProfileKey = "forge-static-tests-" + Guid.NewGuid().ToString("N"),
            CacheDirectory = Path.Combine(_root, "_cache"),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
        _owned.Add(lo);
        _owned.Add(_state);
    }

    public void Dispose()
    {
        foreach (var owned in Enumerable.Reverse(_owned))
        {
            owned.Dispose();
        }

        try
        {
            Directory.Delete(_root, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static string Dir(params string[] parts)
    {
        var path = Path.Combine(parts);
        Directory.CreateDirectory(path);
        return path;
    }

    private void Write(string relative, byte[] bytes)
    {
        var path = Path.Combine(_package, "Data", relative.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    private static string Sha(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));

    private ExportForgeResourceRequest Request(string plugin = "CF_Goblet.esp") => new()
    {
        PluginName = plugin,
        OutputDirectory = _out,
        EditorId = "CF_Goblet",
        SourceDirectory = _package,
        Files = [new ForgeResourceFile { RelativePath = Nif, Sha256 = Sha(NifBytes) }, new ForgeResourceFile { RelativePath = Dds, Sha256 = Sha(DdsBytes) }],
        ModelPath = Model,
        BoundsMin = [-4, -4, -3],
        BoundsMax = [4, 4, 17],
        Description = "Copa de prueba",
    };

    private ExportResponse Export(ExportForgeResourceRequest request, CancellationToken? ct = null) =>
        _state.Export(c => ModPackager.ExportForgeStaticResource(c.Session, request, ct ?? TestContext.Current.CancellationToken));

    private string[] Snapshot() => Directory.GetFileSystemEntries(_out, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal).ToArray();

    [Fact]
    public void TheVerifiedPackageBecomesAModWithOneStatPointingAtTheNif()
    {
        var response = Export(Request());

        Assert.Equal(Path.Combine(_out, "CF_Goblet"), response.ModDirectory);
        Assert.True(response.EslFlagged);
        Assert.Empty(response.Masters);
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));
        var record = Assert.Single(response.Records);
        Assert.Equal(("STAT", "CF_Goblet", "new"), (record.RecordType, record.EditorId, record.Kind));
        Assert.Contains(response.Issues, i => i.Code == "NoCollision");

        // Files byte-identical to the package, at their Data-relative paths.
        Assert.Equal(NifBytes, File.ReadAllBytes(Path.Combine(response.ModDirectory, Nif.Replace('/', Path.DirectorySeparatorChar))));
        Assert.Equal(DdsBytes, File.ReadAllBytes(Path.Combine(response.ModDirectory, Dds.Replace('/', Path.DirectorySeparatorChar))));
        Assert.Equal(["CF_Goblet.esp", Nif, Dds], response.Files.Select(f => f.Path));

        // An independent read-back of the plugin.
        using var mod = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("CF_Goblet.esp"), response.PluginPath), SkyrimRelease.SkyrimSE);
        Assert.True(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
        Assert.Empty(mod.ModHeader.MasterReferences);
        var stat = Assert.Single(mod.Statics);
        Assert.Equal("CF_Goblet", stat.EditorID);
        Assert.Equal(Model, stat.Model!.File.GivenPath);
        Assert.InRange(stat.FormKey.ID, 0x800u, 0xFFFu);
        Assert.Equal((-4, -4, -3), ((int)stat.ObjectBounds.First.X, (int)stat.ObjectBounds.First.Y, (int)stat.ObjectBounds.First.Z));
        Assert.Equal((4, 4, 17), ((int)stat.ObjectBounds.Second.X, (int)stat.ObjectBounds.Second.Y, (int)stat.ObjectBounds.Second.Z));
        Assert.Single(mod.EnumerateMajorRecords());

        // ModPackager writes its manifest with Encoding.UTF8 (BOM), unlike the body packager: read it as text.
        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        Assert.Equal("forge-static-resource", manifest.RootElement.GetProperty("mode").GetString());
        Assert.False(manifest.RootElement.GetProperty("inGameVerified").GetBoolean());
        Assert.Contains("player.placeatme", manifest.RootElement.GetProperty("pendingVerification")[0].GetString(), StringComparison.Ordinal);
        var readme = File.ReadAllText(Path.Combine(response.ModDirectory, "README.md"));
        Assert.Contains("help CF_Goblet 4", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("FaceGen", readme, StringComparison.Ordinal);  // NPC-only wording must not leak into a static resource
        Assert.DoesNotContain("override(s) de NPC", readme, StringComparison.Ordinal);
        Assert.Single(response.PendingVerification);

        using var zip = ZipFile.OpenRead(response.ZipPath!);
        Assert.Equal(["CF_Goblet.esp", "manifest.json", Nif, "README.md", Dds], zip.Entries.Select(e => e.FullName).Order(StringComparer.OrdinalIgnoreCase));
        Assert.Contains(response.Verification, v => v.StartsWith("ZIP releído", StringComparison.Ordinal));
    }

    [Fact]
    public void WithoutTheEslFlagTheStatIsStillTheOnlyRecord()
    {
        var response = Export(Request() with { EslFlag = false, CreateZip = false });
        Assert.False(response.EslFlagged);
        Assert.Null(response.ZipPath);
        using var mod = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("CF_Goblet.esp"), response.PluginPath), SkyrimRelease.SkyrimSE);
        Assert.False(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
        Assert.Single(mod.Statics);
    }

    [Fact]
    public void AFileThatChangedAfterTheStudioVerifiedItIsRefusedAndNothingIsWritten()
    {
        var before = Snapshot();
        Write(Nif, [.. NifBytes, 0xFF]);
        var ex = Assert.Throws<WorkerException>(() => Export(Request()));
        Assert.Equal(ErrorCodes.ValidationFailed, ex.Code);
        Assert.Contains("does not match the hash", ex.Message, StringComparison.Ordinal);
        Assert.Equal(before, Snapshot());
    }

    [Theory]
    [InlineData("../goblet.nif")]
    [InlineData(@"meshes\..\..\goblet.nif")]
    [InlineData(@"C:\meshes\goblet.nif")]
    [InlineData("/meshes/goblet.nif")]
    [InlineData("meshes//goblet.nif")]
    [InlineData("meshes/./goblet.nif")]
    [InlineData("scripts/goblet.pex")]
    [InlineData("meshes/goblet.dds")]
    [InlineData("textures/goblet.nif")]
    [InlineData("meshes/NUL.nif")]
    [InlineData("meshes/a .nif")]
    [InlineData("meshes/a?.nif")]
    [InlineData("SkyrimSE.exe")]
    [InlineData("")]
    public void UnsafePathsAreRefusedBeforeAnythingIsRead(string path)
    {
        var before = Snapshot();
        var request = Request() with { Files = [new ForgeResourceFile { RelativePath = path, Sha256 = Sha(NifBytes) }] };
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(request)).Code);
        Assert.Equal(before, Snapshot());
    }

    [Fact]
    public void TheRequestItselfIsValidated()
    {
        var before = Snapshot();
        ExportForgeResourceRequest[] bad =
        [
            Request() with { ModelPath = @"CharacterForge\goblet\other.nif" },
            Request() with { ModelPath = @"..\goblet.nif" },
            Request() with { EditorId = "1Goblet" },
            Request() with { EditorId = "CF Goblet" },
            Request() with { BoundsMin = [5, -4, -3] },
            Request() with { BoundsMin = [0, 0, 0], BoundsMax = [0, 0, 0] },
            Request() with { BoundsMax = [4, 4] },
            Request() with { Files = [] },
            Request() with { Files = [new ForgeResourceFile { RelativePath = Nif, Sha256 = "abc" }] },
            Request() with { Files = [new ForgeResourceFile { RelativePath = Nif, Sha256 = Sha(NifBytes) }, new ForgeResourceFile { RelativePath = "MESHES/charactERforge/goblet/goblet.nif", Sha256 = Sha(NifBytes) }] },
            Request() with { SourceDirectory = "relative\\package" },
            Request() with { SourceDirectory = Path.Combine(_root, "missing") },
            Request() with { Files = [new ForgeResourceFile { RelativePath = Nif, Sha256 = Sha(NifBytes) }, new ForgeResourceFile { RelativePath = "textures/missing.dds", Sha256 = Sha(DdsBytes) }] },
            Request("CF Goblet.esm"),
        ];
        foreach (var request in bad)
        {
            Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(request)).Code);
        }

        Assert.Equal(before, Snapshot());
    }

    [Fact]
    public void TheOutputCannotBeAnActivePluginsDataFolderNorAnExistingMod()
    {
        var policy = Assert.Throws<WorkerException>(() => Export(Request() with { OutputDirectory = _data }));
        Assert.Equal(ErrorCodes.InvalidArgument, policy.Code);
        Assert.False(File.Exists(Path.Combine(_data, "CF_Goblet", "CF_Goblet.esp")));

        var first = Export(Request());
        var plugin = File.ReadAllBytes(first.PluginPath);
        Assert.Equal(ErrorCodes.OutputExists, Assert.Throws<WorkerException>(() => Export(Request())).Code);
        Assert.Equal(plugin, File.ReadAllBytes(first.PluginPath));
    }

    [Fact]
    public void ACancelledExportLeavesNothingBehind()
    {
        var before = Snapshot();
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        Assert.ThrowsAny<OperationCanceledException>(() => Export(Request(), cts.Token));
        Assert.Equal(before, Snapshot());
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));
    }

    [Fact]
    public void TheSourcePackageIsNeverModified()
    {
        var files = Directory.GetFiles(_package, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal)
            .Select(f => (f, Sha(File.ReadAllBytes(f)), File.GetLastWriteTimeUtc(f))).ToArray();
        Export(Request());
        Assert.Equal(files, Directory.GetFiles(_package, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal)
            .Select(f => (f, Sha(File.ReadAllBytes(f)), File.GetLastWriteTimeUtc(f))).ToArray());
    }
}
