#include <catch2/catch_test_macros.hpp>

#include "sns/outfit/outfit_presets.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::outfit;

TEST_CASE("Outfit presets: builtin presets contain standard archetypes", "[outfit][presets]") {
  const auto presets = builtin_outfit_presets();
  REQUIRE(presets.size() >= 5);

  const auto warrior = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "nordic_warrior"; });
  REQUIRE(warrior != presets.end());
  CHECK(warrior->name == "Guerrera Nórdica");
  CHECK(warrior->pieces.size() >= 3);
  CHECK(warrior->category == "Warrior");

  const auto mage = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "winterhold_mage"; });
  REQUIRE(mage != presets.end());
  CHECK(mage->name == "Maga de Invernalia");
  CHECK(mage->category == "Mage");
}

TEST_CASE("Outfit presets: save and load custom preset from directory", "[outfit][presets]") {
  test::TempDir dir;
  OutfitPreset custom{
      .id = "custom_scout",
      .name = "Exploradora de Whiterun",
      .category = "Scout",
      .description = "Conjunto ligero para patrullas en la estepa.",
      .pieces = {
          Piece{.form_key = "013922:Skyrim.esm", .name = "Leather Armor", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
          Piece{.form_key = "013920:Skyrim.esm", .name = "Leather Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
      },
  };

  const auto file_path = dir.path() / "custom_scout.json";
  CHECK(save_outfit_preset_to_file(file_path, custom));

  const auto loaded = load_outfit_presets_from_dir(dir.path());
  const auto found = std::find_if(loaded.begin(), loaded.end(), [](const auto& p) { return p.id == "custom_scout"; });
  REQUIRE(found != loaded.end());
  CHECK(found->name == "Exploradora de Whiterun");
  CHECK(found->pieces.size() == 2);
  CHECK(found->pieces[0].form_key == "013922:Skyrim.esm");
}

TEST_CASE("Biped slot helpers: display names and category classification", "[outfit][slots]") {
  CHECK(slot_bit(30) == (1u << 0));
  CHECK(slot_bit(32) == (1u << 2));
  CHECK(slot_bit(37) == (1u << 7));
  CHECK(slot_bit(47) == (1u << 17));

  CHECK(slot_category_name(30) == "Cabeza");
  CHECK(slot_category_name(32) == "Torso");
  CHECK(slot_category_name(33) == "Manos");
  CHECK(slot_category_name(37) == "Pies");
  CHECK(slot_category_name(47) == "Capa");
  CHECK(slot_category_name(35) == "Joyería");

  CHECK(slot_display_name(32).find("Cuerpo") != std::string::npos);
  CHECK(slot_display_name(47).find("Capa") != std::string::npos);
}
