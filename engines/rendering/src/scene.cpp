#include "sns/rendering/scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "sns/assets/asset_resolver.hpp"
#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::rendering {
namespace {

using nif::Vec3;

void issue(CharacterScene& scene, std::string severity, std::string code, std::string message, std::string subject) {
  scene.issues.push_back({std::move(severity), std::move(code), std::move(message), std::move(subject)});
}

std::optional<Rgb> rgb_from(const nlohmann::json& j, const char* key) {
  if (!j.contains(key) || !j[key].is_object()) return std::nullopt;
  const auto& c = j[key];
  return Rgb{c.value("r", 255) / 255.0f, c.value("g", 255) / 255.0f, c.value("b", 255) / 255.0f};
}

Vec3 normalized(Vec3 v) {
  const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return len > 1e-8f ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{0, 0, 1};
}

std::optional<nif::MeshFile> load(const assets::AssetResolver& resolver, const std::string& path, CharacterScene& scene,
                                  const std::string& subject) {
  try {
    auto bytes = resolver.read(path);
    if (!bytes) {
      issue(scene, "warning", "MeshNotFound", "Mesh not found as loose file or in any loaded BSA: " + path, subject);
      return std::nullopt;
    }
    auto mesh = nif::load_mesh(*bytes, path);
    for (const auto& w : mesh.warnings) issue(scene, "info", "MeshWarning", w, path);
    return mesh;
  } catch (const Error& e) {
    issue(scene, "warning", "MeshUnreadable", e.what(), subject);
    return std::nullopt;
  }
}

std::string describe(const std::optional<assets::AssetLocation>& loc) {
  if (!loc) return "missing";
  if (loc->kind == assets::AssetLocation::Kind::Loose) return "loose:" + loc->provider;
  return "bsa:" + loc->provider + (loc->archive_provider.empty() ? "" : " (" + loc->archive_provider + ")");
}

RenderPart to_part(const nif::Shape& shape, std::string id, std::string group, std::string label) {
  RenderPart part;
  part.id = std::move(id);
  part.group = std::move(group);
  part.label = std::move(label);
  part.shape_name = shape.name;
  part.shader_type = shape.shader_type;
  part.double_sided = shape.double_sided;
  part.skinned = shape.skinned;
  part.alpha_blend = shape.alpha_blend;
  part.alpha_test = shape.alpha_test;
  part.alpha_flags = shape.alpha_flags;
  part.alpha_cutoff = shape.alpha_threshold / 255.0f;
  part.original_diffuse = shape.textures[nif::Diffuse];
  part.diffuse = part.original_diffuse;
  part.model_space_normals = shape.model_space_normals;
  // Qt's normalMap is tangent-space only; model-space (_msn) maps would light the mesh wrongly.
  if (!shape.model_space_normals) part.normal = shape.textures[nif::Normal];
  if (shape.shader_type == nif::kShaderFaceTint) part.detail = shape.textures[nif::Parallax];
  part.vertex_colors = shape.vertex_colors && shape.colors.size() == shape.positions.size();
  part.parent_node = shape.parent_node;
  part.to_root = shape.to_root;
  part.tangents_swapped = shape.tangents_swapped;
  part.vertex_count = shape.positions.size();
  part.bone_count = shape.bones.size();
  part.shader_block = shape.shader_block;
  part.shader_flags1 = shape.shader_flags1;
  part.shader_flags2 = shape.shader_flags2;
  part.material_alpha = shape.material_alpha;
  part.uv_offset = shape.uv_offset;
  part.uv_scale = shape.uv_scale;
  part.texture_slots = shape.textures;

  const auto n = shape.positions.size();
  part.vertices.resize(n * kFloatsPerVertex);
  constexpr float inf = std::numeric_limits<float>::infinity();
  part.min = {inf, inf, inf};
  part.max = {-inf, -inf, -inf};
  for (std::size_t i = 0; i < n; ++i) {
    float* v = part.vertices.data() + i * kFloatsPerVertex;
    const Vec3 p = to_view_space(shape.positions[i]);
    const Vec3 nrm = i < shape.normals.size() ? to_view_space(normalized(shape.normals[i])) : Vec3{0, 1, 0};
    const Vec3 tan = i < shape.tangents.size() ? to_view_space(normalized(shape.tangents[i])) : Vec3{1, 0, 0};
    const Vec3 bin = i < shape.bitangents.size() ? to_view_space(normalized(shape.bitangents[i])) : Vec3{0, 0, 1};
    // Skyrim's lighting shader samples at uv * uvScale + uvOffset (BSShaderProperty).
    const nif::Vec2 raw = i < shape.uvs.size() ? shape.uvs[i] : nif::Vec2{};
    const nif::Vec2 uv{raw.u * shape.uv_scale.u + shape.uv_offset.u, raw.v * shape.uv_scale.v + shape.uv_offset.v};
    const auto color = part.vertex_colors ? shape.colors[i] : std::array<float, 4>{1, 1, 1, 1};
    const float data[kFloatsPerVertex] = {p.x,   p.y,   p.z,   nrm.x, nrm.y, nrm.z,    uv.u,     uv.v,     tan.x,
                                          tan.y, tan.z, bin.x, bin.y, bin.z, color[0], color[1], color[2], color[3]};
    std::copy(std::begin(data), std::end(data), v);
    part.min = {std::min(part.min[0], p.x), std::min(part.min[1], p.y), std::min(part.min[2], p.z)};
    part.max = {std::max(part.max[0], p.x), std::max(part.max[1], p.y), std::max(part.max[2], p.z)};
  }
  part.indices = shape.indices;

  if (!part.original_diffuse.empty()) {
    if (shape.uvs.size() != n) {
      part.warnings.push_back("textured shape without UV coordinates");
    } else {
      double area = 0;
      for (std::size_t t = 0; t + 2 < shape.indices.size(); t += 3) {
        const auto& a = shape.uvs[shape.indices[t]];
        const auto& b = shape.uvs[shape.indices[t + 1]];
        const auto& c = shape.uvs[shape.indices[t + 2]];
        area += std::abs((b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v));
      }
      part.degenerate_uvs = area < 1e-9;
      if (part.degenerate_uvs) part.warnings.push_back("UV coordinates are degenerate (every triangle has zero UV area)");
    }
  }
  if (shape.model_space_normals && !shape.textures[nif::Normal].empty()) {
    part.warnings.push_back("model-space normal map (_msn) is not reproduced: vertex normals are used");
  }
  return part;
}

float bilinear(const textures::DdsImage& img, float u, float v, int channel) {
  const float x = std::clamp(u * static_cast<float>(img.width) - 0.5f, 0.0f, static_cast<float>(img.width - 1));
  const float y = std::clamp(v * static_cast<float>(img.height) - 0.5f, 0.0f, static_cast<float>(img.height - 1));
  const auto x0 = static_cast<std::uint32_t>(x), y0 = static_cast<std::uint32_t>(y);
  const auto x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
  const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
  const auto* px = reinterpret_cast<const std::uint8_t*>(img.data.data());
  auto at = [&](std::uint32_t xx, std::uint32_t yy) { return px[(static_cast<std::size_t>(yy) * img.width + xx) * 4 + channel] / 255.0f; };
  return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
}

}  // namespace

Appearance appearance_from_json(const nlohmann::json& j) {
  Appearance a;
  a.npc_form_key = j.value("npcFormKey", "");
  a.female = j.value("sex", "male") == "female";
  a.weight = j.value("weight", 50.0f);
  a.height = j.value("height", 1.0f);
  a.race_height = j.value("raceHeight", 1.0f);
  if (!std::isfinite(a.weight) || !std::isfinite(a.height) || !std::isfinite(a.race_height) ||
      a.weight < 0 || a.weight > 100 || a.height <= 0 || a.race_height <= 0 || a.height > 10 || a.race_height > 10)
    throw Error(ErrorKind::Parse, "NPC weight or height is invalid for rendering");
  a.skeleton = j.value("skeletonPath", "");
  a.face_geometry = j.value("faceGeometryPath", "");
  a.face_tint = j.value("faceTintPath", "");
  a.hair_color = rgb_from(j, "hairColor");
  a.skin_tone = rgb_from(j, "skinTone");
  for (const auto& h : j.value("headParts", nlohmann::json::array())) {
    a.head_parts.push_back({h.value("editorId", ""), h.value("type", "")});
  }
  for (const auto& p : j.value("pieces", nlohmann::json::array())) {
    AppearancePiece piece{p.value("armorFormKey", ""), p.value("name", ""), p.value("source", "outfit"),
                          p.value("slotMask", 0u), {}};
    for (const auto& addon : p.value("addons", nlohmann::json::array())) {
      piece.addons.push_back({addon.value("formKey", ""), addon.value("meshPath", ""), addon.value("weightSlider", false),
                              addon.value("slotMask", 0u)});
    }
    a.pieces.push_back(std::move(piece));
  }
  return a;
}

Vec3 to_view_space(const Vec3& s) { return {-s.x, s.z, s.y}; }

std::string weight_variant(std::string_view mesh_path, int variant) {
  const auto lower = text::to_lower_ascii(mesh_path);
  for (const char* suffix : {"_0.nif", "_1.nif"}) {
    if (lower.ends_with(suffix)) {
      std::string out(mesh_path);
      out[out.size() - 5] = variant == 0 ? '0' : '1';
      return out;
    }
  }
  return {};
}

bool blend_by_weight(nif::Shape& heavy, const nif::Shape& thin, float weight) {
  if (heavy.positions.size() != thin.positions.size() || heavy.indices != thin.indices) return false;
  const float t = std::clamp(weight, 0.0f, 100.0f) / 100.0f;
  for (std::size_t i = 0; i < heavy.positions.size(); ++i) {
    auto& h = heavy.positions[i];
    const auto& l = thin.positions[i];
    h = {l.x + (h.x - l.x) * t, l.y + (h.y - l.y) * t, l.z + (h.z - l.z) * t};
  }
  if (heavy.normals.size() == thin.normals.size()) {
    for (std::size_t i = 0; i < heavy.normals.size(); ++i) {
      auto& h = heavy.normals[i];
      const auto& l = thin.normals[i];
      h = normalized({l.x + (h.x - l.x) * t, l.y + (h.y - l.y) * t, l.z + (h.z - l.z) * t});
    }
  }
  return true;
}

SkinningReport apply_skinning(nif::Shape& shape, const nif::Skeleton& skeleton) {
  SkinningReport report;
  if (!shape.skinned || shape.bones.empty() || shape.influences.size() != shape.positions.size()) return report;

  // Per bone: skin space -> global space through the bone; the race skeleton first, then the
  // NIF's own node for bones the skeleton does not have (physics/extra bones).
  std::vector<std::optional<nif::Transform>> skin_to_global(shape.bones.size());
  for (std::size_t b = 0; b < shape.bones.size(); ++b) {
    if (b >= shape.skin_to_bone.size()) {
      report.missing.push_back(shape.bones[b]);
      continue;
    }
    if (const int index = skeleton.index_of(shape.bones[b]); index >= 0) {
      skin_to_global[b] = skeleton.bones[static_cast<std::size_t>(index)].to_global.compose(shape.skin_to_bone[b]);
    } else if (b < shape.mesh_bone_to_global.size() && shape.mesh_bone_to_global[b]) {
      skin_to_global[b] = shape.mesh_bone_to_global[b]->compose(shape.skin_to_bone[b]);
      report.from_mesh.push_back(shape.bones[b]);
    } else {
      report.missing.push_back(shape.bones[b]);
    }
  }

  for (std::size_t v = 0; v < shape.positions.size(); ++v) {
    const Vec3 bind = shape.positions[v];
    const Vec3 bind_n = v < shape.normals.size() ? shape.normals[v] : Vec3{0, 0, 1};
    const Vec3 bind_t = v < shape.tangents.size() ? shape.tangents[v] : Vec3{};
    const Vec3 bind_b = v < shape.bitangents.size() ? shape.bitangents[v] : Vec3{};
    const Vec3 skin = shape.global_to_skin.apply(bind);
    const Vec3 skin_n = shape.global_to_skin.apply_dir(bind_n);
    const Vec3 skin_t = shape.global_to_skin.apply_dir(bind_t);
    const Vec3 skin_b = shape.global_to_skin.apply_dir(bind_b);
    Vec3 p{}, n{}, t{}, bit{};
    float total = 0;
    const auto& inf = shape.influences[v];
    for (std::size_t k = 0; k < 4; ++k) {
      const float w = inf.weight[k];
      const auto bone = inf.bone[k];
      if (w <= 0 || bone >= skin_to_global.size()) continue;
      Vec3 q = bind, m = bind_n, tg = bind_t, bn = bind_b;  // unresolved bone: keep the bind pose
      if (skin_to_global[bone]) {
        const auto& xf = *skin_to_global[bone];
        q = xf.apply(skin);
        m = xf.apply_dir(skin_n);
        tg = xf.apply_dir(skin_t);
        bn = xf.apply_dir(skin_b);
      }
      p = {p.x + q.x * w, p.y + q.y * w, p.z + q.z * w};
      n = {n.x + m.x * w, n.y + m.y * w, n.z + m.z * w};
      t = {t.x + tg.x * w, t.y + tg.y * w, t.z + tg.z * w};
      bit = {bit.x + bn.x * w, bit.y + bn.y * w, bit.z + bn.z * w};
      total += w;
    }
    if (total > 1e-6f) {
      shape.positions[v] = {p.x / total, p.y / total, p.z / total};
      if (v < shape.normals.size()) shape.normals[v] = normalized(n);
      if (v < shape.tangents.size()) shape.tangents[v] = normalized(t);
      if (v < shape.bitangents.size()) shape.bitangents[v] = normalized(bit);
    }
  }
  return report;
}

std::string_view to_string(AlphaMode mode) {
  switch (mode) {
    case AlphaMode::Opaque: return "opaque";
    case AlphaMode::Mask: return "mask";
    case AlphaMode::Blend: return "blend";
    case AlphaMode::BlendWithDepthPrepass: return "blendPrepass";
  }
  return "opaque";
}

AlphaTranslation translate_alpha(bool blend, bool test, std::uint8_t threshold, bool texture_alpha, bool vertex_alpha) {
  const float cutoff = threshold / 255.0f;
  const auto thr = std::to_string(threshold);
  if (!blend && !test) return {AlphaMode::Opaque, cutoff, "no NiAlphaProperty blend/test"};
  if (!texture_alpha && !vertex_alpha) {
    return {AlphaMode::Opaque, cutoff, "NiAlphaProperty set, but no texel or vertex carries alpha (< 250)"};
  }
  if (test && !blend) return {AlphaMode::Mask, cutoff, "alpha test, threshold " + thr};
  if (blend && !test) return {AlphaMode::Blend, cutoff, "alpha blend (no test)"};
  return {AlphaMode::BlendWithDepthPrepass, cutoff,
          "alpha blend + test (threshold " + thr + "): depth for opaque texels, blended edges"};
}

textures::DdsImage apply_face_tint(const textures::DdsImage& base, const textures::DdsImage& tint,
                                   const textures::DdsImage* detail) {
  if (base.format != textures::TextureFormat::RGBA8 || tint.format != textures::TextureFormat::RGBA8 ||
      (detail && detail->format != textures::TextureFormat::RGBA8)) {
    throw Error(ErrorKind::InvalidArgument, "apply_face_tint needs decoded RGBA8 images");
  }
  for (const auto* img : {&base, &tint, detail}) {
    if (img && (img->width == 0 || img->height == 0 || img->data.size() != 4ull * img->width * img->height))
      throw Error(ErrorKind::InvalidArgument, "apply_face_tint got an empty or truncated image");
  }
  textures::DdsImage out = base;
  auto* px = reinterpret_cast<std::uint8_t*>(out.data.data());
  constexpr float detail_offset[3] = {0.00392156886f, 0.0f, 0.00392156886f};
  for (std::uint32_t y = 0; y < base.height; ++y) {
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(base.height);
    for (std::uint32_t x = 0; x < base.width; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(base.width);
      auto* texel = px + (static_cast<std::size_t>(y) * base.width + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const float b = texel[c] / 255.0f;
        const float t = bilinear(tint, u, v, c);
        const float d = detail ? 3.984375f * (detail_offset[c] + bilinear(*detail, u, v, c)) : 1.0f;
        const float result = (b * b + 2.0f * t * b * (1.0f - b)) * d;
        texel[c] = static_cast<std::uint8_t>(std::clamp(result, 0.0f, 1.0f) * 255.0f + 0.5f);
      }
    }
  }
  return out;
}

std::vector<bool> resolve_equipped(const std::vector<std::uint32_t>& slot_masks) {
  std::vector<bool> equipped(slot_masks.size(), true);
  std::uint32_t taken = 0;
  for (std::size_t i = slot_masks.size(); i-- > 0;) {
    if (slot_masks[i] != 0 && (slot_masks[i] & taken) != 0) equipped[i] = false;
    else taken |= slot_masks[i];
  }
  return equipped;
}

bool is_covered(std::uint32_t slots, std::uint32_t covered) { return (slots & covered) != 0; }

std::vector<std::string> smp_collision_shapes(std::string_view xml) {
  std::vector<std::string> names;
  auto decode = [](std::string_view raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '&') {
        bool matched = false;
        for (const auto& [entity, ch] : {std::pair{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}}) {
          if (raw.substr(i).starts_with(entity)) {
            out += ch;
            i += std::string_view(entity).size() - 1;
            matched = true;
            break;
          }
        }
        if (matched) continue;
      }
      out += raw[i];
    }
    return out;
  };
  std::size_t at = 0;
  while ((at = xml.find('<', at)) != std::string_view::npos) {
    if (xml.substr(at).starts_with("<!--")) {  // comments may hold disabled shapes
      const auto end = xml.find("-->", at + 4);
      if (end == std::string_view::npos) break;
      at = end + 3;
      continue;
    }
    const auto close = xml.find('>', at);
    if (close == std::string_view::npos) break;
    const auto tag = xml.substr(at + 1, close - at - 1);
    at = close + 1;
    const auto head = tag.find_first_of(" \t\r\n/");
    if (head == std::string_view::npos || (tag.substr(0, head) != "per-vertex-shape" && tag.substr(0, head) != "per-triangle-shape")) continue;
    // name="..." or name='...'
    for (std::size_t p = tag.find("name", head); p != std::string_view::npos; p = tag.find("name", p + 4)) {
      if (!std::isspace(static_cast<unsigned char>(tag[p - 1]))) continue;
      auto q = p + 4;
      while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
      if (q >= tag.size() || tag[q] != '=') continue;
      ++q;
      while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
      if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) continue;
      const auto end = tag.find(tag[q], q + 1);
      if (end == std::string_view::npos) break;
      auto name = decode(tag.substr(q + 1, end - q - 1));
      if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(std::move(name));
      break;
    }
  }
  return names;
}

std::string hidden_reason(const nif::Shape& shape, const std::vector<std::string>& collision) {
  if (shape.hidden) return "NIF Hidden flag set on the shape or a parent node";
  if (!shape.has_shader) return "no shader property: the game does not draw this shape";
  const bool untextured = std::all_of(shape.textures.begin(), shape.textures.end(), [](const auto& t) { return t.empty(); });
  const bool in_xml = std::any_of(collision.begin(), collision.end(), [&](const auto& n) { return text::iequals_ascii(n, shape.name); });
  if (untextured && in_xml) return "untextured SMP collision shape (physics proxy)";
  return {};
}

CharacterScene build_character_scene(const Appearance& appearance, const assets::AssetResolver& resolver,
                                     const SceneOptions& options) {
  CharacterScene scene;
  scene.scale = appearance.height * appearance.race_height;
  auto check_cancel = [&] {
    if (options.cancelled && options.cancelled()) throw Error(ErrorKind::Cancelled, "Scene loading cancelled");
  };
  for (const auto& error : resolver.issues()) issue(scene, "warning", error.code, error.message, error.archive);

  std::optional<nif::Skeleton> skeleton;
  if (!appearance.skeleton.empty()) {
    try {
      if (auto bytes = resolver.read(appearance.skeleton)) {
        skeleton = nif::load_skeleton(*bytes, appearance.skeleton);
        scene.skeleton_bones = skeleton->bones.size();
        if (options.include_skeleton) {
          for (const auto& bone : skeleton->bones) {
            if (bone.parent < 0) continue;
            const auto a = to_view_space(bone.to_global.translation);
            const auto b = to_view_space(skeleton->bones[static_cast<std::size_t>(bone.parent)].to_global.translation);
            scene.skeleton_lines.insert(scene.skeleton_lines.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
          }
        }
      } else {
        issue(scene, "warning", "SkeletonMissing", "Skeleton resource is missing", appearance.skeleton);
      }
    } catch (const std::exception& e) {
      issue(scene, "warning", "SkeletonUnreadable", e.what(), appearance.skeleton);
    }
  }

  // ---- 1. geometry --------------------------------------------------------------------------
  auto append_mesh = [&](const std::string& path, const std::string& group, const std::string& label,
                         const std::string& armor, const std::string& addon_key, std::uint32_t mask, bool slider,
                         bool hide_hair) {
    check_cancel();
    if (path.empty()) return;
    ++scene.stats.meshes_requested;
    auto mesh = load(resolver, path, scene, label);
    if (!mesh) return;
    ++scene.stats.meshes_loaded;
    const auto location = describe(resolver.locate(path));
    std::optional<nif::MeshFile> thin;
    const auto thin_path = weight_variant(path, 0);
    if (slider && !thin_path.empty() && text::to_lower_ascii(thin_path) != text::to_lower_ascii(path)) {
      thin = load(resolver, thin_path, scene, label);
    }
    std::vector<std::string> collision;
    if (!mesh->physics_xml.empty()) {
      if (auto xml = resolver.read(mesh->physics_xml)) collision = smp_collision_shapes(*xml);
      else issue(scene, "info", "PhysicsXmlMissing", "SMP physics file named by the mesh was not found: " + mesh->physics_xml, path);
    }
    std::vector<std::string> hidden, extra_bones, missing_bones;
    for (auto& shape : mesh->shapes) {
      check_cancel();
      if (shape.indices.empty() || shape.positions.empty()) continue;
      auto reason = hidden_reason(shape, collision);
      if (reason.empty() && hide_hair &&
          (shape.shader_type == nif::kShaderHairTint || text::to_lower_ascii(shape.name).find("hair") != std::string::npos)) {
        reason = "hair under a headgear slot (31/41)";
      }
      if (!reason.empty()) {
        hidden.push_back(shape.name + " (" + reason + ")");
        ++scene.stats.hidden_shapes;
      }
      bool blended = false;
      if (thin) {
        const auto found = std::find_if(thin->shapes.begin(), thin->shapes.end(), [&](const auto& s) { return s.name == shape.name; });
        if (found != thin->shapes.end()) blended = blend_by_weight(shape, *found, appearance.weight);
        if (!blended) issue(scene, "warning", "WeightTopologyMismatch", "Weight meshes do not match; heavy mesh retained", path);
      }
      SkinningReport skinning;
      if (shape.skinned && options.apply_skinning && skeleton && !shape.bones.empty()) {
        skinning = apply_skinning(shape, *skeleton);
        for (const auto& b : skinning.from_mesh) {
          if (std::find(extra_bones.begin(), extra_bones.end(), b) == extra_bones.end()) extra_bones.push_back(b);
        }
        for (const auto& b : skinning.missing) {
          if (std::find(missing_bones.begin(), missing_bones.end(), b) == missing_bones.end()) missing_bones.push_back(b);
        }
      }
      auto part = to_part(shape, std::to_string(scene.parts.size()), group, label);
      part.armor_form_key = armor;
      part.slot_mask = mask;
      part.mesh_path = path;
      part.mesh_location = location;
      part.weight_blended = blended;
      part.skinning_applied = shape.skinned && options.apply_skinning && skeleton && skinning.missing.empty();
      part.bones_from_mesh = std::move(skinning.from_mesh);
      part.bones_missing = std::move(skinning.missing);
      part.physics_xml = mesh->physics_xml;
      part.addon_form_key = addon_key;
      if (!part.bones_missing.empty()) {
        part.warnings.push_back(std::to_string(part.bones_missing.size()) + " bones missing from skeleton and mesh (bind pose kept), e.g. " +
                                part.bones_missing.front());
      }
      if (thin && !blended) part.warnings.push_back("_0/_1 weight meshes differ: heavy mesh shown");
      if (part.original_diffuse.empty() && reason.empty()) part.warnings.push_back("no diffuse texture in the NIF: drawn untextured");
      part.shown_by_default = reason.empty();
      part.hidden_reason = std::move(reason);
      if (shape.shader_type == nif::kShaderHairTint) part.tint = appearance.hair_color;
      if (shape.shader_type == nif::kShaderSkinTint) part.tint = appearance.skin_tone;
      // FaceTint keeps its own diffuse (slot 0); the FaceGen tint is composited over it below.
      if (shape.shader_type == nif::kShaderFaceTint) part.face_tint = appearance.face_tint;
      if (part.shown_by_default) scene.stats.triangles += part.indices.size() / 3;
      scene.stats.skinned_shapes += part.skinned ? 1 : 0;
      scene.parts.push_back(std::move(part));
    }
    if (!hidden.empty()) {
      std::string names;
      for (const auto& h : hidden) names += (names.empty() ? "" : ", ") + h;
      issue(scene, "info", "ShapesHiddenByDefault",
            "Listed but not drawn, as in game (they can be shown from the inspector): " + names, path);
    }
    if (!extra_bones.empty()) {
      issue(scene, "info", "ExtraBonesFromMesh",
            std::to_string(extra_bones.size()) + " bones are not in the race skeleton but are defined by the mesh itself "
            "(e.g. " + extra_bones.front() + "; typical of SMP/physics). Their NIF reference transforms are used.", path);
    }
    if (!missing_bones.empty()) {
      scene.stats.bones_missing += static_cast<int>(missing_bones.size());
      issue(scene, "warning", "BoneMissing",
            std::to_string(missing_bones.size()) + " bones exist neither in the skeleton nor in the mesh (e.g. " +
            missing_bones.front() + "); their weights keep the bind pose.", path);
    }
  };

  // Outfit pieces that share slots: like the game, only the last one equipped stays.
  std::vector<const AppearancePiece*> outfit;
  std::vector<std::uint32_t> outfit_masks;
  for (const auto& piece : appearance.pieces) {
    if (piece.source == "outfit") {
      outfit.push_back(&piece);
      outfit_masks.push_back(piece.slot_mask);
    }
  }
  const auto equipped = resolve_equipped(outfit_masks);
  std::uint32_t covered = 0;
  for (std::size_t i = 0; i < outfit.size(); ++i) {
    if (equipped[i]) {
      covered |= outfit[i]->slot_mask;
      continue;
    }
    ++scene.stats.pieces_unequipped;
    std::string by;
    for (std::size_t j = i + 1; j < outfit.size(); ++j) {
      if (equipped[j] && (outfit[j]->slot_mask & outfit[i]->slot_mask)) by = outfit[j]->name;
    }
    issue(scene, "info", "PieceUnequipped",
          outfit[i]->name + " is not shown: " + by + " comes later in the outfit and uses the same slot "
          "(the game keeps only the last item equipped per slot)", outfit[i]->armor_form_key);
  }
  for (std::size_t i = 0; i < outfit.size(); ++i) {
    if (!equipped[i]) continue;
    for (const auto& addon : outfit[i]->addons) {
      append_mesh(addon.mesh, "outfit", outfit[i]->name, outfit[i]->armor_form_key, addon.form_key, addon.slot_mask, addon.weight_slider, false);
    }
  }
  for (const auto& piece : appearance.pieces) {
    if (piece.source != "skin") continue;
    for (const auto& addon : piece.addons) {
      if (is_covered(addon.slot_mask, covered)) continue;
      append_mesh(addon.mesh, "skin", piece.name, piece.armor_form_key, addon.form_key, addon.slot_mask, addon.weight_slider, false);
    }
  }
  append_mesh(appearance.face_geometry, "head", "FaceGen", "", "", 0, false, is_covered(kHairSlots, covered));

  // ---- 2. textures, head first, smallest adequate stored mip -----------------------------------
  struct Request {
    std::string key;
    int priority;
    bool normal;
  };
  std::vector<Request> requests;
  auto request = [&](const std::string& key, int priority, bool normal) {
    if (key.empty()) return;
    const auto it = std::find_if(requests.begin(), requests.end(), [&](const Request& r) { return r.key == key; });
    if (it == requests.end()) requests.push_back({key, priority, normal});
    else it->priority = std::min(it->priority, priority);
  };
  for (const auto& part : scene.parts) {
    const bool head = part.group == "head";
    const int later = part.shown_by_default ? 0 : 4;  // shapes the game does not draw come last
    request(part.diffuse, (head ? 0 : 1) + later, false);
    request(part.face_tint, later, false);
    request(part.detail, later, false);
    request(part.normal, (head ? 2 : 3) + later, true);
  }
  std::stable_sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) { return a.priority < b.priority; });

  std::uint64_t texture_bytes = 0;
  for (const auto& req : requests) {
    check_cancel();
    TextureInfo info;
    info.path = req.key;
    info.normal_map = req.normal;
    info.location = describe(resolver.locate(req.key));
    try {
      auto bytes = resolver.read(req.key);
      if (!bytes) throw Error(ErrorKind::NotFound, "Texture not found as loose file or in any loaded BSA");
      std::uint32_t cap = req.normal ? options.normal_max_dimension : options.diffuse_max_dimension;
      std::shared_ptr<textures::DdsImage> image;
      for (;;) {
        const auto parsed = textures::parse_dds(*bytes, cap);
        auto decoded = textures::decode_rgba(parsed, req.normal);
        if (std::max(decoded.width, decoded.height) > cap) decoded = textures::downscale_rgba(decoded, cap);
        if (texture_bytes + decoded.data.size() <= options.texture_budget_bytes) {
          image = std::make_shared<textures::DdsImage>(std::move(decoded));
          break;
        }
        if (cap <= 256) throw Error(ErrorKind::InvalidArgument, "Scene texture budget exceeded even at 256 px");
        cap /= 2;  // degrade resolution instead of dropping the texture
      }
      if (image->width != image->source_width || image->height != image->source_height) ++scene.stats.textures_reduced;
      texture_bytes += image->data.size();
      info.width = image->width;
      info.height = image->height;
      info.source_width = image->source_width;
      info.source_height = image->source_height;
      info.mip_level = image->mip_level;
      info.format = std::string(textures::to_string(textures::parse_dds(*bytes, 1).format));
      info.alpha = textures::measure_alpha(*image);
      scene.textures[req.key] = std::move(image);
      ++scene.stats.textures_loaded;
    } catch (const Error& e) {
      if (e.kind() == ErrorKind::Cancelled) throw;
      ++scene.stats.textures_missing;
      info.error = e.what();
      issue(scene, "warning", "TextureUnreadable", e.what(), req.key);
    } catch (const std::exception& e) {
      ++scene.stats.textures_missing;
      info.error = e.what();
      issue(scene, "warning", "TextureUnreadable", e.what(), req.key);
    }
    scene.texture_info[req.key] = std::move(info);
  }

  // ---- 3. FaceGen tint composition ------------------------------------------------------------
  for (auto& part : scene.parts) {
    if (part.face_tint.empty()) continue;
    const auto base = scene.textures.find(part.diffuse);
    const auto tint = scene.textures.find(part.face_tint);
    if (base == scene.textures.end()) continue;  // already reported as TextureUnreadable
    if (tint == scene.textures.end()) {
      issue(scene, "warning", "FaceTintMissing", "FaceGen tint texture unavailable: the head shows its base skin only", part.face_tint);
      continue;
    }
    const auto detail = part.detail.empty() ? scene.textures.end() : scene.textures.find(part.detail);
    const auto key = part.diffuse + "|facetint:" + part.face_tint;
    if (!scene.textures.contains(key)) {
      auto composite = std::make_shared<textures::DdsImage>(
          apply_face_tint(*base->second, *tint->second, detail == scene.textures.end() ? nullptr : detail->second.get()));
      TextureInfo info = scene.texture_info[part.diffuse];
      info.path = key;
      info.location = "composite";
      info.derived_from = part.diffuse + " + " + part.face_tint + (detail == scene.textures.end() ? "" : " + " + part.detail);
      info.alpha = textures::measure_alpha(*composite);
      scene.texture_info[key] = std::move(info);
      scene.textures[key] = std::move(composite);
    }
    part.diffuse = key;
  }

  // ---- 4. alpha translation, measured on what really reaches the material ---------------------
  for (auto& part : scene.parts) {
    for (const auto& [what, key] : {std::pair{"diffuse", part.original_diffuse}, {"normal map", part.normal}, {"FaceGen tint", part.face_tint}}) {
      if (key.empty()) continue;
      if (const auto it = scene.texture_info.find(key); it != scene.texture_info.end() && !it->second.error.empty()) {
        part.warnings.push_back(std::string(what) + " unavailable (" + key + "): " + it->second.error);
      }
    }
    bool texture_alpha = false;
    if (const auto it = scene.texture_info.find(part.diffuse); it != scene.texture_info.end() && it->second.error.empty()) {
      texture_alpha = it->second.alpha.uses_alpha;
    }
    bool vertex_alpha = false;
    if (part.vertex_colors) {
      for (std::size_t i = kColorOffset + 3; i < part.vertices.size(); i += kFloatsPerVertex) {
        if (part.vertices[i] < 0.98f) {
          vertex_alpha = true;
          break;
        }
      }
    }
    const auto translation = translate_alpha(part.alpha_blend, part.alpha_test,
                                             static_cast<std::uint8_t>(std::lround(part.alpha_cutoff * 255)), texture_alpha,
                                             vertex_alpha || part.material_alpha < 0.999f);
    part.alpha_mode = translation.mode;
    part.alpha_reason = translation.reason;
  }

  scene.stats.shapes = static_cast<int>(scene.parts.size());
  // Bounds (camera framing) cover what is drawn; physics helpers such as a 160-unit ground plane
  // would otherwise shrink the character to a few pixels.
  bool any_bounds = false;
  for (const auto& part : scene.parts) {
    if (!part.shown_by_default) continue;
    for (int c = 0; c < 3; ++c) {
      scene.min[c] = any_bounds ? std::min(scene.min[c], part.min[c]) : part.min[c];
      scene.max[c] = any_bounds ? std::max(scene.max[c], part.max[c]) : part.max[c];
    }
    any_bounds = true;
  }
  if (!any_bounds) {
    issue(scene, "error", "EmptyScene", "No drawable character geometry could be loaded", appearance.npc_form_key);
  }
  if (scene.stats.textures_reduced > 0) {
    issue(scene, "info", "TexturesReduced",
          std::to_string(scene.stats.textures_reduced) + " textures use a smaller stored mip level for the preview (max " +
              std::to_string(options.diffuse_max_dimension) + " px colour, " + std::to_string(options.normal_max_dimension) + " px normals)",
          appearance.npc_form_key);
  }
  issue(scene, "info", "PreviewFidelity",
        "Reference pose preview; Skyrim shaders, animations and SMP physics are not reproduced", appearance.npc_form_key);
  return scene;
}

}  // namespace sns::rendering
