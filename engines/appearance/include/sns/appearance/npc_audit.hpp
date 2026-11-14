#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sns/appearance/control_map.hpp"

// NPC appearance audit (NPC Health & Beauty Center): turns the control map of one NPC plus the facts the
// studio gathered about its head parts into coded findings (HAIR_xxx / FACE_xxx / NPC_xxx), each with
// evidence, severity, confidence, probable cause, suggested action and verification state.
//
// Pure: no file system, no worker. The studio resolves every input from the MO2 snapshot and getNpc.
//
// Status semantics (one rule for every code):
//   CONFIRMADO   the data inconsistency is proven by records/files (a mesh the record names is not in the
//                profile, a FaceGen that cannot be parsed, a FaceGen shape list that lacks the record's hair).
//   SOSPECHOSO   inferred from attribution (which mod set what) or from a partial check; it may be deliberate.
// The visual consequence in game is a separate axis (`verification`): almost every finding needs an in-game
// look to be called a visible defect, and nothing here ever claims VALIDADO EN SKYRIM.
namespace sns::appearance {

// Exact state labels shown by the panel and written to NPC_APPEARANCE_FINDINGS.json.
inline constexpr const char* kStatusConfirmed = "CONFIRMADO";
inline constexpr const char* kStatusSuspected = "SOSPECHOSO";
inline constexpr const char* kStatusClean = "SIN_ERROR_DETECTADO";
inline constexpr const char* kStatusRepairedInFiles = "REPARADO_EN_ARCHIVOS";
inline constexpr const char* kStatusValidatedInSkyrim = "VALIDADO_EN_SKYRIM";

inline constexpr const char* kVerifyOffline = "COMPROBADO_FUERA_DEL_JUEGO";
inline constexpr const char* kVerifyInGame = "REQUIERE_VALIDACION_EN_SKYRIM";

// One head part of the NPC's record (getNpc headParts), with its files resolved in the MO2 VFS.
struct AuditHeadPart {
  std::string form_key;
  std::string editor_id;
  std::string type;              // HDPT type: "Hair", "Face", "Eyes", "Eyebrows", "FacialHair", "Scar", "Misc"
  bool record_resolved = true;   // false: the HDPT FormKey is not in the active load order
  std::string plugin;            // plugin that defines the winning HDPT
  std::string record_mod;        // MO2 mod that provides that plugin
  std::string model_path;        // Data-relative mesh; empty for mesh-less placeholders ("...NoGash")
  AssetSource mesh;              // resolution of model_path (unset when model_path is empty)
  std::vector<AssetSource> textures;  // HDPT texture set / diffuse+normal the mesh uses (not found = missing)
  std::optional<bool> valid_for_race; // false: the NPC's race is not in the part's valid-races list
  std::optional<bool> in_facegen;     // whether the effective FaceGen has a shape named after editor_id
  std::optional<bool> visible_in_viewer;  // the studio's viewer drew the part with geometry (HAIR_003)
};

// An armor of the NPC's default outfit in the Hair slot (31): a wig or a hood. While it is worn the game hides the
// NPC's hair head parts, so a wig with nothing to draw for the NPC's race leaves the head bald even though the
// record and the FaceGen are fine (HAIR_SMP_007 / HAIR_SMP_001).
struct AuditHairArmor {
  std::string form_key;
  std::string editor_id;
  std::string plugin;      // winning plugin of the ARMO
  std::string record_mod;  // MO2 mod that provides it
  bool smp = false;        // worker HasSmp: an addon mesh declares HDT-SMP physics
  // Addons with a model for the NPC's race and sex (worker getAppearance); unset when they could not be resolved.
  std::optional<int> addons_for_race;
  std::vector<AssetSource> meshes;   // those addons' meshes
  std::vector<AssetSource> physics;  // physics XMLs the meshes declare ("HDT Skinned Mesh Physics Object")
};

// A hair shape of the effective FaceGen skinned to bones outside the race skeleton: SMP hair baked by the Creation
// Kit. The CK drops the physics XML path and may rewrite those bones' hierarchy and rest pose; comparing with the
// head part's own NIF tells which (independent check of what the smp-facegen tool repairs).
struct AuditSmpFaceGenHair {
  std::string shape;                       // FaceGen shape (head part EditorID)
  std::string source_mesh;                 // the head part's own NIF ("" when the shape is not a record part)
  std::vector<std::string> physics_bones;  // non-skeleton bones the FaceGen shape is skinned to
  std::string source_physics_xml;          // XML path the head part's NIF declares ("" = no SMP physics)
  bool facegen_has_physics_xml = false;    // the FaceGen kept an "HDT Skinned Mesh Physics Object" path
  std::optional<AssetSource> physics_xml;  // resolution of the XML path in the profile
  std::optional<float> max_bone_offset;    // largest rest-pose distance FaceGen vs source, game units
  std::vector<std::string> bones_missing_in_source;  // FaceGen bones the head part's NIF does not have
};

struct AuditSubject {
  std::string form_key;
  std::string editor_id;
  std::string name;
  std::string defining_plugin;   // origin plugin (FaceGen folder)
  std::string winning_plugin;
  bool uses_traits_template = false;  // traits come from a template: no own head, hair or FaceGen
  bool race_uses_facegen = true;      // false: creature / non-humanoid race
  bool chargen_preset = false;        // "Is CharGen Face Preset": only used by the RaceMenu presets
  bool is_player = false;             // 000007:Skyrim.esm: its head comes from the save, never from FaceGen
  // Placed references (ACHR) of the NPC in the load order; 0 = only spawned by scripts/leveled lists or unused,
  // unset = unknown. Lowers the priority of visual defects, never hides them.
  std::optional<int> placed_references;
  std::vector<AuditHeadPart> head_parts;
  // Hair shapes the effective FaceGen carries that are not head parts of the record (the FaceGen was made
  // for another hair, or for a record that had hair).
  std::vector<std::string> facegen_foreign_hair_shapes;
  // Whether the effective FaceGen draws any hair at all (a HairTint shader shape, or a shape named "*hair*");
  // unset when it was not parsed. Only "false" proves a bald head: a FaceGen with another hair is not bald.
  std::optional<bool> facegen_has_hair;
  std::vector<std::string> missing_masters;   // masters of the winning plugin absent from the load order
  // FaceGen files for this NPC found under another plugin folder or FormID (an ESL-ified or renumbered
  // plugin left the old files behind).
  std::vector<std::string> stale_facegen_paths;
  std::vector<AuditHairArmor> hair_armors;       // default-outfit armors in the Hair slot
  std::vector<AuditSmpFaceGenHair> smp_facegen;  // SMP hair shapes of the effective FaceGen
  std::optional<bool> fsmp_installed;            // SKSE\Plugins\hdtSMP64.dll present in the profile
  // The owner saw this NPC bald in Skyrim (reported, not inferred). Without a confirmed hair cause it stays an
  // open case (HAIR_SMP_010), never "no error detected".
  bool observed_bald_in_game = false;
};

// Largest rest-pose offset (game units) between a FaceGen SMP bone and the same bone of the hair's own NIF that is
// still the same pose; beyond it the CK rewrote the bone and the hair is drawn elsewhere.
inline constexpr float kSmpBoneTolerance = 0.5f;

struct AuditFinding {
  std::string code;          // "HAIR_001" ...
  std::string title;         // short Spanish label of the code
  std::string status;        // kStatusConfirmed / kStatusSuspected
  std::string severity;      // "alta", "media", "baja"
  double confidence = 0.0;   // 0..1, how sure the classification is (not how bad it looks)
  std::string evidence;      // concrete facts: paths, mods, plugins, measured values
  std::string cause;         // probable cause
  std::string action;        // suggested action (never applied automatically)
  std::string verification;  // kVerifyOffline / kVerifyInGame
  std::string resource;      // file or record the finding is about
  std::string responsible;   // mod or plugin most likely responsible
  std::string control_code;  // control-map finding it derives from, if any
};

struct NpcAudit {
  std::string form_key;
  std::string status;        // worst finding status, or kStatusClean
  // Why some checks do not apply: "plantilla-traits", "raza-sin-facegen", "preset-chargen",
  // "calvo-intencional"; empty when none. Exempt NPCs are never counted as defects.
  std::vector<std::string> exemptions;
  // Whether regenerating the FaceGen from the winning record loses nothing: true only when there is no
  // FaceGen or it is the game's own (BSA of <Game Data>). Any mod's FaceGen may be a sculpted head or the
  // face a revert hides (then the fix is a resolution patch). The repair planner must not regenerate
  // automatically when this is false.
  bool facegen_regeneration_safe = true;
  std::vector<AuditFinding> findings;
};

NpcAudit audit_npc_appearance(const AuditSubject& subject, const ControlInputs& inputs, const ControlReport& control);

// ---- Reversible hair repair: take a broken wig out of the NPC's default outfit so the FaceGen hair shows again.
// The proper fix is installing the wig's missing dependency (it brings the physics back); this one needs no download
// and is undone by disabling the repair mod. Never applied to an NPC whose FaceGen has no full hair of its own.

// Vertices a FaceGen hair shape needs to count as a full hairstyle (a hairline or scalp has a few hundred).
inline constexpr int kFullHairVertices = 1000;

struct WigRepairCandidate {
  std::string form_key;
  std::vector<std::string> outfit_armors;  // ARMO FormKeys of the default outfit, in order
  int outfit_other_entries = 0;            // non-ARMO entries (leveled lists): the outfit export cannot carry them
  std::vector<std::string> broken_wigs;    // hair-slot armors with a CONFIRMED HAIR_SMP_001 / HAIR_SMP_007
  int facegen_hair_vertices = 0;           // largest hair shape of the effective FaceGen (0 = none / not read)
};

struct WigRepairDecision {
  std::string form_key;
  bool repair = false;
  std::vector<std::string> keep_armors;  // the new outfit (the original minus the broken wigs)
  std::vector<std::string> removed;      // the broken wigs taken out
  std::string reason;                    // Spanish: why it is repaired, or why not
};

WigRepairDecision plan_wig_repair(const WigRepairCandidate& candidate);

// Spanish title of a code ("" when unknown).
std::string audit_code_title(const std::string& code);

}  // namespace sns::appearance
