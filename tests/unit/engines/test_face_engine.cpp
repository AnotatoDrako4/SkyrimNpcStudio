#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sns/face/face_morphs.hpp"
#include "sns/face/head_parts.hpp"

using namespace sns::face;

TEST_CASE("Face Engine: Standard face morph definitions and clamping", "[face]") {
  const auto& morphs = get_standard_face_morphs();
  CHECK_FALSE(morphs.empty());

  const auto* nose_def = find_morph_def("NoseLongVsShort");
  REQUIRE(nose_def != nullptr);
  CHECK(nose_def->category == MorphCategory::Nose);
  CHECK(nose_def->min_value == -1.0f);
  CHECK(nose_def->max_value == 1.0f);

  // Clamping within bounds
  CHECK(clamp_morph_value("NoseLongVsShort", 0.5f) == 0.5f);
  CHECK(clamp_morph_value("NoseLongVsShort", 2.5f) == 1.0f);
  CHECK(clamp_morph_value("NoseLongVsShort", -3.0f) == -1.0f);

  // Unknown morph clamps to default [-1, 1]
  CHECK(clamp_morph_value("UnknownCustomMorph", 1.8f) == 1.0f);
  CHECK(clamp_morph_value("UnknownCustomMorph", -1.8f) == -1.0f);

  CHECK(morph_category_name(MorphCategory::Chin) == "Chin");
}

TEST_CASE("Face Engine: Head part types, compatibility, and filtering", "[face]") {
  CHECK(string_to_head_part_type("Hair") == HeadPartType::Hair);
  CHECK(string_to_head_part_type("Eyes") == HeadPartType::Eyes);
  CHECK(string_to_head_part_type("Brows") == HeadPartType::Eyebrows);
  CHECK(string_to_head_part_type("Beard") == HeadPartType::FacialHair);
  CHECK(head_part_type_to_string(HeadPartType::Hair) == "Hair");

  HeadPartInfo hair1{
      .form_key = "012345:Skyrim.esm",
      .editor_id = "HairFemaleNord01",
      .type = HeadPartType::Hair,
      .mesh_path = "meshes\\hair1.nif",
      .plugin = "Skyrim.esm",
      .valid_for_sex = "female",
      .valid_for_race = "NordRace",
  };

  HeadPartInfo hair2{
      .form_key = "012346:Skyrim.esm",
      .editor_id = "HairMaleNord01",
      .type = HeadPartType::Hair,
      .mesh_path = "meshes\\hair2.nif",
      .plugin = "Skyrim.esm",
      .valid_for_sex = "male",
      .valid_for_race = "NordRace",
  };

  HeadPartInfo eyes1{
      .form_key = "012347:Skyrim.esm",
      .editor_id = "EyesHumanBlue",
      .type = HeadPartType::Eyes,
      .mesh_path = "meshes\\eyes1.nif",
      .plugin = "Skyrim.esm",
      .valid_for_sex = "any",
      .valid_for_race = "",
  };

  std::vector<HeadPartInfo> catalog = {hair1, hair2, eyes1};

  SECTION("Filtering hair for female Nord") {
    auto filtered = filter_head_parts(catalog, HeadPartType::Hair, "NordRace", "female");
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].editor_id == "HairFemaleNord01");
  }

  SECTION("Filtering hair for male Nord") {
    auto filtered = filter_head_parts(catalog, HeadPartType::Hair, "NordRace", "male");
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].editor_id == "HairMaleNord01");
  }

  SECTION("Filtering eyes compatible with any sex/race") {
    auto filtered = filter_head_parts(catalog, HeadPartType::Eyes, "BretonRace", "female");
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].editor_id == "EyesHumanBlue");
  }
}
