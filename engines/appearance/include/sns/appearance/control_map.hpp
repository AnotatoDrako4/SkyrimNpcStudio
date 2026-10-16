#pragma once

#include <optional>
#include <string>
#include <vector>

// Appearance control map of one NPC: which plugin (and MO2 mod) controls every part of its effective
// appearance, what each one overrides, and whether the record and the face files fit together.
// Pure: every input is gathered by the studio (worker override chain, MO2 VFS, facial analysis).
namespace sns::appearance {

// One record version of the NPC, in load order (origin first, winner last).
struct OverrideLink {
  std::string plugin;
  std::string mod;  // MO2 mod that provides the plugin ("<Game Data>" for the game's own)
  bool is_origin = false;
  bool is_winner = false;
  std::vector<std::string> changed_fields;  // tracked fields that differ from the previous version
  // Every MO2 mod with a copy of the plugin file, ascending priority (`mod` is the winner). A losing copy
  // means the plugin was replaced file-for-file (translation, cleaned or patched copy).
  std::vector<std::string> plugin_mods;
};

// Where a Data-relative file of the face (FaceGen geometry or tint) comes from.
struct AssetSource {
  std::string path;
  bool found = false;
  std::string winner_mod;               // mod whose copy the game loads (for a BSA, the mod that provides it)
  std::string kind;                     // "loose", "bsa" or "" when missing
  std::vector<std::string> loose_mods;  // every MO2 mod with a loose copy, ascending priority (winner last)
};

struct ControlInputs {
  std::vector<OverrideLink> chain;
  AssetSource facegen;
  AssetSource facetint;
  // Facial analysis: max distance between the FaceGen the record produces and the profile's FaceGen,
  // when both could be compared (same topology). Small = the FaceGen was made from this record.
  std::optional<float> regeneration_deviation;
  // Tracked fields a non-winning version changed whose value did not survive (worker "lost edits").
  std::vector<std::string> lost_fields;
  // False when the NPC is not expected to have a FaceGen of its own: it takes its traits from a template,
  // or its race never uses FaceGen (creatures). A missing FaceGen is then normal, not a warning.
  bool facegen_expected = true;
  // Whether every head part of the record has a shape of the same name in the effective FaceGen (shapes
  // are named after the head part EditorIDs); unset when not checked.
  std::optional<bool> facegen_has_record_head_parts;
};

struct ComponentControl {
  std::string key;     // "face-shape", "head-parts", "face-texture", "skin-tint", "hair-color", "race", "outfit", "body", "facegen", "facetint"
  std::string label;   // Spanish, for the panel
  std::string plugin;  // controlling plugin (record parts) or empty (files)
  std::string mod;     // controlling MO2 mod
  std::string kind;    // "record", "loose", "bsa" or "missing"
  std::vector<std::string> overridden;  // "plugin (mod)" for record parts, "mod" for files: what lost
};

struct ControlFinding {
  std::string severity;     // "ok", "info", "warning"
  std::string code;
  std::string message;      // what was found
  std::string consequence;  // what it means in game
  std::string suggestion;   // what to do (never applied automatically)
};

struct ControlReport {
  std::vector<ComponentControl> components;
  std::vector<ControlFinding> findings;
  // The version that set the fields the FaceGen geometry is built from (head parts, morphs, race, weight).
  std::string face_plugin;
  std::string face_mod;
};

ControlReport analyze_appearance_control(const ControlInputs& in);

// A deviation below this (game units) means the profile's FaceGen is what the record produces.
inline constexpr float kFaceGenMatchesRecord = 0.05f;

}  // namespace sns::appearance
