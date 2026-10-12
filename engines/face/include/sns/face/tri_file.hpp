#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sns::face {

// One morph of a FaceGen TRI file: a per-vertex offset in the head part mesh's vertex space.
struct TriMorph {
  std::string name;
  std::vector<std::array<float, 3>> deltas;  // vertex_count entries (zero for untouched vertices)
};

// FaceGen "FRTRI003" morph file as used by Skyrim head parts (race, expression and chargen TRI).
// Difference morphs (int16 deltas * scale) and statistical morphs (absolute targets for a vertex
// subset) are both converted to per-vertex deltas.
struct TriFile {
  std::uint32_t vertex_count = 0;
  std::vector<std::array<float, 3>> base;  // the TRI's own base positions (informative)
  std::vector<TriMorph> morphs;

  // First morph with this name, ignoring ASCII case: the Creation Kit uses the first one when a
  // file repeats a name (vanilla FemaleHeadCharGen.tri has two "NoseType9"). nullptr if absent.
  [[nodiscard]] const TriMorph* find(std::string_view name) const;
};

// Throws sns::Error(Parse) for anything that is not a well-formed FRTRI003 file.
TriFile parse_tri(std::string_view bytes, std::string_view label);

}  // namespace sns::face
