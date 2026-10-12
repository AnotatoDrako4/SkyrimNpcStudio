using System.Globalization;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Strings;
using Noggog;
using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker.PluginReader;

/// <summary>A plugin of the active load order, opened as a lazy binary overlay.</summary>
internal sealed class SessionPlugin
{
    public required ModKey ModKey { get; init; }
    public required string Path { get; init; }
    public required int LoadIndex { get; init; }
    public required MasterStyle Style { get; init; }
    public required bool IsMasterFlagged { get; init; }
    public required IReadOnlyList<ModKey> Masters { get; init; }
    public required ISkyrimModDisposableGetter Mod { get; init; }
    public required long Size { get; init; }
    public required DateTime LastWriteUtc { get; init; }

    /// <summary>Index within its FormID space: 0..253 for full plugins, 0..4095 for light ones.</summary>
    public required int SpaceIndex { get; init; }

    public bool IsLight => Style == MasterStyle.Small;

    public string FormIdPrefix => IsLight
        ? string.Create(CultureInfo.InvariantCulture, $"FE{SpaceIndex:X3}")
        : SpaceIndex.ToString("X2", CultureInfo.InvariantCulture);
}

/// <summary>
/// The enabled plugins of one MO2 profile, in load order, plus a Mutagen link cache over them.
/// Owns the open overlays; dispose to release file handles.
/// </summary>
internal sealed class LoadOrderSession : IDisposable
{
    private const int MaxFullPlugins = 254;
    private const int MaxLightPlugins = 4096;

    private readonly Dictionary<ModKey, SessionPlugin> _byKey;

    private LoadOrderSession(List<SessionPlugin> plugins, IReadOnlyCache<IModMasterStyledGetter, ModKey> styles)
    {
        Plugins = plugins;
        MasterStyles = styles;
        _byKey = plugins.ToDictionary(p => p.ModKey);
        LinkCache = plugins.Select(p => (ISkyrimModGetter)p.Mod).ToImmutableLinkCache();
        _official = new Lazy<ILinkCache<ISkyrimMod, ISkyrimModGetter>>(() => plugins
            .Where(p => NpcCatalog.FaceRecipeQueries.IsBaseGameMaster(p.ModKey))
            .Select(p => (ISkyrimModGetter)p.Mod)
            .ToImmutableLinkCache());
    }

    private readonly Lazy<ILinkCache<ISkyrimMod, ISkyrimModGetter>> _official;

    /// <summary>Records as the five base-game masters define them (no mod or Creation Club overrides).</summary>
    public ILinkCache<ISkyrimMod, ISkyrimModGetter> OfficialLinkCache => _official.Value;

    public IReadOnlyList<SessionPlugin> Plugins { get; }

    public IReadOnlyCache<IModMasterStyledGetter, ModKey> MasterStyles { get; }

    public ILinkCache<ISkyrimMod, ISkyrimModGetter> LinkCache { get; }

    public SessionPlugin? Find(ModKey key) => _byKey.GetValueOrDefault(key);

    /// <summary>Maps Skyrim.ini sLanguage ("SPANISH", "english"…) to Mutagen's language; unknown values fall back to English.</summary>
    public static Language ParseLanguage(string? sLanguage, List<Issue> issues)
    {
        if (string.IsNullOrWhiteSpace(sLanguage))
        {
            return Language.English;
        }

        var normalized = sLanguage.Trim() switch
        {
            var s when s.Equals("CHINESE", StringComparison.OrdinalIgnoreCase) => nameof(Language.Chinese),
            var s when s.Equals("BRAZILIAN", StringComparison.OrdinalIgnoreCase) => nameof(Language.Portuguese_Brazil),
            var s => s,
        };
        if (Enum.TryParse<Language>(normalized, ignoreCase: true, out var language))
        {
            return language;
        }

        issues.Add(Issue.Warning("UnknownLanguage", $"Game language '{sLanguage}' is not supported; English strings are used."));
        return Language.English;
    }

    /// <summary>
    /// Opens the enabled entries. Entries that cannot be opened are skipped and reported;
    /// structural problems (missing masters, master after dependent) are reported as errors
    /// but do not abort the session, mirroring how the studio must still let users inspect.
    /// </summary>
    public static LoadOrderSession Open(IReadOnlyList<LoadOrderEntry> entries, Language language, List<Issue> issues, CancellationToken ct)
    {
        var enabled = new List<(LoadOrderEntry Entry, ModKey Key, FileInfo File)>();
        var seen = new HashSet<ModKey>();
        foreach (var entry in entries.Where(e => e.Enabled))
        {
            if (!ModKey.TryFromFileName(entry.FileName, out var key))
            {
                issues.Add(Issue.Error("InvalidPluginName", $"'{entry.FileName}' is not a valid plugin file name.", entry.FileName));
                continue;
            }

            if (!seen.Add(key))
            {
                issues.Add(Issue.Warning("DuplicatePlugin", $"'{entry.FileName}' appears more than once; later entries ignored.", entry.FileName));
                continue;
            }

            if (string.IsNullOrWhiteSpace(entry.Path))
            {
                issues.Add(Issue.Error("PluginNotFound", $"'{entry.FileName}' is enabled but no file provides it.", entry.FileName));
                continue;
            }

            var file = new FileInfo(entry.Path);
            if (!file.Exists)
            {
                issues.Add(Issue.Error("PluginNotFound", $"'{entry.Path}' does not exist.", entry.FileName));
                continue;
            }

            enabled.Add((entry, key, file));
        }

        // Pass 1: read only the headers to learn every plugin's master style. Mutagen needs this
        // before parsing records, because FormIDs that point into a light master use a different layout.
        var styles = new Dictionary<ModKey, KeyedMasterStyle>();
        foreach (var (entry, key, file) in enabled)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                var style = KeyedMasterStyle.FromPath(new ModPath(key, file.FullName), GameRelease.SkyrimSE);
                styles[key] = style;
            }
            catch (Exception ex)
            {
                issues.Add(Issue.Error("PluginHeaderUnreadable", $"Cannot read header: {ex.Message}", entry.FileName));
            }
        }

        var styleCache = new Cache<IModMasterStyledGetter, ModKey>(x => x.ModKey);
        foreach (var style in styles.Values)
        {
            styleCache.Set(style);
        }

        // Pass 2: open overlays and check master availability/order.
        var plugins = new List<SessionPlugin>();
        var loadedSoFar = new HashSet<ModKey>();
        int fullIndex = 0, lightIndex = 0;
        foreach (var (entry, key, file) in enabled)
        {
            ct.ThrowIfCancellationRequested();
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
                        StringsParam = new StringsReadParameters { TargetLanguage = language },
                    });
            }
            catch (Exception ex)
            {
                issues.Add(Issue.Error("PluginUnreadable", $"Cannot open plugin: {ex.Message}", entry.FileName));
                continue;
            }

            var masters = mod.ModHeader.MasterReferences.Select(m => m.Master).ToList();
            foreach (var master in masters)
            {
                if (!loadedSoFar.Contains(master))
                {
                    var code = seen.Contains(master) && styles.ContainsKey(master) ? "MasterLoadsLater" : "MissingMaster";
                    issues.Add(Issue.Error(code, $"Requires master '{master.FileName}', which is not loaded before it.", entry.FileName));
                }
            }

            var isLight = style.MasterStyle == MasterStyle.Small;
            var spaceIndex = isLight ? lightIndex++ : fullIndex++;
            if ((!isLight && spaceIndex >= MaxFullPlugins) || (isLight && spaceIndex >= MaxLightPlugins))
            {
                issues.Add(Issue.Error("PluginLimitExceeded", "The game cannot load this many plugins of this kind.", entry.FileName));
            }

            plugins.Add(new SessionPlugin
            {
                ModKey = key,
                Path = file.FullName,
                LoadIndex = plugins.Count,
                Style = style.MasterStyle,
                IsMasterFlagged = mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Master),
                Masters = masters,
                Mod = mod,
                Size = file.Length,
                LastWriteUtc = file.LastWriteTimeUtc,
                SpaceIndex = spaceIndex,
            });
            loadedSoFar.Add(key);
        }

        return new LoadOrderSession(plugins, styleCache);
    }

    /// <summary>Formats a FormKey the way xEdit shows it for this load order, e.g. "000A2C8E" or "FE00A800".</summary>
    public string FormatFormId(FormKey formKey)
    {
        if (!_byKey.TryGetValue(formKey.ModKey, out var plugin))
        {
            return string.Create(CultureInfo.InvariantCulture, $"??{formKey.ID & 0xFFFFFF:X6}");
        }

        var value = plugin.IsLight
            ? 0xFE000000u | ((uint)plugin.SpaceIndex << 12) | (formKey.ID & 0xFFFu)
            : ((uint)plugin.SpaceIndex << 24) | (formKey.ID & 0xFFFFFFu);
        return value.ToString("X8", CultureInfo.InvariantCulture);
    }

    /// <summary>Parses an xEdit-style load-order FormID ("000A2C8E", "FE00A800") into a FormKey.</summary>
    public bool TryParseFormId(string text, out FormKey formKey)
    {
        formKey = FormKey.Null;
        var trimmed = text.Trim();
        if (trimmed.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = trimmed[2..];
        }

        if (trimmed.Length != 8 || !uint.TryParse(trimmed, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var value))
        {
            return false;
        }

        SessionPlugin? owner;
        uint localId;
        if (value >> 24 == 0xFE)
        {
            var lightIndex = (int)((value >> 12) & 0xFFF);
            owner = Plugins.FirstOrDefault(p => p.IsLight && p.SpaceIndex == lightIndex);
            localId = value & 0xFFF;
        }
        else
        {
            var fullIndex = (int)(value >> 24);
            owner = Plugins.FirstOrDefault(p => !p.IsLight && p.SpaceIndex == fullIndex);
            localId = value & 0xFFFFFF;
        }

        if (owner is null)
        {
            return false;
        }

        formKey = new FormKey(owner.ModKey, localId);
        return true;
    }

    public void Dispose()
    {
        foreach (var plugin in Plugins)
        {
            plugin.Mod.Dispose();
        }
    }
}
