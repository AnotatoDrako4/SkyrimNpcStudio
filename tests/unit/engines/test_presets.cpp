// RaceMenu .jslot parsing and mapping onto an NPC. The sample mirrors the structure RaceMenu
// (skee64, formatVersion 3) writes on SSE/AE; the values are synthetic.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sns/core/error.hpp"
#include "sns/presets/jslot_parser.hpp"
#include "sns/presets/preset_model.hpp"

using namespace sns;
using namespace sns::presets;
using Catch::Matchers::WithinAbs;

namespace {

const char* kJslot = R"json({
  "actor": {"hairColor": 13333071, "headTexture": "Skyrim.esm|03B522", "weight": 50},
  "bodyMorphs": [{"keys": [{"key": "XPMSE.esp", "value": 1}], "name": "XPMSEAABase_2hweqp"}],
  "faceTextures": [{"index": 0, "texture": "Actors\\Character\\Female\\FemaleHead.dds"}],
  "headParts": [
    {"formId": 333071, "formIdentifier": "Skyrim.esm|05150F", "type": 0},
    {"formId": 2566, "formIdentifier": "High Poly Head.esm|000A06", "type": 1},
    {"formId": 1, "formIdentifier": "KSHairdosSMP.esp|016893", "type": 3},
    {"formId": 2, "formIdentifier": "broken", "type": 9}
  ],
  "modNames": ["High Poly Head.esm", "KSHairdosSMP.esp", "Skyrim.esm"],
  "mods": [{"index": 0, "name": "Skyrim.esm"}, {"index": 1, "name": "High Poly Head.esm"}],
  "morphs": {
    "custom": [{"name": "CME_BrowInOut", "value": 0.25}, {"name": "ECE_Zero", "value": 0.0}],
    "default": {"morphs": [0.3, 0, -0.2, 0, 0, 0.1, 0, 0, 0, 0, 0, 0, 0.5, 0, 0, -0.4, 0, 0, 3.4e38],
                "presets": [10, 4294967295, 16, 11]},
    "sculpt": [{"data": [[399, 97, 1652, -294], [400, 0, 0, 10000]], "host": "KL\\High Poly Head\\FemaleHeadCharGen.tri", "vertices": 3832}],
    "sculptDivisor": 10000
  },
  "overrides": [{"node": "Body [Ovl0]", "values": []}],
  "tintInfo": [
    {"color": 4294960870, "index": 0, "texture": "Actors\\Character\\Character Assets\\TintMasks\\SkinTone.dds"},
    {"color": 2618428425, "index": 1, "texture": "Actors\\Character\\Character Assets\\TintMasks\\FemaleUpperEyeSocket.dds"},
    {"color": 4278190335, "index": 2, "texture": "!COR\\TintMasks\\custom.dds"}
  ],
  "transforms": [{"firstPerson": false, "keys": [], "node": "NPC Head [Head]"}],
  "version": {"formatVersion": 3, "runtimeVersion": 17039392, "signature": 1163086675, "skseVersion": 33554736}
})json";

}  // namespace

TEST_CASE("RaceMenu .jslot files are parsed as RaceMenu writes them", "[presets]") {
  const auto p = parse_jslot(kJslot, "Carrot");
  CHECK(p.name == "Carrot");
  CHECK(p.format_version == 3);
  REQUIRE(p.weight);
  CHECK(*p.weight == 50.0f);
  REQUIRE(p.hair_color);
  CHECK(*p.hair_color == face::Rgb8{0xCB, 0x72, 0x4F});  // 13333071 = 0x00CB724F
  CHECK(p.head_texture == "03B522:Skyrim.esm");

  REQUIRE(p.head_parts.size() == 4);
  CHECK(p.head_parts[0].form_key == "05150F:Skyrim.esm");
  CHECK(p.head_parts[1].form_key == "000A06:High Poly Head.esm");
  CHECK(p.head_parts[2].slot == 3);  // RaceMenu slot, not the HDPT type
  CHECK(p.head_parts[3].form_key.empty());

  REQUIRE(p.has_sliders);
  CHECK_THAT(p.sliders[0], WithinAbs(0.3, 1e-6));
  CHECK_THAT(p.sliders[15], WithinAbs(-0.4, 1e-6));
  REQUIRE(p.has_presets);
  CHECK(p.presets == std::array<std::int32_t, 4>{10, -1, 16, 11});

  CHECK(p.custom_morphs.size() == 1);  // zero-valued custom morphs are dropped
  CHECK(p.custom_morphs.at("CME_BrowInOut") == 0.25f);
  REQUIRE(p.sculpt.size() == 1);
  CHECK(p.sculpt[0].vertex_count == 3832);
  REQUIRE(p.sculpt[0].deltas.size() == 2);
  CHECK(p.sculpt[0].deltas[0].first == 399);
  CHECK_THAT(p.sculpt[0].deltas[0].second[1], WithinAbs(0.1652, 1e-6));
  CHECK_THAT(p.sculpt[0].deltas[1].second[2], WithinAbs(1.0, 1e-6));

  REQUIRE(p.tints.size() == 3);
  CHECK(p.tints[1].color == face::Rgb8{0x12, 0x0C, 0x09});  // 2618428425 = 0x9C120C09
  CHECK_THAT(p.tints[1].value, WithinAbs(0x9C / 255.0, 1e-6));
  CHECK(p.overrides == 1);
  CHECK(p.transforms == 1);
  CHECK(p.body_morphs == 1);
  CHECK(p.plugins == std::vector<std::string>{"High Poly Head.esm", "KSHairdosSMP.esp", "Skyrim.esm"});
  CHECK(p.warnings.size() == 1);

  CHECK_THROWS_AS(parse_jslot("{not json"), sns::Error);
  CHECK_THROWS_AS(parse_jslot(R"({"something": 1})"), sns::Error);
  CHECK(form_key_from_identifier("Dawnguard.esm|02006F97") == "006F97:Dawnguard.esm");
  CHECK(form_key_from_identifier("Skyrim.esm|zz").empty());
}

TEST_CASE("A RaceMenu preset maps onto the NPC record and FaceGen inputs", "[presets]") {
  const auto preset = parse_jslot(kJslot, "Carrot");
  face::FaceRecipe target;
  target.tint_masks = {{24, "textures\\Actors\\Character\\Character Assets\\TintMasks\\SkinTone.dds", 6},
                       {26, "textures\\Actors\\Character\\Character Assets\\TintMasks\\FemaleUpperEyeSocket.dds", 4}};
  appearance::FaceState baseline;
  baseline.values.weight = 100;
  baseline.head_parts = {"040210:Skyrim.esm"};
  baseline.values.tints = {{24, {200, 200, 200}, 1.0f, 0}};

  const auto m = map_preset(preset, baseline, target);
  const auto& v = m.state.values;
  CHECK(m.state.source == "RaceMenu: Carrot");
  CHECK(v.weight == 50.0f);
  CHECK_THAT(v.sliders[0], WithinAbs(0.3, 1e-6));
  CHECK(v.presets[0] == 10);
  CHECK(m.state.head_texture == "03B522:Skyrim.esm");
  CHECK(m.state.head_parts == std::vector<std::string>{"05150F:Skyrim.esm", "000A06:High Poly Head.esm", "016893:KSHairdosSMP.esp"});
  // Tints are matched to the race masks by texture; the skin tone layer also sets QNAM.
  CHECK(m.tints_matched == 2);
  CHECK(m.tints_skipped == 1);
  REQUIRE(v.tints.size() == 2);
  CHECK(v.tints[0].index == 24);
  CHECK(v.tints[1].index == 26);
  CHECK(v.skin_tone == face::Rgb8{0xFF, 0xE6, 0xE6});  // 4294960870 = 0xFFFFE6E6
  CHECK(v.custom_morphs.size() == 1);
  CHECK(v.sculpt.size() == 1);
  CHECK(m.runtime_only.size() == 4);  // overrides, transforms, body morphs, face textures
  CHECK_FALSE(m.notes.empty());       // the unmatched mask and the unreadable head part
}

TEST_CASE("Head parts from light (ESL) plugins use their 12-bit record ID", "[presets]") {
  // Carrot.jslot as RaceMenu writes it: KSHairdosSMP.esp is light, runtime FormID 0xFE016893.
  const auto preset = parse_jslot(R"({
    "version": {"formatVersion": 4},
    "headParts": [
      {"formId": 4261505171, "formIdentifier": "KSHairdosSMP.esp|016893", "type": 3},
      {"formId": 333071, "formIdentifier": "Skyrim.esm|05150F", "type": 1}
    ]
  })", "Carrot");
  REQUIRE(preset.head_parts.size() == 2);
  CHECK(preset.head_parts[0].form_key == "000893:KSHairdosSMP.esp");
  CHECK(preset.head_parts[1].form_key == "05150F:Skyrim.esm");
}
