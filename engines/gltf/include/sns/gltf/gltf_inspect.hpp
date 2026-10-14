#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Structural validation and inspection of glTF 2.0 assets (.glb binary and .gltf JSON with
// external or embedded resources), before anything is handed to a renderer.
//
// Qt's RuntimeLoader "does not sandbox or validate asset contents" (Qt 6.11 documentation), so
// Character Forge runs these checks first: container framing, JSON, every reference index, buffer
// and accessor ranges, every vertex index against the vertex count, node hierarchy, resource URIs
// confined to the asset folder, and size limits. Counts are reported in three kinds:
//   declared  - what the JSON lists;
//   validated - data whose references, ranges and indices were checked here;
//   rendered  - only the viewport can say that (not this library).
namespace sns::gltf {

struct Limits {
  std::uint64_t max_file_bytes = 512ull * 1024 * 1024;      // the .glb / .gltf itself
  std::uint64_t max_json_bytes = 64ull * 1024 * 1024;
  std::uint64_t max_resource_bytes = 1024ull * 1024 * 1024; // external + data: resources together
  std::uint64_t max_vertices = 20'000'000;                  // over all primitives
  std::uint64_t max_triangles = 20'000'000;
  std::size_t max_objects = 200'000;                        // per top-level array (nodes, accessors...)
};

struct Issue {
  std::string severity;  // "error" (not loadable) or "warning"
  std::string code;      // GlbTruncated, BufferMissing, ImageMissing, EmptyGeometry, ModelTooLarge...
  std::string message;   // technical, English
  std::string where;     // JSON pointer such as "/meshes/0/primitives/1" (may be empty)
};

struct Primitive {
  int mode = 4;  // 4 = triangles
  int material = -1;
  std::uint64_t vertices = 0;
  std::uint64_t indices = 0;    // 0 = not indexed
  std::uint64_t triangles = 0;
  bool normals = false;
  bool uv0 = false;
  bool skinned = false;         // JOINTS_0 + WEIGHTS_0
  std::size_t morph_targets = 0;
};

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
};

struct Skin {
  std::string name;
  std::vector<std::string> joints;  // node names ("" when unnamed)
};

struct Material {
  std::string name;
  std::string alpha_mode = "OPAQUE";
  bool double_sided = false;
  bool base_color_texture = false;
  bool normal_texture = false;
};

struct Image {
  std::string name;
  std::string mime;
  std::string source;  // "glb", "data-uri" or the relative path
  std::uint64_t bytes = 0;
};

struct Animation {
  std::string name;
  std::size_t channels = 0;
};

struct Node {
  std::string name;
  int mesh = -1;
  int skin = -1;
  int parent = -1;
  std::size_t children = 0;
};

// An external file the asset needs, resolved inside the asset's folder.
struct Resource {
  std::string uri;             // as written in the JSON (percent-decoded)
  std::string relative_path;   // '/'-separated path below the asset folder
  std::string kind;            // "buffer" or "image"
  std::uint64_t bytes = 0;
};

struct Inspection {
  std::string container;  // "glb" or "gltf"
  std::string generator;
  std::string version;
  std::uint64_t file_bytes = 0;

  // Declared: sizes of the JSON arrays.
  std::size_t scenes = 0, nodes = 0, meshes = 0, primitives = 0, accessors = 0, buffers = 0, buffer_views = 0,
              materials = 0, textures = 0, images = 0, samplers = 0, skins = 0, animations = 0, cameras = 0;
  std::size_t joints = 0;         // over all skins
  std::size_t morph_targets = 0;  // over all primitives

  // Validated: totals of primitives whose accessors passed the checks.
  std::uint64_t vertices = 0, indices = 0, triangles = 0;
  bool indices_checked = false;  // every index was read and compared with its vertex count
  // Union of the POSITION accessors' declared min/max (mesh-local space, node transforms not
  // applied): {min x, y, z, max x, y, z}.
  std::optional<std::array<double, 6>> position_bounds;

  std::vector<Mesh> mesh_list;
  std::vector<Node> node_list;
  // Nodes that draw a mesh, depth-first from the default scene's roots (scene 0 when "scene" is
  // absent; every root node when there are no scenes), children in array order.
  std::vector<std::size_t> mesh_nodes_in_scene_order;
  std::vector<Skin> skin_list;
  std::vector<Material> material_list;
  std::vector<Image> image_list;
  std::vector<Animation> animation_list;
  std::vector<std::string> extensions_used, extensions_required;
  std::vector<Resource> resources;
  std::vector<Issue> issues;

  [[nodiscard]] bool loadable() const;  // no issue is an error
};

// Returns the bytes of a resource given its validated relative path, or nullopt when missing.
using ResourceReader = std::function<std::optional<std::string>(const std::string& relative_path)>;

// Validates in-memory asset bytes. `glb` selects the container. Resources (external buffers and
// images) are fetched through `read`. `cancelled` is polled between expensive steps.
Inspection inspect(std::string_view bytes, bool glb, const ResourceReader& read, const Limits& limits = {},
                   const std::function<bool()>& cancelled = {});

// The asset read from disk, with every byte that was validated, so a caller can hand exactly
// these bytes to a renderer (no second read of a file that could have changed meanwhile).
struct LoadedAsset {
  Inspection inspection;
  std::string main_bytes;
  std::vector<std::pair<std::string, std::string>> resources;  // relative path, bytes
};

// Reads `file` (.glb or .gltf by extension) and validates it. External resources must be regular
// files below the asset's folder: absolute paths, drive letters, schemes (http:, file:...), "..",
// symbolic links and anything resolving outside the folder are rejected.
LoadedAsset load_and_inspect(const std::filesystem::path& file, const Limits& limits = {},
                             const std::function<bool()>& cancelled = {});

// Percent-decodes a URI reference and checks it is a plain relative path ("a/b c.bin"). Returns
// the '/'-separated path, or nullopt when it must be rejected.
std::optional<std::string> safe_relative_uri(std::string_view uri);

}  // namespace sns::gltf
