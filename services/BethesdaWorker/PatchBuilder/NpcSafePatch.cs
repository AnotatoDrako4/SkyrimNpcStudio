using System.Collections;
using System.Reflection;
using System.Text.RegularExpressions;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>
/// NPC Safe Patch Engine: the guarantee that an export only changes what the user asked for.
///
/// The override is a copy of the winning record (GetOrAddAsOverride), so everything else is the same
/// by construction; this class proves it after the fact. The written plugin is re-read and EVERY
/// NPC_ property (race, class, voice, stats, AI packages, inventory, factions, spells, ...) is compared
/// with the winning record through Mutagen's generated equality mask, not through a hand-written list.
/// A difference outside the allowed set of the requested operation fails the export
/// (UnauthorizedNpcChange); the differences that are allowed come back as a readable semantic diff
/// (field, original, exported, reason, dependencies) for the manifest, the README and the UI.
/// </summary>
internal static partial class NpcSafePatch
{
    /// <summary>Everything the face editor writes into NPC_ (Mode B / C with a complete face).</summary>
    public static readonly IReadOnlySet<string> FaceFields = new HashSet<string>(StringComparer.Ordinal)
    {
        "HairColor", "FaceMorph", "FaceParts", "HeadParts", "HeadTexture", "Weight", "Height", "TextureLighting", "TintLayers",
    };

    /// <summary>
    /// Record bookkeeping an override legitimately rewrites without changing the NPC: version stamps and
    /// record flags (compression). Nothing else is ignored.
    /// </summary>
    private static readonly HashSet<string> Bookkeeping = new(StringComparer.Ordinal)
    {
        "VersionControl", "FormVersion", "Version2", "SkyrimMajorRecordFlags", "MajorRecordFlagsRaw", "IsCompressed",
    };

    private static readonly Dictionary<string, (string Subrecord, string Reason)> Known = new(StringComparer.Ordinal)
    {
        ["HairColor"] = ("HCLF", "Color de cabello pedido por el usuario"),
        ["FaceMorph"] = ("NAM9", "Sliders del rostro editados"),
        ["FaceParts"] = ("NAMA", "Presets de nariz, ojos y boca editados"),
        ["HeadParts"] = ("PNAM", "Piezas de cabeza (Head Parts) editadas"),
        ["HeadTexture"] = ("FTST", "Textura facial editada"),
        ["Weight"] = ("NAM7", "Peso del NPC editado (afecta al FaceGen)"),
        ["Height"] = ("NAM6", "Altura del NPC editada"),
        ["TextureLighting"] = ("QNAM", "Tono de piel editado"),
        ["TintLayers"] = ("TINI/TINC/TINV", "Capas de tinte facial editadas"),
        ["DefaultOutfit"] = ("DOFT", "Atuendo por defecto asignado"),
        ["SleepingOutfit"] = ("SOFT", "Atuendo para dormir asignado"),
    };

    /// <summary>"HCLF"/"NAM6" of FaceAssignment.OnlyFields to the property each one writes.</summary>
    public static IReadOnlySet<string> AllowedForFace(FaceAssignment face)
    {
        if (face.OnlyFields is not { } only)
        {
            // The fields the user actually changed, when the studio says so; else the whole face.
            return face.RequestedFields is { } requested
                ? new HashSet<string>(requested.Where(FaceFields.Contains), StringComparer.Ordinal)
                : FaceFields;
        }

        // Subrecord -> property for every face field (HCLF and NAM6 for a minimal change; the face fields
        // of FacePatchBuilder.SourceFaceFields for a face copied from another version).
        var allowed = new HashSet<string>(StringComparer.Ordinal);
        foreach (var field in only)
        {
            if (field == "TINT") allowed.Add("TintLayers");
            else if (Known.FirstOrDefault(k => k.Value.Subrecord == field).Key is { } property && FaceFields.Contains(property)) allowed.Add(property);
        }

        return allowed;
    }

    public static IReadOnlySet<string> AllowedForOutfit(bool alsoSleeping) =>
        alsoSleeping
            ? new HashSet<string>(StringComparer.Ordinal) { "DefaultOutfit", "SleepingOutfit" }
            : new HashSet<string>(StringComparer.Ordinal) { "DefaultOutfit" };

    /// <summary>Union of the allowed sets of several operations on the same NPC.</summary>
    public static IReadOnlySet<string> Union(params IReadOnlySet<string>?[] sets)
    {
        var all = new HashSet<string>(StringComparer.Ordinal);
        foreach (var set in sets)
        {
            if (set is not null) all.UnionWith(set);
        }

        return all;
    }

    /// <summary>
    /// Checks every NPC override of the written plugin against its winning record. Throws (nothing is
    /// installed) when any of them differs outside the allowed fields of its operation.
    /// </summary>
    public static IReadOnlyList<NpcSemanticDiff> CheckAll(
        LoadOrderSession session, ModKey patchKey, IEnumerable<INpcGetter> written,
        IReadOnlyDictionary<FormKey, IReadOnlySet<string>> allowedByNpc, string operation)
    {
        var violations = new List<Issue>();
        var diffs = new List<NpcSemanticDiff>();
        foreach (var npc in written)
        {
            if (!session.LinkCache.TryResolve<INpcGetter>(npc.FormKey, out var original))
            {
                throw new WorkerException(ErrorCodes.ValidationFailed, $"Verification failed: winning record of {npc.FormKey} not found.");
            }

            var allowed = allowedByNpc.TryGetValue(npc.FormKey, out var set) ? set : new HashSet<string>();
            diffs.Add(Check(original, npc, patchKey, allowed, operation, violations));
        }

        if (violations.Count > 0)
        {
            throw new WorkerException(ErrorCodes.ValidationFailed,
                "Verification failed: the export changes NPC fields it was not asked to change; nothing was written.", violations);
        }

        return diffs;
    }

    /// <summary>
    /// Compares the override that was written with the winning record. Differences outside
    /// <paramref name="allowed"/> are appended to <paramref name="violations"/> as errors.
    /// </summary>
    public static NpcSemanticDiff Check(
        INpcGetter original, INpcGetter exported, ModKey patchKey, IReadOnlySet<string> allowed, string operation,
        List<Issue> violations)
    {
        var changes = new List<NpcFieldChange>();
        var subject = original.FormKey.ToString();
        foreach (var name in DifferingProperties(original, exported))
        {
            var known = Known.TryGetValue(name, out var info);
            var subrecord = known ? info.Subrecord : "";
            if (!allowed.Contains(name))
            {
                violations.Add(Issue.Error("UnauthorizedNpcChange",
                    $"The export changes {name}{(known ? $" ({subrecord})" : "")} of {original.EditorID ?? subject}, which {operation} does not ask for.",
                    subject));
                continue;
            }

            var originalLinks = new List<FormKey>();
            var exportedLinks = new List<FormKey>();
            var before = Render(Read(original, name), originalLinks);
            var after = Render(Read(exported, name), exportedLinks);
            changes.Add(new NpcFieldChange
            {
                Field = name,
                Subrecord = subrecord,
                Original = before,
                Exported = after,
                Reason = known ? info.Reason : operation,
                Dependencies = exportedLinks.Except(originalLinks).Select(k => k.ModKey == patchKey ? $"{k} (nuevo en este mod)" : k.ToString()).ToArray(),
            });
        }

        return new NpcSemanticDiff { Npc = subject, NpcEditorId = original.EditorID, Changes = changes };
    }

    /// <summary>Names of the NPC_ properties whose value differs, from Mutagen's generated equality mask.</summary>
    internal static IReadOnlyList<string> DifferingProperties(INpcGetter original, INpcGetter exported)
    {
        var mask = exported.GetEqualsMask(original);
        var maskType = mask.GetType();
        var members = maskType.GetFields(BindingFlags.Public | BindingFlags.Instance)
            .Select(f => (f.Name, Value: f.GetValue(mask)))
            .Concat(maskType.GetProperties(BindingFlags.Public | BindingFlags.Instance)
                .Where(p => p.GetIndexParameters().Length == 0)
                .Select(p => (p.Name, Value: p.GetValue(mask))));
        // Mutagen masks keep one public member per record field. A mask with none would make every
        // export look clean, so that is an error rather than "no differences".
        var any = false;
        var differing = new List<string>();
        foreach (var (name, value) in members)
        {
            any = true;
            if (Bookkeeping.Contains(name)) continue;
            // null: the mask reported nothing for the member, so it is checked again on the values
            // themselves; a verdict that cannot be read counts as a difference (never assumed equal).
            var equal = MaskVerdict(value) ?? Fingerprint(Read(original, name)) == Fingerprint(Read(exported, name));
            if (!equal) differing.Add(name);
        }

        if (!any) throw new InvalidOperationException("Mutagen's equality mask for NPC_ exposes no members; the safe patch check cannot run.");
        // A localized master (Skyrim.esm) stores a name as a string-table reference; a non-localized patch
        // stores the text the session resolved in the game's language. Same text, different storage:
        // what the game shows does not change, so it is not a change.
        return differing.Where(name => !SameResolvedText(Read(original, name), Read(exported, name))).ToList();
    }

    private static bool SameResolvedText(object? a, object? b) =>
        a is Mutagen.Bethesda.Strings.ITranslatedStringGetter ta && b is Mutagen.Bethesda.Strings.ITranslatedStringGetter tb
        && string.Equals(ta.String, tb.String, StringComparison.Ordinal);

    /// <summary>
    /// Verdict of one mask member: a bool, or a MaskItem whose Overall is the bool. Null when the mask
    /// reported nothing (the caller compares the values); false for any shape this code cannot read,
    /// so an unknown mask type is a difference, never a silent "equal".
    /// </summary>
    internal static bool? MaskVerdict(object? member)
    {
        switch (member)
        {
            case null:
                return null;
            case bool equal:
                return equal;
        }

        var type = member.GetType();
        var overall = type.GetProperty("Overall")?.GetValue(member) ?? type.GetField("Overall")?.GetValue(member);
        return overall is bool b ? b : false;
    }

    /// <summary>Complete text of a value (links, lists and Mutagen objects), for comparing values the mask did not report.</summary>
    internal static string Fingerprint(object? value)
    {
        return value switch
        {
            null => "",
            // A link names the same record whatever letter case its master is written with (Mutagen's FormKey
            // equality ignores it; ToString keeps it).
            IFormLinkGetter link => link.IsNull ? "" : $"{link.FormKey.ID:X6}:{link.FormKey.ModKey.FileName.String.ToUpperInvariant()}",
            string text => text,
            IEnumerable items and not string => string.Join("|", items.Cast<object?>().Select(Fingerprint)),
            _ => Whitespace().Replace(value.ToString() ?? "", " ").Trim(),
        };
    }

    private static object? Read(INpcGetter record, string name)
    {
        foreach (var type in new[] { typeof(INpcGetter) }.Concat(typeof(INpcGetter).GetInterfaces()))
        {
            if (type.GetProperty(name) is { } property && property.GetIndexParameters().Length == 0)
            {
                return property.GetValue(record);
            }
        }

        return null;
    }

    private static string Render(object? value, List<FormKey> links)
    {
        switch (value)
        {
            case null:
                return "(ninguno)";
            case string text:
                return text;
            case IFormLinkGetter link:
                if (link.IsNull) return "(ninguno)";
                links.Add(link.FormKey);
                return link.FormKey.ToString();
            case IEnumerable items:
            {
                var parts = new List<string>();
                var count = 0;
                foreach (var item in items)
                {
                    if (count++ < 8) parts.Add(Render(item, links));
                    else Render(item, links);
                }

                return count == 0 ? "[]" : $"[{string.Join(", ", parts)}{(count > 8 ? $", … (+{count - 8})" : "")}]";
            }
        }

        var flat = Whitespace().Replace(value.ToString() ?? "", " ").Trim();
        return flat.Length <= 300 ? flat : flat[..300] + "…";
    }

    [GeneratedRegex(@"\s+")]
    private static partial Regex Whitespace();
}
