#include "sns/face/face_recipe.hpp"

#include <algorithm>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::face {

using Json = nlohmann::json;

namespace {

Rgb8 rgb_from_json(const Json& j) {
  return Rgb8{j.value("r", static_cast<std::uint8_t>(255)), j.value("g", static_cast<std::uint8_t>(255)),
              j.value("b", static_cast<std::uint8_t>(255))};
}
Json rgb_to_json(const Rgb8& c) { return {{"r", c.r}, {"g", c.g}, {"b", c.b}}; }

std::optional<std::array<std::string, 9>> texture_set_from_json(const Json& j) {
  if (!j.is_array()) return std::nullopt;
  std::array<std::string, 9> set{};
  for (std::size_t i = 0; i < set.size() && i < j.size(); ++i) {
    if (j[i].is_string()) set[i] = j[i].get<std::string>();
  }
  return set;
}
Json texture_set_to_json(const std::optional<std::array<std::string, 9>>& set) {
  if (!set) return nullptr;
  Json out = Json::array();
  for (const auto& s : *set) out.push_back(s);
  return out;
}

}  // namespace

const TintMaskDef* FaceRecipe::tint_mask(std::uint16_t index) const {
  const auto it = std::find_if(tint_masks.begin(), tint_masks.end(), [&](const auto& m) { return m.index == index; });
  return it == tint_masks.end() ? nullptr : &*it;
}

const HeadPartData* FaceRecipe::face_part() const {
  const auto it = std::find_if(parts.begin(), parts.end(), [](const auto& p) { return p.type == HeadPartType::Face; });
  return it == parts.end() ? nullptr : &*it;
}

std::string race_morph_name(const FaceRecipe& recipe) {
  return recipe.morph_race_editor_id.empty() ? recipe.race_editor_id : recipe.morph_race_editor_id;
}

FaceValues values_from_json(const Json& j) {
  FaceValues v;
  if (const auto it = j.find("sliders"); it != j.end() && it->is_array()) {
    for (std::size_t i = 0; i < v.sliders.size() && i < it->size(); ++i) v.sliders[i] = (*it)[i].get<float>();
  }
  if (const auto it = j.find("presets"); it != j.end() && it->is_array()) {
    for (std::size_t i = 0; i < v.presets.size() && i < it->size(); ++i) v.presets[i] = (*it)[i].get<std::int32_t>();
  }
  v.weight = j.value("weight", 50.0f);
  if (const auto it = j.find("skinTone"); it != j.end() && it->is_object()) v.skin_tone = rgb_from_json(*it);
  if (const auto it = j.find("hairColor"); it != j.end() && it->is_object()) v.hair_color = rgb_from_json(*it);
  for (const auto& t : j.value("tints", Json::array())) {
    TintLayer layer;
    layer.index = t.value("index", static_cast<std::uint16_t>(0));
    if (const auto c = t.find("color"); c != t.end() && c->is_object()) layer.color = rgb_from_json(*c);
    layer.value = t.value("value", 1.0f);
    layer.preset = t.value("preset", -1);
    v.tints.push_back(layer);
  }
  // Keep the object alive: items() of a temporary would dangle inside the range-for (C++20).
  const auto custom = j.value("customMorphs", Json::object());
  for (const auto& [name, value] : custom.items()) {
    if (value.is_number()) v.custom_morphs[name] = value.get<float>();
  }
  for (const auto& s : j.value("sculpt", Json::array())) {
    SculptTarget target;
    target.host = s.value("host", "");
    target.vertex_count = s.value("vertexCount", 0u);
    for (const auto& d : s.value("deltas", Json::array())) {
      if (!d.is_array() || d.size() != 4) continue;
      target.deltas.push_back({d[0].get<std::uint32_t>(), {d[1].get<float>(), d[2].get<float>(), d[3].get<float>()}});
    }
    v.sculpt.push_back(std::move(target));
  }
  return v;
}

Json values_to_json(const FaceValues& v) {
  Json sliders = Json::array();
  for (const float s : v.sliders) sliders.push_back(s);
  Json presets = Json::array();
  for (const auto p : v.presets) presets.push_back(p);
  Json tints = Json::array();
  for (const auto& t : v.tints) {
    tints.push_back({{"index", t.index}, {"color", rgb_to_json(t.color)}, {"value", t.value}, {"preset", t.preset}});
  }
  Json sculpt = Json::array();
  for (const auto& s : v.sculpt) {
    Json deltas = Json::array();
    for (const auto& [index, d] : s.deltas) deltas.push_back({index, d[0], d[1], d[2]});
    sculpt.push_back({{"host", s.host}, {"vertexCount", s.vertex_count}, {"deltas", std::move(deltas)}});
  }
  Json out{{"sliders", std::move(sliders)},
           {"presets", std::move(presets)},
           {"weight", v.weight},
           {"skinTone", rgb_to_json(v.skin_tone)},
           {"tints", std::move(tints)},
           {"customMorphs", v.custom_morphs},
           {"sculpt", std::move(sculpt)}};
  if (v.hair_color) out["hairColor"] = rgb_to_json(*v.hair_color);
  return out;
}

FaceRecipe recipe_from_json(const Json& j) {
  if (!j.is_object()) throw Error(ErrorKind::Parse, "face recipe is not a JSON object");
  FaceRecipe r;
  r.npc_form_key = j.value("npcFormKey", "");
  r.source_plugin = j.value("sourcePlugin", "");
  r.facegen_mesh_path = j.value("faceGenMeshPath", "");
  r.facegen_tint_path = j.value("faceGenTintPath", "");
  r.female = j.value("female", false);
  r.height = j.value("height", 1.0f);
  if (const auto race = j.find("race"); race != j.end() && race->is_object()) {
    r.race_form_key = race->value("formKey", "");
    r.race_editor_id = race->value("editorId", "");
    r.morph_race_editor_id = race->value("morphRaceEditorId", "");
    r.vampire = race->value("vampire", false);
  }
  for (const auto& p : j.value("headParts", Json::array())) {
    HeadPartData part;
    part.form_key = p.value("formKey", "");
    part.editor_id = p.value("editorId", "");
    part.type = string_to_head_part_type(p.value("type", "Misc"));
    part.model = p.value("model", "");
    part.extra = p.value("extra", false);
    part.plugin = p.value("plugin", "");
    part.official = p.value("official", false);
    part.valid_sex = p.value("validSex", "any");
    part.valid_for_race = p.value("validForRace", true);
    for (const auto& t : p.value("tris", Json::array())) {
      const int kind = t.value("kind", 2);
      if (kind < 0 || kind > 2) continue;
      part.tris.push_back({static_cast<TriKind>(kind), t.value("path", "")});
    }
    if (const auto ts = p.find("textureSet"); ts != p.end()) part.texture_set = texture_set_from_json(*ts);
    r.parts.push_back(std::move(part));
  }
  r.npc_head_parts = j.value("npcHeadParts", std::vector<std::string>{});
  if (const auto ts = j.find("faceTextureSet"); ts != j.end()) r.face_texture_set = texture_set_from_json(*ts);
  r.face_texture_set_form_key = j.value("faceTextureSetFormKey", "");
  r.npc_head_texture = j.value("npcHeadTexture", "");
  for (const auto& m : j.value("tintMasks", Json::array())) {
    r.tint_masks.push_back({m.value("index", static_cast<std::uint16_t>(0)), m.value("maskPath", ""),
                            m.value("maskType", static_cast<std::uint16_t>(0))});
  }
  for (const auto& e : j.value("morphExtensions", Json::array())) {
    r.morph_extensions.push_back({e.value("host", ""), e.value("tri", "")});
  }
  if (const auto v = j.find("values"); v != j.end() && v->is_object()) r.values = values_from_json(*v);
  return r;
}

Json recipe_to_json(const FaceRecipe& r) {
  Json parts = Json::array();
  for (const auto& p : r.parts) {
    Json tris = Json::array();
    for (const auto& t : p.tris) tris.push_back({{"kind", static_cast<int>(t.kind)}, {"path", t.path}});
    parts.push_back({{"formKey", p.form_key},
                     {"editorId", p.editor_id},
                     {"type", std::string(head_part_type_to_string(p.type))},
                     {"model", p.model},
                     {"extra", p.extra},
                     {"plugin", p.plugin},
                     {"official", p.official},
                     {"validSex", p.valid_sex},
                     {"validForRace", p.valid_for_race},
                     {"tris", std::move(tris)},
                     {"textureSet", texture_set_to_json(p.texture_set)}});
  }
  Json masks = Json::array();
  for (const auto& m : r.tint_masks) masks.push_back({{"index", m.index}, {"maskPath", m.mask_path}, {"maskType", m.mask_type}});
  Json extensions = Json::array();
  for (const auto& e : r.morph_extensions) extensions.push_back({{"host", e.host}, {"tri", e.tri}});
  return {{"npcFormKey", r.npc_form_key},
          {"sourcePlugin", r.source_plugin},
          {"faceGenMeshPath", r.facegen_mesh_path},
          {"faceGenTintPath", r.facegen_tint_path},
          {"female", r.female},
          {"height", r.height},
          {"race",
           {{"formKey", r.race_form_key}, {"editorId", r.race_editor_id}, {"morphRaceEditorId", r.morph_race_editor_id}, {"vampire", r.vampire}}},
          {"headParts", std::move(parts)},
          {"npcHeadParts", r.npc_head_parts},
          {"faceTextureSet", texture_set_to_json(r.face_texture_set)},
          {"faceTextureSetFormKey", r.face_texture_set_form_key},
          {"npcHeadTexture", r.npc_head_texture},
          {"tintMasks", std::move(masks)},
          {"morphExtensions", std::move(extensions)},
          {"values", values_to_json(r.values)}};
}

}  // namespace sns::face
