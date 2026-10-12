#include "sns/presets/jslot_parser.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::presets {

using Json = nlohmann::json;

namespace {

face::Rgb8 rgb_of(std::uint32_t argb) {
  return {static_cast<std::uint8_t>((argb >> 16) & 0xFF), static_cast<std::uint8_t>((argb >> 8) & 0xFF),
          static_cast<std::uint8_t>(argb & 0xFF)};
}

std::optional<std::uint32_t> as_u32(const Json& j) {
  if (j.is_number_unsigned()) return j.get<std::uint32_t>();
  if (j.is_number_integer()) return static_cast<std::uint32_t>(j.get<std::int64_t>());
  if (j.is_number_float()) return static_cast<std::uint32_t>(j.get<double>());
  return std::nullopt;
}

bool is_hex(std::string_view s) {
  return !s.empty() && s.size() <= 8 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

}  // namespace

std::string form_key_from_identifier(std::string_view identifier) {
  const auto bar = identifier.find('|');
  if (bar == std::string_view::npos || bar == 0) return {};
  const auto plugin = text::trim(identifier.substr(0, bar));
  auto id = text::trim(identifier.substr(bar + 1));
  if (plugin.empty() || !is_hex(id)) return {};
  // FormKeys use the 6 low hex digits (the load-order byte is not part of the identity).
  std::string digits(id);
  if (digits.size() > 6) digits = digits.substr(digits.size() - 6);
  while (digits.size() < 6) digits.insert(digits.begin(), '0');
  for (auto& c : digits) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return digits + ":" + std::string(plugin);
}

RaceMenuPreset parse_jslot(std::string_view json_text, std::string name) {
  auto json = Json::parse(json_text, nullptr, false);
  if (json.is_discarded() || !json.is_object()) throw Error(ErrorKind::Parse, "the .jslot file is not valid JSON");
  if (!json.contains("headParts") && !json.contains("morphs") && !json.contains("actor")) {
    throw Error(ErrorKind::Parse, "the file is JSON but not a RaceMenu preset (no actor, headParts or morphs)");
  }

  RaceMenuPreset p;
  p.name = std::move(name);
  std::vector<std::string> plugins;
  if (const auto v = json.find("version"); v != json.end() && v->is_object()) p.format_version = v->value("formatVersion", 0);

  if (const auto actor = json.find("actor"); actor != json.end() && actor->is_object()) {
    if (const auto w = actor->find("weight"); w != actor->end() && w->is_number()) p.weight = std::clamp(w->get<float>(), 0.0f, 100.0f);
    if (const auto h = actor->find("hairColor"); h != actor->end()) {
      if (const auto argb = as_u32(*h)) p.hair_color = rgb_of(*argb);
    }
    if (const auto t = actor->find("headTexture"); t != actor->end() && t->is_string()) {
      p.head_texture = form_key_from_identifier(t->get<std::string>());
      if (p.head_texture.empty() && !t->get<std::string>().empty()) p.warnings.push_back("headTexture unreadable: " + t->get<std::string>());
    }
  }

  for (const auto& hp : json.value("headParts", Json::array())) {
    if (!hp.is_object()) continue;
    JslotHeadPart part;
    part.form_identifier = hp.value("formIdentifier", "");
    part.form_key = form_key_from_identifier(part.form_identifier);
    part.slot = hp.value("type", 0);
    if (const auto bar = part.form_identifier.find('|'); bar != std::string::npos) part.plugin = part.form_identifier.substr(0, bar);
    if (part.form_key.empty()) p.warnings.push_back("head part without a readable formIdentifier: " + part.form_identifier);
    if (!part.plugin.empty()) plugins.push_back(part.plugin);
    p.head_parts.push_back(std::move(part));
  }

  if (const auto morphs = json.find("morphs"); morphs != json.end() && morphs->is_object()) {
    if (const auto def = morphs->find("default"); def != morphs->end() && def->is_object()) {
      const auto values = def->value("morphs", Json::array());
      if (values.is_array() && values.size() >= face::kSliderCount) {
        p.has_sliders = true;
        for (std::size_t i = 0; i < face::kSliderCount; ++i) {
          const float v = values[i].is_number() ? values[i].get<float>() : 0.0f;
          p.sliders[i] = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
        }
      } else if (!values.empty()) {
        p.warnings.push_back(std::format("morphs.default.morphs has {} values, expected 19", values.size()));
      }
      const auto presets = def->value("presets", Json::array());
      if (presets.is_array() && presets.size() >= face::kPresetSlotCount) {
        p.has_presets = true;
        for (std::size_t i = 0; i < face::kPresetSlotCount; ++i) {
          const auto raw = as_u32(presets[i]).value_or(0xFFFFFFFFu);
          p.presets[i] = raw == 0xFFFFFFFFu || raw == 255u ? -1 : static_cast<std::int32_t>(raw);
        }
      }
    }
    for (const auto& c : morphs->value("custom", Json::array())) {
      if (c.is_object() && c.contains("name") && c["name"].is_string() && c.contains("value") && c["value"].is_number()) {
        const float v = c["value"].get<float>();
        if (std::isfinite(v) && v != 0.0f) p.custom_morphs[c["name"].get<std::string>()] = v;
      }
    }
    const auto divisor_json = morphs->value("sculptDivisor", Json(10000));
    const double divisor = divisor_json.is_number() && divisor_json.get<double>() > 0 ? divisor_json.get<double>() : 10000.0;
    const auto sculpt = morphs->find("sculpt");
    if (sculpt != morphs->end() && sculpt->is_array()) {
      for (const auto& host : *sculpt) {
        if (!host.is_object()) continue;
        face::SculptTarget target;
        target.host = host.value("host", "");
        target.vertex_count = host.value("vertices", 0u);
        for (const auto& d : host.value("data", Json::array())) {
          if (!d.is_array() || d.size() != 4 || !d[0].is_number()) continue;
          target.deltas.push_back({d[0].get<std::uint32_t>(),
                                   {static_cast<float>(d[1].get<double>() / divisor), static_cast<float>(d[2].get<double>() / divisor),
                                    static_cast<float>(d[3].get<double>() / divisor)}});
        }
        if (!target.host.empty() && !target.deltas.empty()) p.sculpt.push_back(std::move(target));
      }
    }
  }

  for (const auto& t : json.value("tintInfo", Json::array())) {
    if (!t.is_object()) continue;
    JslotTint tint;
    tint.index = t.value("index", 0);
    tint.texture = t.value("texture", "");
    const auto argb = t.contains("color") ? as_u32(t["color"]).value_or(0) : 0u;
    tint.color = rgb_of(argb);
    tint.value = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
    p.tints.push_back(std::move(tint));
  }
  for (const auto& f : json.value("faceTextures", Json::array())) {
    if (f.is_object() && f.contains("texture") && f["texture"].is_string()) p.face_textures.push_back(f["texture"].get<std::string>());
  }
  for (const auto& m : json.value("mods", Json::array())) {
    if (m.is_object() && m.contains("name") && m["name"].is_string()) plugins.push_back(m["name"].get<std::string>());
  }
  for (const auto& m : json.value("modNames", Json::array())) {
    if (m.is_string()) plugins.push_back(m.get<std::string>());
  }
  auto count = [&](const char* key) {
    const auto it = json.find(key);
    return it != json.end() && it->is_array() ? it->size() : std::size_t{0};
  };
  p.overrides = count("overrides");
  p.transforms = count("transforms");
  p.body_morphs = count("bodyMorphs");

  std::sort(plugins.begin(), plugins.end(), [](const auto& a, const auto& b) { return text::to_lower_ascii(a) < text::to_lower_ascii(b); });
  plugins.erase(std::unique(plugins.begin(), plugins.end(), [](const auto& a, const auto& b) { return text::iequals_ascii(a, b); }),
                plugins.end());
  p.plugins = std::move(plugins);
  return p;
}

}  // namespace sns::presets
