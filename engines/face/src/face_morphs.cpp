#include "sns/face/face_morphs.hpp"

namespace sns::face {

namespace {

MorphSliderDef slider(std::size_t index, std::string key, MorphCategory category, std::string es, std::string en,
                      std::string positive, std::string negative) {
  MorphSliderDef def;
  def.key = std::move(key);
  def.index = index;
  def.category = category;
  def.display_name_es = std::move(es);
  def.display_name_en = std::move(en);
  def.positive_morph = std::move(positive);
  def.negative_morph = std::move(negative);
  return def;
}

const std::vector<MorphSliderDef>& table() {
  static const std::vector<MorphSliderDef> kSliders = {
      slider(0, "NoseLongVsShort", MorphCategory::Nose, "Nariz: larga / corta", "Nose: long / short", "NoseLong", "NoseShort"),
      slider(1, "NoseUpVsDown", MorphCategory::Nose, "Nariz: arriba / abajo", "Nose: up / down", "NoseUp", "NoseDown"),
      slider(2, "JawUpVsDown", MorphCategory::Jaw, "Mandíbula: abajo / arriba", "Jaw: down / up", "JawDown", "JawUp"),
      slider(3, "JawNarrowVsWide", MorphCategory::Jaw, "Mandíbula: ancha / estrecha", "Jaw: wide / narrow", "JawWide", "JawNarrow"),
      slider(4, "JawForwardVsBack", MorphCategory::Jaw, "Mandíbula: adelante / atrás", "Jaw: forward / back", "JawForward", "JawBack"),
      slider(5, "CheeksUpVsDown", MorphCategory::Cheeks, "Pómulos: arriba / abajo", "Cheeks: up / down", "CheeksUp", "CheeksDown"),
      slider(6, "CheeksForwardVsBack", MorphCategory::Cheeks, "Pómulos: hacia fuera / dentro", "Cheeks: out / in", "CheeksOut", "CheeksIn"),
      slider(7, "EyesUpVsDown", MorphCategory::Eyes, "Ojos: arriba / abajo", "Eyes: up / down", "EyesMoveUp", "EyesMoveDown"),
      slider(8, "EyesInVsOut", MorphCategory::Eyes, "Ojos: separados / juntos", "Eyes: out / in", "EyesMoveOut", "EyesMoveIn"),
      slider(9, "BrowsUpVsDown", MorphCategory::Brows, "Cejas: arriba / abajo", "Brows: up / down", "BrowUp", "BrowDown"),
      slider(10, "BrowsInVsOut", MorphCategory::Brows, "Cejas: separadas / juntas", "Brows: out / in", "BrowOut", "BrowIn"),
      slider(11, "BrowsForwardVsBack", MorphCategory::Brows, "Cejas: adelante / atrás", "Brows: forward / back", "BrowForward", "BrowBack"),
      slider(12, "LipsUpVsDown", MorphCategory::Lips, "Labios: arriba / abajo", "Lips: up / down", "LipMoveUp", "LipMoveDown"),
      slider(13, "LipsInVsOut", MorphCategory::Lips, "Labios: hacia fuera / dentro", "Lips: out / in", "LipMoveOut", "LipMoveIn"),
      slider(14, "ChinNarrowVsWide", MorphCategory::Chin, "Barbilla: ancha / estrecha", "Chin: wide / thin", "ChinWide", "ChinThin"),
      slider(15, "ChinUpVsDown", MorphCategory::Chin, "Barbilla: abajo / arriba", "Chin: down / up", "ChinMoveDown", "ChinMoveUp"),
      slider(16, "ChinUnderbiteVsOverbite", MorphCategory::Chin, "Mordida: prognatismo / retrognatismo", "Bite: underbite / overbite", "Underbite", "Overbite"),
      slider(17, "EyesForwardVsBack", MorphCategory::Eyes, "Ojos: adelante / atrás", "Eyes: forward / back", "EyesForward", "EyesBack"),
  };
  return kSliders;
}

}  // namespace

const std::vector<MorphSliderDef>& get_standard_face_morphs() { return table(); }

const MorphSliderDef* find_morph_def(std::string_view key) {
  for (const auto& def : table()) {
    if (def.key == key) return &def;
  }
  return nullptr;
}

float clamp_morph_value(std::string_view key, float value) {
  if (const auto* def = find_morph_def(key)) return def->clamp(value);
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

const std::array<FacePresetSlotDef, kPresetSlotCount>& face_preset_slots() {
  static const std::array<FacePresetSlotDef, kPresetSlotCount> kSlots = {{
      {0, "Nose", "NoseType", "Forma de nariz", "Nose type"},
      {1, "Unknown", "", "(sin uso)", "(unused)"},
      {2, "Eyes", "EyesType", "Forma de ojos", "Eyes type"},
      {3, "Mouth", "LipType", "Forma de labios", "Lips type"},
  }};
  return kSlots;
}

}  // namespace sns::face
