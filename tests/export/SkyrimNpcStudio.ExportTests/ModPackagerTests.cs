using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Skyrim;
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
        ModPackager.VerifyZip(response.ZipPath!, response.ModDirectory, TestContext.Current.CancellationToken);
    }

    [Fact]
    public void DeliveredPluginReopensFromZipWithExpectedMastersAndRecords()
    {
        var response = ExportOutfit("SNS_ZipReadback.esp");
        var extracted = Path.Combine(_out, "readback");
        ZipFile.ExtractToDirectory(response.ZipPath!, extracted);
        using var plugin = SkyrimMod.CreateFromBinaryOverlay(
            new ModPath(ModKey.FromFileName("SNS_ZipReadback.esp"), Path.Combine(extracted, "SNS_ZipReadback.esp")),
            SkyrimRelease.SkyrimSE, new BinaryReadParameters { MasterFlagsLookup = _state.Read(c => c.Session.MasterStyles) });
        Assert.Equal(response.Masters, plugin.ModHeader.MasterReferences.Select(m => m.Master.FileName.String));
        Assert.Equal(SyntheticLoadOrder.Lydia, Assert.Single(plugin.Npcs).FormKey);
        Assert.Equal(Assert.Single(plugin.Outfits).FormKey, plugin.Npcs.First().DefaultOutfit.FormKey);
        Assert.Empty(plugin.Armors);
        using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(extracted, "manifest.json")));
        foreach (var file in manifest.RootElement.GetProperty("files").EnumerateArray())
        {
            var bytes = File.ReadAllBytes(Path.Combine(extracted, file.GetProperty("path").GetString()!));
            Assert.Equal(file.GetProperty("bytes").GetInt64(), bytes.LongLength);
            Assert.Equal(file.GetProperty("sha256").GetString(), Convert.ToHexString(SHA256.HashData(bytes)));
        }

        // Optional retained synthetic artifact for a local engineering run; never install it into a real profile.
        if (Environment.GetEnvironmentVariable("SNS_SYNTHETIC_EXPORT_EVIDENCE") is { Length: > 0 } evidence)
        {
            Directory.CreateDirectory(evidence);
            File.Copy(response.ZipPath!, Path.Combine(evidence, "SNS_ZipReadback.zip"), overwrite: false);
            File.Copy(response.ManifestPath, Path.Combine(evidence, "manifest.json"), overwrite: false);
        }
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
        ModPackager.VerifyZip(zip, dir, TestContext.Current.CancellationToken);
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
        var error = Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(zip, dir, TestContext.Current.CancellationToken));
        Assert.Equal(ErrorCodes.ValidationFailed, error.Code);
    }

    [Theory]
    [InlineData("meshes//nested/")]
    [InlineData("meshes/unexpected/")]
    [InlineData("meshes/", "payload")]
    public void VerifyZip_RejectsMalformedUnexpectedOrNonemptyDirectories(string name, string content = "")
    {
        var dir = MakeModDir("directory-entry");
        var zip = Path.Combine(_out, "directory-entry.zip");
        WriteZip(zip, ("a.esp", "plugin"), ("meshes/x.nif", "nif"), (name, content));
        var error = Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(zip, dir, TestContext.Current.CancellationToken));
        Assert.Equal(ErrorCodes.ValidationFailed, error.Code);
    }

    [Fact]
    public void PackagingAndVerificationObserveCancellationBeforeOpeningFiles()
    {
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        var zip = Path.Combine(_out, "cancelled.zip");
        Assert.Throws<OperationCanceledException>(() => ModPackager.CreateZip(_out, zip, cancellation.Token));
        Assert.False(File.Exists(zip));
        Assert.Throws<OperationCanceledException>(() => ModPackager.VerifyZip(zip, _out, cancellation.Token));
    }

    [Fact]
    public void CreateZipSupportsFilesOlderThanTheZipTimestampRange()
    {
        var dir = MakeModDir("old-date");
        File.SetLastWriteTime(Path.Combine(dir, "a.esp"), new DateTime(1970, 1, 1));
        var zip = Path.Combine(_out, "old-date.zip");
        ModPackager.CreateZip(dir, zip, TestContext.Current.CancellationToken);
        ModPackager.VerifyZip(zip, dir, TestContext.Current.CancellationToken);
    }

    [Fact]
    public void VerifyZip_AcceptsAnEmptyDirectoryContainingExpectedFiles()
    {
        var dir = MakeModDir("explicit-directory");
        var zip = Path.Combine(_out, "explicit-directory.zip");
        WriteZip(zip, ("a.esp", "plugin"), ("meshes/", ""), ("meshes/x.nif", "nif"));
        ModPackager.VerifyZip(zip, dir, TestContext.Current.CancellationToken);
    }

    [Fact]
    public void VerifyZip_RejectsAnExtraFileAMissingFileAndChangedContent()
    {
        var dir = MakeModDir("tamper");
        var extra = Path.Combine(_out, "extra.zip");
        WriteZip(extra, ("a.esp", "plugin"), ("meshes/x.nif", "nif"), ("surprise.dll", "x"));
        Assert.Contains("surprise.dll", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(extra, dir, TestContext.Current.CancellationToken)).Message, StringComparison.Ordinal);

        var missing = Path.Combine(_out, "missing.zip");
        WriteZip(missing, ("a.esp", "plugin"));
        Assert.Contains("missing", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(missing, dir, TestContext.Current.CancellationToken)).Message, StringComparison.Ordinal);

        var changed = Path.Combine(_out, "changed.zip");
        WriteZip(changed, ("a.esp", "PLUGIN"), ("meshes/x.nif", "nif"));
        Assert.Contains("differs", Assert.Throws<WorkerException>(() => ModPackager.VerifyZip(changed, dir, TestContext.Current.CancellationToken)).Message, StringComparison.Ordinal);
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

    /// <summary>
    /// Audit v2 F-19: the description went into meta.ini's notes as is, so a line break started a new INI line:
    /// "[Plugins]" or "gameName=..." in a description became sections and keys MO2 reads. The value is now one
    /// line in QSettings' escaping (MO2 reads meta.ini with QSettings), and reads back as the same text.
    /// </summary>
    [Fact]
    public void AMultiLineDescriptionCannotAddLinesToMetaIni()
    {
        const string description = "Línea 1\n[Plugins]\r\ngameName=Oblivion\nruta C:\\mods \"citada\"\tfin\u0007";
        var response = _state.Read(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, new ExportOutfitPatchRequest
        {
            PluginName = "SNS_Packager_Notes.esp",
            OutputDirectory = _out,
            CreateZip = false,
            Description = description,
            Assignments =
            [
                new OutfitAssignment
                {
                    NpcFormKey = SyntheticLoadOrder.Lydia.ToString(),
                    ArmorFormKeys = [SyntheticLoadOrder.SteelCuirass.ToString()],
                },
            ],
        }, CancellationToken.None));

        var lines = File.ReadAllLines(Path.Combine(response.ModDirectory, "meta.ini"));
        Assert.Equal(["[General]"], lines.Where(l => l.StartsWith('[')).ToArray());
        var keys = lines.Skip(1).Select(l => l[..l.IndexOf('=', StringComparison.Ordinal)]).ToArray();
        Assert.Equal(["gameName", "modid", "version", "installationFile", "repository", "comments", "notes"], keys);
        Assert.Contains("gameName=SkyrimSE", lines);

        // Other control characters (here BEL) have no QSettings escape MO2 shows; they become a space.
        var notes = lines.Single(l => l.StartsWith("notes=", StringComparison.Ordinal))["notes=".Length..];
        Assert.Equal(description.Replace('\u0007', ' '), UnescapeQSettingsValue(notes));
    }

    /// <summary>QSettings' reading of a quoted INI value: \\ \" \n \r \t escapes inside double quotes.</summary>
    private static string UnescapeQSettingsValue(string raw)
    {
        Assert.True(raw.Length >= 2 && raw[0] == '"' && raw[^1] == '"', $"notes is not one quoted value: {raw}");
        var text = new System.Text.StringBuilder();
        for (var i = 1; i < raw.Length - 1; i++)
        {
            if (raw[i] != '\\')
            {
                Assert.NotEqual('"', raw[i]);
                text.Append(raw[i]);
                continue;
            }

            text.Append(raw[++i] switch
            {
                'n' => '\n',
                'r' => '\r',
                't' => '\t',
                '"' => '"',
                '\\' => '\\',
                var other => throw new Xunit.Sdk.XunitException($"unexpected escape \\{other}"),
            });
        }

        return text.ToString();
    }

    public void Dispose()
    {
        _state.Dispose();
        _lo.Dispose();
        try { Directory.Delete(_out, recursive: true); } catch { }
    }
}
