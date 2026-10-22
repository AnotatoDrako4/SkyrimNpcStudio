using System.Diagnostics;
using Microsoft.Data.Sqlite;
using StreamJsonRpc;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.Validation;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;

namespace SkyrimNpcStudio.Worker.Rpc;

/// <summary>
/// JSON-RPC surface of the worker. Every method takes a single DTO (params as a JSON object)
/// and maps expected failures to protocol error codes; unexpected ones become -32000.
/// </summary>
internal sealed class WorkerRpcTarget(WorkerState state, Action requestShutdown, Action<string, object> notify)
{
    private static readonly string MutagenVersion =
        typeof(Mutagen.Bethesda.Skyrim.SkyrimMod).Assembly.GetName().Version?.ToString(3) ?? "unknown";

    [JsonRpcMethod(Methods.Handshake)]
    public HandshakeResponse Handshake(HandshakeRequest request)
    {
        if (request.ProtocolMajor != Protocol.Major)
        {
            throw Fail(ErrorCodes.ProtocolMismatch,
                $"Protocol {request.ProtocolMajor}.x is not supported; this worker speaks {Protocol.Major}.{Protocol.Minor}.");
        }

        Log.Info($"handshake from {request.ClientName} {request.ClientVersion} (protocol {request.ProtocolMajor}.{request.ProtocolMinor})");
        return new HandshakeResponse
        {
            WorkerVersion = ModPackager.WorkerVersion,
            ProtocolMajor = Protocol.Major,
            ProtocolMinor = Protocol.Minor,
            MutagenVersion = MutagenVersion,
            ProcessId = Environment.ProcessId,
            Capabilities = ["catalog.npc", "catalog.armor", "catalog.getAppearance", "outfit.validate", "export.outfitPatch", "catalog.headParts", "face.recipe", "export.faceOverhaul", "export.completeOverhaul", Methods.CreationValidateNpc, Methods.CreationExportNpc, Methods.CreationSearchRecords,
                Methods.CreationAnalyzeVoice, Methods.CreationGetBehaviorCatalog, Methods.CreationApplyArchetype, Methods.CreationUndoArchetype, Methods.CreationValidateBehavior],
        };
    }

    [JsonRpcMethod(Methods.Ping)]
    public PingResponse Ping() => new() { UnixTimeMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() };

    [JsonRpcMethod(Methods.Shutdown)]
    public bool Shutdown()
    {
        Log.Info("shutdown requested by client");
        requestShutdown();
        return true;
    }

    [JsonRpcMethod(Methods.CatalogIndex)]
    public Task<IndexResponse> Index(IndexRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() =>
        {
            Log.Info($"indexing {request.Plugins.Count} plugins for profile {request.ProfileKey}");
            var response = state.Index(request, p => notify(Methods.IndexProgress, p), ct);
            Log.Info($"indexed: {response.NpcCount} NPC, {response.ArmorCount} ARMO, {response.OutfitCount} OTFT; parsed {response.ParsedPlugins}, cached {response.CachedPlugins}, {response.ElapsedMs} ms");
            return response;
        }), ct);

    [JsonRpcMethod(Methods.CatalogStatus)]
    public CatalogStatusResponse Status() => state.Status();

    [JsonRpcMethod(Methods.ListPlugins)]
    public ListPluginsResponse ListPlugins() => Guard(() => state.Read(c => c.Queries.ListPlugins()));

    [JsonRpcMethod(Methods.SearchNpcs)]
    public NpcSearchResponse SearchNpcs(NpcSearchRequest request) => Guard(() => state.Read(c => c.Queries.SearchNpcs(request)));

    [JsonRpcMethod(Methods.GetNpc)]
    public NpcDetails GetNpc(NpcDetailsRequest request) => Guard(() => state.Read(c =>
        c.Queries.GetNpc(request.FormKey) ?? throw new WorkerException(ErrorCodes.RecordNotFound, $"NPC '{request.FormKey}' is not in the active load order.")));

    [JsonRpcMethod(Methods.SearchArmors)]
    public ArmorSearchResponse SearchArmors(ArmorSearchRequest request) => Guard(() => state.Read(c => c.Queries.SearchArmors(request)));

    [JsonRpcMethod(Methods.GetAppearance)]
    public AppearanceResponse GetAppearance(AppearanceRequest request) =>
        Guard(() => state.Read(c => AppearanceQueries.Get(c.Session, c.Queries, request)));

    [JsonRpcMethod(Methods.SearchHeadParts)]
    public SearchHeadPartsResponse SearchHeadParts(SearchHeadPartsRequest request) =>
        Guard(() => state.Read(c => string.IsNullOrWhiteSpace(request.NpcFormKey)
            ? c.Queries.SearchHeadParts(request)
            : HeadPartQueries.ForNpc(c.Session, c.Queries, request)));

    [JsonRpcMethod(Methods.GetFaceRecipe)]
    public FaceRecipeResponse GetFaceRecipe(FaceRecipeRequest request) =>
        Guard(() => state.Read(c => FaceRecipeQueries.Get(c.Session, c.Queries, request)));

    [JsonRpcMethod(Methods.ValidateOutfit)]
    public OutfitValidateResponse ValidateOutfit(OutfitValidateRequest request) =>
        Guard(() => state.Read(c => OutfitValidator.Validate(c.Session, c.Queries, request)));

    [JsonRpcMethod(Methods.ExportOutfitPatch)]
    public Task<ExportResponse> ExportOutfitPatch(ExportOutfitPatchRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() =>
        {
            Log.Info($"exporting outfit patch {request.PluginName} to {request.OutputDirectory}");
            var response = state.Export(c => ModPackager.ExportOutfitPatch(c.Session, c.Queries, request, ct));
            Log.Info($"exported {response.PluginPath} (sha256 {response.PluginSha256[..12]}…), masters: {string.Join(", ", response.Masters)}");
            return response;
        }), ct);

    [JsonRpcMethod(Methods.ExportFacePatch)]
    public Task<ExportResponse> ExportFacePatch(ExportFaceOverhaulRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() =>
        {
            Log.Info($"exporting face overhaul {request.PluginName} to {request.OutputDirectory}");
            var response = state.Export(c => ModPackager.ExportFaceOverhaul(c.Session, c.Queries, request, ct));
            Log.Info($"exported {response.PluginPath} (sha256 {response.PluginSha256[..12]}…), masters: {string.Join(", ", response.Masters)}");
            return response;
        }), ct);

    [JsonRpcMethod(Methods.ExportCompleteNpc)]
    public Task<ExportResponse> ExportCompleteNpc(ExportCompleteOverhaulRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() =>
        {
            Log.Info($"exporting complete overhaul {request.PluginName} to {request.OutputDirectory}");
            var response = state.Export(c => ModPackager.ExportCompleteOverhaul(c.Session, c.Queries, request, ct));
            Log.Info($"exported {response.PluginPath} (sha256 {response.PluginSha256[..12]}…), masters: {string.Join(", ", response.Masters)}");
            return response;
        }), ct);

    [JsonRpcMethod(Methods.CreationValidateNpc)]
    public Task<CreationValidateResponse> ValidateNativeNpc(CreationValidateRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() => state.Read(c => NativeNpcBuilder.Validate(c.Session, c.Queries, request))), ct);

    [JsonRpcMethod(Methods.CreationExportNpc)]
    public Task<CreationExportResponse> ExportNativeNpc(CreationExportRequest request, CancellationToken ct) =>
        Task.Run(() => Guard(() => state.Export(c => NativeNpcBuilder.Export(c.Session, c.Queries, request, ct))), ct);

    [JsonRpcMethod(Methods.CreationSearchRecords)]
    public IReadOnlyList<CreationRecord> SearchCreationRecords(CreationSearchRequest request) =>
        Guard(() => state.Read(c => CreationRecordQueries.Search(c.Session, request)));

    [JsonRpcMethod(Methods.CreationAnalyzeVoice)]
    public CreationAnalyzeVoiceResponse AnalyzeVoice(CreationAnalyzeVoiceRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleAnalyzeVoice(request));

    [JsonRpcMethod(Methods.CreationGetBehaviorCatalog)]
    public CreationBehaviorCatalogResponse GetBehaviorCatalog(CreationBehaviorCatalogRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleGetCatalog(request));

    [JsonRpcMethod(Methods.CreationApplyArchetype)]
    public CreationApplyArchetypeResponse ApplyArchetype(CreationApplyArchetypeRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleApplyArchetype(request));

    [JsonRpcMethod(Methods.CreationUndoArchetype)]
    public CreationUndoArchetypeResponse UndoArchetype(CreationUndoArchetypeRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleUndoArchetype(request));

    [JsonRpcMethod(Methods.CreationValidateBehavior)]
    public CreationValidateBehaviorResponse ValidateBehavior(CreationValidateBehaviorRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleValidateBehavior(request));

    internal static T Guard<T>(Func<T> body)
    {
        try
        {
            return body();
        }
        catch (WorkerException ex)
        {
            Log.Warn($"{ex.Code}: {ex.Message}");
            throw Fail(ex.Code, ex.Message, ex.Issues);
        }
        catch (ArgumentException ex)
        {
            throw Fail(ErrorCodes.InvalidArgument, ex.Message);
        }
        catch (OperationCanceledException)
        {
            throw; // StreamJsonRpc reports -32800 RequestCanceled.
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or SqliteException)
        {
            Log.Error(ex.ToString());
            throw Fail(ErrorCodes.IoError, ex.Message);
        }
        catch (Exception ex)
        {
            // A worker bug: StreamJsonRpc answers -32000 with the message alone, so the type and stack trace
            // only reach the studio's console through this line (audit v2 F-18).
            Log.Error(ex.ToString());
            throw;
        }
    }

    private static LocalRpcException Fail(int code, string message, IReadOnlyList<Issue>? issues = null) =>
        new(message) { ErrorCode = code, ErrorData = issues is { Count: > 0 } ? new { issues } : null };
}

internal static class Log
{
    // stderr is captured by the studio and shown in its console panel; stdout stays unused.
    public static void Info(string message) => Write("INFO", message);

    public static void Warn(string message) => Write("WARN", message);

    public static void Error(string message) => Write("ERROR", message);

    private static void Write(string level, string message) =>
        Console.Error.WriteLine($"{DateTime.Now:HH:mm:ss.fff} [{level}] [worker:{Environment.ProcessId}] {message}");

    [Conditional("DEBUG")]
    public static void Debug(string message) => Write("DEBUG", message);
}
