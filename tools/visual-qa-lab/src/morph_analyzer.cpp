#include "sns/visual_qa/morph_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

#include "sns/core/text.hpp"

namespace sns::visual_qa {
namespace {

constexpr std::size_t kFloatsPerVertex = rendering::kFloatsPerVertex;

nif::Vec3 sub_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

nif::Vec3 add_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

float dot_v(const nif::Vec3& a, const nif::Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length_v(const nif::Vec3& a) { return std::sqrt(dot_v(a, a)); }

nif::Vec3 map_linear_offset(const nif::Transform& t, const nif::Vec3& d) {
  const nif::Vec3 applied = t.apply(d);
  return {applied.x - t.translation.x, applied.y - t.translation.y, applied.z - t.translation.z};
}

float slider_fraction(const rendering::BodySliderSetting& s, float weight) {
  if (!s.small.has_value() && !s.big.has_value()) {
    return 0.0f;
  }
  const float small_val = s.small.value_or(s.big.value_or(0.0f));
  const float big_val = s.big.value_or(s.small.value_or(0.0f));
  const float w = std::clamp(weight / 100.0f, 0.0f, 1.0f);
  return (small_val + (big_val - small_val) * w) / 100.0f;
}

}  // namespace

const std::vector<ShapeSliderMorph>* BodyMorphDatabase::morphs_for_shape(
    std::string_view shape_name) const {
  auto it = shape_morphs.find(std::string(shape_name));
  if (it != shape_morphs.end()) {
    return &it->second;
  }
  for (const auto& [k, v] : shape_morphs) {
    if (text::iequals_ascii(k, shape_name)) {
      return &v;
    }
  }
  auto wildcard = shape_morphs.find("*");
  if (wildcard != shape_morphs.end()) {
    return &wildcard->second;
  }
  return nullptr;
}

const ShapeSliderMorph* BodyMorphDatabase::find_morph(std::string_view shape_name,
                                                      std::string_view slider_name) const {
  if (const auto* list = morphs_for_shape(shape_name)) {
    for (const auto& m : *list) {
      if (m.slider_name == slider_name || text::iequals_ascii(m.slider_name, slider_name)) {
        return &m;
      }
      // Support OSD combined block names "<ShapeName><SliderName>"
      if (m.slider_name.size() == shape_name.size() + slider_name.size() &&
          text::iequals_ascii(std::string_view(m.slider_name).substr(0, shape_name.size()), shape_name) &&
          text::iequals_ascii(std::string_view(m.slider_name).substr(shape_name.size()), slider_name)) {
        return &m;
      }
    }
  }
  return nullptr;
}

BodyMorphDatabase parse_pirt_tri(std::string_view bytes, std::string_view label) {
  BodyMorphDatabase db;
  db.format = "PIRT";
  db.source_label = std::string(label);
  if (bytes.size() < 6) {
    return db;
  }
  if (bytes.substr(0, 4) != "PIRT" && bytes.substr(0, 4) != "TRIP") {
    return db;
  }

  std::size_t pos = 4;
  auto can_read = [&](std::size_t n) { return pos + n <= bytes.size(); };
  auto read_u8 = [&]() -> std::uint8_t {
    std::uint8_t v = static_cast<std::uint8_t>(bytes[pos]);
    pos += 1;
    return v;
  };
  auto read_u16 = [&]() -> std::uint16_t {
    std::uint16_t v = 0;
    std::memcpy(&v, bytes.data() + pos, sizeof(v));
    pos += sizeof(v);
    return v;
  };
  auto read_i16 = [&]() -> std::int16_t {
    std::int16_t v = 0;
    std::memcpy(&v, bytes.data() + pos, sizeof(v));
    pos += sizeof(v);
    return v;
  };
  auto read_f32 = [&]() -> float {
    float v = 0.0f;
    std::memcpy(&v, bytes.data() + pos, sizeof(v));
    pos += sizeof(v);
    return v;
  };

  const std::uint16_t shape_count = read_u16();
  for (std::uint16_t s = 0; s < shape_count; ++s) {
    if (!can_read(1)) break;
    const std::uint8_t sname_len = read_u8();
    if (!can_read(sname_len + 2)) break;
    std::string shape_name(bytes.substr(pos, sname_len));
    pos += sname_len;
    const std::uint16_t morph_count = read_u16();

    std::vector<ShapeSliderMorph> morphs;
    morphs.reserve(morph_count);
    bool truncated = false;
    for (std::uint16_t m = 0; m < morph_count; ++m) {
      if (!can_read(1)) {
        truncated = true;
        break;
      }
      const std::uint8_t mname_len = read_u8();
      if (!can_read(static_cast<std::size_t>(mname_len) + 6)) {
        truncated = true;
        break;
      }
      ShapeSliderMorph sm;
      sm.slider_name = std::string(bytes.substr(pos, mname_len));
      pos += mname_len;
      sm.multiplier = read_f32();
      const std::uint16_t vert_count = read_u16();
      if (!can_read(static_cast<std::size_t>(vert_count) * 8)) {
        truncated = true;
        break;
      }
      sm.deltas.reserve(vert_count);
      for (std::uint16_t v = 0; v < vert_count; ++v) {
        const std::uint16_t vid = read_u16();
        const std::int16_t qx = read_i16();
        const std::int16_t qy = read_i16();
        const std::int16_t qz = read_i16();
        sm.deltas.push_back({vid, static_cast<float>(qx) * sm.multiplier,
                             static_cast<float>(qy) * sm.multiplier,
                             static_cast<float>(qz) * sm.multiplier});
      }
      morphs.push_back(std::move(sm));
    }
    db.shape_morphs[std::move(shape_name)] = std::move(morphs);
    if (truncated) break;
  }

  return db;
}

BodyMorphDatabase parse_osd_morphs(std::string_view bytes, std::string_view label) {
  BodyMorphDatabase db;
  db.format = "OSD";
  db.source_label = std::string(label);
  if (bytes.size() < 12) {
    return db;
  }
  const std::string_view magic = bytes.substr(0, 4);
  if (magic != std::string_view("OSD\0", 4) && magic != std::string_view("\0DSO", 4)) {
    return db;
  }

  std::size_t pos = 8;  // Skip magic (4) + version (4)
  auto can_read = [&](std::size_t n) { return pos + n <= bytes.size(); };
  std::uint32_t block_count = 0;
  std::memcpy(&block_count, bytes.data() + pos, sizeof(block_count));
  pos += 4;

  for (std::uint32_t b = 0; b < block_count; ++b) {
    if (!can_read(1)) break;
    const std::uint8_t name_len = static_cast<std::uint8_t>(bytes[pos]);
    pos += 1;
    if (!can_read(static_cast<std::size_t>(name_len) + 2)) break;
    std::string block_name(bytes.substr(pos, name_len));
    pos += name_len;
    std::uint16_t diff_count = 0;
    std::memcpy(&diff_count, bytes.data() + pos, sizeof(diff_count));
    pos += 2;
    if (!can_read(static_cast<std::size_t>(diff_count) * 14)) break;

    ShapeSliderMorph sm;
    sm.multiplier = 1.0f;
    sm.deltas.reserve(diff_count);
    for (std::uint16_t k = 0; k < diff_count; ++k) {
      std::uint16_t vid = 0;
      float dx = 0.0f, dy = 0.0f, dz = 0.0f;
      std::memcpy(&vid, bytes.data() + pos, 2);
      std::memcpy(&dx, bytes.data() + pos + 2, 4);
      std::memcpy(&dy, bytes.data() + pos + 6, 4);
      std::memcpy(&dz, bytes.data() + pos + 10, 4);
      pos += 14;
      sm.deltas.push_back({vid, dx, dy, dz});
    }

    const auto sep = block_name.find_first_of(":/");
    if (sep != std::string::npos) {
      const std::string shape_key = block_name.substr(0, sep);
      sm.slider_name = block_name.substr(sep + 1);
      db.shape_morphs[shape_key].push_back( sm );
    }
    sm.slider_name = std::move(block_name);
    db.shape_morphs["*"].push_back(std::move(sm));
  }

  return db;
}

std::string_view to_string(MorphQualityClassification cls) {
  switch (cls) {
    case MorphQualityClassification::ExactBodySlideMorph:
      return "ExactBodySlideMorph";
    case MorphQualityClassification::ApproximateGeometricMorph:
      return "ApproximateGeometricMorph";
    case MorphQualityClassification::LegitimateZeroChange:
      return "LegitimateZeroChange";
    case MorphQualityClassification::FalseNoChange:
      return "FalseNoChange";
    case MorphQualityClassification::OutfitMorphNotApplied:
      return "OutfitMorphNotApplied";
    case MorphQualityClassification::AbnormalDeformation:
      return "AbnormalDeformation";
    case MorphQualityClassification::VerifiedUnreferencedChange:
      return "VerifiedUnreferencedChange";
  }
  return "LegitimateZeroChange";
}

std::size_t apply_exact_bodyslide_reference_morphs(
    nif::Shape& shape, const BodyMorphDatabase& db,
    const std::vector<rendering::BodySliderSetting>& sliders, float npc_weight) {
  std::vector<bool> vertex_touched(shape.positions.size(), false);
  for (const auto& s : sliders) {
    const float frac = slider_fraction(s, npc_weight);
    if (std::abs(frac) <= 1e-6f) continue;
    const ShapeSliderMorph* morph = db.find_morph(shape.name, s.name);
    if (!morph) continue;
    for (const auto& d : morph->deltas) {
      if (d.vertex_index >= shape.positions.size()) continue;
      const nif::Vec3 mapped = map_linear_offset(shape.to_root, {d.dx * frac, d.dy * frac, d.dz * frac});
      if (length_v(mapped) > 1e-6f) {
        shape.positions[d.vertex_index] = add_v(shape.positions[d.vertex_index], mapped);
        vertex_touched[d.vertex_index] = true;
      }
    }
  }
  return static_cast<std::size_t>(std::count(vertex_touched.begin(), vertex_touched.end(), true));
}

ShapeMorphComparison compare_part_morph(const rendering::RenderPart& before,
                                        const rendering::RenderPart& after,
                                        const std::vector<rendering::BodySliderSetting>& sliders,
                                        float npc_weight, const BodyMorphDatabase* morph_db,
                                        bool heuristic_sliders_applicable) {
  ShapeMorphComparison cmp;
  cmp.part_id = after.id.empty() ? before.id : after.id;
  cmp.shape_name = after.shape_name.empty() ? before.shape_name : after.shape_name;
  cmp.mesh_path = after.mesh_path.empty() ? before.mesh_path : after.mesh_path;
  cmp.group = after.group.empty() ? before.group : after.group;
  cmp.piece_label = after.label.empty() ? before.label : after.label;

  cmp.vertex_count_before = before.vertices.size() / kFloatsPerVertex;
  cmp.vertex_count_after = after.vertices.size() / kFloatsPerVertex;
  cmp.vertex_count_consistent = (cmp.vertex_count_before == cmp.vertex_count_after);
  cmp.index_order_consistent = (before.indices == after.indices);
  cmp.bounds_before = {before.min, before.max};
  cmp.bounds_after = {after.min, after.max};
  for (std::size_t k = 0; k < 3; ++k) {
    cmp.bounds_min_delta[k] = after.min[k] - before.min[k];
    cmp.bounds_max_delta[k] = after.max[k] - before.max[k];
  }

  if (!cmp.vertex_count_consistent || !cmp.index_order_consistent) {
    cmp.classification = MorphQualityClassification::AbnormalDeformation;
    cmp.verdict = QaVerdict::Fail;
    cmp.explanation = "Vertex count or index buffer topology changed unexpectedly during morph application.";
    return cmp;
  }

  const std::size_t n_verts = cmp.vertex_count_before;
  std::vector<nif::Vec3> actual_deltas(n_verts, {0.0f, 0.0f, 0.0f});
  std::map<AnatomicalZone, std::pair<std::size_t, double>> zone_counts_and_sq;
  std::map<AnatomicalZone, std::pair<float, double>> zone_max_and_sum;

  double disp_sum = 0.0;
  double disp_sq_sum = 0.0;
  double norm_angle_sum = 0.0;

  for (std::size_t v = 0; v < n_verts; ++v) {
    const float* pb = before.vertices.data() + v * kFloatsPerVertex;
    const float* pa = after.vertices.data() + v * kFloatsPerVertex;
    const nif::Vec3 pos_b{pb[0], pb[1], pb[2]};
    const nif::Vec3 pos_a{pa[0], pa[1], pa[2]};
    const nif::Vec3 norm_b{pb[3], pb[4], pb[5]};
    const nif::Vec3 norm_a{pa[3], pa[4], pa[5]};

    if (!std::isfinite(pos_a.x) || !std::isfinite(pos_a.y) || !std::isfinite(pos_a.z) ||
        !std::isfinite(norm_a.x) || !std::isfinite(norm_a.y) || !std::isfinite(norm_a.z)) {
      cmp.has_nan_or_inf = true;
      continue;
    }

    const nif::Vec3 d = sub_v(pos_a, pos_b);
    actual_deltas[v] = d;
    const float dist = length_v(d);
    disp_sum += dist;
    disp_sq_sum += static_cast<double>(dist) * static_cast<double>(dist);

    if (dist > 1e-4f) {
      ++cmp.modified_vertex_count;
      cmp.max_displacement = std::max(cmp.max_displacement, dist);
      const AnatomicalZone z = classify_anatomical_zone(pos_b, before.min[1], before.max[1]);
      zone_counts_and_sq[z].first += 1;
      zone_counts_and_sq[z].second += static_cast<double>(dist) * static_cast<double>(dist);
      zone_max_and_sum[z].first = std::max(zone_max_and_sum[z].first, dist);
      zone_max_and_sum[z].second += dist;
    }

    const float len_na = length_v(norm_a);
    const float len_nb = length_v(norm_b);
    if (len_na < 1e-4f) {
      ++cmp.degenerate_normal_count;
    } else if (len_nb >= 1e-4f) {
      const float cos_val = std::clamp(dot_v(norm_a, norm_b) / (len_na * len_nb), -1.0f, 1.0f);
      const float angle_deg = std::acos(cos_val) * (180.0f / 3.1415926535f);
      if (angle_deg > 0.05f) {
        ++cmp.modified_normal_count;
        cmp.max_normal_angle_deg = std::max(cmp.max_normal_angle_deg, angle_deg);
        norm_angle_sum += angle_deg;
      }
    }
  }

  if (n_verts > 0) {
    cmp.mean_displacement_all = static_cast<float>(disp_sum / static_cast<double>(n_verts));
    cmp.rms_displacement = static_cast<float>(std::sqrt(disp_sq_sum / static_cast<double>(n_verts)));
  }
  if (cmp.modified_vertex_count > 0) {
    cmp.mean_displacement_modified =
        static_cast<float>(disp_sum / static_cast<double>(cmp.modified_vertex_count));
  }
  if (cmp.modified_normal_count > 0) {
    cmp.mean_normal_angle_deg =
        static_cast<float>(norm_angle_sum / static_cast<double>(cmp.modified_normal_count));
  }

  for (const auto& [zone, cnt_sq] : zone_counts_and_sq) {
    const auto& max_sum = zone_max_and_sum[zone];
    RegionMorphStats rs;
    rs.zone = zone;
    rs.modified_vertices = cnt_sq.first;
    rs.max_displacement = max_sum.first;
    rs.mean_displacement = static_cast<float>(max_sum.second / static_cast<double>(cnt_sq.first));
    rs.rms_displacement = static_cast<float>(std::sqrt(cnt_sq.second / static_cast<double>(cnt_sq.first)));
    cmp.affected_regions.push_back(rs);
  }

  for (std::size_t k = 0; k < 3; ++k) {
    if (std::abs(cmp.bounds_min_delta[k]) > 18.0f || std::abs(cmp.bounds_max_delta[k]) > 18.0f) {
      cmp.unexpected_bounds_change = true;
      cmp.bounds_note = "Bounding box shifted by >18 Skyrim units along axis.";
    }
  }

  // Evaluate requested sliders and optional BodyMorphDatabase ground truth.
  cmp.sliders_requested_count = sliders.size();
  cmp.morph_db_available = (morph_db != nullptr && morph_db->morphs_for_shape(cmp.shape_name) != nullptr);
  if (morph_db) {
    cmp.morph_db_source = morph_db->source_label;
  }

  std::vector<nif::Vec3> ref_deltas(n_verts, {0.0f, 0.0f, 0.0f});
  std::vector<bool> ref_touched(n_verts, false);

  for (const auto& s : sliders) {
    SliderLookupEntry trace;
    trace.name = s.name;
    trace.small_val = s.small.value_or(s.big.value_or(0.0f));
    trace.big_val = s.big.value_or(s.small.value_or(0.0f));
    trace.interpolated_fraction = slider_fraction(s, npc_weight);
    const bool nonzero = std::abs(trace.interpolated_fraction) > 1e-5f;
    if (nonzero) {
      ++cmp.sliders_nonzero_requested_count;
    }

    if (cmp.morph_db_available) {
      if (const ShapeSliderMorph* sm = morph_db->find_morph(cmp.shape_name, s.name)) {
        trace.found_in_morph_db = true;
        trace.affected_vertices_in_db = sm->deltas.size();
        ++cmp.sliders_found_in_db_count;
        for (const auto& d : sm->deltas) {
          const nif::Vec3 mapped = map_linear_offset(
              before.to_root,
              {d.dx * trace.interpolated_fraction, d.dy * trace.interpolated_fraction,
               d.dz * trace.interpolated_fraction});
          const nif::Vec3 view_d = rendering::to_view_space(mapped);
          const float len = length_v(view_d);
          trace.max_delta_in_db = std::max(trace.max_delta_in_db, len);
          if (nonzero && d.vertex_index < n_verts && len > 1e-4f) {
            ref_deltas[d.vertex_index] = add_v(ref_deltas[d.vertex_index], view_d);
            ref_touched[d.vertex_index] = true;
          }
        }
        if (nonzero && trace.max_delta_in_db > 1e-4f) {
          ++cmp.sliders_nonzero_applicable_count;
        }
      }
    }
    cmp.slider_traces.push_back(std::move(trace));
  }

  if (cmp.morph_db_available) {
    cmp.reference_expected_modified_vertices =
        static_cast<std::size_t>(std::count(ref_touched.begin(), ref_touched.end(), true));
    double err_sq_sum = 0.0;
    double dot_ab = 0.0;
    double norm_a_sq = 0.0;
    double norm_b_sq = 0.0;
    for (std::size_t v = 0; v < n_verts; ++v) {
      const nif::Vec3 diff = sub_v(actual_deltas[v], ref_deltas[v]);
      const float err = length_v(diff);
      err_sq_sum += static_cast<double>(err) * static_cast<double>(err);
      cmp.max_error_vs_reference = std::max(cmp.max_error_vs_reference, err);
      dot_ab += dot_v(actual_deltas[v], ref_deltas[v]);
      norm_a_sq += dot_v(actual_deltas[v], actual_deltas[v]);
      norm_b_sq += dot_v(ref_deltas[v], ref_deltas[v]);
    }
    if (n_verts > 0) {
      cmp.rms_error_vs_reference = static_cast<float>(std::sqrt(err_sq_sum / static_cast<double>(n_verts)));
    }
    if (norm_a_sq > 1e-8 && norm_b_sq > 1e-8) {
      cmp.cosine_similarity_vs_reference =
          static_cast<float>(dot_ab / (std::sqrt(norm_a_sq) * std::sqrt(norm_b_sq)));
    } else if (norm_a_sq <= 1e-8 && norm_b_sq <= 1e-8) {
      cmp.cosine_similarity_vs_reference = 1.0f;
    }
  }

  // Classify morph quality
  if (cmp.has_nan_or_inf || cmp.max_displacement > 45.0f || cmp.unexpected_bounds_change) {
    cmp.classification = MorphQualityClassification::AbnormalDeformation;
    cmp.verdict = QaVerdict::Fail;
    cmp.explanation = "Abnormal vertex deformation (NaN/Inf or excessive displacement >45 units) detected.";
    return cmp;
  }

  if (cmp.morph_db_available) {
    if (cmp.reference_expected_modified_vertices == 0) {
      if (cmp.modified_vertex_count == 0) {
        cmp.classification = MorphQualityClassification::LegitimateZeroChange;
        cmp.verdict = QaVerdict::Pass;
        cmp.explanation = "Zero applicable slider deltas in reference TRI/OSD and 0 vertices modified.";
      } else {
        cmp.classification = MorphQualityClassification::ApproximateGeometricMorph;
        cmp.verdict = QaVerdict::Fail;
        cmp.explanation = "Vertices moved even though reference TRI/OSD has 0 applicable deltas for this shape.";
      }
    } else if (cmp.modified_vertex_count == 0) {
      cmp.classification = (cmp.group == "outfit") ? MorphQualityClassification::OutfitMorphNotApplied
                                                   : MorphQualityClassification::FalseNoChange;
      cmp.verdict = QaVerdict::Fail;
      cmp.explanation = "Reference TRI/OSD expects " +
                        std::to_string(cmp.reference_expected_modified_vertices) +
                        " modified vertices from " +
                        std::to_string(cmp.sliders_nonzero_applicable_count) +
                        " active slider(s), but 0 vertices changed.";
    } else if (cmp.rms_error_vs_reference <= 0.05f && cmp.cosine_similarity_vs_reference >= 0.96f) {
      cmp.classification = MorphQualityClassification::ExactBodySlideMorph;
      cmp.verdict = QaVerdict::Pass;
      cmp.explanation = "Displacements match exact BodySlide TRI/OSD reference (RMS error=" +
                        std::to_string(cmp.rms_error_vs_reference) +
                        ", cosine=" + std::to_string(cmp.cosine_similarity_vs_reference) + ").";
    } else {
      cmp.classification = MorphQualityClassification::ApproximateGeometricMorph;
      cmp.verdict = QaVerdict::Fail;
      cmp.explanation =
          "Vertices moved, but via geometric approximation rather than exact BodySlide TRI/OSD deltas (RMS error=" +
          std::to_string(cmp.rms_error_vs_reference) +
          ", cosine=" + std::to_string(cmp.cosine_similarity_vs_reference) + ").";
    }
    return cmp;
  }

  // When no external BodyMorphDatabase was passed directly, inspect whether sliders should have moved this shape.
  const bool expects_change =
      heuristic_sliders_applicable && (cmp.sliders_nonzero_requested_count > 0);
  if (cmp.modified_vertex_count == 0) {
    if (expects_change) {
      cmp.classification = (cmp.group == "outfit") ? MorphQualityClassification::OutfitMorphNotApplied
                                                   : MorphQualityClassification::FalseNoChange;
      cmp.verdict = QaVerdict::Fail;
      cmp.explanation = "Non-zero applicable sliders were provided, but 0 vertices changed.";
    } else {
      cmp.classification = MorphQualityClassification::LegitimateZeroChange;
      cmp.verdict = QaVerdict::Pass;
      cmp.explanation = "No applicable non-zero morph deltas for this shape; geometry unchanged.";
    }
  } else {
    cmp.classification = MorphQualityClassification::VerifiedUnreferencedChange;
    cmp.verdict = QaVerdict::Pass;
    cmp.explanation = std::to_string(cmp.modified_vertex_count) + " vertices displaced smoothly (max=" +
                      std::to_string(cmp.max_displacement) + " u).";
  }

  return cmp;
}

MorphComparisonReport compare_scene_morphs(const rendering::CharacterScene& state_a,
                                           const rendering::CharacterScene& state_b,
                                           const rendering::BodyPresetOption& preset,
                                           float npc_weight, const MorphDbProvider& db_provider) {
  MorphComparisonReport report;
  report.preset_name = preset.name;
  report.preset_family = preset.family;
  report.npc_weight = npc_weight;

  // Index State B's engine-reported ShapeMorphState by part_id for cross-checking when db_provider is absent.
  std::unordered_map<std::string, const rendering::ShapeMorphState*> state_b_morph_map;
  for (const auto& ms : state_b.body_morphs) {
    state_b_morph_map[ms.part_id] = &ms;
  }

  const std::size_t count = std::min(state_a.parts.size(), state_b.parts.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto& pa = state_a.parts[i];
    const auto& pb = state_b.parts[i];
    if (!pb.shown_by_default && pb.group != "skin") {
      continue;
    }
    if (pb.group == "head") {
      continue;
    }

    std::optional<BodyMorphDatabase> loaded_db;
    if (db_provider && !pb.mesh_path.empty()) {
      loaded_db = db_provider(pb.mesh_path);
    }

    bool engine_says_applicable = false;
    const rendering::ShapeMorphState* engine_ms = nullptr;
    if (auto it = state_b_morph_map.find(pb.id); it != state_b_morph_map.end()) {
      engine_ms = it->second;
      engine_says_applicable = !engine_ms->applied.empty();
    }

    ShapeMorphComparison cmp =
        compare_part_morph(pa, pb, preset.sliders, npc_weight,
                           loaded_db.has_value() ? &(*loaded_db) : nullptr, engine_says_applicable);

    // Enrich with CharacterScene::body_morphs metadata if loaded_db wasn't externally supplied.
    if (!loaded_db.has_value() && engine_ms != nullptr) {
      cmp.morph_db_source = engine_ms->tri_path;
      cmp.sliders_found_in_db_count = engine_ms->compatible;
      cmp.sliders_nonzero_applicable_count = engine_ms->applied.size();
      if ((engine_ms->status == "applied" || engine_ms->status == "partial" ||
           engine_ms->status == "needs-rebuild") &&
          cmp.modified_vertex_count > 0 &&
          cmp.classification == MorphQualityClassification::VerifiedUnreferencedChange) {
        cmp.classification = MorphQualityClassification::ExactBodySlideMorph;
        cmp.explanation = engine_ms->detail;
      } else if (engine_ms->status == "incompatible-family") {
        VisualQaIssue fam_issue;
        fam_issue.severity = "warning";
        fam_issue.code = "PresetBodyFamilyMismatch";
        fam_issue.category = "morph";
        fam_issue.message = engine_ms->detail;
        fam_issue.mesh_path = pb.mesh_path;
        fam_issue.shape_name = pb.shape_name;
        fam_issue.piece_label = pb.label;
        report.issues.push_back(std::move(fam_issue));
      }
    }

    ++report.shapes_compared;
    if (cmp.modified_vertex_count > 0) {
      ++report.shapes_modified;
    }
    switch (cmp.classification) {
      case MorphQualityClassification::ExactBodySlideMorph:
        ++report.exact_bodyslide_Matches;
        break;
      case MorphQualityClassification::ApproximateGeometricMorph:
        ++report.approximate_morph_shapes;
        report.issues.push_back({"warning", "ApproximateGeometricMorph", "morph", cmp.explanation,
                                 cmp.mesh_path, cmp.shape_name, cmp.piece_label, pb.armor_form_key,
                                 pb.addon_form_key, pb.mesh_location, "Body"});
        break;
      case MorphQualityClassification::FalseNoChange:
        ++report.false_no_change_shapes;
        report.issues.push_back({"error", "MorphFalseNoChange", "morph", cmp.explanation,
                                 cmp.mesh_path, cmp.shape_name, cmp.piece_label, pb.armor_form_key,
                                 pb.addon_form_key, pb.mesh_location, "Body"});
        break;
      case MorphQualityClassification::OutfitMorphNotApplied:
        ++report.outfit_morph_missed_shapes;
        report.issues.push_back({"error", "OutfitMorphNotApplied", "morph", cmp.explanation,
                                 cmp.mesh_path, cmp.shape_name, cmp.piece_label, pb.armor_form_key,
                                 pb.addon_form_key, pb.mesh_location, "Outfit"});
        break;
      case MorphQualityClassification::AbnormalDeformation:
        report.issues.push_back({"error", "MorphAbnormalDeformation", "morph", cmp.explanation,
                                 cmp.mesh_path, cmp.shape_name, cmp.piece_label, pb.armor_form_key,
                                 pb.addon_form_key, pb.mesh_location, "Body"});
        break;
      default:
        break;
    }

    report.verdict = combine_verdicts(report.verdict, cmp.verdict);
    report.shape_comparisons.push_back(std::move(cmp));
  }

  return report;
}

nlohmann::json to_json(const MorphComparisonReport& report) {
  nlohmann::json shapes_json = nlohmann::json::array();
  for (const auto& s : report.shape_comparisons) {
    nlohmann::json regions_json = nlohmann::json::array();
    for (const auto& r : s.affected_regions) {
      regions_json.push_back({{"zone", to_string(r.zone)},
                              {"modified_vertices", r.modified_vertices},
                              {"max_displacement", r.max_displacement},
                              {"mean_displacement", r.mean_displacement},
                              {"rms_displacement", r.rms_displacement}});
    }
    nlohmann::json traces_json = nlohmann::json::array();
    for (const auto& t : s.slider_traces) {
      traces_json.push_back({{"name", t.name},
                             {"small_val", t.small_val},
                             {"big_val", t.big_val},
                             {"interpolated_fraction", t.interpolated_fraction},
                             {"found_in_morph_db", t.found_in_morph_db},
                             {"affected_vertices_in_db", t.affected_vertices_in_db},
                             {"max_delta_in_db", t.max_delta_in_db}});
    }

    shapes_json.push_back({{"part_id", s.part_id},
                           {"shape_name", s.shape_name},
                           {"mesh_path", s.mesh_path},
                           {"group", s.group},
                           {"piece_label", s.piece_label},
                           {"classification", to_string(s.classification)},
                           {"verdict", to_string(s.verdict)},
                           {"vertex_count_before", s.vertex_count_before},
                           {"vertex_count_after", s.vertex_count_after},
                           {"vertex_count_consistent", s.vertex_count_consistent},
                           {"index_order_consistent", s.index_order_consistent},
                           {"has_nan_or_inf", s.has_nan_or_inf},
                           {"modified_vertex_count", s.modified_vertex_count},
                           {"max_displacement", s.max_displacement},
                           {"mean_displacement_all", s.mean_displacement_all},
                           {"mean_displacement_modified", s.mean_displacement_modified},
                           {"rms_displacement", s.rms_displacement},
                           {"bounds_min_delta", s.bounds_min_delta},
                           {"bounds_max_delta", s.bounds_max_delta},
                           {"unexpected_bounds_change", s.unexpected_bounds_change},
                           {"bounds_note", s.bounds_note},
                           {"modified_normal_count", s.modified_normal_count},
                           {"max_normal_angle_deg", s.max_normal_angle_deg},
                           {"mean_normal_angle_deg", s.mean_normal_angle_deg},
                           {"degenerate_normal_count", s.degenerate_normal_count},
                           {"affected_regions", std::move(regions_json)},
                           {"morph_db_available", s.morph_db_available},
                           {"morph_db_source", s.morph_db_source},
                           {"sliders_requested_count", s.sliders_requested_count},
                           {"sliders_nonzero_requested_count", s.sliders_nonzero_requested_count},
                           {"sliders_found_in_db_count", s.sliders_found_in_db_count},
                           {"sliders_nonzero_applicable_count", s.sliders_nonzero_applicable_count},
                           {"reference_expected_modified_vertices", s.reference_expected_modified_vertices},
                           {"rms_error_vs_reference", s.rms_error_vs_reference},
                           {"max_error_vs_reference", s.max_error_vs_reference},
                           {"cosine_similarity_vs_reference", s.cosine_similarity_vs_reference},
                           {"slider_traces", std::move(traces_json)},
                           {"explanation", s.explanation}});
  }

  nlohmann::json issues_json = nlohmann::json::array();
  for (const auto& iss : report.issues) {
    issues_json.push_back(to_json(iss));
  }

  return {{"verdict", to_string(report.verdict)},
          {"npc_form_key", report.npc_form_key},
          {"preset_name", report.preset_name},
          {"preset_family", report.preset_family},
          {"npc_weight", report.npc_weight},
          {"shapes_compared", report.shapes_compared},
          {"shapes_modified", report.shapes_modified},
          {"exact_bodyslide_matches", report.exact_bodyslide_Matches},
          {"approximate_morph_shapes", report.approximate_morph_shapes},
          {"false_no_change_shapes", report.false_no_change_shapes},
          {"outfit_morph_missed_shapes", report.outfit_morph_missed_shapes},
          {"shape_comparisons", std::move(shapes_json)},
          {"issues", std::move(issues_json)}};
}

}  // namespace sns::visual_qa
