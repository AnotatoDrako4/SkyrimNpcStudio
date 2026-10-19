#include "sns/nif/mesh.hpp"

#include <NifFile.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/nif/safe_load.hpp"

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

// NiAVObject flag bit 0 hides the object and its whole subtree.
bool hidden_in_chain(const nifly::NifFile& nif, nifly::NiAVObject* object) {
  if (object->flags & 0x1) return true;
  for (auto* parent = nif.GetParentNode(object); parent; parent = nif.GetParentNode(parent)) {
    if (parent->flags & 0x1) return true;
  }
  return false;
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 unit(const Vec3& v) {
  const float len = std::sqrt(dot(v, v));
  return len > 1e-12f ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{};
}

// Structural prevalidation, then nifly, every failure as Error(Parse) (audit F-10; see safe_load.hpp).
void load_nif(nifly::NifFile& nif, std::string_view bytes, std::string_view label) { load_nifly(nif, bytes, label); }

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

bool orient_tangent_basis(Shape& s) {
  const auto n = s.positions.size();
  if (s.tangents.size() != n || s.bitangents.size() != n || s.uvs.size() != n || s.indices.size() < 3) return false;
  // Compare both stored vectors with the geometric dP/du of each triangle (sampled).
  double along_tangent = 0, along_bitangent = 0;
  const std::size_t step = std::max<std::size_t>(1, s.indices.size() / 3 / 4000) * 3;
  for (std::size_t t = 0; t + 2 < s.indices.size(); t += step) {
    const auto i0 = s.indices[t], i1 = s.indices[t + 1], i2 = s.indices[t + 2];
    const Vec3 e1 = sub(s.positions[i1], s.positions[i0]);
    const Vec3 e2 = sub(s.positions[i2], s.positions[i0]);
    const float du1 = s.uvs[i1].u - s.uvs[i0].u, dv1 = s.uvs[i1].v - s.uvs[i0].v;
    const float du2 = s.uvs[i2].u - s.uvs[i0].u, dv2 = s.uvs[i2].v - s.uvs[i0].v;
    const float r = du1 * dv2 - du2 * dv1;
    if (std::abs(r) < 1e-12f) continue;  // degenerate UVs carry no orientation
    const Vec3 dpdu = unit({(e1.x * dv2 - e2.x * dv1) / r, (e1.y * dv2 - e2.y * dv1) / r, (e1.z * dv2 - e2.z * dv1) / r});
    const Vec3 t_avg = unit({s.tangents[i0].x + s.tangents[i1].x + s.tangents[i2].x,
                             s.tangents[i0].y + s.tangents[i1].y + s.tangents[i2].y,
                             s.tangents[i0].z + s.tangents[i1].z + s.tangents[i2].z});
    const Vec3 b_avg = unit({s.bitangents[i0].x + s.bitangents[i1].x + s.bitangents[i2].x,
                             s.bitangents[i0].y + s.bitangents[i1].y + s.bitangents[i2].y,
                             s.bitangents[i0].z + s.bitangents[i1].z + s.bitangents[i2].z});
    along_tangent += std::abs(dot(dpdu, t_avg));
    along_bitangent += std::abs(dot(dpdu, b_avg));
  }
  if (along_bitangent > along_tangent * 1.2) {
    std::swap(s.tangents, s.bitangents);
    s.tangents_swapped = true;
  }
  return s.tangents_swapped;
}

MeshFile load_mesh(std::string_view bytes, std::string_view label) {
  nifly::NifFile nif;
  load_nif(nif, bytes, label);
  MeshFile file;

  auto check_extra_data = [&](auto& refs) {
    if (!file.physics_xml.empty()) return;
    for (const auto& ref : refs) {
      const auto* data = nif.GetHeader().GetBlock<nifly::NiStringExtraData>(ref);
      if (!data) continue;
      const auto name = data->name.get();
      const auto val = data->stringData.get();
      const auto name_lower = text::to_lower_ascii(name);
      const auto val_lower = text::to_lower_ascii(val);
      if (name_lower.find("hdt") != std::string::npos ||
          name_lower.find("smp") != std::string::npos ||
          name_lower.find("physics") != std::string::npos ||
          val_lower.ends_with(".xml")) {
        auto xml = text::data_path_key(text::trim(val));
        if (xml.starts_with("data\\")) xml.erase(0, 5);
        if (!xml.empty() && xml.ends_with(".xml")) {
          file.physics_xml = xml;
          break;
        }
      }
    }
  };

  if (auto* root = nif.GetRootNode()) {
    check_extra_data(root->extraDataRefs);
  }
  if (file.physics_xml.empty()) {
    for (auto* node : nif.GetNodes()) {
      check_extra_data(node->extraDataRefs);
      if (!file.physics_xml.empty()) break;
    }
  }

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
    s.hidden = hidden_in_chain(nif, shape);
    if (auto* parent = nif.GetParentNode(shape); parent && parent != nif.GetRootNode()) s.parent_node = parent->name.get();
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
    s.to_root = from(to_root);

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

    orient_tangent_basis(s);

    if (auto* shader = nif.GetShader(shape)) {
      s.has_shader = true;
      s.shader_block = shader->GetBlockName();
      s.has_texture_set = shader->HasTextureSet();
      if (const auto* bs = dynamic_cast<const nifly::BSShaderProperty*>(shader)) {
        s.shader_flags1 = bs->shaderFlags1;
        s.shader_flags2 = bs->shaderFlags2;
      }
      s.material_alpha = shader->GetAlpha();
      const auto offset = shader->GetUVOffset();
      const auto scale = shader->GetUVScale();
      s.uv_offset = {offset.u, offset.v};
      s.uv_scale = {scale.u, scale.v};
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
      s.alpha_flags = alpha->flags;
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
        nifly::MatTransform bone_global;
        s.mesh_bone_to_global.push_back(nif.GetNodeTransformToGlobal(s.bones[b], bone_global)
                                            ? std::optional<Transform>(from(bone_global)) : std::nullopt);
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
      if (const auto* ref = shape->SkinInstanceRef()) {
        if (const auto* skin = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(*ref)) {
          if (const auto* partition = nif.GetHeader().GetBlock(skin->skinPartitionRef)) {
            std::uint32_t total = 0;
            for (const auto& block : partition->partitions) total += block.numTriangles;
            s.partition_triangles = total;
          }
        }
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
