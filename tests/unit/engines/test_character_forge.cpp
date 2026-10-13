#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <filesystem>
#include <limits>
#include "sns/core/error.hpp"
#include "sns/creation/forge_project.hpp"
#include "temp_dir.hpp"

using namespace sns::creation;

TEST_CASE("Character Forge .sncf projects persist without a selected NPC", "[forge][projects]") {
  sns::test::TempDir temp;
  const auto folder = temp.path() / "Dragonborn_New.sncf";
  const auto initial = ForgeProjectStore::create("Personaje original", {"MO2", "C:/MO2/ModOrganizer.ini", "Default", "AE"});
  ForgeProjectStore::save(folder, initial);
  REQUIRE(std::filesystem::exists(folder / "project.json"));
  REQUIRE(std::filesystem::is_directory(folder / "assets"));

  const auto loaded = ForgeProjectStore::load(folder);
  CHECK(loaded.id == initial.id);
  CHECK(loaded.name == initial.name);
  CHECK(loaded.environment.profile == "Default");
  CHECK(loaded.npc.name.empty());
}

TEST_CASE("Character Forge has its own undo/redo history", "[forge][history]") {
  ForgeDraft draft;
  draft.replace(ForgeProjectStore::create("Original"));
  CHECK_FALSE(draft.dirty());
  draft.set_character_name("Aerin");
  draft.set_editor_id("SNS_Aerin");
  REQUIRE(draft.can_undo());
  CHECK(draft.dirty());
  CHECK(draft.current().npc.editor_id == "SNS_Aerin");
  CHECK(draft.undo());
  CHECK(draft.current().npc.editor_id.empty());
  CHECK(draft.undo());
  CHECK(draft.current().npc.name.empty());
  CHECK(draft.redo());
  CHECK(draft.current().npc.name == "Aerin");
  draft.mark_saved();
  CHECK_FALSE(draft.dirty());
}

TEST_CASE("Character Forge rejects incompatible, invalid and non-.sncf projects", "[forge][projects]") {
  sns::test::TempDir temp;
  auto project = ForgeProjectStore::create("Prueba");
  CHECK_THROWS_AS(ForgeProjectStore::save(temp.path() / "Wrong.snps", project), sns::Error);
  const auto folder = temp.path() / "Wrong.sncf";
  std::filesystem::create_directories(folder);
  {
    std::ofstream file(folder / "project.json");
    file << R"({"kind":"characterForge","formatVersion":99,"id":"a","name":"test"})";
  }
  CHECK_THROWS_AS(ForgeProjectStore::load(folder), sns::Error);
  {
    std::ofstream file(folder / "project.json", std::ios::trunc);
    file << "{not-json";
  }
  CHECK_THROWS_AS(ForgeProjectStore::load(folder), sns::Error);
  ForgeDraft draft;
  draft.replace(std::move(project));
  CHECK_THROWS_AS(draft.set_editor_id("my invalid editor id!"), sns::Error);
}

TEST_CASE("Character Forge cannot overwrite another .sncf project", "[forge][projects][safety]") {
  sns::test::TempDir temp;
  const auto path = temp.path() / "Shared.sncf";
  auto first = ForgeProjectStore::create("First");
  auto second = ForgeProjectStore::create("Second");
  ForgeProjectStore::save(path, first);
  CHECK_THROWS_AS(ForgeProjectStore::save(path, second), sns::Error);
  CHECK(ForgeProjectStore::load(path).id == first.id);
}

TEST_CASE("Native NPC persists and its full draft participates in undo", "[forge][native]") {
  sns::test::TempDir temp;
  ForgeDraft draft;
  draft.replace(ForgeProjectStore::create("Native"));
  NativeNpcDraft npc;
  npc.name = "Aerin"; npc.editor_id = "SNS_Aerin";
  npc.race_form_key = "013746:Skyrim.esm"; npc.female = true;
  npc.voice_form_key = "0A0050:Skyrim.esm"; npc.class_form_key = "0A0051:Skyrim.esm";
  npc.level = 12; npc.health = 180;
  npc.head_part_form_keys = {"051623:Skyrim.esm"};
  npc.outfit_armor_form_keys = {"013952:Skyrim.esm"};
  npc.inventory = {{"013953:Skyrim.esm", 2}};
  npc.factions = {{"0A0030:Skyrim.esm", 0}};
  npc.placement = NativePlacement{"01691D:Skyrim.esm", 10, 20, 30, 0};
  draft.set_native_npc(npc);
  CHECK(draft.current().npc.health == 180);
  CHECK(draft.undo()); CHECK(draft.current().npc.name.empty());
  CHECK(draft.redo()); CHECK(draft.current().npc.inventory.front().count == 2);
  const auto folder = temp.path() / "Native.sncf";
  ForgeProjectStore::save(folder, draft.current());
  const auto loaded = ForgeProjectStore::load(folder);
  CHECK(loaded.npc.editor_id == "SNS_Aerin");
  CHECK(loaded.npc.placement->x == 10);
  CHECK(loaded.npc.outfit_armor_form_keys == npc.outfit_armor_form_keys);
}

TEST_CASE("Forge v1 migrates in memory without changing the original file", "[forge][migration]") {
  sns::test::TempDir temp;
  const auto folder = temp.path() / "Old.sncf";
  std::filesystem::create_directories(folder);
  const std::string json = R"({"formatVersion":1,"kind":"characterForge","id":"v1","name":"Old","character":{"name":"Aerin","editorId":"SNS_Aerin"}})";
  { std::ofstream file(folder / "project.json"); file << json; }
  const auto migrated = ForgeProjectStore::load(folder);
  CHECK(migrated.format_version == 2);
  CHECK(migrated.npc.name == "Aerin");
  CHECK(migrated.npc.race_form_key.empty());
  std::ifstream file(folder / "project.json");
  const std::string unchanged((std::istreambuf_iterator<char>(file)), {});
  CHECK(unchanged == json);
}

TEST_CASE("Native draft rejects invalid EditorID and nonfinite body values", "[forge][validation]") {
  ForgeDraft draft;
  draft.replace(ForgeProjectStore::create("Native"));
  NativeNpcDraft npc;
  npc.editor_id = "123Invalid";
  CHECK_THROWS_AS(draft.set_native_npc(npc), sns::Error);
  npc.editor_id = "SNS_Valid"; npc.height = std::numeric_limits<float>::quiet_NaN();
  CHECK_THROWS_AS(draft.set_native_npc(npc), sns::Error);
}

TEST_CASE("Forge refuses malformed export options before saving", "[forge][schema]") {
  ForgeDraft draft;
  draft.replace(ForgeProjectStore::create("Native"));
  CHECK_THROWS_AS(draft.set_export_options({{"pluginName", 123}}), sns::Error);
  CHECK_THROWS_AS(draft.set_export_options({{"eslFlag", "true"}}), sns::Error);
}

TEST_CASE("Native JSON integers never truncate or wrap", "[forge][schema]") {
  CHECK_THROWS_AS(native_npc_from_json({{"level", 1.9}}), sns::Error);
  CHECK_THROWS_AS(native_npc_from_json({{"health", 4294967396ULL}}), sns::Error);
  CHECK_THROWS_AS(native_npc_from_json({{"inventory", {{{"formKey", "013953:Skyrim.esm"}, {"count", 1.5}}}}}), sns::Error);
}
