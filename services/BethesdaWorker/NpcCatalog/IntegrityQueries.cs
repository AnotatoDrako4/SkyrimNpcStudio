using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

internal static class IntegrityQueries
{
    public static IntegrityRecordResponse Get(LoadOrderSession session, CatalogQueries queries,
        IntegrityRecordRequest request, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var key))
            throw new WorkerException(ErrorCodes.InvalidArgument, "Invalid NPC FormKey.");
        var dependencies = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var unresolved = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var assets = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var chain = session.Plugins.Where(p => p.Mod.Npcs.RecordCache.ContainsKey(key)).Select(p => p.ModKey.FileName.String).ToArray();
        void Track(FormKey form)
        {
            dependencies.Add("record:" + form);
            dependencies.Add("plugin:" + form.ModKey.FileName.String);
            if (session.LinkCache.TryResolveSimpleContext<IMajorRecordGetter>(form, out var context))
                dependencies.Add("plugin:" + context.ModKey.FileName.String);
        }
        Track(key);
        foreach (var plugin in chain) dependencies.Add("plugin:" + plugin);
        if (!session.LinkCache.TryResolve<INpcGetter>(key, out var npc))
            return new(key.ToString(), key.ModKey.FileName.String, "", chain, "missing", [], [], dependencies.ToArray(), [key.ToString()], null);
        var effective = npc;
        var visited = new HashSet<FormKey> { key };
        while (effective.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits))
        {
            ct.ThrowIfCancellationRequested();
            var parent = effective.Template.FormKey;
            Track(parent);
            if (!visited.Add(parent) || !session.LinkCache.TryResolve<INpcGetter>(parent, out var template))
            {
                unresolved.Add("Traits:" + parent); break;
            }
            effective = template;
            foreach (var plugin in session.Plugins.Where(p => p.Mod.Npcs.RecordCache.ContainsKey(parent)))
                dependencies.Add("plugin:" + plugin.ModKey.FileName.String);
        }
        var fingerprints = RecordExtractor.Fingerprints(effective, null);
        var visual = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var field in RecordExtractor.VisualFields) visual[field] = fingerprints[field];
        visual["Template"] = RecordExtractor.Fingerprints(npc, null)["Template"];
        visual["TemplateFlags"] = RecordExtractor.Fingerprints(npc, null)["TemplateFlags"];
        var parts = new List<string>();
        object? recipeIdentity = null;
        void Links(IMajorRecordGetter record)
        {
            foreach (var link in record.EnumerateFormLinks())
            {
                if (link.IsNull) continue;
                Track(link.FormKey);
                if (!session.LinkCache.TryResolve(link.FormKey, link.Type, out _)) unresolved.Add(link.FormKey.ToString());
            }
        }
        // Only the visual dependency records are inspected; no catalogue rebuild.
        Links(effective);
        Track(effective.Race.FormKey);
        if (session.LinkCache.TryResolve<IRaceGetter>(effective.Race.FormKey, out var race)) Links(race);
        else unresolved.Add(effective.Race.FormKey.ToString());
        try
        {
            var recipe = FaceRecipeQueries.Get(session, queries, new FaceRecipeRequest { NpcFormKey = effective.FormKey.ToString() });
            assets.Add(recipe.FaceGenMeshPath); assets.Add(recipe.FaceGenTintPath);
            foreach (var part in recipe.HeadParts)
            {
                ct.ThrowIfCancellationRequested();
                parts.Add(part.FormKey);
                var partKey = FormKey.Factory(part.FormKey); Track(partKey);
                if (session.LinkCache.TryResolve<IHeadPartGetter>(partKey, out var head)) Links(head);
                if (part.Model.Length > 0) assets.Add(part.Model);
                foreach (var tri in part.Tris) if (tri.Path.Length > 0) assets.Add(tri.Path);
                foreach (var texture in part.TextureSet ?? []) if (texture.Length > 0) assets.Add(texture);
            }
            foreach (var texture in recipe.FaceTextureSet ?? []) if (texture.Length > 0) assets.Add(texture);
            if (recipe.FaceTextureSetFormKey is { } textureKey) Track(FormKey.Factory(textureKey));
            foreach (var mask in recipe.TintMasks) if (mask.MaskPath.Length > 0) assets.Add(mask.MaskPath);
            foreach (var issue in recipe.Issues.Where(i => i.Code.Contains("Unresolved", StringComparison.Ordinal))) unresolved.Add(issue.Message);
            recipeIdentity = new { recipe.Female, recipe.Height, recipe.Race, recipe.Values, recipe.FaceTextureSet,
                Parts = recipe.HeadParts.Select(p => new { p.FormKey, p.Type, p.Model, p.Tris, p.TextureSet, p.Extra, p.ValidForRace, p.ValidSex }), recipe.TintMasks };
        }
        catch (WorkerException ex) { unresolved.Add(ex.Message); }
        var signature = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new { visual, recipeIdentity }))));
        return new(key.ToString(), key.ModKey.FileName.String, chain.LastOrDefault() ?? "", chain, signature,
            parts, assets.ToArray(), dependencies.ToArray(), unresolved.ToArray(), effective.FormKey == key ? null : effective.FormKey.ToString());
    }
}
