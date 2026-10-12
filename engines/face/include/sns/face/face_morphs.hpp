#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
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

// One slider of an NPC_ record's NAM9 ("Face Morph"). The record stores 19 floats: 18 sliders in
// [-1, 1] in the order below, then an unused value (the Creation Kit writes FLT_MAX).
//
// The Creation Kit bakes a slider into FaceGen by adding the chargen TRI morph `positive_morph`
// with weight v when v > 0, or `negative_morph` with weight -v when v < 0. The pairs, including
// the inverted Jaw/Chin "Up vs Down" sliders, were measured on vanilla FaceGen (see
// docs/FACEGEN.md): they are data, not guesses.
struct MorphSliderDef {
  std::string key;  // NAM9 field name as Mutagen/xEdit call it, e.g. "NoseLongVsShort"
  std::size_t index = 0;
  MorphCategory category = MorphCategory::Custom;
  std::string display_name_es;
  std::string display_name_en;
  std::string positive_morph;
  std::string negative_morph;
  float min_value = -1.0f;
  float max_value = 1.0f;
  float default_value = 0.0f;

  [[nodiscard]] float clamp(float value) const noexcept { return std::clamp(value, min_value, max_value); }
};

inline constexpr std::size_t kSliderCount = 18;
inline constexpr std::size_t kNam9Count = 19;

// The 18 sliders in NAM9 order.
const std::vector<MorphSliderDef>& get_standard_face_morphs();
const MorphSliderDef* find_morph_def(std::string_view key);
float clamp_morph_value(std::string_view key, float value);
std::string_view morph_category_name(MorphCategory category);

// NAMA ("Face Parts"): four preset indices. Slot n > 0 selects the chargen morph
// "<prefix><n>" with weight 1 (e.g. NoseType16); 0 and negative values select nothing. The second
// slot is unused by the Creation Kit (vanilla records hold -1 or 0 there).
struct FacePresetSlotDef {
  std::size_t index = 0;
  std::string key;     // "Nose", "Unknown", "Eyes", "Mouth"
  std::string prefix;  // chargen morph prefix; empty for the unused slot
  std::string display_name_es;
  std::string display_name_en;
};
inline constexpr std::size_t kPresetSlotCount = 4;
const std::array<FacePresetSlotDef, kPresetSlotCount>& face_preset_slots();

}  // namespace sns::face
