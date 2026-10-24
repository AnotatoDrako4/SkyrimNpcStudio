using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// export.bodyDistribution: the studio's text files packaged as an MO2 mod with the export guarantees (destination
/// policy, no overwrite, staging + verified ZIP + atomic commit, rollback). Synthetic folders under %TEMP% only.
/// </summary>
public sealed class BodyDistributionPackagerTests : IDisposable
{
    private const string Distribution = @"SKSE\Plugins\BodyChangeNGdistribution.json";
    private const string DistributionJson = "{\n  \"version\": 1,\n  \"npcs\": { \"0A2C8E:Skyrim.esm\": \"Ñandú body – ✓\" }\n}\n";

    private readonly string _root = Path.Combine(Path.GetTempPath(), "sns-bodypkg-" + Guid.NewGuid().ToString("N"));
    private readonly List<IDisposable> _owned = [];
    private readonly WorkerState _state;
    private readonly string _out;

    public BodyDistributionPackagerTests()
    {
        Directory.CreateDirectory(_root);
        _out = Dir(_root, "Exports");
        _state = Profile(Path.Combine(_root, "Data"));
    }

    private WorkerState Profile(string pluginFolder)
    {
        var lo = SyntheticLoadOrder.Create(pluginFolder);
        var state = new WorkerState();
        state.Index(new IndexRequest
        {
            ProfileKey = "body-package-tests-" + Guid.NewGuid().ToString("N"),
            CacheDirectory = Path.Combine(_root, "_cache-" + Guid.NewGuid().ToString("N")),
            Plugins = lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
        _owned.Add(lo);
        _owned.Add(state);
        return state;
    }

    private static string Dir(params string[] parts)
    {
        var path = Path.Combine(parts);
        Directory.CreateDirectory(path);
        return path;
    }

    private ExportBodyDistributionRequest Request(string modName, string? output = null, bool zip = true, params BodyPackageFile[] files) => new()
    {
        ModName = modName,
        OutputDirectory = output ?? _out,
        CreateZip = zip,
        Author = "Tester",
        Description = "Cuerpos por NPC\n[Plugins]",
        Metadata = new Dictionary<string, string> { ["preset"] = "CBBE Curvy", ["npcCount"] = "1" },
        Files = files.Length > 0 ? files :
        [
            new BodyPackageFile { RelativePath = Distribution, Content = DistributionJson },
            new BodyPackageFile { RelativePath = "README.md", Content = "# Body distribution\n" },
            new BodyPackageFile { RelativePath = "reports/affected-npcs.csv", Content = "formKey,name\n0A2C8E:Skyrim.esm,Lydia\n" },
        ],
    };

    private ExportBodyDistributionResponse Export(ExportBodyDistributionRequest request, WorkerState? state = null) =>
        ExportWith(request, TestContext.Current.CancellationToken, state);

    private ExportBodyDistributionResponse ExportWith(ExportBodyDistributionRequest request, CancellationToken ct, WorkerState? state = null) =>
        (state ?? _state).Read(c => BodyDistributionPackager.Export(c.Session, request, ct));

    private string[] Snapshot(string folder) =>
        Directory.GetFileSystemEntries(folder, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal).ToArray();

    [Fact]
    public void TheHappyPathWritesTheFolderTheZipAndAManifestWhoseHashesMatchTheFiles()
    {
        var response = Export(Request("BodyDirector_Test"));

        Assert.Equal(Path.Combine(_out, "BodyDirector_Test"), response.ModDirectory);
        Assert.Equal(Path.Combine(_out, "BodyDirector_Test.zip"), response.ZipPath);
        Assert.Equal(Path.Combine(response.ModDirectory, "manifest.json"), response.ManifestPath);
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));

        // Files exactly as sent: UTF-8 without BOM, OS separators on disk, '/' in the inventory.
        var distribution = File.ReadAllBytes(Path.Combine(response.ModDirectory, "SKSE", "Plugins", "BodyChangeNGdistribution.json"));
        Assert.Equal(new UTF8Encoding(false).GetBytes(DistributionJson), distribution);
        Assert.NotEqual(0xEF, distribution[0]);
        Assert.Equal(["README.md", "reports/affected-npcs.csv", "SKSE/Plugins/BodyChangeNGdistribution.json"], response.Files.Select(f => f.Path));
        foreach (var file in response.Files)
        {
            var bytes = File.ReadAllBytes(Path.Combine(response.ModDirectory, file.Path.Replace('/', Path.DirectorySeparatorChar)));
            Assert.Equal(bytes.LongLength, file.Bytes);
            Assert.Equal(Convert.ToHexString(SHA256.HashData(bytes)), file.Sha256);
        }

        Assert.False(Directory.EnumerateFiles(response.ModDirectory, "*.es?", SearchOption.AllDirectories).Any());

        var manifestBytes = File.ReadAllBytes(response.ManifestPath);
        Assert.NotEqual(0xEF, manifestBytes[0]);
        using var manifest = JsonDocument.Parse(manifestBytes);
        var root = manifest.RootElement;
        Assert.Equal("skyrim-npc-studio/body-distribution", root.GetProperty("format").GetString());
        Assert.Equal(1, root.GetProperty("formatVersion").GetInt32());
        Assert.Equal(ModPackager.WorkerVersion, root.GetProperty("tool").GetProperty("workerVersion").GetString());
        Assert.Equal($"{Protocol.Major}.{Protocol.Minor}", root.GetProperty("tool").GetProperty("protocol").GetString());
        Assert.Equal("BodyDirector_Test", root.GetProperty("modName").GetString());
        Assert.Equal("Tester", root.GetProperty("author").GetString());
        Assert.Equal("CBBE Curvy", root.GetProperty("metadata").GetProperty("preset").GetString());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("readyForGame").ValueKind);
        Assert.Equal("In-game verification has not been performed", root.GetProperty("pendingVerification")[0].GetString());
        Assert.True(DateTime.TryParse(root.GetProperty("createdUtc").GetString(), System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.RoundtripKind, out _));
        var manifestFiles = root.GetProperty("files").EnumerateArray()
            .Select(f => (f.GetProperty("path").GetString()!, f.GetProperty("bytes").GetInt64(), f.GetProperty("sha256").GetString()!)).ToArray();
        Assert.Equal(response.Files.Select(f => (f.Path, f.Bytes, f.Sha256)).ToArray(), manifestFiles);

        // meta.ini for MO2, with the description on one line; not inside the ZIP (MO2 writes its own).
        var meta = File.ReadAllText(Path.Combine(response.ModDirectory, "meta.ini"));
        Assert.Contains("installationFile=BodyDirector_Test.zip", meta, StringComparison.Ordinal);
        Assert.DoesNotContain("\n[Plugins]", meta, StringComparison.Ordinal);

        using var zip = ZipFile.OpenRead(response.ZipPath!);
        Assert.Equal(["manifest.json", "README.md", "reports/affected-npcs.csv", "SKSE/Plugins/BodyChangeNGdistribution.json"],
            zip.Entries.Select(e => e.FullName).Order(StringComparer.OrdinalIgnoreCase));
        foreach (var entry in zip.Entries)
        {
            using var stream = entry.Open();
            var onDisk = File.ReadAllBytes(Path.Combine(response.ModDirectory, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
            Assert.Equal(Convert.ToHexString(SHA256.HashData(onDisk)), Convert.ToHexString(SHA256.HashData(stream)));
        }

        Assert.Contains(response.Verification, v => v.StartsWith("ZIP releído", StringComparison.Ordinal));
        Assert.Contains(response.Verification, v => v.Contains("releídos", StringComparison.Ordinal));
    }

    [Fact]
    public void WithoutZipOnlyTheFolderIsWritten()
    {
        var response = Export(Request("BodyDirector_NoZip", zip: false));
        Assert.Null(response.ZipPath);
        Assert.False(File.Exists(Path.Combine(_out, "BodyDirector_NoZip.zip")));
        Assert.True(File.Exists(response.ManifestPath));
        Assert.DoesNotContain(response.Verification, v => v.StartsWith("ZIP", StringComparison.Ordinal));
    }

    [Fact]
    public void AnExistingFolderOrZipIsNeverOverwritten()
    {
        var first = Export(Request("BodyDirector_Twice"));
        var before = File.ReadAllBytes(first.ManifestPath);

        var again = Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Twice")));
        Assert.Equal(ErrorCodes.OutputExists, again.Code);
        Assert.Equal(before, File.ReadAllBytes(first.ManifestPath));

        // Only a ZIP of that name exists: still refused.
        File.WriteAllText(Path.Combine(_out, "BodyDirector_ZipOnly.zip"), "not mine");
        Assert.Equal(ErrorCodes.OutputExists, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_ZipOnly", zip: false))).Code);
        Assert.Equal("not mine", File.ReadAllText(Path.Combine(_out, "BodyDirector_ZipOnly.zip")));
        Assert.False(Directory.Exists(Path.Combine(_out, "BodyDirector_ZipOnly")));
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));
    }

    [Theory]
    [InlineData(@"..\x")]
    [InlineData("../x.json")]
    [InlineData(@"SKSE\..\..\x.json")]
    [InlineData(@"C:\x")]
    [InlineData("C:x")]
    [InlineData("/x")]
    [InlineData(@"\x")]
    [InlineData(@"\\server\share\x")]
    [InlineData("a//b.json")]
    [InlineData("./a.json")]
    [InlineData("manifest.json")]
    [InlineData("META.INI")]
    [InlineData("NUL.txt")]
    [InlineData("reports/com1.json")]
    [InlineData("a?.json")]
    [InlineData("trailing.")]
    [InlineData("")]
    public void UnsafeOrReservedPathsAreRefusedBeforeAnythingIsWritten(string path)
    {
        var before = Snapshot(_out);
        var ex = Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Bad", files: [new BodyPackageFile { RelativePath = path, Content = "x" }])));
        Assert.Equal(ErrorCodes.InvalidArgument, ex.Code);
        Assert.Equal(before, Snapshot(_out));
    }

    [Fact]
    public void DuplicatePathsAndFileFolderCollisionsAreRefused()
    {
        var before = Snapshot(_out);
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Dup", files:
        [
            new BodyPackageFile { RelativePath = @"SKSE\Plugins\a.json", Content = "1" },
            new BodyPackageFile { RelativePath = "skse/plugins/A.JSON", Content = "2" },
        ]))).Code);
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Dup", files:
        [
            new BodyPackageFile { RelativePath = "reports", Content = "1" },
            new BodyPackageFile { RelativePath = "reports/a.csv", Content = "2" },
        ]))).Code);
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Dup", files:
        [
            new BodyPackageFile { RelativePath = "reports/a.csv", Content = "2" },
            new BodyPackageFile { RelativePath = "REPORTS", Content = "1" },
        ]))).Code);
        Assert.Equal(before, Snapshot(_out));
    }

    [Theory]
    [InlineData("")]
    [InlineData("  ")]
    [InlineData("a/b")]
    [InlineData(@"a\b")]
    [InlineData("..")]
    [InlineData("x..y")]
    [InlineData(".hidden")]
    [InlineData("CON")]
    [InlineData("aux.mod")]
    [InlineData("Name:Stream")]
    [InlineData("trailing ")]
    [InlineData("trailing.")]
    public void UnsafeModNamesAreRefused(string name)
    {
        var before = Snapshot(_out);
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request(name))).Code);
        Assert.Equal(before, Snapshot(_out));
    }

    [Fact]
    public void AModNameOverOneHundredCharactersIsRefused()
    {
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request(new string('a', 101)))).Code);
        Assert.NotNull(Export(Request(new string('b', 100), zip: false)).ModDirectory);
    }

    [Fact]
    public void TheDestinationPolicyRefusesMo2ModsOverwriteProfilesAndTheGameData()
    {
        // A portable MO2 instance whose profile's plugins live in mods/Synthetic (with meta.ini), as ExportDestinationTests builds it.
        var instance = Dir(_root, "MO2");
        File.WriteAllText(Path.Combine(instance, "ModOrganizer.ini"), "[General]\n");
        var modFolder = Dir(instance, "mods", "Synthetic");
        File.WriteAllText(Path.Combine(modFolder, "meta.ini"), "[General]\n");
        var mo2 = Profile(modFolder);

        foreach (var output in new[]
        {
            Dir(instance, "mods", "NewMod"), Dir(instance, "overwrite"), Dir(instance, "profiles", "Default"), instance, modFolder,
        })
        {
            var before = Snapshot(output);
            var ex = Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Policy", output), mo2));
            Assert.Equal(ErrorCodes.InvalidArgument, ex.Code);
            Assert.Equal(before, Snapshot(output));
        }

        // The default profile's game Data folder and a subfolder of it.
        foreach (var output in new[] { Path.Combine(_root, "Data"), Dir(_root, "Data", "SKSE") })
        {
            Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Policy", output))).Code);
            Assert.False(Directory.Exists(Path.Combine(output, "BodyDirector_Policy")));
        }

        // A relative or missing folder is refused too.
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Policy", "Exports"))).Code);
        Assert.Equal(ErrorCodes.InvalidArgument, Assert.Throws<WorkerException>(() => Export(Request("BodyDirector_Policy", Path.Combine(_root, "missing")))).Code);
    }

    [Fact]
    public void AFailureAfterStagingRollsBackAndLeavesNoStagingFolder()
    {
        // Cancelled once the files are staged: the export must remove everything it wrote.
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        var before = Snapshot(_out);
        Assert.ThrowsAny<OperationCanceledException>(() => ExportWith(Request("BodyDirector_Cancelled"), cts.Token));
        Assert.Equal(before, Snapshot(_out));
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));

        // Content that cannot be encoded as UTF-8 (a lone surrogate) fails while writing: same rollback.
        var broken = Request("BodyDirector_Broken", files:
        [
            new BodyPackageFile { RelativePath = "a.json", Content = "{}" },
            new BodyPackageFile { RelativePath = "b.json", Content = "\uD800" },
        ]);
        Assert.ThrowsAny<ArgumentException>(() => Export(broken));
        Assert.Equal(before, Snapshot(_out));
        Assert.False(Directory.Exists(Path.Combine(_out, "BodyDirector_Broken")));
    }

    [Fact]
    public async Task TheRpcMethodExportsAndMapsErrorsToProtocolCodes()
    {
        var rpc = new WorkerRpcTarget(_state, () => { }, (_, _) => { });
        var response = await rpc.ExportBodyDistribution(Request("BodyDirector_Rpc"), CancellationToken.None);
        Assert.True(File.Exists(response.ZipPath));
        Assert.False(_state.Status().Busy);

        var exists = await Assert.ThrowsAsync<LocalRpcException>(() => rpc.ExportBodyDistribution(Request("BodyDirector_Rpc"), CancellationToken.None));
        Assert.Equal(ErrorCodes.OutputExists, exists.ErrorCode);
        var traversal = await Assert.ThrowsAsync<LocalRpcException>(() => rpc.ExportBodyDistribution(
            Request("BodyDirector_Rpc2", files: [new BodyPackageFile { RelativePath = @"..\x", Content = "x" }]), CancellationToken.None));
        Assert.Equal(ErrorCodes.InvalidArgument, traversal.ErrorCode);
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
        catch (UnauthorizedAccessException)
        {
        }
    }
}
