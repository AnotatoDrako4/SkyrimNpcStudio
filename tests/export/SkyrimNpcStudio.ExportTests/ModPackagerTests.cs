using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using Xunit;

namespace SkyrimNpcStudio.ExportTests;

/// <summary>
/// What the exported mod promises: the file inventory, the checks that ran, what only the game can
/// confirm, and a ZIP that is verified (safe names, no extras, identical bytes) before it is delivered.
/// </summary>
public sealed class ModPackagerTests : IDisposable
{
    private readonly SyntheticLoadOrder _lo = SyntheticLoadOrder.Create();
    private readonly WorkerState _state = new();
    private readonly string _out;

    public ModPackagerTests()
    {
        _out = Path.Combine(Path.GetTempPath(), "sns-packager-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_out);
        _state.Index(new IndexRequest
        {
            ProfileKey = "packager-tests",
            CacheDirectory = Path.Combine(_out, "_cache"),
            Plugins = _lo.LoadOrder.Select(p => new LoadOrderEntry { FileName = p.FileName, Path = p.Path }).ToArray(),
        }, _ => { }, CancellationToken.None);
    }

    private ExportResponse ExportOutfit(string name, bool zip = true) =>
        _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, new ExportOutfitPatchRequest
        {
            PluginName = name,
            OutputDirectory = _out,
            CreateZip = zip,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.SteelBoots.ToString()],
                },
            ],
        }, CancellationToken.None));

    [Fact]
    public void TheResponseAndTheManifestListTheFilesTheChecksAndWhatIsStillPending()
    {
        var response = ExportOutfit("SNS_Packager_A.esp");

        var plugin = Assert.Single(response.Files);
        Assert.Equal("SNS_Packager_A.esp", plugin.Path);
        Assert.Equal(response.PluginSha256, plugin.Sha256);
        Assert.Equal(new FileInfo(response.PluginPath).Length, plugin.Bytes);

        Assert.Contains(response.Verification, v => v.StartsWith("Plugin releído con Mutagen", StringComparison.Ordinal));
        Assert.Contains(response.Verification, v => v.StartsWith("ZIP releído", StringComparison.Ordinal));
        Assert.Contains(response.PendingVerification, p => p.Contains("dentro de Skyrim", StringComparison.Ordinal));

        using var manifest = JsonDocument.Parse(File.ReadAllText(response.ManifestPath));
        var root = manifest.RootElement;
        Assert.Equal("SNS_Packager_A.esp", root.GetProperty("files")[0].GetProperty("path").GetString());
        Assert.Equal(response.PluginSha256, root.GetProperty("files")[0].GetProperty("sha256").GetString());
        Assert.True(root.GetProperty("verification").GetArrayLength() >= 2);
        Assert.True(root.GetProperty("pendingVerification").GetArrayLength() >= 1);
        Assert.False(root.GetProperty("inGameVerified").GetBoolean());
    }

    [Fact]
    public void TheDeliveredZipHoldsExactlyTheModFiles()
    {
        var response = ExportOutfit("SNS_Packager_B.esp");

        using var zip = ZipFile.OpenRead(response.ZipPath!);
        var names = zip.Entries.Select(e => e.FullName).Order(StringComparer.Ordinal).ToArray();
        Assert.Equal(["README.md", "SNS_Packager_B.esp", "manifest.json"], names);
        ModPackager.VerifyZip(response.ZipPath!, response.ModDirectory);
    }

    private string MakeModDir(string name)
    {
        var dir = Path.Combine(_out, name);
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "a.esp"), "plugin");
        Directory.CreateDirectory(Path.Combine(dir, "meshes"));
        File.WriteAllText(Path.Combine(dir, "meshes", "x.nif"), "nif");
        return dir;
    }

    private static void WriteZip(string path, params (string Name, string Content)[] entries)
    {
        using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
        foreach (var (name, content) in entries)
        {
            var entry = zip.CreateEntry(name);
            using var writer = new StreamWriter(entry.Open());
            writer.Write(content);
        }
    }

    [Fact]
    public void VerifyZip_AcceptsAZipThatMatchesTheFolder()
    {
        var dir = MakeModDir("ok");
        var zip = Path.Combine(_out, "ok.zip");
        WriteZip(zip, ("a.esp", "plugin"), ("meshes/x.nif", "nif"));
        ModPackager.VerifyZip(zip, dir);
    }

    [Theory]
    [InlineData("../evil.txt")]
    [InlineData("/abs.txt")]
    [InlineData("C:/Windows/evil.txt")]
    [InlineData("meshes\\x.nif")]
    [InlineData("meshes/../../evil.txt")]
    public void VerifyZip_RejectsUnsafeEntryNames(string name)
    {
        var dir = MakeModDir("unsafe");
        var zip = Path.Combine(_out, "unsafe.zip");
        WriteZip(zip, ("a.esp", "plugin"), (name, "x"));
        var error = Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(zip, dir));
        Assert.Equal(ErrorCodes.ValidationFailed, error.Code);
    }

    [Fact]
    public void VerifyZip_RejectsAnExtraFileAMissingFileAndChangedContent()
    {
        var dir = MakeModDir("tamper");
        var extra = Path.Combine(_out, "extra.zip");
        WriteZip(extra, ("a.esp", "plugin"), ("meshes/x.nif", "nif"), ("surprise.dll", "x"));
        Assert.Contains("surprise.dll", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(extra, dir)).Message, StringComparison.Ordinal);

        var missing = Path.Combine(_out, "missing.zip");
        WriteZip(missing, ("a.esp", "plugin"));
        Assert.Contains("missing", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(missing, dir)).Message, StringComparison.Ordinal);

        var changed = Path.Combine(_out, "changed.zip");
        WriteZip(changed, ("a.esp", "PLUGIN"), ("meshes/x.nif", "nif"));
        Assert.Contains("differs", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(changed, dir)).Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AnExistingModFolderOrZipIsNeverOverwritten()
    {
        var first = ExportOutfit("SNS_Packager_C.esp");
        var before = SHA256.HashData(File.ReadAllBytes(first.PluginPath));

        var error = Assert.Throws<WorkerException>(() => ExportOutfit("SNS_Packager_C.esp"));
        Assert.Equal(ErrorCodes.OutputExists, error.Code);
        Assert.Equal(before, SHA256.HashData(File.ReadAllBytes(first.PluginPath)));
        Assert.Empty(Directory.EnumerateDirectories(_out, ".sns-staging-*"));
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
