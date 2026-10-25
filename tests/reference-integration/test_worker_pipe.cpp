#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>

#include "sns/creation/native_npc_draft.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/worker_process.hpp"

using namespace sns::ipc;
using namespace std::chrono_literals;
namespace method = protocol::method;

// AG05 (T05): the PUBLISHED worker process, over its real named pipe, with the studio's own C++ client. Protocol 1.6 and the five behavior
// endpoints, and what a studio that has not indexed anything gets back: a typed NotIndexed, never a fixture answer. Skipped (not passed)
// when the worker has not been published next to the test binaries.

namespace {
int error_code_of(const std::function<void()>& call) {
  try {
    call();
  } catch (const RpcError& e) {
    return e.code();
  }
  return 0;
}
}  // namespace

TEST_CASE("the real worker over its named pipe speaks protocol 1.6 and has the five behavior endpoints", "[pipe][worker][ag05]") {
  const std::filesystem::path exe = SNS_WORKER_EXE;
  if (!std::filesystem::exists(exe)) SKIP("the worker is not published at " + exe.string());
  WorkerProcess process{{exe, {}, std::filesystem::temp_directory_path() / "sns-ag05-pipe-worker.log"}};
  process.start();
  HandshakeResult hello;
  auto client = connect_worker(process, 30s, &hello);
  REQUIRE(client);
  CHECK(hello.protocol_major == protocol::kMajor);
  CHECK(hello.protocol_minor == protocol::kMinor);
  CHECK(hello.protocol_major == 1);
  CHECK(hello.protocol_minor >= 6);  // the behavior endpoints arrived in 1.6; 1.7 added body.inspectNpcs / export.bodyDistribution

  const auto again = client->call(method::kHandshake, Json{{"clientName", "ag05"}, {"clientVersion", "0"}, {"protocolMajor", 1}, {"protocolMinor", 6}}, 10s);
  const auto capabilities = again.at("capabilities");
  for (const char* name : {method::kCreationAnalyzeVoice, method::kCreationGetBehaviorCatalog, method::kCreationApplyArchetype,
                           method::kCreationUndoArchetype, method::kCreationValidateBehavior}) {
    INFO(name);
    CHECK(std::find(capabilities.begin(), capabilities.end(), Json(name)) != capabilities.end());
  }
  for (const char* name : {method::kBodyInspectNpcs, method::kExportBodyDistribution}) {
    INFO(name);
    CHECK(std::find(capabilities.begin(), capabilities.end(), Json(name)) != capabilities.end());
  }

  // Nothing is indexed in this fresh worker: each endpoint that reads the catalog says so, with the typed code.
  const auto npc = sns::creation::native_npc_to_json(sns::creation::NativeNpcDraft{});
  CHECK(error_code_of([&] { client->call(method::kCreationAnalyzeVoice, Json{{"voiceFormKey", "013AD2:Skyrim.esm"}}, 10s); }) == protocol::error::kNotIndexed);
  CHECK(error_code_of([&] { client->call(method::kCreationGetBehaviorCatalog, Json::object(), 10s); }) == protocol::error::kNotIndexed);
  CHECK(error_code_of([&] { client->call(method::kCreationApplyArchetype, Json{{"npc", npc}, {"archetypeId", "defensive-warrior"}, {"dryRun", true}}, 10s); }) == protocol::error::kNotIndexed);
  CHECK(error_code_of([&] { client->call(method::kCreationValidateBehavior, Json{{"npc", npc}}, 10s); }) == protocol::error::kNotIndexed);
  // Undo needs no catalog: a wrong hash is refused, and the method exists.
  const auto undo = error_code_of([&] { client->call(method::kCreationUndoArchetype, Json{{"expectedOriginalSha256", "00"}, {"undoRestoreToken", npc}}, 10s); });
  CHECK(undo != 0);
  CHECK(undo != protocol::error::kMethodNotFound);

  // A request made for a fixture folder is refused, not answered with the lab's synthetic world.
  CHECK(error_code_of([&] { client->call(method::kCreationGetBehaviorCatalog, Json{{"fixtureDirectory", "C:/anywhere"}}, 10s); }) != 0);
}
