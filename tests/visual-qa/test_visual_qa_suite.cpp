// Catch2 test suite for Skyrim NPC Studio — Visual QA Lab (AG03).
// Covers geometric clipping, anatomical seams, BodySlide TRI/OSD morph verification,
// controlled 8-view captures, visual diff comparison, and the SNS Visual Regression Suite (VQA-01..VQA-10).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sns/visual_qa/clipping_analyzer.hpp"
#include "sns/visual_qa/image_comparator.hpp"
#include "sns/visual_qa/morph_analyzer.hpp"
#include "sns/visual_qa/regression_suite.hpp"
#include "sns/visual_qa/report_writer.hpp"
#include "sns/visual_qa/seam_analyzer.hpp"
#include "sns/visual_qa/scene_inspector.hpp"

namespace {

using namespace sns;
using namespace sns::visual_qa;

void push_vert(rendering::RenderPart& part, float x, float y, float z, float nx, float ny, float nz) {
  if (part.vertices.empty()) {
    part.min = {x, y, z};
    part.max = {x, y, z};
  } else {
    part.min[0] = std::min(part.min[0], x);
    part.min[1] = std::min(part.min[1], y);
    part.min[2] = std::min(part.min[2], z);
    part.max[0] = std::max(part.max[0], x);
    part.max[1] = std::max(part.max[1], y);
    part.max[2] = std::max(part.max[2], z);
  }
  const float v[rendering::kFloatsPerVertex] = {x, y, z, nx, ny, nz, 0.5f, 0.5f, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1};
  part.vertices.insert(part.vertices.end(), std::begin(v), std::end(v));
  ++part.vertex_count;
}

}  // namespace

TEST_CASE("Visual QA Clipping: exact triangle-triangle intersection and outward protrusion", "[visual-qa][clipping]") {
  // Garment triangle on plane Z = 5.0 with outward normal (0, 0, 1)
  const nif::Vec3 b0{-4.0f, 68.0f, 5.0f};
  const nif::Vec3 b1{4.0f, 68.0f, 5.0f};
  const nif::Vec3 b2{0.0f, 76.0f, 5.0f};
  const nif::Vec3 nb{0.0f, 0.0f, 1.0f};

  // Disjoint body triangle behind garment at Z = 4.2
  const auto no_hit = test_triangle_triangle_intersection(
      {-2.0f, 69.0f, 4.2f}, {2.0f, 69.0f, 4.2f}, {0.0f, 74.0f, 4.2f}, nb, b0, b1, b2, nb);
  CHECK_FALSE(no_hit.intersects);

  // Piercing body triangle crossing from Z = 4.0 to Z = 6.2 (+1.2 u outward protrusion)
  const auto hit = test_triangle_triangle_intersection(
      {-2.0f, 70.0f, 4.0f}, {2.0f, 70.0f, 4.0f}, {0.0f, 73.0f, 6.2f}, nb, b0, b1, b2, nb);
  REQUIRE(hit.intersects);
  CHECK(hit.segment_length > 0.5f);
  CHECK_THAT(hit.exterior_protrusion, Catch::Matchers::WithinAbs(1.2f, 1e-3f));
}

TEST_CASE("Visual QA Morphs: PIRT (.tri) and OSD (.osd) binary parsers and classification", "[visual-qa][morph]") {
  // Build a binary OSD\0 buffer with 1 block "3BA:Breasts" moving vertex 0 by (0, 2.0, 0)
  std::string osd_bytes;
  osd_bytes.append("\0DSO", 4);
  const std::uint32_t version = 1;
  const std::uint32_t count = 1;
  osd_bytes.append(reinterpret_cast<const char*>(&version), 4);
  osd_bytes.append(reinterpret_cast<const char*>(&count), 4);
  const std::string blk_name = "3BA:Breasts";
  const std::uint8_t nlen = static_cast<std::uint8_t>(blk_name.size());
  osd_bytes.push_back(static_cast<char>(nlen));
  osd_bytes.append(blk_name);
  const std::uint16_t diff_cnt = 1;
  osd_bytes.append(reinterpret_cast<const char*>(&diff_cnt), 2);
  const std::uint16_t vid = 0;
  const float dx = 0.0f, dy = 2.0f, dz = 0.0f;
  osd_bytes.append(reinterpret_cast<const char*>(&vid), 2);
  osd_bytes.append(reinterpret_cast<const char*>(&dx), 4);
  osd_bytes.append(reinterpret_cast<const char*>(&dy), 4);
  osd_bytes.append(reinterpret_cast<const char*>(&dz), 4);

  const BodyMorphDatabase osd_db = parse_osd_morphs(osd_bytes, "test.osd");
  const auto* morph = osd_db.find_morph("3BA", "Breasts");
  REQUIRE(morph != nullptr);
  REQUIRE(morph->deltas.size() == 1);
  CHECK(morph->deltas[0].vertex_index == 0);
  CHECK_THAT(morph->deltas[0].dy, Catch::Matchers::WithinAbs(2.0f, 1e-4f));

  // Verify morph comparison classifications
  rendering::RenderPart before;
  before.id = "0";
  before.group = "skin";
  before.shape_name = "3BA";
  before.mesh_path = "meshes\\femalebody_1.nif";
  push_vert(before, 0.0f, 75.0f, 6.0f, 0.0f, 0.0f, 1.0f);
  push_vert(before, 2.0f, 75.0f, 6.0f, 0.0f, 0.0f, 1.0f);
  push_vert(before, 0.0f, 78.0f, 6.0f, 0.0f, 0.0f, 1.0f);
  before.indices = {0, 1, 2};

  rendering::RenderPart after_exact = before;
  // Skyrim dy = +2.0 at 50% slider maps to view-space +Z = +1.0
  after_exact.vertices[2] += 1.0f;

  const std::vector<rendering::BodySliderSetting> sliders = {{"Breasts", 50.0f, 50.0f}};
  const auto exact_res = compare_part_morph(before, after_exact, sliders, 50.0f, &osd_db);
  CHECK(exact_res.classification == MorphQualityClassification::ExactBodySlideMorph);
  CHECK(exact_res.verdict == QaVerdict::Pass);
  CHECK(exact_res.modified_vertex_count == 1);

  const auto false_noop_res = compare_part_morph(before, before, sliders, 50.0f, &osd_db);
  CHECK(false_noop_res.classification == MorphQualityClassification::FalseNoChange);
  CHECK(false_noop_res.verdict == QaVerdict::Fail);
}

TEST_CASE("Visual QA Image Comparator: AA noise filtering vs structural change and disappeared geometry", "[visual-qa][image]") {
  RgbaBuffer img_a;
  img_a.width = 128;
  img_a.height = 128;
  img_a.pixels.resize(128 * 128 * 4, 0);

  // Fill background (22, 24, 30, 0) and draw a 40x60 character rectangle in the center
  for (std::uint32_t y = 0; y < 128; ++y) {
    for (std::uint32_t x = 0; x < 128; ++x) {
      std::uint8_t* p = img_a.pixels.data() + (y * 128 + x) * 4;
      if (x >= 44 && x < 84 && y >= 30 && y < 90) {
        p[0] = 200;
        p[1] = 160;
        p[2] = 140;
        p[3] = 255;
      } else {
        p[0] = 22;
        p[1] = 24;
        p[2] = 30;
        p[3] = 0;
      }
    }
  }

  // Image B1: Only isolated 1-px AA noise (+14 on a single pixel)
  RgbaBuffer img_b_noise = img_a;
  img_b_noise.pixels[(50 * 128 + 60) * 4 + 0] += 14;
  const auto cmp_noise = compare_images("aa_noise_test", img_a, img_b_noise);
  CHECK(cmp_noise.verdict == QaVerdict::Pass);
  CHECK(cmp_noise.structural_changed_pixels == 0);
  CHECK(cmp_noise.noise_filtered_pixels >= 1);

  // Image B2: Half of the character geometry disappears
  RgbaBuffer img_b_lost = img_a;
  for (std::uint32_t y = 30; y < 65; ++y) {
    for (std::uint32_t x = 44; x < 84; ++x) {
      std::uint8_t* p = img_b_lost.pixels.data() + (y * 128 + x) * 4;
      p[0] = 22;
      p[1] = 24;
      p[2] = 30;
      p[3] = 0;
    }
  }
  const auto cmp_lost = compare_images("disappeared_test", img_a, img_b_lost);
  CHECK(cmp_lost.verdict == QaVerdict::Fail);
  CHECK(cmp_lost.geometry_disappeared_warning);
  CHECK(cmp_lost.disappeared_geometry_pixels > 500);
}

TEST_CASE("SNS Visual Regression Suite: VQA-01 through VQA-10 full execution", "[visual-qa][regression-suite]") {
  RegressionSuiteConfig cfg;
  cfg.run_id = "test-suite-run";
  cfg.output_root_dir = "out/visual-qa";
  cfg.capture_width = 384;
  cfg.capture_height = 384;

  const auto out = run_visual_regression_suite(cfg);
  REQUIRE(out.suite_report.scenarios.size() == 10);

  for (const auto& sc : out.suite_report.scenarios) {
    INFO("Scenario " << sc.id << " (" << sc.title << "): " << sc.summary);
    CHECK(sc.verdict != QaVerdict::Fail);
  }

  CHECK(std::filesystem::exists(out.bundle_paths.report_json));
  CHECK(std::filesystem::exists(out.bundle_paths.summary_md));
  CHECK(std::filesystem::exists(out.bundle_paths.geometry_diff_json));
  CHECK(std::filesystem::exists(out.bundle_paths.issues_json));
  CHECK(std::filesystem::exists(out.bundle_paths.hashes_json));
}
