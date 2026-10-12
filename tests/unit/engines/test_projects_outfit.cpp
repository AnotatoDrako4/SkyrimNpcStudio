#include <catch2/catch_test_macros.hpp>

#include "sns/core/error.hpp"
#include "sns/outfit/outfit_draft.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::outfit;
using namespace sns::projects;

namespace {
Piece piece(std::string key, std::string name, int slot) {
  return {std::move(key), std::move(name), "Skyrim.esm", 1u << (slot - 30)};
}
}  // namespace

TEST_CASE("Outfit edits are undoable commands", "[outfit][commands]") {
  OutfitDraft draft;
  CommandStack stack;
  stack.push(make_add_piece(draft, piece("013952:Skyrim.esm", "Steel Armor", 32), true));
  stack.push(make_add_piece(draft, piece("013951:Skyrim.esm", "Steel Boots", 37), true));
  CHECK(draft.pieces().size() == 2);
  CHECK(draft.combined_mask() == ((1u << 2) | (1u << 7)));

  // Equipping another body piece replaces the cuirass, like the game would.
  stack.push(make_add_piece(draft, piece("000801:ArmorPack.esp", "Crimson Corset", 32), true));
  CHECK(stack.undo_text() == "Add Crimson Corset (replaces Steel Armor)");
  CHECK(draft.pieces().size() == 2);
  CHECK(draft.conflicts().empty());

  CHECK(stack.undo());
  CHECK(draft.contains("013952:Skyrim.esm"));
  CHECK(stack.redo());
  CHECK_FALSE(draft.contains("013952:Skyrim.esm"));

  stack.push(make_remove_piece(draft, "013951:Skyrim.esm"));
  CHECK(draft.pieces().size() == 1);
  CHECK(stack.history().size() == 4);
  CHECK_THROWS_AS(make_remove_piece(draft, "nope"), Error);
  CHECK_THROWS_AS(make_add_piece(draft, piece("000801:ArmorPack.esp", "dup", 33), false), Error);
}

TEST_CASE("Without replacement, slot conflicts are kept and reported", "[outfit]") {
  OutfitDraft draft;
  CommandStack stack;
  stack.push(make_add_piece(draft, piece("a:Skyrim.esm", "A", 32), false));
  stack.push(make_add_piece(draft, piece("b:Skyrim.esm", "B", 32), false));
  const auto conflicts = draft.conflicts();
  REQUIRE(conflicts.size() == 1);
  CHECK(conflicts[0].slot == 32);
}

TEST_CASE("Command stack tracks the saved state and drops the redo branch", "[commands]") {
  OutfitDraft draft;
  CommandStack stack;
  stack.push(make_add_piece(draft, piece("a:S.esm", "A", 30), true));
  stack.mark_clean();
  CHECK(stack.is_clean());
  stack.push(make_add_piece(draft, piece("b:S.esm", "B", 31), true));
  CHECK_FALSE(stack.is_clean());
  stack.undo();
  CHECK(stack.is_clean());
  stack.undo();
  stack.push(make_add_piece(draft, piece("c:S.esm", "C", 33), true));
  CHECK_FALSE(stack.can_redo());
  CHECK_FALSE(stack.is_clean());
}

TEST_CASE("Projects round-trip through .snps folders with version snapshots", "[projects]") {
  test::TempDir dir;
  const auto folder = dir.path() / "Lydia_Custom.snps";
  auto project = ProjectStore::create("Lydia Custom", {"Portable (Apps)", "D:/Apps/ModOrganizer.ini", "Default", "Anniversary Edition"});
  NpcEdit edit;
  edit.target = {"0A2C8E:Skyrim.esm", "HousecarlWhiterun", "Lydia", "Skyrim.esm", 1};
  edit.outfit = OutfitConfig{"SNS_LydiaOutfit", {piece("000801:ArmorPack.esp", "Crimson Corset", 32)}, false};
  project.npcs.push_back(edit);
  project.export_options = {"SkyrimNpcStudio_Lydia.esp", true, true, "D:/Exports"};

  ProjectStore::save(folder, project);
  ProjectStore::save(folder, project);  // second save snapshots the first

  const auto loaded = ProjectStore::load(folder);
  CHECK(loaded.id == project.id);
  CHECK(loaded.environment.profile == "Default");
  REQUIRE(loaded.npcs.size() == 1);
  CHECK(loaded.npcs[0].target.form_key == "0A2C8E:Skyrim.esm");
  REQUIRE(loaded.npcs[0].outfit);
  CHECK(loaded.npcs[0].outfit->pieces[0].plugin == "Skyrim.esm");
  CHECK(loaded.dependencies == std::vector<std::string>{"ArmorPack.esp", "Skyrim.esm"});
  CHECK(loaded.export_options.esl_flag);
  CHECK(ProjectStore::list_versions(folder).size() == 1);
  CHECK(std::filesystem::is_directory(folder / "exports"));
}

TEST_CASE("Projects store and round-trip multiple NPCs", "[projects]") {
  test::TempDir dir;
  const auto folder = dir.path() / "Followers.snps";
  auto project = ProjectStore::create("Followers", {"Portable (Apps)", "D:/Apps/ModOrganizer.ini", "Default", "Anniversary Edition"});

  NpcEdit lydia;
  lydia.target = {"0A2C8E:Skyrim.esm", "HousecarlWhiterun", "Lydia", "Skyrim.esm", 1};
  lydia.outfit = OutfitConfig{"SNS_LydiaOutfit", {piece("000801:ArmorPack.esp", "Crimson Corset", 32)}, false};
  project.npcs.push_back(lydia);

  NpcEdit aela;
  aela.target = {"01A696:Skyrim.esm", "AelaTheHuntress", "Aela", "Skyrim.esm", 1};
  aela.outfit = OutfitConfig{"SNS_AelaOutfit", {piece("013952:Skyrim.esm", "Steel Armor", 32)}, true};
  project.npcs.push_back(aela);

  ProjectStore::save(folder, project);

  const auto loaded = ProjectStore::load(folder);
  REQUIRE(loaded.npcs.size() == 2);
  CHECK(loaded.npcs[0].target.form_key == "0A2C8E:Skyrim.esm");
  CHECK(loaded.npcs[1].target.form_key == "01A696:Skyrim.esm");
  REQUIRE(loaded.npcs[1].outfit);
  CHECK(loaded.npcs[1].outfit->also_sleeping);
  CHECK(loaded.npcs[1].outfit->pieces.size() == 1);
  CHECK(loaded.dependencies == std::vector<std::string>{"ArmorPack.esp", "Skyrim.esm"});
}

TEST_CASE("Loading rejects foreign or future project files", "[projects]") {
  test::TempDir dir;
  dir.write("p.snps/project.json", R"({"format":"something-else","formatVersion":1,"id":"x"})");
  CHECK_THROWS_AS(ProjectStore::load(dir.path() / "p.snps"), Error);
  dir.write("q.snps/project.json", R"({"format":"skyrim-npc-studio/project","formatVersion":99,"id":"x"})");
  CHECK_THROWS_AS(ProjectStore::load(dir.path() / "q.snps"), Error);
  dir.write("r.snps/project.json", "{not json");
  CHECK_THROWS_AS(ProjectStore::load(dir.path() / "r.snps"), Error);
}
