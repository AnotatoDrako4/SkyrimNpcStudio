#include <NifFile.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <sstream>
#include <thread>
#include "temp_dir.hpp"
#include "sns/core/file_io.hpp"
#include "sns/appearance/integrity_guard.hpp"

using namespace sns::appearance::integrity;
namespace {
Json record(const std::string& key, const std::string& path) {
  return {{"formKey", key}, {"definingPlugin", "base.esm"}, {"winner", "base.esm"},
    {"overrides", {"base.esm"}}, {"appearanceSignature", "face-a"}, {"headParts", {"000800:hair.esl"}},
    {"dependencies", {"plugin:base.esm", "record:000800:hair.esl"}}, {"assets", {path}}, {"unresolved", Json::array()}};
}
struct Fixture {
  Records records{{"000001:base.esm", record("000001:base.esm", "meshes/face.nif")},
                  {"000002:base.esm", record("000002:base.esm", "meshes/other.nif")}};
  std::vector<std::string> keys{"000001:base.esm", "000002:base.esm"};
  Json observations{{"meshes/face.nif", {{"resolved", true}, {"provider", "A"}, {"signature", {{"sha256", "a"}, {"modified", 1}}}}},
                    {"meshes/other.nif", {{"resolved", true}, {"provider", "B"}}}};
  Scan run(const Json& old = nullptr, const std::set<std::string>& hints = {}, const std::string& config = "cfg", const Cancel& cancel = {}) {
    return scan(old, "test-profile", config, keys, hints, [&](const auto& k) { return records.at(k); },
      [&](const auto& p) { return observations.at(p); }, "2026-11-07T12:00:00Z", cancel);
  }
};
}
TEST_CASE("Integrity: priority changes with identical winners are not defects", "[integrity]") {
  Fixture f; auto first = f.run(); auto next = f.run(first.snapshot, {}, "reordered");
  CHECK(next.report["findings"].empty()); CHECK(next.report["checkedNpcs"] == 2);
  f.observations["meshes/face.nif"]["signature"]["modified"] = 2;
  CHECK(f.run(next.snapshot, {"asset:meshes/face.nif"}).report["findings"].empty());
}
TEST_CASE("Integrity: real FaceGen provider change rechecks only its dependent NPC", "[integrity]") {
  nifly::NifFile nif;
  nif.Create(nifly::NiVersion::getSSE());
  std::ostringstream nif_bytes(std::ios::binary);
  REQUIRE(nif.Save(nif_bytes) == 0);
  sns::test::TempDir dir;
  dir.write("A/meshes/face.nif", nif_bytes.str()); dir.write("B/meshes/face.nif", nif_bytes.str());
  auto v1 = sns::mo2::VirtualFileSystem::build_from({{"A", dir.path()/"A"}, {"B", dir.path()/"B"}});
  auto v2 = sns::mo2::VirtualFileSystem::build_from({{"B", dir.path()/"B"}, {"A", dir.path()/"A"}});
  sns::assets::AssetResolver r1(v1, {}), r2(v2, {});
  Fixture f;
  f.observations["meshes/face.nif"] = resolve_asset("meshes/face.nif", v1, r1, {});
  auto first = f.run();
  f.observations["meshes/face.nif"] = resolve_asset("meshes/face.nif", v2, r2, {});
  auto next = f.run(first.snapshot, {"asset:meshes/face.nif"});
  REQUIRE(next.report["findings"].size() == 1);
  CHECK(next.report["checkedNpcs"] == 1); CHECK(next.report["checkedAssets"] == 1);
  CHECK(next.report["findings"][0]["rule"] == "EffectiveProviderChanged");
  CHECK_FALSE(next.report["findings"][0]["confirmed"].get<bool>());
}
TEST_CASE("Integrity: hair and HeadPart changes preserve ESL FormKeys", "[integrity]") {
  Fixture f; f.records.begin()->second["assets"] = {"meshes/hair.nif"};
  f.observations["meshes/hair.nif"] = {{"resolved", true}, {"provider", "HairA"}};
  auto first = f.run(); f.observations["meshes/hair.nif"]["provider"] = "HairB";
  auto hair = f.run(first.snapshot, {"asset:meshes/hair.nif"});
  CHECK(hair.report["checkedNpcs"] == 1);
  f.records.begin()->second["appearanceSignature"] = "new-headpart";
  auto parts = f.run(hair.snapshot, {"record:000800:hair.esl"});
  CHECK(parts.report["checkedNpcs"] == 2);
  CHECK(parts.report["findings"][0]["rule"] == "AppearanceChanged");
  CHECK(parts.snapshot["npcs"].contains("000001:base.esm"));
}
TEST_CASE("Integrity: nonappearance plugin edits do not produce regressions", "[integrity]") {
  Fixture f; auto first = f.run();
  auto unchanged = f.run(first.snapshot, {"plugin:base.esm"}); CHECK(unchanged.report["findings"].empty());
  f.records.begin()->second["winner"] = "ai.esp";
  f.records.begin()->second["overrides"].push_back("ai.esp");
  auto changed = f.run(first.snapshot, {"plugin:base.esm"});
  CHECK(changed.report["findings"][0]["severity"] == "informational");
}
TEST_CASE("Integrity: archive replacements and loose changes invalidate effective dependencies", "[integrity]") {
  Fixture f; f.observations["meshes/face.nif"]["archive"] = "faces.bsa";
  auto first = f.run(); f.observations["meshes/face.nif"]["signature"]["sha256"] = "b";
  auto next = f.run(first.snapshot, {"archive:faces.bsa"});
  CHECK(next.report["checkedNpcs"] == 1); CHECK(next.report["findings"][0]["rule"] == "ResourceChanged");
  f.observations["meshes/face.nif"]["signature"]["sha256"] = "c";
  CHECK(f.run(next.snapshot, {"asset:meshes/face.nif"}).report["findings"].size() == 1);
}
TEST_CASE("Integrity: missing dependency confirmed and traits parent indexed", "[integrity]") {
  Fixture f; f.records.begin()->second["dependencies"].push_back("record:000003:base.esm");
  f.records.begin()->second["traitsSource"] = "000003:base.esm";
  auto first = f.run(); f.records.begin()->second["unresolved"] = {"000003:base.esm"};
  auto next = f.run(first.snapshot, {"record:000003:base.esm"});
  CHECK(next.report["checkedNpcs"] == 1); CHECK(next.report["findings"][0]["confirmed"] == true);
  CHECK(next.report["findings"][0]["severity"] == "critical");
}
TEST_CASE("Integrity: corrupt profile and cancelled scans cannot replace baseline", "[integrity]") {
  Fixture f; auto first = f.run(); sns::test::TempDir dir; auto file = dir.path()/"baseline.json";
  save(file, first.snapshot, "");
  CHECK_THROWS(f.run(first.snapshot, {}, "cfg", [] { return true; }));
  CHECK_THROWS(scan(first.snapshot, "other", "cfg", f.keys, {}, {}, {}, "now"));
  CHECK_THROWS(save(file, first.snapshot, first.snapshot["revision"], [] { return true; }));
  CHECK(load(file) == first.snapshot);
  auto corrupt = first.snapshot; corrupt["npcs"] = Json::object();
  CHECK_THROWS(save(file, corrupt, first.snapshot["revision"])); CHECK(load(file) == first.snapshot);
  dir.write("bad.json", "{broken"); CHECK_THROWS(load(dir.path()/"bad.json"));
}
TEST_CASE("Integrity: concurrent snapshots use compare and swap with rollback on stale write", "[integrity]") {
  Fixture f; auto first = f.run(); sns::test::TempDir dir; auto file = dir.path()/"baseline.json";
  save(file, first.snapshot, "");
  auto next = f.run(first.snapshot, {}, "new-config"); std::atomic<int> success{0};
  auto writer = [&] { try { save(file, next.snapshot, first.snapshot["revision"]); ++success; } catch (const std::exception&) {} };
  std::thread a(writer), b(writer); a.join(); b.join();
  CHECK(success == 1); CHECK(load(file) == next.snapshot);
  CHECK_THROWS(save(file, first.snapshot, "stale")); CHECK(load(file) == next.snapshot);
}
TEST_CASE("Integrity: large resources are bounded metadata observations", "[integrity]") {
  sns::test::TempDir dir; auto path = dir.write("large.nif", "x");
  std::filesystem::resize_file(path, 32ull * 1024 * 1024);
  auto sig = file_signature(path); CHECK(sig["confidence"] == "metadata"); CHECK_FALSE(sig.contains("sha256"));
  CHECK(sig["bytes"] == 32ull * 1024 * 1024);
}
TEST_CASE("Integrity: game evidence is human scoped and stale evidence rejected", "[integrity]") {
  Fixture f; auto first = f.run();
  Json evidence{{"npc", f.keys[0]}, {"profile", "test-profile"}, {"plugin", "repair.esp"}, {"modVersion", "1"},
    {"testedUtc", "2026-11-07T12:00:00Z"}, {"screenshot", "capture.png"}, {"observed", "hair visible"},
    {"decision", "approved"}, {"packageSha256", std::string(64, 'a')}, {"inputDigest", "stale"}};
  CHECK_THROWS(add_game_evidence(first.snapshot, evidence));
  evidence["inputDigest"] = first.snapshot["npcs"][f.keys[0]]["inputDigest"];
  add_game_evidence(first.snapshot, evidence); CHECK(first.snapshot["evidence"].size() == 1);
  CHECK(first.snapshot["evidence"][0]["source"] == "human-skyrim-observation");
}
