#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sns/projects/project.hpp"

namespace sns::presets {

struct PresetHeadPart {
  std::string form_id;
  std::string plugin;
  std::string editor_id;
  std::string type;
  std::string mesh_path;
};

struct PresetTint {
  int index = 0;
  projects::ColorRgb color{255, 255, 255};
  float alpha = 1.0f;
  std::string texture_path;
};

struct PresetItem {
  std::string id;
  std::string name;
  std::string description;
  std::string source;  // "jslot", "donor", "snps"
  std::string file_path;
  std::string donor_npc_form_key;
  std::string donor_plugin;
  std::string donor_facegen_mesh;
  std::string donor_facegen_tint;
  std::string race;
  std::string sex;  // "female", "male", "any"
  float weight = 100.0f;
  float height = 1.0f;
  std::optional<projects::ColorRgb> hair_color;
  std::optional<projects::ColorRgb> skin_tone;
  std::vector<PresetHeadPart> head_parts;
  std::map<std::string, float> morphs;
  std::vector<PresetTint> tints;
  std::vector<std::string> dependencies;
  bool has_sculpt_data = false;
  std::string thumbnail_path;
};

struct CompatibilityReport {
  bool compatible = true;
  bool exportable = true;
  std::vector<std::string> issues;
  std::vector<std::string> missing_dependencies;
  std::vector<std::string> non_exportable_features;
};

projects::FaceConfig to_face_config(const PresetItem& preset, const std::string& donor_plugin = "");

}  // namespace sns::presets
