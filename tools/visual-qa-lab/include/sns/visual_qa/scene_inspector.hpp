// Comprehensive scene geometry inspector and SNS Visual Regression Suite (VQA-01..VQA-10) coordinator.
// Detects missing/hidden/out-of-position meshes, weight interpolation defects, vertex spikes,
// covered-body visibility leaks, and cross-family (CBBE 3BA vs UBE) incompatibilities.
#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "sns/body/body_analysis.hpp"
#include "sns/rendering/scene.hpp"
#include "sns/visual_qa/clipping_analyzer.hpp"
#include "sns/visual_qa/image_comparator.hpp"
#include "sns/visual_qa/morph_analyzer.hpp"
#include "sns/visual_qa/seam_analyzer.hpp"
#include "sns/visual_qa/visual_qa_types.hpp"

namespace sns::visual_qa {

struct PartIntegrityDiagnostic {
  std::string part_id;
  std::string label;
  std::string group;
  std::string shape_name;
  std::string mesh_path;
  std::string provider;
  std::string armor_form_key;
  std::string addon_form_key;
  SurfaceRole role = SurfaceRole::VisibleGarment;
  bool shown_by_default = true;
  std::string hidden_reason;
  std::size_t vertex_count = 0;
  std::size_t triangle_count = 0;
  Aabb3D bounds{};
  bool weight_slider_expected = false;
  bool weight_blended = false;
  bool has_nan_or_inf = false;
  std::size_t spiky_triangle_count = 0;
  float max_edge_length = 0.0f;
  bool out_of_position = false;
  std::string out_of_position_reason;
  bool body_leak_under_outfit = false;
  std::string body_leak_reason;
  std::string detected_family;  // "CBBE 3BA", "CBBE", "UBE", "Other", "Unknown"
};

struct SceneIntegrityReport {
  QaVerdict verdict = QaVerdict::Pass;
  std::string npc_form_key;
  float npc_weight = 50.0f;
  float character_scale = 1.0f;
  std::string detected_body_family;
  bool head_loaded = false;
  bool body_or_torso_loaded = false;
  bool hands_loaded = false;
  bool feet_loaded = false;
  std::size_t missing_mesh_count = 0;
  std::size_t unreadable_mesh_count = 0;
  std::size_t unexpected_hidden_pieces = 0;
  std::size_t out_of_position_parts = 0;
  std::size_t weight_interpolation_defects = 0;
  std::size_t abnormal_deformation_parts = 0;
  std::size_t body_visibility_leaks = 0;
  std::size_t family_mismatches = 0;
  std::vector<PartIntegrityDiagnostic> part_diagnostics;
  std::vector<VisualQaIssue> issues;
};

// Inspects a built CharacterScene (and optional Appearance metadata) for integrity, weight, family, and visibility issues.
[[nodiscard]] SceneIntegrityReport analyze_scene_integrity(
    const rendering::CharacterScene& scene,
    const std::optional<rendering::Appearance>& appearance = std::nullopt);

// Complete Visual QA evaluation for one NPC state (plus optional morph and capture comparisons).
struct NpcVisualQaReport {
  std::string run_id;
  std::string timestamp_utc;
  std::string npc_form_key;
  std::string npc_name;
  std::string profile_name;
  std::string mo2_ini_path;
  QaVerdict overall_verdict = QaVerdict::Pass;
  SceneIntegrityReport integrity;
  ClippingAnalysisReport clipping;
  SeamAnalysisReport seams;
  std::optional<MorphComparisonReport> morph_comparison;
  std::vector<CaptureArtifact> captures;
  std::vector<VisualComparisonResult> visual_comparisons;
  std::vector<VisualQaIssue> all_issues;
  double analysis_duration_ms = 0.0;
  std::size_t peak_working_set_bytes = 0;
};

// Runs integrity, clipping, and seam analyzers on a CharacterScene and aggregates issues.
[[nodiscard]] NpcVisualQaReport evaluate_character_scene(
    const rendering::CharacterScene& scene,
    const std::optional<rendering::Appearance>& appearance = std::nullopt,
    std::string_view npc_name = "",
    std::string_view profile_name = "");

// One scenario (VQA-01 .. VQA-10) in the SNS Visual Regression Suite.
struct RegressionScenarioResult {
  std::string id;          // "VQA-01" .. "VQA-10"
  std::string title;       // Human-readable scenario name
  QaVerdict verdict = QaVerdict::Skip;
  double duration_ms = 0.0;
  std::size_t peak_memory_bytes = 0;
  std::string summary;
  std::vector<std::string> evidence_notes;
  std::vector<VisualQaIssue> issues;
  nlohmann::json metrics = nlohmann::json::object();
};

struct VisualRegressionSuiteReport {
  std::string suite_name = "SNS Visual Regression Suite";
  std::string run_id;
  std::string timestamp_utc;
  QaVerdict overall_verdict = QaVerdict::Pass;
  std::size_t passed_count = 0;
  std::size_t failed_count = 0;
  std::size_t skipped_count = 0;
  std::size_t inconclusive_count = 0;
  double total_duration_ms = 0.0;
  std::vector<RegressionScenarioResult> scenarios;
};

[[nodiscard]] nlohmann::json to_json(const SceneIntegrityReport& report);
[[nodiscard]] nlohmann::json to_json(const NpcVisualQaReport& report);
[[nodiscard]] nlohmann::json to_json(const RegressionScenarioResult& scenario);
[[nodiscard]] nlohmann::json to_json(const VisualRegressionSuiteReport& suite);

}  // namespace sns::visual_qa
