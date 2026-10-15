#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sns/face/face_recipe.hpp"
#include "sns/face/tri_file.hpp"

// FaceGen regeneration: builds the head NIF the game loads for an NPC from its head parts, their
// TRI morph files and the record's face values, following the Creation Kit rules measured on
// vanilla data (docs/FACEGEN.md). The same bytes feed the 3D preview and the exported mod.
namespace sns::face {

struct FaceIssue {
  std::string severity;  // "error", "warning", "info"
  std::string code;
  std::string message;
  std::string subject;
};

struct MorphWeight {
  std::string name;
  float weight = 0.0f;
};

// Which TRI morphs the Creation Kit adds, and how much, for a recipe.
struct MorphPlan {
  std::string race_morph;               // from race TRI files, weight 1
  std::vector<MorphWeight> chargen;     // sliders, presets (NoseType16...), VampireMorph
  std::vector<MorphWeight> expression;  // SkinnyMorph = 1 - weight / 100
  std::vector<MorphWeight> custom;      // RaceMenu extension morphs (searched in morphs.ini TRI files)
};
MorphPlan plan_morphs(const FaceRecipe& recipe);

// Reads Data-relative assets (loose winner or BSA) and caches raw bytes and parsed TRI files.
// Thread-safe; meant to live as long as the MO2 snapshot it reads from.
using AssetReader = std::function<std::optional<std::string>(std::string_view data_path)>;
class FaceAssetCache {
 public:
  explicit FaceAssetCache(AssetReader reader, std::size_t max_bytes = 256ull * 1024 * 1024);

  // nullptr when no loose file or archive provides the path.
  std::shared_ptr<const std::string> bytes(std::string_view data_path);
  // nullptr when missing; throws sns::Error(Parse) for a malformed TRI.
  std::shared_ptr<const TriFile> tri(std::string_view data_path);
  void clear();

 private:
  AssetReader reader_;
  std::size_t max_bytes_;
  std::size_t cached_bytes_ = 0;
  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<const std::string>> bytes_;
  std::unordered_map<std::string, std::shared_ptr<const TriFile>> tris_;
};

enum class GeometryMode {
  Regenerate,        // head part meshes + morphs: what the Creation Kit would write
  PreserveExisting,  // existing FaceGen + (morphs(values) - morphs(baseline)); keeps sculpting
};

struct FaceGenBuildOptions {
  GeometryMode mode = GeometryMode::Regenerate;
  // PreserveExisting: the FaceGen to start from and the values it corresponds to. Parts that the
  // existing file does not contain (e.g. a new hairstyle) are regenerated.
  std::shared_ptr<const std::string> existing_facegen;
  std::optional<FaceValues> baseline;
  // Checked between head parts; when it returns true the build stops with `cancelled` set and no NIF.
  std::function<bool()> cancelled;
};

struct PartBuild {
  std::string editor_id;
  std::string model;
  std::string status;  // "built", "no-model", "missing-mesh", "unreadable-mesh", "no-shape"
  std::string geometry_source;  // "regenerated" or "preserved"
  std::size_t vertex_count = 0;
  std::vector<std::string> applied;  // "race:NordRace", "chargen:NoseLong=0.300", "sculpt:141"...
  std::vector<std::string> missing;  // requested morphs the part's TRI files do not define
};

struct FaceGenBuild {
  std::string nif;  // serialized NIF ("" when nothing could be built)
  std::vector<PartBuild> parts;
  std::vector<FaceIssue> issues;
  bool cancelled = false;
  [[nodiscard]] bool exportable() const;  // a NIF was produced and no issue is an error
};

FaceGenBuild build_facegen(const FaceRecipe& recipe, FaceAssetCache& assets, const FaceGenBuildOptions& options = {});

// Hair Color Safe Mode: a byte copy of `facegen_nif` in which only the HairTint color of each
// HairTint shader is rewritten, as build_facegen sets it (twice the CLFM color; black without one).
// The file is not re-serialized: geometry, indices, UV, normals, skinning, partitions, textures,
// head parts and every other block keep their original bytes, which verify_hair_tint_integrity
// then confirms. Errors (nif empty): FaceGenUnreadable, HAIR_TINT_LAYOUT_UNVERIFIED (the color is
// not where the SSE layout puts it), HAIR_TINT_NOT_APPLIED (no HairTint shape: a CLFM color cannot
// change this FaceGen), FACEGEN_GEOMETRY_CHANGED. Hair, eyebrow and beard parts whose shader is
// not HairTint are reported (HairTintUnsupported warning): the color does not reach them.
struct HairTintPatch {
  std::string nif;                   // "" when nothing could be applied (see issues)
  std::vector<std::string> patched;  // shapes whose tint was set
  std::vector<FaceIssue> issues;
};
HairTintPatch patch_hair_tint(std::string_view facegen_nif, const FaceRecipe& recipe);

// FaceGen Geometry Integrity Guard: `candidate` may differ from `original` only in HairTint colors.
// Checked three ways: bytes (equal size, every differing byte inside an original HairTint color),
// semantics per shape matched by name (vertex and triangle counts, positions, indices, normals, UV,
// tangents, vertex colors, transforms, skin bones and weights, skin transforms, partitions, shader
// type, texture slots, alpha) and node transforms, and, when the bytes differ elsewhere without a
// listed difference, a canonical re-serialization of both files with the tints equalized.
struct FaceGenDifference {
  std::string shape;     // "" for the whole file
  std::string property;  // "positions", "indices", "uv", "skin bones", "texture slot 0"...
  std::string before;
  std::string after;
};
struct FaceGenIntegrity {
  bool readable = false;
  bool unchanged_except_hair_tint = false;
  bool byte_identical_outside_hair_tint = false;
  std::vector<FaceGenDifference> differences;
  [[nodiscard]] std::string summary() const;  // first difference, for messages
};
FaceGenIntegrity verify_hair_tint_integrity(std::string_view original, std::string_view candidate);

// Shape-by-shape comparison of two FaceGen NIFs (vertex positions, shape space).
struct ShapeDifference {
  std::string name;
  bool in_a = false, in_b = false;
  std::size_t vertices_a = 0, vertices_b = 0;
  float max_distance = 0.0f;   // only when both have the same vertex count
  float mean_distance = 0.0f;
};
struct FaceGenComparison {
  std::vector<ShapeDifference> shapes;
  bool same_shapes = false;    // same names in both files
  bool same_topology = false;  // and the same vertex count per shape
  float max_distance = 0.0f;   // over shapes present in both with equal vertex counts
};
FaceGenComparison compare_facegen(std::string_view a, std::string_view b);

// What a FaceGen NIF contains, as the game reads it.
struct FaceGenInfo {
  bool readable = false;
  std::string root_type, root_name;
  bool has_facegen_node = false;  // "BSFaceGenNiNodeSkinned"
  std::vector<std::pair<std::string, std::size_t>> shapes;  // name, vertex count
  std::string face_tint_slot;  // texture slot 6 of the FaceTint shader, if any
  std::vector<std::pair<std::string, std::array<float, 3>>> hair_tints;  // HairTint shapes: shader tint color
};
FaceGenInfo inspect_facegen(std::string_view nif_bytes);

// Dark-face and mismatch checks between a FaceGen file and the record it must match.
std::vector<FaceIssue> audit_facegen(const FaceGenInfo& facegen, const FaceRecipe& recipe);

// How far an NPC's face can be edited without damaging what the profile loads, decided from
// evidence only: the winning FaceGen, the head parts' meshes and TRI files (vertex counts) and a
// regeneration from the record. A custom or high-resolution head is never recognised by a name.
enum class FaceCompatibility {
  VanillaCompatible,         // the winning FaceGen is what the record produces
  CustomLimited,             // custom geometry or sculpt; its TRI files fit the meshes
  CustomIncompatibleMorphs,  // custom geometry whose TRI files do not fit its meshes
  FaceGenMissing,
  FaceGenInconsistent,       // unreadable, or not made from the record's head parts
  MissingDependencies,       // head part meshes or TRI files missing or unreadable
  Undetermined,
};
struct TriMismatch {
  std::string part;
  std::string tri;
  std::size_t tri_vertices = 0;
  std::size_t mesh_vertices = 0;
};
// Operations: "hair-color" (Hair Color Safe Mode), "outfit", "face-morphs" (sliders and presets),
// "head-parts", "regenerate" (rebuild the head from the record).
struct FaceCompatibilityReport {
  FaceCompatibility kind = FaceCompatibility::Undetermined;
  std::string code;                   // "vanilla-compatible", "custom-limited", ...
  std::vector<std::string> evidence;  // what was compared and found (technical, English)
  std::vector<TriMismatch> tri_mismatches;
  std::vector<std::string> missing;   // head part meshes / TRI files that could not be used
  std::vector<std::string> allowed;
  std::vector<std::pair<std::string, std::string>> blocked;  // operation, reason
  float regeneration_deviation = -1.0f;  // max distance regeneration vs winning FaceGen (-1: not compared)
  bool hair_tint = false;                // the winning FaceGen has HairTint shapes
};
FaceCompatibilityReport diagnose_face_compatibility(const FaceRecipe& recipe, FaceAssetCache& assets,
                                                    const std::string* winning_facegen, const FaceGenBuild& regenerated);

// RaceMenu "facegenmorphs": meshes\actors\character\facegenmorphs\<plugin>\morphs.ini lines
//   extension = <host chargen TRI>, <extension TRI relative to ...\facegenmorphs\morphs\>
// RaceMenu applies the extension TRI's morphs (custom sliders such as "CME_..." or "EFM_...") to
// head parts whose chargen TRI is the host. Returns Data-relative TRI paths.
std::vector<MorphExtension> parse_morphs_ini(std::string_view ini_text);
inline constexpr const char* kFaceGenMorphsRoot = "meshes\\actors\\character\\facegenmorphs\\";

}  // namespace sns::face
