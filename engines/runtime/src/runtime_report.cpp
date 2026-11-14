#include "sns/runtime/runtime_report.hpp"

#include <cctype>
#include <regex>

#include "sns/core/error.hpp"

using nlohmann::json;

namespace sns::runtime {

namespace {

const std::regex& form_key_regex() {
  static const std::regex re(R"(^([0-9A-Fa-f]{1,8}):([^\\/:]+\.(?:esp|esm|esl))$)",
                             std::regex_constants::icase);
  return re;
}

[[noreturn]] void fail(const std::string& message) {
  throw Error(ErrorKind::InvalidArgument, "runtime report: " + message);
}

std::string req_string(const json& j, const char* key, const std::string& where) {
  if (!j.contains(key) || !j.at(key).is_string()) fail(where + "." + key + " debe ser string");
  return j.at(key).get<std::string>();
}

std::string opt_string(const json& j, const char* key) {
  if (!j.contains(key) || j.at(key).is_null()) return {};
  if (!j.at(key).is_string()) fail(std::string(key) + " debe ser string");
  return j.at(key).get<std::string>();
}

bool opt_bool(const json& j, const char* key, bool fallback) {
  if (!j.contains(key) || j.at(key).is_null()) return fallback;
  if (!j.at(key).is_boolean()) fail(std::string(key) + " debe ser boolean");
  return j.at(key).get<bool>();
}

long long opt_i64(const json& j, const char* key, long long fallback) {
  if (!j.contains(key) || j.at(key).is_null()) return fallback;
  if (!j.at(key).is_number_integer()) fail(std::string(key) + " debe ser entero");
  return j.at(key).get<long long>();
}

Obs parse_obs_field(const json& j, const char* key, const std::string& where) {
  const std::string value = opt_string(j, key);
  if (value.empty()) return Obs::kUnknown;
  bool ok = false;
  const Obs obs = obs_from_string(value, ok);
  if (!ok) fail(where + "." + key + ": valor no válido '" + value + "'");
  return obs;
}

std::vector<std::string> parse_string_array(const json& j, const char* key) {
  std::vector<std::string> out;
  if (!j.contains(key) || j.at(key).is_null()) return out;
  if (!j.at(key).is_array()) fail(std::string(key) + " debe ser array");
  for (const auto& item : j.at(key)) {
    if (!item.is_string()) fail(std::string(key) + " debe ser array de strings");
    out.push_back(item.get<std::string>());
  }
  return out;
}

void check_form_key(const std::string& form_key, const std::string& where) {
  if (!form_key.empty() && !is_valid_form_key(form_key))
    fail(where + ": FormKey no válido '" + form_key + "'");
}

}  // namespace

bool is_valid_form_key(const std::string& form_key) {
  return std::regex_match(form_key, form_key_regex());
}

std::string normalize_form_key(const std::string& form_key) {
  std::smatch match;
  if (!std::regex_match(form_key, match, form_key_regex())) return {};
  std::string id = match[1].str();
  for (auto& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  const size_t first_non_zero = id.find_first_not_of('0');
  if (first_non_zero != std::string::npos && first_non_zero > 0 && id.size() > 1) {
    // Keep at least the significant digits; FormKeys are conventionally 8-wide but ESL ids are short.
    id = id.substr(id.size() > 8 ? first_non_zero : 0);
  }
  return id + ":" + match[2].str();
}

std::string form_key_local_id(const std::string& form_key) {
  std::smatch match;
  if (!std::regex_match(form_key, match, form_key_regex())) return {};
  std::string id = match[1].str();
  for (auto& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return id;
}

std::string form_key_plugin(const std::string& form_key) {
  std::smatch match;
  if (!std::regex_match(form_key, match, form_key_regex())) return {};
  return match[2].str();
}

const char* to_string(Obs value) {
  switch (value) {
    case Obs::kObserved: return "OBSERVED";
    case Obs::kMissing: return "MISSING";
    case Obs::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* to_string(SmpStatus value) {
  switch (value) {
    case SmpStatus::kActive: return "ACTIVE";
    case SmpStatus::kPresentNoPhysics: return "PRESENT_NO_PHYSICS";
    case SmpStatus::kNoSmp: return "NO_SMP";
    case SmpStatus::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* to_string(SelectionKind value) {
  switch (value) {
    case SelectionKind::kNpc: return "npc";
    case SelectionKind::kEmpty: return "empty";
    case SelectionKind::kNotNpc: return "not-npc";
  }
  return "empty";
}

Obs obs_from_string(const std::string& value, bool& ok) {
  ok = true;
  if (value == "OBSERVED") return Obs::kObserved;
  if (value == "MISSING") return Obs::kMissing;
  if (value == "UNKNOWN") return Obs::kUnknown;
  ok = false;
  return Obs::kUnknown;
}

SmpStatus smp_status_from_string(const std::string& value, bool& ok) {
  ok = true;
  if (value == "ACTIVE") return SmpStatus::kActive;
  if (value == "PRESENT_NO_PHYSICS") return SmpStatus::kPresentNoPhysics;
  if (value == "NO_SMP") return SmpStatus::kNoSmp;
  if (value == "UNKNOWN") return SmpStatus::kUnknown;
  ok = false;
  return SmpStatus::kUnknown;
}

SelectionKind selection_from_string(const std::string& value, bool& ok) {
  ok = true;
  if (value == "npc") return SelectionKind::kNpc;
  if (value == "empty") return SelectionKind::kEmpty;
  if (value == "not-npc") return SelectionKind::kNotNpc;
  ok = false;
  return SelectionKind::kEmpty;
}

RuntimeReport parse_runtime_report(const std::string& json_text) {
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::exception& e) {
    throw Error(ErrorKind::Parse, std::string("runtime report: JSON malformado: ") + e.what());
  }
  if (!j.is_object()) fail("el documento debe ser un objeto");

  RuntimeReport report;
  if (!j.contains("schemaVersion") || !j.at("schemaVersion").is_number_integer())
    fail("schemaVersion es obligatorio y entero");
  report.schema_version = j.at("schemaVersion").get<int>();
  if (report.schema_version != kSchemaVersion)
    fail("schemaVersion " + std::to_string(report.schema_version) + " no soportada (esperada " +
         std::to_string(kSchemaVersion) + ")");
  if (opt_string(j, "kind") != kReportKind) fail("kind debe ser '" + std::string(kReportKind) + "'");

  if (j.contains("generator") && j.at("generator").is_object()) {
    report.generator_name = opt_string(j.at("generator"), "name");
    report.generator_version = opt_string(j.at("generator"), "version");
  }
  report.captured_at_local = opt_string(j, "capturedAtLocal");

  if (j.contains("session") && j.at("session").is_object()) {
    const json& s = j.at("session");
    report.session.skyrim_version = opt_string(s, "skyrimVersion");
    report.session.skse_version = opt_string(s, "skseVersion");
    if (s.contains("plugins") && s.at("plugins").is_array()) {
      for (const auto& p : s.at("plugins")) {
        PluginPresence presence;
        presence.name = req_string(p, "name", "session.plugins[]");
        presence.loaded = opt_bool(p, "loaded", false);
        presence.version = opt_string(p, "version");
        report.session.plugins.push_back(std::move(presence));
      }
    }
  }

  if (j.contains("profile") && j.at("profile").is_object()) {
    report.profile.mo2_profile = opt_string(j.at("profile"), "mo2Profile");
    report.profile.detection = opt_string(j.at("profile"), "detection");
    if (report.profile.detection.empty()) report.profile.detection = "unknown";
    if (report.profile.detection != "confirmed" && report.profile.detection != "inferred" &&
        report.profile.detection != "unknown")
      fail("profile.detection no válido");
  }

  {
    const std::string selection = opt_string(j, "selection");
    if (selection.empty()) fail("selection es obligatorio (npc|empty|not-npc)");
    bool ok = false;
    report.selection = selection_from_string(selection, ok);
    if (!ok) fail("selection no válido '" + selection + "'");
  }

  if (j.contains("npc") && j.at("npc").is_object()) {
    const json& n = j.at("npc");
    NpcIdentity npc;
    npc.reference_form_id = opt_string(n, "referenceFormId");
    npc.reference_is_dynamic = opt_bool(n, "referenceIsDynamic", false);
    npc.base_form_key = normalize_form_key(req_string(n, "baseFormKey", "npc"));
    if (npc.base_form_key.empty()) fail("npc.baseFormKey no válido");
    npc.base_plugin = opt_string(n, "basePlugin");
    npc.editor_id = opt_string(n, "editorId");
    npc.display_name = opt_string(n, "displayName");
    npc.race_form_key = normalize_form_key(opt_string(n, "raceFormKey"));
    check_form_key(npc.race_form_key, "npc.raceFormKey");
    npc.race_editor_id = opt_string(n, "raceEditorId");
    npc.sex = opt_string(n, "sex");
    if (npc.sex.empty()) npc.sex = "unknown";
    if (npc.sex != "female" && npc.sex != "male" && npc.sex != "unknown")
      fail("npc.sex no válido");
    npc.has_3d = parse_obs_field(n, "has3d", "npc");
    report.npc = std::move(npc);
  } else if (report.selection == SelectionKind::kNpc) {
    fail("selection=npc pero falta el bloque npc");
  }

  if (j.contains("headParts") && j.at("headParts").is_array()) {
    for (const auto& hp : j.at("headParts")) {
      HeadPartEntry entry;
      entry.form_key = normalize_form_key(req_string(hp, "formKey", "headParts[]"));
      if (entry.form_key.empty()) fail("headParts[].formKey no válido");
      entry.editor_id = opt_string(hp, "editorId");
      entry.type = opt_string(hp, "type");
      report.head_parts.push_back(std::move(entry));
    }
  }

  if (j.contains("hair") && j.at("hair").is_object()) {
    const json& h = j.at("hair");
    report.hair.assigned = parse_obs_field(h, "assigned", "hair");
    report.hair.assigned_form_key = normalize_form_key(opt_string(h, "assignedFormKey"));
    check_form_key(report.hair.assigned_form_key, "hair.assignedFormKey");
    report.hair.geometry_observed = parse_obs_field(h, "geometryObserved", "hair");
    report.hair.geometry_triangles = opt_i64(h, "geometryTriangles", -1);
    const std::string smp = opt_string(h, "smpStatus");
    if (!smp.empty()) {
      bool ok = false;
      report.hair.smp_status = smp_status_from_string(smp, ok);
      if (!ok) fail("hair.smpStatus no válido '" + smp + "'");
    }
    report.hair.visual_status = opt_string(h, "visualStatus");
    if (report.hair.visual_status.empty()) report.hair.visual_status = "REQUIRES_CONFIRMACION";
  }

  if (j.contains("nodes") && j.at("nodes").is_object()) {
    const json& nd = j.at("nodes");
    report.nodes.head_nodes = parse_string_array(nd, "headNodes");
    if (nd.contains("hairNodes") && nd.at("hairNodes").is_array()) {
      for (const auto& hn : nd.at("hairNodes")) {
        NodeEntry entry;
        entry.name = req_string(hn, "name", "nodes.hairNodes[]");
        entry.triangles = opt_i64(hn, "triangles", -1);
        entry.cull_flagged = opt_bool(hn, "cullFlagged", false);
        report.nodes.hair_nodes.push_back(std::move(entry));
      }
    }
    report.nodes.facegen_geometry = parse_obs_field(nd, "faceGenGeometry", "nodes");
  }

  if (j.contains("equipment") && j.at("equipment").is_array()) {
    for (const auto& eq : j.at("equipment")) {
      EquipmentEntry entry;
      entry.slot = static_cast<int>(opt_i64(eq, "slot", -1));
      entry.form_key = normalize_form_key(opt_string(eq, "formKey"));
      check_form_key(entry.form_key, "equipment[].formKey");
      entry.editor_id = opt_string(eq, "editorId");
      entry.kind = opt_string(eq, "kind");
      entry.hides_hair = opt_bool(eq, "hidesHair", false);
      entry.mesh_path = opt_string(eq, "meshPath");
      entry.mesh_resolved = parse_obs_field(eq, "meshResolved", "equipment[]");
      report.equipment.push_back(std::move(entry));
    }
  }

  if (j.contains("distribution") && j.at("distribution").is_array()) {
    for (const auto& d : j.at("distribution")) {
      DistributionEvidence entry;
      entry.source = req_string(d, "source", "distribution[]");
      entry.detail = opt_string(d, "detail");
      entry.form_key = normalize_form_key(opt_string(d, "formKey"));
      check_form_key(entry.form_key, "distribution[].formKey");
      report.distribution.push_back(std::move(entry));
    }
  }

  if (j.contains("checks") && j.at("checks").is_array()) {
    for (const auto& c : j.at("checks")) {
      CheckEntry entry;
      entry.id = req_string(c, "id", "checks[]");
      entry.state = req_string(c, "state", "checks[]");
      if (entry.state != "PASS" && entry.state != "WARN" && entry.state != "FAIL" &&
          entry.state != "UNKNOWN")
        fail("checks[].state no válido '" + entry.state + "'");
      entry.detail = opt_string(c, "detail");
      report.checks.push_back(std::move(entry));
    }
  }

  report.warnings = parse_string_array(j, "warnings");
  report.unavailable_checks = parse_string_array(j, "unavailableChecks");
  return report;
}

json to_json(const RuntimeReport& report) {
  json j;
  j["schemaVersion"] = report.schema_version;
  j["kind"] = report.kind;
  j["generator"] = {{"name", report.generator_name}, {"version", report.generator_version}};
  j["capturedAtLocal"] = report.captured_at_local;
  j["session"] = {{"skyrimVersion", report.session.skyrim_version},
                  {"skseVersion", report.session.skse_version},
                  {"plugins", json::array()}};
  for (const auto& p : report.session.plugins)
    j["session"]["plugins"].push_back(
        {{"name", p.name}, {"loaded", p.loaded}, {"version", p.version}});
  j["profile"] = {{"mo2Profile", report.profile.mo2_profile},
                  {"detection", report.profile.detection}};
  j["selection"] = to_string(report.selection);
  if (report.npc) {
    const auto& n = *report.npc;
    j["npc"] = {{"referenceFormId", n.reference_form_id},
                {"referenceIsDynamic", n.reference_is_dynamic},
                {"baseFormKey", n.base_form_key},
                {"basePlugin", n.base_plugin},
                {"editorId", n.editor_id},
                {"displayName", n.display_name},
                {"raceFormKey", n.race_form_key},
                {"raceEditorId", n.race_editor_id},
                {"sex", n.sex},
                {"has3d", to_string(n.has_3d)}};
  } else {
    j["npc"] = nullptr;
  }
  j["headParts"] = json::array();
  for (const auto& hp : report.head_parts)
    j["headParts"].push_back(
        {{"formKey", hp.form_key}, {"editorId", hp.editor_id}, {"type", hp.type}});
  j["hair"] = {{"assigned", to_string(report.hair.assigned)},
               {"assignedFormKey", report.hair.assigned_form_key},
               {"geometryObserved", to_string(report.hair.geometry_observed)},
               {"geometryTriangles", report.hair.geometry_triangles},
               {"smpStatus", to_string(report.hair.smp_status)},
               {"visualStatus", report.hair.visual_status}};
  j["nodes"] = {{"headNodes", report.nodes.head_nodes},
                {"hairNodes", json::array()},
                {"faceGenGeometry", to_string(report.nodes.facegen_geometry)}};
  for (const auto& hn : report.nodes.hair_nodes)
    j["nodes"]["hairNodes"].push_back({{"name", hn.name},
                                       {"triangles", hn.triangles},
                                       {"cullFlagged", hn.cull_flagged}});
  j["equipment"] = json::array();
  for (const auto& eq : report.equipment)
    j["equipment"].push_back({{"slot", eq.slot},
                              {"formKey", eq.form_key},
                              {"editorId", eq.editor_id},
                              {"kind", eq.kind},
                              {"hidesHair", eq.hides_hair},
                              {"meshPath", eq.mesh_path},
                              {"meshResolved", to_string(eq.mesh_resolved)}});
  j["distribution"] = json::array();
  for (const auto& d : report.distribution)
    j["distribution"].push_back(
        {{"source", d.source}, {"detail", d.detail}, {"formKey", d.form_key}});
  j["checks"] = json::array();
  for (const auto& c : report.checks)
    j["checks"].push_back({{"id", c.id}, {"state", c.state}, {"detail", c.detail}});
  j["warnings"] = report.warnings;
  j["unavailableChecks"] = report.unavailable_checks;
  return j;
}

std::string serialize_runtime_report(const RuntimeReport& report) {
  return to_json(report).dump(2);
}

}  // namespace sns::runtime
