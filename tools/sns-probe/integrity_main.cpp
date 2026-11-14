// Read-only MO2 scanner. All writes are confined to the explicitly selected evidence directory.
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <iostream>
#include "sns/appearance/integrity_guard.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/worker_process.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"

namespace {
using namespace sns;
using namespace appearance::integrity;
using namespace std::chrono_literals;
std::atomic<bool> cancelled{false};
BOOL WINAPI stop(DWORD kind) { if (kind == CTRL_C_EVENT || kind == CTRL_BREAK_EVENT) { cancelled = true; return TRUE; } return FALSE; }
std::string path_text(const std::filesystem::path& p) { return text::path_to_utf8(p); }
bool inside(const std::filesystem::path& child, const std::filesystem::path& parent) {
  if (parent.empty()) return false;
  auto c = canonical(path_text(std::filesystem::weakly_canonical(child))) + "/";
  auto p = canonical(path_text(std::filesystem::weakly_canonical(parent))) + "/";
  return c.starts_with(p);
}
Json metadata(Json value) {
  if (value.contains("signature")) { value["signature"].erase("sha256"); value["signature"].erase("confidence"); }
  return value;
}
}
int main(int argc, char** argv) {
  using namespace sns;
  using namespace appearance::integrity;
  using namespace std::chrono_literals;
  if (argc != 2) { std::cerr << "usage: sns-integrity <job.json>\n"; return 2; }
  try {
    SetConsoleCtrlHandler(stop, TRUE);
    const auto job = Json::parse(file_io::read_file(text::path_from_utf8(argv[1]), 1024*1024));
    const auto instance = mo2::instance_from_path(text::path_from_utf8(job.at("instance").get<std::string>()));
    if (!instance) throw std::runtime_error("MO2 instance not found");
    const auto config = mo2::load_instance_config(*instance);
    if (!mo2::is_supported_game(config)) throw std::runtime_error("Unsupported game");
    const auto profile = mo2::load_profile(config, job.at("profile").get<std::string>());
    const auto game = mo2::inspect_game(config.game_path);
    const auto output = std::filesystem::weakly_canonical(text::path_from_utf8(job.at("output").get<std::string>()));
    for (const auto& protected_path : {config.info.root, config.base_dir, config.game_path, config.mods_dir, config.profiles_dir, config.overwrite_dir})
      if (inside(output, protected_path)) throw std::runtime_error("Evidence output must be outside MO2 and Skyrim");
    const Cancel cancel = [] { return cancelled.load(); };
    mo2::VfsBuildOptions options; options.cancelled = cancel;
    const auto vfs = mo2::VirtualFileSystem::build(config, profile, game, options);
    const auto order = mo2::resolve_load_order(profile, vfs, game);
    const auto archives = assets::archive_load_order(profile, order, vfs);
    assets::AssetResolver resolver(vfs, archives, cancel);
    if (!resolver.issues().empty()) throw std::runtime_error("Archive resolver has errors; baseline not replaced");
    const auto identity = digest(canonical(path_text(config.info.root)) + "\n" + canonical(profile.name));
    const auto baseline_path = output / "baseline.json";
    const auto previous = std::filesystem::exists(baseline_path) ? load(baseline_path) : Json();
    if (!previous.is_null() && previous.at("profile") != identity) throw std::runtime_error("Different profile: select a separate evidence directory");
    Json settings = Json::object();
    for (const auto* name : {"modlist.txt", "plugins.txt", "loadorder.txt", "Skyrim.ini", "SkyrimCustom.ini"}) {
      const auto p = profile.dir / name;
      settings[name] = std::filesystem::exists(p) ? file_signature(p, cancel) : Json();
    }
    settings["language"] = mo2::detect_game_language(profile);
    Json plugins = Json::array(), plugin_signatures = Json::object();
    for (const auto& plugin : order.active) {
      if (!plugin.path) throw std::runtime_error("Active plugin unresolved: " + plugin.name);
      plugins.push_back({{"fileName", plugin.name}, {"path", path_text(*plugin.path)}});
      plugin_signatures[canonical(plugin.name)] = file_signature(*plugin.path, cancel);
    }
    // Plugin identity/order affects dependency selection even when the three MO2 text files did not change.
    settings["pluginOrder"] = Json::array();
    for (const auto& plugin : order.active) settings["pluginOrder"].push_back(canonical(plugin.name));
    settings["archives"] = Json::array();
    for (const auto& archive : archives) settings["archives"].push_back({archive.name, archive.provider, archive.reason});
    std::set<std::string> hints = job.value("hints", std::set<std::string>{});
    if (!previous.is_null()) {
      const auto old = previous.value("pluginSignatures", Json::object());
      for (auto it = plugin_signatures.begin(); it != plugin_signatures.end(); ++it)
        if (!old.contains(it.key()) || old[it.key()] != it.value()) hints.insert("plugin:" + it.key());
      std::set<std::string> polled;
      for (const auto& npc : previous.at("npcs")) for (auto it = npc.at("assets").begin(); it != npc.at("assets").end(); ++it)
        if (polled.insert(it.key()).second && metadata(resolve_asset(it.key(), vfs, resolver, archives, cancel, false)) != metadata(it.value()))
          hints.insert("asset:" + it.key());
    }
    std::filesystem::create_directories(output);
    const auto profile_configuration = digest(settings.dump());
    auto record_settings = settings; record_settings.erase("modlist.txt");
    const auto record_configuration = digest(record_settings.dump());
    const bool records_unchanged = !previous.is_null() && previous.at("configuration") == record_configuration
        && previous.value("pluginSignatures", Json()) == plugin_signatures
        && std::none_of(hints.begin(), hints.end(), [](const auto& h) { return h.starts_with("record:") || h.starts_with("plugin:"); });
    std::unique_ptr<ipc::WorkerProcess> worker;
    std::unique_ptr<ipc::JsonRpcClient> client;
    Json index{{"skipped", true}, {"reason", "effective plugin state unchanged"}};
    auto read_record = [&](const std::string& key) -> Json {
      if (records_unchanged && previous.at("npcs").contains(key)) return previous.at("npcs").at(key).at("record");
      if (!client) {
        worker = std::make_unique<ipc::WorkerProcess>(ipc::WorkerProcess::Options{text::path_from_utf8(job.at("worker").get<std::string>()), {}, output/"worker.log"});
        worker->start(); ipc::HandshakeResult handshake;
        client = ipc::connect_worker(*worker, 30s, &handshake);
        if (handshake.protocol_minor < 8) throw std::runtime_error("Worker protocol 1.8 required");
        index = client->call(ipc::protocol::method::kCatalogIndex,
          {{"profileKey", identity}, {"cacheDirectory", path_text(output/"catalog")}, {"plugins", plugins}, {"language", settings["language"]}}, 300s, &cancelled);
      }
      return client->call(ipc::protocol::method::kGetIntegrityRecord, {{"npcFormKey", key}}, 30s, &cancelled);
    };
    SYSTEMTIME now{}; GetSystemTime(&now); char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    auto result = scan(previous, identity, record_configuration, job.at("npcs").get<std::vector<std::string>>(), hints,
      read_record,
      [&](const auto& path) { return resolve_asset(path, vfs, resolver, archives, cancel); }, stamp, cancel);
    for (const auto* name : {"modlist.txt", "plugins.txt", "loadorder.txt", "Skyrim.ini", "SkyrimCustom.ini"}) {
      const auto p = profile.dir / name;
      const auto after = std::filesystem::exists(p) ? file_signature(p, cancel) : Json();
      if (after != settings.at(name)) throw std::runtime_error("Profile changed during scan; retry");
    }
    for (const auto& plugin : order.active)
      if (file_signature(*plugin.path, cancel) != plugin_signatures.at(canonical(plugin.name)))
        throw std::runtime_error("Plugin changed during scan; retry");
    result.snapshot["profileConfiguration"] = profile_configuration;
    result.snapshot["pluginSignatures"] = plugin_signatures;
    result.snapshot.erase("revision"); result.snapshot["revision"] = digest(result.snapshot.dump());
    for (const auto& evidence : job.value("gameEvidence", Json::array())) add_game_evidence(result.snapshot, evidence);
    result.report["revision"] = result.snapshot["revision"]; result.report["catalog"] = index;
    result.report["health"] = health(result.snapshot, result.report);
    // Immutable report written first; baseline remains the commit point and references the same revision.
    file_io::write_file_atomic(output/(result.snapshot["revision"].get<std::string>() + ".report.json"), result.report.dump(2));
    save(baseline_path, result.snapshot, previous.is_null() ? "" : previous.at("revision").get<std::string>(), cancel);
    std::cout << result.report.dump(2) << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
