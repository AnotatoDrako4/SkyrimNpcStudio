#include "sns/visual_qa/scene_inspector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include "sns/core/text.hpp"

namespace sns::visual_qa {
namespace {

constexpr std::size_t kFloatsPerVertex = rendering::kFloatsPerVertex;

std::size_t current_peak_working_set_bytes() {
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return static_cast<std::size_t>(pmc.PeakWorkingSetSize);
  }
  return 0;
}

std::string classify_family_string(std::string_view mesh_path, std::string_view shape_name) {
  const std::string m = text::to_lower_ascii(mesh_path);
  const std::string s = text::to_lower_ascii(shape_name);
  if (m.find("ube") != std::string::npos || s.find("ube") != std::string::npos) {
    return "UBE";
  }
  if (m.find("3ba") != std::string::npos || s.find("3ba") != std::string::npos ||
      m.find("3bbb") != std::string::npos || s.find("3bbb") != std::string::npos) {
    return "CBBE 3BA";
  }
  if (m.find("cbbe") != std::string::npos || s.find("cbbe") != std::string::npos) {
    return "CBBE";
  }
  if (m.find("bhunp") != std::string::npos || s.find("bhunp") != std::string::npos ||
      m.find("unp") != std::string::npos || s.find("unp") != std::string::npos) {
    return "UNP/BHUNP";
  }
  return "Unknown";
}

bool families_conflict(std::string_view a, std::string_view b) {
  if (a.empty() || b.empty() || a == "Unknown" || b == "Unknown" || a == b) {
    return false;
  }
  // "CBBE" and "CBBE 3BA" share the same base UV/seam topology.
  if ((a == "CBBE" && b == "CBBE 3BA") || (a == "CBBE 3BA" && b == "CBBE")) {
    return false;
  }
  return true;
}

}  // namespace

SceneIntegrityReport analyze_scene_integrity(
    const rendering::CharacterScene& scene,
    const std::optional<rendering::Appearance>& appearance) {
  SceneIntegrityReport report;
  report.character_scale = scene.scale > 0.0f ? scene.scale : 1.0f;
  if (appearance.has_value()) {
    report.npc_form_key = appearance->npc_form_key;
    report.npc_weight = appearance->weight;
  }

  // Map addon form_key / mesh_path to weight_slider flag when Appearance is provided.
  std::unordered_map<std::string, bool> weight_slider_by_mesh;
  if (appearance.has_value()) {
    for (const auto& piece : appearance->pieces) {
      for (const auto& addon : piece.addons) {
        if (!addon.mesh.empty()) {
          weight_slider_by_mesh[text::to_lower_ascii(addon.mesh)] = addon.weight_slider;
        }
      }
    }
  }

  // Inspect scene-level issues for missing or unreadable meshes and weight mismatches.
  for (const auto& iss : scene.issues) {
    if (iss.code == "MeshMissing" || iss.code == "SkeletonMissing") {
      ++report.missing_mesh_count;
      report.issues.push_back({"error", iss.code, "visibility", iss.message, iss.subject, "", "",
                               "", "", "", "Scene"});
    } else if (iss.code == "MeshUnreadable" || iss.code == "SkeletonUnreadable") {
      ++report.unreadable_mesh_count;
      report.issues.push_back({"error", iss.code, "visibility", iss.message, iss.subject, "", "",
                               "", "", "", "Scene"});
    } else if (iss.code == "WeightTopologyMismatch") {
      ++report.weight_interpolation_defects;
      report.issues.push_back({"error", "WeightTopologyMismatch", "weight", iss.message,
                               iss.subject, "", "", "", "", "", "Body"});
    }
  }

  for (const auto& od : scene.outfit_diagnostics) {
    if (od.equipped && !od.meshes_exist) {
      ++report.missing_mesh_count;
      report.issues.push_back({"error", "OutfitMeshMissing", "visibility",
                               "Equipped outfit piece '" + od.name + "' is missing its NIF mesh.",
                               od.mesh_paths.empty() ? "" : od.mesh_paths.front(), "", od.name,
                               od.armor_form_key, "", "", "Outfit"});
    }
  }

  // Collect lower-body / torso garments (slots 45, 49, 37) to detect embedded-body visibility leaks.
  std::vector<const rendering::RenderPart*> secondary_garments;
  for (const auto& part : scene.parts) {
    if (classify_part_role(part) == SurfaceRole::VisibleGarment) {
      const auto slots = rendering::slots_from_mask(part.slot_mask);
      for (int sl : slots) {
        if (sl != 32) {
          secondary_garments.push_back(&part);
          break;
        }
      }
    }
  }

  std::set<std::string> detected_families;
  std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> equipped_piece_vis_counts;

  for (const auto& part : scene.parts) {
    PartIntegrityDiagnostic diag;
    diag.part_id = part.id;
    diag.label = part.label;
    diag.group = part.group;
    diag.shape_name = part.shape_name;
    diag.mesh_path = part.mesh_path;
    diag.provider = part.mesh_location;
    diag.armor_form_key = part.armor_form_key;
    diag.addon_form_key = part.addon_form_key;
    diag.role = classify_part_role(part);
    diag.shown_by_default = part.shown_by_default;
    diag.hidden_reason = part.hidden_reason;
    diag.vertex_count = part.vertices.size() / kFloatsPerVertex;
    diag.triangle_count = part.indices.size() / 3;
    diag.bounds = {part.min, part.max};
    diag.weight_blended = part.weight_blended;
    diag.detected_family = classify_family_string(part.mesh_path, part.shape_name);

    if (diag.detected_family != "Unknown" &&
        diag.role != SurfaceRole::HiddenNifShape &&
        diag.role != SurfaceRole::SmpCollisionProxy &&
        diag.role != SurfaceRole::UnequippedOutfitPiece) {
      detected_families.insert(diag.detected_family);
    }

    if (part.group == "outfit" && diag.role != SurfaceRole::UnequippedOutfitPiece) {
      auto& counts = equipped_piece_vis_counts[part.label.empty() ? part.mesh_path : part.label];
      ++counts.first;
      if (part.shown_by_default) {
        ++counts.second;
      }
    }

    // Track base anatomical presence
    const std::string lower_shape = text::to_lower_ascii(part.shape_name);
    const std::string lower_mesh = text::to_lower_ascii(part.mesh_path);
    if (diag.role == SurfaceRole::VisibleHeadSkin ||
        (part.group == "head" && part.shown_by_default && lower_shape.find("head") != std::string::npos)) {
      report.head_loaded = true;
    }
    if (diag.role == SurfaceRole::VisibleBodySkin || diag.role == SurfaceRole::EmbeddedBodyInOutfit ||
        diag.role == SurfaceRole::CoveredBodyBySlot) {
      if ((part.max[1] - part.min[1]) >= 35.0f || lower_mesh.find("body") != std::string::npos ||
          lower_shape == "3ba") {
        report.body_or_torso_loaded = true;
      }
      if (lower_mesh.find("hand") != std::string::npos || lower_shape.find("hand") != std::string::npos) {
        report.hands_loaded = true;
      }
      if (lower_mesh.find("feet") != std::string::npos || lower_mesh.find("foot") != std::string::npos ||
          lower_shape.find("feet") != std::string::npos || lower_shape.find("foot") != std::string::npos) {
        report.feet_loaded = true;
      }
    }

    // Weight slider interpolation check
    auto ws_it = weight_slider_by_mesh.find(lower_mesh);
    if (ws_it != weight_slider_by_mesh.end() && ws_it->second) {
      diag.weight_slider_expected = true;
      if (appearance.has_value() && appearance->weight < 99.5f &&
          lower_mesh.ends_with("_1.nif") && !part.weight_blended && part.shown_by_default) {
        ++report.weight_interpolation_defects;
        report.issues.push_back({
            "warning",
            "WeightSliderNotBlended",
            "weight",
            "Mesh '" + part.mesh_path + "' (shape '" + part.shape_name +
                "') has weightSlider=true for NPC weight " +
                std::to_string(static_cast<int>(appearance->weight)) +
                ", but _0/_1 weight interpolation was not applied (missing or mismatched _0.nif).",
            part.mesh_path,
            part.shape_name,
            part.label,
            part.armor_form_key,
            part.addon_form_key,
            part.mesh_location,
            "Body"});
      }
    }

    // Check vertex finiteness and spiky triangle deformations on visible parts
    if (part.shown_by_default && diag.vertex_count > 0) {
      for (std::size_t v = 0; v < diag.vertex_count; ++v) {
        const float* pv = part.vertices.data() + v * kFloatsPerVertex;
        if (!std::isfinite(pv[0]) || !std::isfinite(pv[1]) || !std::isfinite(pv[2]) ||
            !std::isfinite(pv[3]) || !std::isfinite(pv[4]) || !std::isfinite(pv[5])) {
          diag.has_nan_or_inf = true;
          break;
        }
      }

      const float spike_edge_limit = 32.0f * report.character_scale;
      for (std::size_t t = 0; t + 2 < part.indices.size(); t += 3) {
        const std::uint32_t i0 = part.indices[t + 0];
        const std::uint32_t i1 = part.indices[t + 1];
        const std::uint32_t i2 = part.indices[t + 2];
        if (i0 >= diag.vertex_count || i1 >= diag.vertex_count || i2 >= diag.vertex_count) {
          diag.has_nan_or_inf = true;
          continue;
        }
        const float* p0 = part.vertices.data() + i0 * kFloatsPerVertex;
        const float* p1 = part.vertices.data() + i1 * kFloatsPerVertex;
        const float* p2 = part.vertices.data() + i2 * kFloatsPerVertex;
        auto edge_len = [](const float* a, const float* b) {
          const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
          return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        const float e0 = edge_len(p0, p1);
        const float e1 = edge_len(p1, p2);
        const float e2 = edge_len(p2, p0);
        const float max_e = std::max({e0, e1, e2});
        diag.max_edge_length = std::max(diag.max_edge_length, max_e);
        if (max_e > spike_edge_limit) {
          ++diag.spiky_triangle_count;
        }
      }

      if (diag.has_nan_or_inf || diag.spiky_triangle_count > 0) {
        ++report.abnormal_deformation_parts;
        report.issues.push_back({
            "error",
            "AbnormalVertexDeformation",
            "morph",
            "Abnormal vertex deformation in shape '" + part.shape_name + "' (" + part.mesh_path +
                "): NaN/Inf=" + (diag.has_nan_or_inf ? "true" : "false") +
                ", spiky triangles=" + std::to_string(diag.spiky_triangle_count) +
                " (max edge=" + std::to_string(diag.max_edge_length) + " u).",
            part.mesh_path,
            part.shape_name,
            part.label,
            part.armor_form_key,
            part.addon_form_key,
            part.mesh_location,
            "Body"});
      }

      // Check for out-of-position geometry
      const nif::Vec3 c = diag.bounds.center();
      if (diag.role == SurfaceRole::VisibleHeadSkin && c.y < 55.0f * report.character_scale) {
        diag.out_of_position = true;
        diag.out_of_position_reason =
            "Head mesh center Y=" + std::to_string(c.y) + " is far below anatomical head height (~105 u).";
      } else if (std::abs(c.x) > 85.0f * report.character_scale ||
                 c.y < -30.0f * report.character_scale ||
                 c.y > 175.0f * report.character_scale ||
                 std::abs(c.z) > 85.0f * report.character_scale) {
        diag.out_of_position = true;
        diag.out_of_position_reason = "Part bounding box center (" + std::to_string(c.x) + ", " +
                                      std::to_string(c.y) + ", " + std::to_string(c.z) +
                                      ") is displaced outside the character volume.";
      }
      if (diag.out_of_position) {
        ++report.out_of_position_parts;
        report.issues.push_back({"error", "GeometryOutOfPosition", "visibility",
                                 diag.out_of_position_reason, part.mesh_path, part.shape_name,
                                 part.label, part.armor_form_key, part.addon_form_key,
                                 part.mesh_location, "Scene"});
      }

      // Defect 9: Check if an embedded full body inside a slot-32 garment stays visible under secondary garments (slots 45/49)
      if (diag.role == SurfaceRole::EmbeddedBodyInOutfit && (part.max[1] - part.min[1]) > 55.0f &&
          !secondary_garments.empty()) {
        std::vector<std::string> overlapping_pieces;
        for (const auto* sg : secondary_garments) {
          if (sg->mesh_path == part.mesh_path) continue;
          const Aabb3D sg_box{sg->min, sg->max};
          if (diag.bounds.overlaps(sg_box)) {
            const std::string lbl = sg->label.empty() ? sg->shape_name : sg->label;
            if (std::find(overlapping_pieces.begin(), overlapping_pieces.end(), lbl) ==
                overlapping_pieces.end()) {
              overlapping_pieces.push_back(lbl);
            }
          }
        }
        if (!overlapping_pieces.empty()) {
          diag.body_leak_under_outfit = true;
          std::ostringstream oss;
          oss << "Embedded full body shape '" << part.shape_name << "' inside '" << part.label
              << "' (" << part.mesh_path
              << ") remains visible beneath secondary garments occupying other slots (";
          for (std::size_t k = 0; k < overlapping_pieces.size(); ++k) {
            if (k > 0) oss << ", ";
            oss << overlapping_pieces[k];
          }
          oss << "), causing under-garment body visibility leaks and cross-garment clipping.";
          diag.body_leak_reason = oss.str();
          ++report.body_visibility_leaks;
          report.issues.push_back({"warning", "CoveredBodyVisibleLeak", "visibility",
                                   diag.body_leak_reason, part.mesh_path, part.shape_name,
                                   part.label, part.armor_form_key, part.addon_form_key,
                                   part.mesh_location, "Torso"});
        }
      }
    }

    report.part_diagnostics.push_back(std::move(diag));
  }

  // Check if any equipped outfit piece had all of its shapes hidden unexpectedly.
  for (const auto& [piece_name, counts] : equipped_piece_vis_counts) {
    if (counts.first > 0 && counts.second == 0) {
      ++report.unexpected_hidden_pieces;
      report.issues.push_back({
          "error",
          "OutfitPieceUnexpectedlyHidden",
          "visibility",
          "Equipped outfit piece '" + piece_name + "' has " + std::to_string(counts.first) +
              " shape(s), but 0 are visible by default.",
          "",
          "",
          piece_name,
          "",
          "",
          "",
          "Outfit"});
    }
  }

  // Check for cross-family conflicts (e.g. CBBE 3BA vs UBE).
  std::vector<std::string> fam_list(detected_families.begin(), detected_families.end());
  for (std::size_t i = 0; i < fam_list.size(); ++i) {
    for (std::size_t j = i + 1; j < fam_list.size(); ++j) {
      if (families_conflict(fam_list[i], fam_list[j])) {
        ++report.family_mismatches;
        report.issues.push_back({
            "error",
            "IncompatibleBodyFamilies",
            "family",
            "Scene mixes incompatible body families: '" + fam_list[i] + "' and '" + fam_list[j] + "'.",
            "",
            "",
            "",
            "",
            "",
            "",
            "Body"});
      }
    }
  }

  if (detected_families.empty()) {
    report.detected_body_family = "Standard/Unknown";
  } else {
    std::ostringstream oss;
    bool first = true;
    for (const auto& f : detected_families) {
      if (!first) oss << " + ";
      oss << f;
      first = false;
    }
    report.detected_body_family = oss.str();
  }

  if (scene.parts.empty()) {
    report.verdict = QaVerdict::Skip;
  } else if (report.missing_mesh_count > 0 || report.unreadable_mesh_count > 0 ||
             report.unexpected_hidden_pieces > 0 || report.out_of_position_parts > 0 ||
             report.abnormal_deformation_parts > 0 || report.family_mismatches > 0) {
    report.verdict = QaVerdict::Fail;
  } else {
    report.verdict = QaVerdict::Pass;
  }

  return report;
}

NpcVisualQaReport evaluate_character_scene(
    const rendering::CharacterScene& scene,
    const std::optional<rendering::Appearance>& appearance,
    std::string_view npc_name,
    std::string_view profile_name) {
  const auto t0 = std::chrono::steady_clock::now();

  NpcVisualQaReport report;
  if (appearance.has_value()) {
    report.npc_form_key = appearance->npc_form_key;
  }
  report.npc_name = std::string(npc_name);
  report.profile_name = std::string(profile_name);

  report.integrity = analyze_scene_integrity(scene, appearance);
  report.clipping = analyze_scene_clipping(scene);
  report.seams = analyze_scene_seams(scene);

  report.all_issues.insert(report.all_issues.end(), report.integrity.issues.begin(),
                           report.integrity.issues.end());
  report.all_issues.insert(report.all_issues.end(), report.clipping.issues.begin(),
                           report.clipping.issues.end());
  report.all_issues.insert(report.all_issues.end(), report.seams.issues.begin(),
                           report.seams.issues.end());

  report.overall_verdict =
      combine_verdicts(report.integrity.verdict,
                       combine_verdicts(report.clipping.verdict, report.seams.verdict));

  const auto t1 = std::chrono::steady_clock::now();
  report.analysis_duration_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  report.peak_working_set_bytes = current_peak_working_set_bytes();
  return report;
}

nlohmann::json to_json(const SceneIntegrityReport& report) {
  nlohmann::json parts_json = nlohmann::json::array();
  for (const auto& p : report.part_diagnostics) {
    parts_json.push_back({{"part_id", p.part_id},
                          {"label", p.label},
                          {"group", p.group},
                          {"shape_name", p.shape_name},
                          {"mesh_path", p.mesh_path},
                          {"provider", p.provider},
                          {"armor_form_key", p.armor_form_key},
                          {"addon_form_key", p.addon_form_key},
                          {"role", to_string(p.role)},
                          {"shown_by_default", p.shown_by_default},
                          {"hidden_reason", p.hidden_reason},
                          {"vertex_count", p.vertex_count},
                          {"triangle_count", p.triangle_count},
                          {"bounds_min", p.bounds.min},
                          {"bounds_max", p.bounds.max},
                          {"weight_slider_expected", p.weight_slider_expected},
                          {"weight_blended", p.weight_blended},
                          {"has_nan_or_inf", p.has_nan_or_inf},
                          {"spiky_triangle_count", p.spiky_triangle_count},
                          {"max_edge_length", p.max_edge_length},
                          {"out_of_position", p.out_of_position},
                          {"out_of_position_reason", p.out_of_position_reason},
                          {"body_leak_under_outfit", p.body_leak_under_outfit},
                          {"body_leak_reason", p.body_leak_reason},
                          {"detected_family", p.detected_family}});
  }

  nlohmann::json issues_json = nlohmann::json::array();
  for (const auto& iss : report.issues) {
    issues_json.push_back(to_json(iss));
  }

  return {{"verdict", to_string(report.verdict)},
          {"npc_form_key", report.npc_form_key},
          {"npc_weight", report.npc_weight},
          {"character_scale", report.character_scale},
          {"detected_body_family", report.detected_body_family},
          {"head_loaded", report.head_loaded},
          {"body_or_torso_loaded", report.body_or_torso_loaded},
          {"hands_loaded", report.hands_loaded},
          {"feet_loaded", report.feet_loaded},
          {"missing_mesh_count", report.missing_mesh_count},
          {"unreadable_mesh_count", report.unreadable_mesh_count},
          {"unexpected_hidden_pieces", report.unexpected_hidden_pieces},
          {"out_of_position_parts", report.out_of_position_parts},
          {"weight_interpolation_defects", report.weight_interpolation_defects},
          {"abnormal_deformation_parts", report.abnormal_deformation_parts},
          {"body_visibility_leaks", report.body_visibility_leaks},
          {"family_mismatches", report.family_mismatches},
          {"part_diagnostics", std::move(parts_json)},
          {"issues", std::move(issues_json)}};
}

nlohmann::json to_json(const NpcVisualQaReport& report) {
  nlohmann::json caps_json = nlohmann::json::array();
  for (const auto& c : report.captures) {
    caps_json.push_back(to_json(c));
  }
  nlohmann::json vcmp_json = nlohmann::json::array();
  for (const auto& vc : report.visual_comparisons) {
    vcmp_json.push_back(to_json(vc));
  }
  nlohmann::json issues_json = nlohmann::json::array();
  for (const auto& iss : report.all_issues) {
    issues_json.push_back(to_json(iss));
  }

  nlohmann::json out = {{"run_id", report.run_id},
                        {"timestamp_utc", report.timestamp_utc},
                        {"npc_form_key", report.npc_form_key},
                        {"npc_name", report.npc_name},
                        {"profile_name", report.profile_name},
                        {"mo2_ini_path", report.mo2_ini_path},
                        {"overall_verdict", to_string(report.overall_verdict)},
                        {"analysis_duration_ms", report.analysis_duration_ms},
                        {"peak_working_set_bytes", report.peak_working_set_bytes},
                        {"integrity", to_json(report.integrity)},
                        {"clipping", to_json(report.clipping)},
                        {"seams", to_json(report.seams)},
                        {"captures", std::move(caps_json)},
                        {"visual_comparisons", std::move(vcmp_json)},
                        {"all_issues", std::move(issues_json)}};
  if (report.morph_comparison.has_value()) {
    out["morph_comparison"] = to_json(*report.morph_comparison);
  }
  return out;
}

nlohmann::json to_json(const RegressionScenarioResult& scenario) {
  nlohmann::json issues_json = nlohmann::json::array();
  for (const auto& iss : scenario.issues) {
    issues_json.push_back(to_json(iss));
  }
  return {{"id", scenario.id},
          {"title", scenario.title},
          {"verdict", to_string(scenario.verdict)},
          {"duration_ms", scenario.duration_ms},
          {"peak_memory_bytes", scenario.peak_memory_bytes},
          {"summary", scenario.summary},
          {"evidence_notes", scenario.evidence_notes},
          {"metrics", scenario.metrics},
          {"issues", std::move(issues_json)}};
}

nlohmann::json to_json(const VisualRegressionSuiteReport& suite) {
  nlohmann::json scen_json = nlohmann::json::array();
  for (const auto& s : suite.scenarios) {
    scen_json.push_back(to_json(s));
  }
  return {{"suite_name", suite.suite_name},
          {"run_id", suite.run_id},
          {"timestamp_utc", suite.timestamp_utc},
          {"overall_verdict", to_string(suite.overall_verdict)},
          {"passed_count", suite.passed_count},
          {"failed_count", suite.failed_count},
          {"skipped_count", suite.skipped_count},
          {"inconclusive_count", suite.inconclusive_count},
          {"total_duration_ms", suite.total_duration_ms},
          {"scenarios", std::move(scen_json)}};
}

}  // namespace sns::visual_qa
