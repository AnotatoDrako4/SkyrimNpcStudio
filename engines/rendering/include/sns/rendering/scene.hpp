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

// Interleaved vertex: position(3) normal(3) uv(2) tangent(3) binormal(3) color(4), Y-up view space.
inline constexpr std::size_t kFloatsPerVertex = 18;
inline constexpr std::size_t kColorOffset = 14;

// How a Skyrim NiAlphaProperty is reproduced with Qt Quick 3D's PrincipledMaterial.
enum class AlphaMode {
  Opaque,                // no alpha reaches the material
  Mask,                  // alpha test only: discard below the NIF threshold
  Blend,                 // alpha blend only (eyes, brows, glass): sorted, no depth write
  BlendWithDepthPrepass  // blend + test (hair, scalp): depth written for opaque texels, edges blended
};
std::string_view to_string(AlphaMode mode);

struct AlphaTranslation {
  AlphaMode mode = AlphaMode::Opaque;
  float cutoff = 0.5f;  // used by Mask; informative otherwise
  std::string reason;
};

// blend/test/threshold come from the NiAlphaProperty. texture_alpha / vertex_alpha say whether any
// alpha actually reaches the material, measured on the decoded texture / the vertex colors.
AlphaTranslation translate_alpha(bool blend, bool test, std::uint8_t threshold, bool texture_alpha, bool vertex_alpha);

// What the viewer did with one texture, for the inspector and the diagnostics.
struct TextureInfo {
  std::string path;
  std::string location;  // "loose:<mod>", "bsa:<archive> (<mod>)", "composite" or "missing"
  std::uint32_t width = 0, height = 0;               // uploaded size
  std::uint32_t source_width = 0, source_height = 0; // size of mip 0 in the file
  std::uint32_t mip_level = 0;
  std::string format;
  bool normal_map = false;
  textures::AlphaStats alpha;
  std::string derived_from;  // composite textures: the inputs
  std::string error;
};

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
  // Raw NiAlphaProperty data and its translation (see translate_alpha).
  bool alpha_blend = false;
  bool alpha_test = false;
  float alpha_cutoff = 0.5f;
  std::uint16_t alpha_flags = 0;
  AlphaMode alpha_mode = AlphaMode::Opaque;
  std::string alpha_reason;
  bool double_sided = false;
  bool vertex_colors = false;  // shader asks for vertex colors and the mesh has them
  bool skinned = false;
  bool skinning_applied = false;
  bool weight_blended = false;

  // Inspector data: where everything came from and what was done to it.
  std::string original_diffuse;  // slot 0 as stored in the NIF
  std::string detail;            // FaceTint detail map (slot 3)
  std::string face_tint;         // FaceGen tint composited into the diffuse
  std::string mesh_location;     // "loose:<mod>" / "bsa:<archive> (<mod>)"
  std::string parent_node;
  nif::Transform to_root;
  bool model_space_normals = false;
  bool tangents_swapped = false;
  std::size_t vertex_count = 0;
  std::size_t bone_count = 0;
  std::vector<std::string> bones_from_mesh;  // resolved from the NIF's own nodes (SMP/extra bones)
  std::vector<std::string> bones_missing;    // neither in the skeleton nor in the NIF: bind pose kept
  std::string shader_block;                  // "" when the shape has no shader property
  std::uint32_t shader_flags1 = 0, shader_flags2 = 0;
  float material_alpha = 1.0f;
  nif::Vec2 uv_offset{0, 0}, uv_scale{1, 1};  // already applied to the vertex UVs
  std::string physics_xml;                   // SMP definition of the mesh, if any

  // Shapes the game does not draw are still listed (for the inspector) but start hidden and are
  // left out of the bounds: no shader property, the NIF Hidden flag, an untextured SMP collision
  // proxy, or hair under a headgear slot. `hidden_reason` says which.
  bool shown_by_default = true;
  std::string hidden_reason;

  std::string addon_form_key;                // ARMA the mesh comes from ("" for FaceGen)
  std::array<std::string, 9> texture_slots;  // BSShaderTextureSet as stored in the NIF
  bool degenerate_uvs = false;               // textured, but every UV triangle has zero area
  std::vector<std::string> warnings;         // per-part problems, in plain language
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
  int textures_reduced = 0;    // a smaller stored mip (or a box-filtered copy) was used
  int hidden_shapes = 0;       // parts listed but not drawn by default (see RenderPart::hidden_reason)
  int pieces_unequipped = 0;   // outfit pieces replaced by a later piece in the same slot
};

struct SceneOptions {
  bool apply_skinning = false;  // CPU-skin skinned shapes with the race skeleton (reference pose)
  bool include_skeleton = true;
  std::function<bool()> cancelled;
  // Preview texture policy: the DDS mip whose larger side fits is used (real file data, no
  // resampling); files without mips are box-filtered. Head textures are loaded first.
  std::uint32_t diffuse_max_dimension = 2048;
  std::uint32_t normal_max_dimension = 1024;
  std::uint64_t texture_budget_bytes = 512ull * 1024 * 1024;  // decoded RGBA8 for the whole scene
};

struct CharacterScene {
  std::vector<RenderPart> parts;
  std::vector<float> skeleton_lines;  // pairs of view-space points
  std::size_t skeleton_bones = 0;
  std::unordered_map<std::string, std::shared_ptr<const textures::DdsImage>> textures;
  std::unordered_map<std::string, TextureInfo> texture_info;  // every texture requested, loaded or not
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
// Bones the race skeleton lacks but the NIF defines itself (SMP/physics "A-S-..." bones) use the
// NIF's own reference transform. Bones found nowhere keep the bind-pose position for their weight,
// so vertices are never pulled towards the origin.
struct SkinningReport {
  std::vector<std::string> from_mesh;
  std::vector<std::string> missing;
};
SkinningReport apply_skinning(nif::Shape& shape, const nif::Skeleton& skeleton);

// FaceGen composition of BSLightingShader's FaceTint path (as reimplemented by Community Shaders):
// result = (base^2 + 2*tint*base*(1-base)) * detail * 3.984375. Neutral tint is 0.5, neutral
// detail is ~0.25 (64/255). `detail` may be null. Output has the base's size.
textures::DdsImage apply_face_tint(const textures::DdsImage& base, const textures::DdsImage& tint,
                                   const textures::DdsImage* detail);

// Skyrim equips outfit items in order and an item unequips whatever shares its slots, so with
// overlapping pieces the LAST one stays. Returns, per piece, whether it remains equipped.
std::vector<bool> resolve_equipped(const std::vector<std::uint32_t>& slot_masks);

// Shape names an HDT-SMP XML uses as collision bodies (<per-vertex-shape name=".."> and
// <per-triangle-shape name="..">), in file order, without duplicates.
std::vector<std::string> smp_collision_shapes(std::string_view xml);

// Why the game would not draw `shape` ("" = it is drawn). `collision` lists the SMP collision
// shape names of its mesh: such a shape with no texture at all is a physics proxy.
std::string hidden_reason(const nif::Shape& shape, const std::vector<std::string>& collision);

// Slot rules: a skin addon is hidden by any outfit piece sharing a slot; hair hides under 31/41.
bool is_covered(std::uint32_t slots, std::uint32_t covered);
inline constexpr std::uint32_t kHairSlots = (1u << (31 - 30)) | (1u << (41 - 30));

}  // namespace sns::rendering
