#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/face/face_recipe.hpp"

namespace sns::presets {

// A RaceMenu preset (.jslot, "formatVersion" 3 as written by RaceMenu/skee64 for SSE/AE). Only
// what the file really contains is represented; nothing is invented when a field is absent.
struct JslotHeadPart {
  std::string form_identifier;  // "Skyrim.esm|05150F" as stored
  std::string form_key;         // "05150F:Skyrim.esm" (empty when the identifier is malformed)
  std::string plugin;
  int slot = 0;                 // RaceMenu's index in the actor's head part array (NOT the HDPT type)
};

struct JslotTint {
  int index = 0;            // RaceMenu tint slot (not the race TINI)
  std::string texture;      // mask path as stored ("Actors\...\SkinTone.dds")
  face::Rgb8 color;
  float value = 0.0f;       // alpha byte / 255
};

struct RaceMenuPreset {
  std::string name;
  std::string file_path;
  int format_version = 0;
  std::optional<float> weight;
  std::optional<face::Rgb8> hair_color;
  std::string head_texture;  // FormKey of the face texture set (FTST), "" when absent
  std::vector<JslotHeadPart> head_parts;
  bool has_sliders = false;
  std::array<float, face::kSliderCount> sliders{};  // morphs.default.morphs[0..17] (NAM9)
  bool has_presets = false;
  std::array<std::int32_t, face::kPresetSlotCount> presets{{-1, -1, -1, -1}};  // morphs.default.presets (NAMA)
  std::map<std::string, float> custom_morphs;      // morphs.custom
  std::vector<face::SculptTarget> sculpt;          // morphs.sculpt, already divided by sculptDivisor
  std::vector<JslotTint> tints;
  std::vector<std::string> face_textures;          // faceTextures[].texture (RaceMenu runtime override)
  std::vector<std::string> plugins;                // every plugin the preset names, sorted, unique
  // RaceMenu/NiOverride runtime data: applied by RaceMenu in game, not part of an NPC record.
  std::size_t overrides = 0, transforms = 0, body_morphs = 0;
  std::vector<std::string> warnings;               // fields that were present but unreadable
};

// Throws sns::Error(Parse) when the text is not JSON or not a RaceMenu preset at all.
RaceMenuPreset parse_jslot(std::string_view json_text, std::string name = {});

// "Skyrim.esm|05150F" -> "05150F:Skyrim.esm"; empty for anything else.
std::string form_key_from_identifier(std::string_view identifier);

}  // namespace sns::presets
