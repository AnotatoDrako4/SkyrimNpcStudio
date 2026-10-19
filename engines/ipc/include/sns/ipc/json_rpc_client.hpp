#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>

#include "sns/core/error.hpp"
#include "sns/ipc/transport.hpp"

namespace sns::ipc {

using Json = nlohmann::json;

// A JSON-RPC error returned by the worker (or synthesized by the client for timeouts/disconnects).
class RpcError : public Error {
 public:
  RpcError(int code, const std::string& message, Json data = nullptr);
  [[nodiscard]] int code() const noexcept { return code_; }
  [[nodiscard]] const Json& data() const noexcept { return data_; }

 private:
  int code_;
  Json data_;
};

struct PendingCall {
  std::int64_t id = 0;
  std::shared_future<Json> result;
};

// JSON-RPC 2.0 client: request ids, concurrent in-flight calls, timeouts, cancellation via
// "$/cancelRequest" and server notifications. Callbacks run on the internal reader thread.
class JsonRpcClient {
 public:
  using NotificationHandler = std::function<void(const std::string& method, const Json& params)>;
  using DisconnectHandler = std::function<void(const std::string& reason)>;

  explicit JsonRpcClient(std::unique_ptr<Transport> transport);
  ~JsonRpcClient();
  JsonRpcClient(const JsonRpcClient&) = delete;
  JsonRpcClient& operator=(const JsonRpcClient&) = delete;

  // Handlers must be set before start().
  void set_notification_handler(NotificationHandler handler);
  void set_disconnect_handler(DisconnectHandler handler);
  void start();

  // Sends a request; `params` must be an object, or null to omit it.
  PendingCall call_async(std::string_view method, Json params = nullptr);

  // Blocking call. Throws RpcError on server errors, timeout (kClientTimeout) or disconnect.
  // When `cancel` becomes true the request is cancelled on the server and RpcError(-32800) is thrown.
  Json call(std::string_view method, Json params, std::chrono::milliseconds timeout,
            const std::atomic<bool>* cancel = nullptr);

  // Asks the server to cancel an in-flight request; its future then completes with -32800.
  void cancel(std::int64_t id);

  void notify(std::string_view method, Json params = nullptr);

  // Closes the transport, fails all pending calls and joins the reader thread.
  void close();

  [[nodiscard]] bool connected() const noexcept { return connected_; }

 private:
  void reader_loop();
  void dispatch(const std::string& message);
  void fail_all(int code, const std::string& reason);
  void send(const Json& message);

  std::unique_ptr<Transport> transport_;
  std::thread reader_;
  std::atomic<bool> connected_{false};
  std::atomic<std::int64_t> next_id_{1};
  std::mutex pending_mutex_;
  std::map<std::int64_t, std::promise<Json>> pending_;
  NotificationHandler on_notification_;
  DisconnectHandler on_disconnect_;
};

// Read-only methods whose Busy answer (1007: an export or an index owns the worker's catalog, audit v2
// F-14) is worth waiting for. Exports, indexing and shutdown are never repeated.
[[nodiscard]] bool retries_when_busy(std::string_view method) noexcept;

// Pauses between Busy retries: `first`, doubled after each retry up to `longest`.
struct BusyBackoff {
  std::chrono::milliseconds first{100};
  std::chrono::milliseconds longest{1000};
};

// JsonRpcClient::call that, for retries_when_busy() methods, repeats the request after a Busy answer
// until it succeeds, fails with another error, is cancelled (-32800) or `timeout` - shared by all the
// attempts - runs out, which throws the last Busy error. Other methods behave exactly like call().
Json call_retrying_busy(JsonRpcClient& client, std::string_view method, const Json& params,
                        std::chrono::milliseconds timeout, const std::atomic<bool>* cancel = nullptr,
                        BusyBackoff backoff = {});

struct HandshakeResult {
  std::string worker_version;
  std::string mutagen_version;
  int protocol_major = 0;
  int protocol_minor = 0;
  std::uint32_t process_id = 0;
};

// Performs system.handshake and verifies the protocol major version.
HandshakeResult handshake(JsonRpcClient& client, std::string_view client_name, std::string_view client_version,
                          std::chrono::milliseconds timeout = std::chrono::seconds(10));

}  // namespace sns::ipc
