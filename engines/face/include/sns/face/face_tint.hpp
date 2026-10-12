#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sns/face/facegen.hpp"
#include "sns/textures/dds.hpp"

namespace sns::face {

inline constexpr std::uint32_t kFaceTintSize = 512;  // vanilla FaceTint textures are 512 x 512

struct FaceTintBuild {
  textures::DdsImage rgba;  // RGBA8, size x size, alpha 255
  std::vector<std::string> layers;  // "index:mask (value)" of every layer that was drawn
  std::vector<FaceIssue> issues;
};

// Creation Kit FaceTint composition (validated on vanilla FaceTint DDS, docs/FACEGEN.md):
// start from the skin tone (QNAM), then for each NPC tint layer in record order blend its color
// over the result with weight mask.red * value; the skin tone mask (type 6) blends the skin tone.
FaceTintBuild compose_face_tint(const FaceRecipe& recipe, FaceAssetCache& assets, std::uint32_t size = kFaceTintSize);

// BC3 + full mip chain, like vanilla FaceTint files.
std::string encode_face_tint(const textures::DdsImage& rgba);

}  // namespace sns::face
