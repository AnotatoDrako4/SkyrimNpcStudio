namespace SkyrimNpcStudio.Worker.Contracts;

/// <summary>
/// Wire protocol between the Qt studio (client) and this worker (server).
/// JSON-RPC 2.0 over a Windows named pipe, framed with LSP-style
/// "Content-Length: N\r\n\r\n" headers. Must stay in sync with
/// engines/ipc/include/sns/ipc/protocol.hpp.
/// </summary>
public static class Protocol
{
    /// <summary>Incompatible changes bump the major version; the handshake rejects a mismatch.</summary>
    public const int Major = 1;

    /// <summary>Additive changes (new optional fields, new methods) bump the minor version.</summary>
    public const int Minor = 2;
}

public static class Methods
{
    public const string Handshake = "system.handshake";
    public const string Ping = "system.ping";
    public const string Shutdown = "system.shutdown";

    public const string CatalogIndex = "catalog.index";
    public const string CatalogStatus = "catalog.status";
    public const string ListPlugins = "catalog.listPlugins";
    public const string SearchNpcs = "catalog.searchNpcs";
    public const string GetNpc = "catalog.getNpc";
    public const string GetAppearance = "catalog.getAppearance";
    public const string SearchArmors = "catalog.searchArmors";

    public const string ValidateOutfit = "outfit.validate";
    public const string ExportOutfitPatch = "export.outfitPatch";
    public const string SearchHeadParts = "catalog.searchHeadParts";
    public const string ExportFacePatch = "export.faceOverhaul";
    public const string ExportCompleteNpc = "export.completeOverhaul";
    public const string GetFaceRecipe = "face.getRecipe";

    /// <summary>Server → client notification emitted while indexing.</summary>
    public const string IndexProgress = "catalog.progress";
}

/// <summary>
/// Application error codes. They live OUTSIDE -32768..-32000: StreamJsonRpc already uses
/// -32000..-32004 of the "implementation-defined server error" range (e.g. -32003 means
/// "response serialization failure" to its client). -32800 (request cancelled) follows LSP.
/// </summary>
public static class ErrorCodes
{
    public const int ProtocolMismatch = 1001;
    public const int NotIndexed = 1002;
    public const int RecordNotFound = 1003;
    public const int ValidationFailed = 1004;
    public const int InvalidLoadOrder = 1005;
    public const int IoError = 1006;
    public const int Busy = 1007;
    public const int OutputExists = 1008;
    public const int InvalidArgument = 1009;
    public const int RequestCancelled = -32800;
}
