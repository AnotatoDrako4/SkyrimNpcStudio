#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace sns::creation {

struct NativeInventoryItem { std::string form_key; int count = 1; };
struct NativeFaction { std::string form_key; int rank = 0; };
struct NativePlacement {
  std::string cell_form_key;
  float x = 0, y = 0, z = 0, rotation_z = 0;
};

// Creation-only model. No existing NPC identity, template or mutable editor state.
struct NativeNpcDraft {
  std::string name, editor_id, description;
  std::string race_form_key, voice_form_key, class_form_key, combat_style_form_key;
  bool female = false;
  float height = 1, weight = 50;
  int level = 1, min_level = 1, max_level = 100;
  std::optional<float> level_multiplier;
  int health = 100, magicka = 100, stamina = 100;
  std::map<std::string, int> skills;
  std::vector<std::string> head_part_form_keys, outfit_armor_form_keys, package_form_keys;
  std::vector<std::string> spell_form_keys, perk_form_keys;
  std::string hair_color_form_key, head_texture_form_key;
  // Structured FaceValuesDto: 18 sliders, four presets, skinTone and tints. Persisted
  // unchanged; worker validates against Bethesda records before technical export.
  nlohmann::json face = nullptr;
  std::vector<NativeInventoryItem> inventory;
  std::vector<NativeFaction> factions;
  std::optional<NativePlacement> placement;
};

void validate_native_draft(const NativeNpcDraft& draft);
void validate_export_options(const nlohmann::json& options);
nlohmann::json native_npc_to_json(const NativeNpcDraft& draft);
NativeNpcDraft native_npc_from_json(const nlohmann::json& json);

}  // namespace sns::creation
