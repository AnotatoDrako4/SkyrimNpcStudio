#pragma once

#include <cstddef>
#include <map>
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
  // Changed fields whose new value is a copy of an earlier version's (a patch that forwards another
  // mod's face): field -> plugin that introduced that value. The value is attributed to that plugin.
  std::map<std::string, std::string> copied_from;
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
  std::string via;  // record parts: the later plugin that carries `plugin`'s value as a copy (a patch), or empty
};

struct ControlFinding {
  std::string severity;     // "ok", "info", "warning"
  std::string code;
  std::string message;      // what was found
  std::string consequence;  // what it means in game
  std::string suggestion;   // what to do (never applied automatically)
};

// Assisted resolution of FaceGenRecordMismatch when the FaceGen's mod also has a version of the record
// in the chain: «Usar el rostro de X». A patch that copies the face fields of X's version onto the
// winning record (everything else stays the winner's) and loads after both, so the record says what
// X's FaceGen, which already wins in MO2, was built from. No FaceGen is copied or regenerated.
struct FaceResolution {
  std::string source_plugin;  // X: the version whose face fields are copied
  std::string source_mod;     // the MO2 mod that provides X and the effective FaceGen
  std::string winner_plugin;  // the record the patch overrides (every other field is kept)
  std::string winner_mod;
  // NPC_ properties to copy, only those whose value differs between X's version and the winner
  // (subset of kResolutionFields, in that order).
  std::vector<std::string> fields;
  // Plugins the patch must load after (declared as its masters): X and the winner, in chain order.
  std::vector<std::string> load_after;
  std::string label;   // "Usar el rostro de «X»"
  std::string detail;  // what the patch does, in Spanish
};

struct ControlReport {
  std::vector<ComponentControl> components;
  std::vector<ControlFinding> findings;
  // The version that set the fields the FaceGen geometry is built from (head parts, morphs, race, weight).
  std::string face_plugin;
  std::string face_mod;
  std::string face_via;  // the later plugin that carries those values as a copy (e.g. a patch), or empty
  // Offered only with FaceGenRecordMismatch; never applied automatically.
  std::optional<FaceResolution> resolution;
};

ControlReport analyze_appearance_control(const ControlInputs& in);

// Index of the version whose value of `field` the winner has: the last one that changed it (the origin
// when none did), or, when that change is a copy (`copied_from`), the version that introduced the value
// -- unless the version that wrote it belongs to `facegen_mod` (the effective FaceGen's mod): its own.
std::size_t field_controller(const std::vector<OverrideLink>& chain, const std::string& field, const std::string& facegen_mod = {});

// Face fields a resolution may copy (the face the FaceGen is built from, plus the skin and hair colors
// baked into it). Height, race and sex are never copied.
inline const std::vector<std::string> kResolutionFields = {"HeadParts", "FaceMorph", "FaceParts", "TintLayers",
                                                           "HairColor", "HeadTexture", "TextureLighting", "Weight"};

// A deviation below this (game units) means the profile's FaceGen is what the record produces.
inline constexpr float kFaceGenMatchesRecord = 0.05f;

}  // namespace sns::appearance
