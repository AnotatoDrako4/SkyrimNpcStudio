// Anatomical seam analyzer for Skyrim NPC Studio Visual QA Lab.
// Inspects open mesh boundaries at Neck-Head, Left/Right Wrist-Hand, and Left/Right Ankle-Foot junctions.
// Distinguishes welded alignment, abnormal gaps, intersections, lateral misalignments, scale mismatches,
// and seams concealed under equipped garments, using tolerances scaled to the character's geometry.
#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "sns/rendering/scene.hpp"
#include "sns/visual_qa/visual_qa_types.hpp"

namespace sns::visual_qa {

enum class SeamKind {
  NeckHead,
  WristHandLeft,
  WristHandRight,
  AnkleFootLeft,
  AnkleFootRight
};

[[nodiscard]] std::string_view to_string(SeamKind kind);

enum class SeamStatus {
  Aligned,               // Boundary rings match within calibrated welded tolerance.
  MinorDeviation,        // Sub-millimeter deviation within acceptable rendering tolerance.
  AbnormalGap,           // Visible separation between corresponding boundary loops.
  Intersection,          // Boundary loops cross axially / interpenetrate.
  Misalignment,          // Boundary loop centroids are laterally shifted.
  ScaleMismatch,         // Boundary loop radii/circumferences differ (e.g. CBBE body vs UBE head/hands).
  CoveredByGarment,      // Junction is enclosed by an equipped visible garment; not declared as a visible seam defect.
  NoComparableBoundary   // One or both meshes are missing or have no open boundary loop in the junction region.
};

[[nodiscard]] std::string_view to_string(SeamStatus status);

struct SeamJunctionReport {
  SeamKind kind = SeamKind::NeckHead;
  SeamStatus status = SeamStatus::NoComparableBoundary;
  QaVerdict verdict = QaVerdict::Skip;
  std::string part_a_id;
  std::string part_a_shape;
  std::string part_a_mesh;
  std::string part_b_id;
  std::string part_b_shape;
  std::string part_b_mesh;
  std::size_t boundary_vertices_a = 0;
  std::size_t boundary_vertices_b = 0;
  float min_distance = 0.0f;
  float max_distance = 0.0f;
  float mean_distance = 0.0f;
  float rms_distance = 0.0f;
  float p95_distance = 0.0f;
  float centroid_offset_axial = 0.0f;    // Positive = gap separation along limb/neck axis; negative = axial overlap
  float centroid_offset_lateral = 0.0f;  // Shear/lateral offset perpendicular to the limb/neck axis
  float radius_a = 0.0f;
  float radius_b = 0.0f;
  float scale_ratio = 1.0f;              // radius_a / radius_b
  float radial_mismatch_pct = 0.0f;      // |radius_a - radius_b| / max(radius_a, radius_b) * 100
  float calibrated_aligned_limit = 0.05f;
  float calibrated_gap_limit = 0.22f;
  bool covered_by_garment = false;
  std::string covering_garment_label;
  std::string body_family_hint;
  std::string explanation;
};

struct SeamAnalysisOptions {
  // Base tolerances in Skyrim units for scale = 1.0 (1 unit ≈ 1.428 cm).
  // Scaled proportionally by scene.scale and adjusted when comparing across skinning/FaceGen boundaries.
  float base_aligned_rms_units = 0.06f;     // ~0.85 mm
  float base_gap_rms_units = 0.22f;         // ~3.1 mm
  float base_gap_max_units = 0.35f;         // ~5.0 mm
  float base_misalignment_units = 0.25f;    // ~3.5 mm lateral shift
  float max_radial_mismatch_pct = 4.5f;     // >4.5% circumference difference = scale mismatch
};

struct SeamAnalysisReport {
  QaVerdict verdict = QaVerdict::Pass;
  float character_scale = 1.0f;
  std::string detected_body_family;
  std::size_t junctions_evaluated = 0;
  std::size_t junctions_aligned = 0;
  std::size_t junctions_defective = 0;
  std::size_t junctions_covered_by_garment = 0;
  std::size_t junctions_missing_boundary = 0;
  std::vector<SeamJunctionReport> junctions;
  std::vector<VisualQaIssue> issues;
};

// Analyzes Neck-Head, Wrist-Hand, and Ankle-Foot seams in a CharacterScene.
[[nodiscard]] SeamAnalysisReport analyze_scene_seams(
    const rendering::CharacterScene& scene,
    const SeamAnalysisOptions& options = {});

[[nodiscard]] nlohmann::json to_json(const SeamAnalysisReport& report);

}  // namespace sns::visual_qa
