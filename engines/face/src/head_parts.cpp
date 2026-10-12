#include "sns/face/head_parts.hpp"

#include "sns/core/text.hpp"

namespace sns::face {

std::string_view head_part_type_to_string(HeadPartType type) {
  switch (type) {
    case HeadPartType::Face: return "Face";
    case HeadPartType::Hair: return "Hair";
    case HeadPartType::Eyes: return "Eyes";
    case HeadPartType::Eyebrows: return "Eyebrows";
    case HeadPartType::FacialHair: return "FacialHair";
    case HeadPartType::Scars: return "Scars";
    case HeadPartType::Misc: return "Misc";
  }
  return "Misc";
}

HeadPartType string_to_head_part_type(std::string_view str) {
  if (text::iequals_ascii(str, "Face")) return HeadPartType::Face;
  if (text::iequals_ascii(str, "Hair")) return HeadPartType::Hair;
  if (text::iequals_ascii(str, "Eyes")) return HeadPartType::Eyes;
  if (text::iequals_ascii(str, "Eyebrows") || text::iequals_ascii(str, "Brows")) return HeadPartType::Eyebrows;
  if (text::iequals_ascii(str, "FacialHair") || text::iequals_ascii(str, "Beard")) return HeadPartType::FacialHair;
  if (text::iequals_ascii(str, "Scars") || text::iequals_ascii(str, "Scar")) return HeadPartType::Scars;
  return HeadPartType::Misc;
}

bool HeadPartInfo::is_compatible(std::string_view race, std::string_view sex) const {
  if (!valid_for_sex.empty() && !text::iequals_ascii(valid_for_sex, "any") && !sex.empty()) {
    if (!text::iequals_ascii(valid_for_sex, sex)) {
      return false;
    }
  }

  if (!valid_for_race.empty() && !race.empty()) {
    if (!text::iequals_ascii(valid_for_race, race)) {
      return false;
    }
  }

  return true;
}

std::vector<HeadPartInfo> filter_head_parts(
    const std::vector<HeadPartInfo>& parts,
    HeadPartType type,
    std::string_view race,
    std::string_view sex) {
  std::vector<HeadPartInfo> result;
  for (const auto& hp : parts) {
    if (hp.type != type) continue;
    if (!hp.is_compatible(race, sex)) continue;
    result.push_back(hp);
  }
  return result;
}

}  // namespace sns::face
