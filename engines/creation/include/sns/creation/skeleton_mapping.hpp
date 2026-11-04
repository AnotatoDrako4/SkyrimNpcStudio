#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// Skeleton mapping for the SkinnedMesh profile (Character Forge 3.0). It compares the bones of a Blender armature
// (forge_skyrim.py armatures[].bonesDetail: bind transforms in armature space, as PyNifly writes them for SKYRIMSE)
// with an authorized reference skeleton read by sns::nif::load_skeleton (the game's or PyNifly's skeleton.nif, read
// only). Nothing is guessed: a bone is "verified" only with the same name, a consistent parent and the same bind
// transform; "suggested" is a purely textual match (case, separators, bracket tags) that needs a human decision;
// anatomy is never inferred.
namespace sns::creation::skyrim {

// Bind transform tolerances. PyNifly stores float32 and the report rounds translations to 1e-5 and rotations to 1e-6.
inline constexpr double kBoneTranslationTolerance = 0.01;  // Skyrim units (0.14 mm)
inline constexpr double kBoneRotationToleranceDeg = 0.1;

struct ReferenceBone {
  std::string name;
  std::string parent;  // "" for a root
  std::array<double, 3> translation{};
  std::array<std::array<double, 3>, 3> rotation{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};  // row-major
  double scale = 1.0;
};

struct ReferenceSkeleton {
  std::string label;   // e.g. "PyNifly skeletons/Skyrim/skeleton.nif"
  std::string sha256;  // of the file that was read
  std::vector<ReferenceBone> bones;
};

// Reads a skeleton NIF (sns::nif::load_skeleton) into a reference; bones in NIF root space. Throws sns::Error(Parse).
ReferenceSkeleton load_reference_skeleton(std::string_view nif_bytes, std::string label, std::string sha256);

// Lower case, "[...]" tags removed, '_' '.' '-' as spaces, spaces collapsed: "NPC Spine [Spn0]" -> "npc spine".
std::string normalized_bone_name(std::string_view name);

// Angle in degrees between two rotations (row-major 3x3).
double rotation_error_deg(const std::array<std::array<double, 3>, 3>& a, const std::array<std::array<double, 3>, 3>& b);

// map_skeleton(armature, reference, weighted_bones) -> {
//   reference: {label, sha256, bones}, armature,
//   bones: [{name, status: verified|suggested|unrecognized|incompatible, reference, weighted, parent, referenceParent,
//            translationError, rotationErrorDeg, detail}],
//   counts: {verified, suggested, unrecognized, incompatible}, weighted: {same keys, weighted bones only},
//   allWeightedVerified }
// `weighted_bones` are the bones that carry weights in the exported meshes (only those reach the NIF skin).
nlohmann::json map_skeleton(const nlohmann::json& armature, const ReferenceSkeleton& reference,
                            const std::vector<std::string>& weighted_bones);

}  // namespace sns::creation::skyrim
