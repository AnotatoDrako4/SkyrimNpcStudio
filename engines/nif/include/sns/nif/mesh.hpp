#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Qt-free, nifly-free view of Skyrim SE NIF meshes. Coordinates stay in Skyrim space (Z up, units).
namespace sns::nif {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct Vec2 {
  float u = 0, v = 0;
};

// Similarity transform: p' = translation + rotation * (p * scale). rotation is row-major.
struct Transform {
  Vec3 translation;
  std::array<std::array<float, 3>, 3> rotation{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  float scale = 1.0f;

  [[nodiscard]] Vec3 apply(const Vec3& p) const;
  [[nodiscard]] Vec3 apply_dir(const Vec3& d) const;
  // (*this) after (other): first other, then this.
  [[nodiscard]] Transform compose(const Transform& other) const;
};

// Up to four bone influences per vertex (indices into Shape::bones).
struct Influence {
  std::array<std::uint16_t, 4> bone{};
  std::array<float, 4> weight{};
};

// BSTextureSet slots used by BSLightingShaderProperty.
enum TextureSlot : std::uint8_t { Diffuse = 0, Normal = 1, GlowOrSkin = 2, Parallax = 3, EnvMap = 4, EnvMask = 5, Tint = 6, Backlight = 7 };

// Shader types of BSLightingShaderProperty that need a color from the record.
inline constexpr std::uint32_t kShaderFaceTint = 4;
inline constexpr std::uint32_t kShaderSkinTint = 5;
inline constexpr std::uint32_t kShaderHairTint = 6;

struct Shape {
  std::string name;
  // Vertex data in the NIF root space (skinned shapes: skin space mapped back with global-to-skin^-1).
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec3> tangents;
  std::vector<Vec3> bitangents;
  std::vector<Vec2> uvs;
  std::vector<std::array<float, 4>> colors;
  std::vector<std::uint32_t> indices;

  std::array<std::string, 9> textures;  // Data-relative, normalized ("textures\...")
  std::uint32_t shader_type = 0;
  bool has_shader = false;         // the game draws no geometry without a shader property
  std::string shader_block;        // "BSLightingShaderProperty", "BSEffectShaderProperty", ...
  bool has_texture_set = false;    // BSShaderTextureSet attached (lighting shaders)
  std::uint32_t shader_flags1 = 0;
  std::uint32_t shader_flags2 = 0;
  float material_alpha = 1.0f;     // shader property "Alpha" multiplier
  Vec2 uv_offset{0, 0};
  Vec2 uv_scale{1, 1};
  bool double_sided = false;
  bool model_space_normals = false;
  bool vertex_colors = false;
  bool alpha_blend = false;
  bool alpha_test = false;
  std::uint8_t alpha_threshold = 128;
  std::uint16_t alpha_flags = 0;  // raw NiAlphaProperty flags (0 = no alpha property)

  // NiAVObject flag bit 0 ("Hidden") on the shape or any ancestor: the game never draws it.
  // SMP/physics helpers such as "VirtualGround" or collision proxies use it.
  bool hidden = false;
  std::string parent_node;  // parent NiNode name ("" when the parent is the unnamed root)
  Transform to_root;        // transform that was applied to the stored vertex data
  // Bethesda NIFs do not agree on which stored vector is dP/du; the pair is swapped when the UV
  // gradient says so, so that `tangents` always follows +U.
  bool tangents_swapped = false;

  bool skinned = false;
  Transform global_to_skin;               // identity for unskinned shapes
  std::vector<std::string> bones;
  std::vector<Transform> skin_to_bone;    // one per bone
  // Bone nodes present in THIS file (reference pose, NIF root space). Armors with physics or
  // extra bones (e.g. SMP "A-S-000001") carry them; the race skeleton does not.
  std::vector<std::optional<Transform>> mesh_bone_to_global;
  std::vector<Influence> influences;      // one per vertex when skinned
  std::vector<std::uint16_t> partitions;  // dismember partition slot ids (e.g. 32 = body)
  // Triangles stored in the NiSkinPartition blocks (nullopt without a skin partition). The game
  // draws skinned SSE geometry from these partitions, not from the shape's own triangle list.
  std::optional<std::uint32_t> partition_triangles;
};

struct MeshFile {
  std::vector<Shape> shapes;
  std::vector<std::string> warnings;
  // HDT-SMP physics definition named by the root's "HDT Skinned Mesh Physics Object" string
  // extra data (Data-relative, e.g. "meshes\armor\skirt.xml"); empty when the mesh has none.
  std::string physics_xml;
};

// Parses a NIF from memory. Throws sns::Error(Parse) if the file cannot be read at all;
// shapes that cannot be converted are skipped with a warning.
MeshFile load_mesh(std::string_view bytes, std::string_view label);

struct Bone {
  std::string name;
  int parent = -1;
  Transform to_global;
};

struct Skeleton {
  std::vector<Bone> bones;
  [[nodiscard]] int index_of(std::string_view name) const;
};

Skeleton load_skeleton(std::string_view bytes, std::string_view label);

// "actors\x.dds", "Data\Textures\x.dds" or "textures/x.dds" -> "textures\x.dds" (empty stays empty).
std::string normalize_texture_path(std::string_view path);

// Makes `tangents` follow the UV gradient dP/du (swapping with `bitangents` if the file stores
// them the other way). Returns true when a swap was needed. Exposed for tests.
bool orient_tangent_basis(Shape& shape);

}  // namespace sns::nif
