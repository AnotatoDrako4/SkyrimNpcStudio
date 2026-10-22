#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Preset <-> Export Head readiness: which RaceMenu "Export Head" NIF belongs to a .jslot preset, and whether that head can be
// used as a face source. A preset's head parts are known by FormKey; an Export Head names its head parts as NIF shapes (the
// shape is the head part's EditorID), so the pairing is made on content (shape overlap) with the file name only as a hint.
// Pure: the studio supplies the facts (EditorIDs resolved by the worker, shapes read by the safe NIF loader, vertex counts).
// Verdicts never say «safe»: a present NIF is not a safe FaceGen, and nothing here was seen inside Skyrim
// (UNVERIFIED_IN_SKYRIM is always listed as a caveat). The overlap-plus-name idea is the one of GenerateFollower's
// HeadImports (GPL-3); no code of it was used.
namespace sns::presets {

enum class HeadVerdict {
  ReadyForReview,    // one readable head holds the preset's face part (and its topology fits when it could be checked)
  NeedsAsset,        // no Export Head for this preset, or a file it needs (a texture) is missing
  NeedsChoice,       // two heads fit equally well: a person must pick (manual choice)
  FaceGenUnreadable, // the head that belongs to the preset could not be opened (damaged or not a NIF)
  HeadPartMismatch,  // the head lacks the preset's face part, the preset names none, or the race does not fit
  UnsupportedTopology,  // the head's face mesh has another vertex count than the face part it should replace
};
[[nodiscard]] std::string_view to_string(HeadVerdict verdict);

struct HeadShape {
  std::string name;
  std::uint32_t vertices = 0;
};

struct HeadCandidate {
  std::string path;       // as shown (Data-relative or absolute)
  std::string stem;       // file name without folders and without any number of trailing ".nif"
  bool readable = false;  // opened by the safe NIF loader
  std::vector<HeadShape> shapes;
  std::int64_t modified = 0;  // seconds since the epoch: a newer export of the same name wins a tie
  std::vector<std::string> missing_textures;  // textures the face shape names that the profile does not have
};

struct PresetHeadFacts {
  std::string preset_name;                 // file stem of the .jslot
  std::vector<std::string> shape_names;    // EditorIDs of the preset's head parts that have a model (resolved)
  std::string face_part;                   // EditorID of the head part of type Face ("" when unknown)
  std::optional<std::uint32_t> face_vertices;  // vertex count of that part's base mesh (nullopt: not checked)
  std::optional<bool> race_ok;             // false: a head part of the preset is not valid for the target race
  std::vector<std::string> unresolved;     // FormKeys whose EditorID could not be resolved (they are ignored)
};

struct RankedHead {
  std::size_t index = 0;  // into the candidate list
  double score = 0.0;
  double overlap = 0.0;   // share of the preset's resolved parts found as shapes of the head (extra shapes, e.g. hair, are not held against it)
  int common = 0;         // shapes in common
  bool exact_name = false;
  bool near_name = false;
  std::string reason;     // Spanish, for the card
};

struct HeadMatch {
  HeadVerdict verdict = HeadVerdict::NeedsAsset;
  std::vector<std::string> codes;     // NoExportHead, TexturesMissing, FaceShapeMissing, RaceIncompatible, TopologyDiffers, ...
  std::string message;                // Spanish, one sentence
  std::vector<RankedHead> ranked;     // viable candidates, best first
  std::optional<std::size_t> chosen;  // index into the candidate list (the best, or the manual choice)
  bool needs_manual_choice = false;
  std::vector<std::string> caveats;   // always contains "UNVERIFIED_IN_SKYRIM"
};

// `manual_choice` (an index into `candidates`) overrides the ranking but is checked like any other head.
[[nodiscard]] HeadMatch match_export_head(const PresetHeadFacts& facts, const std::vector<HeadCandidate>& candidates,
                                          std::optional<std::size_t> manual_choice = std::nullopt);

// "1 Okiri.nif.nif.nif" -> "1 Okiri" (RaceMenu names can pile up extensions).
[[nodiscard]] std::string normalize_head_stem(std::string_view file_name);

}  // namespace sns::presets
