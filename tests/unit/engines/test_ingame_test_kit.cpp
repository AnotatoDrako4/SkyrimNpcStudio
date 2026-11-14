#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <string>

#include "sns/core/error.hpp"
#include "sns/mo2/ingame_test_kit.hpp"

using namespace sns;
using namespace sns::mo2;

namespace {

ResolvedPlugin plugin(std::string name, bool light, bool master = false) {
  ResolvedPlugin p;
  p.name = std::move(name);
  p.active = true;
  p.path = std::filesystem::path("C:/mods") / p.name;
  p.provider = "Mod of " + p.name;
  PluginHeader h;
  h.is_light = light;
  h.is_master = master || light;
  p.header = h;
  return p;
}

// Official masters, a Creation Club light master, an ESL-flagged ESP, a plugin listed but absent, regular plugins.
EffectiveLoadOrder sample_order() {
  EffectiveLoadOrder lo;
  lo.active.push_back(plugin("Skyrim.esm", false, true));          // 00
  lo.active.push_back(plugin("Update.esm", false, true));          // 01
  lo.active.push_back(plugin("ccBGSSSE001-Fish.esm", true, true));  // FE 000
  lo.active.push_back(plugin("ArmorPack.esp", true));              // FE 001
  lo.active.push_back(plugin("Patch.esp", false));                 // 02
  ResolvedPlugin missing = plugin("Missing.esp", false);
  missing.path.reset();  // enabled in plugins.txt, no mod provides it: the game skips it
  missing.header.reset();
  lo.active.push_back(missing);
  lo.active.push_back(plugin("CF_skyrim_prop.esp", true));  // FE 002
  lo.active.push_back(plugin("Late.esp", false));           // 03
  return lo;
}

constexpr const char* kSha = "F3B842C1D191D4F415A5C0E86CBDB7D4ABF0386F1B17D81D2A9FE61DE4D1A6B4";

ExportManifestSummary sample_manifest() {
  ExportManifestSummary m;
  m.format = "skyrim-npc-studio/export";
  m.mode = "forge-static-resource";
  m.plugin_file = "CF_skyrim_prop.esp";
  m.plugin_sha256 = kSha;
  m.esl_flagged = true;
  m.records = {{"000800:CF_skyrim_prop.esp", "STAT", "CF_skyrim_prop", "new"},
               {"0A2C8E:Skyrim.esm", "NPC_", "HousecarlWhiterun", "override"},
               {"000801:CF_skyrim_prop.esp", "OTFT", "CF_Outfit", "new"},
               {"000800:CF_skyrim_prop.esp", "STAT", "CF_skyrim_prop", "new"}};  // repeated: one command only
  return m;
}

InGameTestKitContext context(std::string sha = kSha) {
  return {"Pruebas SNS", std::move(sha), "2026-11-04T22:00:00Z", "D:/exports/CF_skyrim_prop"};
}

}  // namespace

TEST_CASE("FormKeys parse as the worker writes them", "[mo2][ingame-test]") {
  const auto key = parse_form_key("0A2C8E:Skyrim.esm");
  REQUIRE(key);
  CHECK(key->local_id == 0x0A2C8E);
  CHECK(key->plugin == "Skyrim.esm");
  CHECK(parse_form_key("000800:CF_skyrim_prop.esp")->local_id == 0x800);
  CHECK_FALSE(parse_form_key("Skyrim.esm"));
  CHECK_FALSE(parse_form_key("GGGGGG:Skyrim.esm"));
  CHECK_FALSE(parse_form_key("1000000:Skyrim.esm"));  // more than 24 bits
  CHECK_FALSE(parse_form_key("000800:notes.txt"));
  CHECK_FALSE(parse_form_key(":Skyrim.esm"));
}

TEST_CASE("Runtime FormIDs follow full and light indexes separately", "[mo2][ingame-test]") {
  const auto lo = sample_order();
  CHECK(format_form_id(*runtime_form_id(lo, "Skyrim.esm", 0x0A2C8E).id) == "000A2C8E");
  CHECK(format_form_id(*runtime_form_id(lo, "update.esm", 0x123).id) == "01000123");  // names compare case-insensitively
  CHECK(format_form_id(*runtime_form_id(lo, "ccBGSSSE001-Fish.esm", 0x801).id) == "FE000801");
  CHECK(format_form_id(*runtime_form_id(lo, "ArmorPack.esp", 0x800).id) == "FE001800");
  CHECK(format_form_id(*runtime_form_id(lo, "Patch.esp", 0x000D62).id) == "02000D62");
  CHECK(format_form_id(*runtime_form_id(lo, "CF_skyrim_prop.esp", 0x800).id) == "FE002800");
  CHECK(format_form_id(*runtime_form_id(lo, "Late.esp", 0x1).id) == "03000001");  // Missing.esp took no index

  SECTION("a light plugin cannot hold a local id above 0xFFF") {
    const auto r = runtime_form_id(lo, "ArmorPack.esp", 0x1000);
    CHECK_FALSE(r.id);
    CHECK(r.error.find("ESL") != std::string::npos);
  }
  SECTION("a plugin that is not active has no runtime FormID") {
    const auto r = runtime_form_id(lo, "NotInstalled.esp", 0x800);
    CHECK_FALSE(r.id);
    CHECK(r.error.find("no está activo") != std::string::npos);
  }
  SECTION("an unreadable header before the target makes every later index unknown") {
    auto broken = lo;
    broken.active[4].header.reset();  // Patch.esp: file present, header unreadable
    CHECK_FALSE(runtime_form_id(broken, "Late.esp", 1).id);
    CHECK(runtime_form_id(broken, "ArmorPack.esp", 0x800).id);  // loads before it: still known
  }
  SECTION("full plugins beyond index 0xFD are refused") {
    EffectiveLoadOrder many;
    for (int i = 0; i < 0xFE; ++i) many.active.push_back(plugin("P" + std::to_string(i) + ".esp", false));
    many.active.push_back(plugin("Overflow.esp", false));
    CHECK(format_form_id(*runtime_form_id(many, "P253.esp", 1).id) == "FD000001");
    CHECK_FALSE(runtime_form_id(many, "Overflow.esp", 1).id);
  }
}

TEST_CASE("Export manifests are read with their BOM and refused when foreign", "[mo2][ingame-test]") {
  const std::string body =
      R"({"format":"skyrim-npc-studio/export","formatVersion":2,"mode":"forge-static-resource",)"
      R"("plugin":{"fileName":"CF_skyrim_prop.esp","sha256":"ABC","eslFlagged":true},)"
      R"("records":[{"formKey":"000800:CF_skyrim_prop.esp","recordType":"STAT","editorId":"CF_skyrim_prop","kind":"new"}]})";
  const auto m = parse_export_manifest("\xEF\xBB\xBF" + body);
  CHECK(m.plugin_file == "CF_skyrim_prop.esp");
  CHECK(m.plugin_sha256 == "ABC");
  CHECK(m.esl_flagged);
  REQUIRE(m.records.size() == 1);
  CHECK(m.records[0].record_type == "STAT");

  CHECK_THROWS_AS(parse_export_manifest("not json"), Error);
  CHECK_THROWS_AS(parse_export_manifest(R"({"format":"skyrim-npc-studio/body-distribution","plugin":{}})"), Error);
  CHECK_THROWS_AS(parse_export_manifest(R"({"format":"skyrim-npc-studio/export","plugin":{"fileName":"A.esp"}})"), Error);
}

TEST_CASE("The in-game test kit places what the export changed and nothing else", "[mo2][ingame-test]") {
  const auto kit = build_ingame_test_kit(sample_manifest(), sample_order(), context());
  INFO(kit.report_text);
  REQUIRE(kit.ok);
  CHECK(kit.kit_name == "SNS_Test_CF_skyrim_prop");
  CHECK(kit.batch_file == "SNS_Test_CF_skyrim_prop.txt");
  CHECK(kit.batch_text == "player.placeatme FE002800 1\r\nplayer.placeatme 000A2C8E 1\r\n");
  CHECK(kit.batch_text.find("coc") == std::string::npos);
  REQUIRE(kit.lines.size() == 4);
  CHECK(kit.lines[2].command.empty());  // OTFT: no console command
  CHECK(kit.lines[2].note.find("OTFT") != std::string::npos);
  // The report ties the observation to these bytes and stays unverified.
  CHECK(kit.report_text.find(kSha) != std::string::npos);
  CHECK(kit.report_text.find("Pruebas SNS") != std::string::npos);
  CHECK(kit.report_text.find("`bat SNS_Test_CF_skyrim_prop`") != std::string::npos);
  CHECK(kit.report_text.find("inGameVerified: false") != std::string::npos);
  CHECK(kit.report_text.find("Mod of CF_skyrim_prop.esp") != std::string::npos);
}

TEST_CASE("The in-game test kit refuses a profile that does not load this export", "[mo2][ingame-test]") {
  SECTION("plugin not active") {
    auto lo = sample_order();
    lo.active.erase(lo.active.begin() + 6);
    const auto kit = build_ingame_test_kit(sample_manifest(), lo, context());
    CHECK_FALSE(kit.ok);
    REQUIRE_FALSE(kit.errors.empty());
    CHECK(kit.errors[0].find("Pruebas SNS") != std::string::npos);
  }
  SECTION("another build of the plugin is installed") {
    const auto kit = build_ingame_test_kit(sample_manifest(), sample_order(), context(std::string(64, '0')));
    CHECK_FALSE(kit.ok);
    REQUIRE_FALSE(kit.errors.empty());
    CHECK(kit.errors[0].find("SHA-256") != std::string::npos);
  }
  SECTION("the hash comparison ignores case") {
    std::string lower(kSha);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    CHECK(build_ingame_test_kit(sample_manifest(), sample_order(), context(lower)).ok);
  }
  SECTION("the installed plugin could not be read") {
    CHECK_FALSE(build_ingame_test_kit(sample_manifest(), sample_order(), context("")).ok);
  }
  SECTION("nothing to place or add") {
    auto m = sample_manifest();
    m.records = {{"000801:CF_skyrim_prop.esp", "OTFT", "CF_Outfit", "new"}};
    const auto kit = build_ingame_test_kit(m, sample_order(), context());
    CHECK_FALSE(kit.ok);
    CHECK(kit.batch_text.empty());
  }
}

TEST_CASE("Kit names are safe for MO2 and the console", "[mo2][ingame-test]") {
  auto m = sample_manifest();
  m.plugin_file = "My Mod-01 (v2).esp";
  auto lo = sample_order();
  lo.active.push_back(plugin("My Mod-01 (v2).esp", false));
  m.records = {{"000D62:My Mod-01 (v2).esp", "ARMO", "MyArmor", "new"}};
  const auto kit = build_ingame_test_kit(m, lo, context());
  CHECK(kit.ok);
  CHECK(kit.kit_name == "SNS_Test_My_Mod_01__v2_");
  CHECK(kit.batch_text == "player.additem 04000D62 1\r\n");
}
