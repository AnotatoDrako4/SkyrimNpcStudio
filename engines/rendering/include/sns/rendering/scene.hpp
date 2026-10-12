#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sns/nif/mesh.hpp"
#include "sns/textures/dds.hpp"

namespace sns::assets {
class AssetResolver;
}

namespace sns::rendering {

// ---------------------------------------------------------------- appearance (from the worker)

struct Rgb {
  float r = 1, g = 1, b = 1;
};

struct AddonModel {
  std::string form_key;
  std::string mesh;  // Data-relative "meshes\..."
  bool weight_slider = false;
  std::uint32_t slot_mask = 0;
};

struct AppearancePiece {
  std::string armor_form_key;
  std::string name;
  std::string source;  // "outfit" or "skin"
  std::uint32_t slot_mask = 0;
  std::vector<AddonModel> addons;
};

struct HeadPartInfo {
  std::string editor_id;
  std::string type;  // "Hair", "Eyes", "Face"...
};

struct Appearance {
  std::string npc_form_key;
  bool female = false;
  float weight = 50.0f;       // 0..100
  float height = 1.0f;        // NPC multiplier
  float race_height = 1.0f;
  std::string skeleton;       // "meshes\actors\character\character assets\skeleton_female.nif"
  std::string face_geometry;  // FaceGen NIF
  std::string face_tint;      // FaceGen tint DDS
  std::optional<Rgb> hair_color;
  std::optional<Rgb> skin_tone;
  std::vector<HeadPartInfo> head_parts;
  std::vector<AppearancePiece> pieces;
};

Appearance appearance_from_json(const nlohmann::json& json);

// ---------------------------------------------------------------- scene

// Interleaved vertex: position(3) normal(3) uv(2) tangent(3) binormal(3), Y-up view space.
inline constexpr std::size_t kFloatsPerVertex = 14;

struct RenderPart {
  std::string id;
  std::string label;
  std::string group;  // "outfit", "skin" or "head"
  std::string armor_form_key;
  std::uint32_t slot_mask = 0;
  std::string mesh_path;
  std::string shape_name;
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;
  std::array<float, 3> min{}, max{};
  std::string diffuse;  // texture keys into CharacterScene::textures (empty = none)
  std::string normal;
  std::optional<Rgb> tint;
  std::uint32_t shader_type = 0;
  bool alpha_blend = false;
  bool alpha_test = false;
  float alpha_cutoff = 0.5f;
  bool double_sided = false;
  bool skinned = false;
  bool skinning_applied = false;
  bool weight_blended = false;
};

struct SceneIssue {
  std::string severity;  // "error", "warning", "info"
  std::string code;
  std::string message;
  std::string subject;
};

struct SceneStats {
  int meshes_requested = 0;
  int meshes_loaded = 0;
  int shapes = 0;
  std::size_t triangles = 0;
  int textures_loaded = 0;
  int textures_missing = 0;
  int skinned_shapes = 0;
  int bones_missing = 0;
};

struct SceneOptions {
  bool apply_skinning = false;  // CPU-skin skinned shapes with the race skeleton (reference pose)
  bool include_skeleton = true;
  std::function<bool()> cancelled;
};

struct CharacterScene {
  std::vector<RenderPart> parts;
  std::vector<float> skeleton_lines;  // pairs of view-space points
  std::size_t skeleton_bones = 0;
  std::unordered_map<std::string, std::shared_ptr<const textures::DdsImage>> textures;
  std::vector<SceneIssue> issues;
  SceneStats stats;
  float scale = 1.0f;                // race height x NPC height (applied to the whole character)
  std::array<float, 3> min{}, max{};  // unscaled view-space bounds
};

CharacterScene build_character_scene(const Appearance& appearance, const assets::AssetResolver& resolver,
                                     const SceneOptions& options);

// ---------------------------------------------------------------- building blocks (tested directly)

// Skyrim is Z-up and actors face +Y; the viewer is Y-up and looks down -Z. This proper rotation
// keeps triangle winding and puts the character's face towards a camera on +Z.
nif::Vec3 to_view_space(const nif::Vec3& skyrim);

// "...\cuirass_1.nif" -> variant 0: "...\cuirass_0.nif". Returns empty if the path has no _0/_1 suffix.
std::string weight_variant(std::string_view mesh_path, int variant);

// The game blends the _0 (thin) and _1 (heavy) meshes by NPC weight 0..100.
// Returns false when the meshes do not match vertex for vertex (heavy is kept as is).
bool blend_by_weight(nif::Shape& heavy, const nif::Shape& thin, float weight);

// Moves skinned vertices to the skeleton's pose: v' = sum_i w_i * boneToGlobal_i(skinToBone_i(globalToSkin(v))).
// Returns the names of bones the skeleton does not have (those influences are dropped).
std::vector<std::string> apply_skinning(nif::Shape& shape, const nif::Skeleton& skeleton);

// Slot rules: a skin addon is hidden by any outfit piece sharing a slot; hair hides under 31/41.
bool is_covered(std::uint32_t slots, std::uint32_t covered);
inline constexpr std::uint32_t kHairSlots = (1u << (31 - 30)) | (1u << (41 - 30));

}  // namespace sns::rendering
