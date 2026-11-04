// Geometric clipping and garment-fit analyzer for Skyrim NPC Studio Visual QA Lab.
// Implements AABB broad-phase, BVH spatial indexing, exact triangle-triangle intersection,
// exterior protrusion verification, anatomical zone clustering, and false-positive filtering.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "sns/rendering/scene.hpp"
#include "sns/visual_qa/visual_qa_types.hpp"

namespace sns::visual_qa {

// 3D Axis-Aligned Bounding Box in Y-up view space.
struct Aabb3D {
  std::array<float, 3> min{0.0f, 0.0f, 0.0f};
  std::array<float, 3> max{0.0f, 0.0f, 0.0f};

  [[nodiscard]] bool valid() const { return min[0] <= max[0] && min[1] <= max[1] && min[2] <= max[2]; }
  [[nodiscard]] bool overlaps(const Aabb3D& other, float margin = 0.0f) const {
    return (min[0] - margin <= other.max[0] && max[0] + margin >= other.min[0]) &&
           (min[1] - margin <= other.max[1] && max[1] + margin >= other.min[1]) &&
           (min[2] - margin <= other.max[2] && max[2] + margin >= other.min[2]);
  }
  void expand(const nif::Vec3& p);
  void expand(const Aabb3D& other);
  [[nodiscard]] nif::Vec3 center() const {
    return {(min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f};
  }
  [[nodiscard]] nif::Vec3 size() const {
    return {max[0] - min[0], max[1] - min[1], max[2] - min[2]};
  }
};

// Result of testing two 3D triangles for non-degenerate geometric intersection.
struct TriangleIntersectionResult {
  bool intersects = false;
  nif::Vec3 point_a{};           // First endpoint of the intersection segment
  nif::Vec3 point_b{};           // Second endpoint of the intersection segment
  float segment_length = 0.0f;   // Length of the intersection line segment between the two triangles
  float exterior_protrusion = 0.0f;  // Max signed distance a body vertex protrudes to the exterior (+normal) side of the garment triangle
};

// Exact 3D triangle-triangle intersection test (Moller segment-piercing method with grazing tolerance).
[[nodiscard]] TriangleIntersectionResult test_triangle_triangle_intersection(
    const nif::Vec3& a0, const nif::Vec3& a1, const nif::Vec3& a2, const nif::Vec3& norm_a,
    const nif::Vec3& b0, const nif::Vec3& b1, const nif::Vec3& b2, const nif::Vec3& norm_b);

// Clustered penetration incident localized to one anatomical zone between a body surface and a garment surface.
struct ZoneClippingIncident {
  AnatomicalZone zone = AnatomicalZone::ChestBreasts;
  std::string body_part_id;
  std::string body_shape_name;
  std::string body_mesh_path;
  SurfaceRole body_role = SurfaceRole::VisibleBodySkin;
  std::string garment_part_id;
  std::string garment_shape_name;
  std::string garment_mesh_path;
  std::string garment_piece_label;
  std::string armor_form_key;
  std::size_t intersecting_triangle_pairs = 0;
  std::size_t protruding_body_triangles = 0;
  float max_exterior_protrusion = 0.0f;
  float mean_exterior_protrusion = 0.0f;
  float max_segment_length = 0.0f;
  Aabb3D penetration_bounds{};
  nif::Vec3 centroid{};
  ClippingClassification classification = ClippingClassification::GeometricIntersectionDetected;
  std::string explanation;
};

// Fit evaluation of a garment shape relative to the underlying/visible body surface in overlapping vertical slices.
struct GarmentFitDiagnostic {
  std::string garment_part_id;
  std::string garment_piece_label;
  std::string armor_form_key;
  std::string garment_mesh_path;
  std::string garment_shape_name;
  std::string provider;
  std::string body_reference_shape;
  std::string fit_verdict;  // "WELL_FITTED", "TOO_SMALL_CLIPPING", "TOO_LARGE_FLOATING", "EMBEDDED_BODY_LEAK", "NO_OVERLAPPING_BODY"
  float mean_radial_clearance = 0.0f;  // Signed radial distance (garment_radius - body_radius) in Skyrim units
  float min_radial_clearance = 0.0f;
  float max_radial_clearance = 0.0f;
  float scale_ratio_to_body = 1.0f;    // Mean ratio of garment radial extent to body radial extent
  std::size_t slices_compared = 0;
  std::string explanation;
};

// Per-garment clipping and fit diagnosis.
struct GarmentClippingDiagnostic {
  std::string piece_label;
  std::string armor_form_key;
  std::string mesh_path;
  std::string shape_name;
  std::string provider;
  std::vector<int> biped_slots;
  std::size_t total_intersecting_pairs = 0;
  std::size_t confirmed_clipping_zones = 0;
  std::size_t possible_clipping_zones = 0;
  std::size_t geometric_only_zones = 0;
  float worst_protrusion = 0.0f;
  ClippingClassification overall_classification = ClippingClassification::GeometricIntersectionDetected;
  QaVerdict verdict = QaVerdict::Pass;
  std::vector<ZoneClippingIncident> incidents;
  GarmentFitDiagnostic fit{};
};

// Record of a shape or pair excluded by false-positive filtering.
struct ExcludedSurfaceRecord {
  std::string part_id;
  std::string shape_name;
  std::string mesh_path;
  std::string piece_label;
  SurfaceRole role = SurfaceRole::HiddenNifShape;
  ClippingClassification exclusion_kind = ClippingClassification::ExcludedHiddenNifShape;
  std::string reason;
};

struct ClippingAnalysisOptions {
  // Minimum signed distance along outward garment normal to count as exterior protrusion (Skyrim units).
  float min_protrusion_possible = 0.06f;
  float min_protrusion_confirmed = 0.22f;
  // Minimum intersecting triangle pairs in a zone to promote from possible to confirmed when protrusion is high.
  std::size_t min_confirmed_tri_pairs = 3;
  // Radial clearance above which a fitted torso/limb garment is flagged as abnormally oversized/floating.
  float max_normal_garment_clearance = 6.5f;
  // Whether to also inspect covered skin addons (group=="skin", shown_by_default==false) for fit comparison,
  // while keeping their intersections classified as ExcludedCoveredByBipedSlot.
  bool analyze_covered_skin_fit = true;
};

struct ClippingAnalysisReport {
  QaVerdict verdict = QaVerdict::Pass;
  std::size_t parts_inspected = 0;
  std::size_t visible_body_parts = 0;
  std::size_t visible_garment_parts = 0;
  std::size_t excluded_parts = 0;
  std::size_t broad_phase_pairs_tested = 0;
  std::size_t broad_phase_pairs_overlapping = 0;
  std::size_t bvh_candidate_tri_pairs = 0;
  std::size_t exact_intersecting_tri_pairs = 0;
  std::size_t confirmed_visual_clipping_count = 0;
  std::size_t possible_visible_clipping_count = 0;
  std::size_t geometric_only_intersection_count = 0;
  std::size_t covered_slot_excluded_intersections = 0;
  std::size_t intentional_overlap_excluded_count = 0;
  std::vector<GarmentClippingDiagnostic> garment_diagnostics;
  std::vector<ZoneClippingIncident> all_incidents;
  std::vector<ExcludedSurfaceRecord> excluded_surfaces;
  std::vector<VisualQaIssue> issues;
};

// Runs the complete clipping and garment-fit analysis on an already built CharacterScene.
[[nodiscard]] ClippingAnalysisReport analyze_scene_clipping(
    const rendering::CharacterScene& scene,
    const ClippingAnalysisOptions& options = {});

[[nodiscard]] nlohmann::json to_json(const ClippingAnalysisReport& report);

}  // namespace sns::visual_qa
