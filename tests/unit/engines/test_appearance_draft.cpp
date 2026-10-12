#include <catch2/catch_test_macros.hpp>

#include "sns/appearance/appearance_draft.hpp"
#include "sns/core/error.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::appearance;
using namespace sns::projects;

TEST_CASE("Appearance edits are undoable commands", "[appearance][commands]") {
  AppearanceDraft draft;
  CommandStack stack;

  draft.set_target({"0A2C8E:Skyrim.esm", "HousecarlWhiterun", "Lydia", "Skyrim.esm", 1});
  projects::FaceConfig initial_face{
      .preset_id = "initial",
      .preset_name = "Vanilla Lydia",
      .preset_source = "vanilla",
      .donor_npc_form_key = "0A2C8E:Skyrim.esm",
      .donor_plugin = "Skyrim.esm",
      .hair_color = ColorRgb{40, 30, 25},
      .skin_tone = ColorRgb{210, 200, 190},
      .weight = 80.0f,
      .height = 1.0f,
  };
  draft.set_initial_face(initial_face);

  CHECK_FALSE(draft.is_dirty());
  CHECK_FALSE(draft.is_face_dirty());

  // Apply donor face
  projects::FaceConfig donor{
      .preset_id = "helen",
      .preset_name = "Helen Lydia Replacer",
      .preset_source = "donor",
      .donor_npc_form_key = "0A2C8E:Skyrim.esm",
      .donor_plugin = "Helen_Lydia_replacer.esp",
      .donor_facegen_mesh = "meshes/actors/character/facegendata/facegeom/Skyrim.esm/000A2C8E.nif",
      .donor_facegen_tint = "textures/actors/character/facegendata/facetint/Skyrim.esm/000A2C8E.dds",
      .hair_color = ColorRgb{48, 35, 33},
      .skin_tone = ColorRgb{221, 221, 221},
      .head_parts = {
          {"000801:Helen_Lydia_replacer.esp", "AAA_Hair", "Hair", "meshes/helen/hair.nif"},
          {"000802:Helen_Lydia_replacer.esp", "AAA_GirlHeads", "Face", "meshes/helen/head.nif"},
      },
      .weight = 100.0f,
      .height = 0.97f,
  };

  stack.push(make_apply_donor_face(draft, donor, "Aplicar apariencia de Helen Replacer"));
  CHECK(draft.is_face_dirty());
  CHECK(draft.face().config.donor_plugin == "Helen_Lydia_replacer.esp");
  CHECK(draft.face().config.weight == 100.0f);

  // Set facial slider
  stack.push(make_set_face_slider(draft, "NoseLongShort", 0.45f));
  CHECK(draft.face().slider_value("NoseLongShort") == 0.45f);

  // Undo slider
  CHECK(stack.undo());
  CHECK(draft.face().slider_value("NoseLongShort") == 0.0f);

  // Redo slider
  CHECK(stack.redo());
  CHECK(draft.face().slider_value("NoseLongShort") == 0.45f);

  // Undo donor face
  CHECK(stack.undo()); // undo slider
  CHECK(stack.undo()); // undo donor face
  CHECK_FALSE(draft.face().active);
  CHECK(draft.face().config.donor_plugin == "Skyrim.esm");
}

TEST_CASE("Projects format v2 stores both outfit and face with backward compatibility", "[projects][v2]") {
  test::TempDir dir;
  const auto folder = dir.path() / "Lydia_Complete.snps";
  auto project = ProjectStore::create("Lydia Complete", {"Default", "D:/MO2/ModOrganizer.ini", "Default", "Anniversary Edition"});

  NpcEdit edit;
  edit.target = {"0A2C8E:Skyrim.esm", "HousecarlWhiterun", "Lydia", "Helen_Lydia_replacer.esp", 4};
  edit.outfit = OutfitConfig{
      .outfit_editor_id = "SNS_LydiaOutfit",
      .pieces = {{"00000A:[Amsedillir] Runes of Roots.esp", "Corset", "[Amsedillir] Runes of Roots.esp", 4}},
      .also_sleeping = false,
  };
  edit.face = FaceConfig{
      .preset_id = "helen_lydia",
      .preset_name = "Helen Lydia",
      .preset_source = "donor",
      .donor_npc_form_key = "0A2C8E:Skyrim.esm",
      .donor_plugin = "Helen_Lydia_replacer.esp",
      .donor_facegen_mesh = "meshes/actors/character/facegendata/facegeom/Skyrim.esm/000A2C8E.nif",
      .donor_facegen_tint = "textures/actors/character/facegendata/facetint/Skyrim.esm/000A2C8E.dds",
      .hair_color = ColorRgb{48, 35, 33},
      .skin_tone = ColorRgb{221, 221, 221},
      .head_parts = {{"000801:Helen_Lydia_replacer.esp", "AAA_Hair", "Hair", "meshes/hair.nif"}},
      .weight = 100.0f,
      .height = 0.97f,
  };
  project.npcs.push_back(edit);

  ProjectStore::save(folder, project);
  CHECK(std::filesystem::exists(folder / "face.json"));
  CHECK(std::filesystem::exists(folder / "outfit.json"));
  CHECK(std::filesystem::exists(folder / "npc.json"));

  const auto loaded = ProjectStore::load(folder);
  REQUIRE(loaded.npcs.size() == 1);
  CHECK(loaded.format_version == 2);
  REQUIRE(loaded.npcs[0].face.has_value());
  CHECK(loaded.npcs[0].face->donor_plugin == "Helen_Lydia_replacer.esp");
  CHECK(loaded.npcs[0].face->weight == 100.0f);
  REQUIRE(loaded.npcs[0].outfit.has_value());
  CHECK(loaded.npcs[0].outfit->pieces.size() == 1);

  // Check dependencies collected both outfit and face donor plugins
  CHECK(std::find(loaded.dependencies.begin(), loaded.dependencies.end(), "[Amsedillir] Runes of Roots.esp") != loaded.dependencies.end());
  CHECK(std::find(loaded.dependencies.begin(), loaded.dependencies.end(), "Helen_Lydia_replacer.esp") != loaded.dependencies.end());
}

TEST_CASE("ProjectStore loads legacy format v1 projects seamlessly", "[projects][v1_compat]") {
  test::TempDir dir;
  const auto folder = dir.path() / "Legacy.snps";

  // Simulate an old v1 project without face.json
  dir.write("Legacy.snps/project.json", R"({
    "format": "skyrim-npc-studio/project",
    "formatVersion": 1,
    "id": "1234567812345678",
    "name": "Legacy Project",
    "createdUtc": "2026-10-11T12:00:00Z",
    "modifiedUtc": "2026-10-11T12:00:00Z",
    "environment": {"instanceName": "Default", "instanceIni": "", "profile": "Default", "gameEdition": "SE"},
    "exportOptions": {"pluginName": "Legacy.esp", "eslFlag": false, "createZip": false, "outputDirectory": ""},
    "history": []
  })");
  dir.write("Legacy.snps/npc.json", R"({
    "formatVersion": 1,
    "npcs": [{"formKey": "0A2C8E:Skyrim.esm", "editorId": "Lydia", "name": "Lydia", "winningPlugin": "Skyrim.esm", "overrideCount": 1}]
  })");
  dir.write("Legacy.snps/outfit.json", R"({
    "formatVersion": 1,
    "outfits": {
      "0A2C8E:Skyrim.esm": {
        "outfitEditorId": "SNS_LydiaOutfit",
        "pieces": [{"formKey": "013952:Skyrim.esm", "name": "Steel Armor", "plugin": "Skyrim.esm", "slotMask": 4}],
        "alsoSleeping": false
      }
    }
  })");

  const auto loaded = ProjectStore::load(folder);
  REQUIRE(loaded.npcs.size() == 1);
  CHECK(loaded.format_version == 1);
  REQUIRE(loaded.npcs[0].outfit.has_value());
  CHECK(loaded.npcs[0].outfit->pieces[0].name == "Steel Armor");
  CHECK_FALSE(loaded.npcs[0].face.has_value());
}
