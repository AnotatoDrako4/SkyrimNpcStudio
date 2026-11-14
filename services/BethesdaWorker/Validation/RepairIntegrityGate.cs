using System.Security.Cryptography;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.Validation;

/// <summary>Additional boundary for face repairs, after the builder's field-mask readback, before publication.</summary>
internal static class RepairIntegrityGate
{
    internal static void Check(LoadOrderSession session, BuiltPatch built, string directory, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        using var mod = SkyrimMod.CreateFromBinaryOverlay(
            new ModPath(ModKey.FromFileName(Path.GetFileName(built.PluginPath)), built.PluginPath), SkyrimRelease.SkyrimSE,
            new BinaryReadParameters { MasterFlagsLookup = session.MasterStyles });
        var records = mod.EnumerateMajorRecords().ToArray();
        var actual = records.Select(r => r.FormKey.ToString()).ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (actual.Count != records.Length || !actual.SetEquals(built.Records.Select(r => r.FormKey)))
            Fail("Unexpected or duplicate record in repair package.");
        foreach (var master in mod.ModHeader.MasterReferences)
            if (session.Find(master.Master) is null) Fail("Unresolved master: " + master.Master);
        foreach (var record in records)
        {
            ct.ThrowIfCancellationRequested();
            foreach (var link in record.EnumerateFormLinks())
            {
                if (link.IsNull) continue;
                if (link.FormKey.ModKey == mod.ModKey)
                {
                    if (!actual.Contains(link.FormKey.ToString())) Fail("Missing local reference: " + link.FormKey);
                }
                else if (!session.LinkCache.TryResolve(link.FormKey, link.Type, out _)) Fail("Unresolved reference: " + link.FormKey);
            }
        }
        // Recursively verify extra HeadParts too; the NPC's direct links alone do not prove they resolve.
        var seen = new HashSet<FormKey>();
        void Part(FormKey key)
        {
            ct.ThrowIfCancellationRequested();
            if (!seen.Add(key)) return;
            if (!session.LinkCache.TryResolve<IHeadPartGetter>(key, out var part)) { Fail("Unresolved HeadPart: " + key); return; }
            foreach (var link in part.EnumerateFormLinks())
                if (!link.IsNull && !session.LinkCache.TryResolve(link.FormKey, link.Type, out _)) Fail("Unresolved HeadPart dependency: " + link.FormKey);
            foreach (var child in part.ExtraParts) Part(child.FormKey);
        }
        foreach (var npc in mod.Npcs) foreach (var part in npc.HeadParts) Part(part.FormKey);
        var allowed = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFileName(built.PluginPath) };
        foreach (var asset in built.Generated ?? [])
        {
            ct.ThrowIfCancellationRequested();
            var paths = CatalogQueries.FaceGenFor(FormKey.Factory(asset.Npc));
            var name = asset.DataPath.Replace('\\', '/');
            if (!name.Equals(paths.GeometryPath.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase)
                && !name.Equals(paths.TintPath.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase)) Fail("Unexpected FaceGen identity: " + name);
            if (!actual.Contains(asset.Npc) || !allowed.Add(name)) Fail("Duplicate or unrelated FaceGen: " + name);
            using var stream = File.OpenRead(Path.Combine(directory, name));
            if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(asset.Sha256, StringComparison.OrdinalIgnoreCase)) Fail("Staged FaceGen changed: " + name);
        }
        CheckFiles(directory, allowed, ct);
    }

    internal static void CheckFiles(string directory, IReadOnlySet<string> allowed, CancellationToken ct)
    {
        var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void Visit(string dir)
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(dir))
            {
                ct.ThrowIfCancellationRequested();
                var attributes = File.GetAttributes(entry);
                if ((attributes & FileAttributes.ReparsePoint) != 0) Fail("Links are not permitted in repair staging.");
                if ((attributes & FileAttributes.Directory) != 0) { Visit(entry); continue; }
                var relative = Path.GetRelativePath(directory, entry).Replace('\\', '/');
                if (!found.Add(relative) || !allowed.Contains(relative)) Fail("Unexpected file: " + relative);
            }
        }
        Visit(directory);
        if (!found.SetEquals(allowed)) Fail("Repair package is missing allowed files.");
    }
    private static void Fail(string message) => throw new WorkerException(ErrorCodes.ValidationFailed, message);
}
