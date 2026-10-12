#include "sns/face/face_morphs.hpp"

namespace sns::face {

namespace {

const std::vector<MorphSliderDef> kStandardMorphs = {
    // Nose
    {"NoseLongVsShort", MorphCategory::Nose, "Longitud de la nariz", "Nose Length", -1.0f, 1.0f, 0.0f},
    {"NoseUpVsDown", MorphCategory::Nose, "Altura de la nariz", "Nose Height", -1.0f, 1.0f, 0.0f},

    // Jaw
    {"JawUpVsDown", MorphCategory::Jaw, "Altura de la mandíbula", "Jaw Height", -1.0f, 1.0f, 0.0f},
    {"JawNarrowVsWide", MorphCategory::Jaw, "Anchura de la mandíbula", "Jaw Width", -1.0f, 1.0f, 0.0f},
    {"JawForwardVsBack", MorphCategory::Jaw, "Profundidad de mandíbula", "Jaw Depth", -1.0f, 1.0f, 0.0f},

    // Cheeks
    {"CheeksUpVsDown", MorphCategory::Cheeks, "Altura de mejillas", "Cheeks Height", -1.0f, 1.0f, 0.0f},
    {"CheeksForwardVsBack", MorphCategory::Cheeks, "Prominencia de mejillas", "Cheeks Depth", -1.0f, 1.0f, 0.0f},

    // Eyes
    {"EyesUpVsDown", MorphCategory::Eyes, "Altura de ojos", "Eyes Height", -1.0f, 1.0f, 0.0f},
    {"EyesInVsOut", MorphCategory::Eyes, "Separación de ojos", "Eyes Separation", -1.0f, 1.0f, 0.0f},
    {"EyesForwardVsBack", MorphCategory::Eyes, "Profundidad de ojos", "Eyes Depth", -1.0f, 1.0f, 0.0f},

    // Brows
    {"BrowsUpVsDown", MorphCategory::Brows, "Altura de cejas", "Brows Height", -1.0f, 1.0f, 0.0f},
    {"BrowsInVsOut", MorphCategory::Brows, "Separación de cejas", "Brows Separation", -1.0f, 1.0f, 0.0f},
    {"BrowsForwardVsBack", MorphCategory::Brows, "Profundidad de cejas", "Brows Depth", -1.0f, 1.0f, 0.0f},

    // Lips
    {"LipsUpVsDown", MorphCategory::Lips, "Altura de labios", "Lips Height", -1.0f, 1.0f, 0.0f},
    {"LipsInVsOut", MorphCategory::Lips, "Profundidad de labios", "Lips Depth", -1.0f, 1.0f, 0.0f},

    // Chin
    {"ChinNarrowVsWide", MorphCategory::Chin, "Anchura de la barbilla", "Chin Width", -1.0f, 1.0f, 0.0f},
    {"ChinUpVsDown", MorphCategory::Chin, "Altura de la barbilla", "Chin Height", -1.0f, 1.0f, 0.0f},
    {"ChinUnderbiteVsOverbite", MorphCategory::Chin, "Prognatismo de barbilla", "Chin Underbite", -1.0f, 1.0f, 0.0f},
};

}  // namespace

const std::vector<MorphSliderDef>& get_standard_face_morphs() {
  return kStandardMorphs;
}

const MorphSliderDef* find_morph_def(std::string_view key) {
  for (const auto& def : kStandardMorphs) {
    if (def.key == key) {
      return &def;
    }
  }
  return nullptr;
}

float clamp_morph_value(std::string_view key, float value) {
  if (const auto* def = find_morph_def(key)) {
    return def->clamp(value);
  }
  return std::clamp(value, -1.0f, 1.0f);
}

std::string_view morph_category_name(MorphCategory category) {
  switch (category) {
    case MorphCategory::Nose: return "Nose";
    case MorphCategory::Jaw: return "Jaw";
    case MorphCategory::Cheeks: return "Cheeks";
    case MorphCategory::Eyes: return "Eyes";
    case MorphCategory::Brows: return "Brows";
    case MorphCategory::Lips: return "Lips";
    case MorphCategory::Chin: return "Chin";
    case MorphCategory::Custom: return "Custom";
  }
  return "Unknown";
}

}  // namespace sns::face
