#include "sns/ipc/json_rpc_client.hpp"

#include <array>

#include "sns/ipc/framing.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::ipc {

RpcError::RpcError(int code, const std::string& message, Json data)
    : Error(code == protocol::error::kRequestCancelled ? ErrorKind::Cancelled
            : code == protocol::error::kClientTimeout  ? ErrorKind::Timeout
                                                       : ErrorKind::Remote,
            message),
      code_(code),
      data_(std::move(data)) {}

JsonRpcClient::JsonRpcClient(std::unique_ptr<Transport> transport) : transport_(std::move(transport)) {}

JsonRpcClient::~JsonRpcClient() { close(); }

void JsonRpcClient::set_notification_handler(NotificationHandler handler) { on_notification_ = std::move(handler); }

void JsonRpcClient::set_disconnect_handler(DisconnectHandler handler) { on_disconnect_ = std::move(handler); }

void JsonRpcClient::start() {
  connected_ = true;
  reader_ = std::thread([this] { reader_loop(); });
}

void JsonRpcClient::send(const Json& message) {
  transport_->write(encode_frame(message.dump(-1, ' ', false, Json::error_handler_t::replace)));
}

PendingCall JsonRpcClient::call_async(std::string_view method, Json params) {
  if (!connected_) throw RpcError(protocol::error::kClientDisconnected, "worker is not connected");
  if (!params.is_null() && !params.is_object()) throw Error(ErrorKind::InvalidArgument, "params must be an object");

  const auto id = next_id_++;
  std::promise<Json> promise;
  PendingCall pending{id, promise.get_future().share()};
  {
    std::lock_guard lock(pending_mutex_);
    pending_.emplace(id, std::move(promise));
  }

  Json request = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
  if (!params.is_null()) request["params"] = std::move(params);
  try {
    send(request);
  } catch (...) {
    std::lock_guard lock(pending_mutex_);
    pending_.erase(id);
    throw;
  }
  return pending;
}

Json JsonRpcClient::call(std::string_view method, Json params, std::chrono::milliseconds timeout,
                         const std::atomic<bool>* cancel) {
  auto pending = call_async(method, std::move(params));
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool cancel_sent = false;
  for (;;) {
    if (pending.result.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
      return pending.result.get();  // rethrows RpcError set by the reader thread
    }
    if (cancel && cancel->load() && !cancel_sent) {
      this->cancel(pending.id);
      cancel_sent = true;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      this->cancel(pending.id);
      {
        std::lock_guard lock(pending_mutex_);
        pending_.erase(pending.id);
      }
      throw RpcError(protocol::error::kClientTimeout,
                     std::string(method) + " timed out after " + std::to_string(timeout.count()) + " ms");
    }
  }
}

void JsonRpcClient::cancel(std::int64_t id) {
  try {
    notify(protocol::method::kCancelRequest, Json{{"id", id}});
  } catch (const Error&) {
    // Disconnected: the pending call fails through fail_all().
  }
}

void JsonRpcClient::notify(std::string_view method, Json params) {
  if (!connected_) throw RpcError(protocol::error::kClientDisconnected, "worker is not connected");
  Json message = {{"jsonrpc", "2.0"}, {"method", method}};
  if (!params.is_null()) message["params"] = std::move(params);
  send(message);
}

void JsonRpcClient::close() {
  if (!transport_) return;
  transport_->close();
  if (reader_.joinable()) {
    if (reader_.get_id() == std::this_thread::get_id()) {
      reader_.detach();  // close() called from a callback on the reader thread: read() returns 0 next
    } else {
      reader_.join();
    }
  }
  connected_ = false;
  transport_->release();  // the worker now sees end-of-stream and exits
  fail_all(protocol::error::kClientDisconnected, "connection closed");
}

void JsonRpcClient::reader_loop() {
  FrameDecoder decoder;
  std::array<char, 64 * 1024> buffer{};
  std::string reason = "worker closed the connection";
  try {
    for (;;) {
      const auto n = transport_->read(buffer.data(), buffer.size());
      if (n == 0) break;
      decoder.feed(std::string_view(buffer.data(), n));
      while (auto message = decoder.next()) dispatch(*message);
    }
  } catch (const std::exception& ex) {
    reason = std::string("connection error: ") + ex.what();
  }
  connected_ = false;
  fail_all(protocol::error::kClientDisconnected, reason);
  if (on_disconnect_) on_disconnect_(reason);
}

void JsonRpcClient::dispatch(const std::string& message) {
  Json json = Json::parse(message, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) throw Error(ErrorKind::Protocol, "worker sent invalid JSON");

  const bool has_id = json.contains("id") && !json["id"].is_null();
  if (json.contains("method")) {
    if (has_id) {
      // The worker never calls the client; answer so it does not wait forever.
      send(Json{{"jsonrpc", "2.0"},
                {"id", json["id"]},
                {"error", {{"code", protocol::error::kMethodNotFound}, {"message", "client exposes no methods"}}}});
    } else if (on_notification_) {
      try {
        on_notification_(json["method"].get<std::string>(), json.value("params", Json::object()));
      } catch (...) {
        // A faulty UI callback must not tear down the connection.
      }
    }
    return;
  }
  if (!has_id || !json["id"].is_number_integer()) return;

  std::promise<Json> promise;
  {
    std::lock_guard lock(pending_mutex_);
    const auto it = pending_.find(json["id"].get<std::int64_t>());
    if (it == pending_.end()) return;  // late reply to a timed-out call
    promise = std::move(it->second);
    pending_.erase(it);
  }

  if (json.contains("error")) {
    const auto& e = json["error"];
    promise.set_exception(std::make_exception_ptr(
        RpcError(e.value("code", protocol::error::kInternalError), e.value("message", std::string("unknown error")),
                 e.value("data", Json()))));
  } else {
    promise.set_value(json.value("result", Json()));
  }
}

void JsonRpcClient::fail_all(int code, const std::string& reason) {
  std::map<std::int64_t, std::promise<Json>> pending;
  {
    std::lock_guard lock(pending_mutex_);
    pending.swap(pending_);
  }
  for (auto& [id, promise] : pending) {
    promise.set_exception(std::make_exception_ptr(RpcError(code, reason)));
  }
}

HandshakeResult handshake(JsonRpcClient& client, std::string_view client_name, std::string_view client_version,
                          std::chrono::milliseconds timeout) {
  const auto r = client.call(protocol::method::kHandshake,
                             Json{{"clientName", client_name},
                                  {"clientVersion", client_version},
                                  {"protocolMajor", protocol::kMajor},
                                  {"protocolMinor", protocol::kMinor}},
                             timeout);
  HandshakeResult result{
      r.at("workerVersion").get<std::string>(), r.at("mutagenVersion").get<std::string>(),
      r.at("protocolMajor").get<int>(),         r.at("protocolMinor").get<int>(),
      r.at("processId").get<std::uint32_t>(),
  };
  if (result.protocol_major != protocol::kMajor) {
    throw RpcError(protocol::error::kProtocolMismatch, "worker speaks protocol " +
                                                           std::to_string(result.protocol_major) + ".x, expected " +
                                                           std::to_string(protocol::kMajor) + ".x");
  }
  return result;
}

}  // namespace sns::ipc
