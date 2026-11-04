#include "sns/visual_qa/report_writer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace sns::visual_qa {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

constexpr std::uint32_t rotr32(std::uint32_t x, unsigned int n) {
  return (x >> n) | (x << (32u - n));
}

struct Sha256Context {
  std::array<std::uint32_t, 8> state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> block{};
  std::size_t block_len = 0;
  std::uint64_t total_bits = 0;

  void transform(const std::uint8_t data[64]) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(data[i * 4 + 0]) << 24u) |
             (static_cast<std::uint32_t>(data[i * 4 + 1]) << 16u) |
             (static_cast<std::uint32_t>(data[i * 4 + 2]) << 8u) |
             (static_cast<std::uint32_t>(data[i * 4 + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3u);
      const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10u);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
      const std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = S0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  void update(const std::uint8_t* bytes, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
      block[block_len++] = bytes[i];
      if (block_len == 64) {
        transform(block.data());
        total_bits += 512;
        block_len = 0;
      }
    }
  }

  std::string finalize() {
    total_bits += static_cast<std::uint64_t>(block_len) * 8u;
    block[block_len++] = 0x80u;
    if (block_len > 56) {
      while (block_len < 64) block[block_len++] = 0;
      transform(block.data());
      block_len = 0;
    }
    while (block_len < 56) block[block_len++] = 0;
    for (int i = 7; i >= 0; --i) {
      block[56 + (7 - i)] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xFFu);
    }
    transform(block.data());

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint32_t word : state) {
      oss << std::setw(8) << word;
    }
    return oss.str();
  }
};

void write_text_file(const std::filesystem::path& p, std::string_view text) {
  std::ofstream out(p, std::ios::binary);
  if (out) {
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }
}

}  // namespace

std::string sha256_bytes(std::string_view data) {
  Sha256Context ctx;
  ctx.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
  return ctx.finalize();
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  Sha256Context ctx;
  std::array<char, 32768> buf{};
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize got = in.gcount();
    if (got > 0) {
      ctx.update(reinterpret_cast<const std::uint8_t*>(buf.data()), static_cast<std::size_t>(got));
    }
  }
  return ctx.finalize();
}

std::string current_utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm utc_tm{};
  gmtime_s(&utc_tm, &t);
  std::ostringstream oss;
  oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string make_run_id(std::string_view prefix) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm utc_tm{};
  gmtime_s(&utc_tm, &t);
  std::ostringstream oss;
  if (!prefix.empty()) {
    oss << prefix << "-";
  }
  oss << std::put_time(&utc_tm, "%Y%m%d-%H%M%S");
  return oss.str();
}

ReportBundlePaths write_visual_qa_report_bundle(
    const std::filesystem::path& out_root,
    const NpcVisualQaReport& primary_npc_report,
    const std::optional<VisualRegressionSuiteReport>& regression_suite,
    const BuildAndToolMetadata& build_meta,
    const std::map<std::string, std::string>& input_hashes) {
  const std::string run_id =
      !primary_npc_report.run_id.empty() ? primary_npc_report.run_id : make_run_id("vqa");
  const std::string ts = !primary_npc_report.timestamp_utc.empty()
                             ? primary_npc_report.timestamp_utc
                             : current_utc_timestamp();

  ReportBundlePaths paths;
  paths.run_dir = out_root / run_id;
  paths.report_json = paths.run_dir / "report.json";
  paths.summary_md = paths.run_dir / "summary.md";
  paths.geometry_diff_json = paths.run_dir / "geometry-diff.json";
  paths.issues_json = paths.run_dir / "issues.json";
  paths.hashes_json = paths.run_dir / "artifact-hashes.json";

  std::error_code ec;
  std::filesystem::create_directories(paths.run_dir, ec);

  // 1. Build geometry-diff.json
  nlohmann::json geom_diff = {
      {"run_id", run_id},
      {"timestamp_utc", ts},
      {"npc_form_key", primary_npc_report.npc_form_key},
      {"npc_name", primary_npc_report.npc_name},
      {"profile_name", primary_npc_report.profile_name},
      {"integrity", to_json(primary_npc_report.integrity)},
      {"clipping", to_json(primary_npc_report.clipping)},
      {"seams", to_json(primary_npc_report.seams)}};
  if (primary_npc_report.morph_comparison.has_value()) {
    geom_diff["morph_comparison"] = to_json(*primary_npc_report.morph_comparison);
  }
  write_text_file(paths.geometry_diff_json, geom_diff.dump(2));

  // 2. Build issues.json
  nlohmann::json issues_arr = nlohmann::json::array();
  for (const auto& iss : primary_npc_report.all_issues) {
    issues_arr.push_back(to_json(iss));
  }
  if (regression_suite.has_value()) {
    for (const auto& sc : regression_suite->scenarios) {
      for (const auto& iss : sc.issues) {
        issues_arr.push_back(to_json(iss));
      }
    }
  }
  nlohmann::json issues_doc = {{"run_id", run_id},
                               {"timestamp_utc", ts},
                               {"total_issues", issues_arr.size()},
                               {"issues", std::move(issues_arr)}};
  write_text_file(paths.issues_json, issues_doc.dump(2));

  // 3. Build report.json
  nlohmann::json build_json = {{"studio_version", build_meta.studio_version},
                               {"visual_qa_version", build_meta.visual_qa_version},
                               {"compiler_id", build_meta.compiler_id},
                               {"build_type", build_meta.build_type},
                               {"build_dir", build_meta.build_dir},
                               {"executable_path", build_meta.executable_path},
                               {"executable_sha256", build_meta.executable_sha256},
                               {"worker_path", build_meta.worker_path},
                               {"worker_sha256", build_meta.worker_sha256}};

  nlohmann::json report_doc = {{"run_id", run_id},
                               {"timestamp_utc", ts},
                               {"in_game_verification", "PENDIENTE_SKYRIM"},
                               {"build_metadata", std::move(build_json)},
                               {"input_hashes", input_hashes},
                               {"primary_npc_evaluation", to_json(primary_npc_report)}};
  if (regression_suite.has_value()) {
    report_doc["regression_suite"] = to_json(*regression_suite);
  }
  write_text_file(paths.report_json, report_doc.dump(2));

  // 4. Build summary.md
  std::ostringstream md;
  md << std::fixed << std::setprecision(3);
  md << "# Skyrim NPC Studio — Visual QA Lab Report (`" << run_id << "`)\n\n";
  md << "- **Timestamp (UTC):** `" << ts << "`\n";
  md << "- **Primary NPC:** `" << primary_npc_report.npc_name << "` (`"
     << primary_npc_report.npc_form_key << "`)\n";
  md << "- **MO2 Profile:** `" << primary_npc_report.profile_name << "`\n";
  md << "- **MO2 INI Path:** `" << primary_npc_report.mo2_ini_path << "`\n";
  md << "- **Primary NPC Verdict:** **" << to_string(primary_npc_report.overall_verdict) << "**\n";
  md << "- **In-Game Verification Status:** **PENDIENTE_SKYRIM** *(Never marked VERIFICADO_EN_SKYRIM without live game launch)*\n";
  md << "- **Analysis Duration:** `" << primary_npc_report.analysis_duration_ms << " ms`\n";
  md << "- **Peak Working Set:** `"
     << (primary_npc_report.peak_working_set_bytes / (1024.0 * 1024.0)) << " MB`\n\n";

  md << "## 1. Geometric & Anatomical Subsystems\n\n";
  md << "| Subsystem | Verdict | Key Telemetry |\n";
  md << "|---|---|---|\n";
  md << "| **Scene Integrity** | **" << to_string(primary_npc_report.integrity.verdict)
     << "** | Family: `" << primary_npc_report.integrity.detected_body_family
     << "`, Missing meshes: `" << primary_npc_report.integrity.missing_mesh_count
     << "`, Body leaks: `" << primary_npc_report.integrity.body_visibility_leaks
     << "`, Weight defects: `" << primary_npc_report.integrity.weight_interpolation_defects << "` |\n";
  md << "| **Clipping & Garment Fit** | **" << to_string(primary_npc_report.clipping.verdict)
     << "** | Confirmed clipping zones: `" << primary_npc_report.clipping.confirmed_visual_clipping_count
     << "`, Possible: `" << primary_npc_report.clipping.possible_visible_clipping_count
     << "`, Intersecting tri pairs: `" << primary_npc_report.clipping.exact_intersecting_tri_pairs
     << "`, Covered-slot excluded: `" << primary_npc_report.clipping.covered_slot_excluded_intersections << "` |\n";
  md << "| **Anatomical Seams** | **" << to_string(primary_npc_report.seams.verdict)
     << "** | Evaluated: `" << primary_npc_report.seams.junctions_evaluated
     << "`, Aligned: `" << primary_npc_report.seams.junctions_aligned
     << "`, Concealed by garment: `" << primary_npc_report.seams.junctions_covered_by_garment
     << "`, Defective: `" << primary_npc_report.seams.junctions_defective << "` |\n";
  if (primary_npc_report.morph_comparison.has_value()) {
    const auto& mc = *primary_npc_report.morph_comparison;
    md << "| **BodySlide Morphs** | **" << to_string(mc.verdict) << "** | Preset: `"
       << mc.preset_name << "`, Shapes modified: `" << mc.shapes_modified << "/"
       << mc.shapes_compared << "`, Exact matches: `" << mc.exact_bodyslide_Matches
       << "`, False no-change: `" << mc.false_no_change_shapes << "` |\n";
  }
  md << "\n";

  md << "## 2. Anatomical Seam Junction Details\n\n";
  md << "| Junction | Status | Verdict | Part A | Part B | RMS (u) | Max (u) | Radial Mismatch (%) | Concealing Garment |\n";
  md << "|---|---|---|---|---|---:|---:|---:|---|\n";
  for (const auto& j : primary_npc_report.seams.junctions) {
    md << "| `" << to_string(j.kind) << "` | `" << to_string(j.status) << "` | **"
       << to_string(j.verdict) << "** | `" << j.part_a_shape << "` | `" << j.part_b_shape
       << "` | " << j.rms_distance << " | " << j.max_distance << " | " << j.radial_mismatch_pct
       << "% | " << (j.covering_garment_label.empty() ? "-" : "`" + j.covering_garment_label + "`")
       << " |\n";
  }
  md << "\n";

  if (!primary_npc_report.clipping.garment_diagnostics.empty()) {
    md << "## 3. Per-Garment Clipping & Fit Diagnostics\n\n";
    md << "| Piece | Shape | Classification | Verdict | Intersecting Tri Pairs | Max Protrusion (u) | Fit Verdict | Scale Ratio |\n";
    md << "|---|---|---|---|---:|---:|---|---:|\n";
    for (const auto& g : primary_npc_report.clipping.garment_diagnostics) {
      md << "| `" << g.piece_label << "` | `" << g.shape_name << "` | `"
         << to_string(g.overall_classification) << "` | **" << to_string(g.verdict) << "** | "
         << g.total_intersecting_pairs << " | " << g.worst_protrusion << " | `"
         << g.fit.fit_verdict << "` | " << g.fit.scale_ratio_to_body << " |\n";
    }
    md << "\n";
  }

  if (regression_suite.has_value()) {
    md << "## 4. SNS Visual Regression Suite (`VQA-01` .. `VQA-10`)\n\n";
    md << "- **Suite Overall Verdict:** **" << to_string(regression_suite->overall_verdict) << "** "
       << "(`PASS=" << regression_suite->passed_count
       << "`, `FAIL=" << regression_suite->failed_count
       << "`, `SKIP=" << regression_suite->skipped_count
       << "`, `INCONCLUSIVE=" << regression_suite->inconclusive_count << "`)\n\n";
    md << "| ID | Scenario | Verdict | Duration (ms) | Summary |\n";
    md << "|---|---|---|---:|---|\n";
    for (const auto& sc : regression_suite->scenarios) {
      md << "| `" << sc.id << "` | " << sc.title << " | **" << to_string(sc.verdict) << "** | "
         << sc.duration_ms << " | " << sc.summary << " |\n";
    }
    md << "\n";
  }

  if (!primary_npc_report.all_issues.empty()) {
    md << "## 5. Detected Issues (" << primary_npc_report.all_issues.size() << ")\n\n";
    for (const auto& iss : primary_npc_report.all_issues) {
      md << "- **[" << iss.severity << " / `" << iss.code << "`]** (" << iss.category
         << ") — " << iss.message;
      if (!iss.mesh_path.empty()) {
        md << " *(mesh: `" << iss.mesh_path << "`";
        if (!iss.shape_name.empty()) md << ", shape: `" << iss.shape_name << "`";
        md << ")*";
      }
      md << "\n";
    }
    md << "\n";
  }

  write_text_file(paths.summary_md, md.str());

  // 5. Compute SHA-256 hashes for all generated artifacts in run_dir
  nlohmann::json hashes_arr = nlohmann::json::array();
  for (const auto& entry : std::filesystem::recursive_directory_iterator(paths.run_dir, ec)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path() == paths.hashes_json) continue;
    const auto rel = std::filesystem::relative(entry.path(), paths.run_dir, ec).generic_string();
    hashes_arr.push_back({{"relative_path", rel},
                          {"sha256", sha256_file(entry.path())},
                          {"size_bytes", entry.file_size(ec)}});
  }
  nlohmann::json hashes_doc = {{"run_id", run_id},
                               {"timestamp_utc", ts},
                               {"artifacts", std::move(hashes_arr)}};
  write_text_file(paths.hashes_json, hashes_doc.dump(2));

  return paths;
}

}  // namespace sns::visual_qa
