#include "sns/visual_qa/seam_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace sns::visual_qa {
namespace {

constexpr std::size_t kFloatsPerVertex = rendering::kFloatsPerVertex;

nif::Vec3 sub_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

nif::Vec3 add_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

nif::Vec3 scale_v(const nif::Vec3& a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}

float dot_v(const nif::Vec3& a, const nif::Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length_sq_v(const nif::Vec3& a) { return dot_v(a, a); }

float length_v(const nif::Vec3& a) { return std::sqrt(length_sq_v(a)); }

nif::Vec3 normalize_v(const nif::Vec3& a) {
  const float len = length_v(a);
  if (len <= 1e-7f) {
    return {0.0f, 1.0f, 0.0f};
  }
  return {a.x / len, a.y / len, a.z / len};
}

float point_to_segment_distance(const nif::Vec3& p, const nif::Vec3& a, const nif::Vec3& b) {
  const nif::Vec3 ab = sub_v(b, a);
  const float ab_len_sq = length_sq_v(ab);
  if (ab_len_sq <= 1e-10f) {
    return length_v(sub_v(p, a));
  }
  const float t = std::clamp(dot_v(sub_v(p, a), ab) / ab_len_sq, 0.0f, 1.0f);
  const nif::Vec3 proj = add_v(a, scale_v(ab, t));
  return length_v(sub_v(p, proj));
}

struct BoundaryLoop {
  std::vector<nif::Vec3> vertices;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  nif::Vec3 centroid{};
  nif::Vec3 bounds_min{};
  nif::Vec3 bounds_max{};
  float mean_radius = 0.0f;
  nif::Vec3 outward_axis{0.0f, 1.0f, 0.0f};
};

struct QuantizedPos {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t z = 0;
  bool operator<(const QuantizedPos& other) const {
    return std::tie(x, y, z) < std::tie(other.x, other.y, other.z);
  }
};

QuantizedPos quantize_position(const nif::Vec3& p, float inv_cell = 1000.0f) {
  return {static_cast<std::int64_t>(std::llround(p.x * inv_cell)),
          static_cast<std::int64_t>(std::llround(p.y * inv_cell)),
          static_cast<std::int64_t>(std::llround(p.z * inv_cell))};
}

// Extracts connected open boundary loops from a RenderPart after welding UV-split vertices by 3D position.
std::vector<BoundaryLoop> extract_open_boundary_loops(const rendering::RenderPart& part) {
  std::vector<BoundaryLoop> loops;
  const std::size_t vertex_count = part.vertices.size() / kFloatsPerVertex;
  if (vertex_count < 3 || part.indices.size() < 3) {
    return loops;
  }

  std::map<QuantizedPos, std::uint32_t> pos_to_welded;
  std::vector<std::uint32_t> vertex_to_welded(vertex_count, 0);
  std::vector<nif::Vec3> welded_positions;
  welded_positions.reserve(vertex_count);

  for (std::size_t v = 0; v < vertex_count; ++v) {
    const float* base = part.vertices.data() + v * kFloatsPerVertex;
    const nif::Vec3 pos{base[0], base[1], base[2]};
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
      continue;
    }
    const QuantizedPos q = quantize_position(pos);
    auto [it, inserted] = pos_to_welded.emplace(q, static_cast<std::uint32_t>(welded_positions.size()));
    if (inserted) {
      welded_positions.push_back(pos);
    }
    vertex_to_welded[v] = it->second;
  }

  std::map<std::pair<std::uint32_t, std::uint32_t>, int> edge_use_count;
  for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
    const std::uint32_t i0 = part.indices[i + 0];
    const std::uint32_t i1 = part.indices[i + 1];
    const std::uint32_t i2 = part.indices[i + 2];
    if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
      continue;
    }
    const std::uint32_t w0 = vertex_to_welded[i0];
    const std::uint32_t w1 = vertex_to_welded[i1];
    const std::uint32_t w2 = vertex_to_welded[i2];
    if (w0 == w1 || w1 == w2 || w2 == w0) {
      continue;
    }
    const std::pair<std::uint32_t, std::uint32_t> e0{std::min(w0, w1), std::max(w0, w1)};
    const std::pair<std::uint32_t, std::uint32_t> e1{std::min(w1, w2), std::max(w1, w2)};
    const std::pair<std::uint32_t, std::uint32_t> e2{std::min(w2, w0), std::max(w2, w0)};
    ++edge_use_count[e0];
    ++edge_use_count[e1];
    ++edge_use_count[e2];
  }

  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> adj;
  for (const auto& [edge, count] : edge_use_count) {
    if (count == 1) {
      adj[edge.first].push_back(edge.second);
      adj[edge.second].push_back(edge.first);
    }
  }

  std::set<std::uint32_t> visited;
  for (const auto& [start_vert, _] : adj) {
    if (visited.contains(start_vert)) {
      continue;
    }
    std::vector<std::uint32_t> comp_welded;
    std::vector<std::uint32_t> stack{start_vert};
    visited.insert(start_vert);
    while (!stack.empty()) {
      const std::uint32_t cur = stack.back();
      stack.pop_back();
      comp_welded.push_back(cur);
      for (std::uint32_t nxt : adj[cur]) {
        if (!visited.contains(nxt)) {
          visited.insert(nxt);
          stack.push_back(nxt);
        }
      }
    }

    if (comp_welded.size() < 4) {
      continue;
    }

    BoundaryLoop loop;
    std::unordered_map<std::uint32_t, std::size_t> local_idx;
    loop.vertices.reserve(comp_welded.size());
    loop.bounds_min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
    loop.bounds_max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest()};

    nif::Vec3 sum{0.0f, 0.0f, 0.0f};
    for (std::uint32_t w : comp_welded) {
      const nif::Vec3& p = welded_positions[w];
      local_idx[w] = loop.vertices.size();
      loop.vertices.push_back(p);
      sum = add_v(sum, p);
      loop.bounds_min.x = std::min(loop.bounds_min.x, p.x);
      loop.bounds_min.y = std::min(loop.bounds_min.y, p.y);
      loop.bounds_min.z = std::min(loop.bounds_min.z, p.z);
      loop.bounds_max.x = std::max(loop.bounds_max.x, p.x);
      loop.bounds_max.y = std::max(loop.bounds_max.y, p.y);
      loop.bounds_max.z = std::max(loop.bounds_max.z, p.z);
    }
    loop.centroid = scale_v(sum, 1.0f / static_cast<float>(loop.vertices.size()));

    float radius_sum = 0.0f;
    for (const auto& p : loop.vertices) {
      radius_sum += length_v(sub_v(p, loop.centroid));
    }
    loop.mean_radius = radius_sum / static_cast<float>(loop.vertices.size());

    for (std::uint32_t w : comp_welded) {
      const std::size_t u_local = local_idx[w];
      for (std::uint32_t nxt : adj[w]) {
        auto it = local_idx.find(nxt);
        if (it != local_idx.end() && u_local < it->second) {
          loop.edges.emplace_back(u_local, it->second);
        }
      }
    }

    // Estimate outward tube axis by comparing loop centroid against nearby interior vertices of this part.
    nif::Vec3 interior_sum{0.0f, 0.0f, 0.0f};
    std::size_t interior_count = 0;
    const float search_min = std::max(0.5f, loop.mean_radius * 0.25f);
    const float search_max = std::max(4.5f, loop.mean_radius * 2.2f);
    for (std::size_t w = 0; w < welded_positions.size(); ++w) {
      if (local_idx.contains(static_cast<std::uint32_t>(w))) {
        continue;
      }
      const float dist = length_v(sub_v(welded_positions[w], loop.centroid));
      if (dist >= search_min && dist <= search_max) {
        interior_sum = add_v(interior_sum, welded_positions[w]);
        ++interior_count;
      }
    }
    if (interior_count > 0) {
      const nif::Vec3 interior_centroid = scale_v(interior_sum, 1.0f / static_cast<float>(interior_count));
      loop.outward_axis = normalize_v(sub_v(loop.centroid, interior_centroid));
    }

    loops.push_back(std::move(loop));
  }

  return loops;
}

std::string to_lower_copy(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string detect_body_family_from_part(const rendering::RenderPart& part) {
  const std::string lower_mesh = to_lower_copy(part.mesh_path);
  const std::string lower_shape = to_lower_copy(part.shape_name);
  if (lower_mesh.find("ube") != std::string::npos || lower_shape.find("ube") != std::string::npos) {
    return "UBE";
  }
  if (lower_mesh.find("3ba") != std::string::npos || lower_shape.find("3ba") != std::string::npos ||
      lower_mesh.find("3bbb") != std::string::npos || lower_shape.find("3bbb") != std::string::npos) {
    return "CBBE 3BA";
  }
  if (lower_mesh.find("cbbe") != std::string::npos || lower_shape.find("cbbe") != std::string::npos) {
    return "CBBE";
  }
  if (lower_mesh.find("bhunp") != std::string::npos || lower_shape.find("bhunp") != std::string::npos ||
      lower_mesh.find("unp") != std::string::npos || lower_shape.find("unp") != std::string::npos) {
    return "UNP/BHUNP";
  }
  return {};
}

bool is_loop_in_junction_region(const BoundaryLoop& loop, SeamKind kind, float char_min_y,
                                float char_max_y, float scale) {
  const float height = std::max(char_max_y - char_min_y, 50.0f);
  const float norm_y = (loop.centroid.y - char_min_y) / height;
  const float s = std::max(scale, 0.5f);

  switch (kind) {
    case SeamKind::NeckHead:
      return norm_y >= 0.70f && norm_y <= 0.90f && std::abs(loop.centroid.x) <= 9.0f * s &&
             loop.mean_radius >= 2.2f * s && loop.mean_radius <= 11.0f * s;
    case SeamKind::WristHandLeft:
      return loop.centroid.x >= 8.0f * s && norm_y >= 0.35f && norm_y <= 0.78f &&
             loop.mean_radius >= 0.9f * s && loop.mean_radius <= 6.5f * s;
    case SeamKind::WristHandRight:
      return loop.centroid.x <= -8.0f * s && norm_y >= 0.35f && norm_y <= 0.78f &&
             loop.mean_radius >= 0.9f * s && loop.mean_radius <= 6.5f * s;
    case SeamKind::AnkleFootLeft:
      return loop.centroid.x >= 0.4f * s && std::abs(loop.centroid.x) <= 22.0f * s &&
             norm_y >= 0.015f && norm_y <= 0.22f && loop.mean_radius >= 1.1f * s &&
             loop.mean_radius <= 7.5f * s;
    case SeamKind::AnkleFootRight:
      return loop.centroid.x <= -0.4f * s && std::abs(loop.centroid.x) <= 22.0f * s &&
             norm_y >= 0.015f && norm_y <= 0.22f && loop.mean_radius >= 1.1f * s &&
             loop.mean_radius <= 7.5f * s;
  }
  return false;
}

bool is_torso_candidate(const rendering::RenderPart& part, SurfaceRole role) {
  if (role != SurfaceRole::VisibleBodySkin && role != SurfaceRole::EmbeddedBodyInOutfit &&
      role != SurfaceRole::CoveredBodyBySlot) {
    return false;
  }
  const float y_span = part.max[1] - part.min[1];
  if (y_span >= 35.0f) {
    return true;
  }
  const std::string lower_id = to_lower_copy(part.id);
  const std::string lower_shape = to_lower_copy(part.shape_name);
  const std::string lower_mesh = to_lower_copy(part.mesh_path);
  return lower_id.find("body") != std::string::npos || lower_id.find("torso") != std::string::npos ||
         lower_shape.find("body") != std::string::npos || lower_shape == "3ba" ||
         lower_mesh.find("body") != std::string::npos;
}

bool is_head_candidate(const rendering::RenderPart& part, SurfaceRole role) {
  if (role == SurfaceRole::VisibleHeadSkin) {
    return true;
  }
  if (part.group == "head" && part.shown_by_default) {
    const std::string lower_shape = to_lower_copy(part.shape_name);
    if (lower_shape.find("head") != std::string::npos) {
      return true;
    }
  }
  const std::string lower_id = to_lower_copy(part.id);
  const std::string lower_mesh = to_lower_copy(part.mesh_path);
  return part.shown_by_default &&
         (lower_id.find("head") != std::string::npos || lower_mesh.find("head") != std::string::npos);
}

bool is_hand_candidate(const rendering::RenderPart& part, SurfaceRole role) {
  if (role != SurfaceRole::VisibleBodySkin && role != SurfaceRole::EmbeddedBodyInOutfit &&
      role != SurfaceRole::CoveredBodyBySlot) {
    return false;
  }
  const std::string lower_id = to_lower_copy(part.id);
  const std::string lower_shape = to_lower_copy(part.shape_name);
  const std::string lower_mesh = to_lower_copy(part.mesh_path);
  return lower_id.find("hand") != std::string::npos || lower_shape.find("hand") != std::string::npos ||
         lower_mesh.find("hand") != std::string::npos;
}

bool is_foot_candidate(const rendering::RenderPart& part, SurfaceRole role) {
  if (role != SurfaceRole::VisibleBodySkin && role != SurfaceRole::EmbeddedBodyInOutfit &&
      role != SurfaceRole::CoveredBodyBySlot) {
    return false;
  }
  const std::string lower_id = to_lower_copy(part.id);
  const std::string lower_shape = to_lower_copy(part.shape_name);
  const std::string lower_mesh = to_lower_copy(part.mesh_path);
  return lower_id.find("feet") != std::string::npos || lower_id.find("foot") != std::string::npos ||
         lower_shape.find("feet") != std::string::npos || lower_shape.find("foot") != std::string::npos ||
         lower_mesh.find("feet") != std::string::npos || lower_mesh.find("foot") != std::string::npos;
}

int role_priority(SurfaceRole role) {
  switch (role) {
    case SurfaceRole::VisibleBodySkin:
    case SurfaceRole::VisibleHeadSkin:
      return 3;
    case SurfaceRole::EmbeddedBodyInOutfit:
      return 2;
    case SurfaceRole::CoveredBodyBySlot:
      return 1;
    default:
      return 0;
  }
}

struct LoopCandidate {
  const rendering::RenderPart* part = nullptr;
  SurfaceRole role = SurfaceRole::HiddenNifShape;
  BoundaryLoop loop;
};

std::string find_covering_garment(const rendering::CharacterScene& scene, const BoundaryLoop& loop_a,
                                  const BoundaryLoop& loop_b, SeamKind kind,
                                  SurfaceRole role_a, SurfaceRole role_b) {
  const nif::Vec3 mid = scale_v(add_v(loop_a.centroid, loop_b.centroid), 0.5f);
  const float radius = std::max(loop_a.mean_radius, loop_b.mean_radius);

  // Check if any visible garment part's geometry encloses the seam loop region.
  for (const auto& part : scene.parts) {
    const SurfaceRole r = classify_part_role(part);
    if (r != SurfaceRole::VisibleGarment) {
      continue;
    }
    const float margin = 0.8f;
    if (mid.x < part.min[0] - margin || mid.x > part.max[0] + margin ||
        mid.y < part.min[1] - margin || mid.y > part.max[1] + margin ||
        mid.z < part.min[2] - margin || mid.z > part.max[2] + margin) {
      continue;
    }

    // Count garment vertices within the cylindrical neighbourhood of the junction.
    std::size_t nearby_verts = 0;
    const std::size_t vcount = part.vertices.size() / kFloatsPerVertex;
    const float max_dist = std::max(radius * 2.1f, 3.5f);
    for (std::size_t v = 0; v < vcount; ++v) {
      const float* base = part.vertices.data() + v * kFloatsPerVertex;
      const nif::Vec3 p{base[0], base[1], base[2]};
      if (length_v(sub_v(p, mid)) <= max_dist) {
        ++nearby_verts;
      }
    }
    if (nearby_verts >= 12) {
      return !part.label.empty() ? part.label : part.id;
    }
  }

  // Also check if either side of a limb seam was hidden by an equipped Biped slot (e.g. boots on slot 37).
  if (kind != SeamKind::NeckHead &&
      (role_a == SurfaceRole::CoveredBodyBySlot || role_b == SurfaceRole::CoveredBodyBySlot)) {
    const int target_slot =
        (kind == SeamKind::WristHandLeft || kind == SeamKind::WristHandRight) ? 33 : 37;
    for (const auto& part : scene.parts) {
      if (classify_part_role(part) == SurfaceRole::VisibleGarment) {
        const auto slots = rendering::slots_from_mask(part.slot_mask);
        if (std::find(slots.begin(), slots.end(), target_slot) != slots.end()) {
          return !part.label.empty() ? part.label : part.id;
        }
      }
    }
  }

  return {};
}

SeamJunctionReport evaluate_single_junction(
    SeamKind kind, const rendering::CharacterScene& scene,
    const std::vector<std::pair<const rendering::RenderPart*, std::vector<BoundaryLoop>>>& part_loops,
    float char_min_y, float char_max_y, const SeamAnalysisOptions& options) {
  SeamJunctionReport j;
  j.kind = kind;
  const float scale = std::max(scene.scale, 0.5f);
  j.calibrated_aligned_limit = options.base_aligned_rms_units * scale;
  j.calibrated_gap_limit = options.base_gap_rms_units * scale;

  std::vector<LoopCandidate> candidates_a;
  std::vector<LoopCandidate> candidates_b;

  for (const auto& [part, loops] : part_loops) {
    const SurfaceRole role = classify_part_role(*part);
    const bool cand_a = is_torso_candidate(*part, role);
    bool cand_b = false;
    switch (kind) {
      case SeamKind::NeckHead:
        cand_b = is_head_candidate(*part, role);
        break;
      case SeamKind::WristHandLeft:
      case SeamKind::WristHandRight:
        cand_b = is_hand_candidate(*part, role);
        break;
      case SeamKind::AnkleFootLeft:
      case SeamKind::AnkleFootRight:
        cand_b = is_foot_candidate(*part, role);
        break;
    }

    if (!cand_a && !cand_b) {
      continue;
    }

    for (const auto& loop : loops) {
      if (!is_loop_in_junction_region(loop, kind, char_min_y, char_max_y, scale)) {
        continue;
      }
      if (cand_a) {
        candidates_a.push_back({part, role, loop});
      }
      if (cand_b) {
        candidates_b.push_back({part, role, loop});
      }
    }
  }

  if (candidates_a.empty() || candidates_b.empty()) {
    j.status = SeamStatus::NoComparableBoundary;
    j.verdict = QaVerdict::Skip;
    j.explanation = "Required open mesh boundary loop not present for one or both sides of the junction.";
    return j;
  }

  // Choose the best pair of boundary loops (highest visibility priority, then closest loop centroids).
  const LoopCandidate* best_a = nullptr;
  const LoopCandidate* best_b = nullptr;
  int best_priority = -1;
  float best_centroid_dist = std::numeric_limits<float>::max();

  for (const auto& ca : candidates_a) {
    for (const auto& cb : candidates_b) {
      if (ca.part == cb.part) {
        continue;
      }
      const int prio = role_priority(ca.role) + role_priority(cb.role);
      const float cdist = length_v(sub_v(ca.loop.centroid, cb.loop.centroid));
      if (prio > best_priority || (prio == best_priority && cdist < best_centroid_dist)) {
        best_priority = prio;
        best_centroid_dist = cdist;
        best_a = &ca;
        best_b = &cb;
      }
    }
  }

  if (!best_a || !best_b) {
    j.status = SeamStatus::NoComparableBoundary;
    j.verdict = QaVerdict::Skip;
    j.explanation = "Could not pair distinct meshes across junction.";
    return j;
  }

  j.part_a_id = best_a->part->id;
  j.part_a_shape = best_a->part->shape_name;
  j.part_a_mesh = best_a->part->mesh_path;
  j.part_b_id = best_b->part->id;
  j.part_b_shape = best_b->part->shape_name;
  j.part_b_mesh = best_b->part->mesh_path;
  j.boundary_vertices_a = best_a->loop.vertices.size();
  j.boundary_vertices_b = best_b->loop.vertices.size();

  const std::string fam_a = detect_body_family_from_part(*best_a->part);
  const std::string fam_b = detect_body_family_from_part(*best_b->part);
  if (!fam_a.empty() && !fam_b.empty() && fam_a != fam_b) {
    j.body_family_hint = fam_a + " vs " + fam_b;
  } else if (!fam_a.empty()) {
    j.body_family_hint = fam_a;
  } else if (!fam_b.empty()) {
    j.body_family_hint = fam_b;
  }

  // Compute bidirectional point-to-edge-segment distances between Loop A and Loop B.
  auto min_dist_to_loop = [](const nif::Vec3& p, const BoundaryLoop& target) -> float {
    float best = std::numeric_limits<float>::max();
    if (!target.edges.empty()) {
      for (const auto& [u, v] : target.edges) {
        best = std::min(best, point_to_segment_distance(p, target.vertices[u], target.vertices[v]));
      }
    } else {
      for (const auto& q : target.vertices) {
        best = std::min(best, length_v(sub_v(p, q)));
      }
    }
    return best;
  };

  std::vector<float> distances;
  distances.reserve(best_a->loop.vertices.size() + best_b->loop.vertices.size());
  for (const auto& p : best_a->loop.vertices) {
    distances.push_back(min_dist_to_loop(p, best_b->loop));
  }
  for (const auto& q : best_b->loop.vertices) {
    distances.push_back(min_dist_to_loop(q, best_a->loop));
  }

  std::sort(distances.begin(), distances.end());
  j.min_distance = distances.front();
  j.max_distance = distances.back();
  double sum = 0.0;
  double sq_sum = 0.0;
  for (float d : distances) {
    sum += d;
    sq_sum += static_cast<double>(d) * static_cast<double>(d);
  }
  j.mean_distance = static_cast<float>(sum / static_cast<double>(distances.size()));
  j.rms_distance = static_cast<float>(std::sqrt(sq_sum / static_cast<double>(distances.size())));
  const std::size_t p95_idx =
      std::min(distances.size() - 1, static_cast<std::size_t>(std::floor(distances.size() * 0.95)));
  j.p95_distance = distances[p95_idx];

  // Axial and lateral centroid offsets along the junction's anatomical tube axis.
  nif::Vec3 axis = best_a->loop.outward_axis;
  if (kind == SeamKind::NeckHead) {
    axis = {0.0f, 1.0f, 0.0f};
  } else if (kind == SeamKind::AnkleFootLeft || kind == SeamKind::AnkleFootRight) {
    axis = {0.0f, -1.0f, 0.0f};
  }
  const nif::Vec3 delta_c = sub_v(best_b->loop.centroid, best_a->loop.centroid);
  j.centroid_offset_axial = dot_v(delta_c, axis);
  const nif::Vec3 lateral_vec = sub_v(delta_c, scale_v(axis, j.centroid_offset_axial));
  j.centroid_offset_lateral = length_v(lateral_vec);

  j.radius_a = best_a->loop.mean_radius;
  j.radius_b = best_b->loop.mean_radius;
  j.scale_ratio = j.radius_a / std::max(j.radius_b, 1e-5f);
  j.radial_mismatch_pct =
      std::abs(j.radius_a - j.radius_b) / std::max({j.radius_a, j.radius_b, 1e-5f}) * 100.0f;

  j.covering_garment_label =
      find_covering_garment(scene, best_a->loop, best_b->loop, kind, best_a->role, best_b->role);
  j.covered_by_garment = !j.covering_garment_label.empty();

  const float max_gap_limit = options.base_gap_max_units * scale;
  const float misalign_limit = options.base_misalignment_units * scale;

  // Determine raw geometric status before garment concealment check.
  SeamStatus raw_status = SeamStatus::Aligned;
  if (j.radial_mismatch_pct > options.max_radial_mismatch_pct &&
      j.rms_distance > j.calibrated_aligned_limit * 1.5f) {
    raw_status = SeamStatus::ScaleMismatch;
  } else if (j.centroid_offset_lateral > misalign_limit &&
             j.centroid_offset_lateral > std::abs(j.centroid_offset_axial) * 1.15f) {
    raw_status = SeamStatus::Misalignment;
  } else if (j.centroid_offset_axial < -j.calibrated_gap_limit &&
             j.rms_distance > j.calibrated_aligned_limit * 1.8f) {
    raw_status = SeamStatus::Intersection;
  } else if (j.rms_distance > j.calibrated_gap_limit || j.max_distance > max_gap_limit) {
    raw_status = SeamStatus::AbnormalGap;
  } else if (j.rms_distance <= j.calibrated_aligned_limit &&
             j.max_distance <= j.calibrated_gap_limit) {
    raw_status = SeamStatus::Aligned;
  } else {
    raw_status = SeamStatus::MinorDeviation;
  }

  std::ostringstream expl;
  expl << std::fixed << std::setprecision(3);
  if (raw_status == SeamStatus::Aligned || raw_status == SeamStatus::MinorDeviation) {
    j.status = raw_status;
    j.verdict = QaVerdict::Pass;
    expl << to_string(kind) << " boundary rings match within calibrated tolerance (RMS="
         << j.rms_distance << ", max=" << j.max_distance << ", radial mismatch="
         << j.radial_mismatch_pct << "%).";
    if (j.covered_by_garment) {
      expl << " Junction is also covered by '" << j.covering_garment_label << "'.";
    }
  } else if (j.covered_by_garment) {
    j.status = SeamStatus::CoveredByGarment;
    j.verdict = QaVerdict::Pass;
    expl << to_string(kind) << " exhibits underlying " << to_string(raw_status) << " (RMS="
         << j.rms_distance << ", max=" << j.max_distance
         << "), but the junction is concealed by equipped garment '" << j.covering_garment_label
         << "' and is not declared as a visible seam defect.";
  } else {
    j.status = raw_status;
    j.verdict = QaVerdict::Fail;
    expl << to_string(kind) << " defect [" << to_string(raw_status) << "] between '" << j.part_a_id
         << "' (" << j.part_a_mesh << ") and '" << j.part_b_id << "' (" << j.part_b_mesh
         << "): RMS=" << j.rms_distance << " (limit " << j.calibrated_gap_limit
         << "), max=" << j.max_distance << ", axial offset=" << j.centroid_offset_axial
         << ", lateral offset=" << j.centroid_offset_lateral
         << ", radial mismatch=" << j.radial_mismatch_pct << "%.";
  }
  j.explanation = expl.str();
  return j;
}

}  // namespace

std::string_view to_string(SeamKind kind) {
  switch (kind) {
    case SeamKind::NeckHead:
      return "NeckHead";
    case SeamKind::WristHandLeft:
      return "WristHandLeft";
    case SeamKind::WristHandRight:
      return "WristHandRight";
    case SeamKind::AnkleFootLeft:
      return "AnkleFootLeft";
    case SeamKind::AnkleFootRight:
      return "AnkleFootRight";
  }
  return "NeckHead";
}

std::string_view to_string(SeamStatus status) {
  switch (status) {
    case SeamStatus::Aligned:
      return "Aligned";
    case SeamStatus::MinorDeviation:
      return "MinorDeviation";
    case SeamStatus::AbnormalGap:
      return "AbnormalGap";
    case SeamStatus::Intersection:
      return "Intersection";
    case SeamStatus::Misalignment:
      return "Misalignment";
    case SeamStatus::ScaleMismatch:
      return "ScaleMismatch";
    case SeamStatus::CoveredByGarment:
      return "CoveredByGarment";
    case SeamStatus::NoComparableBoundary:
      return "NoComparableBoundary";
  }
  return "NoComparableBoundary";
}

SeamAnalysisReport analyze_scene_seams(const rendering::CharacterScene& scene,
                                       const SeamAnalysisOptions& options) {
  SeamAnalysisReport report;
  report.character_scale = scene.scale > 0.0f ? scene.scale : 1.0f;

  float char_min_y = scene.min[1];
  float char_max_y = scene.max[1];
  if (!(char_max_y > char_min_y + 10.0f)) {
    char_min_y = 0.0f;
    char_max_y = 124.0f * report.character_scale;
  }

  std::vector<std::pair<const rendering::RenderPart*, std::vector<BoundaryLoop>>> part_loops;
  part_loops.reserve(scene.parts.size());

  std::set<std::string> families;
  for (const auto& part : scene.parts) {
    const SurfaceRole role = classify_part_role(part);
    if (role == SurfaceRole::HiddenNifShape || role == SurfaceRole::SmpCollisionProxy ||
        role == SurfaceRole::UnequippedOutfitPiece) {
      continue;
    }
    const std::string fam = detect_body_family_from_part(part);
    if (!fam.empty()) {
      families.insert(fam);
    }
    if (role == SurfaceRole::VisibleBodySkin || role == SurfaceRole::EmbeddedBodyInOutfit ||
        role == SurfaceRole::VisibleHeadSkin || role == SurfaceRole::CoveredBodyBySlot ||
        is_head_candidate(part, role)) {
      part_loops.emplace_back(&part, extract_open_boundary_loops(part));
    }
  }

  if (families.size() == 1) {
    report.detected_body_family = *families.begin();
  } else if (families.size() > 1) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& f : families) {
      if (!first) {
        oss << " + ";
      }
      oss << f;
      first = false;
    }
    report.detected_body_family = oss.str();
  } else {
    report.detected_body_family = "Standard/Unknown";
  }

  const std::array<SeamKind, 5> all_kinds = {SeamKind::NeckHead, SeamKind::WristHandLeft,
                                             SeamKind::WristHandRight, SeamKind::AnkleFootLeft,
                                             SeamKind::AnkleFootRight};

  for (SeamKind kind : all_kinds) {
    SeamJunctionReport j =
        evaluate_single_junction(kind, scene, part_loops, char_min_y, char_max_y, options);
    if (j.status == SeamStatus::NoComparableBoundary) {
      ++report.junctions_missing_boundary;
    } else {
      ++report.junctions_evaluated;
      if (j.status == SeamStatus::Aligned || j.status == SeamStatus::MinorDeviation) {
        ++report.junctions_aligned;
      } else if (j.status == SeamStatus::CoveredByGarment) {
        ++report.junctions_covered_by_garment;
        VisualQaIssue issue;
        issue.severity = "info";
        issue.code = "SeamConcealedByGarment";
        issue.category = "seam";
        issue.message = j.explanation;
        issue.mesh_path = j.part_b_mesh;
        issue.shape_name = j.part_b_shape;
        issue.piece_label = j.covering_garment_label;
        issue.zone = std::string(to_string(kind));
        report.issues.push_back(std::move(issue));
      } else {
        ++report.junctions_defective;
        VisualQaIssue issue;
        issue.severity = "error";
        issue.code = std::string(to_string(kind)) + "_" + std::string(to_string(j.status));
        issue.category = "seam";
        issue.message = j.explanation;
        issue.mesh_path = j.part_b_mesh;
        issue.shape_name = j.part_b_shape;
        issue.piece_label = j.part_b_id;
        issue.zone = std::string(to_string(kind));
        report.issues.push_back(std::move(issue));
      }
    }
    report.junctions.push_back(std::move(j));
  }

  if (report.junctions_defective > 0) {
    report.verdict = QaVerdict::Fail;
  } else if (report.junctions_evaluated == 0) {
    report.verdict = QaVerdict::Skip;
  } else {
    report.verdict = QaVerdict::Pass;
  }

  return report;
}

nlohmann::json to_json(const SeamAnalysisReport& report) {
  nlohmann::json junctions_json = nlohmann::json::array();
  for (const auto& j : report.junctions) {
    junctions_json.push_back({{"kind", to_string(j.kind)},
                              {"status", to_string(j.status)},
                              {"verdict", to_string(j.verdict)},
                              {"part_a_id", j.part_a_id},
                              {"part_a_shape", j.part_a_shape},
                              {"part_a_mesh", j.part_a_mesh},
                              {"part_b_id", j.part_b_id},
                              {"part_b_shape", j.part_b_shape},
                              {"part_b_mesh", j.part_b_mesh},
                              {"boundary_vertices_a", j.boundary_vertices_a},
                              {"boundary_vertices_b", j.boundary_vertices_b},
                              {"min_distance", j.min_distance},
                              {"max_distance", j.max_distance},
                              {"mean_distance", j.mean_distance},
                              {"rms_distance", j.rms_distance},
                              {"p95_distance", j.p95_distance},
                              {"centroid_offset_axial", j.centroid_offset_axial},
                              {"centroid_offset_lateral", j.centroid_offset_lateral},
                              {"radius_a", j.radius_a},
                              {"radius_b", j.radius_b},
                              {"scale_ratio", j.scale_ratio},
                              {"radial_mismatch_pct", j.radial_mismatch_pct},
                              {"calibrated_aligned_limit", j.calibrated_aligned_limit},
                              {"calibrated_gap_limit", j.calibrated_gap_limit},
                              {"covered_by_garment", j.covered_by_garment},
                              {"covering_garment_label", j.covering_garment_label},
                              {"body_family_hint", j.body_family_hint},
                              {"explanation", j.explanation}});
  }

  nlohmann::json issues_json = nlohmann::json::array();
  for (const auto& issue : report.issues) {
    issues_json.push_back(to_json(issue));
  }

  return {{"verdict", to_string(report.verdict)},
          {"character_scale", report.character_scale},
          {"detected_body_family", report.detected_body_family},
          {"junctions_evaluated", report.junctions_evaluated},
          {"junctions_aligned", report.junctions_aligned},
          {"junctions_defective", report.junctions_defective},
          {"junctions_covered_by_garment", report.junctions_covered_by_garment},
          {"junctions_missing_boundary", report.junctions_missing_boundary},
          {"junctions", std::move(junctions_json)},
          {"issues", std::move(issues_json)}};
}

}  // namespace sns::visual_qa
