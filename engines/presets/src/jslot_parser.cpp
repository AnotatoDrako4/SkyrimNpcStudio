#include "sns/presets/jslot_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>

#include "sns/core/text.hpp"

namespace sns::presets {

using Json = nlohmann::json;

namespace {

projects::ColorRgb argb_to_rgb(std::uint32_t argb) {
  return projects::ColorRgb{
      .r = static_cast<std::uint8_t>((argb >> 16) & 0xFF),
      .g = static_cast<std::uint8_t>((argb >> 8) & 0xFF),
      .b = static_cast<std::uint8_t>(argb & 0xFF),
  };
}

std::string head_part_type_name(int type) {
  switch (type) {
    case 0: return "Face";
    case 1: return "Body";
    case 2: return "Hair";
    case 3: return "Eyes";
    case 4: return "FacialHair";
    case 5: return "Eyebrows";
    case 6: return "Scars";
    default: return "Misc";
  }
}

}  // namespace

PresetItem parse_jslot(std::string_view json_str, std::string default_name) {
  PresetItem item;
  item.source = "jslot";
  item.name = default_name.empty() ? "RaceMenu Preset" : std::move(default_name);

  auto json = Json::parse(json_str, nullptr, false);
  if (json.is_discarded()) {
    item.description = "Archivo .jslot con JSON malformado";
    return item;
  }

  // Header / basic info
  if (json.contains("header") && json["header"].is_object()) {
    const auto& h = json["header"];
    item.name = h.value("name", item.name);
    item.race = h.value("race", "");
    int g = h.value("gender", 1);
    item.sex = (g == 0) ? "male" : "female";
  } else {
    item.name = json.value("name", item.name);
    item.race = json.value("race", "");
    if (json.contains("gender")) {
      int g = json.value("gender", 1);
      item.sex = (g == 0) ? "male" : "female";
    } else if (json.contains("sex")) {
      item.sex = json.value("sex", "female");
    } else {
      item.sex = "female";
    }
  }

  item.id = item.name;
  std::replace(item.id.begin(), item.id.end(), ' ', '_');

  item.weight = json.value("weight", 100.0f);
  item.height = json.value("height", 1.0f);

  // Hair color
  if (json.contains("hairColor")) {
    if (json["hairColor"].is_number_integer()) {
      auto raw = json["hairColor"].get<std::uint32_t>();
      item.hair_color = argb_to_rgb(raw);
    } else if (json["hairColor"].is_object()) {
      item.hair_color = projects::ColorRgb{
          .r = json["hairColor"].value("r", static_cast<std::uint8_t>(255)),
          .g = json["hairColor"].value("g", static_cast<std::uint8_t>(255)),
          .b = json["hairColor"].value("b", static_cast<std::uint8_t>(255)),
      };
    }
  }

  // Skin tone
  if (json.contains("skinTone")) {
    if (json["skinTone"].is_number_integer()) {
      auto raw = json["skinTone"].get<std::uint32_t>();
      item.skin_tone = argb_to_rgb(raw);
    } else if (json["skinTone"].is_object()) {
      item.skin_tone = projects::ColorRgb{
          .r = json["skinTone"].value("r", static_cast<std::uint8_t>(255)),
          .g = json["skinTone"].value("g", static_cast<std::uint8_t>(255)),
          .b = json["skinTone"].value("b", static_cast<std::uint8_t>(255)),
      };
    }
  }

  // Head parts
  if (json.contains("headParts") && json["headParts"].is_array()) {
    for (const auto& hp : json["headParts"]) {
      PresetHeadPart part;
      if (hp.contains("type")) {
        if (hp["type"].is_number()) part.type = head_part_type_name(hp["type"].get<int>());
        else part.type = hp["type"].get<std::string>();
      }
      part.editor_id = hp.value("editorId", hp.value("name", ""));
      part.plugin = hp.value("modName", hp.value("plugin", ""));
      if (hp.contains("formId")) {
        if (hp["formId"].is_number()) {
          part.form_id = std::format("{:08X}", hp["formId"].get<std::uint32_t>());
        } else {
          part.form_id = hp["formId"].get<std::string>();
        }
      }
      part.mesh_path = hp.value("meshPath", "");
      if (!part.plugin.empty()) item.dependencies.push_back(part.plugin);
      item.head_parts.push_back(std::move(part));
    }
  }

  // Morphs / sliders
  if (json.contains("morphs")) {
    if (json["morphs"].is_array()) {
      for (const auto& m : json["morphs"]) {
        if (m.is_object() && m.contains("name") && m.contains("value")) {
          item.morphs[m["name"].get<std::string>()] = m["value"].get<float>();
        }
      }
    } else if (json["morphs"].is_object()) {
      for (auto it = json["morphs"].begin(); it != json["morphs"].end(); ++it) {
        if (it.value().is_number()) item.morphs[it.key()] = it.value().get<float>();
      }
    }
  }

  if (json.contains("sliders") && json["sliders"].is_array()) {
    for (const auto& s : json["sliders"]) {
      if (s.is_object() && s.contains("id") && s.contains("value")) {
        item.morphs[s["id"].get<std::string>()] = s["value"].get<float>();
      }
    }
  }

  // Tint layers
  if (json.contains("tints") && json["tints"].is_array()) {
    for (const auto& t : json["tints"]) {
      PresetTint tint;
      tint.index = t.value("index", 0);
      tint.alpha = t.value("interpolation", 1.0f);
      tint.texture_path = t.value("texture", "");
      if (t.contains("color")) {
        if (t["color"].is_number()) {
          tint.color = argb_to_rgb(t["color"].get<std::uint32_t>());
        } else if (t["color"].is_object()) {
          tint.color = projects::ColorRgb{
              .r = t["color"].value("r", static_cast<std::uint8_t>(255)),
              .g = t["color"].value("g", static_cast<std::uint8_t>(255)),
              .b = t["color"].value("b", static_cast<std::uint8_t>(255)),
          };
        }
      }
      item.tints.push_back(std::move(tint));
    }
  }

  // Sculpt data
  if (json.contains("sculpt") && !json["sculpt"].empty()) {
    item.has_sculpt_data = true;
  }

  // Deduplicate dependencies
  std::sort(item.dependencies.begin(), item.dependencies.end());
  item.dependencies.erase(std::unique(item.dependencies.begin(), item.dependencies.end()), item.dependencies.end());

  return item;
}

CompatibilityReport evaluate_compatibility(
    const PresetItem& preset,
    const projects::NpcTarget& target,
    const std::string& target_race,
    const std::string& target_sex,
    const std::vector<std::string>& active_plugins) {
  CompatibilityReport report;

  if (!preset.sex.empty() && !target_sex.empty() && preset.sex != "any" &&
      !text::iequals_ascii(preset.sex, target_sex)) {
    report.compatible = false;
    report.issues.push_back(
        std::format("Incompatibilidad de sexo: el preset es para '{}' pero el NPC objetivo '{}' es '{}'.",
                    preset.sex, target.name.empty() ? target.form_key : target.name, target_sex));
  }

  if (!preset.race.empty() && !target_race.empty() &&
      !text::iequals_ascii(preset.race, target_race)) {
    report.issues.push_back(
        std::format("Advertencia de raza: el preset fue creado para '{}' y el NPC objetivo es '{}'. La topología puede diferir.",
                    preset.race, target_race));
  }

  if (!active_plugins.empty()) {
    for (const auto& dep : preset.dependencies) {
      bool found = false;
      for (const auto& p : active_plugins) {
        if (text::iequals_ascii(p, dep)) {
          found = true;
          break;
        }
      }
      if (!found) {
        report.missing_dependencies.push_back(dep);
        report.issues.push_back(std::format("Dependencia de mod no activa en el perfil: '{}'", dep));
      }
    }
  }

  if (preset.has_sculpt_data) {
    report.non_exportable_features.push_back("Escultura facial RaceMenu (sculpt data)");
    report.issues.push_back("El preset contiene datos de escultura personalizada que requieren regeneración de FaceGen en Creation Kit.");
  }

  report.exportable = report.compatible && report.missing_dependencies.empty();
  return report;
}

}  // namespace sns::presets
