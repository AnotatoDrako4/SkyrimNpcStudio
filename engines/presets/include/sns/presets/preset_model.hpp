#pragma once

#include <string>
#include <vector>

#include "sns/appearance/appearance_draft.hpp"
#include "sns/face/face_recipe.hpp"
#include "sns/presets/jslot_parser.hpp"

namespace sns::presets {

// What applying a RaceMenu preset to an NPC means for the record and the FaceGen:
// NAM9/NAMA/weight/hair color/head parts/face texture set from the preset; tints matched to the
// target race's masks by texture path (RaceMenu stores mask paths, not TINI indices); the skin
// tone layer also gives QNAM; custom morphs and sculpt are baked into FaceGen.
struct PresetMapping {
  appearance::FaceState state;
  std::vector<std::string> notes;            // everything that could not be carried over, in plain language
  std::vector<std::string> runtime_only;     // RaceMenu/NiOverride data the NPC record cannot hold
  std::size_t tints_matched = 0, tints_skipped = 0;
};

PresetMapping map_preset(const RaceMenuPreset& preset, const appearance::FaceState& baseline, const face::FaceRecipe& target);

}  // namespace sns::presets
