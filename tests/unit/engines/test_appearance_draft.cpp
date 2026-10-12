#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "sns/appearance/appearance_draft.hpp"
#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::appearance;
using namespace sns::projects;

namespace {

// Lydia-like recipe: hair, face, eyes from the record and the race; vanilla values.
face::FaceRecipe lydia_recipe() {
  face::FaceRecipe r;
  r.npc_form_key = "0A2C8E:Skyrim.esm";
  r.race_editor_id = "NordRace";
  r.female = true;
  auto part = [](const char* fk, const char* edid, face::HeadPartType type) {
    face::HeadPartData p;
    p.form_key = fk;
    p.editor_id = edid;
    p.type = type;
    p.model = std::string("meshes\\") + edid + ".nif";
    return p;
  };
  r.parts = {part("051193:Skyrim.esm", "HairFemaleNord02", face::HeadPartType::Hair),
             part("051623:Skyrim.esm", "FemaleHeadNord", face::HeadPartType::Face),
             part("040210:Skyrim.esm", "FemaleEyesHumanGreenHazel", face::HeadPartType::Eyes),
             part("051176:Skyrim.esm", "HairFemaleNord04", face::HeadPartType::Hair)};
  r.npc_head_parts = {"0EC1B2:Skyrim.esm", "051193:Skyrim.esm", "040210:Skyrim.esm"};
  r.values.sliders[0] = 0.1f;
  r.values.sliders[7] = -0.6f;
  r.values.presets = {16, -1, 9, 16};
  r.values.weight = 100.0f;
  r.values.skin_tone = {206, 205, 204};
  r.values.hair_color = face::Rgb8{26, 28, 28};
  r.values.tints = {{24, {206, 205, 204}, 1.0f, 602}, {31, {47, 32, 19}, 0.61f, 661}};
  return r;
}

AppearanceDraft loaded_draft(const face::FaceRecipe& recipe) {
  AppearanceDraft draft;
  draft.set_target({"0A2C8E:Skyrim.esm", "HousecarlWhiterun", "Lydia", "Skyrim.esm", 1});
  draft.set_initial_outfit({{"013952:Skyrim.esm", "Steel Armor", "Skyrim.esm", 1u << 2}});
  draft.set_face_baseline(face_state_from_recipe(recipe));
  return draft;
}

}  // namespace

TEST_CASE("Face and outfit edits share one undo history; a slider drag is one step", "[appearance][commands]") {
  const auto recipe = lydia_recipe();
  auto draft = loaded_draft(recipe);
  CommandStack stack;
  CHECK_FALSE(draft.is_dirty());

  // Dragging the nose slider produces many commands that merge into one undo step.
  for (const float v : {0.2f, 0.4f, 0.6f, 0.8f}) {
    auto next = draft.face();
    next.values.sliders[0] = v;
    stack.push(make_set_face(draft, next, "Nariz", "slider:NoseLongVsShort"));
  }
  CHECK(draft.face().values.sliders[0] == 0.8f);
  CHECK(stack.history().size() == 1);
  CHECK(draft.is_face_dirty());

  stack.push(outfit::make_add_piece(draft.outfit(), {"013951:Skyrim.esm", "Steel Boots", "Skyrim.esm", 1u << 7}, true));
  auto hair = draft.face();
  hair.head_parts[1] = "051176:Skyrim.esm";
  stack.push(make_set_face(draft, hair, "Cambiar cabello"));
  stack.push(make_set_height(draft, 1.03f));
  stack.push(make_set_height(draft, 1.05f));  // merged with the previous height change
  CHECK(stack.history().size() == 4);

  // Undo walks back through face, outfit and body changes in order.
  REQUIRE(stack.undo());
  CHECK(draft.height() == 1.0f);
  REQUIRE(stack.undo());
  CHECK(draft.face().head_parts[1] == "051193:Skyrim.esm");
  REQUIRE(stack.undo());
  CHECK(draft.outfit().pieces().size() == 1);
  REQUIRE(stack.undo());
  CHECK(draft.face().values.sliders[0] == 0.1f);
  CHECK_FALSE(draft.is_dirty());

  REQUIRE(stack.redo());
  CHECK(draft.face().values.sliders[0] == 0.8f);

  // A different slider is a new step even right after.
  auto eyes = draft.face();
  eyes.values.sliders[7] = 0.3f;
  stack.push(make_set_face(draft, eyes, "Ojos", "slider:EyesUpVsDown"));
  CHECK(stack.history().size() == 2);

  // Merging never crosses the saved state.
  stack.mark_clean();
  auto again = draft.face();
  again.values.sliders[7] = 0.4f;
  stack.push(make_set_face(draft, again, "Ojos", "slider:EyesUpVsDown"));
  CHECK(stack.history().size() == 3);
  CHECK_FALSE(stack.is_clean());
}

TEST_CASE("Format 3 projects store and restore the complete face", "[appearance][projects]") {
  const auto recipe = lydia_recipe();
  auto draft = loaded_draft(recipe);
  auto edited = draft.face();
  edited.values.sliders[3] = -0.45f;
  edited.values.presets[0] = 3;
  edited.values.weight = 60.0f;
  edited.values.hair_color = face::Rgb8{120, 30, 20};
  edited.values.tints[1].value = 0.9f;
  edited.values.custom_morphs["CME_BrowInOut"] = 0.25f;
  edited.values.sculpt = {{"Actors\\Character\\Character Assets\\FemaleHeadCharGen.tri", 996, {{12, {0.5f, 0.0f, -0.25f}}}}};
  edited.head_parts[1] = "051176:Skyrim.esm";
  edited.geometry_mode = "preserve";
  edited.source = "RaceMenu: Carrot.jslot";
  draft.set_face(edited);

  test::TempDir dir;
  auto project = ProjectStore::create("Lydia", {});
  auto edit = draft.to_npc_edit();
  edit.face = to_face_config(draft.face(), recipe);
  CHECK(edit.face->head_parts[1].editor_id == "HairFemaleNord04");
  CHECK(edit.face->head_parts[1].type == "Hair");
  project.npcs.push_back(edit);
  const auto folder = dir.path() / "Lydia.snps";
  ProjectStore::save(folder, project);

  const auto loaded = ProjectStore::load(folder);
  CHECK(loaded.format_version == 3);
  REQUIRE(loaded.npcs.size() == 1);
  REQUIRE(loaded.npcs[0].face);
  REQUIRE(loaded.npcs[0].outfit);
  CHECK(loaded.npcs[0].face->complete);

  std::vector<std::string> notes;
  const auto restored = from_face_config(*loaded.npcs[0].face, face_state_from_recipe(recipe), recipe, &notes);
  CHECK(restored == edited);
  CHECK(notes.empty());

  // Saving again without face edits removes face.json: the old edits must not come back.
  project.npcs[0].face.reset();
  ProjectStore::save(folder, project);
  CHECK_FALSE(std::filesystem::exists(folder / "face.json"));
  CHECK_FALSE(ProjectStore::load(folder).npcs[0].face.has_value());
}

TEST_CASE("Format 2 face edits migrate on top of the record", "[appearance][projects]") {
  const auto recipe = lydia_recipe();
  const auto baseline = face_state_from_recipe(recipe);

  // What Character Creator 2.0 saved: one slider, a hair by type, the default weight 100.
  FaceConfig v2;
  v2.preset_source = "custom";
  v2.morph_sliders = {{"JawNarrowVsWide", 0.3f}, {"NotASlider", 1.0f}};
  v2.head_parts = {{"051176:Skyrim.esm", "HairFemaleNord04", "Hair", ""}, {"", "Brows?", "Eyebrows", ""}};
  v2.weight = 100.0f;
  v2.skin_tone = ColorRgb{180, 150, 140};

  // The record says weight 100 here, so use a different baseline weight to see it is preserved.
  auto lighter = baseline;
  lighter.values.weight = 70.0f;
  std::vector<std::string> notes;
  const auto migrated = from_face_config(v2, lighter, recipe, &notes);
  CHECK(migrated.values.sliders[3] == 0.3f);
  CHECK(migrated.values.sliders[0] == 0.1f);  // untouched sliders keep the record's value
  CHECK(migrated.values.weight == 70.0f);     // the default 100 is not applied
  CHECK(migrated.values.skin_tone == face::Rgb8{180, 150, 140});
  CHECK(std::count(migrated.head_parts.begin(), migrated.head_parts.end(), "051176:Skyrim.esm") == 1);
  CHECK(std::count(migrated.head_parts.begin(), migrated.head_parts.end(), "051193:Skyrim.esm") == 0);
  CHECK(std::count(migrated.head_parts.begin(), migrated.head_parts.end(), "040210:Skyrim.esm") == 1);
  CHECK(notes.size() == 3);  // unknown slider, part without FormKey, weight ignored

  v2.preset_source = "donor";
  CHECK(from_face_config(v2, lighter, recipe).values.weight == 100.0f);
}

TEST_CASE("Format 2 project files still load", "[appearance][projects]") {
  test::TempDir dir;
  const auto folder = dir.path() / "Old.snps";
  std::filesystem::create_directories(folder);
  auto write = [&](const char* name, const char* content) { file_io::write_file_atomic(folder / name, content); };
  write("project.json", R"({"format":"skyrim-npc-studio/project","formatVersion":2,"id":"abc","name":"Old"})");
  write("npc.json", R"({"formatVersion":2,"npcs":[{"formKey":"0A2C8E:Skyrim.esm","editorId":"HousecarlWhiterun"}]})");
  write("outfit.json", R"({"formatVersion":2,"outfits":{"0A2C8E:Skyrim.esm":{"pieces":[{"formKey":"013952:Skyrim.esm"}]}}})");
  write("face.json", R"({"formatVersion":2,"faces":{"0A2C8E:Skyrim.esm":{"morphSliders":{"NoseUpVsDown":0.5},"weight":100}}})");
  const auto p = ProjectStore::load(folder);
  CHECK(p.format_version == 2);
  REQUIRE(p.npcs.size() == 1);
  REQUIRE(p.npcs[0].outfit);
  CHECK(p.npcs[0].outfit->pieces.size() == 1);
  REQUIRE(p.npcs[0].face);
  CHECK_FALSE(p.npcs[0].face->complete);
  CHECK(p.npcs[0].face->morph_sliders.at("NoseUpVsDown") == 0.5f);
}
