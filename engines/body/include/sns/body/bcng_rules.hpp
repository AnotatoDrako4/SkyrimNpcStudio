#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace sns::body::bcng {

// Body Change NG v1.3.7 distribution file, SKSE\Plugins\BodyChangeNGdistribution.json, schema 8
// (src/BodyChangeNG/DistributionAuthoring.*). The root is exactly {"schemaVersion":8,"rules":[...]};
// BCNG rejects the WHOLE file on the first bad rule, so parse_distribution() is as strict as its decoder
// and the writer only emits what the decoder accepts. Order is priority: the first matching rule with a
// compatible candidate wins. There is no AND/OR, weight, chance or percentage in the format.
inline constexpr int kSchemaVersion = 8;
inline constexpr std::size_t kMaxRules = 256;  // after per-feature normalization
inline constexpr std::size_t kMaxTextBytes = 1024;
inline constexpr std::size_t kMaxNamePluginTargetBytes = 512;
inline constexpr std::string_view kDistributionPath = "SKSE\\Plugins\\BodyChangeNGdistribution.json";
inline constexpr std::string_view kSettingsPath = "SKSE\\Plugins\\BodyChangeNG\\settings.json";

enum class Scope { All, Npc, Name, Faction, Plugin, Race, CustomFollowers, Elders, Keyword, Class, CombatStyle };

std::string_view scope_name(Scope scope);                     // exact BCNG spelling ("combatStyle", ...)
std::optional<Scope> scope_from_name(std::string_view name);  // case-sensitive like BCNG
bool scope_takes_target(Scope scope);                         // false for all, customFollowers, elders
bool scope_is_form(Scope scope);                              // npc, faction, race, keyword, class, combatStyle
const std::vector<Scope>& all_scopes();

struct Target {
  enum class Kind { None, Text, NamePlugin, Form };
  Kind kind = Kind::None;
  std::string text;           // Text: EditorID-or-name (form scopes), display name (name), plugin file (plugin)
  std::string plugin;         // NamePlugin, Form
  std::uint32_t form_id = 0;  // Form: local ID in `plugin` (light plugins: 0..0xFFF)
  std::string label;          // Form: informative note

  bool operator==(const Target&) const = default;
};

// A target that cannot be misread: {"plugin","formId","label"} from a FormKey "0A2C8E:Skyrim.esm".
// `light` gives the 3-digit local ID BCNG expects for ESL-flagged plugins. Throws Error(InvalidArgument).
Target form_target(std::string_view form_key, std::string label, bool light = false);
// "0A2C8E:Skyrim.esm" for a Form target ("" for other kinds).
std::string target_form_key(const Target& target);

struct PresetRef {
  std::string name;  // exact <Preset name>; BCNG compares case-sensitively
  std::string file;  // optional: XML path relative to SliderPresets ("Folder/X.xml") to pick among duplicates

  bool operator==(const PresetRef&) const = default;
};

struct Rule {
  std::string id;    // seeds BCNG's per-actor pick: keep it stable
  std::string name;  // display only
  bool enabled = true;
  std::string sex = "female";
  Scope scope = Scope::Npc;
  Target target;
  std::vector<PresetRef> presets;
  bool exclude_custom_followers = true;  // only meaningful on scope all
  bool exclude_elders = true;
  bool exclude_children = true;
  nlohmann::json passthrough = nlohmann::json::object();  // nameKey/skins/futaSkins/overlays kept verbatim

  // Pools BCNG's normalization splits into separate rules: presets, skins, futaSkins, overlays.
  [[nodiscard]] std::size_t feature_count() const;
};

struct Distribution {
  std::vector<Rule> rules;
  [[nodiscard]] std::size_t normalized_rule_count() const;  // what BCNG counts against kMaxRules
};

// Strict schema 8 decoder (comments allowed, trailing commas not). Errors are Error(Parse) with
// BCNG-style "rules[i]: ..." messages.
Distribution parse_distribution(std::string_view text);
// The checks write_distribution enforces, as "rules[i]: ..." messages; empty when the file is writable.
std::vector<std::string> structural_errors(const Distribution& distribution);
// Canonical schema 8 text, 2-space indent, UTF-8, ends with a newline. Throws Error(InvalidArgument) with
// the first structural error when BCNG could not read it back.
std::string write_distribution(const Distribution& distribution);

// BCNG's global body families (settings.json): which family automatic presets must match.
struct Settings {
  int female_npc_body_type = 0;  // 0 cbbe3ba, 1 bhunpUnp, 2 ube, 3 vanilla
  int male_npc_body_type = 0;    // 0 himbo, 1 sam, 2 vanilla
  bool from_file = false;        // false: BCNG defaults (no settings.json in the profile)
  [[nodiscard]] unsigned female_mask() const;  // FamilyBit of the global female family, 0 for vanilla
  [[nodiscard]] std::string female_label() const;
};
Settings parse_settings(std::string_view text);  // other keys are ignored; malformed JSON throws Error(Parse)

}  // namespace sns::body::bcng
