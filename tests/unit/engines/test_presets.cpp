#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sns/presets/jslot_parser.hpp"
#include "sns/presets/preset_model.hpp"

using namespace sns;
using namespace sns::presets;

TEST_CASE("Presets: Parse RaceMenu jslot with header and parts", "[presets]") {
  const std::string jslot_sample = R"json({
    "header": {
      "name": "Astrid Valkyrie",
      "race": "NordRace",
      "gender": 1
    },
    "weight": 80.0,
    "height": 1.05,
    "hairColor": 16753920,
    "skinTone": 16766640,
    "headParts": [
      {
        "type": 2,
        "editorId": "HairFemaleNord01",
        "modName": "KS Hairdos.esp",
        "formId": 123456,
        "meshPath": "meshes\\actors\\character\\hair\\hair01.nif"
      },
      {
        "type": 3,
        "editorId": "FemaleEyesHumanBlue",
        "modName": "Skyrim.esm",
        "formId": "00012345",
        "meshPath": ""
      }
    ],
    "morphs": [
      {"name": "NoseLongVsShort", "value": 0.35},
      {"name": "JawNarrowVsWide", "value": -0.2}
    ],
    "tints": [
      {
        "index": 1,
        "interpolation": 0.8,
        "color": 16711680,
        "texture": "textures\\actors\\character\\face\\tint01.dds"
      }
    ]
  })json";

  auto item = parse_jslot(jslot_sample);
  CHECK(item.name == "Astrid Valkyrie");
  CHECK(item.race == "NordRace");
  CHECK(item.sex == "female");
  CHECK(item.weight == 80.0f);
  CHECK(item.height == 1.05f);
  REQUIRE(item.hair_color.has_value());
  CHECK(item.hair_color->r == 255);
  CHECK(item.head_parts.size() == 2);
  CHECK(item.head_parts[0].type == "Hair");
  CHECK(item.head_parts[0].plugin == "KS Hairdos.esp");
  CHECK(item.head_parts[0].form_id == "0001E240");
  CHECK(item.morphs.size() == 2);
  CHECK(item.morphs["NoseLongVsShort"] == 0.35f);
  CHECK(item.tints.size() == 1);
  CHECK(item.tints[0].color.r == 255);
  CHECK(item.tints[0].alpha == 0.8f);

  // Conversion to FaceConfig
  auto face_cfg = to_face_config(item);
  CHECK(face_cfg.preset_name == "Astrid Valkyrie");
  CHECK(face_cfg.head_parts.size() == 2);
  CHECK(face_cfg.head_parts[0].editor_id == "HairFemaleNord01");
  CHECK(face_cfg.tint_layers.size() == 1);
}

TEST_CASE("Presets: Evaluate compatibility against target NPC", "[presets]") {
  PresetItem preset;
  preset.name = "Nord Beauty";
  preset.race = "NordRace";
  preset.sex = "female";
  preset.dependencies = {"KS Hairdos.esp", "Brows.esp"};

  projects::NpcTarget target;
  target.name = "Lydia";
  target.form_key = "0A2C94:Skyrim.esm";

  SECTION("Compatible when sex matches and dependencies are active") {
    std::vector<std::string> active = {"Skyrim.esm", "Update.esm", "KS Hairdos.esp", "Brows.esp"};
    auto rep = evaluate_compatibility(preset, target, "NordRace", "female", active);
    CHECK(rep.compatible);
    CHECK(rep.exportable);
    CHECK(rep.missing_dependencies.empty());
    CHECK(rep.issues.empty());
  }

  SECTION("Incompatible when sex does not match") {
    std::vector<std::string> active = {"KS Hairdos.esp", "Brows.esp"};
    auto rep = evaluate_compatibility(preset, target, "NordRace", "male", active);
    CHECK_FALSE(rep.compatible);
    CHECK_FALSE(rep.exportable);
    CHECK_FALSE(rep.issues.empty());
  }

  SECTION("Non-exportable when mod dependencies are missing") {
    std::vector<std::string> active = {"Skyrim.esm"};
    auto rep = evaluate_compatibility(preset, target, "NordRace", "female", active);
    CHECK(rep.compatible);
    CHECK_FALSE(rep.exportable);
    CHECK(rep.missing_dependencies.size() == 2);
  }

  SECTION("Warns and prevents full export on sculpt data") {
    preset.has_sculpt_data = true;
    std::vector<std::string> active = {"KS Hairdos.esp", "Brows.esp"};
    auto rep = evaluate_compatibility(preset, target, "NordRace", "female", active);
    CHECK(rep.compatible);
    CHECK_FALSE(rep.non_exportable_features.empty());
  }
}
