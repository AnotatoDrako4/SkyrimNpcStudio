// Reproducible report writer for Skyrim NPC Studio Visual QA Lab.
// Writes report.json, summary.md, geometry-diff.json, issues.json, and SHA-256 artifact manifests
// to out/visual-qa/<run-id>/ without touching Skyrim/Data or any MO2 directory.
#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/visual_qa/scene_inspector.hpp"

namespace sns::visual_qa {

struct BuildAndToolMetadata {
  std::string studio_version = "1.1.0";
  std::string visual_qa_version = "1.0.0-ag03";
  std::string compiler_id = "MSVC";
  std::string build_type = "Debug";
  std::string build_dir;
  std::string executable_path;
  std::string executable_sha256;
  std::string worker_path;
  std::string worker_sha256;
};

struct ArtifactHashEntry {
  std::string relative_path;
  std::string sha256;
  std::uint64_t size_bytes = 0;
};

struct ReportBundlePaths {
  std::filesystem::path run_dir;
  std::filesystem::path report_json;
  std::filesystem::path summary_md;
  std::filesystem::path geometry_diff_json;
  std::filesystem::path issues_json;
  std::filesystem::path hashes_json;
};

// Computes the lowercase hex SHA-256 digest of a byte buffer or file on disk.
[[nodiscard]] std::string sha256_bytes(std::string_view data);
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);

// Formats an ISO-8601 UTC timestamp and generates a collision-free run identifier.
[[nodiscard]] std::string current_utc_timestamp();
[[nodiscard]] std::string make_run_id(std::string_view prefix = "vqa");

// Writes a complete Visual QA Lab execution bundle to `out_root / run_id` (e.g. out/visual-qa/<run_id>/).
[[nodiscard]] ReportBundlePaths write_visual_qa_report_bundle(
    const std::filesystem::path& out_root,
    const NpcVisualQaReport& primary_npc_report,
    const std::optional<VisualRegressionSuiteReport>& regression_suite = std::nullopt,
    const BuildAndToolMetadata& build_meta = {},
    const std::map<std::string, std::string>& input_hashes = {});

}  // namespace sns::visual_qa
