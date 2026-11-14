#include "sns/rendering/fidelity.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>

#include "sns/nif/mesh.hpp"

namespace sns::rendering {
namespace {

using V3 = std::array<float, 3>;

V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 mul(const V3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
V3 normalized(const V3& a) {
  const float l = std::sqrt(dot(a, a));
  return l > 1e-12f ? mul(a, 1.0f / l) : V3{0, 0, 0};
}

// Closest point of triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5), as body::measure_penetration uses.
V3 closest_on_triangle(const V3& p, const V3& a, const V3& b, const V3& c) {
  const V3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const V3 bp = sub(p, b);
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
  const V3 cp = sub(p, c);
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  const float denom = 1.0f / (va + vb + vc);
  return add(a, add(mul(ab, vb * denom), mul(ac, vc * denom)));
}

V3 at(const std::vector<float>& xyz, std::uint32_t i) {
  const std::size_t k = static_cast<std::size_t>(i) * 3;
  return {xyz[k], xyz[k + 1], xyz[k + 2]};
}

bool has_slot(std::uint32_t mask, int slot) { return slot >= 30 && slot <= 61 && (mask & (1u << (slot - 30))) != 0; }
bool is_slot_partition(std::uint16_t id) { return id >= 30 && id <= 61; }

std::string percent(double ratio) { return std::format("{:.1f} %", ratio * 100.0); }

// Triangles of a part the game draws: all of them, or (probable engine rule) only those whose dismember partition is one of the
// addon's biped slots. Head (FaceGen) parts have no addon and keep everything.
struct DrawnTriangles {
  std::vector<std::uint32_t> indices;
  std::size_t dropped = 0;
  std::set<int> unclaimed;  // partition slots not in the addon's mask
};
DrawnTriangles triangles_drawn_in_game(const RenderPart& part) {
  DrawnTriangles out;
  const bool rule = part.group != "head" && part.slot_mask != 0 && !part.triangle_partitions.empty() &&
                    part.triangle_partitions.size() * 3 == part.indices.size();
  if (!rule) {
    out.indices = part.indices;
    return out;
  }
  out.indices.reserve(part.indices.size());
  for (std::size_t t = 0; t < part.triangle_partitions.size(); ++t) {
    const auto id = part.triangle_partitions[t];
    if (is_slot_partition(id) && !has_slot(part.slot_mask, id)) {
      ++out.dropped;
      out.unclaimed.insert(id);
      continue;
    }
    out.indices.insert(out.indices.end(), {part.indices[t * 3], part.indices[t * 3 + 1], part.indices[t * 3 + 2]});
  }
  return out;
}

// ---- exposure: orthographic z-buffers around the character -------------------------------------------------------------------

struct Occluder {
  const std::vector<float>* positions;
  const std::vector<std::uint32_t>* indices;
};

struct View {
  V3 forward, right, up;
};

std::vector<View> exposure_views() {
  std::vector<View> views;
  const V3 world_up{0, 1, 0};  // the scene is Y-up
  for (const float elevation : {0.0f, 0.45f}) {  // level, and from ~24 degrees above
    for (int k = 0; k < 8; ++k) {
      const float a = static_cast<float>(k) * 0.785398163f;
      // Camera on a circle around the character; forward points from the camera into the scene.
      const V3 forward = normalized(V3{-std::sin(a), -elevation, -std::cos(a)});
      const V3 right = normalized(cross(forward, world_up));
      const V3 up = normalized(cross(right, forward));
      views.push_back({forward, right, up});
    }
  }
  return views;
}

struct DepthBuffer {
  int size = 0;
  float u0 = 0, v0 = 0, pixel = 1;
  std::vector<float> depth;
  [[nodiscard]] float& at(int x, int y) {
    return depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)];
  }
};

void rasterize(DepthBuffer& buffer, const View& view, const std::vector<float>& positions, const std::vector<std::uint32_t>& indices) {
  const auto project = [&](std::uint32_t i, float& x, float& y, float& d) {
    const V3 p = at(positions, i);
    x = (dot(p, view.right) - buffer.u0) / buffer.pixel;
    y = (dot(p, view.up) - buffer.v0) / buffer.pixel;
    d = dot(p, view.forward);
  };
  const std::size_t vertex_count = positions.size() / 3;
  for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
    if (indices[t] >= vertex_count || indices[t + 1] >= vertex_count || indices[t + 2] >= vertex_count) continue;
    float x0, y0, d0, x1, y1, d1, x2, y2, d2;
    project(indices[t], x0, y0, d0);
    project(indices[t + 1], x1, y1, d1);
    project(indices[t + 2], x2, y2, d2);
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::abs(area) < 1e-8f) continue;
    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
    const int max_x = std::min(buffer.size - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
    const int max_y = std::min(buffer.size - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
    for (int py = min_y; py <= max_y; ++py) {
      for (int px = min_x; px <= max_x; ++px) {
        const float cx = static_cast<float>(px) + 0.5f, cy = static_cast<float>(py) + 0.5f;
        const float w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area;
        const float w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;  // either winding: garments are often single-sided
        const float d = w0 * d0 + w1 * d1 + w2 * d2;
        float& slot = buffer.at(px, py);
        if (d < slot) slot = d;
      }
    }
  }
}

// Hidden skin against what is drawn (see ExposedSkin). One z-buffer per view for all subjects; a vertex is exposed when it is farther
// than the margin from every drawn surface and some view it faces has nothing drawn in front of it.
std::vector<ExposedSkin> measure_exposure(const std::vector<const RenderPart*>& subjects, const std::vector<Occluder>& occluders,
                                          const std::vector<float>& drawn_positions, const std::vector<std::uint32_t>& drawn_indices,
                                          const FidelityOptions& options) {
  std::vector<ExposedSkin> results(subjects.size());
  std::vector<std::vector<float>> positions(subjects.size()), normals(subjects.size());
  std::vector<std::vector<char>> exposed(subjects.size());  // bit 1: see-through, bit 2: outside the drawn silhouette
  std::vector<std::vector<char>> far(subjects.size());
  constexpr float kTolerance = 0.25f;  // a garment this close in front still covers (tight clothes, coincident bodies)
  // Distance to the drawn geometry, only up to the margin (grid over the drawn triangles).
  const TriangleGrid drawn(drawn_positions, drawn_indices, options.exposure_margin);
  for (std::size_t s = 0; s < subjects.size(); ++s) {
    positions[s] = part_positions(*subjects[s]);
    normals[s] = part_normals(*subjects[s]);
    results[s].vertices = positions[s].size() / 3;
    exposed[s].assign(results[s].vertices, 0);
    far[s].assign(results[s].vertices, 0);
    for (std::size_t i = 0; i < results[s].vertices; ++i) {
      float distance = 0.0f;
      far[s][i] = drawn.closest(at(positions[s], static_cast<std::uint32_t>(i)), distance) ? 0 : 1;
      results[s].far_from_drawn += static_cast<std::size_t>(far[s][i]);
    }
  }
  for (const auto& view : exposure_views()) {
    if (options.cancelled && options.cancelled()) break;
    float u_min = std::numeric_limits<float>::max(), u_max = -u_min, v_min = u_min, v_max = -u_min;
    for (const auto& xyz : positions) {
      for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) {
        const V3 p{xyz[i], xyz[i + 1], xyz[i + 2]};
        const float u = dot(p, view.right), v = dot(p, view.up);
        u_min = std::min(u_min, u);
        u_max = std::max(u_max, u);
        v_min = std::min(v_min, v);
        v_max = std::max(v_max, v);
      }
    }
    if (u_min > u_max) break;
    DepthBuffer buffer;
    buffer.size = std::max(16, options.exposure_resolution);
    const float extent = std::max(u_max - u_min, v_max - v_min) * 1.02f + 1e-3f;
    buffer.pixel = extent / static_cast<float>(buffer.size);
    buffer.u0 = (u_min + u_max) * 0.5f - extent * 0.5f;
    buffer.v0 = (v_min + v_max) * 0.5f - extent * 0.5f;
    buffer.depth.assign(static_cast<std::size_t>(buffer.size) * static_cast<std::size_t>(buffer.size), std::numeric_limits<float>::infinity());
    for (const auto& o : occluders) rasterize(buffer, view, *o.positions, *o.indices);
    for (std::size_t s = 0; s < subjects.size(); ++s) {
      for (std::size_t i = 0; i < results[s].vertices; ++i) {
        if (!far[s][i]) continue;
        const V3 p = at(positions[s], static_cast<std::uint32_t>(i));
        const V3 n = normals[s].size() == positions[s].size() ? at(normals[s], static_cast<std::uint32_t>(i)) : V3{0, 0, 0};
        if (dot(n, view.forward) > -0.25f) continue;  // not facing this camera
        const int px = static_cast<int>(std::floor((dot(p, view.right) - buffer.u0) / buffer.pixel));
        const int py = static_cast<int>(std::floor((dot(p, view.up) - buffer.v0) / buffer.pixel));
        if (px < 0 || py < 0 || px >= buffer.size || py >= buffer.size) continue;
        const float drawn_depth = buffer.at(px, py);
        const float depth = dot(p, view.forward);
        if (drawn_depth <= depth - kTolerance) continue;  // something drawn in front: covered from this view
        if (std::isinf(drawn_depth)) exposed[s][i] |= 2;
        else if (drawn_depth > depth + options.see_through_gap) exposed[s][i] |= 1;
      }
    }
  }
  for (std::size_t s = 0; s < subjects.size(); ++s) {
    auto& result = results[s];
    float z_min = std::numeric_limits<float>::max(), z_max = -z_min;
    float t_min = z_min, t_max = z_max, o_min = z_min, o_max = z_max;
    for (std::size_t i = 0; i < result.vertices; ++i) {
      if (!exposed[s][i]) continue;
      ++result.exposed;
      const float height = positions[s][i * 3 + 1];  // view-space Y is Skyrim Z
      z_min = std::min(z_min, height);
      z_max = std::max(z_max, height);
      if (exposed[s][i] & 1) {
        ++result.see_through;
        t_min = std::min(t_min, height);
        t_max = std::max(t_max, height);
      } else {
        ++result.outside;
        o_min = std::min(o_min, height);
        o_max = std::max(o_max, height);
      }
    }
    if (result.see_through) {
      result.see_through_min_z = t_min;
      result.see_through_max_z = t_max;
    }
    if (result.outside) {
      result.outside_min_z = o_min;
      result.outside_max_z = o_max;
    }
    result.exposed_ratio = result.vertices ? static_cast<float>(result.exposed) / static_cast<float>(result.vertices) : 0.0f;
    if (result.exposed) {
      result.exposed_min_z = z_min;
      result.exposed_max_z = z_max;
    }
  }
  return results;
}

bool is_body_shape(const RenderPart& part) {
  if (part.group == "head" || part.shader_type != nif::kShaderSkinTint) return false;
  if (has_slot(part.slot_mask, 32) && (part.partitions.empty() ||
                                       std::find(part.partitions.begin(), part.partitions.end(), std::uint16_t{32}) != part.partitions.end()))
    return true;
  return std::find(part.partitions.begin(), part.partitions.end(), std::uint16_t{32}) != part.partitions.end();
}

void add(FidelityReport& report, FidelityFinding finding) {
  switch (finding.category) {
    case FindingCategory::Data: ++report.data_findings; break;
    case FindingCategory::Compatibility: ++report.compatibility_findings; break;
    case FindingCategory::Renderer: ++report.renderer_findings; break;
  }
  if (finding.severity == "error" && finding.category != FindingCategory::Renderer) {
    const auto entry = finding.code + ": " + (finding.piece.empty() ? finding.subject : finding.piece);
    if (finding.category == FindingCategory::Data && finding.confidence == FindingConfidence::Confirmed) report.export_blockers.push_back(entry);
    else report.export_warnings.push_back(entry);
  }
  report.findings.push_back(std::move(finding));
}

std::string piece_label(const RenderPart& part) { return part.label.empty() ? part.shape_name : part.label; }

}  // namespace

std::string_view to_string(FindingCategory category) {
  switch (category) {
    case FindingCategory::Data: return "data";
    case FindingCategory::Compatibility: return "compatibility";
    case FindingCategory::Renderer: return "renderer";
  }
  return "data";
}

std::string_view to_string(FindingConfidence confidence) {
  switch (confidence) {
    case FindingConfidence::Confirmed: return "confirmed";
    case FindingConfidence::Probable: return "probable";
    case FindingConfidence::Uncertain: return "uncertain";
  }
  return "uncertain";
}

std::vector<float> part_positions(const RenderPart& part) {
  std::vector<float> out;
  const std::size_t n = part.vertices.size() / kFloatsPerVertex;
  out.reserve(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    const float* v = part.vertices.data() + i * kFloatsPerVertex;
    out.insert(out.end(), {v[0], v[1], v[2]});
  }
  return out;
}

std::vector<float> part_normals(const RenderPart& part) {
  std::vector<float> out;
  const std::size_t n = part.vertices.size() / kFloatsPerVertex;
  out.reserve(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    const float* v = part.vertices.data() + i * kFloatsPerVertex;
    out.insert(out.end(), {v[3], v[4], v[5]});
  }
  return out;
}

// ---- TriangleGrid -----------------------------------------------------------------------------------------------------------

TriangleGrid::TriangleGrid(const std::vector<float>& positions, const std::vector<std::uint32_t>& indices, float reach)
    : reach_(std::max(reach, 1e-3f)) {
  const std::size_t vertex_count = positions.size() / 3;
  V3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  V3 hi{-lo[0], -lo[1], -lo[2]};
  for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
    if (indices[t] >= vertex_count || indices[t + 1] >= vertex_count || indices[t + 2] >= vertex_count) continue;
    Tri tri{at(positions, indices[t]), at(positions, indices[t + 1]), at(positions, indices[t + 2]), {}, {}, {}};
    const V3 n = cross(sub(tri.b, tri.a), sub(tri.c, tri.a));
    const float len = std::sqrt(dot(n, n));
    if (len <= 1e-12f) continue;
    tri.n = mul(n, 1.0f / len);
    for (std::size_t k = 0; k < 3; ++k) {
      tri.lo[k] = std::min({tri.a[k], tri.b[k], tri.c[k]});
      tri.hi[k] = std::max({tri.a[k], tri.b[k], tri.c[k]});
    }
    for (const V3* p : {&tri.a, &tri.b, &tri.c}) {
      for (std::size_t k = 0; k < 3; ++k) {
        lo[k] = std::min(lo[k], (*p)[k]);
        hi[k] = std::max(hi[k], (*p)[k]);
      }
    }
    tris_.push_back(tri);
  }
  if (tris_.empty()) return;
  cell_ = reach_;
  const auto dims_for = [&](float cell) {
    for (std::size_t k = 0; k < 3; ++k) dims_[k] = std::max(1, static_cast<int>(std::ceil((hi[k] - lo[k] + 2 * reach_) / cell)));
  };
  for (std::size_t k = 0; k < 3; ++k) origin_[k] = lo[k] - reach_;
  dims_for(cell_);
  // Keep the grid bounded on huge or degenerate inputs: coarser cells, same results.
  while (static_cast<std::size_t>(dims_[0]) * static_cast<std::size_t>(dims_[1]) * static_cast<std::size_t>(dims_[2]) > 4'000'000) {
    cell_ *= 2.0f;
    dims_for(cell_);
  }
  cells_.assign(static_cast<std::size_t>(dims_[0]) * static_cast<std::size_t>(dims_[1]) * static_cast<std::size_t>(dims_[2]), {});
  for (std::size_t t = 0; t < tris_.size(); ++t) {
    const auto& tri = tris_[t];
    std::array<int, 3> c0{}, c1{};
    for (std::size_t k = 0; k < 3; ++k) {
      const float mn = std::min({tri.a[k], tri.b[k], tri.c[k]}) - reach_;
      const float mx = std::max({tri.a[k], tri.b[k], tri.c[k]}) + reach_;
      c0[k] = std::clamp(static_cast<int>(std::floor((mn - origin_[k]) / cell_)), 0, dims_[k] - 1);
      c1[k] = std::clamp(static_cast<int>(std::floor((mx - origin_[k]) / cell_)), 0, dims_[k] - 1);
    }
    for (int z = c0[2]; z <= c1[2]; ++z)
      for (int y = c0[1]; y <= c1[1]; ++y)
        for (int x = c0[0]; x <= c1[0]; ++x) cells_[cell_index(x, y, z)].push_back(static_cast<std::uint32_t>(t));
  }
}

std::size_t TriangleGrid::cell_index(int x, int y, int z) const {
  return (static_cast<std::size_t>(z) * static_cast<std::size_t>(dims_[1]) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(dims_[0]) +
         static_cast<std::size_t>(x);
}

bool TriangleGrid::closest(const std::array<float, 3>& p, float& signed_distance) const {
  if (cells_.empty()) return false;
  std::array<int, 3> c{};
  for (std::size_t k = 0; k < 3; ++k) {
    c[k] = static_cast<int>(std::floor((p[k] - origin_[k]) / cell_));
    if (c[k] < 0 || c[k] >= dims_[k]) return false;  // outside the padded bounds: farther than reach from every triangle
  }
  float best = reach_ * reach_;
  bool found = false;
  // Every triangle within reach of any point of this cell was inserted into it (triangle bounds padded by reach).
  for (const auto t : cells_[cell_index(c[0], c[1], c[2])]) {
    const auto& tri = tris_[t];
    float box = 0.0f;  // squared distance from p to the triangle's bounds: a lower bound of the exact distance
    for (std::size_t k = 0; k < 3; ++k) {
      const float outside = std::max({tri.lo[k] - p[k], 0.0f, p[k] - tri.hi[k]});
      box += outside * outside;
    }
    if (box >= best) continue;
    const V3 q = closest_on_triangle(p, tri.a, tri.b, tri.c);
    const V3 d = sub(p, q);
    const float d2 = dot(d, d);
    if (d2 >= best) continue;
    best = d2;
    signed_distance = dot(d, tri.n) < 0 ? -std::sqrt(d2) : std::sqrt(d2);
    found = true;
  }
  return found;
}

GarmentFit measure_fit(const std::vector<float>& body_positions, const std::vector<std::uint32_t>& body_indices,
                       const std::vector<float>& garment_positions, float reach, float tolerance) {
  return measure_fit(TriangleGrid(body_positions, body_indices, reach), garment_positions, tolerance);
}

GarmentFit measure_fit(const TriangleGrid& grid, const std::vector<float>& garment_positions, float tolerance) {
  GarmentFit fit;
  fit.vertices = garment_positions.size() / 3;
  double gap_sum = 0.0;
  for (std::size_t i = 0; i < fit.vertices; ++i) {
    float distance = 0.0f;
    if (!grid.closest(at(garment_positions, static_cast<std::uint32_t>(i)), distance)) continue;
    ++fit.near_body;
    gap_sum += distance;
    if (distance < -tolerance) {
      ++fit.inside;
      fit.max_depth = std::max(fit.max_depth, -distance);
    }
  }
  if (fit.near_body) {
    fit.mean_gap = static_cast<float>(gap_sum / static_cast<double>(fit.near_body));
    fit.inside_ratio = static_cast<float>(fit.inside) / static_cast<float>(fit.near_body);
  }
  return fit;
}

// ---- analysis ---------------------------------------------------------------------------------------------------------------

FidelityReport analyze_fidelity(const Appearance& appearance, const CharacterScene& scene, const FidelityOptions& options) {
  const auto started = std::chrono::steady_clock::now();
  FidelityReport report;
  report.npc_form_key = appearance.npc_form_key;
  const auto stop = [&] { return options.cancelled && options.cancelled() && (report.cancelled = true); };

  std::map<std::string, std::vector<const RenderPart*>> parts_of;  // by armor FormKey
  for (const auto& part : scene.parts) parts_of[part.armor_form_key].push_back(&part);
  std::map<std::string, const OutfitPieceDiagnostic*> diag_of;
  for (const auto& d : scene.outfit_diagnostics) diag_of[d.armor_form_key] = &d;

  // -- resource ledger ------------------------------------------------------------------------------------------------------
  if (!appearance.skeleton.empty())
    report.resources.push_back({"skeleton", appearance.skeleton, "", "", "esqueleto de la raza efectiva (SkeletalModel del sexo del NPC)"});
  for (const auto& piece : appearance.pieces) {
    report.resources.push_back({"armor", piece.armor_form_key, "registro ganador del orden de carga", piece.name,
                                piece.source == "skin" ? "piel del NPC (WNAM) o de su raza" : "pieza del atuendo / prendas en edición"});
    for (const auto& addon : piece.addons) {
      report.resources.push_back({"addon", addon.form_key, "registro ganador del orden de carga", piece.name,
                                  std::format("ARMA válido para la raza y el sexo del NPC; slots {}{}", describe_slots(slots_from_mask(addon.slot_mask)),
                                              addon.weight_slider ? "; usa variantes de peso _0/_1" : "")});
    }
  }
  std::set<std::string> seen_paths;
  for (const auto& part : scene.parts) {
    if (!part.mesh_path.empty() && seen_paths.insert("m|" + part.mesh_path).second) {
      report.resources.push_back({part.group == "head" ? "facegen-geometry" : "mesh", part.mesh_path,
                                  part.mesh_location.empty() ? "missing" : part.mesh_location, piece_label(part),
                                  part.group == "head" ? "FaceGen del NPC que da los rasgos (plantilla Traits si la hay)"
                                                       : "archivo ganador en el VFS de MO2 (suelto sobre BSA, prioridad del perfil)"});
    }
    if (!part.physics_xml.empty() && seen_paths.insert("p|" + part.physics_xml).second) {
      report.resources.push_back({"physics", part.physics_xml, part.physics_xml_location, piece_label(part), "XML SMP nombrado por la malla"});
    }
  }
  for (const auto& [key, info] : scene.texture_info) {
    report.resources.push_back({info.derived_from.empty() ? "texture" : "texture-composite", info.path.empty() ? key : info.path, info.location, "",
                                info.location == "missing" ? "no existe ni suelta ni en los BSA cargados" : "textura ganadora en el VFS de MO2"});
  }
  for (const auto& morph : scene.body_morphs) {
    if (!morph.tri_path.empty() && seen_paths.insert("t|" + morph.tri_path).second)
      report.resources.push_back({"tri", morph.tri_path, morph.tri_provider.empty() ? "missing" : morph.tri_provider, morph.piece,
                                  "TRI que nombra BODYTRI (morphs de BodySlide que aplica RaceMenu)"});
  }

  // -- data findings from the scene ------------------------------------------------------------------------------------------
  for (const auto& issue : scene.issues) {
    if (issue.code == "MeshNotFound" || issue.code == "MeshUnreadable") {
      add(report, {issue.code == "MeshNotFound" ? "MeshMissing" : "MeshUnreadable", FindingCategory::Data, "error", FindingConfidence::Confirmed, "", "",
                   issue.subject,
                   issue.code == "MeshNotFound" ? "La malla no existe en el VFS de MO2 ni en los BSA cargados: el juego no dibuja la pieza."
                                                : "La malla no se puede leer: el juego tampoco la dibujará o puede cerrarse.",
                   issue.message, "Instala o reactiva el mod que aporta la malla, o corrige la ruta del ARMA.", {}});
    } else if (issue.code == "WeightTopologyMismatch") {
      add(report, {"WeightVariantMismatch", FindingCategory::Data, "warning", FindingConfidence::Confirmed, "", "", issue.subject,
                   "Las mallas _0 y _1 no coinciden vértice a vértice: el juego mezcla ambas por peso y la prenda se deforma.", issue.message,
                   "Reconstruye ambas variantes con BodySlide (Build) desde el mismo proyecto.", {}});
    } else if (issue.code == "PhysicsXmlMissing") {
      add(report, {"PhysicsXmlMissing", FindingCategory::Data, "warning", FindingConfidence::Confirmed, "", "", issue.subject,
                   "La malla nombra un XML de físicas SMP que no está instalado: en el juego la prenda no tendrá físicas.", issue.message,
                   "Instala el XML que falta (suele venir con el mod original de la prenda).", {}});
    } else if (issue.code == "SkeletonMissing" || issue.code == "SkeletonUnreadable") {
      add(report, {"SkeletonMissing", FindingCategory::Data, "error", FindingConfidence::Confirmed, "", "", issue.subject,
                   "El esqueleto de la raza no existe o no se puede leer.", issue.message, "Instala el esqueleto (XPMSE u otro) que espera la raza.", {}});
    }
  }
  for (const auto& [key, info] : scene.texture_info) {
    if (info.location != "missing") continue;
    std::vector<std::string> ids;
    std::string owner;
    for (const auto& part : scene.parts) {
      if (part.diffuse == key || part.normal == key) {
        ids.push_back(part.id);
        if (owner.empty()) owner = piece_label(part);
      }
    }
    add(report, {"TextureMissing", FindingCategory::Data, "warning", FindingConfidence::Confirmed, "", owner, info.path.empty() ? key : info.path,
                 "La textura no existe en el VFS de MO2 ni en los BSA cargados: en el juego esa superficie se verá sin textura o de color plano.",
                 "ubicación: missing", "Instala el mod de texturas que falta o corrige la ruta en el NIF.", ids});
  }
  for (const auto& part : scene.parts) {
    if (part.bones_missing.empty()) continue;
    add(report, {"BonesMissing", FindingCategory::Compatibility, "warning", FindingConfidence::Probable, part.armor_form_key, piece_label(part), part.mesh_path,
                 "La malla usa huesos que no están en el esqueleto de la raza ni en el propio NIF: en el juego esos vértices no siguen al cuerpo.",
                 std::format("{} huesos ausentes, p. ej. {}", part.bones_missing.size(), part.bones_missing.front()),
                 "Instala el esqueleto que la prenda espera (XPMSE/3BA/UBE) o reconstrúyela para este cuerpo.", {part.id}});
  }

  // -- pieces: records, unequip, race ----------------------------------------------------------------------------------------
  for (const auto& d : scene.outfit_diagnostics) {
    if (d.no_addon_for_race) {
      add(report, {"PieceNoAddonForRace", FindingCategory::Data, "error", FindingConfidence::Confirmed, d.armor_form_key, d.name, d.armor_form_key,
                   "La pieza no tiene ARMA para la raza y el sexo del NPC: en el juego ocupa sus slots y no dibuja nada (y oculta la piel de esos slots).",
                   "slots " + describe_slots(d.slot_indices),
                   "Añade un ARMA para esta raza (parche de compatibilidad, p. ej. el de UBE) o cambia la raza del NPC.", {}});
    } else if (!d.equipped) {
      add(report, {"PieceUnequipped", FindingCategory::Data, "info", FindingConfidence::Confirmed, d.armor_form_key, d.name, d.armor_form_key,
                   "La pieza no se equipa: otra pieza posterior del atuendo usa los mismos slots.",
                   std::format("conflicto con {} en {}", d.unequipped_by, describe_slots(d.conflicting_slots)),
                   "Quita una de las dos piezas del atuendo o cambia los slots de una de ellas.", {}});
    } else if (!d.meshes_exist) {
      add(report, {"PieceMeshMissing", FindingCategory::Data, "error", FindingConfidence::Confirmed, d.armor_form_key, d.name, d.armor_form_key,
                   "El ARMA de la pieza apunta a una malla que no existe: en el juego la pieza es invisible.", d.reason,
                   "Instala la malla o corrige la ruta del ARMA.", {}});
    }
  }

  // -- dismember partitions (probable engine rule; this viewer draws whole shapes) -------------------------------------------
  std::map<std::string, DrawnTriangles> drawn_in_game;  // by part id, only parts the viewer draws
  std::map<int, std::vector<const RenderPart*>> drawn_partition_owners;
  for (const auto& part : scene.parts) {
    if (part.partition_hidden) {
      std::set<int> slots(part.triangle_partitions.begin(), part.triangle_partitions.end());
      std::string names;
      for (const int sl : slots) names += (names.empty() ? "" : ", ") + describe_slot(sl);
      add(report, {"PartitionNotInAddonSlots", FindingCategory::Compatibility, "warning", FindingConfidence::Probable, part.armor_form_key, piece_label(part),
                   part.mesh_path,
                   std::format("«{}» está en particiones BSDismember ({}) que su ARMA no ocupa: el juego no la dibuja y el visor la oculta igual.",
                               part.shape_name, names),
                   std::format("{} triángulos fuera de los slots del ARMA ({})", part.indices.size() / 3, describe_slots(slots_from_mask(part.slot_mask))),
                   "Si debe verse, haz que la partición coincida con un slot del ARMA (Outfit Studio → Particiones) o añade ese slot al ARMA.", {part.id}});
      continue;
    }
    if (!part.shown_by_default) continue;
    auto drawn = triangles_drawn_in_game(part);
    if (drawn.dropped) {
      std::string slots;
      for (const int s : drawn.unclaimed) slots += (slots.empty() ? "" : ", ") + describe_slot(s);
      const std::size_t triangles = std::max<std::size_t>(1, part.indices.size() / 3);
      add(report, {"PartitionNotInAddonSlots", FindingCategory::Compatibility, "warning", FindingConfidence::Probable, part.armor_form_key, piece_label(part),
                   part.mesh_path,
                   std::format("La forma «{}» tiene triángulos en particiones BSDismember ({}) que su ARMA no ocupa: el juego no dibuja esa zona y este visor sí.",
                               part.shape_name, slots),
                   std::format("{} de {} triángulos ({}) en particiones fuera de los slots del ARMA ({})", drawn.dropped, part.indices.size() / 3,
                               percent(static_cast<double>(drawn.dropped) / static_cast<double>(triangles)), describe_slots(slots_from_mask(part.slot_mask))),
                   "Haz que la partición coincida con un slot del ARMA (Outfit Studio → Particiones) o añade ese slot al ARMA.", {part.id}});
    }
    if (part.group != "head") {
      std::set<int> claimed;
      for (const auto id : part.partitions)
        if (is_slot_partition(id) && has_slot(part.slot_mask, id)) claimed.insert(id);
      for (const int s : claimed) drawn_partition_owners[s].push_back(&part);
    }
    drawn_in_game.emplace(part.id, std::move(drawn));
  }
  // Different drawn addons with geometry in the same claimed slot: the game keeps one (addon priority), this viewer draws all.
  for (const auto& [slot, owners] : drawn_partition_owners) {
    std::map<std::string, const RenderPart*> by_addon;
    for (const auto* p : owners) by_addon.emplace(p->addon_form_key, p);
    if (by_addon.size() < 2) continue;
    std::string names;
    std::vector<std::string> ids;
    for (const auto& [addon, p] : by_addon) {
      names += (names.empty() ? "" : ", ") + piece_label(*p);
      ids.push_back(p->id);
    }
    add(report, {"AddonSlotOverlap", FindingCategory::Compatibility, "warning", FindingConfidence::Uncertain, "", names, describe_slot(slot),
                 std::format("Varios ARMA dibujan geometría en el slot {}: el juego solo muestra el de mayor prioridad de ARMA; el visor dibuja todos.",
                             describe_slot(slot)),
                 "ARMA: " + names + " (la prioridad de cada ARMA no llega a los datos del visor)",
                 "Comprueba la prioridad de los ARMA o libera el slot donde no debe dibujar (Outfit Studio / xEdit).", ids});
  }

  if (stop()) return report;
  // -- body and garment fit ---------------------------------------------------------------------------------------------------
  const auto fit_started = std::chrono::steady_clock::now();
  const RenderPart* body = nullptr;
  for (const auto& part : scene.parts) {
    if (!part.shown_by_default || !is_body_shape(part)) continue;
    if (!body || part.vertex_count > body->vertex_count) body = &part;
  }
  bool body_hidden = false;
  if (!body) {
    for (const auto& part : scene.parts) {
      if (part.group != "skin" || !is_body_shape(part)) continue;
      if (!body || part.vertex_count > body->vertex_count) body = &part;
    }
    body_hidden = body != nullptr;
  }
  if (body) {
    report.body_part_id = body->id;
    report.body_source = body->group == "skin" ? (body_hidden ? "skin-hidden" : "skin") : "outfit";
    report.body_mesh = body->mesh_path;
    report.body_provider = body->mesh_location;
    const auto body_positions = part_positions(*body);
    const auto drawn_body = drawn_in_game.find(body->id);
    const auto& body_triangles = drawn_body != drawn_in_game.end() ? drawn_body->second.indices : body->indices;
    const TriangleGrid body_grid(body_positions, body_triangles, options.penetration_reach);  // once for every garment
    std::map<std::string, const ShapeMorphState*> morph_of;  // part id -> TRI vocabulary verdict
    for (const auto& m : scene.body_morphs) morph_of[m.part_id] = &m;
    for (const auto& part : scene.parts) {
      if (!part.shown_by_default || part.group != "outfit" || &part == body || part.mesh_path == body->mesh_path) continue;
      if (part.shader_type == nif::kShaderSkinTint) continue;  // another body/hands copy, not a garment surface
      if (stop()) return report;
      auto fit = measure_fit(body_grid, part_positions(part), options.penetration_tolerance);
      fit.part_id = part.id;
      fit.body_part_id = body->id;
      fit.piece = piece_label(part);
      fit.shape = part.shape_name;
      fit.body_shape = body->shape_name;
      const float near_share = fit.vertices ? static_cast<float>(fit.near_body) / static_cast<float>(fit.vertices) : 0.0f;
      if (fit.near_body < 16 || near_share < 0.02f) fit.verdict = "not-near-body";
      else if (fit.inside_ratio >= options.clip_badly_ratio) fit.verdict = "clips-badly";
      else if (fit.inside_ratio >= options.clip_ratio) fit.verdict = "clips";
      else fit.verdict = "fits";
      if (fit.verdict == "clips" || fit.verdict == "clips-badly") {
        const auto m = morph_of.find(part.id);
        const std::string family = m != morph_of.end() ? m->second->family_verdict : std::string();
        std::string evidence = std::format(
            "{} de {} vértices cercanos al cuerpo quedan dentro ({}), profundidad máx. {:.2f}, separación media {:.2f} (unidades de Skyrim, pose de referencia)",
            fit.inside, fit.near_body, percent(fit.inside_ratio), fit.max_depth, fit.mean_gap);
        if (!family.empty()) evidence += "; vocabulario de morphs frente al cuerpo: " + family + (m->second->family_reason.empty() ? "" : " (" + m->second->family_reason + ")");
        add(report, {"GarmentClipsBody", FindingCategory::Compatibility, fit.verdict == "clips-badly" ? "error" : "warning", FindingConfidence::Confirmed,
                     part.armor_form_key, fit.piece, part.mesh_path,
                     std::format("El cuerpo atraviesa «{}»: la prenda no se construyó para este cuerpo/preset.", part.shape_name), evidence,
                     family == "incompatible" ? "Usa una versión de la prenda para la familia del cuerpo (o conviértela con transferencia de pesos y morphs, no con escalado)."
                                              : "Reconstruye la prenda en BodySlide con el mismo preset que el cuerpo (con Build Morphs para RaceMenu).",
                     {part.id, body->id}});
      }
      report.fits.push_back(std::move(fit));
    }
  }
  report.fit_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fit_started).count();
  for (const auto& m : scene.body_morphs) {
    if (m.group != "outfit" || m.family_verdict != "incompatible" || !m.shown) continue;
    add(report, {"GarmentFamilyMismatch", FindingCategory::Compatibility, "warning", FindingConfidence::Confirmed, "", m.piece, m.mesh_path,
                 std::format("Los morphs de «{}» no son de la familia del cuerpo visible: no seguirán al preset del cuerpo en el juego.", m.shape),
                 m.family_reason, "Usa la versión de la prenda para el cuerpo del NPC (3BA/UBE/…) y constrúyela con Build Morphs.", {m.part_id}});
  }

  // -- hidden skin: does what is drawn replace it? (holes / missing limbs in game) ----------------------------------------------
  std::vector<const RenderPart*> hidden_skin;
  for (const auto& part : scene.parts) {
    if (part.group != "skin" || part.shown_by_default || part.shader_type != nif::kShaderSkinTint) continue;
    if (part.hidden_reason.find("covered by outfit") == std::string::npos) continue;
    hidden_skin.push_back(&part);
  }
  const auto exposure_started = std::chrono::steady_clock::now();
  if (!hidden_skin.empty()) {
    if (stop()) return report;
    std::vector<const RenderPart*> occluder_parts;
    for (const auto& part : scene.parts)
      if (part.shown_by_default) occluder_parts.push_back(&part);
    std::vector<std::vector<float>> occluder_positions;
    occluder_positions.reserve(occluder_parts.size());
    std::vector<Occluder> occluders;
    // All drawn triangles in one soup for the distance test.
    std::vector<float> drawn_positions;
    std::vector<std::uint32_t> drawn_indices;
    for (const auto* part : occluder_parts) {
      occluder_positions.push_back(part_positions(*part));
      const auto& tris = drawn_in_game.at(part->id).indices;
      occluders.push_back({&occluder_positions.back(), &tris});
      const auto base = static_cast<std::uint32_t>(drawn_positions.size() / 3);
      drawn_positions.insert(drawn_positions.end(), occluder_positions.back().begin(), occluder_positions.back().end());
      for (const auto i : tris) drawn_indices.push_back(base + i);
    }
    auto results = measure_exposure(hidden_skin, occluders, drawn_positions, drawn_indices, options);
    if (stop()) return report;
    for (std::size_t k = 0; k < hidden_skin.size(); ++k) {
      const auto& part = *hidden_skin[k];
      auto& exposed = results[k];
      exposed.part_id = part.id;
      exposed.piece = piece_label(part) + " / " + part.shape_name;
      exposed.covered_by = part.hidden_reason;
      const float vertices = static_cast<float>(std::max<std::size_t>(1, exposed.vertices));
      const float see_ratio = static_cast<float>(exposed.see_through) / vertices;
      const float out_ratio = static_cast<float>(exposed.outside) / vertices;
      const float out_span = exposed.outside_max_z - exposed.outside_min_z;
      if (see_ratio >= options.exposed_ratio) {
        add(report, {"HiddenSkinSeeThrough", FindingCategory::Compatibility, "warning", FindingConfidence::Probable, part.armor_form_key, exposed.piece,
                     part.mesh_path,
                     std::format("El atuendo oculta «{}» y por sus aberturas se vería el interior del personaje: en el juego ahí no hay ni piel ni prenda.",
                                 part.shape_name),
                     std::format("{} de {} vértices ({}) a más de {:.1f} unidades de lo dibujado, con lo siguiente dibujado a más de {:.1f} unidades detrás; altura {:.1f}..{:.1f}. {}",
                                 exposed.see_through, exposed.vertices, percent(see_ratio), options.exposure_margin, options.see_through_gap,
                                 exposed.see_through_min_z, exposed.see_through_max_z, exposed.covered_by),
                     "Usa una prenda que incluya el cuerpo bajo esas aberturas o libera ese slot para que se dibuje la piel.", {part.id}});
      }
      if (out_ratio >= options.exposed_ratio && out_span >= options.missing_part_span) {
        add(report, {"HiddenSkinMissingPart", FindingCategory::Compatibility, "error", FindingConfidence::Probable, part.armor_form_key, exposed.piece,
                     part.mesh_path,
                     std::format("El atuendo oculta «{}» y nada dibujado ocupa una zona de {:.0f} unidades de alto: en el juego faltará esa parte del cuerpo.",
                                 part.shape_name, out_span),
                     std::format("{} de {} vértices ({}) fuera de toda la silueta dibujada; altura {:.1f}..{:.1f}. {}", exposed.outside, exposed.vertices,
                                 percent(out_ratio), exposed.outside_min_z, exposed.outside_max_z, exposed.covered_by),
                     "Equipa la prenda que cubre esa zona, usa una que incluya el cuerpo o haz que la partición del cuerpo de la prenda coincida con sus slots.",
                     {part.id}});
      } else if (out_ratio >= options.exposed_ratio) {
        add(report, {"GarmentReshapesHiddenSkin", FindingCategory::Compatibility, "info", FindingConfidence::Probable, part.armor_form_key, exposed.piece,
                     part.mesh_path,
                     std::format("La prenda que oculta «{}» tiene otra forma (p. ej. tacones frente a un pie plano): la piel oculta sobresale de ella en la pose de referencia.",
                                 part.shape_name),
                     std::format("{} de {} vértices ({}) fuera de la silueta dibujada, en solo {:.1f} unidades de alto ({:.1f}..{:.1f}).", exposed.outside,
                                 exposed.vertices, percent(out_ratio), out_span, exposed.outside_min_z, exposed.outside_max_z),
                     "Nada que corregir si la prenda está pensada para reemplazar esa parte; compruébalo en el juego.", {part.id}});
      }
      report.exposed.push_back(std::move(exposed));
    }
  }

  report.exposure_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - exposure_started).count();

  // -- renderer differences -----------------------------------------------------------------------------------------------------
  for (const auto& part : scene.parts) {
    if (part.shown_by_default || part.hidden_reason.empty()) continue;
    if (part.hidden_reason.find("SMP collision") != std::string::npos || part.hidden_reason.find("head it was fitted on") != std::string::npos ||
        part.hidden_reason.find("reference head") != std::string::npos) {
      add(report, {"ViewerHidesShape", FindingCategory::Renderer, "info", FindingConfidence::Probable, part.armor_form_key, piece_label(part), part.mesh_path,
                   std::format("El visor oculta «{}» por una regla propia (no por un dato del registro).", part.shape_name), part.hidden_reason,
                   "Muéstrala desde el inspector si sospechas que el juego sí la dibuja.", {part.id}});
    }
  }
  std::size_t bones_from_mesh = 0;
  for (const auto& part : scene.parts) bones_from_mesh += part.bones_from_mesh.size();
  if (bones_from_mesh) {
    add(report, {"PhysicsBonesStatic", FindingCategory::Renderer, "info", FindingConfidence::Confirmed, "", "", "",
                 "Los huesos de físicas (SMP/CBPC) se dibujan en la pose de referencia del NIF: el visor no simula físicas.",
                 std::format("{} huesos aportados por las mallas", bones_from_mesh), "Comprueba las físicas en el juego.", {}});
  }
  add(report, {"PreviewLimits", FindingCategory::Renderer, "info", FindingConfidence::Confirmed, "", "", appearance.npc_form_key,
               "Vista en pose de referencia: sin shaders propios de Skyrim/Community Shaders, sin animación, sin físicas y sin morphs de RaceMenu salvo el preset aplicado.",
               "", "Valida en el juego lo que dependa de shaders, físicas o animación.", {}});

  // -- piece verdicts ---------------------------------------------------------------------------------------------------------
  for (const auto& piece : appearance.pieces) {
    PieceVerdict v;
    v.armor_form_key = piece.armor_form_key;
    v.name = piece.name;
    v.source = piece.source;
    const auto found = parts_of.find(piece.armor_form_key);
    std::size_t game_drawn_shapes = 0, partial_shapes = 0, partition_split = 0;
    if (found != parts_of.end()) {
      for (const auto* p : found->second) {
        v.part_ids.push_back(p->id);
        if (!p->shown_by_default) {
          ++v.shapes_hidden;
          if (p->partition_hidden) ++partition_split;
          continue;
        }
        ++v.shapes_drawn;
        const auto& d = drawn_in_game.at(p->id);
        if (d.indices.empty()) continue;
        ++game_drawn_shapes;
        if (d.dropped) ++partial_shapes;
      }
    }
    const auto diag = diag_of.find(piece.armor_form_key);
    const bool unequipped = diag != diag_of.end() && !diag->second->equipped;
    const bool no_addon = diag != diag_of.end() && diag->second->no_addon_for_race;
    if (unequipped || no_addon || game_drawn_shapes == 0) v.game = "not-drawn";
    else if (partial_shapes || partition_split) v.game = "partial";
    else v.game = "drawn";
    v.viewer = v.shapes_drawn ? (v.shapes_hidden ? "partial" : "drawn") : (found == parts_of.end() ? "nothing" : "hidden");
    const bool viewer_draws = v.viewer == "drawn" || v.viewer == "partial";
    // "partial" matches when the viewer already leaves out what the game leaves out (no drawn shape keeps unclaimed triangles).
    v.viewer_matches_game = v.game == "not-drawn" ? !viewer_draws
                            : v.game == "drawn"   ? viewer_draws
                                                  : viewer_draws && partial_shapes == 0;
    for (const auto& f : report.findings)
      if (f.armor_form_key == piece.armor_form_key && std::find(v.finding_codes.begin(), v.finding_codes.end(), f.code) == v.finding_codes.end())
        v.finding_codes.push_back(f.code);
    report.pieces.push_back(std::move(v));
  }

  report.analysis_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  return report;
}

nlohmann::json to_json(const FidelityReport& report) {
  nlohmann::json j;
  j["npcFormKey"] = report.npc_form_key;
  j["body"] = {{"partId", report.body_part_id}, {"source", report.body_source}, {"mesh", report.body_mesh}, {"provider", report.body_provider}};
  j["resources"] = nlohmann::json::array();
  for (const auto& r : report.resources)
    j["resources"].push_back({{"kind", r.kind}, {"key", r.key}, {"provider", r.provider}, {"piece", r.piece}, {"reason", r.reason}});
  j["pieces"] = nlohmann::json::array();
  for (const auto& p : report.pieces)
    j["pieces"].push_back({{"armorFormKey", p.armor_form_key}, {"name", p.name}, {"source", p.source}, {"game", p.game}, {"viewer", p.viewer},
                           {"viewerMatchesGame", p.viewer_matches_game}, {"shapesDrawn", p.shapes_drawn}, {"shapesHidden", p.shapes_hidden},
                           {"partIds", p.part_ids}, {"findings", p.finding_codes}});
  j["fits"] = nlohmann::json::array();
  for (const auto& f : report.fits)
    j["fits"].push_back({{"partId", f.part_id}, {"bodyPartId", f.body_part_id}, {"piece", f.piece}, {"shape", f.shape}, {"bodyShape", f.body_shape},
                         {"vertices", f.vertices}, {"nearBody", f.near_body}, {"inside", f.inside}, {"insideRatio", f.inside_ratio},
                         {"maxDepth", f.max_depth}, {"meanGap", f.mean_gap}, {"verdict", f.verdict}});
  j["exposed"] = nlohmann::json::array();
  for (const auto& e : report.exposed)
    j["exposed"].push_back({{"partId", e.part_id}, {"piece", e.piece}, {"coveredBy", e.covered_by}, {"vertices", e.vertices},
                            {"farFromDrawn", e.far_from_drawn}, {"exposed", e.exposed}, {"seeThrough", e.see_through}, {"outside", e.outside},
                            {"seeThroughMinZ", e.see_through_min_z}, {"seeThroughMaxZ", e.see_through_max_z},
                            {"outsideMinZ", e.outside_min_z}, {"outsideMaxZ", e.outside_max_z},
                            {"exposedRatio", e.exposed_ratio}, {"exposedMinZ", e.exposed_min_z}, {"exposedMaxZ", e.exposed_max_z}});
  j["findings"] = nlohmann::json::array();
  for (const auto& f : report.findings)
    j["findings"].push_back({{"code", f.code}, {"category", std::string(to_string(f.category))}, {"severity", f.severity},
                             {"confidence", std::string(to_string(f.confidence))}, {"armorFormKey", f.armor_form_key}, {"piece", f.piece},
                             {"subject", f.subject}, {"message", f.message}, {"evidence", f.evidence}, {"action", f.action}, {"partIds", f.part_ids}});
  j["counts"] = {{"data", report.data_findings}, {"compatibility", report.compatibility_findings}, {"renderer", report.renderer_findings}};
  j["exportBlockers"] = report.export_blockers;
  j["exportWarnings"] = report.export_warnings;
  j["analysisMs"] = report.analysis_ms;
  j["fitMs"] = report.fit_ms;
  j["exposureMs"] = report.exposure_ms;
  return j;
}

}  // namespace sns::rendering
