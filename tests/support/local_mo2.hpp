#pragma once

// Shared setup of the LOCAL integration tests (real MO2 instance, read-only). See
// tests/integration/test_local_mo2.cpp for the environment variables.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "sns/core/text.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/worker_process.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"
#include "temp_dir.hpp"

namespace sns::test {

inline std::optional<std::string> env(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return std::nullopt;
  std::string s(value);
  free(value);
  return s.empty() ? std::nullopt : std::optional(s);
}

struct LocalSetup {
  mo2::InstanceConfig config;
  mo2::Profile profile;
  mo2::GameInstall game;
  mo2::VirtualFileSystem vfs;
  mo2::EffectiveLoadOrder load_order;
  long long vfs_ms = 0;
};

inline LocalSetup& local_setup() {
  static std::unique_ptr<LocalSetup> setup;
  if (setup) return *setup;
  const auto ini = env("SNS_LOCAL_MO2_INI");
  if (!ini) SKIP("SNS_LOCAL_MO2_INI is not set");

  setup = std::make_unique<LocalSetup>();
  auto info = mo2::instance_from_path(text::path_from_utf8(*ini));
  REQUIRE(info);
  setup->config = mo2::load_instance_config(*info);
  if (const auto game = env("SNS_LOCAL_GAME_DIR")) setup->config.game_path = text::path_from_utf8(*game);
  setup->profile = mo2::load_profile(setup->config, env("SNS_LOCAL_PROFILE").value_or(setup->config.selected_profile));
  setup->game = mo2::inspect_game(setup->config.game_path);
  const auto t0 = std::chrono::steady_clock::now();
  setup->vfs = mo2::VirtualFileSystem::build(setup->config, setup->profile, setup->game);
  setup->vfs_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  setup->load_order = mo2::resolve_load_order(setup->profile, setup->vfs, setup->game);
  return *setup;
}

// A worker that has indexed the local profile (SNS_LOCAL_CACHE_DIR makes re-indexing incremental).
struct IndexedWorker {
  TempDir dir;
  std::unique_ptr<ipc::WorkerProcess> process;
  std::unique_ptr<ipc::JsonRpcClient> client;
  std::map<std::string, std::string> providers;  // plugin -> MO2 mod

  ~IndexedWorker() {
    if (client) {
      try {
        client->call(ipc::protocol::method::kShutdown, nullptr, std::chrono::seconds(10));
      } catch (...) {
      }
    }
  }
};

inline std::unique_ptr<IndexedWorker> indexed_worker(const LocalSetup& s, const char* worker_exe) {
  using namespace std::chrono_literals;
  auto w = std::make_unique<IndexedWorker>();
  w->process = std::make_unique<ipc::WorkerProcess>(ipc::WorkerProcess::Options{worker_exe, {}, w->dir.path() / "worker.log"});
  w->process->start();
  w->client = ipc::connect_worker(*w->process, 30s);
  ipc::Json plugins = ipc::Json::array();
  for (const auto& p : s.load_order.active) {
    plugins.push_back({{"fileName", p.name}, {"path", p.path ? ipc::Json(text::path_to_utf8(*p.path)) : ipc::Json()}, {"enabled", true}});
    w->providers[p.name] = p.provider;
  }
  const auto cache = env("SNS_LOCAL_CACHE_DIR").value_or(text::path_to_utf8(w->dir.path() / "cache"));
  w->client->call(ipc::protocol::method::kCatalogIndex,
                  ipc::Json{{"profileKey", "local-" + s.profile.name},
                            {"cacheDirectory", cache},
                            {"plugins", plugins},
                            {"language", mo2::detect_game_language(s.profile)}},
                  30min);
  return w;
}

}  // namespace sns::test
