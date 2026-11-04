// Reproducible 8-view capture generator and before/after visual image comparator for Visual QA Lab.
// Supports both D3D11 viewport PNGs (from scripts/smoke-test.ps1 -CaptureSet) and deterministic
// scene rasterization with controlled camera, framing, resolution, and lighting metadata.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "sns/rendering/scene.hpp"
#include "sns/visual_qa/visual_qa_types.hpp"

namespace sns::visual_qa {

// The 8 controlled camera views required by Section 7 of the Visual QA Lab specification.
enum class ControlledViewKind {
  FrontFullBody,   // 1. Frontal de cuerpo completo
  BackFullBody,    // 2. Posterior
  SideFullBody,    // 3. Lateral
  Torso,           // 4. Torso
  NeckAndHead,     // 5. Cuello y cabeza
  ArmsAndWrists,   // 6. Brazos y muñecas
  LegsAndAnkles,   // 7. Piernas y tobillos
  FullOutfit       // 8. Vestuario completo
};

[[nodiscard]] std::string_view view_id(ControlledViewKind kind);
[[nodiscard]] std::string_view view_label(ControlledViewKind kind);

// Reproducible camera, framing, resolution, and lighting specification saved alongside every capture.
struct CaptureCameraSpec {
  ControlledViewKind kind = ControlledViewKind::FrontFullBody;
  std::string id;
  std::string label;
  std::uint32_t width = 1024;
  std::uint32_t height = 1024;
  nif::Vec3 eye{0.0f, 62.0f, 160.0f};
  nif::Vec3 target{0.0f, 62.0f, 0.0f};
  nif::Vec3 up{0.0f, 1.0f, 0.0f};
  float ortho_half_height = 70.0f;
  nif::Vec3 key_light_dir{0.35f, 0.45f, 0.82f};
  float ambient_intensity = 0.32f;
  bool include_outfit = true;
  bool highlight_seams_and_clipping = false;
};

// Builds the 8 standard controlled camera presets scaled to a character's vertical bounds.
[[nodiscard]] std::vector<CaptureCameraSpec> build_controlled_camera_set(
    const rendering::CharacterScene& scene,
    std::uint32_t width = 768,
    std::uint32_t height = 768);

struct RgbaBuffer {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;  // RGBA8 row-major (width * height * 4)
};

// Saves or loads an RGBA8 image as PNG on Windows (using WIC / DirectXTex).
bool save_png(const std::filesystem::path& path, const RgbaBuffer& image, std::string* error_out = nullptr);
bool load_png(const std::filesystem::path& path, RgbaBuffer& image_out, std::string* error_out = nullptr);

// Renders a deterministic shaded image of the CharacterScene from the given camera preset.
[[nodiscard]] RgbaBuffer render_controlled_view(
    const rendering::CharacterScene& scene,
    const CaptureCameraSpec& spec);

struct CaptureArtifact {
  CaptureCameraSpec spec;
  std::string image_path;
  std::string sha256;
  std::size_t covered_pixels = 0;
  std::size_t near_white_pixels = 0;
  double coverage_ratio = 0.0;
  bool unexpected_blank_zone = false;
  std::string blank_zone_reason;
};

struct ModifiedPixelRegion {
  std::uint32_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
  std::size_t changed_pixels = 0;
  std::size_t disappeared_pixels = 0;
  std::size_t appeared_pixels = 0;
  std::string anatomical_hint;
};

struct VisualComparisonOptions {
  // Per-channel difference (0..255) below which pixel differences are treated as GPU/lighting/AA noise.
  int channel_noise_tolerance = 10;
  // Minimum 3x3 neighborhood support required so 1-px antialiasing fringes are not flagged as structural changes.
  bool ignore_isolated_aa_edge_pixels = true;
  // Fraction of covered pixels lost that triggers a MissingGeometry / GeometryDisappeared warning.
  double disappeared_coverage_warning_ratio = 0.12;
};

struct VisualComparisonResult {
  std::string view_id;
  std::string before_path;
  std::string after_path;
  std::string diff_image_path;
  QaVerdict verdict = QaVerdict::Pass;
  bool dimensions_match = true;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t total_pixels = 0;
  std::size_t covered_before = 0;
  std::size_t covered_after = 0;
  std::size_t raw_differing_pixels = 0;
  std::size_t noise_filtered_pixels = 0;      // Ignored as GPU/AA/lighting tolerance
  std::size_t structural_changed_pixels = 0;  // Significant visual change beyond AA/lighting tolerance
  std::size_t disappeared_geometry_pixels = 0;
  std::size_t newly_visible_pixels = 0;
  double structural_change_pct = 0.0;         // Percentage of total pixels with structural change
  double mean_abs_diff = 0.0;                 // 0..255
  double rms_diff = 0.0;                      // 0..255
  bool geometry_disappeared_warning = false;
  bool unexpected_transparent_or_empty_zone = false;
  std::vector<ModifiedPixelRegion> modified_regions;
  std::string explanation;
};

// Compares two RGBA buffers (before vs after), optionally writing a visual diff heatmap PNG.
[[nodiscard]] VisualComparisonResult compare_images(
    std::string_view view_id,
    const RgbaBuffer& before,
    const RgbaBuffer& after,
    const std::filesystem::path& diff_png_out = {},
    const VisualComparisonOptions& options = {});

// Renders all 8 controlled views of a CharacterScene to `out_dir`, writes `captures-metadata.json`, and checks for blank zones.
[[nodiscard]] std::vector<CaptureArtifact> capture_controlled_scene_views(
    const rendering::CharacterScene& scene,
    const std::filesystem::path& out_dir,
    std::uint32_t width = 768,
    std::uint32_t height = 768);

[[nodiscard]] nlohmann::json to_json(const CaptureArtifact& artifact);
[[nodiscard]] nlohmann::json to_json(const VisualComparisonResult& cmp);

}  // namespace sns::visual_qa
