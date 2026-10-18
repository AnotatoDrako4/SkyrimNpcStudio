using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Noggog;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>
/// Deterministic, process-independent fingerprint of a record field value.
/// Object.GetHashCode is randomized per process in .NET, so it cannot be cached in SQLite;
/// instead values are canonicalized to text (public getter-interface properties, sorted by
/// name) and hashed with SHA-256.
/// </summary>
internal static class Fingerprint
{
    private const int MaxDepth = 6;

    // Plugin names are case-insensitive in the game (and in Mutagen's ModKey): an override that lists its master
    // as "ccbgssse025-advdsgs.esm" links the same records as the master's own "ccBGSSSE025-AdvDSGS.esm".
    private static void AppendFormKey(StringBuilder sb, FormKey key) =>
        sb.Append(key.ID.ToString("X6", CultureInfo.InvariantCulture)).Append(':').Append(key.ModKey.FileName.String.ToUpperInvariant());

    public static string Of(object? value)
    {
        var sb = new StringBuilder();
        Write(sb, value, 0);
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(sb.ToString()));
        return Convert.ToHexString(hash, 0, 8);
    }

    private static void Write(StringBuilder sb, object? value, int depth)
    {
        switch (value)
        {
            case null:
                sb.Append('~');
                return;
            case string s:
                sb.Append('"').Append(s.Replace("\"", "\\\"", StringComparison.Ordinal)).Append('"');
                return;
            case bool b:
                sb.Append(b ? 'T' : 'F');
                return;
            case float f:
                sb.Append(f.ToString("R", CultureInfo.InvariantCulture));
                return;
            case double d:
                sb.Append(d.ToString("R", CultureInfo.InvariantCulture));
                return;
            case Enum e:
                sb.Append(Convert.ToInt64(e, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture));
                return;
            case IConvertible c when value.GetType().IsPrimitive:
                sb.Append(c.ToString(CultureInfo.InvariantCulture));
                return;
            case FormKey fk:
                AppendFormKey(sb, fk);
                return;
            // Not a Mutagen getter: without this every color (QNAM, TINC) had the same fingerprint.
            case System.Drawing.Color color:
                sb.Append('#').Append(color.ToArgb().ToString("X8", CultureInfo.InvariantCulture));
                return;
            case IFormLinkGetter link:
                if (link.FormKeyNullable is { } key)
                    AppendFormKey(sb, key);
                else
                    sb.Append("null");
                return;
            case ReadOnlyMemorySlice<byte> bytes:
                sb.Append(Convert.ToHexString(bytes));
                return;
            case MemorySlice<byte> bytes:
                sb.Append(Convert.ToHexString(bytes));
                return;
            case IEnumerable seq:
                sb.Append('[');
                foreach (var item in seq)
                {
                    Write(sb, item, depth + 1);
                    sb.Append(',');
                }

                sb.Append(']');
                return;
        }

        if (depth >= MaxDepth)
        {
            sb.Append('…');
            return;
        }

        sb.Append('{');
        foreach (var property in GetterProperties(value.GetType()))
        {
            object? propertyValue;
            try
            {
                propertyValue = property.GetValue(value);
            }
            catch (TargetInvocationException)
            {
                propertyValue = "<unreadable>";
            }

            sb.Append(property.Name).Append('=');
            Write(sb, propertyValue, depth + 1);
            sb.Append(';');
        }

        sb.Append('}');
    }

    private static readonly Dictionary<Type, PropertyInfo[]> PropertyCache = new();

    /// <summary>
    /// Properties declared on the Mutagen "…Getter" interfaces the runtime type implements.
    /// Using the interfaces (not the runtime type) makes binary-overlay objects and fully
    /// materialized objects canonicalize identically.
    /// </summary>
    private static PropertyInfo[] GetterProperties(Type type)
    {
        lock (PropertyCache)
        {
            if (PropertyCache.TryGetValue(type, out var cached))
            {
                return cached;
            }

            var properties = type.GetInterfaces()
                .Where(i => i.Namespace?.StartsWith("Mutagen.Bethesda", StringComparison.Ordinal) == true
                            && i.Name.EndsWith("Getter", StringComparison.Ordinal))
                .SelectMany(i => i.GetProperties())
                .Where(p => p.GetIndexParameters().Length == 0
                            && p.Name is not ("Registration" or "StaticRegistration" or "BinaryWriteTranslator"
                                or "FormKey" or "VersionControl" or "FormVersion" or "Version2" or "EditorID"))
                .GroupBy(p => p.Name, StringComparer.Ordinal)
                .Select(g => g.First())
                .OrderBy(p => p.Name, StringComparer.Ordinal)
                .ToArray();
            PropertyCache[type] = properties;
            return properties;
        }
    }
}
