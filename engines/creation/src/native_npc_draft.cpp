#include "sns/creation/native_npc_draft.hpp"
#include "sns/core/error.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <limits>

namespace sns::creation {
using Json = nlohmann::json;
namespace {
int strict_int(const Json& object, const char* key, int fallback) {
  if (!object.contains(key)) return fallback;
  const auto& v = object.at(key);
  if (!v.is_number_integer() || v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
    throw Error(ErrorKind::Parse, std::string("Expected bounded integer: ") + key);
  return v.get<int>();
}
}
void validate_export_options(const Json& options) {
  if (!options.is_object()) throw Error(ErrorKind::InvalidArgument, "Invalid export options");
  if (options.contains("pluginName") && (!options.at("pluginName").is_string() || options.at("pluginName").get<std::string>().size() > 100))
    throw Error(ErrorKind::InvalidArgument, "Invalid plugin name type/length");
  for (const auto* key : {"eslFlag", "createZip"})
    if (options.contains(key) && !options.at(key).is_boolean()) throw Error(ErrorKind::InvalidArgument, "Export options must be boolean");
}
void validate_native_draft(const NativeNpcDraft& d) {
  const auto fail = [](const char* message) { throw Error(ErrorKind::InvalidArgument, message); };
  static const std::regex editor("^[A-Za-z_][A-Za-z0-9_]{0,127}$");
  if (!d.editor_id.empty() && !std::regex_match(d.editor_id, editor)) fail("Invalid native NPC EditorID");
  if (d.name.size() > 160 || d.description.size() > 4096) fail("Native NPC text exceeds limits");
  if (!std::isfinite(d.height) || d.height < 0.5f || d.height > 2 || !std::isfinite(d.weight) || d.weight < 0 || d.weight > 100) fail("Invalid native NPC height or weight");
  if (d.level < 1 || d.level > 32767 || d.min_level < 1 || d.max_level < d.min_level || d.max_level > 32767 || d.health < 1 || d.health > 65535 || d.magicka < 0 || d.magicka > 65535 || d.stamina < 0 || d.stamina > 65535) fail("Invalid native NPC statistics");
  if (d.level_multiplier && (!std::isfinite(*d.level_multiplier) || *d.level_multiplier <= 0 || *d.level_multiplier > 10)) fail("Invalid level multiplier");
  if (d.inventory.size() > 256 || d.factions.size() > 128 || d.head_part_form_keys.size() > 64 || d.outfit_armor_form_keys.size() > 64 || d.package_form_keys.size() > 64) fail("Native draft collection limit exceeded");
  for (const auto& item : d.inventory) if (item.count < 1 || item.count > 100000) fail("Invalid inventory count");
  for (const auto& faction : d.factions) if (faction.rank < -1 || faction.rank > 127) fail("Invalid faction rank");
  if (d.placement && (!std::isfinite(d.placement->x) || !std::isfinite(d.placement->y) || !std::isfinite(d.placement->z) || !std::isfinite(d.placement->rotation_z))) fail("Invalid placement coordinates");
  if (!d.face.is_null()) {
    if (!d.face.is_object() || !d.face.contains("sliders") || !d.face.at("sliders").is_array() || d.face.at("sliders").size() != 18 || !d.face.contains("presets") || !d.face.at("presets").is_array() || d.face.at("presets").size() != 4) fail("Invalid face schema");
    for (const auto& v : d.face.at("sliders")) if (!v.is_number() || !std::isfinite(v.get<float>()) || v.get<float>() < -1 || v.get<float>() > 1) fail("Invalid face morph");
  }
}
Json native_npc_to_json(const NativeNpcDraft& d) {
  validate_native_draft(d);
  Json j = Json::object();
  j["name"] = d.name;
  j["editorId"] = d.editor_id;
  j["description"] = d.description;
  j["raceFormKey"] = d.race_form_key;
  j["voiceFormKey"] = d.voice_form_key;
  j["classFormKey"] = d.class_form_key;
  j["combatStyleFormKey"] = d.combat_style_form_key;
  j["female"] = d.female;
  j["height"] = d.height;
  j["weight"] = d.weight;
  j["level"] = d.level;
  j["minLevel"] = d.min_level;
  j["maxLevel"] = d.max_level;
  j["health"] = d.health;
  j["magicka"] = d.magicka;
  j["stamina"] = d.stamina;
  j["skills"] = d.skills;
  j["headPartFormKeys"] = d.head_part_form_keys;
  j["outfitArmorFormKeys"] = d.outfit_armor_form_keys;
  j["packageFormKeys"] = d.package_form_keys;
  j["hairColorFormKey"] = d.hair_color_form_key;
  j["headTextureFormKey"] = d.head_texture_form_key;
  j["face"] = d.face;
  j["levelMultiplier"] = d.level_multiplier ? Json(*d.level_multiplier) : Json(nullptr);
  j["inventory"] = Json::array();
  for (const auto& i : d.inventory) j["inventory"].push_back({{"formKey", i.form_key}, {"count", i.count}});
  j["factions"] = Json::array();
  for (const auto& f : d.factions) j["factions"].push_back({{"formKey", f.form_key}, {"rank", f.rank}});
  j["placement"] = nullptr;
  if (d.placement) { const auto& p = *d.placement; j["placement"] = {{"cellFormKey", p.cell_form_key}, {"x", p.x}, {"y", p.y}, {"z", p.z}, {"rotationZ", p.rotation_z}}; }
  return j;
}
NativeNpcDraft native_npc_from_json(const Json& j) {
  if (!j.is_object()) throw Error(ErrorKind::Parse, "Native NPC must be a JSON object");
  NativeNpcDraft d;
  d.name = j.value("name", d.name);
  d.editor_id = j.value("editorId", d.editor_id);
  d.description = j.value("description", d.description);
  d.race_form_key = j.value("raceFormKey", d.race_form_key);
  d.voice_form_key = j.value("voiceFormKey", d.voice_form_key);
  d.class_form_key = j.value("classFormKey", d.class_form_key);
  d.combat_style_form_key = j.value("combatStyleFormKey", d.combat_style_form_key);
  d.female = j.value("female", d.female);
  d.height = j.value("height", d.height);
  d.weight = j.value("weight", d.weight);
  d.level = strict_int(j, "level", d.level);
  d.min_level = strict_int(j, "minLevel", d.min_level);
  d.max_level = strict_int(j, "maxLevel", d.max_level);
  d.health = strict_int(j, "health", d.health);
  d.magicka = strict_int(j, "magicka", d.magicka);
  d.stamina = strict_int(j, "stamina", d.stamina);
  if (j.contains("skills")) {
    if (!j.at("skills").is_object()) throw Error(ErrorKind::Parse, "Skills must be an object");
    for (const auto& [key, value] : j.at("skills").items()) d.skills[key] = strict_int(j.at("skills"), key.c_str(), 0);
  }
  d.head_part_form_keys = j.value("headPartFormKeys", d.head_part_form_keys);
  d.outfit_armor_form_keys = j.value("outfitArmorFormKeys", d.outfit_armor_form_keys);
  d.package_form_keys = j.value("packageFormKeys", d.package_form_keys);
  d.hair_color_form_key = j.value("hairColorFormKey", d.hair_color_form_key);
  d.head_texture_form_key = j.value("headTextureFormKey", d.head_texture_form_key);
  d.face = j.value("face", d.face);
  if (j.contains("levelMultiplier") && !j.at("levelMultiplier").is_null()) d.level_multiplier = j.at("levelMultiplier").get<float>();
  for (const auto* key : {"inventory", "factions"}) if (j.contains(key) && !j.at(key).is_array()) throw Error(ErrorKind::Parse, "Expected draft array");
  for (const auto& i : j.value("inventory", Json::array())) d.inventory.push_back({i.at("formKey").get<std::string>(), strict_int(i, "count", 0)});
  for (const auto& f : j.value("factions", Json::array())) d.factions.push_back({f.at("formKey").get<std::string>(), strict_int(f, "rank", 0)});
  if (j.contains("placement") && !j.at("placement").is_null()) {
    const auto& p = j.at("placement");
    d.placement = NativePlacement{p.at("cellFormKey").get<std::string>(), p.value("x", 0.f), p.value("y", 0.f), p.value("z", 0.f), p.value("rotationZ", 0.f)};
  }
  validate_native_draft(d);
  return d;
}
}  // namespace sns::creation
