using System.Security.Cryptography;
using System.Text;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker.Behavior;

/// <summary>
/// The five behavior endpoints of Character Forge (creation.analyzeVoice, getBehaviorCatalog, applyArchetype, undoArchetype,
/// validateBehavior) answered from the profile this worker has INDEXED: the laboratory's engines run over the very overlays and link
/// cache of the catalog's LoadOrderSession, so winners, overrides, masters and enabled/disabled plugins are the studio's.
/// Nothing here opens, builds or falls back to a fixture folder; a worker that has not indexed answers NotIndexed (from
/// WorkerState.Read) and a request that names a fixture directory is refused.
/// </summary>
internal static class BehaviorRpc
{
    public static CreationAnalyzeVoiceResponse AnalyzeVoice(Catalog catalog, CreationAnalyzeVoiceRequest request) =>
        Run(catalog, request.FixtureDirectory, request.ProfileName, ctx => FollowerBehaviorRpcDispatcher.HandleAnalyzeVoice(ctx, request));

    public static CreationBehaviorCatalogResponse GetCatalog(Catalog catalog, CreationBehaviorCatalogRequest request) =>
        Run(catalog, request.FixtureDirectory, request.ProfileName, ctx => FollowerBehaviorRpcDispatcher.HandleGetCatalog(ctx, request));

    public static CreationApplyArchetypeResponse ApplyArchetype(Catalog catalog, CreationApplyArchetypeRequest request) =>
        Run(catalog, request.FixtureDirectory, request.ProfileName, ctx => FollowerBehaviorRpcDispatcher.HandleApplyArchetype(ctx, request));

    public static CreationValidateBehaviorResponse ValidateBehavior(Catalog catalog, CreationValidateBehaviorRequest request) =>
        Run(catalog, request.FixtureDirectory, request.ProfileName, ctx => FollowerBehaviorRpcDispatcher.HandleValidateBehavior(ctx, request));

    /// <summary>Undo needs no load order: it checks the restore token against the hash the apply reported.</summary>
    public static CreationUndoArchetypeResponse UndoArchetype(CreationUndoArchetypeRequest request) =>
        Translate(() => FollowerBehaviorRpcDispatcher.HandleUndoArchetype(request));

    private static T Run<T>(Catalog catalog, string? fixtureDirectory, string? expectedProfile, Func<PluginContext, T> body)
    {
        if (!string.IsNullOrWhiteSpace(fixtureDirectory))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument,
                "fixtureDirectory is not accepted: the worker answers behavior requests from the profile it has indexed.");
        }

        // A request made for another profile than the one indexed (the studio switched meanwhile) is refused, not answered with this
        // profile's evidence as if it were the other's.
        if (!string.IsNullOrWhiteSpace(expectedProfile) && !string.Equals(expectedProfile, catalog.ProfileKey, StringComparison.Ordinal))
        {
            throw new WorkerException(ErrorCodes.InvalidLoadOrder,
                $"The request was made for profile '{expectedProfile}' but the worker has indexed '{catalog.ProfileKey}'.");
        }

        return Translate(() => body(catalog.BehaviorContext));
    }

    private static T Translate<T>(Func<T> body)
    {
        try
        {
            return body();
        }
        catch (LabUsageException ex)
        {
            var code = ex.Code switch
            {
                "ArchetypeNotFound" or "FixtureNotFound" => ErrorCodes.RecordNotFound,
                "UndoIntegrityMismatch" => ErrorCodes.ValidationFailed,
                _ => ErrorCodes.InvalidArgument,
            };
            throw new WorkerException(code, ex.Message);
        }
    }

    /// <summary>
    /// The lab's view of the catalog's session. The fingerprint identifies THIS load order of THIS profile (profile key, each plugin's
    /// position, name, size and last write, and the disabled names), so a different profile or a changed plugin gives another one and
    /// the studio can tell a stale answer. It is metadata, not a content hash: hashing hundreds of MB per request would defeat the point.
    /// </summary>
    internal static PluginContext BuildContext(Catalog catalog)
    {
        var session = catalog.Session;
        var enabled = session.Plugins.Select(p => new LoadedPluginInfo
        {
            LoadIndex = p.LoadIndex,
            ModKey = p.ModKey,
            FilePath = p.Path,
            SizeBytes = p.Size,
            Sha256 = $"meta:{p.Size}:{p.LastWriteUtc.Ticks}",
            Style = p.Style,
            IsMasterFlagged = p.IsMasterFlagged,
            Masters = p.Masters,
            Mod = p.Mod,
        }).ToArray();

        var text = new StringBuilder("profile=").Append(catalog.ProfileKey).Append(';');
        foreach (var p in enabled)
        {
            text.Append(p.LoadIndex).Append(':').Append(p.ModKey.FileName.String).Append(':').Append(p.SizeBytes).Append(':').Append(p.Sha256).Append(';');
        }

        foreach (var name in catalog.DisabledPlugins.OrderBy(n => n, StringComparer.OrdinalIgnoreCase))
        {
            text.Append("disabled:").Append(name).Append(';');
        }

        var fingerprint = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text.ToString())));
        return PluginContext.FromLoaded(catalog.ProfileKey, enabled, catalog.DisabledPlugins, [], session.LinkCache, session.OfficialLinkCache, fingerprint);
    }
}
