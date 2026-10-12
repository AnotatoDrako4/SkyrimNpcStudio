#pragma once

#include <array>
#include <cstdint>
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
  bool double_sided = false;
  bool model_space_normals = false;
  bool vertex_colors = false;
  bool alpha_blend = false;
  bool alpha_test = false;
  std::uint8_t alpha_threshold = 128;

  bool skinned = false;
  Transform global_to_skin;               // identity for unskinned shapes
  std::vector<std::string> bones;
  std::vector<Transform> skin_to_bone;    // one per bone
  std::vector<Influence> influences;      // one per vertex when skinned
  std::vector<std::uint16_t> partitions;  // dismember partition slot ids (e.g. 32 = body)
};

struct MeshFile {
  std::vector<Shape> shapes;
  std::vector<std::string> warnings;
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

}  // namespace sns::nif
