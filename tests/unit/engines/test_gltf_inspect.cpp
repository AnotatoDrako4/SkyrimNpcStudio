// glTF/GLB validation before rendering: real Blender-exported fixtures (tests/fixtures/models,
// made by tools/blender/make_test_models.py) and hand-made malformed assets.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

#include "sns/core/text.hpp"
#include "sns/gltf/gltf_inspect.hpp"

using namespace sns;
using Json = nlohmann::json;

namespace {

const std::filesystem::path kModels = sns::text::path_from_utf8(SNS_TEST_MODELS_DIR);

std::string read_all(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool has(const gltf::Inspection& i, const std::string& code) {
  return std::any_of(i.issues.begin(), i.issues.end(), [&](const gltf::Issue& x) { return x.code == code; });
}

std::string codes(const gltf::Inspection& i) {
  std::string out;
  for (const auto& x : i.issues) out += x.severity + ":" + x.code + " (" + x.message + ") ";
  return out;
}

std::string base64(std::string_view bytes) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                   static_cast<unsigned char>(bytes[i + 2]);
    out += {t[(n >> 18) & 63], t[(n >> 12) & 63], t[(n >> 6) & 63], t[n & 63]};
  }
  if (bytes.size() - i == 1) {
    const auto n = static_cast<unsigned char>(bytes[i]) << 16;
    out += {t[(n >> 18) & 63], t[(n >> 12) & 63], '=', '='};
  } else if (bytes.size() - i == 2) {
    const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
    out += {t[(n >> 18) & 63], t[(n >> 12) & 63], t[(n >> 6) & 63], '='};
  }
  return out;
}

// One triangle (3 float VEC3 positions + 3 ushort indices) in a data: URI buffer.
Json triangle(std::uint16_t i0 = 0, std::uint16_t i1 = 1, std::uint16_t i2 = 2) {
  std::string bin;
  for (const float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) bin.append(reinterpret_cast<const char*>(&f), 4);
  for (const std::uint16_t i : {i0, i1, i2}) bin.append(reinterpret_cast<const char*>(&i), 2);
  bin.append(2, '\0');
  Json j = Json::object();
  j["asset"] = Json{{"version", "2.0"}};
  j["scene"] = 0;
  j["scenes"] = Json::array({Json{{"nodes", Json::array({0})}}});
  j["nodes"] = Json::array({Json{{"mesh", 0}}});
  Json primitive = Json::object();
  primitive["attributes"] = Json{{"POSITION", 0}};
  primitive["indices"] = 1;
  j["meshes"] = Json::array({Json{{"primitives", Json::array({primitive})}}});
  Json positions = Json::object();
  positions["bufferView"] = 0;
  positions["componentType"] = 5126;
  positions["count"] = 3;
  positions["type"] = "VEC3";
  positions["min"] = Json::array({0, 0, 0});
  positions["max"] = Json::array({1, 1, 0});
  Json indices = Json::object();
  indices["bufferView"] = 1;
  indices["componentType"] = 5123;
  indices["count"] = 3;
  indices["type"] = "SCALAR";
  j["accessors"] = Json::array({positions, indices});
  j["bufferViews"] = Json::array({Json{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                  Json{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}}});
  j["buffers"] = Json::array(
      {Json{{"byteLength", static_cast<int>(bin.size())}, {"uri", "data:application/octet-stream;base64," + base64(bin)}}});
  return j;
}

gltf::Inspection inspect_json(const Json& j, const gltf::ResourceReader& read = {}, const gltf::Limits& limits = {},
                              const std::function<bool()>& cancelled = {}) {
  const auto text = j.dump();
  return gltf::inspect(text, false, read, limits, cancelled);
}

void put_le32(std::string& s, std::size_t at, std::uint32_t v) { std::memcpy(s.data() + at, &v, 4); }

}  // namespace

TEST_CASE("Blender-exported GLB passes and reports declared and validated geometry", "[gltf]") {
  const auto asset = gltf::load_and_inspect(kModels / "suzanne.glb");
  const auto& i = asset.inspection;
  INFO(codes(i));
  REQUIRE(i.loadable());
  CHECK(i.container == "glb");
  CHECK(i.version == "2.0");
  CHECK(i.generator.find("Khronos glTF Blender I/O") != std::string::npos);
  CHECK(i.meshes == 1);
  CHECK(i.primitives == 1);
  CHECK(i.materials == 1);
  CHECK(i.vertices > 500);         // Suzanne's 507 vertices, split along sharp/UV seams by the exporter
  CHECK(i.triangles == 968);       // 500 faces triangulated
  CHECK(i.indices == i.triangles * 3);
  CHECK(i.indices_checked);
  REQUIRE(i.position_bounds);
  CHECK((*i.position_bounds)[3] - (*i.position_bounds)[0] > 2.0);  // size 2.0 monkey, ~2.7 wide
  CHECK(i.resources.empty());
  CHECK(asset.main_bytes.size() == i.file_bytes);
}

TEST_CASE("Rigged figure: skin joints, morph target, animation and embedded texture", "[gltf]") {
  const auto asset = gltf::load_and_inspect(kModels / "rigged_figure.glb");
  const auto& i = asset.inspection;
  INFO(codes(i));
  REQUIRE(i.loadable());
  REQUIRE(i.skin_list.size() == 1);
  const auto& joints = i.skin_list[0].joints;
  CHECK(i.joints == 4);
  for (const auto* name : {"Root", "Spine", "Chest", "Head"}) CHECK(std::find(joints.begin(), joints.end(), name) != joints.end());
  CHECK(i.animations == 1);
  CHECK(i.morph_targets >= 1);
  REQUIRE(i.image_list.size() == 1);
  CHECK(i.image_list[0].source == "glb");
  CHECK(i.image_list[0].mime == "image/png");
  REQUIRE(i.material_list.size() == 1);
  CHECK(i.material_list[0].base_color_texture);
  REQUIRE_FALSE(i.mesh_list.empty());
  CHECK(i.mesh_list[0].primitives[0].skinned);
  CHECK(i.indices_checked);
}

TEST_CASE("Node list and the scene order of nodes that draw a mesh", "[gltf]") {
  const auto figure = gltf::load_and_inspect(kModels / "rigged_figure.glb").inspection;
  REQUIRE(figure.node_list.size() == figure.nodes);
  const auto named = [&](const std::string& name) {
    const auto it = std::find_if(figure.node_list.begin(), figure.node_list.end(), [&](const gltf::Node& n) { return n.name == name; });
    REQUIRE(it != figure.node_list.end());
    return static_cast<std::size_t>(it - figure.node_list.begin());
  };
  const auto body = named("Figure");
  const auto rig = named("FigureRig");
  CHECK(figure.node_list[body].mesh == 0);
  CHECK(figure.node_list[body].skin == 0);
  CHECK(figure.node_list[body].parent == static_cast<int>(rig));
  CHECK(figure.node_list[rig].parent == -1);
  CHECK(figure.node_list[rig].children == 2);
  CHECK(figure.node_list[named("Head")].mesh == -1);
  CHECK(figure.mesh_nodes_in_scene_order == std::vector<std::size_t>{body});

  const auto suzanne = gltf::load_and_inspect(kModels / "suzanne.glb").inspection;
  REQUIRE(suzanne.mesh_nodes_in_scene_order.size() == 1);
  CHECK(suzanne.node_list[suzanne.mesh_nodes_in_scene_order[0]].name == "Suzanne");

  // Blender export with a hierarchy: Body > Helmet, Body > Shield, Sword at the root.
  const auto set = gltf::load_and_inspect(kModels / "parts_set.glb").inspection;
  INFO(codes(set));
  REQUIRE(set.loadable());
  std::vector<std::string> names;
  for (const auto n : set.mesh_nodes_in_scene_order) names.push_back(set.node_list[n].name);
  CHECK(names == std::vector<std::string>{"Body", "Helmet", "Shield", "Sword"});
  const auto& set_body = set.node_list[set.mesh_nodes_in_scene_order[0]];
  REQUIRE(set_body.mesh >= 0);
  CHECK(set.mesh_list[static_cast<std::size_t>(set_body.mesh)].primitives.size() == 2);  // two materials
  CHECK(set_body.children == 2);
  CHECK(set.node_list[set.mesh_nodes_in_scene_order[1]].parent == static_cast<int>(set.mesh_nodes_in_scene_order[0]));

  // Scene roots in their listed order, then depth first with children in array order.
  auto j = triangle();
  j["nodes"] = Json::array({Json{{"mesh", 0}, {"children", Json::array({3, 1})}}, Json{{"mesh", 0}}, Json{{"mesh", 0}},
                            Json{{"name", "group"}, {"children", Json::array({4})}}, Json{{"mesh", 0}}});
  j["scenes"] = Json::array({Json{{"nodes", Json::array({2, 0})}}});
  auto order = inspect_json(j);
  INFO(codes(order));
  REQUIRE(order.loadable());
  CHECK(order.mesh_nodes_in_scene_order == std::vector<std::size_t>{2, 0, 4, 1});

  // "scene" absent: scene 0. No scenes at all: every root node in index order.
  j.erase("scene");
  CHECK(inspect_json(j).mesh_nodes_in_scene_order == std::vector<std::size_t>{2, 0, 4, 1});
  j.erase("scenes");
  CHECK(inspect_json(j).mesh_nodes_in_scene_order == std::vector<std::size_t>{0, 4, 1, 2});

  // An invalid hierarchy gives no order (nothing would be matched by position).
  j["nodes"][4]["children"] = Json::array({3});
  const auto cyclic = inspect_json(j);
  CHECK(has(cyclic, "NodeHierarchyInvalid"));
  CHECK(cyclic.mesh_nodes_in_scene_order.empty());
}

TEST_CASE("Split glTF: external .bin and .png are read from the asset folder", "[gltf]") {
  const auto asset = gltf::load_and_inspect(kModels / "rigged_figure_split" / "rigged_figure.gltf");
  const auto& i = asset.inspection;
  INFO(codes(i));
  REQUIRE(i.loadable());
  CHECK(i.container == "gltf");
  REQUIRE(i.resources.size() == 2);
  REQUIRE(asset.resources.size() == 2);
  std::map<std::string, std::string> files(asset.resources.begin(), asset.resources.end());
  CHECK(files.contains("rigged_figure.bin"));
  CHECK(files.contains("FigureGrid.png"));
  CHECK(i.image_list[0].source == "FigureGrid.png");
}

TEST_CASE("Missing external resources are reported as missing buffer or texture", "[gltf]") {
  const auto dir = std::filesystem::temp_directory_path() / "sns-gltf-missing";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::filesystem::copy_file(kModels / "rigged_figure_split" / "rigged_figure.gltf", dir / "rigged_figure.gltf");
  std::filesystem::copy_file(kModels / "rigged_figure_split" / "rigged_figure.bin", dir / "rigged_figure.bin");
  auto no_texture = gltf::load_and_inspect(dir / "rigged_figure.gltf");
  CHECK(has(no_texture.inspection, "ImageMissing"));
  CHECK_FALSE(no_texture.inspection.loadable());
  std::filesystem::remove(dir / "rigged_figure.bin");
  auto no_buffer = gltf::load_and_inspect(dir / "rigged_figure.gltf");
  CHECK(has(no_buffer.inspection, "BufferMissing"));
  std::filesystem::remove_all(dir);
}

TEST_CASE("Malformed GLB containers are rejected before any JSON is used", "[gltf]") {
  const auto good = read_all(kModels / "suzanne.glb");
  REQUIRE(good.size() > 100);
  auto check = [](const std::string& bytes, const char* code) {
    const auto i = gltf::inspect(bytes, true, {});
    INFO(code << ": " << codes(i));
    CHECK(has(i, code));
    CHECK_FALSE(i.loadable());
  };
  check(good.substr(0, good.size() - 100), "GlbTruncated");  // cut file: header length > size
  check(good.substr(0, 10), "GlbTruncated");
  std::string magic = good;
  magic[0] = 'x';
  check(magic, "NotGltf");
  std::string version = good;
  put_le32(version, 4, 1);
  check(version, "UnsupportedVersion");
  std::string chunk = good;
  put_le32(chunk, 12, static_cast<std::uint32_t>(good.size()));  // JSON chunk longer than the file
  check(chunk, "GlbTruncated");
  std::string json = good;
  json[20] = '!';  // first byte of the JSON chunk
  check(json, "JsonInvalid");
  check(std::string("not a glb at all, just text"), "NotGltf");
}

TEST_CASE("Reference, range and index errors are found in the data", "[gltf]") {
  CHECK(inspect_json(triangle()).loadable());

  auto bad_index = inspect_json(triangle(0, 1, 5));
  INFO(codes(bad_index));
  CHECK(has(bad_index, "IndexOutOfRange"));

  auto out_of_bounds = triangle();
  out_of_bounds["accessors"][0]["count"] = 30;
  CHECK(has(inspect_json(out_of_bounds), "AccessorOutOfBounds"));

  auto bad_ref = triangle();
  bad_ref["meshes"][0]["primitives"][0]["attributes"]["POSITION"] = 7;
  CHECK(has(inspect_json(bad_ref), "InvalidReference"));

  auto view_past_buffer = triangle();
  view_past_buffer["bufferViews"][1]["byteLength"] = 64;
  CHECK(has(inspect_json(view_past_buffer), "AccessorOutOfBounds"));

  auto empty = triangle();
  empty["meshes"][0]["primitives"][0]["attributes"] = Json{{"NORMAL", 0}};
  const auto empty_result = inspect_json(empty);
  CHECK(has(empty_result, "EmptyGeometry"));
  CHECK_FALSE(empty_result.loadable());

  auto cycle = triangle();
  cycle["nodes"] = Json::array({Json{{"mesh", 0}, {"children", Json::array({1})}}, Json{{"children", Json::array({0})}}});
  CHECK(has(inspect_json(cycle), "NodeHierarchyInvalid"));

  auto two_parents = triangle();
  two_parents["nodes"] = Json::array({Json{{"mesh", 0}, {"children", Json::array({2})}}, Json{{"children", Json::array({2})}},
                                      Json::object()});
  CHECK(has(inspect_json(two_parents), "NodeHierarchyInvalid"));

  auto old = triangle();
  old["asset"]["version"] = "1.0";
  CHECK(has(inspect_json(old), "UnsupportedVersion"));

  auto draco = triangle();
  draco["extensionsRequired"] = Json::array({"KHR_draco_mesh_compression"});
  CHECK(has(inspect_json(draco), "CompressedDataUnsupported"));
}

TEST_CASE("Resource URIs cannot leave the asset folder or reach the network", "[gltf]") {
  for (const auto* uri : {"../secret.bin", "a/../../secret.bin", "%2e%2e/secret.bin", "C:/Windows/win.ini", "/etc/passwd",
                          "\\\\server\\share\\x.bin", "https://example.com/x.bin", "file:///C:/x.bin", "a//b.bin", "x.bin?q",
                          "%zz.bin", ""}) {
    INFO(uri);
    CHECK_FALSE(gltf::safe_relative_uri(uri));
    int reads = 0;
    auto j = triangle();
    j["buffers"][0]["uri"] = uri;
    const auto result = inspect_json(j, [&](const std::string&) -> std::optional<std::string> {
      ++reads;
      return std::string(64, '\0');
    });
    CHECK(has(result, "UriRejected"));
    CHECK(reads == 0);  // the reader is never asked for a rejected path
  }
  CHECK(gltf::safe_relative_uri("textures/skin%20diffuse.png") == std::optional<std::string>("textures/skin diffuse.png"));
  CHECK(gltf::safe_relative_uri("./model.bin") == std::optional<std::string>("model.bin"));
}

TEST_CASE("Size limits and cancellation stop the inspection", "[gltf]") {
  gltf::Limits tiny;
  tiny.max_file_bytes = 1000;
  const auto big = gltf::load_and_inspect(kModels / "suzanne.glb", tiny);
  CHECK(has(big.inspection, "ModelTooLarge"));

  gltf::Limits few;
  few.max_vertices = 100;
  const auto many = gltf::load_and_inspect(kModels / "suzanne.glb", few);
  CHECK(has(many.inspection, "ModelTooLarge"));

  const auto cancelled = gltf::load_and_inspect(kModels / "suzanne.glb", {}, [] { return true; });
  CHECK(has(cancelled.inspection, "Cancelled"));
  CHECK_FALSE(cancelled.inspection.loadable());

  CHECK(has(gltf::load_and_inspect(kModels / "does-not-exist.glb").inspection, "FileUnreadable"));
  CHECK(has(gltf::load_and_inspect(kModels / "suzanne.obj").inspection, "NotGltf"));
}
