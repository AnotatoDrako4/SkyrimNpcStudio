// Standalone CLI for Skyrim NPC Studio — Visual QA Lab (AG03).
// Usage:
//   sns-visual-qa [--mo2-ini <path>] [--profile <name>] [--npc <formKey>] [--out-dir <dir>] [--run-id <id>]
//   sns-visual-qa --compare-images <before.png> <after.png> <diff_out.png>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "sns/visual_qa/image_comparator.hpp"
#include "sns/visual_qa/regression_suite.hpp"
#include "sns/visual_qa/report_writer.hpp"

int main(int argc, char** argv) {
  using namespace sns::visual_qa;

  RegressionSuiteConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--compare-images" && i + 3 < argc) {
      const std::filesystem::path before_path(argv[i + 1]);
      const std::filesystem::path after_path(argv[i + 2]);
      const std::filesystem::path diff_path(argv[i + 3]);
      RgbaBuffer img_a, img_b;
      std::string err;
      if (!load_png(before_path, img_a, &err)) {
        std::cerr << "[visual-qa] failed to load before image: " << err << "\n";
        return 2;
      }
      if (!load_png(after_path, img_b, &err)) {
        std::cerr << "[visual-qa] failed to load after image: " << err << "\n";
        return 2;
      }
      auto cmp = compare_images("cli_diff", img_a, img_b, diff_path);
      cmp.before_path = before_path.string();
      cmp.after_path = after_path.string();
      std::cout << to_json(cmp).dump(2) << "\n";
      return cmp.verdict == QaVerdict::Fail ? 1 : 0;
    } else if (arg == "--mo2-ini" && i + 1 < argc) {
      cfg.mo2_ini_path = argv[++i];
    } else if (arg == "--profile" && i + 1 < argc) {
      cfg.profile_name = argv[++i];
    } else if (arg == "--npc" && i + 1 < argc) {
      cfg.primary_npc_form_key = argv[++i];
    } else if (arg == "--secondary-npc" && i + 1 < argc) {
      cfg.secondary_npc_form_key = argv[++i];
    } else if (arg == "--worker" && i + 1 < argc) {
      cfg.worker_exe_path = argv[++i];
    } else if (arg == "--cache-dir" && i + 1 < argc) {
      cfg.cache_root_path = argv[++i];
    } else if (arg == "--out-dir" && i + 1 < argc) {
      cfg.output_root_dir = argv[++i];
    } else if (arg == "--run-id" && i + 1 < argc) {
      cfg.run_id = argv[++i];
    } else if (arg == "--no-captures") {
      cfg.write_captures_and_diffs = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Skyrim NPC Studio — Visual QA Lab (AG03)\n"
          << "Usage:\n"
          << "  sns-visual-qa [--mo2-ini <path>] [--profile <name>] [--npc <formKey>]\n"
          << "                [--secondary-npc <formKey>] [--out-dir <dir>] [--run-id <id>]\n"
          << "  sns-visual-qa --compare-images <before.png> <after.png> <diff_out.png>\n";
      return 0;
    }
  }

  std::cout << "[visual-qa] Starting Skyrim NPC Studio Visual QA Lab...\n";
  const auto result = run_visual_regression_suite(cfg);

  std::cout << "[visual-qa] Run ID: " << result.suite_report.run_id << "\n";
  std::cout << "[visual-qa] Primary NPC (" << result.primary_npc_report.npc_name << " "
            << result.primary_npc_report.npc_form_key
            << ") verdict: " << to_string(result.primary_npc_report.overall_verdict) << " ("
            << result.primary_npc_report.all_issues.size() << " issues)\n";
  std::cout << "[visual-qa] Regression Suite verdict: "
            << to_string(result.suite_report.overall_verdict)
            << " (PASS=" << result.suite_report.passed_count
            << ", FAIL=" << result.suite_report.failed_count
            << ", SKIP=" << result.suite_report.skipped_count
            << ", INCONCLUSIVE=" << result.suite_report.inconclusive_count << ")\n";
  for (const auto& sc : result.suite_report.scenarios) {
    std::cout << "  [" << to_string(sc.verdict) << "] " << sc.id << " - " << sc.title << " ("
              << sc.duration_ms << " ms): " << sc.summary << "\n";
  }
  std::cout << "[visual-qa] Reports written to: " << result.bundle_paths.run_dir.string() << "\n";
  std::cout << "  - report.json:        " << result.bundle_paths.report_json.string() << "\n";
  std::cout << "  - summary.md:         " << result.bundle_paths.summary_md.string() << "\n";
  std::cout << "  - geometry-diff.json: " << result.bundle_paths.geometry_diff_json.string() << "\n";
  std::cout << "  - issues.json:        " << result.bundle_paths.issues_json.string() << "\n";
  std::cout << "  - artifact-hashes:    " << result.bundle_paths.hashes_json.string() << "\n";

  return result.suite_report.failed_count > 0 ? 1 : 0;
}
