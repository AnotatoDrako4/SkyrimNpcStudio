#pragma once

// Wire protocol shared with services/BethesdaWorker/Contracts/Protocol.cs. Keep both in sync.

namespace sns::ipc::protocol {

inline constexpr int kMajor = 1;
inline constexpr int kMinor = 1;

namespace method {
inline constexpr const char* kHandshake = "system.handshake";
inline constexpr const char* kPing = "system.ping";
inline constexpr const char* kShutdown = "system.shutdown";
inline constexpr const char* kCatalogIndex = "catalog.index";
inline constexpr const char* kCatalogStatus = "catalog.status";
inline constexpr const char* kListPlugins = "catalog.listPlugins";
inline constexpr const char* kSearchNpcs = "catalog.searchNpcs";
inline constexpr const char* kGetNpc = "catalog.getNpc";
inline constexpr const char* kGetAppearance = "catalog.getAppearance";
inline constexpr const char* kSearchArmors = "catalog.searchArmors";
inline constexpr const char* kValidateOutfit = "outfit.validate";
inline constexpr const char* kExportOutfitPatch = "export.outfitPatch";
// Server -> client notification.
inline constexpr const char* kIndexProgress = "catalog.progress";
// Client -> server notification understood by StreamJsonRpc (same as LSP).
inline constexpr const char* kCancelRequest = "$/cancelRequest";
}  // namespace method

namespace error {
// JSON-RPC 2.0 standard codes.
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kRequestCancelled = -32800;
// Application codes (outside the -32768..-32000 range used by JSON-RPC libraries).
inline constexpr int kProtocolMismatch = 1001;
inline constexpr int kNotIndexed = 1002;
inline constexpr int kRecordNotFound = 1003;
inline constexpr int kValidationFailed = 1004;
inline constexpr int kInvalidLoadOrder = 1005;
inline constexpr int kIoError = 1006;
inline constexpr int kBusy = 1007;
inline constexpr int kOutputExists = 1008;
inline constexpr int kInvalidArgument = 1009;
// Client-side only, never sent on the wire.
inline constexpr int kClientTimeout = -1;
inline constexpr int kClientDisconnected = -2;
}  // namespace error

}  // namespace sns::ipc::protocol
