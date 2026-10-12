#include "sns/presets/preset_model.hpp"

#include <format>

#include "sns/core/text.hpp"

namespace sns::presets {

namespace {

// "Actors\Character\...\X.dds" (RaceMenu) and "textures\Actors\...\X.dds" (race TINT) are the same mask.
std::string mask_key(std::string_view path) {
  auto key = text::data_path_key(path);
  if (key.rfind("textures\\", 0) == 0) key.erase(0, 9);
  return key;
}

bool is_skin_tone_mask(std::string_view path) { return text::ends_with_icase(path, "skintone.dds"); }

}  // namespace

PresetMapping map_preset(const RaceMenuPreset& preset, const appearance::FaceState& baseline, const face::FaceRecipe& target) {
  PresetMapping out;
  auto& s = out.state;
  s = baseline;
  s.source = "RaceMenu: " + (preset.name.empty() ? std::string("preset") : preset.name);
  auto& v = s.values;

  if (preset.has_sliders) v.sliders = preset.sliders;
  else out.notes.push_back("El preset no trae sliders faciales (morphs.default): se conservan los del NPC.");
  if (preset.has_presets) v.presets = preset.presets;
  if (preset.weight) v.weight = *preset.weight;
  if (preset.hair_color) v.hair_color = preset.hair_color;
  if (!preset.head_texture.empty()) s.head_texture = preset.head_texture;

  std::vector<std::string> parts;
  for (const auto& hp : preset.head_parts) {
    if (hp.form_key.empty()) {
      out.notes.push_back("Pieza de cabeza ilegible en el preset: " + hp.form_identifier);
      continue;
    }
    parts.push_back(hp.form_key);
  }
  if (!parts.empty()) s.head_parts = std::move(parts);

  // Tints: RaceMenu stores the mask texture; the record needs the race's TINI index for it.
  std::vector<face::TintLayer> tints;
  for (const auto& t : preset.tints) {
    const auto key = mask_key(t.texture);
    const auto it = std::find_if(target.tint_masks.begin(), target.tint_masks.end(),
                                 [&](const auto& m) { return mask_key(m.mask_path) == key; });
    if (it == target.tint_masks.end()) {
      ++out.tints_skipped;
      if (t.value > 0.0f) out.notes.push_back("Capa de tinte sin máscara equivalente en la raza del NPC: " + t.texture);
      continue;
    }
    if (is_skin_tone_mask(t.texture)) v.skin_tone = t.color;  // QNAM follows the skin tone layer
    tints.push_back({it->index, t.color, t.value, -1});
    ++out.tints_matched;
  }
  if (!tints.empty()) v.tints = std::move(tints);

  v.custom_morphs = preset.custom_morphs;
  v.sculpt = preset.sculpt;

  if (preset.overrides > 0) {
    out.runtime_only.push_back(std::format("{} overrides de NiOverride (texturas/shaders aplicados por RaceMenu en el juego)", preset.overrides));
  }
  if (preset.transforms > 0) {
    out.runtime_only.push_back(std::format("{} transformaciones de nodos (escala/posición de huesos de RaceMenu)", preset.transforms));
  }
  if (preset.body_morphs > 0) {
    out.runtime_only.push_back(std::format("{} morphs corporales de BodySlide/RaceMenu (necesitan BodyMorph en el juego)", preset.body_morphs));
  }
  if (!preset.face_textures.empty()) {
    out.runtime_only.push_back("texturas faciales sustituidas en tiempo real por RaceMenu (faceTextures); se usa el conjunto de texturas de la cabeza");
  }
  for (const auto& w : preset.warnings) out.notes.push_back("Dato ilegible en el preset: " + w);
  return out;
}

}  // namespace sns::presets
