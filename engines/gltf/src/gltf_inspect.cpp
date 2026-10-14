#include "sns/gltf/gltf_inspect.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <system_error>

#include "sns/core/text.hpp"

namespace sns::gltf {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t kGlbMagic = 0x46546C67;   // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534A;  // "JSON"
constexpr std::uint32_t kChunkBin = 0x004E4942;   // "BIN\0"

std::uint32_t le32(const char* p) {
  std::uint32_t v = 0;
  std::memcpy(&v, p, 4);  // Windows x64 and the GLB format are both little-endian
  return v;
}

std::size_t component_size(int type) {
  switch (type) {
    case 5120: case 5121: return 1;  // (u)byte
    case 5122: case 5123: return 2;  // (u)short
    case 5125: case 5126: return 4;  // uint, float
    default: return 0;
  }
}

int component_count(std::string_view type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT2") return 4;
  if (type == "MAT3") return 9;
  if (type == "MAT4") return 16;
  return 0;
}

std::optional<std::string> decode_base64(std::string_view in) {
  static constexpr signed char table[256] = {
#define X -1
      X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
      X, X, X, X, X, X, X, X, X, X, X, 62, X, X, X, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, X, X, X, X, X, X,
      X, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, X, X, X, X, X,
      X, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, X, X, X, X, X,
      X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
      X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
      X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
      X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
#undef X
  };
  while (!in.empty() && in.back() == '=') in.remove_suffix(1);
  std::string out;
  out.reserve(in.size() * 3 / 4);
  std::uint32_t acc = 0;
  int bits = 0;
  for (const char c : in) {
    const auto v = table[static_cast<unsigned char>(c)];
    if (v < 0) return std::nullopt;
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

bool finite_numbers(const Json& a, std::size_t n) {
  if (!a.is_array() || a.size() != n) return false;
  return std::all_of(a.begin(), a.end(), [](const Json& v) { return v.is_number() && std::isfinite(v.get<double>()); });
}

struct Buffer {
  bool ok = false;
  std::uint64_t length = 0;
  std::string_view data;
};

struct View {
  bool ok = false;
  std::size_t buffer = 0;
  std::uint64_t offset = 0, length = 0;
  std::size_t stride = 0;  // 0 = tightly packed
};

struct Accessor {
  bool ok = false;
  std::uint64_t count = 0;
  int component = 0;
  int components = 0;
  std::size_t element = 0;
  bool has_view = false;
  std::size_t view = 0;
  std::uint64_t offset = 0;
  std::string type;
  const Json* min = nullptr;
  const Json* max = nullptr;
};

class Checker {
 public:
  Checker(Inspection& out, const ResourceReader& read, const Limits& limits, const std::function<bool()>& cancelled)
      : out_(out), read_(read), limits_(limits), cancelled_(cancelled) {}

  void run(std::string_view json_text, std::optional<std::string_view> bin) {
    if (json_text.size() > limits_.max_json_bytes) {
      error("ModelTooLarge", "glTF JSON of " + std::to_string(json_text.size()) + " bytes exceeds the limit", "");
      return;
    }
    root_ = Json::parse(json_text.begin(), json_text.end(), nullptr, /*allow_exceptions=*/false);
    if (root_.is_discarded() || !root_.is_object()) {
      error("JsonInvalid", "the glTF JSON is not a valid JSON object", "");
      return;
    }
    const auto asset = root_.find("asset");
    if (asset == root_.end() || !asset->is_object() || !asset->contains("version") || !(*asset)["version"].is_string()) {
      error("JsonInvalid", "missing asset.version", "/asset");
      return;
    }
    out_.version = (*asset)["version"].get<std::string>();
    if (asset->contains("generator") && (*asset)["generator"].is_string()) out_.generator = (*asset)["generator"].get<std::string>();
    if (out_.version.substr(0, out_.version.find('.')) != "2") {
      error("UnsupportedVersion", "glTF " + out_.version + " is not glTF 2.x", "/asset/version");
      return;
    }
    strings("extensionsUsed", out_.extensions_used);
    strings("extensionsRequired", out_.extensions_required);
    for (const auto& ext : out_.extensions_required) {
      if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression" || ext == "KHR_texture_basisu") {
        error("CompressedDataUnsupported", "the asset requires " + ext + ", whose data cannot be validated here", "/extensionsRequired");
      } else {
        warn("ExtensionRequired", "the asset requires " + ext + "; the renderer may not support it", "/extensionsRequired");
      }
    }
    for (const char* key : {"scenes", "nodes", "meshes", "accessors", "buffers", "bufferViews", "materials", "textures", "images",
                            "samplers", "skins", "animations", "cameras"}) {
      if (!array(key)) return;  // schema error already reported
    }
    out_.scenes = size("scenes");
    out_.nodes = size("nodes");
    out_.meshes = size("meshes");
    out_.accessors = size("accessors");
    out_.buffers = size("buffers");
    out_.buffer_views = size("bufferViews");
    out_.materials = size("materials");
    out_.textures = size("textures");
    out_.images = size("images");
    out_.samplers = size("samplers");
    out_.skins = size("skins");
    out_.animations = size("animations");
    out_.cameras = size("cameras");

    buffers(bin);
    if (stop()) return;
    views();
    accessors();
    images();
    if (stop()) return;
    textures_and_materials();
    meshes();
    if (stop()) return;
    nodes_and_scenes();
    skins();
    animations();
    totals();
  }

 private:
  void error(std::string code, std::string message, std::string where) {
    out_.issues.push_back({"error", std::move(code), std::move(message), std::move(where)});
  }
  void warn(std::string code, std::string message, std::string where) {
    out_.issues.push_back({"warning", std::move(code), std::move(message), std::move(where)});
  }
  bool stop() {
    if (cancelled_ && cancelled_()) {
      if (!cancelled_reported_) error("Cancelled", "validation cancelled", "");
      cancelled_reported_ = true;
      return true;
    }
    return false;
  }
  const Json& items(const char* key) const {
    static const Json empty = Json::array();
    const auto it = root_.find(key);
    return it == root_.end() ? empty : *it;
  }
  std::size_t size(const char* key) const { return items(key).size(); }
  bool array(const char* key) {
    const auto it = root_.find(key);
    if (it == root_.end()) return true;
    if (!it->is_array() || !std::all_of(it->begin(), it->end(), [](const Json& e) { return e.is_object(); })) {
      error("JsonInvalid", std::string(key) + " must be an array of objects", std::string("/") + key);
      return false;
    }
    if (it->size() > limits_.max_objects) {
      error("ModelTooLarge", std::string(key) + " has " + std::to_string(it->size()) + " entries", std::string("/") + key);
      return false;
    }
    return true;
  }
  void strings(const char* key, std::vector<std::string>& into) {
    const auto it = root_.find(key);
    if (it == root_.end()) return;
    if (!it->is_array()) {
      warn("JsonInvalid", std::string(key) + " is not an array", std::string("/") + key);
      return;
    }
    for (const auto& v : *it) {
      if (v.is_string()) into.push_back(v.get<std::string>());
    }
  }
  // Index property `key` of `object` must reference an entry of an array of size n.
  std::optional<std::size_t> ref(const Json& object, const char* key, std::size_t n, const std::string& where,
                                 bool required = false) {
    const auto it = object.find(key);
    if (it == object.end()) {
      if (required) error("InvalidReference", std::string("missing ") + key, where);
      return std::nullopt;
    }
    if (!it->is_number_integer() || it->get<long long>() < 0 || static_cast<std::uint64_t>(it->get<long long>()) >= n) {
      error("InvalidReference", std::string(key) + " does not reference an existing entry", where + "/" + key);
      return std::nullopt;
    }
    return static_cast<std::size_t>(it->get<long long>());
  }
  std::uint64_t uint_value(const Json& object, const char* key, std::uint64_t fallback, const std::string& where, bool& ok) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_number_integer() || it->get<long long>() < 0) {
      error("JsonInvalid", std::string(key) + " must be a non-negative integer", where + "/" + key);
      ok = false;
      return fallback;
    }
    return static_cast<std::uint64_t>(it->get<long long>());
  }

  // Bytes of a URI reference: data: (base64) or a confined relative file.
  std::optional<std::string_view> resource(const std::string& uri, const std::string& kind, const std::string& where,
                                           Image* image) {
    if (uri.rfind("data:", 0) == 0) {
      const auto comma = uri.find(',');
      if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos) {
        error("UriRejected", "only base64 data: URIs are supported", where);
        return std::nullopt;
      }
      if (image) {
        image->source = "data-uri";
        image->mime = uri.substr(5, uri.find(';') - 5);
      }
      auto decoded = decode_base64(std::string_view(uri).substr(comma + 1));
      if (!decoded) {
        error("UriRejected", "the data: URI is not valid base64", where);
        return std::nullopt;
      }
      return own(std::move(*decoded), where);
    }
    const auto relative = safe_relative_uri(uri);
    if (!relative) {
      error("UriRejected", "'" + uri + "' is not a relative path inside the asset folder (absolute, remote or '..')", where);
      return std::nullopt;
    }
    if (image) image->source = *relative;
    auto bytes = read_ ? read_(*relative) : std::nullopt;
    if (!bytes) {
      error(kind == "image" ? "ImageMissing" : "BufferMissing", "'" + *relative + "' is missing or unreadable", where);
      return std::nullopt;
    }
    out_.resources.push_back({uri, *relative, kind, bytes->size()});
    return own(std::move(*bytes), where);
  }
  std::optional<std::string_view> own(std::string bytes, const std::string& where) {
    resource_bytes_ += bytes.size();
    if (resource_bytes_ > limits_.max_resource_bytes) {
      error("ModelTooLarge", "external and embedded resources exceed " + std::to_string(limits_.max_resource_bytes) + " bytes", where);
      return std::nullopt;
    }
    owned_.push_back(std::make_unique<std::string>(std::move(bytes)));
    return std::string_view(*owned_.back());
  }

  void buffers(std::optional<std::string_view> bin) {
    const auto& list = items("buffers");
    buffers_.resize(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/buffers/" + std::to_string(i);
      const auto& b = list[i];
      bool ok = true;
      const auto length = uint_value(b, "byteLength", 0, where, ok);
      if (!ok) continue;
      if (length == 0) {
        error("JsonInvalid", "byteLength must be at least 1", where);
        continue;
      }
      std::optional<std::string_view> data;
      if (b.contains("uri")) {
        if (!b["uri"].is_string()) {
          error("JsonInvalid", "uri must be a string", where + "/uri");
          continue;
        }
        data = resource(b["uri"].get<std::string>(), "buffer", where, nullptr);
      } else if (out_.container == "glb" && i == 0) {
        if (!bin) {
          error("BufferMissing", "buffer 0 refers to the GLB BIN chunk, but the file has none", where);
          continue;
        }
        data = bin;
      } else {
        error("BufferMissing", "the buffer has no uri", where);
        continue;
      }
      if (!data) continue;
      if (data->size() < length) {
        error(out_.container == "glb" && !b.contains("uri") ? "GlbTruncated" : "BufferMissing",
              "the buffer declares " + std::to_string(length) + " bytes but only " + std::to_string(data->size()) + " exist",
              where);
        continue;
      }
      buffers_[i] = {true, length, data->substr(0, static_cast<std::size_t>(length))};
    }
  }

  void views() {
    const auto& list = items("bufferViews");
    views_.resize(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/bufferViews/" + std::to_string(i);
      const auto& v = list[i];
      const auto buffer = ref(v, "buffer", buffers_.size(), where, true);
      bool ok = true;
      const auto offset = uint_value(v, "byteOffset", 0, where, ok);
      const auto length = uint_value(v, "byteLength", 0, where, ok);
      const auto stride = uint_value(v, "byteStride", 0, where, ok);
      if (!buffer || !ok) continue;
      if (length == 0) {
        error("JsonInvalid", "byteLength must be at least 1", where);
        continue;
      }
      if (stride != 0 && (stride < 4 || stride > 252 || stride % 4 != 0)) {
        error("JsonInvalid", "byteStride must be 4..252 and a multiple of 4", where + "/byteStride");
        continue;
      }
      if (!buffers_[*buffer].ok) continue;  // reported with the buffer
      if (offset > buffers_[*buffer].length || length > buffers_[*buffer].length - offset) {
        error("AccessorOutOfBounds", "the view exceeds its buffer", where);
        continue;
      }
      views_[i] = {true, *buffer, offset, length, static_cast<std::size_t>(stride)};
    }
  }

  void accessors() {
    const auto& list = items("accessors");
    accessors_.resize(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/accessors/" + std::to_string(i);
      const auto& a = list[i];
      Accessor acc;
      bool ok = true;
      acc.count = uint_value(a, "count", 0, where, ok);
      acc.offset = uint_value(a, "byteOffset", 0, where, ok);
      if (!a.contains("componentType") || !a["componentType"].is_number_integer() ||
          component_size(a["componentType"].get<int>()) == 0) {
        error("JsonInvalid", "invalid componentType", where);
        continue;
      }
      acc.component = a["componentType"].get<int>();
      acc.type = a.value("type", std::string());
      acc.components = component_count(acc.type);
      if (!ok || acc.components == 0 || acc.count == 0) {
        error("JsonInvalid", "invalid type or count", where);
        continue;
      }
      if (acc.count > limits_.max_vertices * 3) {
        error("ModelTooLarge", "accessor count " + std::to_string(acc.count), where);
        continue;
      }
      acc.element = component_size(acc.component) * static_cast<std::size_t>(acc.components);
      if (a.contains("min")) acc.min = &a["min"];
      if (a.contains("max")) acc.max = &a["max"];
      if (a.contains("bufferView")) {
        const auto view = ref(a, "bufferView", views_.size(), where);
        if (!view) continue;
        if (!views_[*view].ok) continue;
        const auto& v = views_[*view];
        const std::uint64_t stride = v.stride ? v.stride : acc.element;
        if (acc.offset % component_size(acc.component) != 0) warn("AccessorMisaligned", "byteOffset is not aligned", where);
        const std::uint64_t needed = acc.offset + stride * (acc.count - 1) + acc.element;
        if (needed > v.length) {
          error("AccessorOutOfBounds", "the accessor reads " + std::to_string(needed) + " bytes of a " +
                                           std::to_string(v.length) + "-byte view", where);
          continue;
        }
        acc.has_view = true;
        acc.view = *view;
      } else if (!a.contains("sparse")) {
        warn("AccessorWithoutData", "no bufferView: the accessor contains zeros", where);
      }
      if (a.contains("sparse")) sparse(a["sparse"], acc, where + "/sparse");
      acc.ok = true;
      accessors_[i] = acc;
    }
  }

  void sparse(const Json& s, const Accessor& acc, const std::string& where) {
    if (!s.is_object() || !s.contains("count") || !s["count"].is_number_integer() || s["count"].get<long long>() < 1 ||
        static_cast<std::uint64_t>(s["count"].get<long long>()) > acc.count) {
      error("JsonInvalid", "invalid sparse count", where);
      return;
    }
    const auto n = static_cast<std::uint64_t>(s["count"].get<long long>());
    for (const char* part : {"indices", "values"}) {
      if (!s.contains(part) || !s[part].is_object()) {
        error("JsonInvalid", std::string("missing sparse ") + part, where);
        continue;
      }
      const auto& p = s[part];
      const auto view = ref(p, "bufferView", views_.size(), where + "/" + part, true);
      if (!view || !views_[*view].ok) continue;
      bool ok = true;
      const auto offset = uint_value(p, "byteOffset", 0, where + "/" + part, ok);
      std::size_t element = acc.element;
      if (std::string_view(part) == "indices") {
        element = p.contains("componentType") && p["componentType"].is_number_integer() ? component_size(p["componentType"].get<int>()) : 0;
        if (element == 0 || (element == 1 && p["componentType"].get<int>() != 5121)) {
          error("JsonInvalid", "invalid sparse index type", where + "/indices");
          continue;
        }
      }
      if (!ok || offset + element * n > views_[*view].length) error("AccessorOutOfBounds", std::string("sparse ") + part + " exceed their view", where);
    }
  }

  // Reads index `k` of an index accessor (validated: has a view, in range).
  std::uint64_t index_at(const Accessor& a, std::uint64_t k) const {
    const auto& v = views_[a.view];
    const auto& b = buffers_[v.buffer];
    const std::uint64_t stride = v.stride ? v.stride : a.element;
    const char* p = b.data.data() + v.offset + a.offset + stride * k;
    switch (a.component) {
      case 5121: return static_cast<unsigned char>(*p);
      case 5123: {
        std::uint16_t x;
        std::memcpy(&x, p, 2);
        return x;
      }
      default: {
        std::uint32_t x;
        std::memcpy(&x, p, 4);
        return x;
      }
    }
  }

  void images() {
    const auto& list = items("images");
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/images/" + std::to_string(i);
      const auto& im = list[i];
      Image image;
      image.name = im.value("name", std::string());
      image.mime = im.value("mimeType", std::string());
      std::optional<std::string_view> data;
      if (im.contains("uri") && im["uri"].is_string()) {
        data = resource(im["uri"].get<std::string>(), "image", where, &image);
      } else if (im.contains("bufferView")) {
        image.source = "glb";
        if (const auto view = ref(im, "bufferView", views_.size(), where); view && views_[*view].ok) {
          const auto& v = views_[*view];
          data = buffers_[v.buffer].data.substr(static_cast<std::size_t>(v.offset), static_cast<std::size_t>(v.length));
        }
        if (image.mime.empty()) error("JsonInvalid", "an image in a bufferView needs mimeType", where);
      } else {
        error("ImageMissing", "the image has neither uri nor bufferView", where);
      }
      if (data) {
        image.bytes = data->size();
        const bool png = data->size() >= 8 && data->substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8);
        const bool jpeg = data->size() >= 3 && static_cast<unsigned char>((*data)[0]) == 0xFF &&
                          static_cast<unsigned char>((*data)[1]) == 0xD8 && static_cast<unsigned char>((*data)[2]) == 0xFF;
        if (!png && !jpeg) {
          warn("ImageFormatUnsupported", "the image is neither PNG nor JPEG; it may not be displayed", where);
        } else if ((png && image.mime == "image/jpeg") || (jpeg && image.mime == "image/png")) {
          warn("ImageFormatMismatch", "mimeType does not match the image data", where);
        }
        if (image.mime.empty()) image.mime = png ? "image/png" : jpeg ? "image/jpeg" : "";
      }
      out_.image_list.push_back(std::move(image));
    }
  }

  void texture_ref(const Json& m, const char* path, const char* key, const std::string& where, bool* flag) {
    const Json* object = &m;
    if (path) {
      const auto it = m.find(path);
      if (it == m.end() || !it->is_object()) return;
      object = &*it;
    }
    const auto t = object->find(key);
    if (t == object->end()) return;
    if (!t->is_object()) {
      error("JsonInvalid", std::string(key) + " must be an object", where);
      return;
    }
    if (ref(*t, "index", out_.textures, where + "/" + key, true) && flag) *flag = true;
  }

  void textures_and_materials() {
    const auto& textures = items("textures");
    for (std::size_t i = 0; i < textures.size(); ++i) {
      const auto where = "/textures/" + std::to_string(i);
      ref(textures[i], "sampler", out_.samplers, where);
      if (textures[i].contains("source")) {
        ref(textures[i], "source", out_.images, where);
      } else {
        warn("TextureWithoutSource", "the texture has no PNG/JPEG source (extension-only image)", where);
      }
    }
    const auto& materials = items("materials");
    for (std::size_t i = 0; i < materials.size(); ++i) {
      const auto where = "/materials/" + std::to_string(i);
      const auto& m = materials[i];
      Material material;
      material.name = m.value("name", std::string());
      material.alpha_mode = m.value("alphaMode", std::string("OPAQUE"));
      material.double_sided = m.value("doubleSided", false);
      if (material.alpha_mode != "OPAQUE" && material.alpha_mode != "MASK" && material.alpha_mode != "BLEND") {
        warn("MaterialUnsupported", "unknown alphaMode " + material.alpha_mode, where);
      }
      texture_ref(m, "pbrMetallicRoughness", "baseColorTexture", where, &material.base_color_texture);
      texture_ref(m, "pbrMetallicRoughness", "metallicRoughnessTexture", where, nullptr);
      texture_ref(m, nullptr, "normalTexture", where, &material.normal_texture);
      texture_ref(m, nullptr, "occlusionTexture", where, nullptr);
      texture_ref(m, nullptr, "emissiveTexture", where, nullptr);
      if (m.contains("extensions") && m["extensions"].is_object() && m["extensions"].contains("KHR_materials_pbrSpecularGlossiness")) {
        warn("MaterialUnsupported", "specular-glossiness materials may render differently", where);
      }
      out_.material_list.push_back(std::move(material));
    }
  }

  // An attribute accessor of a primitive: valid reference and the expected shape.
  const Accessor* attribute(const Json& attributes, const char* name, const std::string& where) {
    const auto index = ref(attributes, name, accessors_.size(), where);
    if (!index || !accessors_[*index].ok) return nullptr;
    return &accessors_[*index];
  }

  void meshes() {
    const auto& list = items("meshes");
    for (std::size_t m = 0; m < list.size(); ++m) {
      const auto mesh_where = "/meshes/" + std::to_string(m);
      Mesh mesh;
      mesh.name = list[m].value("name", std::string());
      const auto prims = list[m].find("primitives");
      if (prims == list[m].end() || !prims->is_array() || prims->empty()) {
        error("JsonInvalid", "a mesh needs at least one primitive", mesh_where);
        out_.mesh_list.push_back(std::move(mesh));
        continue;
      }
      for (std::size_t p = 0; p < prims->size(); ++p) {
        if (stop()) return;
        const auto where = mesh_where + "/primitives/" + std::to_string(p);
        const auto& prim = (*prims)[p];
        ++out_.primitives;
        Primitive info;
        if (!prim.is_object() || !prim.contains("attributes") || !prim["attributes"].is_object()) {
          error("JsonInvalid", "a primitive needs attributes", where);
          continue;
        }
        info.mode = prim.value("mode", 4);
        if (info.mode < 0 || info.mode > 6) {
          error("JsonInvalid", "invalid primitive mode", where + "/mode");
          continue;
        }
        if (const auto material = ref(prim, "material", out_.materials, where)) info.material = static_cast<int>(*material);
        const auto& attrs = prim["attributes"];
        const auto* position = attribute(attrs, "POSITION", where + "/attributes");
        if (!attrs.contains("POSITION")) {
          warn("EmptyGeometry", "the primitive has no POSITION attribute and draws nothing", where);
          mesh.primitives.push_back(info);
          continue;
        }
        if (!position) {
          mesh.primitives.push_back(info);
          continue;  // reported with the accessor
        }
        if (position->type != "VEC3" || position->component != 5126) {
          error("JsonInvalid", "POSITION must be a float VEC3", where + "/attributes/POSITION");
          mesh.primitives.push_back(info);
          continue;
        }
        info.vertices = position->count;
        bool valid = true;
        for (const auto& [name, value] : attrs.items()) {
          const auto* acc = attribute(attrs, name.c_str(), where + "/attributes");
          if (!acc) {
            valid = false;
            continue;
          }
          if (acc->count != position->count) {
            error("JsonInvalid", "attribute " + name + " has " + std::to_string(acc->count) + " entries, POSITION " +
                                     std::to_string(position->count), where + "/attributes/" + name);
            valid = false;
          }
        }
        info.normals = attrs.contains("NORMAL");
        info.uv0 = attrs.contains("TEXCOORD_0");
        info.skinned = attrs.contains("JOINTS_0") && attrs.contains("WEIGHTS_0");
        if (prim.contains("targets") && prim["targets"].is_array()) {
          info.morph_targets = prim["targets"].size();
          for (std::size_t t = 0; t < prim["targets"].size(); ++t) {
            const auto& target = prim["targets"][t];
            if (!target.is_object()) continue;
            for (const auto& [name, value] : target.items()) {
              const auto* acc = attribute(target, name.c_str(), where + "/targets/" + std::to_string(t));
              if (acc && acc->count != position->count) {
                error("JsonInvalid", "morph target attribute count differs from POSITION", where + "/targets/" + std::to_string(t));
                valid = false;
              }
            }
          }
        }
        if (prim.contains("indices")) {
          const auto index = ref(prim, "indices", accessors_.size(), where);
          if (!index || !accessors_[*index].ok) {
            valid = false;
          } else {
            const auto& acc = accessors_[*index];
            if (acc.type != "SCALAR" || (acc.component != 5121 && acc.component != 5123 && acc.component != 5125)) {
              error("JsonInvalid", "indices must be unsigned integer scalars", where + "/indices");
              valid = false;
            } else if (!acc.has_view) {
              error("BufferMissing", "the index accessor has no data", where + "/indices");
              valid = false;
            } else {
              info.indices = acc.count;
              for (std::uint64_t k = 0; k < acc.count; ++k) {
                if ((k & 0xFFFFF) == 0 && stop()) return;
                if (index_at(acc, k) >= position->count) {
                  error("IndexOutOfRange", "index " + std::to_string(index_at(acc, k)) + " at position " + std::to_string(k) +
                                               " is not below the vertex count " + std::to_string(position->count),
                        where + "/indices");
                  valid = false;
                  break;
                }
              }
              ++checked_index_sets_;
            }
          }
        }
        const auto n = info.indices ? info.indices : info.vertices;
        if (info.mode == 4) {
          if (n % 3 != 0) warn("JsonInvalid", "triangle list size is not a multiple of 3", where);
          info.triangles = n / 3;
        } else if (info.mode == 5 || info.mode == 6) {
          info.triangles = n >= 3 ? n - 2 : 0;
        }
        if (position->min && position->max && finite_numbers(*position->min, 3) && finite_numbers(*position->max, 3)) {
          std::array<double, 6> b{};
          for (int c = 0; c < 3; ++c) {
            b[static_cast<std::size_t>(c)] = (*position->min)[static_cast<std::size_t>(c)].get<double>();
            b[static_cast<std::size_t>(c) + 3] = (*position->max)[static_cast<std::size_t>(c)].get<double>();
          }
          if (!out_.position_bounds) {
            out_.position_bounds = b;
          } else {
            for (int c = 0; c < 3; ++c) {
              auto& lo = (*out_.position_bounds)[static_cast<std::size_t>(c)];
              auto& hi = (*out_.position_bounds)[static_cast<std::size_t>(c) + 3];
              lo = std::min(lo, b[static_cast<std::size_t>(c)]);
              hi = std::max(hi, b[static_cast<std::size_t>(c) + 3]);
            }
          }
        } else {
          warn("PositionBoundsMissing", "POSITION has no valid min/max (required by glTF)", where + "/attributes/POSITION");
        }
        out_.morph_targets += info.morph_targets;
        if (valid) {
          out_.vertices += info.vertices;
          out_.indices += info.indices;
          out_.triangles += info.triangles;
          ++valid_primitives_;
          if (info.indices) ++indexed_primitives_;
        }
        mesh.primitives.push_back(info);
      }
      out_.mesh_list.push_back(std::move(mesh));
    }
  }

  void nodes_and_scenes() {
    const auto& nodes = items("nodes");
    std::vector<long long> parent(nodes.size(), -1);
    bool hierarchy_ok = true;
    // Index stored in the node list only when it references an existing object.
    const auto valid_index = [](const Json& n, const char* key, std::size_t count) {
      if (!n.contains(key) || !n[key].is_number_integer()) return -1;
      const auto v = n[key].get<long long>();
      return v >= 0 && static_cast<std::size_t>(v) < count ? static_cast<int>(v) : -1;
    };
    out_.node_list.resize(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const auto where = "/nodes/" + std::to_string(i);
      const auto& n = nodes[i];
      ref(n, "mesh", out_.meshes, where);
      ref(n, "skin", out_.skins, where);
      ref(n, "camera", out_.cameras, where);
      auto& info = out_.node_list[i];
      info.name = n.value("name", std::string());
      info.mesh = valid_index(n, "mesh", out_.meshes);
      info.skin = valid_index(n, "skin", out_.skins);
      if (n.contains("children") && n["children"].is_array()) info.children = n["children"].size();
      if (n.contains("matrix") && !finite_numbers(n["matrix"], 16)) error("JsonInvalid", "matrix needs 16 finite numbers", where);
      if (n.contains("translation") && !finite_numbers(n["translation"], 3)) error("JsonInvalid", "invalid translation", where);
      if (n.contains("rotation") && !finite_numbers(n["rotation"], 4)) error("JsonInvalid", "invalid rotation", where);
      if (n.contains("scale") && !finite_numbers(n["scale"], 3)) error("JsonInvalid", "invalid scale", where);
      if (!n.contains("children")) continue;
      if (!n["children"].is_array()) {
        error("JsonInvalid", "children must be an array", where);
        hierarchy_ok = false;
        continue;
      }
      for (const auto& c : n["children"]) {
        if (!c.is_number_integer() || c.get<long long>() < 0 || static_cast<std::size_t>(c.get<long long>()) >= nodes.size()) {
          error("InvalidReference", "a child does not reference an existing node", where + "/children");
          hierarchy_ok = false;
          continue;
        }
        const auto child = static_cast<std::size_t>(c.get<long long>());
        if (child == i || parent[child] != -1) {
          error("NodeHierarchyInvalid", "node " + std::to_string(child) + " has more than one parent or is its own child", where);
          hierarchy_ok = false;
          continue;
        }
        parent[child] = static_cast<long long>(i);
      }
    }
    // With at most one parent each, the hierarchy is a forest unless following parents loops.
    if (hierarchy_ok) {
      std::vector<unsigned char> state(nodes.size(), 0);  // 0 new, 1 on the current path, 2 done
      for (std::size_t i = 0; i < nodes.size() && hierarchy_ok; ++i) {
        std::vector<std::size_t> path;
        auto n = static_cast<long long>(i);
        while (n != -1 && state[static_cast<std::size_t>(n)] == 0) {
          state[static_cast<std::size_t>(n)] = 1;
          path.push_back(static_cast<std::size_t>(n));
          n = parent[static_cast<std::size_t>(n)];
        }
        if (n != -1 && state[static_cast<std::size_t>(n)] == 1) {
          error("NodeHierarchyInvalid", "the node hierarchy contains a cycle through node " + std::to_string(n), "/nodes");
          hierarchy_ok = false;
        }
        for (const auto p : path) state[p] = 2;
      }
    }
    const auto& scenes = items("scenes");
    for (std::size_t s = 0; s < scenes.size(); ++s) {
      const auto where = "/scenes/" + std::to_string(s);
      if (!scenes[s].contains("nodes")) continue;
      if (!scenes[s]["nodes"].is_array()) {
        error("JsonInvalid", "scene nodes must be an array", where);
        continue;
      }
      for (const auto& n : scenes[s]["nodes"]) {
        if (!n.is_number_integer() || n.get<long long>() < 0 || static_cast<std::size_t>(n.get<long long>()) >= nodes.size()) {
          error("InvalidReference", "a scene node does not exist", where + "/nodes");
        } else if (parent[static_cast<std::size_t>(n.get<long long>())] != -1) {
          warn("JsonInvalid", "a scene lists a node that is not a root", where + "/nodes");
        }
      }
    }
    if (root_.contains("scene")) ref(root_, "scene", out_.scenes, "");
    for (std::size_t i = 0; i < nodes.size(); ++i) out_.node_list[i].parent = static_cast<int>(parent[i]);
    if (hierarchy_ok) mesh_nodes_in_order(parent);
    const bool any_mesh_node = std::any_of(nodes.begin(), nodes.end(), [](const Json& n) { return n.contains("mesh"); });
    if (out_.meshes > 0 && !any_mesh_node) warn("MeshNotInstanced", "no node uses a mesh: nothing may be drawn", "/nodes");
  }

  // Depth-first walk of the default scene (iterative: a 200k-node chain must not overflow the
  // stack). Only called on a validated forest, so every child index is in range and unique.
  void mesh_nodes_in_order(const std::vector<long long>& parent) {
    const auto& nodes = items("nodes");
    const auto& scenes = items("scenes");
    std::vector<std::size_t> roots;
    const Json* scene = nullptr;
    if (root_.contains("scene") && root_["scene"].is_number_integer()) {
      const auto s = root_["scene"].get<long long>();
      if (s >= 0 && static_cast<std::size_t>(s) < scenes.size()) scene = &scenes[static_cast<std::size_t>(s)];
    } else if (!scenes.empty()) {
      scene = &scenes[0];
    }
    if (scene) {
      if (scene->contains("nodes") && (*scene)["nodes"].is_array()) {
        for (const auto& n : (*scene)["nodes"]) {
          if (!n.is_number_integer() || n.get<long long>() < 0) continue;
          const auto i = static_cast<std::size_t>(n.get<long long>());
          if (i < nodes.size() && parent[i] == -1) roots.push_back(i);
        }
      }
    } else if (scenes.empty()) {
      for (std::size_t i = 0; i < nodes.size(); ++i)
        if (parent[i] == -1) roots.push_back(i);
    }
    std::vector<unsigned char> seen(nodes.size(), 0);
    std::vector<std::size_t> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
      const auto i = stack.back();
      stack.pop_back();
      if (seen[i]) continue;  // a scene may list the same root twice
      seen[i] = 1;
      if (out_.node_list[i].mesh >= 0) out_.mesh_nodes_in_scene_order.push_back(i);
      const auto& n = nodes[i];
      if (!n.contains("children")) continue;
      const auto& children = n["children"];
      for (auto c = children.rbegin(); c != children.rend(); ++c) stack.push_back(static_cast<std::size_t>(c->get<long long>()));
    }
  }

  void skins() {
    const auto& list = items("skins");
    const auto& nodes = items("nodes");
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/skins/" + std::to_string(i);
      Skin skin;
      skin.name = list[i].value("name", std::string());
      const auto joints = list[i].find("joints");
      if (joints == list[i].end() || !joints->is_array() || joints->empty()) {
        error("JsonInvalid", "a skin needs joints", where);
        out_.skin_list.push_back(std::move(skin));
        continue;
      }
      for (const auto& j : *joints) {
        if (!j.is_number_integer() || j.get<long long>() < 0 || static_cast<std::size_t>(j.get<long long>()) >= nodes.size()) {
          error("InvalidReference", "a joint does not reference an existing node", where + "/joints");
          continue;
        }
        skin.joints.push_back(nodes[static_cast<std::size_t>(j.get<long long>())].value("name", std::string()));
      }
      ref(list[i], "skeleton", nodes.size(), where);
      if (const auto ibm = ref(list[i], "inverseBindMatrices", accessors_.size(), where); ibm && accessors_[*ibm].ok) {
        const auto& acc = accessors_[*ibm];
        if (acc.type != "MAT4" || acc.component != 5126 || acc.count < joints->size()) {
          error("JsonInvalid", "inverseBindMatrices must be float MAT4 with one matrix per joint", where);
        }
      }
      out_.joints += skin.joints.size();
      out_.skin_list.push_back(std::move(skin));
    }
  }

  void animations() {
    const auto& list = items("animations");
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto where = "/animations/" + std::to_string(i);
      Animation animation;
      animation.name = list[i].value("name", std::string());
      const auto samplers = list[i].value("samplers", Json::array());
      const auto channels = list[i].value("channels", Json::array());
      if (!samplers.is_array() || !channels.is_array()) {
        error("JsonInvalid", "animation samplers/channels must be arrays", where);
        continue;
      }
      for (std::size_t s = 0; s < samplers.size(); ++s) {
        if (!samplers[s].is_object()) continue;
        ref(samplers[s], "input", accessors_.size(), where + "/samplers/" + std::to_string(s), true);
        ref(samplers[s], "output", accessors_.size(), where + "/samplers/" + std::to_string(s), true);
      }
      for (std::size_t c = 0; c < channels.size(); ++c) {
        if (!channels[c].is_object()) continue;
        const auto cw = where + "/channels/" + std::to_string(c);
        ref(channels[c], "sampler", samplers.size(), cw, true);
        if (channels[c].contains("target") && channels[c]["target"].is_object()) {
          ref(channels[c]["target"], "node", out_.nodes, cw + "/target");
        }
      }
      animation.channels = channels.size();
      out_.animation_list.push_back(std::move(animation));
    }
  }

  void totals() {
    out_.indices_checked = valid_primitives_ > 0 && checked_index_sets_ >= indexed_primitives_;
    if (valid_primitives_ == 0 && std::none_of(out_.issues.begin(), out_.issues.end(), [](const Issue& i) { return i.severity == "error"; })) {
      error("EmptyGeometry", "no primitive with valid vertex positions: there is nothing to draw", "/meshes");
    }
    if (out_.vertices > limits_.max_vertices || out_.triangles > limits_.max_triangles) {
      error("ModelTooLarge", std::to_string(out_.vertices) + " vertices / " + std::to_string(out_.triangles) +
                                 " triangles exceed the preview limits", "/meshes");
    }
  }

  Inspection& out_;
  const ResourceReader& read_;
  const Limits& limits_;
  const std::function<bool()>& cancelled_;
  Json root_;
  std::vector<Buffer> buffers_;
  std::vector<View> views_;
  std::vector<Accessor> accessors_;
  std::vector<std::unique_ptr<std::string>> owned_;
  std::uint64_t resource_bytes_ = 0;
  std::size_t valid_primitives_ = 0, indexed_primitives_ = 0, checked_index_sets_ = 0;
  bool cancelled_reported_ = false;
};

}  // namespace

bool Inspection::loadable() const {
  return std::none_of(issues.begin(), issues.end(), [](const Issue& i) { return i.severity == "error"; });
}

std::optional<std::string> safe_relative_uri(std::string_view uri) {
  std::string decoded;
  for (std::size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] == '%') {
      if (i + 2 >= uri.size() || !std::isxdigit(static_cast<unsigned char>(uri[i + 1])) ||
          !std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
        return std::nullopt;
      }
      decoded.push_back(static_cast<char>(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16)));
      i += 2;
    } else {
      decoded.push_back(uri[i]);
    }
  }
  if (decoded.empty() || decoded.size() > 1024) return std::nullopt;
  // Absolute, rooted, drive-letter, scheme, UNC and backslash forms are all refused; so is any NUL.
  if (decoded.front() == '/' || decoded.find('\\') != std::string::npos || decoded.find(':') != std::string::npos ||
      decoded.find('\0') != std::string::npos || decoded.find('?') != std::string::npos || decoded.find('#') != std::string::npos) {
    return std::nullopt;
  }
  std::string out;
  std::size_t start = 0;
  while (start <= decoded.size()) {
    const auto end = std::min(decoded.find('/', start), decoded.size());
    const auto segment = decoded.substr(start, end - start);
    if (segment.empty() || segment == "..") return std::nullopt;
    if (segment != ".") {
      if (!out.empty()) out.push_back('/');
      out += segment;
    }
    start = end + 1;
  }
  if (out.empty() || !text::is_valid_utf8(out)) return std::nullopt;
  return out;
}

Inspection inspect(std::string_view bytes, bool glb, const ResourceReader& read, const Limits& limits,
                   const std::function<bool()>& cancelled) {
  Inspection out;
  out.container = glb ? "glb" : "gltf";
  out.file_bytes = bytes.size();
  if (bytes.size() > limits.max_file_bytes) {
    out.issues.push_back({"error", "ModelTooLarge", "the file has " + std::to_string(bytes.size()) + " bytes", ""});
    return out;
  }
  Checker checker(out, read, limits, cancelled);
  if (!glb) {
    checker.run(bytes, std::nullopt);
    return out;
  }
  // GLB: 12-byte header, then chunks (JSON first, optional BIN second), 4-byte aligned.
  if (bytes.size() < 20) {
    out.issues.push_back({"error", bytes.size() >= 4 && le32(bytes.data()) == kGlbMagic ? "GlbTruncated" : "NotGltf",
                          "the file is too short to be a GLB", ""});
    return out;
  }
  if (le32(bytes.data()) != kGlbMagic) {
    out.issues.push_back({"error", "NotGltf", "the file does not start with the GLB magic 'glTF'", ""});
    return out;
  }
  if (le32(bytes.data() + 4) != 2) {
    out.issues.push_back({"error", "UnsupportedVersion", "GLB container version " + std::to_string(le32(bytes.data() + 4)), ""});
    return out;
  }
  const std::uint64_t length = le32(bytes.data() + 8);
  if (length > bytes.size()) {
    out.issues.push_back({"error", "GlbTruncated", "the header declares " + std::to_string(length) + " bytes but the file has " +
                                                       std::to_string(bytes.size()), ""});
    return out;
  }
  if (length < bytes.size()) {
    out.issues.push_back({"warning", "GlbTrailingData", std::to_string(bytes.size() - length) + " bytes after the declared end", ""});
  }
  std::optional<std::string_view> json, bin;
  std::uint64_t offset = 12;
  int chunk = 0;
  while (offset < length) {
    if (length - offset < 8) {
      out.issues.push_back({"error", "GlbTruncated", "incomplete chunk header at byte " + std::to_string(offset), ""});
      return out;
    }
    const std::uint64_t chunk_length = le32(bytes.data() + offset);
    const auto type = le32(bytes.data() + offset + 4);
    if (chunk_length > length - offset - 8) {
      out.issues.push_back({"error", "GlbTruncated", "chunk " + std::to_string(chunk) + " ends after the file", ""});
      return out;
    }
    if (chunk_length % 4 != 0) {
      out.issues.push_back({"warning", "GlbMisaligned", "chunk " + std::to_string(chunk) + " length is not a multiple of 4", ""});
    }
    const auto data = bytes.substr(static_cast<std::size_t>(offset + 8), static_cast<std::size_t>(chunk_length));
    if (chunk == 0) {
      if (type != kChunkJson) {
        out.issues.push_back({"error", "NotGltf", "the first GLB chunk is not JSON", ""});
        return out;
      }
      json = data;
    } else if (chunk == 1 && type == kChunkBin) {
      bin = data;
    }  // other chunks are ignored, as the specification requires
    offset += 8 + chunk_length;
    ++chunk;
  }
  if (!json) {
    out.issues.push_back({"error", "NotGltf", "the GLB has no JSON chunk", ""});
    return out;
  }
  checker.run(*json, bin);
  return out;
}

LoadedAsset load_and_inspect(const std::filesystem::path& file, const Limits& limits, const std::function<bool()>& cancelled) {
  LoadedAsset asset;
  auto fail = [&](std::string code, std::string message) {
    asset.inspection.issues.push_back({"error", std::move(code), std::move(message), ""});
    return asset;
  };
  const auto extension = text::to_lower_ascii(text::path_to_utf8(file.extension()));
  if (extension != ".glb" && extension != ".gltf") return fail("NotGltf", "only .glb and .gltf files are validated");
  asset.inspection.container = extension == ".glb" ? "glb" : "gltf";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) return fail("FileUnreadable", "the file does not exist or is not a regular file");
  const auto size = std::filesystem::file_size(file, ec);
  if (ec) return fail("FileUnreadable", "the file size cannot be read");
  asset.inspection.file_bytes = size;
  if (size > limits.max_file_bytes) return fail("ModelTooLarge", "the file has " + std::to_string(size) + " bytes");

  auto read_file = [&](const std::filesystem::path& path, std::uint64_t limit) -> std::optional<std::string> {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const auto n = static_cast<std::uint64_t>(in.tellg());
    if (n > limit) return std::nullopt;
    in.seekg(0);
    std::string bytes(static_cast<std::size_t>(n), '\0');
    if (n && !in.read(bytes.data(), static_cast<std::streamsize>(n))) return std::nullopt;
    return bytes;
  };
  auto main = read_file(file, limits.max_file_bytes);
  if (!main) return fail("FileUnreadable", "the file cannot be read");
  asset.main_bytes = std::move(*main);

  // Resources: regular files below the asset folder only. The canonical path must stay inside the
  // folder, which also rejects symbolic links and junctions pointing elsewhere.
  const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(file, ec).parent_path(), ec);
  ResourceReader reader = [&](const std::string& relative) -> std::optional<std::string> {
    std::error_code e;
    const auto candidate = base / text::path_from_utf8(relative);
    if (std::filesystem::is_symlink(candidate, e) || !std::filesystem::is_regular_file(candidate, e)) return std::nullopt;
    const auto resolved = std::filesystem::weakly_canonical(candidate, e);
    if (e) return std::nullopt;
    const auto rel = resolved.lexically_relative(base);
    if (rel.empty() || rel.native().starts_with(L"..")) return std::nullopt;
    auto bytes = read_file(resolved, limits.max_resource_bytes);
    if (bytes) asset.resources.emplace_back(relative, *bytes);
    return bytes;
  };
  asset.inspection = inspect(asset.main_bytes, asset.inspection.container == "glb", reader, limits, cancelled);
  return asset;
}

}  // namespace sns::gltf
