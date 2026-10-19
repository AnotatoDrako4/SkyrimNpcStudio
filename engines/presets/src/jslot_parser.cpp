#include "sns/presets/jslot_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
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

// A whole number in [lo, hi]: integers as stored, floats only when finite, integral and in range (audit v2
// F-15: casting anything else is undefined behaviour or silently drops bits). nullopt for every other value.
std::optional<std::int64_t> as_int(const Json& j, std::int64_t lo, std::int64_t hi) {
  if (j.is_number_unsigned()) {
    const auto v = j.get<std::uint64_t>();
    if (v > static_cast<std::uint64_t>(hi)) return std::nullopt;
    const auto s = static_cast<std::int64_t>(v);
    return s < lo ? std::nullopt : std::optional(s);
  }
  if (j.is_number_integer()) {
    const auto v = j.get<std::int64_t>();
    return v < lo || v > hi ? std::nullopt : std::optional(v);
  }
  if (j.is_number_float()) {
    const auto d = j.get<double>();
    if (!std::isfinite(d) || d != std::trunc(d) || d < static_cast<double>(lo) || d > static_cast<double>(hi)) return std::nullopt;
    return static_cast<std::int64_t>(d);
  }
  return std::nullopt;
}

// A 32-bit ARGB color or runtime FormID. Some tools write them as signed 32-bit integers; those keep their bits.
std::optional<std::uint32_t> as_u32(const Json& j) {
  const auto v = as_int(j, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::uint32_t>::max());
  if (!v) return std::nullopt;
  return static_cast<std::uint32_t>(*v);
}

// A finite number that fits a float; nullopt otherwise (converting a larger double to float is undefined).
std::optional<float> as_float(const Json& j) {
  if (!j.is_number()) return std::nullopt;
  const auto d = j.get<double>();
  if (!std::isfinite(d) || std::abs(d) > static_cast<double>(std::numeric_limits<float>::max())) return std::nullopt;
  return static_cast<float>(d);
}

bool is_hex(std::string_view s) {
  return !s.empty() && s.size() <= 8 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

// Field readers that never throw for a value of the wrong type or size: they keep the fallback and say so.
struct Reader {
  std::vector<std::string>& warnings;

  void unreadable(std::string_view field, const Json& value) {
    auto shown = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (shown.size() > 40) shown = shown.substr(0, 40) + "...";
    warnings.push_back(std::format("{} unreadable: {}", field, shown));
  }

  std::string text(const Json& object, const char* key, std::string_view field) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    unreadable(field, *it);
    return {};
  }

  std::int64_t integer(const Json& object, const char* key, std::int64_t lo, std::int64_t hi, std::string_view field) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return 0;
    if (const auto v = as_int(*it, lo, hi)) return *v;
    unreadable(field, *it);
    return 0;
  }
};

constexpr std::int64_t kIntMax = std::numeric_limits<std::int32_t>::max();
constexpr std::int64_t kU32Max = std::numeric_limits<std::uint32_t>::max();

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
  Reader read{p.warnings};
  std::vector<std::string> plugins;
  try {
    if (const auto v = json.find("version"); v != json.end() && v->is_object()) {
      p.format_version = static_cast<int>(read.integer(*v, "formatVersion", 0, kIntMax, "version.formatVersion"));
    }

    if (const auto actor = json.find("actor"); actor != json.end() && actor->is_object()) {
      if (const auto w = actor->find("weight"); w != actor->end()) {
        if (const auto weight = as_float(*w)) p.weight = std::clamp(*weight, 0.0f, 100.0f);
        else read.unreadable("actor.weight", *w);
      }
      if (const auto h = actor->find("hairColor"); h != actor->end()) {
        if (const auto argb = as_u32(*h)) p.hair_color = rgb_of(*argb);
        else read.unreadable("actor.hairColor", *h);
      }
      const auto texture = read.text(*actor, "headTexture", "actor.headTexture");
      p.head_texture = form_key_from_identifier(texture);
      if (p.head_texture.empty() && !texture.empty()) p.warnings.push_back("headTexture unreadable: " + texture);
    }

    for (const auto& hp : json.value("headParts", Json::array())) {
      if (!hp.is_object()) continue;
      JslotHeadPart part;
      part.form_identifier = read.text(hp, "formIdentifier", "headParts[].formIdentifier");
      part.form_key = form_key_from_identifier(part.form_identifier);
      part.slot = static_cast<int>(read.integer(hp, "type", 0, kIntMax, "headParts[].type"));
      if (const auto bar = part.form_identifier.find('|'); bar != std::string::npos) part.plugin = part.form_identifier.substr(0, bar);
      // Light (ESL-flagged) plugins: RaceMenu writes the runtime FormID 0xFE LLL XXX and its low 24 bits
      // in formIdentifier, so the light slot LLL would be read as part of the record ID. The record ID
      // is only the low 12 bits (KSHairdosSMP.esp|016893 with formId 0xFE016893 is record 0x893).
      if (const auto it = hp.find("formId"); it != hp.end() && !part.plugin.empty()) {
        if (const auto runtime = as_u32(*it); runtime && (*runtime >> 24) == 0xFEu) {
          part.form_key = std::format("{:06X}:{}", *runtime & 0xFFFu, text::trim(part.plugin));
        }
      }
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
            // Sliders are -1..1; a larger finite number is clamped, anything else is neutral.
            const auto& raw = values[i];
            const double v = raw.is_number() ? raw.get<double>() : 0.0;
            p.sliders[i] = std::isfinite(v) ? static_cast<float>(std::clamp(v, -1.0, 1.0)) : 0.0f;
          }
        } else if (values.is_array() && !values.empty()) {
          p.warnings.push_back(std::format("morphs.default.morphs has {} values, expected 19", values.size()));
        }
        const auto presets = def->value("presets", Json::array());
        if (presets.is_array() && presets.size() >= face::kPresetSlotCount) {
          p.has_presets = true;
          for (std::size_t i = 0; i < face::kPresetSlotCount; ++i) {
            // A race preset index (NAMA). RaceMenu writes "none" as -1, 255 or 0xFFFFFFFF.
            const auto raw = as_int(presets[i], std::numeric_limits<std::int32_t>::min(), kU32Max);
            if (raw && (*raw == -1 || *raw == 255 || *raw == kU32Max)) {
              p.presets[i] = -1;
            } else if (raw && *raw >= 0 && *raw < 255) {
              p.presets[i] = static_cast<std::int32_t>(*raw);
            } else {
              p.presets[i] = -1;
              read.unreadable(std::format("morphs.default.presets[{}]", i), presets[i]);
            }
          }
        }
      }
      for (const auto& c : morphs->value("custom", Json::array())) {
        if (c.is_object() && c.contains("name") && c["name"].is_string() && c.contains("value")) {
          if (const auto v = as_float(c["value"]); v && *v != 0.0f) p.custom_morphs[c["name"].get<std::string>()] = *v;
        }
      }
      const auto divisor_json = morphs->value("sculptDivisor", Json(10000));
      const double divisor = divisor_json.is_number() && std::isfinite(divisor_json.get<double>()) && divisor_json.get<double>() > 0
                                 ? divisor_json.get<double>()
                                 : 10000.0;
      const auto sculpt = morphs->find("sculpt");
      if (sculpt != morphs->end() && sculpt->is_array()) {
        for (const auto& host : *sculpt) {
          if (!host.is_object()) continue;
          face::SculptTarget target;
          target.host = read.text(host, "host", "morphs.sculpt[].host");
          target.vertex_count = static_cast<std::uint32_t>(read.integer(host, "vertices", 0, kU32Max, "morphs.sculpt[].vertices"));
          std::size_t skipped = 0;
          for (const auto& d : host.value("data", Json::array())) {
            // [vertex index, dx, dy, dz]: a whole index in 32 bits and three finite offsets that fit a float.
            const auto index = d.is_array() && d.size() == 4 ? as_int(d[0], 0, kU32Max) : std::nullopt;
            std::array<float, 3> offset{};
            bool ok = index.has_value();
            for (std::size_t k = 0; ok && k < 3; ++k) {
              const double v = d[k + 1].is_number() ? d[k + 1].get<double>() / divisor : std::nan("");
              ok = std::isfinite(v) && std::abs(v) <= static_cast<double>(std::numeric_limits<float>::max());
              if (ok) offset[k] = static_cast<float>(v);
            }
            if (!ok) {
              ++skipped;
              continue;
            }
            target.deltas.push_back({static_cast<std::uint32_t>(*index), offset});
          }
          if (skipped > 0) {
            p.warnings.push_back(std::format("morphs.sculpt {}: {} unreadable vertex offset(s) skipped", target.host, skipped));
          }
          if (!target.host.empty() && !target.deltas.empty()) p.sculpt.push_back(std::move(target));
        }
      }
    }

    for (const auto& t : json.value("tintInfo", Json::array())) {
      if (!t.is_object()) continue;
      JslotTint tint;
      tint.index = static_cast<int>(read.integer(t, "index", 0, kIntMax, "tintInfo[].index"));
      tint.texture = read.text(t, "texture", "tintInfo[].texture");
      std::uint32_t argb = 0;
      if (const auto c = t.find("color"); c != t.end()) {
        if (const auto v = as_u32(*c)) argb = *v;
        else read.unreadable("tintInfo[].color", *c);
      }
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
  } catch (const Json::exception& e) {
    // Every field above is type-checked; this only keeps a missed one from escaping as a JSON library error.
    throw Error(ErrorKind::Parse, std::string("the .jslot file has an unreadable field: ") + e.what());
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
