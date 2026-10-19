#include "sns/rendering/scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "sns/assets/asset_resolver.hpp"
#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/rendering/smp_xml.hpp"

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

bool overlaid(const SceneOptions& options, const std::string& key) {
  return options.overlay && options.overlay->contains(key);
}

// Bytes of a Data path: the build's overlay first (generated draft files), then MO2/BSA.
std::optional<std::string> read_asset(const assets::AssetResolver& resolver, const SceneOptions& options, const std::string& path) {
  if (options.overlay) {
    if (const auto it = options.overlay->find(text::data_path_key(path)); it != options.overlay->end() && it->second) {
      return *it->second;
    }
  }
  return resolver.read(path);
}

std::optional<nif::MeshFile> load(const assets::AssetResolver& resolver, const SceneOptions& options, const std::string& path,
                                  CharacterScene& scene, const std::string& subject) {
  const auto key = text::data_path_key(path);
  const bool generated = overlaid(options, key);
  try {
    if (!generated && options.cache) {
      if (const auto cached = options.cache->mesh(key)) {
        for (const auto& w : cached->warnings) issue(scene, "info", "MeshWarning", w, path);
        return *cached;
      }
    }
    auto bytes = read_asset(resolver, options, path);
    if (!bytes) {
      issue(scene, "warning", "MeshNotFound", "Mesh not found as loose file or in any loaded BSA: " + path, subject);
      return std::nullopt;
    }
    auto mesh = nif::load_mesh(*bytes, path);
    for (const auto& w : mesh.warnings) issue(scene, "info", "MeshWarning", w, path);
    if (!generated && options.cache) options.cache->put_mesh(key, std::make_shared<const nif::MeshFile>(mesh));
    return mesh;
  } catch (const Error& e) {
    issue(scene, "warning", "MeshUnreadable", e.what(), subject);
    return std::nullopt;
  } catch (const std::exception& e) {
    // Audit F-12: like textures and the skeleton below, any failure of one mesh drops that mesh only; the
    // rest of the character (the head included) is still built.
    issue(scene, "warning", "MeshUnreadable", "cannot read " + path + ": " + e.what(), subject);
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

// Smooth vertex normals from the geometry: area-weighted face normals, summed over every vertex that
// shares a position (UV seams split vertices; welding keeps the normal continuous across them).
void recompute_smooth_normals(nif::Shape& shape) {
  const auto n = shape.positions.size();
  std::vector<Vec3> sum(n);
  for (std::size_t t = 0; t + 2 < shape.indices.size(); t += 3) {
    const auto i0 = shape.indices[t], i1 = shape.indices[t + 1], i2 = shape.indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n) continue;
    const Vec3& a = shape.positions[i0]; const Vec3& b = shape.positions[i1]; const Vec3& c = shape.positions[i2];
    const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 fn{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};  // |fn| = 2 * area
    for (const auto i : {i0, i1, i2}) sum[i] = {sum[i].x + fn.x, sum[i].y + fn.y, sum[i].z + fn.z};
  }
  // Weld by position (quantized to 1e-4 units).
  std::unordered_map<std::string, Vec3> welded;
  auto key_of = [](const Vec3& p) {
    char k[64];
    std::snprintf(k, sizeof(k), "%lld,%lld,%lld", std::llround(p.x * 1e4), std::llround(p.y * 1e4), std::llround(p.z * 1e4));
    return std::string(k);
  };
  for (std::size_t i = 0; i < n; ++i) {
    auto& w = welded[key_of(shape.positions[i])];
    w = {w.x + sum[i].x, w.y + sum[i].y, w.z + sum[i].z};
  }
  shape.normals.resize(n);
  for (std::size_t i = 0; i < n; ++i) shape.normals[i] = normalized(welded[key_of(shape.positions[i])]);
}

// Per-vertex tangent (along u) and bitangent (along v) from the UVs, orthogonalized against the normal.
// Only for shapes without them: the model-space path never reads tangents, but the bake and Qt need a frame.
void ensure_uv_tangents(nif::Shape& shape) {
  const auto n = shape.positions.size();
  if (shape.tangents.size() == n && shape.bitangents.size() == n) return;
  std::vector<Vec3> tu(n), tv(n);
  for (std::size_t t = 0; t + 2 < shape.indices.size(); t += 3) {
    const auto i0 = shape.indices[t], i1 = shape.indices[t + 1], i2 = shape.indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n || shape.uvs.size() != n) continue;
    const Vec3& p0 = shape.positions[i0]; const Vec3& p1 = shape.positions[i1]; const Vec3& p2 = shape.positions[i2];
    const Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    const float du1 = shape.uvs[i1].u - shape.uvs[i0].u, dv1 = shape.uvs[i1].v - shape.uvs[i0].v;
    const float du2 = shape.uvs[i2].u - shape.uvs[i0].u, dv2 = shape.uvs[i2].v - shape.uvs[i0].v;
    const float r = du1 * dv2 - du2 * dv1;
    if (std::abs(r) < 1e-12f) continue;
    const Vec3 su{(e1.x * dv2 - e2.x * dv1) / r, (e1.y * dv2 - e2.y * dv1) / r, (e1.z * dv2 - e2.z * dv1) / r};
    const Vec3 sv{(e2.x * du1 - e1.x * du2) / r, (e2.y * du1 - e1.y * du2) / r, (e2.z * du1 - e1.z * du2) / r};
    for (const auto i : {i0, i1, i2}) {
      tu[i] = {tu[i].x + su.x, tu[i].y + su.y, tu[i].z + su.z};
      tv[i] = {tv[i].x + sv.x, tv[i].y + sv.y, tv[i].z + sv.z};
    }
  }
  shape.tangents.resize(n);
  shape.bitangents.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3 nr = i < shape.normals.size() ? normalized(shape.normals[i]) : Vec3{0, 0, 1};
    auto ortho = [&](Vec3 v) {
      const float d = v.x * nr.x + v.y * nr.y + v.z * nr.z;
      return normalized({v.x - nr.x * d, v.y - nr.y * d, v.z - nr.z * d});
    };
    shape.tangents[i] = ortho(tu[i]);
    shape.bitangents[i] = ortho(tv[i]);
  }
}

std::vector<float> bind_frames(const nif::Shape& shape) {
  const auto n = shape.positions.size();
  std::vector<float> frames(n * 9);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3 t = i < shape.tangents.size() ? shape.tangents[i] : Vec3{1, 0, 0};
    const Vec3 b = i < shape.bitangents.size() ? shape.bitangents[i] : Vec3{0, 1, 0};
    const Vec3 nr = i < shape.normals.size() ? shape.normals[i] : Vec3{0, 0, 1};
    const float f[9] = {t.x, t.y, t.z, b.x, b.y, b.z, nr.x, nr.y, nr.z};
    std::copy(std::begin(f), std::end(f), frames.data() + i * 9);
  }
  return frames;
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

nif::Skeleton pose_skeleton(const nif::Skeleton& base, std::string_view pose_name) {
  if (pose_name.empty() || pose_name == "reference" || pose_name == "tpose") {
    return base;
  }
  nif::Skeleton posed = base;

  auto rotate_sub_tree = [&](int bone_idx, const std::array<std::array<float, 3>, 3>& R) {
    if (bone_idx < 0 || static_cast<std::size_t>(bone_idx) >= posed.bones.size()) return;
    const auto C = posed.bones[static_cast<std::size_t>(bone_idx)].to_global.translation;

    std::vector<bool> is_descendant(posed.bones.size(), false);
    is_descendant[static_cast<std::size_t>(bone_idx)] = true;
    for (std::size_t i = 0; i < posed.bones.size(); ++i) {
      int p = posed.bones[i].parent;
      while (p >= 0) {
        if (p == bone_idx || is_descendant[static_cast<std::size_t>(p)]) {
          is_descendant[i] = true;
          break;
        }
        p = posed.bones[static_cast<std::size_t>(p)].parent;
      }
    }

    for (std::size_t i = 0; i < posed.bones.size(); ++i) {
      if (!is_descendant[i]) continue;
      auto& b = posed.bones[i];
      const float dx = b.to_global.translation.x - C.x;
      const float dy = b.to_global.translation.y - C.y;
      const float dz = b.to_global.translation.z - C.z;
      b.to_global.translation.x = C.x + (R[0][0] * dx + R[0][1] * dy + R[0][2] * dz);
      b.to_global.translation.y = C.y + (R[1][0] * dx + R[1][1] * dy + R[1][2] * dz);
      b.to_global.translation.z = C.z + (R[2][0] * dx + R[2][1] * dy + R[2][2] * dz);

      std::array<std::array<float, 3>, 3> new_rot{};
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          new_rot[r][c] = R[r][0] * b.to_global.rotation[0][c] +
                          R[r][1] * b.to_global.rotation[1][c] +
                          R[r][2] * b.to_global.rotation[2][c];
        }
      }
      b.to_global.rotation = new_rot;
    }
  };

  auto rot_x = [](float rad) -> std::array<std::array<float, 3>, 3> {
    const float c = std::cos(rad), s = std::sin(rad);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
  };
  auto rot_y = [](float rad) -> std::array<std::array<float, 3>, 3> {
    const float c = std::cos(rad), s = std::sin(rad);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
  };

  auto find_bone = [&](std::string_view name_part) {
    for (std::size_t i = 0; i < posed.bones.size(); ++i) {
      if (text::to_lower_ascii(posed.bones[i].name).find(text::to_lower_ascii(name_part)) != std::string::npos) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  const int l_arm = find_bone("L UpperArm");
  const int r_arm = find_bone("R UpperArm");
  const int l_forearm = find_bone("L Forearm");
  const int r_forearm = find_bone("R Forearm");
  const int l_thigh = find_bone("L Thigh");
  const int r_thigh = find_bone("R Thigh");
  const int spine = find_bone("NPC Spine1");

  if (pose_name == "apose") {
    if (l_arm >= 0) rotate_sub_tree(l_arm, rot_y(0.70f));
    if (r_arm >= 0) rotate_sub_tree(r_arm, rot_y(-0.70f));
  } else if (pose_name == "relaxed") {
    if (l_arm >= 0) rotate_sub_tree(l_arm, rot_y(0.85f));
    if (r_arm >= 0) rotate_sub_tree(r_arm, rot_y(-0.85f));
    if (l_forearm >= 0) rotate_sub_tree(l_forearm, rot_x(0.25f));
    if (r_forearm >= 0) rotate_sub_tree(r_forearm, rot_x(0.25f));
  } else if (pose_name == "combat") {
    if (spine >= 0) rotate_sub_tree(spine, rot_x(0.12f));
    if (l_arm >= 0) rotate_sub_tree(l_arm, rot_y(0.35f));
    if (l_arm >= 0) rotate_sub_tree(l_arm, rot_x(0.45f));
    if (r_arm >= 0) rotate_sub_tree(r_arm, rot_y(-0.35f));
    if (r_arm >= 0) rotate_sub_tree(r_arm, rot_x(0.30f));
    if (l_thigh >= 0) rotate_sub_tree(l_thigh, rot_x(0.20f));
    if (r_thigh >= 0) rotate_sub_tree(r_thigh, rot_x(-0.15f));
  }
  return posed;
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

textures::DdsImage apply_skin_tint(const textures::DdsImage& base, const Rgb& tint) {
  if (base.format != textures::TextureFormat::RGBA8) throw Error(ErrorKind::InvalidArgument, "apply_skin_tint needs a decoded RGBA8 image");
  if (base.width == 0 || base.height == 0 || base.data.size() != 4ull * base.width * base.height)
    throw Error(ErrorKind::InvalidArgument, "apply_skin_tint got an empty or truncated image");
  textures::DdsImage out = base;
  auto* px = reinterpret_cast<std::uint8_t*>(out.data.data());
  const float t[3] = {tint.r, tint.g, tint.b};
  constexpr float scale[3] = {1.01171875f, 0.99609375f, 1.01171875f};
  // One lookup table per channel: the result only depends on the 8-bit texel value.
  std::uint8_t lut[3][256];
  for (int c = 0; c < 3; ++c) {
    for (int v = 0; v < 256; ++v) {
      const float b = v / 255.0f;
      const float result = (b * b + 2.0f * t[c] * b * (1.0f - b)) * scale[c];
      lut[c][v] = static_cast<std::uint8_t>(std::clamp(result, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
  }
  const std::size_t texels = static_cast<std::size_t>(base.width) * base.height;
  for (std::size_t i = 0; i < texels; ++i) {
    auto* texel = px + i * 4;
    for (int c = 0; c < 3; ++c) texel[c] = lut[c][texel[c]];
  }
  return out;
}

namespace {

// Skyrim's _msn decode: n = rgb.xzy * 2 - 1 (Lighting.hlsl, MODELSPACENORMALS).
Vec3 decode_msn(const textures::DdsImage& msn, std::uint32_t x, std::uint32_t y) {
  const auto* t = reinterpret_cast<const std::uint8_t*>(msn.data.data()) + (static_cast<std::size_t>(y) * msn.width + x) * 4;
  return normalized({t[0] / 127.5f - 1.0f, t[2] / 127.5f - 1.0f, t[1] / 127.5f - 1.0f});
}

Vec3 frame_vec(const std::vector<float>& frames, std::size_t vertex, int which) {
  const float* f = frames.data() + vertex * 9 + which * 3;
  return {f[0], f[1], f[2]};
}

void check_msn_inputs(const textures::DdsImage& msn, const std::vector<float>& uvs, const std::vector<float>& frames) {
  if (msn.format != textures::TextureFormat::RGBA8 || msn.width == 0 || msn.height == 0 ||
      msn.data.size() != 4ull * msn.width * msn.height) {
    throw Error(ErrorKind::InvalidArgument, "model-space normal map must be a decoded RGBA8 image");
  }
  if (uvs.size() % 2 != 0 || frames.size() != uvs.size() / 2 * 9) {
    throw Error(ErrorKind::InvalidArgument, "model-space normal bake needs 2 UV floats and 9 frame floats per vertex");
  }
}

std::uint32_t wrap_texel(float coord, std::uint32_t size) {
  const auto i = static_cast<long long>(std::floor(coord));
  const auto s = static_cast<long long>(size);
  return static_cast<std::uint32_t>(((i % s) + s) % s);
}

}  // namespace

float model_space_normal_agreement(const textures::DdsImage& msn, const std::vector<float>& uvs, const std::vector<float>& frames) {
  check_msn_inputs(msn, uvs, frames);
  const std::size_t n = uvs.size() / 2;
  if (n == 0) return 0.0f;
  double sum = 0;
  for (std::size_t v = 0; v < n; ++v) {
    const Vec3 m = decode_msn(msn, wrap_texel(uvs[2 * v] * msn.width, msn.width), wrap_texel(uvs[2 * v + 1] * msn.height, msn.height));
    const Vec3 nv = normalized(frame_vec(frames, v, 2));
    sum += m.x * nv.x + m.y * nv.y + m.z * nv.z;
  }
  return static_cast<float>(sum / static_cast<double>(n));
}

textures::DdsImage bake_model_space_normals(const textures::DdsImage& msn, const std::vector<float>& uvs,
                                            const std::vector<float>& frames, const std::vector<std::uint32_t>& indices, int dilate) {
  check_msn_inputs(msn, uvs, frames);
  const std::uint32_t w = msn.width, h = msn.height;
  textures::DdsImage out = msn;
  auto* px = reinterpret_cast<std::uint8_t*>(out.data.data());
  std::vector<std::uint8_t> covered(static_cast<std::size_t>(w) * h, 0);
  const std::size_t vertex_count = uvs.size() / 2;
  for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
    const std::uint32_t id[3] = {indices[t], indices[t + 1], indices[t + 2]};
    if (id[0] >= vertex_count || id[1] >= vertex_count || id[2] >= vertex_count) continue;
    // Texel centres in pixel space (the material samples at uv * size - 0.5).
    float x[3], y[3];
    for (int k = 0; k < 3; ++k) {
      x[k] = uvs[2 * id[k]] * w - 0.5f;
      y[k] = uvs[2 * id[k] + 1] * h - 0.5f;
    }
    const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (std::abs(area) < 1e-12f) continue;
    const int x0 = static_cast<int>(std::floor(std::min({x[0], x[1], x[2]}))), x1 = static_cast<int>(std::ceil(std::max({x[0], x[1], x[2]})));
    const int y0 = static_cast<int>(std::floor(std::min({y[0], y[1], y[2]}))), y1 = static_cast<int>(std::ceil(std::max({y[0], y[1], y[2]})));
    for (int py = y0; py <= y1; ++py) {
      for (int pxi = x0; pxi <= x1; ++pxi) {
        const float fx = static_cast<float>(pxi), fy = static_cast<float>(py);
        const float b0 = ((x[1] - fx) * (y[2] - fy) - (x[2] - fx) * (y[1] - fy)) / area;
        const float b1 = ((x[2] - fx) * (y[0] - fy) - (x[0] - fx) * (y[2] - fy)) / area;
        const float b2 = 1.0f - b0 - b1;
        constexpr float eps = -1e-4f;
        if (b0 < eps || b1 < eps || b2 < eps) continue;
        const std::uint32_t tx = wrap_texel(fx + 0.5f, w), ty = wrap_texel(fy + 0.5f, h);
        Vec3 axes[3];
        for (int which = 0; which < 3; ++which) {
          const Vec3 a = frame_vec(frames, id[0], which), b = frame_vec(frames, id[1], which), c = frame_vec(frames, id[2], which);
          axes[which] = normalized({a.x * b0 + b.x * b1 + c.x * b2, a.y * b0 + b.y * b1 + c.y * b2, a.z * b0 + b.z * b1 + c.z * b2});
        }
        const Vec3& T = axes[0];
        const Vec3& B = axes[1];
        const Vec3& N = axes[2];
        // Solve [T B N] * c = n by Cramer's rule (the frame need not be orthonormal).
        const float det = T.x * (B.y * N.z - B.z * N.y) - B.x * (T.y * N.z - T.z * N.y) + N.x * (T.y * B.z - T.z * B.y);
        if (std::abs(det) < 1e-6f) continue;
        const Vec3 m = decode_msn(msn, tx, ty);
        const float cx = (m.x * (B.y * N.z - B.z * N.y) - B.x * (m.y * N.z - m.z * N.y) + N.x * (m.y * B.z - m.z * B.y)) / det;
        const float cy = (T.x * (m.y * N.z - m.z * N.y) - m.x * (T.y * N.z - T.z * N.y) + N.x * (T.y * m.z - T.z * m.y)) / det;
        const float cz = (T.x * (B.y * m.z - B.z * m.y) - B.x * (T.y * m.z - T.z * m.y) + m.x * (T.y * B.z - T.z * B.y)) / det;
        const Vec3 ts = normalized({cx, cy, cz});
        auto* o = px + (static_cast<std::size_t>(ty) * w + tx) * 4;
        o[0] = static_cast<std::uint8_t>(std::clamp(ts.x * 127.5f + 127.5f, 0.0f, 255.0f));
        o[1] = static_cast<std::uint8_t>(std::clamp(ts.y * 127.5f + 127.5f, 0.0f, 255.0f));
        o[2] = static_cast<std::uint8_t>(std::clamp(ts.z * 127.5f + 127.5f, 0.0f, 255.0f));
        o[3] = 255;
        covered[static_cast<std::size_t>(ty) * w + tx] = 1;
      }
    }
  }
  // Dilate covered texels into their neighbours so bilinear sampling at island borders never mixes in
  // unbaked data; whatever stays uncovered becomes flat.
  for (int pass = 0; pass < dilate; ++pass) {
    std::vector<std::uint8_t> next = covered;
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        if (covered[static_cast<std::size_t>(y) * w + x]) continue;
        int sum[3] = {0, 0, 0}, count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const long long nx = static_cast<long long>(x) + dx, ny = static_cast<long long>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || !covered[static_cast<std::size_t>(ny) * w + nx]) continue;
            const auto* s = px + (static_cast<std::size_t>(ny) * w + nx) * 4;
            for (int c = 0; c < 3; ++c) sum[c] += s[c];
            ++count;
          }
        }
        if (count == 0) continue;
        auto* o = px + (static_cast<std::size_t>(y) * w + x) * 4;
        for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / count);
        o[3] = 255;
        next[static_cast<std::size_t>(y) * w + x] = 1;
      }
    }
    covered.swap(next);
  }
  for (std::size_t i = 0; i < covered.size(); ++i) {
    if (covered[i]) continue;
    auto* o = px + i * 4;
    o[0] = 128; o[1] = 128; o[2] = 255; o[3] = 255;
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
  return parse_smp_xml(xml).collision_shapes;
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
      if (auto bytes = read_asset(resolver, options, appearance.skeleton)) {
        skeleton = nif::load_skeleton(*bytes, appearance.skeleton);
        if (!options.pose_name.empty() && options.pose_name != "reference") {
          *skeleton = pose_skeleton(*skeleton, options.pose_name);
        }
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
    auto mesh = load(resolver, options, path, scene, label);
    if (!mesh) return;
    ++scene.stats.meshes_loaded;
    const auto location = overlaid(options, text::data_path_key(path)) ? std::string("generated (draft)") : describe(resolver.locate(path));
    std::optional<nif::MeshFile> thin;
    const auto thin_path = weight_variant(path, 0);
    if (slider && !thin_path.empty() && text::to_lower_ascii(thin_path) != text::to_lower_ascii(path)) {
      thin = load(resolver, options, thin_path, scene, label);
    }
    std::vector<std::string> collision;
    std::vector<std::string> physics_bones;
    std::string xml_location;
    std::string xml_error;
    bool xml_well_formed = false;
    if (!mesh->physics_xml.empty()) {
      if (auto xml = read_asset(resolver, options, mesh->physics_xml)) {
        const auto parsed = parse_smp_xml(*xml);
        collision = parsed.collision_shapes;
        physics_bones = parsed.bones;
        xml_well_formed = parsed.well_formed;
        xml_error = parsed.error;
        xml_location = describe(resolver.locate(mesh->physics_xml));
        if (!parsed.well_formed)
          issue(scene, "warning", "PhysicsXmlInvalid", "SMP physics XML cannot be parsed safely: " + parsed.error, mesh->physics_xml);
      } else {
        xml_location = "missing";
        issue(scene, "warning", "PhysicsXmlMissing", "SMP physics file named by the mesh was not found: " + mesh->physics_xml, path);
      }
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
      // _msn: the frame the map is baked against is the bind pose, before skinning moves the vertices.
      std::vector<float> msn_frames;
      const bool msn = shape.model_space_normals && !shape.textures[nif::Normal].empty();
      if (msn && shape.uvs.size() == shape.positions.size()) {
        // Skyrim never reads the vertex normals of a model-space shape, and on real bodies, hands and
        // heads they disagree with the geometry (mean dot 0.1-0.4); the _msn map agrees with geometric
        // normals (0.8-0.99). Light and bake with normals rebuilt from the geometry instead.
        recompute_smooth_normals(shape);
        shape.tangents.clear();
        shape.bitangents.clear();
        shape.tangents_swapped = false;
        ensure_uv_tangents(shape);
        msn_frames = bind_frames(shape);
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
      if (!msn_frames.empty()) {
        part.msn_source = shape.textures[nif::Normal];
        part.msn_frames = std::move(msn_frames);
      } else if (msn) {
        part.warnings.push_back("model-space normal map (_msn) on a shape without UVs: vertex normals are used");
      }
      part.armor_form_key = armor;
      part.slot_mask = mask;
      part.mesh_path = path;
      part.mesh_location = location;
      part.weight_blended = blended;
      part.skinning_applied = shape.skinned && options.apply_skinning && skeleton && skinning.missing.empty();
      part.bones_from_mesh = std::move(skinning.from_mesh);
      part.bones_missing = std::move(skinning.missing);
      part.physics_xml = mesh->physics_xml;
      part.physics_xml_location = xml_location;
      part.physics_xml_well_formed = xml_well_formed;
      part.physics_xml_error = xml_error;
      part.physics_bones = physics_bones;
      part.collision_shapes = collision;
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
  for (std::size_t i = 0; i < outfit.size() && !options.head_only; ++i) {
    if (!equipped[i]) continue;
    for (const auto& addon : outfit[i]->addons) {
      append_mesh(addon.mesh, "outfit", outfit[i]->name, outfit[i]->armor_form_key, addon.form_key, addon.slot_mask, addon.weight_slider, false);
    }
  }
  for (const auto& piece : appearance.pieces) {
    if (piece.source != "skin" || options.head_only) continue;
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
    request(part.msn_source, (head ? 2 : 3) + later, true);
  }
  std::stable_sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) { return a.priority < b.priority; });

  std::uint64_t texture_bytes = 0;
  for (const auto& req : requests) {
    check_cancel();
    TextureInfo info;
    info.path = req.key;
    info.normal_map = req.normal;
    const auto data_key = text::data_path_key(req.key);
    const bool generated = overlaid(options, data_key);
    info.location = generated ? std::string("generated (draft)") : describe(resolver.locate(req.key));
    const std::uint32_t first_cap = req.normal ? options.normal_max_dimension : options.diffuse_max_dimension;
    const auto cache_key = data_key + (req.normal ? "|n|" : "|c|") + std::to_string(first_cap);
    if (!generated && options.cache) {
      if (const auto cached = options.cache->texture(cache_key);
          cached && texture_bytes + cached->image->data.size() <= options.texture_budget_bytes) {
        const auto& image = cached->image;
        if (image->width != image->source_width || image->height != image->source_height) ++scene.stats.textures_reduced;
        texture_bytes += image->data.size();
        info.width = image->width;
        info.height = image->height;
        info.source_width = image->source_width;
        info.source_height = image->source_height;
        info.mip_level = image->mip_level;
        info.format = cached->source_format;
        info.alpha = cached->alpha;
        scene.textures[req.key] = image;
        ++scene.stats.textures_loaded;
        scene.texture_info[req.key] = std::move(info);
        continue;
      }
    }
    try {
      auto bytes = read_asset(resolver, options, req.key);
      if (!bytes) throw Error(ErrorKind::NotFound, "Texture not found as loose file or in any loaded BSA");
      std::uint32_t cap = first_cap;
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
      if (!generated && options.cache && cap == first_cap) options.cache->put_texture(cache_key, {image, info.format, info.alpha});
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

  // ---- 3b. SkinTint composition (FacegenRGBTint) -----------------------------------------------
  // The body's skin tone is a soft-light blend over its diffuse, like the face tint; drawing it as a
  // material multiply darkened the body against the face (seam at the neck). Untextured shapes keep
  // the plain tint color.
  for (auto& part : scene.parts) {
    if (part.shader_type != nif::kShaderSkinTint || !part.tint) continue;
    const auto base = scene.textures.find(part.diffuse);
    if (base == scene.textures.end()) continue;
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%.4f,%.4f,%.4f", part.tint->r, part.tint->g, part.tint->b);
    const auto key = part.diffuse + "|skintint:" + hex;
    if (!scene.textures.contains(key)) {
      auto composite = std::make_shared<textures::DdsImage>(apply_skin_tint(*base->second, *part.tint));
      TextureInfo info = scene.texture_info[part.diffuse];
      info.path = key;
      info.location = "composite";
      info.derived_from = part.diffuse + " + skin tone (QNAM) " + hex;
      info.alpha = textures::measure_alpha(*composite);
      scene.texture_info[key] = std::move(info);
      scene.textures[key] = std::move(composite);
    }
    part.diffuse = key;
    part.tint.reset();  // applied in the texture: a material multiply on top would tint twice
  }

  // ---- 3c. model-space normal maps baked to tangent space ----------------------------------------
  for (auto& part : scene.parts) {
    if (part.msn_source.empty()) continue;
    const auto msn = scene.textures.find(part.msn_source);
    if (msn == scene.textures.end()) {
      part.warnings.push_back("model-space normal map unavailable: vertex normals are used");
      part.msn_frames.clear();
      continue;
    }
    std::vector<float> uvs(part.vertex_count * 2);
    for (std::size_t v = 0; v < part.vertex_count; ++v) {
      uvs[2 * v] = part.vertices[v * kFloatsPerVertex + 6];
      uvs[2 * v + 1] = part.vertices[v * kFloatsPerVertex + 7];
    }
    part.msn_agreement = model_space_normal_agreement(*msn->second, uvs, part.msn_frames);
    if (part.msn_agreement < 0.5f) {
      char text[160];
      std::snprintf(text, sizeof(text), "model-space normal map does not match this mesh (agreement %.2f): vertex normals are used",
                    part.msn_agreement);
      part.warnings.push_back(text);
      issue(scene, "warning", "ModelSpaceNormalsMismatch", text, part.msn_source);
      part.msn_frames.clear();
      continue;
    }
    const auto key = part.msn_source + "|tangent:" + part.mesh_path + "|" + part.shape_name;
    if (!scene.textures.contains(key)) {
      auto baked = std::make_shared<textures::DdsImage>(bake_model_space_normals(*msn->second, uvs, part.msn_frames, part.indices));
      TextureInfo info = scene.texture_info[part.msn_source];
      info.path = key;
      info.location = "composite";
      char text[96];
      std::snprintf(text, sizeof(text), " (model space -> tangent space, agreement %.2f)", part.msn_agreement);
      info.derived_from = part.msn_source + text;
      info.normal_map = true;
      scene.texture_info[key] = std::move(info);
      scene.textures[key] = std::move(baked);
    }
    part.normal = key;
    part.msn_frames.clear();
    part.msn_frames.shrink_to_fit();
    char text[200];
    std::snprintf(text, sizeof(text), "%s: model-space normal map converted to tangent space (agreement with vertex normals %.2f)",
                  part.shape_name.c_str(), part.msn_agreement);
    issue(scene, "info", "ModelSpaceNormalsBaked", text, part.msn_source);
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

// ------------------------------------------------------------------------------------- cache

std::shared_ptr<const nif::MeshFile> SceneCache::mesh(const std::string& key) const {
  std::lock_guard lock(mutex_);
  const auto it = meshes_.find(key);
  return it == meshes_.end() ? nullptr : it->second;
}

void SceneCache::put_mesh(const std::string& key, std::shared_ptr<const nif::MeshFile> mesh) {
  std::lock_guard lock(mutex_);
  meshes_[key] = std::move(mesh);
}

std::optional<SceneCache::Texture> SceneCache::texture(const std::string& key) const {
  std::lock_guard lock(mutex_);
  const auto it = textures_.find(key);
  if (it == textures_.end()) return std::nullopt;
  return it->second;
}

void SceneCache::put_texture(const std::string& key, Texture texture) {
  if (!texture.image) return;
  std::lock_guard lock(mutex_);
  const auto size = texture.image->data.size();
  if (texture_bytes_ + size > budget_) {
    textures_.clear();  // simple and predictable: start over rather than evicting piecemeal
    texture_bytes_ = 0;
  }
  if (const auto it = textures_.find(key); it != textures_.end()) texture_bytes_ -= it->second.image->data.size();
  texture_bytes_ += size;
  textures_[key] = std::move(texture);
}

void SceneCache::clear() {
  std::lock_guard lock(mutex_);
  meshes_.clear();
  textures_.clear();
  texture_bytes_ = 0;
}

std::size_t SceneCache::mesh_count() const {
  std::lock_guard lock(mutex_);
  return meshes_.size();
}

std::size_t SceneCache::texture_count() const {
  std::lock_guard lock(mutex_);
  return textures_.size();
}

}  // namespace sns::rendering
