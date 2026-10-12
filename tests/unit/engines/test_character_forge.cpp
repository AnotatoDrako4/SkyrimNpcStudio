#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <filesystem>
#include "sns/core/error.hpp"
#include "sns/creation/forge_project.hpp"
#include "temp_dir.hpp"

using namespace sns::creation;

TEST_CASE("Character Forge .sncf projects persist without a selected NPC", "[forge][projects]") {
  test::TempDir temp;
  const auto folder = temp.path() / "Dragonborn_New.sncf";
  const auto initial = ForgeProjectStore::create("Personaje original", {"MO2", "C:/MO2/ModOrganizer.ini", "Default", "AE"});
  ForgeProjectStore::save(folder, initial);
  REQUIRE(std::filesystem::exists(folder / "project.json"));
  REQUIRE(std::filesystem::is_directory(folder / "assets"));

  const auto loaded = ForgeProjectStore::load(folder);
  CHECK(loaded.id == initial.id);
  CHECK(loaded.name == initial.name);
  CHECK(loaded.environment.profile == "Default");
  CHECK(loaded.character_name.empty());
}

TEST_CASE("Character Forge has its own undo/redo history", "[forge][history]") {
  ForgeDraft draft;
  draft.replace(ForgeProjectStore::create("Original"));
  CHECK_FALSE(draft.dirty());
  draft.set_character_name("Aerin");
  draft.set_editor_id("SNS_Aerin");
  REQUIRE(draft.can_undo());
  CHECK(draft.dirty());
  CHECK(draft.current().editor_id == "SNS_Aerin");
  CHECK(draft.undo());
  CHECK(draft.current().editor_id.empty());
  CHECK(draft.undo());
  CHECK(draft.current().character_name.empty());
  CHECK(draft.redo());
  CHECK(draft.current().character_name == "Aerin");
  draft.mark_saved();
  CHECK_FALSE(draft.dirty());
}

TEST_CASE("Character Forge rejects incompatible, invalid and non-.sncf projects", "[forge][projects]") {
  test::TempDir temp;
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
