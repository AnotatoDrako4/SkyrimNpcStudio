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

// Hair color only: the same FaceGen with the HairTint shader color of every HairTint shape set as
// build_facegen sets it (twice the CLFM color; black without one). Geometry, textures and every
// other block are kept, so a color change needs no regeneration. Hair, eyebrow and beard parts
// whose shader is not HairTint are reported (HairTintUnsupported): the color does not reach them.
struct HairTintPatch {
  std::string nif;                   // "" when the input could not be read or written
  std::vector<std::string> patched;  // shapes whose tint was set
  std::vector<FaceIssue> issues;
};
HairTintPatch patch_hair_tint(std::string_view facegen_nif, const FaceRecipe& recipe);

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

// RaceMenu "facegenmorphs": meshes\actors\character\facegenmorphs\<plugin>\morphs.ini lines
//   extension = <host chargen TRI>, <extension TRI relative to ...\facegenmorphs\morphs\>
// RaceMenu applies the extension TRI's morphs (custom sliders such as "CME_..." or "EFM_...") to
// head parts whose chargen TRI is the host. Returns Data-relative TRI paths.
std::vector<MorphExtension> parse_morphs_ini(std::string_view ini_text);
inline constexpr const char* kFaceGenMorphsRoot = "meshes\\actors\\character\\facegenmorphs\\";

}  // namespace sns::face
