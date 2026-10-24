#include "sns/body/bcng_rules.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <set>

#include "sns/body/bodyslide_presets.hpp"
#include "sns/core/error.hpp"

namespace sns::body::bcng {

using Json = nlohmann::json;
using OJson = nlohmann::ordered_json;

namespace {

constexpr std::string_view kScopeNames[] = {"all",     "npc",     "name",  "faction", "plugin",     "race",
                                            "customFollowers",    "elders", "keyword", "class", "combatStyle"};

[[noreturn]] void fail(std::string_view message) { throw Error(ErrorKind::Parse, std::string(message)); }
void check(bool ok, std::string_view message) {
  if (!ok) fail(message);
}

void keys(const Json& value, std::initializer_list<std::string_view> allowed) {
  check(value.is_object(), "expected a JSON object");
  for (const auto& [key, ignored] : value.items()) {
    (void)ignored;
    check(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field: " + key);
  }
}

// BCNG Text(): a string of 1..1024 UTF-8 bytes.
std::string text_value(const Json& value) {
  check(value.is_string(), "expected a name/string");
  auto s = value.get<std::string>();
  check(!s.empty() && s.size() <= kMaxTextBytes, "name/string must contain 1..1024 UTF-8 bytes");
  return s;
}

bool bool_value(const Json& source, const char* key, bool fallback) {
  if (!source.contains(key)) return fallback;
  check(source.at(key).is_boolean(), std::string(key) + " must be true or false");
  return source.at(key).get<bool>();
}

std::uint32_t form_id_value(const Json& value) {
  std::uint32_t result = 0;
  if (value.is_number_unsigned()) {
    const auto n = value.get<std::uint64_t>();
    check(n <= 0xFFFFFFu, "formId is outside the local range");
    result = static_cast<std::uint32_t>(n);
  } else {
    const auto t = text_value(value);
    std::string_view digits = t;
    if (digits.starts_with("0x") || digits.starts_with("0X")) digits.remove_prefix(2);
    std::uint64_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
    check(!digits.empty() && ec == std::errc{} && ptr == digits.data() + digits.size(), "invalid hexadecimal local formId");
    check(parsed <= 0xFFFFFFu, "formId must be a local ID, not a runtime RefID");
    result = static_cast<std::uint32_t>(parsed);
  }
  check(result != 0, "formId must be a local ID, not a runtime RefID");
  return result;
}

PresetRef preset_value(const Json& input) {
  if (input.is_string()) return {text_value(input), {}};
  keys(input, {"name", "file", "type", "texture", "id"});
  if (input.contains("id")) {
    check(input.size() == 1, "id cannot be combined with asset name qualifiers");
    // Migration form "<relative xml path>\x1F<name>".
    const auto id = text_value(input.at("id"));
    const auto sep = id.find('\x1F');
    if (sep == std::string::npos) return {id, {}};
    return {id.substr(sep + 1), id.substr(0, sep)};
  }
  check(input.contains("name"), "asset requires name or id");
  for (const auto& [k, v] : input.items()) {
    (void)k;
    (void)text_value(v);
  }
  check(!input.contains("type") && !input.contains("texture"),
        "type/texture qualifiers are not used on body presets (the director only edits name/file)");
  check(std::string_view("@BCNG-asset:").size() + input.dump().size() <= 4096, "qualified asset reference is too long");
  return {input.at("name").get<std::string>(), input.value("file", std::string{})};
}

Rule decode_rule(const Json& source) {
  keys(source, {"id", "name", "nameKey", "enabled", "sex", "scope", "target", "presets", "skins", "futaSkins", "overlays",
                "excludeCustomFollowers", "excludeElders", "excludeChildren"});
  check(source.contains("scope"), "missing field: scope");
  const auto scope_text = text_value(source.at("scope"));
  const auto scope = scope_from_name(scope_text);
  check(scope.has_value(), "unknown scope: " + scope_text);
  check(source.contains("sex"), "missing field: sex");
  const auto sex = text_value(source.at("sex"));
  check(sex == "female" || sex == "male", "sex must be female or male");

  Rule r;
  r.scope = *scope;
  r.sex = sex;
  r.enabled = bool_value(source, "enabled", true);
  r.exclude_custom_followers = bool_value(source, "excludeCustomFollowers", true);
  r.exclude_elders = bool_value(source, "excludeElders", true);
  r.exclude_children = bool_value(source, "excludeChildren", true);
  if (source.contains("id")) r.id = text_value(source.at("id"));
  if (source.contains("name")) r.name = text_value(source.at("name"));
  if (source.contains("nameKey")) r.passthrough["nameKey"] = text_value(source.at("nameKey"));

  if (!scope_takes_target(r.scope)) {
    check(!source.contains("target"), "this scope takes no target");
  } else {
    check(source.contains("target"), "missing field: target");
    const auto& t = source.at("target");
    if (r.scope == Scope::Name || r.scope == Scope::Plugin) {
      check(t.is_string(), "name/plugin target must be a string");
      check(t.get_ref<const std::string&>().size() <= kMaxNamePluginTargetBytes, "name/plugin target is too long");
      r.target = {Target::Kind::Text, t.get<std::string>(), {}, 0, {}};
    } else if (t.is_object() && t.contains("formId")) {
      keys(t, {"plugin", "formId", "label"});
      check(t.contains("plugin"), "missing field: plugin");
      r.target.kind = Target::Kind::Form;
      r.target.plugin = text_value(t.at("plugin"));
      r.target.form_id = form_id_value(t.at("formId"));
      if (t.contains("label")) r.target.label = text_value(t.at("label"));
    } else if (t.is_string()) {
      check(t.get_ref<const std::string&>().size() <= kMaxTextBytes, "target is too long");
      r.target = {Target::Kind::Text, t.get<std::string>(), {}, 0, {}};
    } else {
      keys(t, {"name", "plugin"});
      check(t.contains("name"), "missing field: name");
      r.target.kind = Target::Kind::NamePlugin;
      r.target.text = text_value(t.at("name"));
      if (t.contains("plugin")) r.target.plugin = text_value(t.at("plugin"));
    }
  }

  if (source.contains("presets")) {
    const auto& p = source.at("presets");
    if (p.is_array()) {
      for (const auto& e : p) r.presets.push_back(preset_value(e));
    } else {
      r.presets.push_back(preset_value(p));
    }
  }
  // Skins, futa skins and overlays are not edited by the director: kept verbatim after a shape check.
  for (const auto* key : {"skins", "futaSkins"}) {
    if (!source.contains(key)) continue;
    const auto& v = source.at(key);
    check(v.is_array() || v.is_string() || v.is_object(), std::string(key) + " must be an asset or a list of assets");
    r.passthrough[key] = v;
  }
  if (source.contains("overlays")) {
    keys(source.at("overlays"), {"face", "body", "hands", "feet"});
    r.passthrough["overlays"] = source.at("overlays");
  }
  return r;
}

std::size_t pool_size(const Json& v) {
  if (v.is_array()) return v.size();
  return v.is_null() ? 0 : 1;
}

}  // namespace

std::string_view scope_name(Scope scope) { return kScopeNames[static_cast<std::size_t>(scope)]; }

std::optional<Scope> scope_from_name(std::string_view name) {
  for (std::size_t i = 0; i < std::size(kScopeNames); ++i) {
    if (kScopeNames[i] == name) return static_cast<Scope>(i);
  }
  return std::nullopt;
}

bool scope_takes_target(Scope scope) {
  return scope != Scope::All && scope != Scope::CustomFollowers && scope != Scope::Elders;
}

bool scope_is_form(Scope scope) {
  return scope == Scope::Npc || scope == Scope::Faction || scope == Scope::Race || scope == Scope::Keyword ||
         scope == Scope::Class || scope == Scope::CombatStyle;
}

const std::vector<Scope>& all_scopes() {
  static const std::vector<Scope> scopes = {Scope::All,     Scope::Npc,  Scope::Name,  Scope::Faction,
                                            Scope::Plugin,  Scope::Race, Scope::CustomFollowers, Scope::Elders,
                                            Scope::Keyword, Scope::Class, Scope::CombatStyle};
  return scopes;
}

Target form_target(std::string_view form_key, std::string label, bool light) {
  const auto colon = form_key.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= form_key.size()) {
    throw Error(ErrorKind::InvalidArgument, std::format("FormKey inválida: «{}» (se espera 0A2C8E:Plugin.esp)", form_key));
  }
  const auto hex = form_key.substr(0, colon);
  std::uint32_t id = 0;
  const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
  if (ec != std::errc{} || ptr != hex.data() + hex.size() || id > 0xFFFFFFu) {
    throw Error(ErrorKind::InvalidArgument, std::format("FormKey inválida: «{}»", form_key));
  }
  if (light) id &= 0xFFFu;
  if (id == 0) throw Error(ErrorKind::InvalidArgument, std::format("FormKey sin ID local: «{}»", form_key));
  Target t;
  t.kind = Target::Kind::Form;
  t.plugin = std::string(form_key.substr(colon + 1));
  t.form_id = id;
  t.label = std::move(label);
  return t;
}

std::string target_form_key(const Target& target) {
  if (target.kind != Target::Kind::Form) return {};
  return std::format("{:06X}:{}", target.form_id, target.plugin);
}

std::size_t Rule::feature_count() const {
  std::size_t n = presets.empty() ? 0 : 1;
  for (const auto* key : {"skins", "futaSkins"}) {
    if (passthrough.contains(key) && pool_size(passthrough.at(key)) > 0) ++n;
  }
  if (passthrough.contains("overlays")) {
    for (const auto& [area, pool] : passthrough.at("overlays").items()) {
      (void)area;
      if (pool_size(pool) > 0) {
        ++n;  // BCNG keeps all overlay areas in one normalized row
        break;
      }
    }
  }
  return n;
}

std::size_t Distribution::normalized_rule_count() const {
  std::size_t n = 0;
  for (const auto& r : rules) n += r.feature_count();
  return n;
}

Distribution parse_distribution(std::string_view text) {
  Json root;
  try {
    root = Json::parse(text, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
  } catch (const Json::parse_error& e) {
    fail(std::format("JSON inválido: {}", e.what()));
  }
  check(root.is_object(), "distribution root must be an object");
  const auto schema_value = root.contains("schemaVersion") ? root.at("schemaVersion") : Json(0);
  check(schema_value.is_number_integer(), "unsupported distribution schema");
  const auto schema = schema_value.get<int>();
  check(root.contains("rules") && root.at("rules").is_array(), "unsupported distribution schema");
  check(schema == kSchemaVersion,
        std::format("schemaVersion {}: BCNG migra los esquemas 3..7 al cargarlos; este editor solo escribe y valida el 8", schema));
  keys(root, {"schemaVersion", "rules"});
  Distribution d;
  std::size_t index = 0;
  for (const auto& rule : root.at("rules")) {
    try {
      d.rules.push_back(decode_rule(rule));
    } catch (const Error& e) {
      fail(std::format("rules[{}]: {}", index, e.what()));
    } catch (const Json::exception& e) {
      fail(std::format("rules[{}]: {}", index, e.what()));
    }
    ++index;
  }
  return d;
}

std::vector<std::string> structural_errors(const Distribution& d) {
  std::vector<std::string> out;
  std::set<std::string> ids;
  auto text_ok = [](const std::string& s, std::size_t max = kMaxTextBytes) { return !s.empty() && s.size() <= max; };
  for (std::size_t i = 0; i < d.rules.size(); ++i) {
    const auto& r = d.rules[i];
    auto err = [&](std::string_view m) { out.push_back(std::format("rules[{}]: {}", i, m)); };
    if (!text_ok(r.id)) err("id must contain 1..1024 UTF-8 bytes (BCNG renames rules without id, which rerolls their picks)");
    else if (!ids.insert(r.id).second) err(std::format("duplicate id «{}» (BCNG renames it and rerolls the pick)", r.id));
    if (!r.name.empty() && r.name.size() > kMaxTextBytes) err("name is longer than 1024 bytes");
    if (r.sex != "female" && r.sex != "male") err("sex must be female or male");
    if (!scope_takes_target(r.scope)) {
      if (r.target.kind != Target::Kind::None) err("this scope takes no target");
    } else if (r.scope == Scope::Name || r.scope == Scope::Plugin) {
      if (r.target.kind != Target::Kind::Text || !text_ok(r.target.text, kMaxNamePluginTargetBytes)) {
        err("name/plugin target must be a non-empty string of at most 512 bytes");
      }
    } else {
      switch (r.target.kind) {
        case Target::Kind::None: err("missing target"); break;
        case Target::Kind::Text:
          if (!text_ok(r.target.text)) err("target must contain 1..1024 UTF-8 bytes");
          break;
        case Target::Kind::NamePlugin:
          if (!text_ok(r.target.text)) err("target name must contain 1..1024 UTF-8 bytes");
          if (!r.target.plugin.empty() && !text_ok(r.target.plugin)) err("target plugin is too long");
          break;
        case Target::Kind::Form:
          if (!text_ok(r.target.plugin)) err("target plugin must contain 1..1024 UTF-8 bytes");
          if (r.target.form_id == 0 || r.target.form_id > 0xFFFFFFu) err("formId must be a local ID, not a runtime RefID");
          if (r.target.label.size() > kMaxTextBytes) err("target label is too long");
          break;
      }
    }
    for (const auto& p : r.presets) {
      if (!text_ok(p.name)) err("preset name must contain 1..1024 UTF-8 bytes");
      if (!p.file.empty() && p.file.size() > kMaxTextBytes) err("preset file is too long");
    }
  }
  if (const auto n = d.normalized_rule_count(); n > kMaxRules) {
    out.push_back(std::format("{} reglas tras la normalización de BCNG: el límite es {} y el resto se descartaría", n, kMaxRules));
  }
  if (d.rules.size() > kMaxRules) {
    out.push_back(std::format("{} reglas de origen: BCNG solo lee las primeras {}", d.rules.size(), kMaxRules));
  }
  return out;
}

std::string write_distribution(const Distribution& d) {
  if (const auto errors = structural_errors(d); !errors.empty()) throw Error(ErrorKind::InvalidArgument, errors.front());
  OJson root = OJson::object();
  root["schemaVersion"] = kSchemaVersion;
  root["rules"] = OJson::array();
  for (const auto& r : d.rules) {
    OJson row = OJson::object();
    row["id"] = r.id;
    if (!r.name.empty()) row["name"] = r.name;
    row["sex"] = r.sex;
    row["scope"] = std::string(scope_name(r.scope));
    if (r.passthrough.contains("nameKey")) row["nameKey"] = r.passthrough.at("nameKey").get<std::string>();
    if (!r.enabled) row["enabled"] = false;
    // BCNG's own encoder always writes the three filters; they only act on scope "all".
    row["excludeCustomFollowers"] = r.exclude_custom_followers;
    row["excludeElders"] = r.exclude_elders;
    row["excludeChildren"] = r.exclude_children;
    switch (r.target.kind) {
      case Target::Kind::None: break;
      case Target::Kind::Text: row["target"] = r.target.text; break;
      case Target::Kind::NamePlugin: {
        OJson t = OJson::object();
        t["name"] = r.target.text;
        if (!r.target.plugin.empty()) t["plugin"] = r.target.plugin;
        row["target"] = std::move(t);
        break;
      }
      case Target::Kind::Form: {
        OJson t = OJson::object();
        t["plugin"] = r.target.plugin;
        t["formId"] = std::format("0x{:06x}", r.target.form_id);
        if (!r.target.label.empty()) t["label"] = r.target.label;
        row["target"] = std::move(t);
        break;
      }
    }
    if (!r.presets.empty()) {
      OJson pool = OJson::array();
      for (const auto& p : r.presets) {
        if (p.file.empty()) {
          pool.push_back(p.name);
        } else {
          OJson q = OJson::object();
          q["name"] = p.name;
          q["file"] = p.file;
          pool.push_back(std::move(q));
        }
      }
      row["presets"] = std::move(pool);
    }
    for (const auto* key : {"skins", "futaSkins", "overlays"}) {
      if (r.passthrough.contains(key)) row[key] = OJson::parse(r.passthrough.at(key).dump());
    }
    root["rules"].push_back(std::move(row));
  }
  return root.dump(2) + "\n";
}

unsigned Settings::female_mask() const {
  switch (female_npc_body_type) {
    case 0: return kFamCbbe;
    case 1: return kFamUnp;
    case 2: return kFamUbe;
    default: return 0;
  }
}

std::string Settings::female_label() const {
  switch (female_npc_body_type) {
    case 0: return "CBBE 3BA";
    case 1: return "BHUNP / UNP";
    case 2: return "UBE";
    case 3: return "Vanilla (sin preset)";
    default: return std::format("valor desconocido {}", female_npc_body_type);
  }
}

Settings parse_settings(std::string_view text) {
  Json root;
  try {
    root = Json::parse(text, nullptr, true, true);
  } catch (const Json::parse_error& e) {
    fail(std::format("settings.json inválido: {}", e.what()));
  }
  check(root.is_object(), "settings.json: la raíz no es un objeto");
  Settings s;
  s.from_file = true;
  auto read_int = [&](const char* key, int& out) {
    if (root.contains(key) && root.at(key).is_number_integer()) out = root.at(key).get<int>();
  };
  read_int("femaleNpcBodyType", s.female_npc_body_type);
  read_int("maleNpcBodyType", s.male_npc_body_type);
  return s;
}

}  // namespace sns::body::bcng
