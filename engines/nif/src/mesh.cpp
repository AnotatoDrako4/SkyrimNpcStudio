#include "sns/nif/mesh.hpp"

#include <NifFile.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::nif {
namespace {

Vec3 from(const nifly::Vector3& v) { return {v.x, v.y, v.z}; }

Transform from(const nifly::MatTransform& t) {
  Transform out;
  out.translation = from(t.translation);
  for (int r = 0; r < 3; ++r) {
    out.rotation[r] = {t.rotation[r].x, t.rotation[r].y, t.rotation[r].z};
  }
  out.scale = t.scale;
  return out;
}

nifly::MatTransform global_transform(const nifly::NifFile& nif, nifly::NiAVObject* object) {
  nifly::MatTransform xf = object->GetTransformToParent();
  for (auto* parent = nif.GetParentNode(object); parent; parent = nif.GetParentNode(parent)) {
    xf = parent->GetTransformToParent().ComposeTransforms(xf);
  }
  return xf;
}

void load_nif(nifly::NifFile& nif, std::string_view bytes, std::string_view label) {
  if (bytes.size() > 64ull * 1024 * 1024)
    throw Error(ErrorKind::Parse, "NIF exceeds the 64 MiB parser limit");
  std::istringstream in(std::string(bytes), std::ios::binary);
  if (nif.Load(in) != 0 || !nif.IsValid()) {
    throw Error(ErrorKind::Parse, "cannot read NIF " + std::string(label));
  }
}

}  // namespace

Vec3 Transform::apply(const Vec3& p) const {
  const Vec3 s{p.x * scale, p.y * scale, p.z * scale};
  return {translation.x + rotation[0][0] * s.x + rotation[0][1] * s.y + rotation[0][2] * s.z,
          translation.y + rotation[1][0] * s.x + rotation[1][1] * s.y + rotation[1][2] * s.z,
          translation.z + rotation[2][0] * s.x + rotation[2][1] * s.y + rotation[2][2] * s.z};
}

Vec3 Transform::apply_dir(const Vec3& d) const {
  return {rotation[0][0] * d.x + rotation[0][1] * d.y + rotation[0][2] * d.z,
          rotation[1][0] * d.x + rotation[1][1] * d.y + rotation[1][2] * d.z,
          rotation[2][0] * d.x + rotation[2][1] * d.y + rotation[2][2] * d.z};
}

Transform Transform::compose(const Transform& other) const {
  Transform out;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out.rotation[r][c] = rotation[r][0] * other.rotation[0][c] + rotation[r][1] * other.rotation[1][c] +
                           rotation[r][2] * other.rotation[2][c];
    }
  }
  out.scale = scale * other.scale;
  out.translation = apply(other.translation);
  return out;
}

int Skeleton::index_of(std::string_view name) const {
  for (std::size_t i = 0; i < bones.size(); ++i) {
    if (text::iequals_ascii(bones[i].name, name)) return static_cast<int>(i);
  }
  return -1;
}

std::string normalize_texture_path(std::string_view path) {
  auto key = text::data_path_key(text::trim(path));
  if (key.empty()) return key;
  if (key.starts_with("data\\")) key.erase(0, 5);
  if (!key.starts_with("textures\\")) key = "textures\\" + key;
  return key;
}

MeshFile load_mesh(std::string_view bytes, std::string_view label) {
  nifly::NifFile nif;
  load_nif(nif, bytes, label);
  MeshFile file;

  for (auto* shape : nif.GetShapes()) {
    Shape s;
    s.name = shape->name.get();
    const auto* verts = nif.GetVertsForShape(shape);
    std::vector<nifly::Triangle> tris;
    shape->GetTriangles(tris);
    if (!verts || verts->empty() || tris.empty()) {
      file.warnings.push_back("shape '" + s.name + "' has no geometry");
      continue;
    }

    s.skinned = shape->IsSkinned();
    nifly::MatTransform to_root;
    if (s.skinned) {
      nifly::MatTransform global_to_skin;
      if (nif.CalcShapeTransformGlobalToSkin(shape, global_to_skin)) {
        s.global_to_skin = from(global_to_skin);
        to_root = global_to_skin.InverseTransform();
      }
    } else {
      to_root = global_transform(nif, shape);
    }

    s.positions.reserve(verts->size());
    for (const auto& v : *verts) {
      const auto p = from(to_root.ApplyTransform(v));
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw Error(ErrorKind::Parse, "NIF contains non-finite positions");
      s.positions.push_back(p);
    }
    auto directions = [&](const std::vector<nifly::Vector3>* in, std::vector<Vec3>& out) {
      if (!in || in->size() != verts->size()) return;
      out.reserve(in->size());
      for (const auto& d : *in) out.push_back(from(to_root.ApplyTransformToDir(d)));
    };
    directions(nif.GetNormalsForShape(shape), s.normals);
    directions(nif.GetTangentsForShape(shape), s.tangents);
    directions(nif.GetBitangentsForShape(shape), s.bitangents);
    if (const auto* uvs = nif.GetUvsForShape(shape); uvs && uvs->size() == verts->size()) {
      for (const auto& uv : *uvs) s.uvs.push_back({uv.u, uv.v});
    }
    if (const auto* colors = nif.GetColorsForShape(shape); colors && colors->size() == verts->size()) {
      for (const auto& c : *colors) s.colors.push_back({c.r, c.g, c.b, c.a});
    }

    s.indices.reserve(tris.size() * 3);
    const auto count = static_cast<std::uint32_t>(verts->size());
    for (const auto& t : tris) {
      if (t.p1 >= count || t.p2 >= count || t.p3 >= count) continue;  // corrupt triangle
      s.indices.insert(s.indices.end(), {t.p1, t.p2, t.p3});
    }

    if (auto* shader = nif.GetShader(shape)) {
      s.shader_type = shader->GetShaderType();
      s.double_sided = shader->IsDoubleSided();
      s.model_space_normals = shader->IsModelSpace();
      s.vertex_colors = shader->HasVertexColors();
    }
    for (std::uint32_t slot = 0; slot < s.textures.size(); ++slot) {
      std::string texture;
      nif.GetTextureSlot(shape, texture, slot);
      s.textures[slot] = normalize_texture_path(texture);
    }
    if (auto* alpha = nif.GetAlphaProperty(shape)) {
      s.alpha_blend = (alpha->flags & 0x1) != 0;
      s.alpha_test = (alpha->flags & 0x200) != 0;
      s.alpha_threshold = alpha->threshold;
    }

    if (s.skinned) {
      nif.GetShapeBoneList(shape, s.bones);
      s.influences.assign(verts->size(), {});
      for (std::uint32_t b = 0; b < s.bones.size(); ++b) {
        nifly::MatTransform skin_to_bone;
        nif.GetShapeTransformSkinToBone(shape, b, skin_to_bone);
        s.skin_to_bone.push_back(from(skin_to_bone));
        std::unordered_map<std::uint16_t, float> weights;
        nif.GetShapeBoneWeights(shape, b, weights);
        for (const auto& [vertex, weight] : weights) {
          if (vertex >= s.influences.size() || weight <= 0.0f) continue;
          auto& inf = s.influences[vertex];
          // Keep the four strongest influences.
          const auto weakest = std::min_element(inf.weight.begin(), inf.weight.end()) - inf.weight.begin();
          if (weight > inf.weight[static_cast<std::size_t>(weakest)]) {
            inf.weight[static_cast<std::size_t>(weakest)] = weight;
            inf.bone[static_cast<std::size_t>(weakest)] = static_cast<std::uint16_t>(b);
          }
        }
      }
      nifly::NiVector<nifly::BSDismemberSkinInstance::PartitionInfo> parts;
      std::vector<int> tri_parts;
      if (nif.GetShapePartitions(shape, parts, tri_parts)) {
        for (const auto& p : parts) s.partitions.push_back(p.partID);
      }
    }
    file.shapes.push_back(std::move(s));
  }
  if (file.shapes.empty()) file.warnings.push_back(std::string(label) + " contains no renderable shape");
  return file;
}

Skeleton load_skeleton(std::string_view bytes, std::string_view label) {
  nifly::NifFile nif;
  load_nif(nif, bytes, label);
  Skeleton skeleton;
  const auto nodes = nif.GetNodes();
  std::unordered_map<const nifly::NiNode*, int> index;
  for (auto* node : nodes) {
    index[node] = static_cast<int>(skeleton.bones.size());
    skeleton.bones.push_back({node->name.get(), -1, from(global_transform(nif, node))});
  }
  for (auto* node : nodes) {
    if (auto* parent = nif.GetParentNode(node); parent && index.contains(parent)) {
      skeleton.bones[static_cast<std::size_t>(index[node])].parent = index[parent];
    }
  }
  return skeleton;
}

}  // namespace sns::nif
