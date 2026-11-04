// Implementation of the geometric clipping and garment-fit analyzer for Visual QA Lab.
// Uses AABB broad-phase, dual-tree BVH traversal, exact 3D triangle-triangle intersection,
// outward normal protrusion measurement, anatomical zone clustering, and false-positive filtering.
#include "sns/visual_qa/clipping_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <numeric>

#include "sns/core/text.hpp"

namespace sns::visual_qa {

using nlohmann::json;

namespace {

[[nodiscard]] nif::Vec3 sub(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] nif::Vec3 add(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] nif::Vec3 scale_vec(const nif::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] float dot(const nif::Vec3& a, const nif::Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] nif::Vec3 cross(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] float length(const nif::Vec3& v) {
  return std::sqrt(dot(v, v));
}

[[nodiscard]] nif::Vec3 normalize(const nif::Vec3& v) {
  const float len = length(v);
  return len > 1e-8f ? scale_vec(v, 1.0f / len) : nif::Vec3{0.0f, 1.0f, 0.0f};
}

// Checks whether a 3D point p (already on the plane of triangle t0,t1,t2) lies inside the triangle.
[[nodiscard]] bool point_in_triangle_3d(const nif::Vec3& p, const nif::Vec3& t0, const nif::Vec3& t1,
                                        const nif::Vec3& t2) {
  const nif::Vec3 v0 = sub(t2, t0);
  const nif::Vec3 v1 = sub(t1, t0);
  const nif::Vec3 v2 = sub(p, t0);

  const float dot00 = dot(v0, v0);
  const float dot01 = dot(v0, v1);
  const float dot02 = dot(v0, v2);
  const float dot11 = dot(v1, v1);
  const float dot12 = dot(v1, v2);

  const float denom = dot00 * dot11 - dot01 * dot01;
  if (std::abs(denom) < 1e-10f) return false;
  const float inv = 1.0f / denom;
  const float u = (dot11 * dot02 - dot01 * dot12) * inv;
  const float v = (dot00 * dot12 - dot01 * dot02) * inv;
  constexpr float kBaryEps = -1e-4f;
  return (u >= kBaryEps) && (v >= kBaryEps) && (u + v <= 1.0f - kBaryEps);
}

// Extracts vertex position and normal from an interleaved RenderPart vertex buffer.
[[nodiscard]] nif::Vec3 part_pos(const rendering::RenderPart& part, std::uint32_t idx) {
  const float* v = part.vertices.data() + static_cast<std::size_t>(idx) * rendering::kFloatsPerVertex;
  return {v[0], v[1], v[2]};
}

[[nodiscard]] nif::Vec3 part_norm(const rendering::RenderPart& part, std::uint32_t idx) {
  const float* v = part.vertices.data() + static_cast<std::size_t>(idx) * rendering::kFloatsPerVertex;
  return {v[3], v[4], v[5]};
}

struct TriPrim {
  std::uint32_t tri_index = 0;
  nif::Vec3 v0{}, v1{}, v2{};
  nif::Vec3 normal{};
  nif::Vec3 centroid{};
  Aabb3D bounds{};
};

struct BvhNode {
  Aabb3D bounds{};
  int left = -1;
  int right = -1;
  std::size_t first = 0;
  std::size_t count = 0;

  [[nodiscard]] bool is_leaf() const { return left < 0 && right < 0; }
};

class TriangleBvh {
 public:
  explicit TriangleBvh(const rendering::RenderPart& part) {
    const std::size_t vert_count = part.vertices.size() / rendering::kFloatsPerVertex;
    const std::size_t tri_count = part.indices.size() / 3;
    prims_.reserve(tri_count);
    for (std::size_t t = 0; t < tri_count; ++t) {
      const std::uint32_t i0 = part.indices[t * 3];
      const std::uint32_t i1 = part.indices[t * 3 + 1];
      const std::uint32_t i2 = part.indices[t * 3 + 2];
      if (i0 >= vert_count || i1 >= vert_count || i2 >= vert_count) continue;
      TriPrim p;
      p.tri_index = static_cast<std::uint32_t>(t);
      p.v0 = part_pos(part, i0);
      p.v1 = part_pos(part, i1);
      p.v2 = part_pos(part, i2);
      if (!std::isfinite(p.v0.x) || !std::isfinite(p.v0.y) || !std::isfinite(p.v0.z) ||
          !std::isfinite(p.v1.x) || !std::isfinite(p.v1.y) || !std::isfinite(p.v1.z) ||
          !std::isfinite(p.v2.x) || !std::isfinite(p.v2.y) || !std::isfinite(p.v2.z)) {
        continue;
      }
      const nif::Vec3 n_sum = add(add(part_norm(part, i0), part_norm(part, i1)), part_norm(part, i2));
      const nif::Vec3 geom_n = cross(sub(p.v1, p.v0), sub(p.v2, p.v0));
      p.normal = length(n_sum) > 1e-4f ? normalize(n_sum) : normalize(geom_n);
      p.centroid = scale_vec(add(add(p.v0, p.v1), p.v2), 1.0f / 3.0f);
      p.bounds.min = p.bounds.max = {p.v0.x, p.v0.y, p.v0.z};
      p.bounds.expand(p.v1);
      p.bounds.expand(p.v2);
      prims_.push_back(p);
    }
    if (!prims_.empty()) {
      nodes_.reserve(prims_.size() * 2);
      build_recursive(0, prims_.size());
    }
  }

  [[nodiscard]] bool empty() const { return nodes_.empty(); }
  [[nodiscard]] const std::vector<TriPrim>& prims() const { return prims_; }
  [[nodiscard]] const std::vector<BvhNode>& nodes() const { return nodes_; }

 private:
  int build_recursive(std::size_t first, std::size_t count) {
    const int node_idx = static_cast<int>(nodes_.size());
    nodes_.push_back({});

    Aabb3D box = prims_[first].bounds;
    for (std::size_t i = first + 1; i < first + count; ++i) {
      box.expand(prims_[i].bounds);
    }
    nodes_[static_cast<std::size_t>(node_idx)].bounds = box;

    constexpr std::size_t kMaxLeafSize = 8;
    if (count <= kMaxLeafSize) {
      nodes_[static_cast<std::size_t>(node_idx)].first = first;
      nodes_[static_cast<std::size_t>(node_idx)].count = count;
      return node_idx;
    }

    const nif::Vec3 ext = box.size();
    int axis = 0;
    if (ext.y > ext.x && ext.y >= ext.z) axis = 1;
    else if (ext.z > ext.x && ext.z > ext.y) axis = 2;

    const std::size_t mid = first + count / 2;
    auto coord = [axis](const TriPrim& p) {
      return axis == 0 ? p.centroid.x : (axis == 1 ? p.centroid.y : p.centroid.z);
    };
    std::nth_element(prims_.begin() + static_cast<std::ptrdiff_t>(first),
                     prims_.begin() + static_cast<std::ptrdiff_t>(mid),
                     prims_.begin() + static_cast<std::ptrdiff_t>(first + count),
                     [&](const TriPrim& a, const TriPrim& b) { return coord(a) < coord(b); });

    const int left_idx = build_recursive(first, mid - first);
    const int right_idx = build_recursive(mid, first + count - mid);
    nodes_[static_cast<std::size_t>(node_idx)].left = left_idx;
    nodes_[static_cast<std::size_t>(node_idx)].right = right_idx;
    return node_idx;
  }

  std::vector<TriPrim> prims_;
  std::vector<BvhNode> nodes_;
};

// Checks if a shape name or texture inside an outfit NIF represents an embedded body/hand/foot skin reference mesh.
[[nodiscard]] bool is_embedded_body_shape(const rendering::RenderPart& part) {
  if (part.shader_type == nif::kShaderSkinTint) return true;
  const std::string sname = text::to_lower_ascii(part.shape_name);
  if (sname == "3ba" || sname == "3bbb" || sname == "cbbe" || sname == "baseshape" ||
      sname == "ube" || sname == "ube_body" || sname == "body" || sname == "femalebody" ||
      sname == "hands" || sname == "femalehands" || sname == "feet" || sname == "femalefeet" ||
      sname == "3ba_vagina" || sname == "3ba_anus" || sname.starts_with("3ba_")) {
    return true;
  }
  const std::string diff = text::to_lower_ascii(part.original_diffuse);
  if (diff.find("femalebody") != std::string::npos || diff.find("femalehands") != std::string::npos ||
      diff.find("femalefeet") != std::string::npos) {
    return true;
  }
  return false;
}

[[nodiscard]] bool is_genital_subshape(std::string_view shape_name) {
  const std::string s = text::to_lower_ascii(shape_name);
  return s.find("vagina") != std::string::npos || s.find("anus") != std::string::npos ||
         s.find("labia") != std::string::npos;
}

[[nodiscard]] bool is_limb_joint_zone(AnatomicalZone z) {
  return z == AnatomicalZone::ShoulderUpperArmLeft || z == AnatomicalZone::ShoulderUpperArmRight ||
         z == AnatomicalZone::ForearmWristLeft || z == AnatomicalZone::ForearmWristRight;
}

}  // namespace

// ---- Shared visual_qa_types.hpp functions -----------------------------------------------------

std::string_view to_string(QaVerdict verdict) {
  switch (verdict) {
    case QaVerdict::Pass: return "PASS";
    case QaVerdict::Fail: return "FAIL";
    case QaVerdict::Skip: return "SKIP";
    case QaVerdict::Inconclusive: return "INCONCLUSIVE";
  }
  return "INCONCLUSIVE";
}

QaVerdict combine_verdicts(QaVerdict a, QaVerdict b) {
  if (a == QaVerdict::Fail || b == QaVerdict::Fail) return QaVerdict::Fail;
  if (a == QaVerdict::Inconclusive || b == QaVerdict::Inconclusive) return QaVerdict::Inconclusive;
  if (a == QaVerdict::Pass || b == QaVerdict::Pass) return QaVerdict::Pass;
  return QaVerdict::Skip;
}

std::string_view to_string(AnatomicalZone zone) {
  switch (zone) {
    case AnatomicalZone::Head: return "Head";
    case AnatomicalZone::Neck: return "Neck";
    case AnatomicalZone::ChestBreasts: return "Chest_Breasts";
    case AnatomicalZone::UpperBack: return "Upper_Back";
    case AnatomicalZone::WaistAbdomen: return "Waist_Abdomen";
    case AnatomicalZone::PelvisGroin: return "Pelvis_Groin";
    case AnatomicalZone::ButtGlutes: return "Butt_Glutes";
    case AnatomicalZone::ShoulderUpperArmLeft: return "Shoulder_UpperArm_L";
    case AnatomicalZone::ShoulderUpperArmRight: return "Shoulder_UpperArm_R";
    case AnatomicalZone::ForearmWristLeft: return "Forearm_Wrist_L";
    case AnatomicalZone::ForearmWristRight: return "Forearm_Wrist_R";
    case AnatomicalZone::HandLeft: return "Hand_L";
    case AnatomicalZone::HandRight: return "Hand_R";
    case AnatomicalZone::ThighLeft: return "Thigh_L";
    case AnatomicalZone::ThighRight: return "Thigh_R";
    case AnatomicalZone::CalfAnkleLeft: return "Calf_Ankle_L";
    case AnatomicalZone::CalfAnkleRight: return "Calf_Ankle_R";
    case AnatomicalZone::FootLeft: return "Foot_L";
    case AnatomicalZone::FootRight: return "Foot_R";
    case AnatomicalZone::CloakOrAccessory: return "Cloak_Accessory";
  }
  return "Unknown";
}

AnatomicalZone classify_anatomical_zone(const nif::Vec3& view_pos, float char_min_y, float char_max_y) {
  const float span = (char_max_y - char_min_y) > 40.0f ? (char_max_y - char_min_y) : 124.0f;
  const float base = (char_max_y - char_min_y) > 40.0f ? char_min_y : 0.0f;
  const float yn = (view_pos.y - base) / span;
  const float ax = std::abs(view_pos.x);
  const bool left_side = view_pos.x >= 0.0f;  // view.x = -skyrim.x

  if (yn > 0.855f && ax < 14.0f) return AnatomicalZone::Head;
  if (yn >= 0.795f && yn <= 0.855f && ax < 10.0f) return AnatomicalZone::Neck;

  // Arms and hands extend laterally in reference and A-pose.
  if (yn >= 0.34f && yn <= 0.85f && ax >= 12.0f) {
    if (ax >= 22.5f) return left_side ? AnatomicalZone::HandLeft : AnatomicalZone::HandRight;
    if (ax >= 16.2f) return left_side ? AnatomicalZone::ForearmWristLeft : AnatomicalZone::ForearmWristRight;
    return left_side ? AnatomicalZone::ShoulderUpperArmLeft : AnatomicalZone::ShoulderUpperArmRight;
  }

  if (yn >= 0.65f) {
    return view_pos.z >= -1.0f ? AnatomicalZone::ChestBreasts : AnatomicalZone::UpperBack;
  }
  if (yn >= 0.535f) {
    return AnatomicalZone::WaistAbdomen;
  }
  if (yn >= 0.43f) {
    return view_pos.z < -1.5f ? AnatomicalZone::ButtGlutes : AnatomicalZone::PelvisGroin;
  }
  if (yn >= 0.25f) {
    return left_side ? AnatomicalZone::ThighLeft : AnatomicalZone::ThighRight;
  }
  if (yn >= 0.075f) {
    return left_side ? AnatomicalZone::CalfAnkleLeft : AnatomicalZone::CalfAnkleRight;
  }
  return left_side ? AnatomicalZone::FootLeft : AnatomicalZone::FootRight;
}

std::string_view to_string(SurfaceRole role) {
  switch (role) {
    case SurfaceRole::VisibleBodySkin: return "VISIBLE_BODY_SKIN";
    case SurfaceRole::EmbeddedBodyInOutfit: return "EMBEDDED_BODY_IN_OUTFIT";
    case SurfaceRole::VisibleHeadSkin: return "VISIBLE_HEAD_SKIN";
    case SurfaceRole::VisibleGarment: return "VISIBLE_GARMENT";
    case SurfaceRole::VisibleHeadPart: return "VISIBLE_HEAD_PART";
    case SurfaceRole::CoveredBodyBySlot: return "COVERED_BODY_BY_SLOT";
    case SurfaceRole::HiddenNifShape: return "HIDDEN_NIF_SHAPE";
    case SurfaceRole::SmpCollisionProxy: return "SMP_COLLISION_PROXY";
    case SurfaceRole::UnequippedOutfitPiece: return "UNEQUIPPED_OUTFIT_PIECE";
  }
  return "VISIBLE_GARMENT";
}

SurfaceRole classify_part_role(const rendering::RenderPart& part) {
  const std::string sname_lower = text::to_lower_ascii(part.shape_name);
  const std::string reason_lower = text::to_lower_ascii(part.hidden_reason);

  const bool in_collision_list = std::any_of(
      part.collision_shapes.begin(), part.collision_shapes.end(),
      [&](const std::string& c) { return text::iequals_ascii(c, part.shape_name); });

  if (!part.shown_by_default) {
    if (reason_lower.find("covered by outfit") != std::string::npos) {
      return SurfaceRole::CoveredBodyBySlot;
    }
    if (reason_lower.find("unequipped:") != std::string::npos) {
      return SurfaceRole::UnequippedOutfitPiece;
    }
    if (reason_lower.find("smp collision") != std::string::npos || in_collision_list ||
        sname_lower == "virtualground" || sname_lower == "topc" || sname_lower == "pantsc" ||
        sname_lower.find("cphy") != std::string::npos || sname_lower.find("collision") != std::string::npos) {
      return SurfaceRole::SmpCollisionProxy;
    }
    return SurfaceRole::HiddenNifShape;
  }

  if (sname_lower == "virtualground" ||
      (in_collision_list && part.original_diffuse.empty())) {
    return SurfaceRole::SmpCollisionProxy;
  }

  if (part.group == "skin") {
    return SurfaceRole::VisibleBodySkin;
  }
  if (part.group == "head") {
    const bool is_head_mesh =
        (part.shader_type == nif::kShaderFaceTint) ||
        (sname_lower.find("head") != std::string::npos &&
         sname_lower.find("hair") == std::string::npos &&
         sname_lower.find("brow") == std::string::npos &&
         sname_lower.find("eye") == std::string::npos &&
         sname_lower.find("mouth") == std::string::npos &&
         sname_lower.find("horn") == std::string::npos);
    return is_head_mesh ? SurfaceRole::VisibleHeadSkin : SurfaceRole::VisibleHeadPart;
  }
  if (part.group == "outfit") {
    if (is_embedded_body_shape(part)) {
      return SurfaceRole::EmbeddedBodyInOutfit;
    }
    return SurfaceRole::VisibleGarment;
  }
  return SurfaceRole::VisibleGarment;
}

std::string_view to_string(ClippingClassification cls) {
  switch (cls) {
    case ClippingClassification::GeometricIntersectionDetected: return "GEOMETRIC_INTERSECTION_DETECTED";
    case ClippingClassification::PossibleVisibleClipping: return "POSSIBLE_VISIBLE_CLIPPING";
    case ClippingClassification::ConfirmedVisualClipping: return "CONFIRMED_VISUAL_CLIPPING";
    case ClippingClassification::Indeterminate: return "INDETERMINATE";
    case ClippingClassification::ExcludedCoveredByBipedSlot: return "EXCLUDED_COVERED_BY_BIPED_SLOT";
    case ClippingClassification::ExcludedHiddenNifShape: return "EXCLUDED_HIDDEN_NIF_SHAPE";
    case ClippingClassification::ExcludedSmpCollision: return "EXCLUDED_SMP_COLLISION";
    case ClippingClassification::ExcludedIntentionalOverlap: return "EXCLUDED_INTENTIONAL_OVERLAP";
    case ClippingClassification::ExcludedReferencePoseOnly: return "EXCLUDED_REFERENCE_POSE_ONLY";
  }
  return "INDETERMINATE";
}

json to_json(const VisualQaIssue& issue) {
  return json{
      {"severity", issue.severity},
      {"code", issue.code},
      {"category", issue.category},
      {"message", issue.message},
      {"meshPath", issue.mesh_path},
      {"shapeName", issue.shape_name},
      {"pieceLabel", issue.piece_label},
      {"armorFormKey", issue.armor_form_key},
      {"addonFormKey", issue.addon_form_key},
      {"provider", issue.provider},
      {"zone", issue.zone}};
}

// ---- Aabb3D & Triangle-Triangle Intersection --------------------------------------------------

void Aabb3D::expand(const nif::Vec3& p) {
  min[0] = std::min(min[0], p.x);
  min[1] = std::min(min[1], p.y);
  min[2] = std::min(min[2], p.z);
  max[0] = std::max(max[0], p.x);
  max[1] = std::max(max[1], p.y);
  max[2] = std::max(max[2], p.z);
}

void Aabb3D::expand(const Aabb3D& other) {
  if (!other.valid()) return;
  min[0] = std::min(min[0], other.min[0]);
  min[1] = std::min(min[1], other.min[1]);
  min[2] = std::min(min[2], other.min[2]);
  max[0] = std::max(max[0], other.max[0]);
  max[1] = std::max(max[1], other.max[1]);
  max[2] = std::max(max[2], other.max[2]);
}

TriangleIntersectionResult test_triangle_triangle_intersection(
    const nif::Vec3& a0, const nif::Vec3& a1, const nif::Vec3& a2, const nif::Vec3& norm_a,
    const nif::Vec3& b0, const nif::Vec3& b1, const nif::Vec3& b2, const nif::Vec3& norm_b) {
  TriangleIntersectionResult res;

  // Quick triangle AABB test
  const float min_ax = std::min({a0.x, a1.x, a2.x}), max_ax = std::max({a0.x, a1.x, a2.x});
  const float min_bx = std::min({b0.x, b1.x, b2.x}), max_bx = std::max({b0.x, b1.x, b2.x});
  if (min_ax > max_bx || max_ax < min_bx) return res;

  const float min_ay = std::min({a0.y, a1.y, a2.y}), max_ay = std::max({a0.y, a1.y, a2.y});
  const float min_by = std::min({b0.y, b1.y, b2.y}), max_by = std::max({b0.y, b1.y, b2.y});
  if (min_ay > max_by || max_ay < min_by) return res;

  const float min_az = std::min({a0.z, a1.z, a2.z}), max_az = std::max({a0.z, a1.z, a2.z});
  const float min_bz = std::min({b0.z, b1.z, b2.z}), max_bz = std::max({b0.z, b1.z, b2.z});
  if (min_az > max_bz || max_az < min_bz) return res;

  // Plane of B (garment)
  nif::Vec3 nb = cross(sub(b1, b0), sub(b2, b0));
  const float len_nb = length(nb);
  if (len_nb < 1e-8f) return res;
  nb = scale_vec(nb, 1.0f / len_nb);
  // Orient nb to agree with the outward normal norm_b (or norm_a when norm_b is zero).
  const nif::Vec3 ref_outward = length(norm_b) > 1e-4f ? norm_b : norm_a;
  if (dot(nb, ref_outward) < 0.0f) nb = scale_vec(nb, -1.0f);

  const float da[3] = {dot(sub(a0, b0), nb), dot(sub(a1, b0), nb), dot(sub(a2, b0), nb)};
  constexpr float kPlaneEps = 1e-5f;
  if ((da[0] > kPlaneEps && da[1] > kPlaneEps && da[2] > kPlaneEps) ||
      (da[0] < -kPlaneEps && da[1] < -kPlaneEps && da[2] < -kPlaneEps)) {
    return res;
  }

  // Plane of A (body)
  nif::Vec3 na = cross(sub(a1, a0), sub(a2, a0));
  const float len_na = length(na);
  if (len_na < 1e-8f) return res;
  na = scale_vec(na, 1.0f / len_na);
  if (length(norm_a) > 1e-4f && dot(na, norm_a) < 0.0f) na = scale_vec(na, -1.0f);

  const float db[3] = {dot(sub(b0, a0), na), dot(sub(b1, a0), na), dot(sub(b2, a0), na)};
  if ((db[0] > kPlaneEps && db[1] > kPlaneEps && db[2] > kPlaneEps) ||
      (db[0] < -kPlaneEps && db[1] < -kPlaneEps && db[2] < -kPlaneEps)) {
    return res;
  }

  std::vector<nif::Vec3> pierce_points;
  pierce_points.reserve(6);

  const nif::Vec3 va[3] = {a0, a1, a2};
  for (int e = 0; e < 3; ++e) {
    const int i = e;
    const int j = (e + 1) % 3;
    if ((da[i] > kPlaneEps && da[j] < -kPlaneEps) || (da[i] < -kPlaneEps && da[j] > kPlaneEps)) {
      const float t = da[i] / (da[i] - da[j]);
      const nif::Vec3 p = add(va[i], scale_vec(sub(va[j], va[i]), t));
      if (point_in_triangle_3d(p, b0, b1, b2)) {
        pierce_points.push_back(p);
      }
    }
  }

  const nif::Vec3 vb[3] = {b0, b1, b2};
  for (int e = 0; e < 3; ++e) {
    const int i = e;
    const int j = (e + 1) % 3;
    if ((db[i] > kPlaneEps && db[j] < -kPlaneEps) || (db[i] < -kPlaneEps && db[j] > kPlaneEps)) {
      const float t = db[i] / (db[i] - db[j]);
      const nif::Vec3 q = add(vb[i], scale_vec(sub(vb[j], vb[i]), t));
      if (point_in_triangle_3d(q, a0, a1, a2)) {
        pierce_points.push_back(q);
      }
    }
  }

  if (pierce_points.size() < 2) return res;

  // Find the two most distant piercing points forming the intersection segment.
  float best_dist_sq = 0.0f;
  std::size_t best_i = 0, best_j = 1;
  for (std::size_t i = 0; i < pierce_points.size(); ++i) {
    for (std::size_t j = i + 1; j < pierce_points.size(); ++j) {
      const nif::Vec3 d = sub(pierce_points[j], pierce_points[i]);
      const float d2 = dot(d, d);
      if (d2 > best_dist_sq) {
        best_dist_sq = d2;
        best_i = i;
        best_j = j;
      }
    }
  }

  if (best_dist_sq < 1e-8f) return res;

  res.intersects = true;
  res.point_a = pierce_points[best_i];
  res.point_b = pierce_points[best_j];
  res.segment_length = std::sqrt(best_dist_sq);

  // Exterior protrusion: maximum positive signed distance of body triangle A's vertices
  // onto the outward (+nb) side of garment triangle B.
  float max_ext = 0.0f;
  for (int k = 0; k < 3; ++k) {
    if (da[k] > max_ext) max_ext = da[k];
  }
  res.exterior_protrusion = max_ext;
  return res;
}

namespace {

// Evaluates radial fit between a garment shape and a reference body shape across vertical slices.
[[nodiscard]] GarmentFitDiagnostic evaluate_garment_fit(
    const rendering::RenderPart& garment,
    const rendering::RenderPart& body_ref,
    float max_normal_clearance) {
  GarmentFitDiagnostic fit;
  fit.garment_part_id = garment.id;
  fit.garment_piece_label = garment.label;
  fit.armor_form_key = garment.armor_form_key;
  fit.garment_mesh_path = garment.mesh_path;
  fit.garment_shape_name = garment.shape_name;
  fit.provider = garment.mesh_location;
  fit.body_reference_shape = body_ref.shape_name + " (" + body_ref.mesh_path + ")";

  const float overlap_min_y = std::max(garment.min[1], body_ref.min[1]);
  const float overlap_max_y = std::min(garment.max[1], body_ref.max[1]);
  if (overlap_max_y - overlap_min_y < 2.0f) {
    fit.fit_verdict = "NO_OVERLAPPING_BODY";
    fit.explanation = "La prenda no comparte rango vertical suficiente con la malla corporal de referencia.";
    return fit;
  }

  constexpr int kNumSlices = 12;
  constexpr int kNumSectors = 16;  // Angular sectors around the vertical Y axis
  const float slice_h = (overlap_max_y - overlap_min_y) / static_cast<float>(kNumSlices);

  // Compute slice axis (x_center, z_center) from body_ref in each slice, then compare max radial distance per sector.
  std::vector<std::array<float, kNumSectors>> body_rad(kNumSlices);
  std::vector<std::array<float, kNumSectors>> garm_rad(kNumSlices);
  for (int s = 0; s < kNumSlices; ++s) {
    body_rad[static_cast<std::size_t>(s)].fill(-1.0f);
    garm_rad[static_cast<std::size_t>(s)].fill(-1.0f);
  }

  const std::size_t b_verts = body_ref.vertices.size() / rendering::kFloatsPerVertex;
  for (std::size_t i = 0; i < b_verts; ++i) {
    const nif::Vec3 p = part_pos(body_ref, static_cast<std::uint32_t>(i));
    if (p.y < overlap_min_y || p.y > overlap_max_y) continue;
    // Focus on torso/pelvis/legs (|x| < 18) so extended arms don't skew radial cylinder slices
    if (std::abs(p.x) > 18.0f && p.y > 48.0f) continue;
    const int s = std::clamp(static_cast<int>((p.y - overlap_min_y) / slice_h), 0, kNumSlices - 1);
    const float angle = std::atan2(p.z, p.x);  // -pi .. pi
    const int sec = std::clamp(static_cast<int>((angle + 3.14159265f) / (2.0f * 3.14159265f) * kNumSectors), 0, kNumSectors - 1);
    const float r = std::sqrt(p.x * p.x + p.z * p.z);
    body_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)] =
        std::max(body_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)], r);
  }

  const std::size_t g_verts = garment.vertices.size() / rendering::kFloatsPerVertex;
  for (std::size_t i = 0; i < g_verts; ++i) {
    const nif::Vec3 p = part_pos(garment, static_cast<std::uint32_t>(i));
    if (p.y < overlap_min_y || p.y > overlap_max_y) continue;
    if (std::abs(p.x) > 18.0f && p.y > 48.0f) continue;
    const int s = std::clamp(static_cast<int>((p.y - overlap_min_y) / slice_h), 0, kNumSlices - 1);
    const float angle = std::atan2(p.z, p.x);
    const int sec = std::clamp(static_cast<int>((angle + 3.14159265f) / (2.0f * 3.14159265f) * kNumSectors), 0, kNumSectors - 1);
    const float r = std::sqrt(p.x * p.x + p.z * p.z);
    garm_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)] =
        std::max(garm_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)], r);
  }

  float sum_clearance = 0.0f;
  float sum_ratio = 0.0f;
  float min_c = std::numeric_limits<float>::infinity();
  float max_c = -std::numeric_limits<float>::infinity();
  std::size_t valid_samples = 0;

  for (int s = 0; s < kNumSlices; ++s) {
    for (int sec = 0; sec < kNumSectors; ++sec) {
      const float br = body_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)];
      const float gr = garm_rad[static_cast<std::size_t>(s)][static_cast<std::size_t>(sec)];
      if (br > 1.5f && gr > 1.5f) {
        const float c = gr - br;
        sum_clearance += c;
        sum_ratio += gr / br;
        min_c = std::min(min_c, c);
        max_c = std::max(max_c, c);
        ++valid_samples;
      }
    }
  }

  if (valid_samples == 0) {
    fit.fit_verdict = "NO_OVERLAPPING_BODY";
    fit.explanation = "Sin sectores cilíndricos comparables entre prenda y cuerpo.";
    return fit;
  }

  fit.slices_compared = valid_samples;
  fit.mean_radial_clearance = sum_clearance / static_cast<float>(valid_samples);
  fit.min_radial_clearance = min_c;
  fit.max_radial_clearance = max_c;
  fit.scale_ratio_to_body = sum_ratio / static_cast<float>(valid_samples);

  const std::string sname_l = text::to_lower_ascii(garment.shape_name);
  const bool is_cloak_or_skirt = sname_l.find("cloak") != std::string::npos ||
                                 sname_l.find("cape") != std::string::npos ||
                                 sname_l.find("skirt") != std::string::npos;

  if (fit.scale_ratio_to_body < 0.96f || fit.mean_radial_clearance < -0.35f) {
    fit.fit_verdict = "TOO_SMALL_CLIPPING";
    fit.explanation = std::format(
        "La prenda '{}' tiene una envolvente radial menor que el cuerpo (ratio {:.3f}, holgura media {:.2f} u, mín {:.2f} u): "
        "prenda demasiado pequeña o construida para un preset más delgado.",
        garment.shape_name, fit.scale_ratio_to_body, fit.mean_radial_clearance, fit.min_radial_clearance);
  } else if (!is_cloak_or_skirt && (fit.scale_ratio_to_body > 1.38f || fit.mean_radial_clearance > max_normal_clearance)) {
    fit.fit_verdict = "TOO_LARGE_FLOATING";
    fit.explanation = std::format(
        "La prenda '{}' excede ampliamente el contorno corporal (ratio {:.3f}, holgura media {:.2f} u): "
        "prenda demasiado grande o desajustada para el cuerpo.",
        garment.shape_name, fit.scale_ratio_to_body, fit.mean_radial_clearance);
  } else {
    fit.fit_verdict = "WELL_FITTED";
    fit.explanation = std::format(
        "Proporción prenda/cuerpo dentro de rango (ratio {:.3f}, holgura radial media {:.2f} u).",
        fit.scale_ratio_to_body, fit.mean_radial_clearance);
  }
  return fit;
}

}  // namespace

ClippingAnalysisReport analyze_scene_clipping(
    const rendering::CharacterScene& scene,
    const ClippingAnalysisOptions& options) {
  ClippingAnalysisReport report;
  report.parts_inspected = scene.parts.size();

  const float char_min_y = scene.min[1];
  const float char_max_y = scene.max[1];

  std::vector<std::size_t> visible_body_indices;
  std::vector<std::size_t> covered_body_indices;
  std::vector<std::size_t> visible_garment_indices;
  std::vector<SurfaceRole> roles(scene.parts.size());

  for (std::size_t i = 0; i < scene.parts.size(); ++i) {
    const auto& part = scene.parts[i];
    const SurfaceRole role = classify_part_role(part);
    roles[i] = role;

    if (role == SurfaceRole::HiddenNifShape || role == SurfaceRole::SmpCollisionProxy ||
        role == SurfaceRole::UnequippedOutfitPiece || role == SurfaceRole::CoveredBodyBySlot) {
      ++report.excluded_parts;
      ClippingClassification excl_cls = ClippingClassification::ExcludedHiddenNifShape;
      if (role == SurfaceRole::CoveredBodyBySlot) {
        excl_cls = ClippingClassification::ExcludedCoveredByBipedSlot;
        covered_body_indices.push_back(i);
      } else if (role == SurfaceRole::SmpCollisionProxy) {
        excl_cls = ClippingClassification::ExcludedSmpCollision;
      } else if (role == SurfaceRole::UnequippedOutfitPiece) {
        excl_cls = ClippingClassification::ExcludedIntentionalOverlap;
      }
      report.excluded_surfaces.push_back({
          part.id, part.shape_name, part.mesh_path, part.label, role, excl_cls,
          part.hidden_reason.empty() ? std::string(to_string(role)) : part.hidden_reason});
      continue;
    }

    if (role == SurfaceRole::VisibleBodySkin || role == SurfaceRole::EmbeddedBodyInOutfit) {
      // Genital sub-meshes (3BA_Vagina, 3BA_Anus) are internal anatomical sub-parts attached to 3BA;
      // only the primary body/hand/foot surfaces participate in exterior garment clipping checks.
      if (is_genital_subshape(part.shape_name)) {
        report.excluded_surfaces.push_back({
            part.id, part.shape_name, part.mesh_path, part.label, role,
            ClippingClassification::ExcludedIntentionalOverlap,
            "Sub-malla anatómica interna (3BA subshape) solapada por diseño con el cuerpo base."});
        ++report.intentional_overlap_excluded_count;
        continue;
      }
      visible_body_indices.push_back(i);
      ++report.visible_body_parts;
    } else if (role == SurfaceRole::VisibleGarment) {
      visible_garment_indices.push_back(i);
      ++report.visible_garment_parts;
    }
  }

  // Build BVHs only for parts that participate in tests.
  std::map<std::size_t, TriangleBvh> bvh_cache;
  auto get_bvh = [&](std::size_t part_idx) -> const TriangleBvh& {
    auto it = bvh_cache.find(part_idx);
    if (it == bvh_cache.end()) {
      it = bvh_cache.emplace(part_idx, TriangleBvh(scene.parts[part_idx])).first;
    }
    return it->second;
  };

  // Pick a primary reference body (either a visible body skin / embedded 3BA torso, or the covered slot-32 skin body)
  // so every garment gets a radial fit evaluation even when the naked body is hidden by slot 32.
  const rendering::RenderPart* primary_torso_body = nullptr;
  for (std::size_t idx : visible_body_indices) {
    const auto& p = scene.parts[idx];
    if ((p.max[1] - p.min[1]) > 45.0f) {
      primary_torso_body = &p;
      break;
    }
  }
  if (!primary_torso_body) {
    for (std::size_t idx : covered_body_indices) {
      const auto& p = scene.parts[idx];
      if ((p.max[1] - p.min[1]) > 45.0f) {
        primary_torso_body = &p;
        break;
      }
    }
  }

  // Also count how many intersections would have occurred on covered skin addons if not filtered,
  // proving that false-positive Biped slot filtering is active and accounted for.
  if (options.analyze_covered_skin_fit) {
    for (std::size_t cov_idx : covered_body_indices) {
      const auto& cov_part = scene.parts[cov_idx];
      const Aabb3D cov_box{cov_part.min, cov_part.max};
      for (std::size_t g_idx : visible_garment_indices) {
        const auto& g_part = scene.parts[g_idx];
        const Aabb3D g_box{g_part.min, g_part.max};
        if (cov_box.overlaps(g_box)) {
          ++report.covered_slot_excluded_intersections;
        }
      }
    }
  }

  // Evaluate each visible garment part against all visible body surfaces.
  for (std::size_t g_idx : visible_garment_indices) {
    const auto& garment = scene.parts[g_idx];
    const Aabb3D garment_box{garment.min, garment.max};

    GarmentClippingDiagnostic g_diag;
    g_diag.piece_label = garment.label;
    g_diag.armor_form_key = garment.armor_form_key;
    g_diag.mesh_path = garment.mesh_path;
    g_diag.shape_name = garment.shape_name;
    g_diag.provider = garment.mesh_location;
    g_diag.biped_slots = rendering::slots_from_mask(garment.slot_mask);
    if (primary_torso_body) {
      g_diag.fit = evaluate_garment_fit(garment, *primary_torso_body, options.max_normal_garment_clearance);
    }

    for (std::size_t b_idx : visible_body_indices) {
      const auto& body = scene.parts[b_idx];
      const Aabb3D body_box{body.min, body.max};
      ++report.broad_phase_pairs_tested;

      // 1. Broad phase: AABB overlap check
      if (!garment_box.overlaps(body_box)) continue;
      ++report.broad_phase_pairs_overlapping;

      // Exclude hands embedded inside a glove NIF when paired against another piece's forearm unless protruding,
      // but DO test embedded body shapes (e.g. 3BA in Shirt) against both Shirt and other garments (Corset, Pants, Boots).
      const auto& bvh_body = get_bvh(b_idx);
      const auto& bvh_garm = get_bvh(g_idx);
      if (bvh_body.empty() || bvh_garm.empty()) continue;

      // 2. Mid phase: Dual-BVH traversal collecting candidate triangle pairs
      struct ZoneAccumulator {
        std::size_t intersecting_pairs = 0;
        std::size_t protruding_tris = 0;
        float max_protrusion = 0.0f;
        float sum_protrusion = 0.0f;
        float max_seg_len = 0.0f;
        nif::Vec3 sum_pos{0.0f, 0.0f, 0.0f};
        Aabb3D bounds{};
        bool bounds_init = false;
      };
      std::map<AnatomicalZone, ZoneAccumulator> zone_map;

      const auto& nodes_a = bvh_body.nodes();
      const auto& prims_a = bvh_body.prims();
      const auto& nodes_b = bvh_garm.nodes();
      const auto& prims_b = bvh_garm.prims();

      std::vector<std::pair<int, int>> stack;
      stack.reserve(128);
      stack.emplace_back(0, 0);

      while (!stack.empty()) {
        const auto [na_idx, nb_idx] = stack.back();
        stack.pop_back();

        const BvhNode& na = nodes_a[static_cast<std::size_t>(na_idx)];
        const BvhNode& nb = nodes_b[static_cast<std::size_t>(nb_idx)];
        if (!na.bounds.overlaps(nb.bounds)) continue;

        if (na.is_leaf() && nb.is_leaf()) {
          for (std::size_t ia = na.first; ia < na.first + na.count; ++ia) {
            const TriPrim& ta = prims_a[ia];
            for (std::size_t ib = nb.first; ib < nb.first + nb.count; ++ib) {
              const TriPrim& tb = prims_b[ib];
              if (!ta.bounds.overlaps(tb.bounds)) continue;
              ++report.bvh_candidate_tri_pairs;

              // 3. Narrow phase: Exact 3D triangle-triangle intersection
              const auto tri_res = test_triangle_triangle_intersection(
                  ta.v0, ta.v1, ta.v2, ta.normal, tb.v0, tb.v1, tb.v2, tb.normal);
              if (!tri_res.intersects) continue;

              ++report.exact_intersecting_tri_pairs;
              const nif::Vec3 mid = scale_vec(add(tri_res.point_a, tri_res.point_b), 0.5f);
              const AnatomicalZone zone = classify_anatomical_zone(mid, char_min_y, char_max_y);

              auto& acc = zone_map[zone];
              ++acc.intersecting_pairs;
              if (tri_res.exterior_protrusion >= options.min_protrusion_possible) {
                ++acc.protruding_tris;
              }
              acc.max_protrusion = std::max(acc.max_protrusion, tri_res.exterior_protrusion);
              acc.sum_protrusion += tri_res.exterior_protrusion;
              acc.max_seg_len = std::max(acc.max_seg_len, tri_res.segment_length);
              acc.sum_pos = add(acc.sum_pos, mid);
              if (!acc.bounds_init) {
                acc.bounds.min = acc.bounds.max = {mid.x, mid.y, mid.z};
                acc.bounds_init = true;
              }
              acc.bounds.expand(tri_res.point_a);
              acc.bounds.expand(tri_res.point_b);
            }
          }
        } else if (na.is_leaf()) {
          stack.emplace_back(na_idx, nb.left);
          stack.emplace_back(na_idx, nb.right);
        } else if (nb.is_leaf()) {
          stack.emplace_back(na.left, nb_idx);
          stack.emplace_back(na.right, nb_idx);
        } else {
          stack.emplace_back(na.left, nb.left);
          stack.emplace_back(na.left, nb.right);
          stack.emplace_back(na.right, nb.left);
          stack.emplace_back(na.right, nb.right);
        }
      }

      // 4. Convert accumulated per-zone clusters into structured incidents with false-positive awareness
      for (const auto& [zone, acc] : zone_map) {
        if (acc.intersecting_pairs == 0) continue;

        ZoneClippingIncident inc;
        inc.zone = zone;
        inc.body_part_id = body.id;
        inc.body_shape_name = body.shape_name;
        inc.body_mesh_path = body.mesh_path;
        inc.body_role = roles[b_idx];
        inc.garment_part_id = garment.id;
        inc.garment_shape_name = garment.shape_name;
        inc.garment_mesh_path = garment.mesh_path;
        inc.garment_piece_label = garment.label;
        inc.armor_form_key = garment.armor_form_key;
        inc.intersecting_triangle_pairs = acc.intersecting_pairs;
        inc.protruding_body_triangles = acc.protruding_tris;
        inc.max_exterior_protrusion = acc.max_protrusion;
        inc.mean_exterior_protrusion = acc.sum_protrusion / static_cast<float>(acc.intersecting_pairs);
        inc.max_segment_length = acc.max_seg_len;
        inc.penetration_bounds = acc.bounds;
        inc.centroid = scale_vec(acc.sum_pos, 1.0f / static_cast<float>(acc.intersecting_pairs));

        const bool same_nif = text::iequals_ascii(body.mesh_path, garment.mesh_path);
        const bool cross_piece_embedded_body =
            (roles[b_idx] == SurfaceRole::EmbeddedBodyInOutfit && !same_nif);

        if (garment.alpha_mode == rendering::AlphaMode::Blend) {
          inc.classification = ClippingClassification::Indeterminate;
          inc.explanation = std::format(
              "Intersección en zona {} entre '{}' y '{}' ({} pares de triángulos), pero la prenda usa AlphaBlend semitransparente.",
              to_string(zone), body.shape_name, garment.shape_name, acc.intersecting_pairs);
        } else if (acc.max_protrusion < options.min_protrusion_possible || acc.protruding_tris == 0) {
          inc.classification = ClippingClassification::GeometricIntersectionDetected;
          inc.explanation = std::format(
              "Contacto/intersección geométrica de borde en {} entre '{}' y '{}' ({} pares, protrusión máx {:.3f} u < umbral visible {:.3f} u).",
              to_string(zone), body.shape_name, garment.shape_name, acc.intersecting_pairs,
              acc.max_protrusion, options.min_protrusion_possible);
          ++g_diag.geometric_only_zones;
          ++report.geometric_only_intersection_count;
        } else if (is_limb_joint_zone(zone) && acc.intersecting_pairs < 12 && acc.max_protrusion < 0.35f) {
          inc.classification = ClippingClassification::PossibleVisibleClipping;
          inc.explanation = std::format(
              "Posible clipping en articulación {} ({} pares, protrusión máx {:.2f} u): puede depender del pliegue en la pose de referencia.",
              to_string(zone), acc.intersecting_pairs, acc.max_protrusion);
          ++g_diag.possible_clipping_zones;
          ++report.possible_visible_clipping_count;
        } else if (acc.max_protrusion >= options.min_protrusion_confirmed &&
                   acc.intersecting_pairs >= options.min_confirmed_tri_pairs) {
          inc.classification = ClippingClassification::ConfirmedVisualClipping;
          if (cross_piece_embedded_body) {
            inc.explanation = std::format(
                "Clipping visual confirmado en {}: el cuerpo incrustado '{}' (de '{}') permanece visible y atraviesa la prenda '{}' (de '{}') con {} pares de triángulos en intersección y protrusión exterior de {:.2f} u.",
                to_string(zone), body.shape_name, body.mesh_path, garment.shape_name, garment.mesh_path,
                acc.intersecting_pairs, acc.max_protrusion);
          } else {
            inc.explanation = std::format(
                "Clipping visual confirmado en {}: la superficie corporal '{}' atraviesa hacia el exterior la prenda '{}' ({} pares de triángulos, protrusión máx {:.2f} u).",
                to_string(zone), body.shape_name, garment.shape_name, acc.intersecting_pairs, acc.max_protrusion);
          }
          ++g_diag.confirmed_clipping_zones;
          ++report.confirmed_visual_clipping_count;
        } else {
          inc.classification = ClippingClassification::PossibleVisibleClipping;
          inc.explanation = std::format(
              "Posible clipping visible en {} entre '{}' y '{}' ({} pares de triángulos, protrusión exterior máx {:.2f} u).",
              to_string(zone), body.shape_name, garment.shape_name, acc.intersecting_pairs, acc.max_protrusion);
          ++g_diag.possible_clipping_zones;
          ++report.possible_visible_clipping_count;
        }

        g_diag.total_intersecting_pairs += acc.intersecting_pairs;
        g_diag.worst_protrusion = std::max(g_diag.worst_protrusion, acc.max_protrusion);
        g_diag.incidents.push_back(inc);
        report.all_incidents.push_back(inc);
      }
    }

    if (g_diag.confirmed_clipping_zones > 0) {
      g_diag.overall_classification = ClippingClassification::ConfirmedVisualClipping;
      g_diag.verdict = QaVerdict::Fail;
      report.issues.push_back({
          "error",
          "ConfirmedVisualClipping",
          "clipping",
          std::format("Prenda '{}' ({}): clipping visual confirmado en {} zona(s) anatómica(s) ({} pares de triángulos, protrusión máx {:.2f} u).",
                      g_diag.piece_label, g_diag.shape_name, g_diag.confirmed_clipping_zones,
                      g_diag.total_intersecting_pairs, g_diag.worst_protrusion),
          g_diag.mesh_path,
          g_diag.shape_name,
          g_diag.piece_label,
          g_diag.armor_form_key,
          "",
          g_diag.provider,
          g_diag.incidents.empty() ? "" : std::string(to_string(g_diag.incidents.front().zone))});
    } else if (g_diag.possible_clipping_zones > 0) {
      g_diag.overall_classification = ClippingClassification::PossibleVisibleClipping;
      g_diag.verdict = QaVerdict::Inconclusive;
      report.issues.push_back({
          "warning",
          "PossibleVisibleClipping",
          "clipping",
          std::format("Prenda '{}' ({}): posible clipping visible en {} zona(s) ({} pares de triángulos, protrusión máx {:.2f} u).",
                      g_diag.piece_label, g_diag.shape_name, g_diag.possible_clipping_zones,
                      g_diag.total_intersecting_pairs, g_diag.worst_protrusion),
          g_diag.mesh_path,
          g_diag.shape_name,
          g_diag.piece_label,
          g_diag.armor_form_key,
          "",
          g_diag.provider,
          g_diag.incidents.empty() ? "" : std::string(to_string(g_diag.incidents.front().zone))});
    } else if (g_diag.fit.fit_verdict == "TOO_SMALL_CLIPPING" || g_diag.fit.fit_verdict == "TOO_LARGE_FLOATING") {
      g_diag.verdict = QaVerdict::Fail;
      report.issues.push_back({
          "warning",
          g_diag.fit.fit_verdict == "TOO_SMALL_CLIPPING" ? "GarmentTooSmall" : "GarmentTooLarge",
          "fit",
          g_diag.fit.explanation,
          g_diag.mesh_path,
          g_diag.shape_name,
          g_diag.piece_label,
          g_diag.armor_form_key,
          "",
          g_diag.provider,
          "Torso"});
    } else {
      g_diag.verdict = QaVerdict::Pass;
    }

    report.verdict = combine_verdicts(report.verdict, g_diag.verdict);
    report.garment_diagnostics.push_back(std::move(g_diag));
  }

  return report;
}

json to_json(const ClippingAnalysisReport& report) {
  json garments = json::array();
  for (const auto& g : report.garment_diagnostics) {
    json incs = json::array();
    for (const auto& inc : g.incidents) {
      incs.push_back({
          {"zone", to_string(inc.zone)},
          {"bodyPartId", inc.body_part_id},
          {"bodyShape", inc.body_shape_name},
          {"bodyMesh", inc.body_mesh_path},
          {"bodyRole", to_string(inc.body_role)},
          {"garmentPartId", inc.garment_part_id},
          {"garmentShape", inc.garment_shape_name},
          {"garmentMesh", inc.garment_mesh_path},
          {"intersectingTrianglePairs", inc.intersecting_triangle_pairs},
          {"protrudingBodyTriangles", inc.protruding_body_triangles},
          {"maxExteriorProtrusion", inc.max_exterior_protrusion},
          {"meanExteriorProtrusion", inc.mean_exterior_protrusion},
          {"maxSegmentLength", inc.max_segment_length},
          {"centroid", {inc.centroid.x, inc.centroid.y, inc.centroid.z}},
          {"boundsMin", inc.penetration_bounds.min},
          {"boundsMax", inc.penetration_bounds.max},
          {"classification", to_string(inc.classification)},
          {"explanation", inc.explanation}});
    }
    garments.push_back({
        {"pieceLabel", g.piece_label},
        {"armorFormKey", g.armor_form_key},
        {"meshPath", g.mesh_path},
        {"shapeName", g.shape_name},
        {"provider", g.provider},
        {"bipedSlots", g.biped_slots},
        {"totalIntersectingPairs", g.total_intersecting_pairs},
        {"confirmedClippingZones", g.confirmed_clipping_zones},
        {"possibleClippingZones", g.possible_clipping_zones},
        {"geometricOnlyZones", g.geometric_only_zones},
        {"worstProtrusion", g.worst_protrusion},
        {"overallClassification", to_string(g.overall_classification)},
        {"verdict", to_string(g.verdict)},
        {"fit", {
            {"verdict", g.fit.fit_verdict},
            {"bodyReferenceShape", g.fit.body_reference_shape},
            {"meanRadialClearance", g.fit.mean_radial_clearance},
            {"minRadialClearance", g.fit.min_radial_clearance},
            {"maxRadialClearance", g.fit.max_radial_clearance},
            {"scaleRatioToBody", g.fit.scale_ratio_to_body},
            {"slicesCompared", g.fit.slices_compared},
            {"explanation", g.fit.explanation}
        }},
        {"incidents", std::move(incs)}});
  }

  json excluded = json::array();
  for (const auto& ex : report.excluded_surfaces) {
    excluded.push_back({
        {"partId", ex.part_id},
        {"shapeName", ex.shape_name},
        {"meshPath", ex.mesh_path},
        {"pieceLabel", ex.piece_label},
        {"role", to_string(ex.role)},
        {"exclusionKind", to_string(ex.exclusion_kind)},
        {"reason", ex.reason}});
  }

  json issues_arr = json::array();
  for (const auto& iss : report.issues) issues_arr.push_back(to_json(iss));

  return json{
      {"verdict", to_string(report.verdict)},
      {"partsInspected", report.parts_inspected},
      {"visibleBodyParts", report.visible_body_parts},
      {"visibleGarmentParts", report.visible_garment_parts},
      {"excludedParts", report.excluded_parts},
      {"broadPhasePairsTested", report.broad_phase_pairs_tested},
      {"broadPhasePairsOverlapping", report.broad_phase_pairs_overlapping},
      {"bvhCandidateTriPairs", report.bvh_candidate_tri_pairs},
      {"exactIntersectingTriPairs", report.exact_intersecting_tri_pairs},
      {"confirmedVisualClippingCount", report.confirmed_visual_clipping_count},
      {"possibleVisibleClippingCount", report.possible_visible_clipping_count},
      {"geometricOnlyIntersectionCount", report.geometric_only_intersection_count},
      {"coveredSlotExcludedIntersections", report.covered_slot_excluded_intersections},
      {"intentionalOverlapExcludedCount", report.intentional_overlap_excluded_count},
      {"garmentDiagnostics", std::move(garments)},
      {"excludedSurfaces", std::move(excluded)},
      {"issues", std::move(issues_arr)}};
}

}  // namespace sns::visual_qa
