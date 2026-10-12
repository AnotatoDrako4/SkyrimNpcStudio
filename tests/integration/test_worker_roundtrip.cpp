#include <catch2/catch_test_macros.hpp>

#include <atomic>

#include "process.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/transport.hpp"
#include "sns/ipc/worker_process.hpp"
#include "sns/mo2/plugin_header.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::ipc;
using namespace std::chrono_literals;
namespace method = protocol::method;

namespace {

struct RunningWorker {
  test::TempDir dir;
  WorkerProcess process{{SNS_WORKER_EXE, {}, dir.path() / "worker.log"}};
  std::unique_ptr<JsonRpcClient> client;
  HandshakeResult hello;

  RunningWorker() {
    process.start();
    client = connect_worker(process, 30s, &hello);
  }
};

Json generate_fixture(const std::filesystem::path& dir) {
  const std::wstring cmd = L"\"" + text::to_wide(SNS_FIXTURE_GEN_EXE) + L"\" \"" + dir.native() + L"\"";
  REQUIRE(test::run_and_wait(cmd) == 0);
  return Json::parse(file_io::read_text_file(dir / "fixture.json"));
}

int error_code_of(const std::function<void()>& f) {
  try {
    f();
  } catch (const RpcError& e) {
    return e.code();
  }
  return 0;
}

}  // namespace

TEST_CASE("Worker: handshake, ping and protocol errors over a real named pipe", "[integration][worker]") {
  RunningWorker w;
  CHECK(w.hello.protocol_major == protocol::kMajor);
  CHECK(w.hello.mutagen_version == "0.54.4");
  CHECK(w.hello.process_id == w.process.pid());

  CHECK(w.client->call(method::kPing, nullptr, 5s).contains("unixTimeMs"));
  CHECK(w.client->call(method::kCatalogStatus, nullptr, 5s)["indexed"] == false);
  CHECK(error_code_of([&] { w.client->call(method::kSearchNpcs, Json{{"text", "x"}}, 5s); }) == protocol::error::kNotIndexed);
  CHECK(error_code_of([&] { w.client->call("no.such.method", Json::object(), 5s); }) == protocol::error::kMethodNotFound);

  w.client->call(method::kShutdown, nullptr, 5s);
  CHECK(w.process.wait(10s) == std::optional<std::uint32_t>(0));
}

TEST_CASE("Worker: exits when the studio disconnects", "[integration][worker]") {
  RunningWorker w;
  w.client->close();
  CHECK(w.process.wait(10s).has_value());
}

TEST_CASE("Worker: pipe owner is verified", "[integration][worker][security]") {
  test::TempDir dir;
  WorkerProcess worker({SNS_WORKER_EXE, {}, dir.path() / "worker.log"});
  worker.start();
  // Claim that a different process (ourselves) should own the pipe: the connection is refused.
  CHECK_THROWS_AS(NamedPipeTransport::connect(worker.pipe_name(), 30s, GetCurrentProcessId()), Error);
  worker.terminate();
}

TEST_CASE("First use case on synthetic data: Lydia's outfit exported as a mod", "[integration][usecase]") {
  struct {
    test::TempDir dir;
  } w;
  const auto fixture = generate_fixture(w.dir.path() / "data");

  // Handlers must be installed before start(), so this test builds its own client.
  std::atomic<int> progress_events{0};
  WorkerProcess worker({SNS_WORKER_EXE, {}, w.dir.path() / "worker.log"});
  worker.start();
  auto transport = NamedPipeTransport::connect(worker.pipe_name(), 30s, worker.pid());
  JsonRpcClient client(std::move(transport));
  client.set_notification_handler([&](const std::string& m, const Json&) {
    if (m == method::kIndexProgress) ++progress_events;
  });
  client.start();
  handshake(client, "integration-test", "0");

  // 3-4. Index the profile's load order.
  Json plugins = Json::array();
  for (const auto& p : fixture["plugins"]) plugins.push_back({{"fileName", p["fileName"]}, {"path", p["path"]}, {"enabled", true}});
  const auto index = client.call(method::kCatalogIndex,
                                 Json{{"profileKey", "integration"},
                                      {"cacheDirectory", text::path_to_utf8(w.dir.path() / "cache")},
                                      {"plugins", plugins}},
                                 120s);
  CHECK(index["npcCount"] == 4);
  CHECK(progress_events > 0);

  // 5-6. Find Lydia and identify the winning record.
  const auto found = client.call(method::kSearchNpcs, Json{{"text", "Lydia"}}, 10s);
  REQUIRE(found["total"] == 1);
  const auto lydia = found["items"][0];
  CHECK(lydia["formKey"] == fixture["lydia"]);
  CHECK(lydia["winningPlugin"] == "LateTweak.esp");

  // 7. Inspect the current outfit.
  const auto details = client.call(method::kGetNpc, Json{{"formKey", lydia["formKey"]}}, 10s);
  CHECK(details["defaultOutfit"]["outfit"]["editorId"] == "HousecarlOutfit");
  CHECK(details["overrides"].size() == 3);

  // 8-10. Pick pieces from different plugins and validate slots/dependencies.
  const Json pieces = Json::array({fixture["crimsonCuirass"], fixture["steelBoots"], fixture["crimsonCloak"]});
  const auto validation = client.call(method::kValidateOutfit, Json{{"npcFormKey", lydia["formKey"]}, {"armorFormKeys", pieces}}, 10s);
  CHECK(validation["exportable"] == true);

  const auto conflict = client.call(method::kValidateOutfit,
                                    Json{{"npcFormKey", lydia["formKey"]},
                                         {"armorFormKeys", Json::array({fixture["steelCuirass"], fixture["elvenCuirass"]})}},
                                    10s);
  CHECK(conflict["exportable"] == false);

  // 12-16. Build the OTFT + NPC override, write the ESP, validate and package it.
  const auto out = w.dir.path() / "exports";
  std::filesystem::create_directories(out);
  const Json request = {{"pluginName", "SkyrimNpcStudio_Lydia.esp"},
                        {"outputDirectory", text::path_to_utf8(out)},
                        {"eslFlag", true},
                        {"assignments", Json::array({{{"npcFormKey", lydia["formKey"]}, {"armorFormKeys", pieces}}})}};
  const auto exported = client.call(method::kExportOutfitPatch, request, 60s);
  CHECK(exported["eslFlagged"] == true);
  CHECK(exported["masters"] == Json::array({"Skyrim.esm", "ArmorPack.esp"}));

  // Independent check of the written file with the C++ header reader.
  const auto header = mo2::read_plugin_header(text::path_from_utf8(exported["pluginPath"].get<std::string>()));
  CHECK(header.light_flag);
  CHECK(header.masters == std::vector<std::string>{"Skyrim.esm", "ArmorPack.esp"});
  CHECK(std::filesystem::exists(text::path_from_utf8(exported["zipPath"].get<std::string>())));
  CHECK(std::filesystem::exists(out / "SkyrimNpcStudio_Lydia" / "meta.ini"));

  // Never overwrite.
  CHECK(error_code_of([&] { client.call(method::kExportOutfitPatch, request, 60s); }) == protocol::error::kOutputExists);
  client.close();
  CHECK(worker.wait(10s).has_value());
}
