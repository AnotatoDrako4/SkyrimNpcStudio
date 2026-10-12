#include "sns/appearance/appearance_draft.hpp"

#include <algorithm>
#include <cmath>

#include "sns/core/text.hpp"

namespace sns::appearance {

namespace {

projects::ColorRgb to_project(const face::Rgb8& c) { return {c.r, c.g, c.b}; }
face::Rgb8 to_face(const projects::ColorRgb& c) { return {c.r, c.g, c.b}; }

const face::HeadPartData* part_of(const face::FaceRecipe& recipe, const std::string& form_key) {
  const auto it = std::find_if(recipe.parts.begin(), recipe.parts.end(),
                               [&](const auto& p) { return text::iequals_ascii(p.form_key, form_key); });
  return it == recipe.parts.end() ? nullptr : &*it;
}

class SetFaceCommand final : public projects::Command {
 public:
  SetFaceCommand(AppearanceDraft& draft, FaceState next, std::string description, std::string key)
      : draft_(draft), before_(draft.face()), after_(std::move(next)), description_(std::move(description)), key_(std::move(key)) {}

  void apply() override { draft_.set_face(after_); }
  void revert() override { draft_.set_face(before_); }
  [[nodiscard]] std::string description() const override { return description_; }
  bool absorb(const projects::Command& next) override {
    const auto* other = dynamic_cast<const SetFaceCommand*>(&next);
    if (!other || key_.empty() || other->key_ != key_ || &other->draft_ != &draft_) return false;
    after_ = other->after_;
    return true;
  }

 private:
  AppearanceDraft& draft_;
  FaceState before_;
  FaceState after_;
  std::string description_;
  std::string key_;
};

class SetHeightCommand final : public projects::Command {
 public:
  SetHeightCommand(AppearanceDraft& draft, float height) : draft_(draft), before_(draft.height()), after_(height) {}
  void apply() override { draft_.set_height(after_); }
  void revert() override { draft_.set_height(before_); }
  [[nodiscard]] std::string description() const override { return "Cambiar estatura"; }
  bool absorb(const projects::Command& next) override {
    const auto* other = dynamic_cast<const SetHeightCommand*>(&next);
    if (!other || &other->draft_ != &draft_) return false;
    after_ = other->after_;
    return true;
  }

 private:
  AppearanceDraft& draft_;
  float before_;
  float after_;
};

}  // namespace

void AppearanceDraft::set_target(projects::NpcTarget target) {
  target_ = std::move(target);
  face_ = face_baseline_ = {};
  face_loaded_ = false;
  height_ = height_baseline_ = 1.0f;
  outfit_.assign({});
  initial_outfit_.clear();
}

void AppearanceDraft::set_initial_outfit(std::vector<outfit::Piece> pieces) {
  initial_outfit_ = pieces;
  outfit_.assign(std::move(pieces));
}

void AppearanceDraft::set_face_baseline(FaceState baseline) {
  face_baseline_ = baseline;
  face_ = std::move(baseline);
  face_loaded_ = true;
}

void AppearanceDraft::set_height_baseline(float height) {
  height_baseline_ = height;
  height_ = height;
}

projects::NpcEdit AppearanceDraft::to_npc_edit() const {
  projects::NpcEdit edit;
  edit.target = target_;
  if (!outfit_.pieces().empty() || is_outfit_dirty()) {
    edit.outfit = projects::OutfitConfig{
        .outfit_editor_id = "SNS_" + (target_.editor_id.empty() ? std::string("Npc") : target_.editor_id) + "Outfit",
        .pieces = outfit_.pieces(),
        .also_sleeping = false,
    };
  }
  return edit;
}

void AppearanceDraft::reset() {
  outfit_.assign(initial_outfit_);
  face_ = face_baseline_;
  height_ = height_baseline_;
}

FaceState face_state_from_recipe(const face::FaceRecipe& recipe) {
  FaceState state;
  state.values = recipe.values;
  state.head_parts = recipe.npc_head_parts;
  state.head_texture = recipe.npc_head_texture;
  return state;
}

projects::FaceConfig to_face_config(const FaceState& state, const face::FaceRecipe& recipe) {
  projects::FaceConfig c;
  c.complete = true;
  c.preset_name = state.source;
  c.geometry_mode = state.geometry_mode;
  c.head_texture = state.head_texture;
  const auto& v = state.values;
  for (const auto& def : face::get_standard_face_morphs()) c.morph_sliders[def.key] = v.sliders[def.index];
  c.face_presets.assign(v.presets.begin(), v.presets.end());
  c.weight = v.weight;
  c.skin_tone = to_project(v.skin_tone);
  if (v.hair_color) c.hair_color = to_project(*v.hair_color);
  for (const auto& t : v.tints) {
    c.tint_layers.push_back({t.index, to_project(t.color), t.value, static_cast<std::uint32_t>(std::max(t.preset, 0))});
  }
  c.custom_morphs = v.custom_morphs;
  for (const auto& s : v.sculpt) {
    projects::SculptConfig sc{s.host, s.vertex_count, {}};
    for (const auto& [index, d] : s.deltas) sc.deltas.push_back({static_cast<float>(index), d[0], d[1], d[2]});
    c.sculpt.push_back(std::move(sc));
  }
  for (const auto& fk : state.head_parts) {
    projects::HeadPartRef ref{.form_key = fk};
    if (const auto* part = part_of(recipe, fk)) {
      ref.editor_id = part->editor_id;
      ref.type = std::string(face::head_part_type_to_string(part->type));
      ref.mesh_path = part->model;
    }
    c.head_parts.push_back(std::move(ref));
  }
  return c;
}

FaceState from_face_config(const projects::FaceConfig& config, const FaceState& baseline, const face::FaceRecipe& recipe,
                           std::vector<std::string>* notes) {
  auto note = [&](std::string text) {
    if (notes) notes->push_back(std::move(text));
  };
  FaceState s = baseline;
  if (!config.preset_name.empty()) s.source = config.preset_name;
  if (!config.geometry_mode.empty()) s.geometry_mode = config.geometry_mode;

  for (const auto& [key, value] : config.morph_sliders) {
    if (const auto* def = face::find_morph_def(key)) {
      s.values.sliders[def->index] = def->clamp(std::isfinite(value) ? value : 0.0f);
    } else {
      note("Slider desconocido ignorado: " + key);
    }
  }
  if (config.face_presets.size() == face::kPresetSlotCount) {
    for (std::size_t i = 0; i < face::kPresetSlotCount; ++i) s.values.presets[i] = config.face_presets[i];
  }
  if (config.complete || config.preset_source == "donor" || config.weight != 100.0f) {
    s.values.weight = std::clamp(config.weight, 0.0f, 100.0f);
  } else {
    note("Proyecto de formato 2: el peso 100 guardado por defecto no se aplica; se conserva el del registro.");
  }
  if (config.skin_tone) s.values.skin_tone = to_face(*config.skin_tone);
  if (config.hair_color) s.values.hair_color = to_face(*config.hair_color);
  if (!config.tint_layers.empty()) {
    if (config.complete) s.values.tints.clear();
    for (const auto& t : config.tint_layers) {
      face::TintLayer layer{static_cast<std::uint16_t>(t.index), to_face(t.color), std::clamp(t.alpha, 0.0f, 1.0f),
                            static_cast<std::int32_t>(t.preset)};
      const auto it = std::find_if(s.values.tints.begin(), s.values.tints.end(), [&](const auto& x) { return x.index == layer.index; });
      if (it != s.values.tints.end()) *it = layer;
      else s.values.tints.push_back(layer);
    }
  }
  if (config.complete || !config.custom_morphs.empty()) s.values.custom_morphs = config.custom_morphs;
  if (config.complete || !config.sculpt.empty()) {
    s.values.sculpt.clear();
    for (const auto& sc : config.sculpt) {
      face::SculptTarget t{sc.host, sc.vertex_count, {}};
      for (const auto& d : sc.deltas) {
        if (d[0] < 0) continue;
        t.deltas.push_back({static_cast<std::uint32_t>(d[0]), {d[1], d[2], d[3]}});
      }
      s.values.sculpt.push_back(std::move(t));
    }
  }

  if (config.complete) s.head_texture = config.head_texture;
  if (config.complete) {
    s.head_parts.clear();
    for (const auto& ref : config.head_parts) {
      if (!ref.form_key.empty()) s.head_parts.push_back(ref.form_key);
    }
  } else {
    // Format 2: each listed part replaces the record's part of the same type.
    for (const auto& ref : config.head_parts) {
      if (ref.form_key.empty()) {
        note("Pieza de cabeza sin FormKey ignorada: " + (ref.editor_id.empty() ? ref.type : ref.editor_id));
        continue;
      }
      const auto type = face::string_to_head_part_type(ref.type);
      std::erase_if(s.head_parts, [&](const std::string& fk) {
        const auto* part = part_of(recipe, fk);
        return part && part->type == type;
      });
      s.head_parts.push_back(ref.form_key);
    }
  }
  return s;
}

std::unique_ptr<projects::Command> make_set_face(AppearanceDraft& draft, FaceState next, std::string description,
                                                 std::string merge_key) {
  return std::make_unique<SetFaceCommand>(draft, std::move(next), std::move(description), std::move(merge_key));
}

std::unique_ptr<projects::Command> make_set_height(AppearanceDraft& draft, float height) {
  return std::make_unique<SetHeightCommand>(draft, height);
}

}  // namespace sns::appearance
