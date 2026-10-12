#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace sns::face {

enum class MorphCategory {
  Nose,
  Jaw,
  Cheeks,
  Eyes,
  Brows,
  Lips,
  Chin,
  Custom
};

struct MorphSliderDef {
  std::string key;
  MorphCategory category;
  std::string display_name_es;
  std::string display_name_en;
  float min_value = -1.0f;
  float max_value = 1.0f;
  float default_value = 0.0f;

  [[nodiscard]] float clamp(float value) const noexcept {
    return std::clamp(value, min_value, max_value);
  }
};

const std::vector<MorphSliderDef>& get_standard_face_morphs();
const MorphSliderDef* find_morph_def(std::string_view key);
float clamp_morph_value(std::string_view key, float value);
std::string_view morph_category_name(MorphCategory category);

}  // namespace sns::face
