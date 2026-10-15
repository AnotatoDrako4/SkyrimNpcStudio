#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "sns/appearance/appearance_draft.hpp"

namespace sns::appearance {

// Easy Mod Creator: what the guided export would write for the NPC being edited, which export path
// that is, and whether every check passes. It never adds an operation of its own: it reads the state
// the editors already have (face build kind, FaceGen report, outfit validation, compatibility) and
// only offers what those backends can export.

struct EasyModInputs {
  bool npc_loaded = false;
  std::string npc_label;
  FaceBuildKind face_kind = FaceBuildKind::Unchanged;
  bool face_exportable = false;          // AppearanceEditor::exportable()
  bool face_building = false;
  std::string face_integrity;            // FaceGen report "integrity" ("hair-tint-only" after Hair Color Safe Mode)
  bool height_changed = false;
  bool outfit_changed = false;           // draft pieces differ from the NPC's current outfit
  std::size_t outfit_pieces = 0;
  bool outfit_validating = false;
  bool outfit_exportable = false;        // OutfitEditor::exportable()
  bool outfit_preview_material = false;  // a material variant that is preview-only
  std::vector<std::string> blocked_operations;  // face compatibility: "hair-color", "face-morphs", ...
  std::string provenance_conflict;       // winning ESP and FaceGen from different mods
};

enum class EasyModState { Ok, Warning, Waiting, Blocked, Info };

// "outfit-patch" (Mode A), "face-overhaul" (Mode B), "complete-overhaul" (Mode C).
enum class EasyModMode { None, OutfitPatch, FaceOverhaul, CompleteOverhaul };

struct EasyModItem {
  std::string key;
  EasyModState state = EasyModState::Info;
  std::string label;
  std::string detail;
};

struct EasyModPlan {
  EasyModMode mode = EasyModMode::None;
  std::vector<EasyModItem> changes;  // what the mod will contain
  std::vector<EasyModItem> checks;   // validation shown before creating the mod
  bool can_export = false;           // something to export and no Blocked/Waiting check
  std::string summary;
};

[[nodiscard]] EasyModPlan plan_easy_mod(const EasyModInputs& in);
[[nodiscard]] std::string_view to_string(EasyModMode mode);
[[nodiscard]] std::string_view to_string(EasyModState state);

}  // namespace sns::appearance
