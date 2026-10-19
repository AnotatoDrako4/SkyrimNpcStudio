#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "sns/ipc/framing.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"

using namespace sns::ipc;
using namespace std::chrono_literals;

namespace {

// One direction of an in-memory duplex stream.
class Channel {
 public:
  void write(std::string_view bytes) {
    std::lock_guard lock(m_);
    data_.insert(data_.end(), bytes.begin(), bytes.end());
    cv_.notify_all();
  }
  std::size_t read(char* buffer, std::size_t capacity) {
    std::unique_lock lock(m_);
    cv_.wait(lock, [&] { return !data_.empty() || closed_; });
    if (data_.empty()) return 0;
    const auto n = std::min(capacity, data_.size());
    std::copy_n(data_.begin(), n, buffer);
    data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
  }
  void close() {
    std::lock_guard lock(m_);
    closed_ = true;
    cv_.notify_all();
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::deque<char> data_;
  bool closed_ = false;
};

struct Pipe {
  Channel to_server;
  Channel to_client;
};

class ClientEnd final : public Transport {
 public:
  explicit ClientEnd(std::shared_ptr<Pipe> p) : pipe_(std::move(p)) {}
  std::size_t read(char* b, std::size_t n) override { return pipe_->to_client.read(b, n); }
  void write(std::string_view bytes) override { pipe_->to_server.write(bytes); }
  void close() override {
    pipe_->to_client.close();
    pipe_->to_server.close();
  }

 private:
  std::shared_ptr<Pipe> pipe_;
};

// Minimal scripted server: answers by method name, records cancel notifications.
class FakeServer {
 public:
  explicit FakeServer(std::shared_ptr<Pipe> pipe) : pipe_(std::move(pipe)) {
    thread_ = std::thread([this] { run(); });
  }
  ~FakeServer() {
    pipe_->to_server.close();
    pipe_->to_client.close();
    thread_.join();
  }

  std::vector<std::int64_t> cancelled() {
    std::lock_guard lock(m_);
    return cancelled_;
  }
  // The next `times` requests of `method` are answered Busy (1007), as the worker does while an export or
  // an index owns its catalog; after that the method echoes its params.
  void busy_for(const std::string& method, int times) {
    std::lock_guard lock(m_);
    busy_left_[method] = times;
  }
  int requests(const std::string& method) {
    std::lock_guard lock(m_);
    return requests_[method];
  }
  void send(const Json& message) { pipe_->to_client.write(encode_frame(message.dump())); }
  void hang_up() { pipe_->to_client.close(); }

 private:
  void run() {
    FrameDecoder decoder;
    char buffer[4096];
    std::vector<Json> hanging;  // "slow.method" requests answered only when cancelled
    for (;;) {
      const auto n = pipe_->to_server.read(buffer, sizeof buffer);
      if (n == 0) return;
      decoder.feed(std::string_view(buffer, n));
      while (auto msg = decoder.next()) {
        const auto req = Json::parse(*msg);
        const auto method = req.value("method", "");
        if (method == protocol::method::kCancelRequest) {
          const auto id = req["params"]["id"].get<std::int64_t>();
          {
            std::lock_guard lock(m_);
            cancelled_.push_back(id);
          }
          send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32800}, {"message", "Request was cancelled."}}}});
        } else if (answer_busy(method)) {
          send({{"jsonrpc", "2.0"},
                {"id", req["id"]},
                {"error", {{"code", protocol::error::kBusy}, {"message", "An export is using the catalog; retry when it finishes."}}}});
        } else if (method == "echo") {
          send({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", req["params"]}});
        } else if (method == "fail") {
          send({{"jsonrpc", "2.0"},
                {"id", req["id"]},
                {"error", {{"code", protocol::error::kRecordNotFound}, {"message", "nope"}, {"data", {{"x", 1}}}}}});
        } else if (method == "notify-then-echo") {
          send({{"jsonrpc", "2.0"}, {"method", "catalog.progress"}, {"params", {{"stage", "plugins"}}}});
          send({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", true}});
        } else if (method == "slow.method") {
          hanging.push_back(req);
        } else if (method.find('.') != std::string::npos) {
          send({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", req.value("params", Json())}});
        }
      }
    }
  }

  bool answer_busy(const std::string& method) {
    std::lock_guard lock(m_);
    ++requests_[method];
    auto it = busy_left_.find(method);
    if (it == busy_left_.end() || it->second == 0) return false;
    --it->second;
    return true;
  }

  std::shared_ptr<Pipe> pipe_;
  std::thread thread_;
  std::mutex m_;
  std::vector<std::int64_t> cancelled_;
  std::map<std::string, int> busy_left_;
  std::map<std::string, int> requests_;
};

struct Harness {
  std::shared_ptr<Pipe> pipe = std::make_shared<Pipe>();
  FakeServer server{pipe};
  JsonRpcClient client{std::make_unique<ClientEnd>(pipe)};
};

}  // namespace

TEST_CASE("Calls return results and map JSON-RPC errors", "[ipc][rpc]") {
  Harness h;
  h.client.start();
  CHECK(h.client.call("echo", Json{{"a", 1}}, 2s) == Json{{"a", 1}});
  try {
    h.client.call("fail", Json::object(), 2s);
    FAIL("expected RpcError");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kRecordNotFound);
    CHECK(std::string(e.what()) == "nope");
    CHECK(e.data()["x"] == 1);
  }
}

TEST_CASE("Concurrent calls are matched by id", "[ipc][rpc]") {
  Harness h;
  h.client.start();
  std::vector<std::thread> threads;
  std::atomic<int> ok{0};
  for (int i = 0; i < 16; ++i) {
    threads.emplace_back([&, i] {
      if (h.client.call("echo", Json{{"i", i}}, 5s)["i"] == i) ++ok;
    });
  }
  for (auto& t : threads) t.join();
  CHECK(ok == 16);
}

TEST_CASE("Notifications reach the handler before the response", "[ipc][rpc]") {
  Harness h;
  std::vector<std::string> seen;
  std::mutex m;
  h.client.set_notification_handler([&](const std::string& method, const Json& params) {
    std::lock_guard lock(m);
    seen.push_back(method + ":" + params.value("stage", ""));
  });
  h.client.start();
  CHECK(h.client.call("notify-then-echo", nullptr, 2s) == true);
  std::lock_guard lock(m);
  CHECK(seen == std::vector<std::string>{"catalog.progress:plugins"});
}

TEST_CASE("A timeout cancels the request on the server", "[ipc][rpc]") {
  Harness h;
  h.client.start();
  try {
    h.client.call("slow.method", Json::object(), 150ms);
    FAIL("expected timeout");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kClientTimeout);
    CHECK(e.kind() == sns::ErrorKind::Timeout);
  }
  for (int i = 0; i < 100 && h.server.cancelled().empty(); ++i) std::this_thread::sleep_for(5ms);
  CHECK(h.server.cancelled().size() == 1);
}

TEST_CASE("A cancel flag cancels an in-flight call", "[ipc][rpc]") {
  Harness h;
  h.client.start();
  std::atomic<bool> cancel{false};
  std::thread canceller([&] {
    std::this_thread::sleep_for(100ms);
    cancel = true;
  });
  try {
    h.client.call("slow.method", Json::object(), 10s, &cancel);
    FAIL("expected cancellation");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kRequestCancelled);
    CHECK(e.kind() == sns::ErrorKind::Cancelled);
  }
  canceller.join();
}

TEST_CASE("Disconnect fails pending calls and reports the reason", "[ipc][rpc]") {
  Harness h;
  std::atomic<bool> disconnected{false};
  h.client.set_disconnect_handler([&](const std::string&) { disconnected = true; });
  h.client.start();
  auto pending = h.client.call_async("slow.method", Json::object());
  h.server.hang_up();
  CHECK_THROWS_AS(pending.result.get(), RpcError);
  for (int i = 0; i < 100 && !disconnected; ++i) std::this_thread::sleep_for(5ms);
  CHECK(disconnected);
  CHECK_FALSE(h.client.connected());
  CHECK_THROWS_AS(h.client.call("echo", Json::object(), 1s), RpcError);
}

TEST_CASE("Params must be an object", "[ipc][rpc]") {
  Harness h;
  h.client.start();
  CHECK_THROWS_AS(h.client.call_async("echo", Json::array({1, 2})), sns::Error);
}

// Audit v2 F-14: while an export or an index owns the worker's catalog, every read answers Busy at once
// instead of queuing behind it. The studio waits for those reads (bounded by the caller's timeout) and
// never repeats an export.
TEST_CASE("Only reads are retried after Busy", "[ipc][rpc][busy]") {
  for (const auto* read : {protocol::method::kListPlugins, protocol::method::kSearchNpcs, protocol::method::kGetNpc,
                           protocol::method::kGetAppearance, protocol::method::kSearchArmors,
                           protocol::method::kValidateOutfit, protocol::method::kSearchHeadParts,
                           protocol::method::kGetFaceRecipe, protocol::method::kCreationValidateNpc,
                           protocol::method::kCreationSearchRecords, protocol::method::kCatalogStatus}) {
    INFO(read);
    CHECK(retries_when_busy(read));
  }
  for (const auto* write : {protocol::method::kExportOutfitPatch, protocol::method::kExportFacePatch,
                            protocol::method::kExportCompleteNpc, protocol::method::kCreationExportNpc,
                            protocol::method::kCatalogIndex, protocol::method::kShutdown, protocol::method::kHandshake}) {
    INFO(write);
    CHECK_FALSE(retries_when_busy(write));
  }
  CHECK_FALSE(retries_when_busy("catalog.somethingNew"));
}

TEST_CASE("A read answered Busy is repeated until the worker answers it", "[ipc][rpc][busy]") {
  Harness h;
  h.client.start();
  h.server.busy_for(protocol::method::kSearchNpcs, 2);
  const auto params = Json{{"text", "Lydia"}};

  CHECK(call_retrying_busy(h.client, protocol::method::kSearchNpcs, params, 5s, nullptr, {10ms, 40ms}) == params);
  CHECK(h.server.requests(protocol::method::kSearchNpcs) == 3);
}

TEST_CASE("An export answered Busy is not repeated", "[ipc][rpc][busy]") {
  Harness h;
  h.client.start();
  h.server.busy_for(protocol::method::kExportOutfitPatch, 1);
  try {
    call_retrying_busy(h.client, protocol::method::kExportOutfitPatch, Json{{"pluginName", "X.esp"}}, 5s, nullptr, {10ms, 40ms});
    FAIL("expected Busy");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kBusy);
  }
  CHECK(h.server.requests(protocol::method::kExportOutfitPatch) == 1);
}

TEST_CASE("Busy retries end at the caller's timeout with the Busy error", "[ipc][rpc][busy]") {
  Harness h;
  h.client.start();
  h.server.busy_for(protocol::method::kGetNpc, 1000);
  const auto start = std::chrono::steady_clock::now();
  try {
    call_retrying_busy(h.client, protocol::method::kGetNpc, Json{{"formKey", "0A2C8E:Skyrim.esm"}}, 300ms, nullptr, {20ms, 50ms});
    FAIL("expected Busy");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kBusy);
    CHECK(std::string(e.what()).find("export") != std::string::npos);
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(elapsed >= 250ms);
  CHECK(elapsed < 2s);
  CHECK(h.server.requests(protocol::method::kGetNpc) >= 3);
}

TEST_CASE("A cancel flag stops the Busy retries", "[ipc][rpc][busy]") {
  Harness h;
  h.client.start();
  h.server.busy_for(protocol::method::kSearchNpcs, 100000);
  std::atomic<bool> cancel{false};
  std::thread canceller([&] {
    std::this_thread::sleep_for(150ms);
    cancel = true;
  });
  const auto start = std::chrono::steady_clock::now();
  try {
    call_retrying_busy(h.client, protocol::method::kSearchNpcs, Json::object(), 30s, &cancel, {20ms, 50ms});
    FAIL("expected cancellation");
  } catch (const RpcError& e) {
    CHECK(e.code() == protocol::error::kRequestCancelled);
  }
  canceller.join();
  CHECK(std::chrono::steady_clock::now() - start < 2s);
}
