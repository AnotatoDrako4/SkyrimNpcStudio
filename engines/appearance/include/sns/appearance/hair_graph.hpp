#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sns/appearance/control_map.hpp"

namespace sns::appearance {

// Origin source of an NPC's hairstyle
enum class HairSourceKind {
  HeadPartOriginal,  // Origin plugin HeadPart
  HeadPartWinning,   // Winning override HeadPart
  FaceGen,           // Geometry or HairTint baked into FaceGen NIF
  WornEquipment,     // Worn armor / default outfit ARMO/ARMA
  Wig,               // Dedicated wig ARMO
  Spid,              // SPID _DISTR.ini distribution
  Hdf,               // Hairstyle Distribution Framework rule
  OtherDistributor   // KID, CID, SkyPatcher or other distributor
};

// Stage of assignment confidence
enum class AssignmentStage {
  StaticRecord,     // Static entry in ESP/ESM/FaceGen
  PlannedDynamic,   // Planned rule in distributor config (pending execution)
  ConfirmedInGame   // Verified with in-game evidence (VALIDADO_EN_SKYRIM)
};

// One concrete hair resource node in the assignment graph
struct HairResourceNode {
  std::string form_key;
  std::string editor_id;
  HairSourceKind source_kind = HairSourceKind::HeadPartWinning;
  AssignmentStage stage = AssignmentStage::StaticRecord;
  std::string origin_plugin;
  std::string winning_plugin;
  std::string record_mod;
  std::string effective_race;
  std::string body_family;  // "ube", "cbbe3ba", "bhunp", "vanilla", "unknown"
  std::string head_family;  // "ube", "highpolyhead", "vanilla", "unknown"

  std::string model_path;              // Data-relative NIF
  AssetSource mesh_source;             // Resolved location in VFS
  std::vector<std::string> texture_paths;
  std::vector<AssetSource> texture_sources;
  std::string physics_xml_path;        // Data-relative XML
  std::optional<AssetSource> physics_xml_source;

  uint32_t slot_mask = 0;              // Biped slot flags (e.g. 31 = Hair, 42 = LongHair)
  bool requires_fsmp = false;
  bool requires_hair_suppression_fix = false;
  bool requires_ube_patch = false;
  std::string condition_summary;
  std::vector<std::string> bones;
  bool suppressed_by_equipment = false;
  std::string suppressing_equipment;
};

// Full hair assignment graph for an NPC
struct HairAssignmentGraph {
  std::string npc_form_key;
  std::string npc_editor_id;
  std::string effective_race;
  std::string body_family;
  bool is_ube = false;
  std::vector<HairResourceNode> nodes;

  [[nodiscard]] bool has_active_hair() const;
  [[nodiscard]] const HairResourceNode* primary_hair() const;
  [[nodiscard]] std::vector<const HairResourceNode*> active_wigs() const;
  [[nodiscard]] std::vector<const HairResourceNode*> dynamic_assignments() const;
  [[nodiscard]] bool has_smp() const;
};

struct AuditSubject;

// Constructs the hair assignment graph by parsing an NPC's static headparts, wigs, and facegen
HairAssignmentGraph build_hair_assignment_graph(const AuditSubject& subject,
                                              const std::string& effective_race = "",
                                              const std::string& body_family = "");

// Formats a summary of the graph for audit evidence
std::string describe_hair_graph(const HairAssignmentGraph& graph);

}  // namespace sns::appearance
