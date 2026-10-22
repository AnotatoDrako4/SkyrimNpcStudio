using System.Globalization;
using System.Text;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Strings;
using Noggog;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

public sealed record LoadOrderPluginEntry(string FileName, string FullPath, bool Enabled);

public sealed class LoadedPluginInfo
{
    public required int LoadIndex { get; init; }
    public required ModKey ModKey { get; init; }
    public required string FilePath { get; init; }
    public required long SizeBytes { get; init; }
    public required string Sha256 { get; init; }
    public required MasterStyle Style { get; init; }
    public required bool IsMasterFlagged { get; init; }
    public required IReadOnlyList<ModKey> Masters { get; init; }
    public required ISkyrimModDisposableGetter Mod { get; init; }
    public bool IsLight => Style == MasterStyle.Small;
}

/// <summary>
/// Manages an isolated, read-only Mutagen Skyrim SE load-order context over synthetic or read-only fixture plugins.
/// Tracks enabled vs disabled plugins, provenance (defining vs winning plugin), and load-order fingerprints.
/// </summary>
public sealed class PluginContext : IDisposable
{
    private static readonly HashSet<string> VanillaBaseMasters = new(StringComparer.OrdinalIgnoreCase)
    {
        "Skyrim.esm",
        "Update.esm",
        "Dawnguard.esm",
        "HearthFires.esm",
        "Dragonborn.esm"
    };

    private readonly Dictionary<ModKey, LoadedPluginInfo> _enabledByKey;
    private readonly HashSet<ModKey> _disabledKeys;

    public string FixtureDirectory { get; }
    public string ProfileName { get; }
    public IReadOnlyList<LoadedPluginInfo> EnabledPlugins { get; }
    public IReadOnlyList<string> DisabledPluginNames { get; }
    public IReadOnlyList<BehaviorIssue> LoadIssues { get; }
    public ILinkCache<ISkyrimMod, ISkyrimModGetter> LinkCache { get; }
    public ILinkCache<ISkyrimMod, ISkyrimModGetter> VanillaLinkCache { get; }
    public string LoadOrderFingerprint { get; }

    private PluginContext(
        string fixtureDirectory,
        string profileName,
        List<LoadedPluginInfo> enabledPlugins,
        List<string> disabledPluginNames,
        HashSet<ModKey> disabledKeys,
        List<BehaviorIssue> loadIssues)
    {
        FixtureDirectory = fixtureDirectory;
        ProfileName = profileName;
        EnabledPlugins = enabledPlugins;
        DisabledPluginNames = disabledPluginNames;
        _disabledKeys = disabledKeys;
        LoadIssues = loadIssues;
        _enabledByKey = enabledPlugins.ToDictionary(p => p.ModKey);

        LinkCache = enabledPlugins
            .Select(p => (ISkyrimModGetter)p.Mod)
            .ToImmutableLinkCache();

        VanillaLinkCache = enabledPlugins
            .Where(p => IsVanillaBaseMaster(p.ModKey))
            .Select(p => (ISkyrimModGetter)p.Mod)
            .ToImmutableLinkCache();

        var fpBuilder = new StringBuilder();
        fpBuilder.Append("profile=").Append(profileName).Append(';');
        foreach (var p in enabledPlugins)
        {
            fpBuilder.Append(p.LoadIndex)
                .Append(':')
                .Append(p.ModKey.FileName.String)
                .Append(':')
                .Append(p.Sha256)
                .Append(';');
        }
        foreach (var d in disabledPluginNames.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
        {
            fpBuilder.Append("disabled:").Append(d).Append(';');
        }
        LoadOrderFingerprint = SandboxGuard.ComputeSha256Hex(fpBuilder.ToString());
    }

    public static bool IsVanillaBaseMaster(ModKey modKey) =>
        VanillaBaseMasters.Contains(modKey.FileName.String);

    public bool IsModEnabled(ModKey modKey) => _enabledByKey.ContainsKey(modKey);

    public bool IsModDisabled(ModKey modKey) => _disabledKeys.Contains(modKey);

    public LoadedPluginInfo? FindEnabled(ModKey modKey) => _enabledByKey.GetValueOrDefault(modKey);

    /// <summary>
    /// Opens a fixture directory using its plugins.txt (or profile-&lt;name&gt;.txt) in strictly read-only binary overlay mode.
    /// </summary>
    public static PluginContext OpenDirectory(string directoryPath, string? profileName = null)
    {
        if (string.IsNullOrWhiteSpace(directoryPath) || !Directory.Exists(directoryPath))
        {
            throw new LabUsageException("FixtureNotFound", $"Fixture directory does not exist: '{directoryPath}'.", 2);
        }

        var fullDir = Path.GetFullPath(directoryPath);
        var activeProfile = string.IsNullOrWhiteSpace(profileName) ? "default" : profileName.Trim();
        var pluginsFile = activeProfile.Equals("default", StringComparison.OrdinalIgnoreCase)
            ? Path.Combine(fullDir, "plugins.txt")
            : Path.Combine(fullDir, $"plugins.{activeProfile}.txt");

        var entries = new List<LoadOrderPluginEntry>();
        if (File.Exists(pluginsFile))
        {
            foreach (var rawLine in File.ReadAllLines(pluginsFile, Encoding.UTF8))
            {
                var line = rawLine.Trim();
                if (string.IsNullOrEmpty(line) || line.StartsWith('#'))
                {
                    continue;
                }
                bool enabled = true;
                string fileName = line;
                if (line.StartsWith('*'))
                {
                    enabled = true;
                    fileName = line[1..].Trim();
                }
                else if (line.StartsWith('-'))
                {
                    enabled = false;
                    fileName = line[1..].Trim();
                }
                entries.Add(new LoadOrderPluginEntry(fileName, Path.Combine(fullDir, fileName), enabled));
            }
        }
        else
        {
            // Fallback deterministic ordering: Skyrim.esm first, then .esm, .esl, .esp
            var files = Directory.GetFiles(fullDir)
                .Where(f => f.EndsWith(".esm", StringComparison.OrdinalIgnoreCase)
                         || f.EndsWith(".esp", StringComparison.OrdinalIgnoreCase)
                         || f.EndsWith(".esl", StringComparison.OrdinalIgnoreCase))
                .Select(f => new FileInfo(f))
                .OrderBy(f => f.Name.Equals("Skyrim.esm", StringComparison.OrdinalIgnoreCase) ? 0 : 1)
                .ThenBy(f => f.Extension.Equals(".esm", StringComparison.OrdinalIgnoreCase) ? 0 : 1)
                .ThenBy(f => f.Name, StringComparer.OrdinalIgnoreCase);

            foreach (var f in files)
            {
                entries.Add(new LoadOrderPluginEntry(f.Name, f.FullName, true));
            }
        }

        return OpenEntries(fullDir, activeProfile, entries);
    }

    public static PluginContext OpenEntries(
        string fixtureDirectory,
        string profileName,
        IReadOnlyList<LoadOrderPluginEntry> entries)
    {
        var issues = new List<BehaviorIssue>();
        var enabledCandidates = new List<(LoadOrderPluginEntry Entry, ModKey Key, FileInfo File)>();
        var disabledNames = new List<string>();
        var disabledKeys = new HashSet<ModKey>();
        var seenEnabled = new HashSet<ModKey>();

        foreach (var entry in entries)
        {
            if (!ModKey.TryFromFileName(entry.FileName, out var key))
            {
                issues.Add(new BehaviorIssue(
                    "InvalidPluginName",
                    ValidationSeverity.ERROR,
                    $"'{entry.FileName}' is not a valid Bethesda plugin file name.",
                    SourcePlugin: entry.FileName));
                continue;
            }

            if (!entry.Enabled)
            {
                disabledNames.Add(entry.FileName);
                disabledKeys.Add(key);
                continue;
            }

            if (!seenEnabled.Add(key))
            {
                issues.Add(new BehaviorIssue(
                    "DuplicatePlugin",
                    ValidationSeverity.WARNING,
                    $"Plugin '{entry.FileName}' is listed multiple times; duplicate entry ignored.",
                    SourcePlugin: entry.FileName));
                continue;
            }

            var file = new FileInfo(entry.FullPath);
            if (!file.Exists)
            {
                issues.Add(new BehaviorIssue(
                    "PluginNotFound",
                    ValidationSeverity.ERROR,
                    $"Enabled plugin '{entry.FileName}' was not found at '{entry.FullPath}'.",
                    SourcePlugin: entry.FileName));
                continue;
            }

            enabledCandidates.Add((entry, key, file));
        }

        var styles = new Dictionary<ModKey, KeyedMasterStyle>();
        foreach (var (entry, key, file) in enabledCandidates)
        {
            try
            {
                styles[key] = KeyedMasterStyle.FromPath(new ModPath(key, file.FullName), GameRelease.SkyrimSE);
            }
            catch (Exception ex)
            {
                issues.Add(new BehaviorIssue(
                    "PluginHeaderUnreadable",
                    ValidationSeverity.ERROR,
                    $"Cannot read header of '{entry.FileName}': {ex.Message}",
                    SourcePlugin: entry.FileName));
            }
        }

        var styleCache = new Cache<IModMasterStyledGetter, ModKey>(x => x.ModKey);
        foreach (var style in styles.Values)
        {
            styleCache.Set(style);
        }

        var loadedPlugins = new List<LoadedPluginInfo>();
        var loadedSoFar = new HashSet<ModKey>();
        foreach (var (entry, key, file) in enabledCandidates)
        {
            if (!styles.TryGetValue(key, out var style))
            {
                continue;
            }

            ISkyrimModDisposableGetter mod;
            try
            {
                mod = SkyrimMod.CreateFromBinaryOverlay(
                    new ModPath(key, file.FullName),
                    SkyrimRelease.SkyrimSE,
                    new BinaryReadParameters
                    {
                        MasterFlagsLookup = styleCache,
                        StringsParam = new StringsReadParameters { TargetLanguage = Language.English },
                    });
            }
            catch (Exception ex)
            {
                issues.Add(new BehaviorIssue(
                    "PluginUnreadable",
                    ValidationSeverity.ERROR,
                    $"Cannot open binary overlay for '{entry.FileName}': {ex.Message}",
                    SourcePlugin: entry.FileName));
                continue;
            }

            var masters = mod.ModHeader.MasterReferences.Select(m => m.Master).ToList();
            foreach (var master in masters)
            {
                if (!loadedSoFar.Contains(master))
                {
                    issues.Add(new BehaviorIssue(
                        "MissingOrUnorderedMaster",
                        ValidationSeverity.ERROR,
                        $"Plugin '{entry.FileName}' requires master '{master.FileName}', which is not loaded before it.",
                        SourcePlugin: entry.FileName,
                        Evidence: $"Required master: {master.FileName}"));
                }
            }

            loadedPlugins.Add(new LoadedPluginInfo
            {
                LoadIndex = loadedPlugins.Count,
                ModKey = key,
                FilePath = file.FullName,
                SizeBytes = file.Length,
                Sha256 = SandboxGuard.ComputeFileSha256Hex(file.FullName),
                Style = style.MasterStyle,
                IsMasterFlagged = mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Master),
                Masters = masters,
                Mod = mod,
            });
            loadedSoFar.Add(key);
        }

        return new PluginContext(fixtureDirectory, profileName, loadedPlugins, disabledNames, disabledKeys, issues);
    }

    /// <summary>
    /// Strict FormKey parser. Supports Mutagen format ("013AD2:Skyrim.esm" or "00013AD2:Skyrim.esm").
    /// Never guesses by EditorID.
    /// </summary>
    public static bool TryParseFormKey(string? text, out FormKey formKey)
    {
        formKey = FormKey.Null;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var trimmed = text.Trim();
        if (FormKey.TryFactory(trimmed, out var parsed) && !parsed.IsNull)
        {
            formKey = parsed;
            return true;
        }

        var colonIndex = trimmed.IndexOf(':');
        if (colonIndex > 0 && colonIndex < trimmed.Length - 1)
        {
            var idPart = trimmed[..colonIndex].Trim();
            var modPart = trimmed[(colonIndex + 1)..].Trim();
            if (idPart.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                idPart = idPart[2..];
            }
            if (uint.TryParse(idPart, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var id) &&
                ModKey.TryFromFileName(modPart, out var modKey))
            {
                formKey = new FormKey(modKey, id & 0xFFFFFFu);
                return !formKey.IsNull;
            }
        }

        return false;
    }

    public void Dispose()
    {
        foreach (var plugin in EnabledPlugins)
        {
            plugin.Mod.Dispose();
        }
    }
}
