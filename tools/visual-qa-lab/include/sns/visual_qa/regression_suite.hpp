// Automated SNS Visual Regression Suite (VQA-01 .. VQA-10) runner for Skyrim NPC Studio Visual QA Lab.
// Executes both deterministic geometric fixtures and real MO2 profile evaluations (Serana + Lydia).
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "sns/visual_qa/report_writer.hpp"
#include "sns/visual_qa/scene_inspector.hpp"

namespace sns::visual_qa {

struct RegressionSuiteConfig {
  std::filesystem::path mo2_ini_path = "D:/Apps/ModOrganizer.ini";
  std::string profile_name = "Default";
  std::string primary_npc_form_key = "002B6C:Dawnguard.esm";   // Serana
  std::string secondary_npc_form_key = "0A2C8E:Skyrim.esm";    // Lydia
  std::filesystem::path worker_exe_path;
  std::filesystem::path cache_root_path = "out/f0-latest-local-cache";
  std::filesystem::path output_root_dir = "out/visual-qa";
  std::string run_id;
  std::uint32_t capture_width = 512;
  std::uint32_t capture_height = 512;
  bool write_captures_and_diffs = true;
};

struct RegressionSuiteExecutionOutput {
  NpcVisualQaReport primary_npc_report;
  std::optional<NpcVisualQaReport> secondary_npc_report;
  VisualRegressionSuiteReport suite_report;
  ReportBundlePaths bundle_paths;
};

// Runs VQA-01 through VQA-10, generates 8-view captures and before/after visual diffs,
// and writes the complete report bundle to `config.output_root_dir / run_id`.
[[nodiscard]] RegressionSuiteExecutionOutput run_visual_regression_suite(
    const RegressionSuiteConfig& config = {});

}  // namespace sns::visual_qa
