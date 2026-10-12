#include "sns/rendering/scene.hpp"

#include <algorithm>
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

RenderPart to_part(const nif::Shape& shape, std::string id, std::string group, std::string label) {
  RenderPart part;
  part.id = std::move(id);
  part.group = std::move(group);
  part.label = std::move(label);
  part.shape_name = shape.name;
  part.shader_type = shape.shader_type;
  part.double_sided = shape.double_sided;
  part.skinned = shape.skinned;
  part.alpha_test = shape.alpha_test;
  part.alpha_blend = shape.alpha_blend && !shape.alpha_test;  // alpha test is stabler than unsorted blending
  part.alpha_cutoff = shape.alpha_threshold / 255.0f;
  part.diffuse = shape.textures[nif::Diffuse];
  if (!shape.model_space_normals) part.normal = shape.textures[nif::Normal];

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
    const nif::Vec2 uv = i < shape.uvs.size() ? shape.uvs[i] : nif::Vec2{};
    const float data[kFloatsPerVertex] = {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, uv.u, uv.v,
                                          tan.x, tan.y, tan.z, bin.x, bin.y, bin.z};
    std::copy(std::begin(data), std::end(data), v);
    part.min = {std::min(part.min[0], p.x), std::min(part.min[1], p.y), std::min(part.min[2], p.z)};
    part.max = {std::max(part.max[0], p.x), std::max(part.max[1], p.y), std::max(part.max[2], p.z)};
  }
  part.indices = shape.indices;
  return part;
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

std::vector<std::string> apply_skinning(nif::Shape& shape, const nif::Skeleton& skeleton) {
  std::vector<std::string> missing;
  if (!shape.skinned || shape.bones.empty() || shape.influences.size() != shape.positions.size()) return missing;

  // Per bone: skin space -> skeleton global space, through the bone.
  std::vector<std::optional<nif::Transform>> skin_to_global(shape.bones.size());
  for (std::size_t b = 0; b < shape.bones.size(); ++b) {
    const int index = skeleton.index_of(shape.bones[b]);
    if (index < 0 || b >= shape.skin_to_bone.size()) {
      missing.push_back(shape.bones[b]);
      continue;
    }
    skin_to_global[b] = skeleton.bones[static_cast<std::size_t>(index)].to_global.compose(shape.skin_to_bone[b]);
  }

  for (std::size_t v = 0; v < shape.positions.size(); ++v) {
    const Vec3 skin = shape.global_to_skin.apply(shape.positions[v]);
    const Vec3 skin_n = v < shape.normals.size() ? shape.global_to_skin.apply_dir(shape.normals[v]) : Vec3{0, 0, 1};
    const Vec3 skin_t = v < shape.tangents.size() ? shape.global_to_skin.apply_dir(shape.tangents[v]) : Vec3{};
    const Vec3 skin_b = v < shape.bitangents.size() ? shape.global_to_skin.apply_dir(shape.bitangents[v]) : Vec3{};
    Vec3 p{}, n{}, t{}, bit{};
    float total = 0;
    const auto& inf = shape.influences[v];
    for (int k = 0; k < 4; ++k) {
      const float w = inf.weight[static_cast<std::size_t>(k)];
      const auto bone = inf.bone[static_cast<std::size_t>(k)];
      if (w <= 0 || bone >= skin_to_global.size() || !skin_to_global[bone]) continue;
      const Vec3 q = skin_to_global[bone]->apply(skin);
      const Vec3 m = skin_to_global[bone]->apply_dir(skin_n);
      p = {p.x + q.x * w, p.y + q.y * w, p.z + q.z * w};
      n = {n.x + m.x * w, n.y + m.y * w, n.z + m.z * w};
      const auto tangent = skin_to_global[bone]->apply_dir(skin_t);
      const auto binormal = skin_to_global[bone]->apply_dir(skin_b);
      t = {t.x + tangent.x*w, t.y + tangent.y*w, t.z + tangent.z*w};
      bit = {bit.x + binormal.x*w, bit.y + binormal.y*w, bit.z + binormal.z*w};
      total += w;
    }
    if (total > 1e-6f) {
      shape.positions[v] = {p.x / total, p.y / total, p.z / total};
      if (v < shape.normals.size()) shape.normals[v] = normalized(n);
      if (v < shape.tangents.size()) shape.tangents[v] = normalized(t);
      if (v < shape.bitangents.size()) shape.bitangents[v] = normalized(bit);
    }
  }
  return missing;
}

bool is_covered(std::uint32_t slots, std::uint32_t covered) { return (slots & covered) != 0; }

CharacterScene build_character_scene(const Appearance& appearance, const assets::AssetResolver& resolver,
                                     const SceneOptions& options) {
  CharacterScene scene;
  scene.scale = appearance.height * appearance.race_height;
  auto check_cancel = [&] {
    if (options.cancelled && options.cancelled()) throw Error(ErrorKind::Cancelled, "Scene loading cancelled");
  };
  for (const auto& error : resolver.issues()) issue(scene, "warning", "ArchiveUnreadable", error.message, error.archive);
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
      } else issue(scene, "warning", "SkeletonMissing", "Skeleton resource is missing", appearance.skeleton);
    } catch (const std::exception& e) { issue(scene, "warning", "SkeletonUnreadable", e.what(), appearance.skeleton); }
  }
  std::unordered_set<std::string> attempted_textures;
  std::size_t texture_bytes = 0;
  auto texture = [&](std::string& path, bool normal = false) {
    check_cancel();
    if (path.empty() || !attempted_textures.insert(path).second) return;
    try {
      auto bytes = resolver.read(path);
      if (!bytes) throw Error(ErrorKind::NotFound, "Texture resource missing");
      const auto parsed = textures::parse_dds(*bytes);
      if (texture_bytes + textures::level_size(textures::TextureFormat::RGBA8, parsed.width, parsed.height) > 512ull * 1024 * 1024)
        throw Error(ErrorKind::InvalidArgument, "Scene texture budget exceeded (512 MiB)");
      auto image = std::make_shared<textures::DdsImage>(textures::decode_rgba(parsed, normal));
      if (texture_bytes + image->data.size() > 512ull * 1024 * 1024)
        throw Error(ErrorKind::InvalidArgument, "Scene texture budget exceeded (512 MiB)");
      texture_bytes += image->data.size();
      scene.textures[path] = std::move(image);
      ++scene.stats.textures_loaded;
    } catch (const std::exception& e) {
      ++scene.stats.textures_missing;
      issue(scene, "warning", "TextureUnreadable", e.what(), path);
    }
  };
  auto append_mesh = [&](const std::string& path, const std::string& group, const std::string& label,
                         const std::string& armor, std::uint32_t mask, bool slider, bool hide_hair) {
    check_cancel();
    if (path.empty()) return;
    ++scene.stats.meshes_requested;
    auto mesh = load(resolver, path, scene, label);
    if (!mesh) return;
    ++scene.stats.meshes_loaded;
    std::optional<nif::MeshFile> thin;
    const auto thin_path = weight_variant(path, 0);
    if (slider && !thin_path.empty()) thin = load(resolver, thin_path, scene, label);
    for (auto& shape : mesh->shapes) {
      check_cancel();
      if (shape.indices.empty() || shape.positions.empty()) continue;
      if (hide_hair && (shape.shader_type == nif::kShaderHairTint ||
                        text::to_lower_ascii(shape.name).find("hair") != std::string::npos)) continue;
      bool blended = false;
      if (thin) {
        const auto found = std::find_if(thin->shapes.begin(), thin->shapes.end(), [&](const auto& s) { return s.name == shape.name; });
        if (found != thin->shapes.end()) blended = blend_by_weight(shape, *found, appearance.weight);
        if (!blended) issue(scene, "warning", "WeightTopologyMismatch", "Weight meshes do not match; heavy mesh retained", path);
      }
      bool skinned = false;
      if (shape.skinned && options.apply_skinning && skeleton && !shape.bones.empty()) {
        const auto missing = apply_skinning(shape, *skeleton);
        scene.stats.bones_missing += static_cast<int>(missing.size());
        skinned = missing.empty();
        for (const auto& bone : missing) issue(scene, "warning", "BoneMissing", "Skeleton does not contain " + bone, path);
      }
      auto part = to_part(shape, std::to_string(scene.parts.size()), group, label);
      part.armor_form_key = armor;
      part.slot_mask = mask;
      part.mesh_path = path;
      part.weight_blended = blended;
      part.skinning_applied = skinned;
      if (shape.shader_type == nif::kShaderHairTint) part.tint = appearance.hair_color;
      if (shape.shader_type == nif::kShaderSkinTint) part.tint = appearance.skin_tone;
      // FaceGen's tint belongs to the NPC's origin key and overrides the embedded diffuse path.
      if (shape.shader_type == nif::kShaderFaceTint && !appearance.face_tint.empty()) part.diffuse = appearance.face_tint;
      texture(part.diffuse);
      texture(part.normal, true);
      scene.stats.triangles += part.indices.size() / 3;
      scene.stats.skinned_shapes += part.skinned ? 1 : 0;
      scene.parts.push_back(std::move(part));
    }
  };
  std::uint32_t covered = 0;
  for (const auto& piece : appearance.pieces) if (piece.source == "outfit") covered |= piece.slot_mask;
  for (const auto& piece : appearance.pieces) {
    for (const auto& addon : piece.addons) {
      if (piece.source == "skin" && is_covered(addon.slot_mask, covered)) continue;
      append_mesh(addon.mesh, piece.source, piece.name, piece.armor_form_key, addon.slot_mask, addon.weight_slider, false);
    }
  }
  append_mesh(appearance.face_geometry, "head", "FaceGen", "", 0, false, is_covered(kHairSlots, covered));
  scene.stats.shapes = static_cast<int>(scene.parts.size());
  if (!scene.parts.empty()) {
    scene.min = scene.parts.front().min;
    scene.max = scene.parts.front().max;
    for (const auto& part : scene.parts) for (int c = 0; c < 3; ++c) {
      scene.min[c] = std::min(scene.min[c], part.min[c]);
      scene.max[c] = std::max(scene.max[c], part.max[c]);
    }
  } else issue(scene, "error", "EmptyScene", "No character geometry could be loaded", appearance.npc_form_key);
  issue(scene, "info", "PreviewFidelity", "Reference pose preview; Skyrim shaders, animations and SMP physics are not reproduced", appearance.npc_form_key);
  return scene;
}

}  // namespace sns::rendering
