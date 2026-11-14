#include "sns/appearance/integrity_guard.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include "sns/core/file_io.hpp"
#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/nif/safe_load.hpp"

namespace sns::appearance::integrity {
namespace {
void check(const Cancel& cancel) { if (cancel && cancel()) throw std::runtime_error("integrity: cancelled"); }
struct Hash {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  Hash() {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
      throw std::runtime_error("SHA256 unavailable");
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
      BCryptCloseAlgorithmProvider(algorithm, 0); throw std::runtime_error("SHA256 create failed");
    }
  }
  ~Hash() { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); }
  void add(std::string_view s) {
    while (!s.empty()) {
      auto n = static_cast<ULONG>(std::min<std::size_t>(s.size(), 65536));
      if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())), n, 0) < 0)
        throw std::runtime_error("SHA256 update failed");
      s.remove_prefix(n);
    }
  }
  std::string finish() {
    std::array<unsigned char, 32> bytes{};
    if (BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
      throw std::runtime_error("SHA256 finish failed");
    std::string out;
    for (auto b : bytes) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; }
    return out;
  }
};
std::string revision(Json j) { j.erase("revision"); return digest(j.dump()); }
void validate(const Json& j) {
  if (j.at("schema") != 1 || j.at("profile").get<std::string>().empty() || !j.at("npcs").is_object()
      || j.at("revision") != revision(j)) throw std::runtime_error("integrity: corrupt or unsupported baseline");
}
std::set<std::string> dependencies(const Json& record) {
  std::set<std::string> result;
  for (const auto& d : record.at("dependencies")) result.insert(canonical(d.get<std::string>()));
  for (const auto& p : record.at("assets")) result.insert("asset:" + canonical(p.get<std::string>()));
  return result;
}
Json effective_asset(Json value) {
  // A touched timestamp alone is not proof of changed content when a full digest is available.
  if (value.contains("signature") && value["signature"].contains("sha256")) value["signature"].erase("modified");
  return value;
}
}
std::string canonical(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c); });
  return s;
}
std::string digest(std::string_view bytes) { Hash h; h.add(bytes); return h.finish(); }
Json file_signature(const std::filesystem::path& path, const Cancel& cancel, bool content) {
  check(cancel);
  const auto size = std::filesystem::file_size(path);
  const auto time = std::filesystem::last_write_time(path);
  Json result{{"bytes", size}, {"modified", time.time_since_epoch().count()}};
  // Bounded memory and I/O. Large resources remain metadata observations, never claims of byte equality.
  if (content && size <= 1024 * 1024) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("integrity: resource unreadable");
    Hash h; std::array<char, 65536> buffer{};
    while (in) { check(cancel); in.read(buffer.data(), buffer.size()); h.add({buffer.data(), static_cast<size_t>(in.gcount())}); }
    if (!in.eof()) throw std::runtime_error("integrity: resource read failed");
    result["sha256"] = h.finish(); result["confidence"] = "content";
  } else result["confidence"] = "metadata";
  if (size != std::filesystem::file_size(path) || time != std::filesystem::last_write_time(path))
    throw std::runtime_error("integrity: resource changed during scan");
  return result;
}
Json resolve_asset(const std::string& path, const mo2::VirtualFileSystem& vfs,
                   const assets::AssetResolver& resolver, const std::vector<assets::ArchiveRef>& archives,
                   const Cancel& cancel, bool content) {
  check(cancel);
  const auto location = resolver.locate(path);
  if (!location) return {{"resolved", false}};
  Json result{{"resolved", true}, {"provider", location->provider}};
  std::filesystem::path backing;
  if (location->kind == assets::AssetLocation::Kind::Loose) {
    result["kind"] = "loose";
    const auto file = vfs.resolve(path);
    if (!file) throw std::runtime_error("integrity: inconsistent VFS");
    backing = file->absolute;
    result["signature"] = file_signature(file->absolute, cancel, content);
  } else {
    result["kind"] = "archive"; result["archive"] = canonical(location->provider);
    result["provider"] = location->archive_provider;
    auto it = std::find_if(archives.begin(), archives.end(), [&](const auto& a) {
      return canonical(a.name) == canonical(location->provider) && a.provider == location->archive_provider;
    });
    if (it == archives.end()) throw std::runtime_error("integrity: archive missing from effective order");
    backing = it->path;
    result["signature"] = file_signature(it->path, cancel, content);
  }
  if (content && canonical(path).ends_with(".nif")) {
    check(cancel);
    try {
      const auto bytes = resolver.read(path, nif::kMaxNifBytes);
      if (!bytes) throw std::runtime_error("integrity: resource disappeared during dependency inspection");
      result["embeddedAssets"] = nif::texture_dependencies(*bytes, path);
      result["dependencyInspection"] = {{"state", "complete"}, {"version", 1}};
    } catch (const Error& error) {
      result["embeddedAssets"] = Json::array();
      result["dependencyInspection"] = {{"state", "unavailable"}, {"version", 1}, {"detail", error.what()}};
    }
    check(cancel);
    const auto after = file_signature(backing, cancel, false);
    if (after.at("bytes") != result["signature"]["bytes"] || after.at("modified") != result["signature"]["modified"])
      throw std::runtime_error("integrity: resource changed during dependency inspection");
  }
  return result;
}
Scan scan(const Json& previous, const std::string& profile, const std::string& configuration,
          const std::vector<std::string>& npc_keys, const std::set<std::string>& hints,
          const ReadRecord& records, const ReadAsset& assets, const std::string& utc, const Cancel& cancel) {
  check(cancel);
  if (profile.empty() || configuration.empty() || utc.empty()) throw std::runtime_error("integrity: missing scan identity");
  const bool initial = previous.is_null();
  if (!initial) { validate(previous); if (previous.at("profile") != profile) throw std::runtime_error("integrity: profile changed; create a new baseline"); }
  Json next = initial ? Json{{"schema", 1}, {"profile", profile}, {"npcs", Json::object()}, {"evidence", Json::array()}} : previous;
  Json findings = Json::array();
  std::set<std::string> wanted, dirty;
  for (const auto& key : npc_keys) wanted.insert(canonical(key));
  if (initial || previous.value("analysisVersion", 0) != 2 || previous.at("configuration") != configuration) dirty = wanted;
  else {
    for (const auto& hint : hints) {
      const auto id = canonical(hint);
      // A changed plugin can introduce overrides that were not in the previous reverse index.
      // Conservatively inspect the watched set, never the entire catalogue, for plugin-level hints.
      if (id.starts_with("plugin:")) dirty.insert(wanted.begin(), wanted.end());
      if (previous.at("index").contains(id))
        for (const auto& key : previous.at("index").at(id)) dirty.insert(key.get<std::string>());
    }
    for (const auto& key : wanted) if (!next["npcs"].contains(key)) dirty.insert(key);
  }
  for (auto it = next["npcs"].begin(); it != next["npcs"].end();) {
    if (!wanted.contains(it.key())) it = next["npcs"].erase(it); else ++it;
  }
  Json asset_cache = Json::object();
  std::size_t checked = 0;
  for (const auto& key : dirty) {
    if (!wanted.contains(key)) continue;
    check(cancel); ++checked;
    auto record = records(key);
    if (canonical(record.at("formKey").get<std::string>()) != key) throw std::runtime_error("integrity: record identity mismatch");
    auto deps = dependencies(record); deps.insert("record:" + key);
    Json observed = Json::object();
    std::vector<std::string> pending = record.at("assets").get<std::vector<std::string>>();
    for (std::size_t i = 0; i < pending.size(); ++i) {
      check(cancel); auto path = canonical(pending[i]);
      if (path.size() > 1024 || !text::is_safe_relative_path(path)) throw std::runtime_error("integrity: unsafe asset dependency");
      if (observed.contains(path)) continue;
      if (observed.size() >= 4096) throw std::runtime_error("integrity: dependency budget exceeded");
      if (!asset_cache.contains(path)) asset_cache[path] = assets(path);
      observed[path] = asset_cache[path];
      deps.insert("asset:" + path);
      for (const auto& embedded : observed[path].value("embeddedAssets", Json::array())) pending.push_back(embedded.get<std::string>());
      if (observed[path].contains("archive")) deps.insert("archive:" + canonical(observed[path]["archive"].get<std::string>()));
    }
    Json state{{"record", record}, {"assets", observed}, {"dependencies", deps}, {"lastAnalysis", utc}};
    Json open = next["npcs"].contains(key) ? next["npcs"][key].value("openRisks", Json::array()) : Json::array();
    // Confirmed missing dependencies are recomputed; potential compatibility risks need an audit to close.
    open.erase(std::remove_if(open.begin(), open.end(), [](const auto& f) { return f.at("confirmed").template get<bool>(); }), open.end());
    auto emit = [&](const std::string& rule, const std::string& severity, bool confirmed, const std::string& resource, const Json& before, const Json& after) {
      findings.push_back({{"npc", key}, {"rule", rule}, {"severity", severity}, {"confirmed", confirmed},
          {"resource", resource}, {"before", before}, {"after", after}, {"evidence", "effective-worker-vfs-observation"},
          {"suggestion", confirmed ? "Resolver dependencia y repetir auditoría" : "REQUIERE AUDITORÍA FACIAL"}});
      bool regression = false;
      if (confirmed && next["npcs"].contains(key)) {
        const auto& prior = next["npcs"][key];
        if (rule == "MissingDependency") regression = prior["record"]["unresolved"].empty();
        if (rule == "MissingAsset" && prior["assets"].contains(resource)) regression = prior["assets"][resource].value("resolved", false);
      }
      findings.back()["newRegression"] = regression;
      findings.back()["id"] = digest(Json{{"npc", key}, {"rule", rule}, {"resource", resource}, {"before", before}, {"after", after}}.dump());
      if (severity != "informational") {
        const auto& finding = findings.back();
        if (std::find(open.begin(), open.end(), finding) == open.end()) open.push_back(finding);
      }
    };
    if (!record.at("unresolved").empty()) emit("MissingDependency", "critical", true, "", nullptr, record["unresolved"]);
    for (auto it = observed.begin(); it != observed.end(); ++it)
      if (!it.value().at("resolved").get<bool>()) emit("MissingAsset", "critical", true, it.key(), nullptr, it.value());
    for (auto it = observed.begin(); it != observed.end(); ++it)
      if (it.value().contains("dependencyInspection") && it.value()["dependencyInspection"]["state"] != "complete")
        emit("NifDependenciesUnavailable", "medium", false, it.key(), nullptr, it.value()["dependencyInspection"]);
    if (next["npcs"].contains(key)) {
      const auto& old = next["npcs"][key];
      if (old["record"]["appearanceSignature"] != record["appearanceSignature"])
        emit("AppearanceChanged", "high", false, "", old["record"]["appearanceSignature"], record["appearanceSignature"]);
      else if (old["record"]["winner"] != record["winner"] || old["record"]["overrides"] != record["overrides"])
        emit("OverrideChainChanged", "informational", false, "", old["record"]["overrides"], record["overrides"]);
      for (auto it = observed.begin(); it != observed.end(); ++it) {
        const auto before = old["assets"].value(it.key(), Json());
        if (effective_asset(before) != effective_asset(it.value())) {
          const bool provider = !before.is_null() && before.value("provider", "") != it.value().value("provider", "");
          emit(provider ? "EffectiveProviderChanged" : "ResourceChanged", provider ? "high" : "medium", false, it.key(), before, it.value());
        } else if (it.value().contains("signature") && it.value()["signature"].value("confidence", "") == "metadata") {
          emit("ContentUnverified", "medium", false, it.key(), before, it.value());
        }
      }
    }
    Json effective = observed;
    for (auto& a : effective) a = effective_asset(a);
    state["inputDigest"] = digest(Json{{"record", record}, {"assets", effective}}.dump());
    if (next["npcs"].contains(key) && next["npcs"][key]["inputDigest"] != state["inputDigest"])
      for (const auto& audit : next.value("audits", Json::array()))
        if (canonical(audit.at("npc").get<std::string>()) == key && audit.at("decision") == "passed"
            && audit.at("inputDigest") == next["npcs"][key]["inputDigest"])
          emit("AuditStale", "medium", false, "", audit.at("inputDigest"), state["inputDigest"]);
    state["openRisks"] = open;
    // Last valid analysis is preserved as historical evidence, never promoted to game acceptance.
    bool valid = record.at("unresolved").empty();
    for (const auto& a : observed) valid = valid && a.at("resolved").get<bool>()
        && (!a.contains("dependencyInspection") || a["dependencyInspection"]["state"] == "complete");
    if (valid) state["lastValidAnalysis"] = Json{{"utc", utc}, {"inputDigest", state["inputDigest"]}};
    else if (next["npcs"].contains(key)) state["lastValidAnalysis"] = next["npcs"][key].value("lastValidAnalysis", Json());
    next["npcs"][key] = std::move(state);
  }
  next["index"] = Json::object();
  for (auto it = next["npcs"].begin(); it != next["npcs"].end(); ++it)
    for (const auto& dep : it.value()["dependencies"]) {
      auto id = dep.get<std::string>(); if (!next["index"].contains(id)) next["index"][id] = Json::array();
      next["index"][id].push_back(it.key());
    }
  check(cancel); next["analysisVersion"] = 2; next["configuration"] = configuration; next["lastScan"] = utc;
  next["revision"] = revision(next);
  return {next, {{"schema", 1}, {"profile", profile}, {"revision", next["revision"]}, {"checkedNpcs", checked},
      {"checkedAssets", asset_cache.size()}, {"findings", findings}, {"inGameVerified", false}}};
}
Json load(const std::filesystem::path& path) {
  auto j = Json::parse(file_io::read_file(path, 64 * 1024 * 1024)); validate(j); return j;
}
void save(const std::filesystem::path& path, const Json& snapshot, const std::string& expected_revision, const Cancel& cancel) {
  check(cancel); validate(snapshot);
  std::filesystem::create_directories(path.parent_path());
  auto lock_path = path; lock_path += L".lock";
  HANDLE handle = CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("integrity: baseline busy");
  struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{handle};
  const auto current = std::filesystem::exists(path) ? load(path).at("revision").get<std::string>() : "";
  if (current != expected_revision) throw std::runtime_error("integrity: stale baseline writer");
  check(cancel); file_io::write_file_atomic(path, snapshot.dump(2));
}
void add_game_evidence(Json& snapshot, const Json& evidence) {
  validate(snapshot);
  for (const auto* field : {"npc", "profile", "plugin", "modVersion", "testedUtc", "screenshot", "observed", "decision", "inputDigest", "packageSha256"})
    if (evidence.at(field).get<std::string>().empty()) throw std::runtime_error("integrity: incomplete game evidence");
  const auto key = canonical(evidence.at("npc").get<std::string>());
  if (evidence.at("profile") != snapshot.at("profile") || !snapshot.at("npcs").contains(key)
      || evidence.at("inputDigest") != snapshot.at("npcs").at(key).at("inputDigest")
      || (evidence.at("decision") != "approved" && evidence.at("decision") != "rejected"))
    throw std::runtime_error("integrity: stale or invalid game evidence");
  auto e = evidence; e["source"] = "human-skyrim-observation";
  const auto package_hash = e.at("packageSha256").get<std::string>();
  if (package_hash.size() != 64 || package_hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    throw std::runtime_error("integrity: invalid package SHA256");
  snapshot["evidence"].push_back(e); snapshot["revision"] = revision(snapshot);
}
void add_audit_result(Json& snapshot, const Json& receipt) {
  validate(snapshot);
  for (const auto* field : {"id", "profile", "npc", "inputDigest", "decision", "auditor", "version", "analyzedUtc", "evidenceSha256"})
    if (receipt.at(field).get<std::string>().empty()) throw std::runtime_error("integrity: incomplete audit receipt");
  const auto hash = receipt.at("evidenceSha256").get<std::string>();
  if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    throw std::runtime_error("integrity: invalid audit evidence SHA256");
  const auto key = canonical(receipt.at("npc").get<std::string>());
  if (receipt.at("profile") != snapshot.at("profile") || !snapshot.at("npcs").contains(key)
      || receipt.at("inputDigest") != snapshot.at("npcs").at(key).at("inputDigest"))
    throw std::runtime_error("integrity: stale audit receipt");
  const auto decision = receipt.at("decision").get<std::string>();
  if (decision != "passed" && decision != "failed" && decision != "deferred") throw std::runtime_error("integrity: unknown audit decision");
  for (const auto& prior : snapshot.value("audits", Json::array()))
    if (prior.at("id") == receipt.at("id")) {
      if (prior != receipt) throw std::runtime_error("integrity: audit ID collision");
      return;
    }
  const auto ids = receipt.at("findingIds").get<std::set<std::string>>();
  if (ids.empty() || ids.size() != receipt.at("findingIds").size()) throw std::runtime_error("integrity: invalid audit scope");
  auto risks = snapshot.at("npcs").at(key).at("openRisks");
  for (const auto& id : ids) {
    auto it = std::find_if(risks.begin(), risks.end(), [&](const auto& risk) { return risk.value("id", "") == id; });
    if (it == risks.end()) throw std::runtime_error("integrity: unknown audit finding");
    if (decision == "passed" && (it->at("confirmed").get<bool>() || it->at("rule") == "NifDependenciesUnavailable" || it->at("rule") == "ContentUnverified"))
      throw std::runtime_error("integrity: unresolved machine evidence cannot be waived by audit");
  }
  if (decision == "passed") risks.erase(std::remove_if(risks.begin(), risks.end(), [&](const auto& risk) {
    return ids.contains(risk.value("id", ""));
  }), risks.end());
  // Commit only after every field and finding was checked: failures leave snapshot byte-identical.
  Json next = snapshot;
  next["npcs"][key]["openRisks"] = risks;
  if (!next.contains("audits")) next["audits"] = Json::array();
  next["audits"].push_back(receipt); next["revision"] = revision(next);
  snapshot = std::move(next);
}
Json health(const Json& snapshot, const Json& report) {
  validate(snapshot);
  if (report.at("revision") != snapshot.at("revision")) throw std::runtime_error("integrity: stale health report");
  Json risks = Json::array(), regressions = Json::array(), evidence = snapshot.at("evidence"), audits = snapshot.value("audits", Json::array());
  std::set<std::string> affected;
  bool critical = false;
  for (const auto& npc : snapshot.at("npcs")) for (const auto& risk : npc.value("openRisks", Json::array())) risks.push_back(risk);
  for (const auto& finding : report.at("findings")) {
    affected.insert(finding.at("npc").get<std::string>());
    if (finding.value("newRegression", false)) regressions.push_back(finding);
  }
  for (const auto& risk : risks) critical = critical || risk.at("severity") == "critical";
  for (auto& item : evidence) {
    const auto key = canonical(item.at("npc").get<std::string>());
    item["stale"] = !snapshot.at("npcs").contains(key) || snapshot.at("npcs").at(key).at("inputDigest") != item.at("inputDigest");
  }
  for (auto& item : audits) {
    const auto key = canonical(item.at("npc").get<std::string>());
    item["stale"] = !snapshot.at("npcs").contains(key) || snapshot.at("npcs").at(key).at("inputDigest") != item.at("inputDigest");
  }
  return {{"lastScan", snapshot.at("lastScan")}, {"profile", snapshot.at("profile")}, {"findings", report.at("findings")}, {"openRisks", risks},
      {"affectedNpcs", affected}, {"newRegressions", regressions}, {"profileIntegrity", critical ? "blocked" : risks.empty() ? "no-detected-defect" : "requires-audit"},
      {"gameEvidence", evidence}, {"audits", audits}, {"state", "PENDIENTE_SKYRIM"}, {"inGameVerified", false}};
}
}  // namespace sns::appearance::integrity
