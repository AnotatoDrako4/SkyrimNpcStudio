// Geometric morph verification and BodySlide PIRT (.tri) / OSD (.osd) reference validator for Visual QA Lab.
// Compares character/garment geometry before (State A) and after (State B) applying a BodySlide preset,
// distinguishing exact BodySlide morphs from heuristic approximations, legitimate zero-delta presets, and silent no-ops.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/rendering/scene.hpp"
#include "sns/visual_qa/clipping_analyzer.hpp"
#include "sns/visual_qa/visual_qa_types.hpp"

namespace sns::visual_qa {

// Sparse per-vertex offset entry from a BodySlide PIRT (.tri) or OutfitStudio (.osd) morph database.
struct SparseVertexDelta {
  std::uint16_t vertex_index = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;
};

// One named slider morph for a specific shape inside a PIRT (.tri) or OSD file.
struct ShapeSliderMorph {
  std::string slider_name;
  float multiplier = 1.0f;
  std::vector<SparseVertexDelta> deltas;
};

// Parsed BodySlide PIRT (.tri) or OSD morph database (shape_name -> list of slider morphs).
struct BodyMorphDatabase {
  std::string format;  // "PIRT" or "OSD"
  std::string source_label;
  std::map<std::string, std::vector<ShapeSliderMorph>> shape_morphs;

  [[nodiscard]] const std::vector<ShapeSliderMorph>* morphs_for_shape(std::string_view shape_name) const;
  [[nodiscard]] const ShapeSliderMorph* find_morph(std::string_view shape_name, std::string_view slider_name) const;
};

// Parses a BodySlide "PIRT" (.tri) binary file into a BodyMorphDatabase.
[[nodiscard]] BodyMorphDatabase parse_pirt_tri(std::string_view bytes, std::string_view label = "body.tri");

// Parses an OutfitStudio "OSD\0" (.osd) binary file into a BodyMorphDatabase.
[[nodiscard]] BodyMorphDatabase parse_osd_morphs(std::string_view bytes, std::string_view label = "morphs.osd");

enum class MorphQualityClassification {
  ExactBodySlideMorph,        // Displacements match the BodySlide TRI/OSD reference within tolerance.
  ApproximateGeometricMorph,  // Vertices moved, but via heuristic approximation rather than exact TRI/OSD deltas.
  LegitimateZeroChange,       // Preset has zero effective slider values (or none apply), and State A == State B.
  FalseNoChange,              // Non-zero applicable deltas exist, yet 0 vertices changed between State A and State B.
  OutfitMorphNotApplied,      // Garment has compatible morphs, yet the engine did not deform the garment.
  AbnormalDeformation,        // NaN/Inf positions, broken index topology, or exploded vertex displacements.
  VerifiedUnreferencedChange  // Vertices changed smoothly in expected regions, no external TRI/OSD file provided.
};

[[nodiscard]] std::string_view to_string(MorphQualityClassification cls);

struct SliderLookupEntry {
  std::string name;
  float small_val = 0.0f;
  float big_val = 0.0f;
  float interpolated_fraction = 0.0f;  // Effective slider weight at NPC weight (e.g. 0.0 .. 1.0)
  bool found_in_morph_db = false;
  std::size_t affected_vertices_in_db = 0;
  float max_delta_in_db = 0.0f;
};

struct RegionMorphStats {
  AnatomicalZone zone = AnatomicalZone::ChestBreasts;
  std::size_t modified_vertices = 0;
  float max_displacement = 0.0f;
  float mean_displacement = 0.0f;
  float rms_displacement = 0.0f;
};

struct ShapeMorphComparison {
  std::string part_id;
  std::string shape_name;
  std::string mesh_path;
  std::string group;  // "skin", "outfit", "head"
  std::string piece_label;
  MorphQualityClassification classification = MorphQualityClassification::LegitimateZeroChange;
  QaVerdict verdict = QaVerdict::Pass;

  // Vertex & topology consistency
  std::size_t vertex_count_before = 0;
  std::size_t vertex_count_after = 0;
  bool vertex_count_consistent = true;
  bool index_order_consistent = true;
  bool has_nan_or_inf = false;

  // Displacement metrics (Skyrim / view-space units)
  std::size_t modified_vertex_count = 0;
  float max_displacement = 0.0f;
  float mean_displacement_all = 0.0f;
  float mean_displacement_modified = 0.0f;
  float rms_displacement = 0.0f;

  // Bounds comparison
  Aabb3D bounds_before{};
  Aabb3D bounds_after{};
  std::array<float, 3> bounds_min_delta{0.0f, 0.0f, 0.0f};
  std::array<float, 3> bounds_max_delta{0.0f, 0.0f, 0.0f};
  bool unexpected_bounds_change = false;
  std::string bounds_note;

  // Normal vector comparison
  std::size_t modified_normal_count = 0;
  float max_normal_angle_deg = 0.0f;
  float mean_normal_angle_deg = 0.0f;
  std::size_t degenerate_normal_count = 0;

  // Anatomical regions affected
  std::vector<RegionMorphStats> affected_regions;

  // Slider traceability & TRI/OSD reference comparison
  bool morph_db_available = false;
  std::string morph_db_source;
  std::size_t sliders_requested_count = 0;
  std::size_t sliders_nonzero_requested_count = 0;
  std::size_t sliders_found_in_db_count = 0;
  std::size_t sliders_nonzero_applicable_count = 0;
  std::size_t reference_expected_modified_vertices = 0;
  float rms_error_vs_reference = 0.0f;
  float max_error_vs_reference = 0.0f;
  float cosine_similarity_vs_reference = 0.0f;
  std::vector<SliderLookupEntry> slider_traces;
  std::string explanation;
};

struct MorphComparisonReport {
  QaVerdict verdict = QaVerdict::Pass;
  std::string npc_form_key;
  std::string preset_name;
  std::string preset_family;
  float npc_weight = 50.0f;
  std::size_t shapes_compared = 0;
  std::size_t shapes_modified = 0;
  std::size_t exact_bodyslide_Matches = 0;
  std::size_t approximate_morph_shapes = 0;
  std::size_t false_no_change_shapes = 0;
  std::size_t outfit_morph_missed_shapes = 0;
  std::vector<ShapeMorphComparison> shape_comparisons;
  std::vector<VisualQaIssue> issues;
};

using MorphDbProvider = std::function<std::optional<BodyMorphDatabase>(std::string_view mesh_path)>;

// Compares a single RenderPart before and after applying sliders, optionally verifying against a BodyMorphDatabase.
[[nodiscard]] ShapeMorphComparison compare_part_morph(
    const rendering::RenderPart& before,
    const rendering::RenderPart& after,
    const std::vector<rendering::BodySliderSetting>& sliders,
    float npc_weight,
    const BodyMorphDatabase* morph_db = nullptr,
    bool heuristic_sliders_applicable = false);

// Compares two CharacterScenes (State A without preset, State B with preset applied).
[[nodiscard]] MorphComparisonReport compare_scene_morphs(
    const rendering::CharacterScene& state_a,
    const rendering::CharacterScene& state_b,
    const rendering::BodyPresetOption& preset,
    float npc_weight,
    const MorphDbProvider& db_provider = {});

// Applies exact PIRT (.tri) / OSD morph deltas directly to a nif::Shape (used to build ground-truth BodySlide reference fixtures in tests).
std::size_t apply_exact_bodyslide_reference_morphs(
    nif::Shape& shape,
    const BodyMorphDatabase& db,
    const std::vector<rendering::BodySliderSetting>& sliders,
    float npc_weight);

[[nodiscard]] nlohmann::json to_json(const MorphComparisonReport& report);

}  // namespace sns::visual_qa
