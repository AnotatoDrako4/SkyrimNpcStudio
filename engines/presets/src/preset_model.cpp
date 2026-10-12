#include "sns/presets/preset_model.hpp"

namespace sns::presets {

projects::FaceConfig to_face_config(const PresetItem& preset, const std::string& donor_plugin) {
  projects::FaceConfig cfg;
  cfg.preset_id = preset.id;
  cfg.preset_name = preset.name;
  cfg.preset_source = preset.source;
  cfg.donor_npc_form_key = preset.donor_npc_form_key;
  cfg.donor_plugin = donor_plugin.empty() ? preset.donor_plugin : donor_plugin;
  cfg.donor_facegen_mesh = preset.donor_facegen_mesh;
  cfg.donor_facegen_tint = preset.donor_facegen_tint;
  cfg.hair_color = preset.hair_color;
  cfg.skin_tone = preset.skin_tone;
  cfg.weight = preset.weight;
  cfg.height = preset.height;
  cfg.morph_sliders = preset.morphs;

  for (const auto& hp : preset.head_parts) {
    projects::HeadPartRef ref;
    ref.form_key = hp.form_id.empty() ? "" : (hp.form_id + (hp.plugin.empty() ? "" : ":" + hp.plugin));
    ref.editor_id = hp.editor_id;
    ref.type = hp.type;
    ref.mesh_path = hp.mesh_path;
    cfg.head_parts.push_back(std::move(ref));
  }

  for (const auto& t : preset.tints) {
    projects::TintLayerConfig tc;
    tc.index = static_cast<std::uint32_t>(t.index);
    tc.color = t.color;
    tc.alpha = t.alpha;
    tc.preset = 0;
    cfg.tint_layers.push_back(tc);
  }

  return cfg;
}

}  // namespace sns::presets
