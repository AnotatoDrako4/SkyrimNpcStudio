#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/appearance/npc_audit.hpp"

namespace sns::appearance {

// Correlation state between static audit and runtime observation
enum class CorrelationState {
  StaticValid,                 // Static audit clean, runtime clean
  RuntimeAssetMissing,         // NIF or DDS could not be found/loaded at runtime
  RuntimeGeometryMissing,      // Reference exists but hair node/geometry not drawn (bald)
  SmpDependencyError,          // SMP physics crashed or XML/DLL failed
  DynamicAssignmentSuspected,  // SPID/HDF gave an unexpected item/wig
  VisualFailureConfirmed,      // Baldness or visual defect confirmed by in-game report
  Inconclusive,                // Insufficient runtime data to confirm or refute
  ValidatedInSkyrim            // Fully verified and rendering correctly in-game
};

std::string correlation_state_to_string(CorrelationState state);

struct NpcRuntimeEquippedItem {
  uint32_t slot = 0;              // Biped slot (e.g. 30 Head, 31 Hair)
  std::string form_key;
  std::string name;
  std::string model_path;
  bool is_wig = false;
  bool has_addon_for_race = true;
};

struct NpcRuntimeNode {
  std::string name;
  bool attached = false;
  bool visible = false;
  std::vector<std::string> bones;
};

struct NpcRuntimeHairState {
  std::string assigned_source;     // "RECORD_HEADPART", "WORN_WIG", "SPID_DISTRIBUTION", "UNKNOWN"
  std::string head_part_form_key;
  bool facegen_geometry_observed = false;
  std::string facegen_hair_shape_name;
  std::optional<std::string> worn_wig_form_key;
  std::optional<uint32_t> worn_wig_slot;
  bool smp_active = false;
  std::string smp_xml_path;
  std::string visual_status;       // "BALD", "VISIBLE_STATIC", "VISIBLE_PHYSICS", "UNKNOWN", "VALIDATED_IN_SKYRIM"
};

struct NpcRuntimeReport {
  int schema_version = 1;
  std::string capture_timestamp_utc;
  std::string game_version;
  std::string skse_version;
  std::string profile;

  // NPC identification
  std::string base_form_key;
  std::string reference_form_id;   // Runtime FormID in hex (e.g. "0001A697")
  std::string editor_id;
  std::string name;
  std::string effective_race;
  std::string origin_plugin;
  std::string winning_plugin;

  // Hair & Appearance
  NpcRuntimeHairState hair;
  std::vector<NpcRuntimeNode> head_nodes;
  std::vector<NpcRuntimeEquippedItem> equipped_items;

  std::vector<std::string> diagnostics;
  std::vector<std::string> evidence;
};

struct RuntimeCorrelationReport {
  std::string npc_form_key;
  CorrelationState state = CorrelationState::Inconclusive;
  bool false_negative_detected = false;  // TRUE if static audit said clean but runtime proved defect!
  std::string false_negative_reason;
  std::vector<std::string> discrepancies;
  std::vector<std::string> suggested_actions;
  std::string explanation;
};

// Parses a JSON string into an NpcRuntimeReport. Returns nullopt on parse error or invalid schema.
std::optional<NpcRuntimeReport> parse_npc_runtime_report(std::string_view json_str);

// Serializes an NpcRuntimeReport into formatted JSON.
std::string serialize_npc_runtime_report(const NpcRuntimeReport& report);

// Correlates a static audit finding with a captured runtime report.
RuntimeCorrelationReport correlate_static_and_runtime(const NpcAudit& static_audit, const NpcRuntimeReport& runtime_report);

// Generates console command lines to inspect this NPC in Skyrim
std::vector<std::string> generate_ingame_inspection_commands(const std::string& runtime_form_id);

}  // namespace sns::appearance
