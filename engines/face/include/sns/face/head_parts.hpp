#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sns::face {

enum class HeadPartType {
  Face,
  Hair,
  Eyes,
  Eyebrows,
  FacialHair,
  Scars,
  Misc
};

std::string_view head_part_type_to_string(HeadPartType type);
HeadPartType string_to_head_part_type(std::string_view str);

struct HeadPartInfo {
  std::string form_key;
  std::string editor_id;
  HeadPartType type = HeadPartType::Misc;
  std::string mesh_path;
  std::string plugin;
  std::string valid_for_sex = "any";  // "male", "female", "any"
  std::string valid_for_race;         // e.g. "NordRace", or empty for any

  [[nodiscard]] bool is_compatible(std::string_view race, std::string_view sex) const;
};

std::vector<HeadPartInfo> filter_head_parts(
    const std::vector<HeadPartInfo>& parts,
    HeadPartType type,
    std::string_view race = "",
    std::string_view sex = "");

}  // namespace sns::face
