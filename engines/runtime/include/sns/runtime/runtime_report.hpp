#pragma once

// NPC Runtime Inspector - versioned runtime evidence report (schema v1).
//
// The report is produced inside Skyrim by the sns-runtime-inspector SKSE plugin (plugins/skse/)
// and consumed here, in Skyrim NPC Studio, for correlation against the static MO2 audit.
//
// Design rules (mission K3):
//  - JSON versionado y validado: schemaVersion is mandatory; unknown versions are rejected.
//  - Nunca conviertas UNKNOWN en OK: parsers keep UNKNOWN as UNKNOWN; only explicit evidence upgrades a state.
//  - No pointers, no raw memory, no personal data: only form keys, plugin names, node names and counts.
//  - Identity is the base NPC FormKey ("013BB9:Skyrim.esm"), never the display name.
//  - The runtime reference FormID is kept separately: dynamic references (0xFF......) are flagged, not trusted
//    as persistent identity.

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sns::runtime {

inline constexpr int kSchemaVersion = 1;
inline constexpr const char* kReportKind = "sns.npc-runtime-report";

// Tri-state evidence: never collapse UNKNOWN into a positive result.
enum class Obs { kObserved, kMissing, kUnknown };

enum class SmpStatus { kActive, kPresentNoPhysics, kNoSmp, kUnknown };

enum class SelectionKind { kNpc, kEmpty, kNotNpc };

struct PluginPresence {
  std::string name;      // e.g. "hdtSMP64.dll"
  bool loaded = false;
  std::string version;   // best effort; empty when the plugin does not report one
};

struct SessionInfo {
  std::string skyrim_version;  // e.g. "1.6.1170.0"
  std::string skse_version;    // e.g. "2.2.6"
  std::vector<PluginPresence> plugins;
};

struct ProfileInfo {
  std::string mo2_profile;  // empty when it could not be determined
  // "confirmed" (read from a reliable source), "inferred" (heuristic), "unknown".
  std::string detection = "unknown";
};

struct NpcIdentity {
  std::string reference_form_id;    // runtime FormID, hex "0x........"; may be dynamic
  bool reference_is_dynamic = false;  // 0xFF......: spawned, not a persistent placed reference
  std::string base_form_key;        // "013BB9:Skyrim.esm" - persistent identity
  std::string base_plugin;          // plugin of the winning base record at runtime
  std::string editor_id;            // optional, may be empty
  std::string display_name;         // display only, never identity
  std::string race_form_key;
  std::string race_editor_id;
  std::string sex;  // "female" | "male" | "unknown"
  Obs has_3d = Obs::kUnknown;
};

struct HeadPartEntry {
  std::string form_key;
  std::string editor_id;
  std::string type;  // HDPT type: "Hair", "Face", "Eyes", "Eyebrows", "FacialHair", "Scar", "Misc"
};

struct HairSummary {
  Obs assigned = Obs::kUnknown;
  std::string assigned_form_key;  // hair head part effective at runtime, when observed
  Obs geometry_observed = Obs::kUnknown;
  long long geometry_triangles = -1;  // -1 = not measured
  SmpStatus smp_status = SmpStatus::kUnknown;
  // Visual status is NEVER auto-confirmed: a human (or equivalent validated evidence) must attest.
  std::string visual_status = "REQUIRES_CONFIRMACION";
};

struct NodeEntry {
  std::string name;
  long long triangles = -1;  // -1 = not a geometry / not measured
  bool cull_flagged = false; // node marked not-visible by the scene graph
};

struct NodeSummary {
  std::vector<std::string> head_nodes;
  std::vector<NodeEntry> hair_nodes;
  Obs facegen_geometry = Obs::kUnknown;
};

struct EquipmentEntry {
  int slot = -1;              // biped slot (31 = hair/circlet zone, 42 = circlet...)
  std::string form_key;
  std::string editor_id;
  std::string kind;           // "wig" | "hood" | "circlet" | "helmet" | "other"
  bool hides_hair = false;    // occupies a slot that makes the game hide hair head parts
  std::string mesh_path;      // Data-relative addon mesh when known
  Obs mesh_resolved = Obs::kUnknown;  // mesh file observable from runtime data
};

struct DistributionEvidence {
  std::string source;   // "SPID" | "HDF" | "other"
  std::string detail;   // what was observed (log line summary, applied record)
  std::string form_key; // applied record, when known
};

struct CheckEntry {
  std::string id;      // stable check identifier, e.g. "HAIR_NODE_ABSENT"
  std::string state;   // "PASS" | "WARN" | "FAIL" | "UNKNOWN"
  std::string detail;
};

struct RuntimeReport {
  int schema_version = kSchemaVersion;
  std::string kind = kReportKind;
  std::string generator_name;
  std::string generator_version;
  std::string captured_at_local;  // ISO-8601 local time
  SessionInfo session;
  ProfileInfo profile;
  SelectionKind selection = SelectionKind::kEmpty;
  std::optional<NpcIdentity> npc;  // absent when selection is empty / not an NPC
  std::vector<HeadPartEntry> head_parts;
  HairSummary hair;
  NodeSummary nodes;
  std::vector<EquipmentEntry> equipment;
  std::vector<DistributionEvidence> distribution;
  std::vector<CheckEntry> checks;
  std::vector<std::string> warnings;
  std::vector<std::string> unavailable_checks;
};

// ---- FormKey helpers --------------------------------------------------------------------------

// Validates "XXXXXXXX:plugin.esp" (also .esm/.esl, 1-8 hex digits local id).
bool is_valid_form_key(const std::string& form_key);

// Normalizes hex to uppercase; plugin part unchanged. Returns "" when invalid.
std::string normalize_form_key(const std::string& form_key);

// Local hex id part ("013BB9") uppercased, "" when invalid.
std::string form_key_local_id(const std::string& form_key);

// Plugin part ("Skyrim.esm"), "" when invalid.
std::string form_key_plugin(const std::string& form_key);

// ---- Parsing / serialization -------------------------------------------------------------------

// Parses and strictly validates a report. Throws sns::Error(Parse) on malformed JSON and
// sns::Error(InvalidArgument) on schema violations (unknown version, bad types, bad form keys).
RuntimeReport parse_runtime_report(const std::string& json_text);

// Serializes back to JSON (pretty, stable key order as declared). Round-trip safe.
nlohmann::json to_json(const RuntimeReport& report);
std::string serialize_runtime_report(const RuntimeReport& report);

const char* to_string(Obs value);
const char* to_string(SmpStatus value);
const char* to_string(SelectionKind value);
Obs obs_from_string(const std::string& value, bool& ok);
SmpStatus smp_status_from_string(const std::string& value, bool& ok);
SelectionKind selection_from_string(const std::string& value, bool& ok);

}  // namespace sns::runtime
