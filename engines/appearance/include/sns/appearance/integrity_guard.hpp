#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>
#include "sns/assets/asset_resolver.hpp"

namespace sns::appearance::integrity {
using Json = nlohmann::json;
using Cancel = std::function<bool()>;
using Records = std::map<std::string, Json>;
// Keys: record:<FormKey>, plugin:<filename>, asset:<Data-relative path>, archive:<filename>.
// Events are hints only. The callbacks must read the NEW effective worker/VFS state.
using ReadRecord = std::function<Json(const std::string&)>;
using ReadAsset = std::function<Json(const std::string&)>;
std::string canonical(std::string text);
std::string digest(std::string_view bytes);
Json file_signature(const std::filesystem::path& path, const Cancel& cancel = {}, bool content = true);
Json resolve_asset(const std::string& path, const mo2::VirtualFileSystem& vfs,
                   const assets::AssetResolver& resolver, const std::vector<assets::ArchiveRef>& archives,
                   const Cancel& cancel = {}, bool content = true);
struct Scan {
  Json snapshot;
  Json report;
};
// Empty previous creates a baseline. Profile changes require explicit new baseline; never compare profiles.
// Configuration changes conservatively recheck records; only changed effective observations produce findings.
Scan scan(const Json& previous, const std::string& profile, const std::string& configuration,
          const std::vector<std::string>& npc_keys, const std::set<std::string>& hints,
          const ReadRecord& records, const ReadAsset& assets, const std::string& utc,
          const Cancel& cancel = {});
Json load(const std::filesystem::path& path);
// Compare-and-swap under a cross-process lock; concurrent stale writers fail without replacing data.
void save(const std::filesystem::path& path, const Json& snapshot, const std::string& expected_revision,
          const Cancel& cancel = {});
void add_game_evidence(Json& snapshot, const Json& evidence);
// Named potential risks only; never confirmed absence. Receipt is bound to profile/NPC/input/evidence.
void add_audit_result(Json& snapshot, const Json& receipt);
Json health(const Json& snapshot, const Json& report);
}  // namespace sns::appearance::integrity
