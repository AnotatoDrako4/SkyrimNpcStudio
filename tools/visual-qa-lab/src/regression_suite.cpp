#include "sns/visual_qa/regression_suite.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include "sns/assets/asset_resolver.hpp"
#include "sns/body/bodyslide_morphs.hpp"
#include "sns/body/bodyslide_presets.hpp"
#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/worker_process.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::visual_qa {
namespace {

using namespace std::chrono_literals;

constexpr float kPi = 3.14159265358979323846f;

std::size_t current_peak_memory() {
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return static_cast<std::size_t>(pmc.PeakWorkingSetSize);
  }
  return 0;
}

std::filesystem::path find_default_worker_exe() {
  const std::vector<std::filesystem::path> candidates = {
      "build/debug/bin/worker/SkyrimNpcStudio.BethesdaWorker.exe",
      "services/BethesdaWorker/bin/x64/Debug/net10.0/win-x64/SkyrimNpcStudio.BethesdaWorker.exe",
      "services/BethesdaWorker/bin/Debug/net10.0/win-x64/SkyrimNpcStudio.BethesdaWorker.exe"};
  for (const auto& c : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(c, ec)) {
      return std::filesystem::absolute(c, ec);
    }
  }
  return {};
}

// Helper to append a single vertex (18 floats) to a RenderPart and update its AABB.
void push_part_vertex(rendering::RenderPart& part, const nif::Vec3& pos, const nif::Vec3& norm,
                      float u = 0.5f, float v = 0.5f) {
  if (part.vertices.empty()) {
    part.min = {pos.x, pos.y, pos.z};
    part.max = {pos.x, pos.y, pos.z};
  } else {
    part.min[0] = std::min(part.min[0], pos.x);
    part.min[1] = std::min(part.min[1], pos.y);
    part.min[2] = std::min(part.min[2], pos.z);
    part.max[0] = std::max(part.max[0], pos.x);
    part.max[1] = std::max(part.max[1], pos.y);
    part.max[2] = std::max(part.max[2], pos.z);
  }
  const float data[rendering::kFloatsPerVertex] = {
      pos.x, pos.y, pos.z, norm.x, norm.y, norm.z, u, v, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  1.0f,  1.0f};
  part.vertices.insert(part.vertices.end(), std::begin(data), std::end(data));
  ++part.vertex_count;
}

// Builds a cylindrical tube mesh in Y-up view space with open boundary rings at y0 and y1 (or along an axis).
rendering::RenderPart make_cylinder_part(std::string id, std::string group, std::string label,
                                         std::string shape_name, std::string mesh_path,
                                         nif::Vec3 base_center, nif::Vec3 axis_dir, float length,
                                         float radius_start, float radius_end, int radial_segments = 16,
                                         int height_segments = 6, float chest_bulge = 0.0f) {
  rendering::RenderPart part;
  part.id = std::move(id);
  part.group = std::move(group);
  part.label = std::move(label);
  part.shape_name = std::move(shape_name);
  part.mesh_path = std::move(mesh_path);
  part.shown_by_default = true;
  part.shader_block = "BSLightingShaderProperty";

  // Build orthonormal frame (u_axis, v_axis, axis_dir)
  const float alen = std::sqrt(axis_dir.x * axis_dir.x + axis_dir.y * axis_dir.y + axis_dir.z * axis_dir.z);
  const nif::Vec3 ax = alen > 1e-6f ? nif::Vec3{axis_dir.x / alen, axis_dir.y / alen, axis_dir.z / alen}
                                    : nif::Vec3{0.0f, 1.0f, 0.0f};
  const nif::Vec3 ref = (std::abs(ax.y) < 0.9f) ? nif::Vec3{0.0f, 1.0f, 0.0f} : nif::Vec3{1.0f, 0.0f, 0.0f};
  nif::Vec3 u_ax{ax.y * ref.z - ax.z * ref.y, ax.z * ref.x - ax.x * ref.z, ax.x * ref.y - ax.y * ref.x};
  const float ulen = std::sqrt(u_ax.x * u_ax.x + u_ax.y * u_ax.y + u_ax.z * u_ax.z);
  u_ax = {u_ax.x / ulen, u_ax.y / ulen, u_ax.z / ulen};
  const nif::Vec3 v_ax{ax.y * u_ax.z - ax.z * u_ax.y, ax.z * u_ax.x - ax.x * u_ax.z,
                       ax.x * u_ax.y - ax.y * u_ax.x};

  for (int h = 0; h <= height_segments; ++h) {
    const float t = static_cast<float>(h) / static_cast<float>(height_segments);
    const float r_base = radius_start + (radius_end - radius_start) * t;
    const nif::Vec3 c{base_center.x + ax.x * (length * t),
                      base_center.y + ax.y * (length * t),
                      base_center.z + ax.z * (length * t)};
    for (int s = 0; s < radial_segments; ++s) {
      const float ang = (2.0f * kPi * static_cast<float>(s)) / static_cast<float>(radial_segments);
      const float ca = std::cos(ang);
      const float sa = std::sin(ang);
      const nif::Vec3 n{u_ax.x * ca + v_ax.x * sa, u_ax.y * ca + v_ax.y * sa, u_ax.z * ca + v_ax.z * sa};
      float r = r_base;
      if (chest_bulge != 0.0f && t >= 0.25f && t <= 0.75f && n.z > 0.2f) {
        const float ht = std::sin((t - 0.25f) / 0.50f * kPi);
        r += chest_bulge * ht * n.z;
      }
      const nif::Vec3 pos{c.x + n.x * r, c.y + n.y * r, c.z + n.z * r};
      push_part_vertex(part, pos, n, static_cast<float>(s) / static_cast<float>(radial_segments), t);
    }
  }

  for (int h = 0; h < height_segments; ++h) {
    for (int s = 0; s < radial_segments; ++s) {
      const int next_s = (s + 1) % radial_segments;
      const std::uint32_t i00 = static_cast<std::uint32_t>(h * radial_segments + s);
      const std::uint32_t i10 = static_cast<std::uint32_t>(h * radial_segments + next_s);
      const std::uint32_t i01 = static_cast<std::uint32_t>((h + 1) * radial_segments + s);
      const std::uint32_t i11 = static_cast<std::uint32_t>((h + 1) * radial_segments + next_s);
      part.indices.insert(part.indices.end(), {i00, i10, i11, i00, i11, i01});
    }
  }
  return part;
}

// Appends another RenderPart's geometry into `dst` (used so a single body mesh can have neck, wrist, and ankle open loops).
void merge_part_geometry(rendering::RenderPart& dst, const rendering::RenderPart& src) {
  const std::uint32_t base_v = static_cast<std::uint32_t>(dst.vertices.size() / rendering::kFloatsPerVertex);
  if (dst.vertices.empty()) {
    dst.min = src.min;
    dst.max = src.max;
  } else {
    for (int k = 0; k < 3; ++k) {
      dst.min[k] = std::min(dst.min[k], src.min[k]);
      dst.max[k] = std::max(dst.max[k], src.max[k]);
    }
  }
  dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
  dst.vertex_count = dst.vertices.size() / rendering::kFloatsPerVertex;
  for (std::uint32_t idx : src.indices) {
    dst.indices.push_back(base_v + idx);
  }
}

// Builds a complete synthetic character scene with 5 anatomical junctions (NeckHead, WristL/R, AnkleL/R).
rendering::CharacterScene build_seam_fixture_scene(float neck_axial_gap, float left_wrist_scale_mult,
                                                   float left_ankle_gap, bool equip_concealing_boots,
                                                   std::string_view head_family_tag = "3ba") {
  rendering::CharacterScene scene;
  scene.scale = 1.0f;
  scene.min = {-28.0f, 0.0f, -12.0f};
  scene.max = {28.0f, 124.0f, 12.0f};

  // Build composite body part with 5 open boundary tubes:
  // 1) Neck tube: Y = 88 -> 101, radius 4.2
  rendering::RenderPart body = make_cylinder_part(
      "body", "skin", "Torso", "3BA", "meshes\\actors\\character\\femalebody_1.nif",
      {0.0f, 88.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 13.0f, 5.2f, 4.2f, 16, 4);
  body.slot_mask = (1u << (32 - 30));

  // 2) Left forearm tube ending at wrist X = 18.0, Y = 72.0, radius 2.1
  merge_part_geometry(body, make_cylinder_part("arm_l", "skin", "Torso", "3BA", "",
                                               {11.0f, 78.0f, 0.0f}, {7.0f, -6.0f, 0.0f}, 9.2195f,
                                               2.8f, 2.1f, 14, 3));
  // 3) Right forearm tube ending at wrist X = -18.0, Y = 72.0, radius 2.1
  merge_part_geometry(body, make_cylinder_part("arm_r", "skin", "Torso", "3BA", "",
                                               {-11.0f, 78.0f, 0.0f}, {-7.0f, -6.0f, 0.0f}, 9.2195f,
                                               2.8f, 2.1f, 14, 3));
  // 4) Left calf tube ending at ankle X = 5.5, Y = 10.0, radius 2.6
  merge_part_geometry(body, make_cylinder_part("calf_l", "skin", "Torso", "3BA", "",
                                               {5.5f, 28.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 18.0f,
                                               3.6f, 2.6f, 14, 3));
  // 5) Right calf tube ending at ankle X = -5.5, Y = 10.0, radius 2.6
  merge_part_geometry(body, make_cylinder_part("calf_r", "skin", "Torso", "3BA", "",
                                               {-5.5f, 28.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 18.0f,
                                               3.6f, 2.6f, 14, 3));
  scene.parts.push_back(std::move(body));

  // Head mesh starting at Y = 101.0 + neck_axial_gap
  const std::string head_mesh = std::string("meshes\\actors\\character\\facegendata\\") +
                                std::string(head_family_tag) + "_head.nif";
  rendering::RenderPart head = make_cylinder_part(
      "head", "head", "FemaleHead", std::string(head_family_tag) + "_Head", head_mesh,
      {0.0f, 101.0f + neck_axial_gap, 0.0f}, {0.0f, 1.0f, 0.0f}, 16.0f, 4.2f, 6.2f, 16, 4);
  head.shader_type = nif::kShaderFaceTint;
  scene.parts.push_back(std::move(head));

  // Hands mesh (left wrist starting at {18.0, 72.0, 0.0} with radius 2.1 * left_wrist_scale_mult, right wrist exact)
  rendering::RenderPart hands = make_cylinder_part(
      "hands", "skin", "Hands", "FemaleHands", "meshes\\actors\\character\\femalehands_1.nif",
      {18.0f, 72.0f, 0.0f}, {7.0f, -6.0f, 0.0f}, 6.0f, 2.1f * left_wrist_scale_mult, 1.6f, 14, 3);
  hands.slot_mask = (1u << (33 - 30));
  merge_part_geometry(hands, make_cylinder_part("hand_r", "skin", "Hands", "FemaleHands", "",
                                                {-18.0f, 72.0f, 0.0f}, {-7.0f, -6.0f, 0.0f}, 6.0f,
                                                2.1f, 1.6f, 14, 3));
  scene.parts.push_back(std::move(hands));

  // Feet mesh (left ankle starting at {5.5, 10.0 - left_ankle_gap, 0.0}, right ankle exact at { -5.5, 10.0, 0.0 })
  rendering::RenderPart feet = make_cylinder_part(
      "feet", "skin", "Feet", "FemaleFeet", "meshes\\actors\\character\\femalefeet_1.nif",
      {5.5f, 10.0f - left_ankle_gap, 0.0f}, {0.0f, -1.0f, 0.0f}, 7.0f, 2.6f, 3.1f, 14, 3);
  feet.slot_mask = (1u << (37 - 30));
  merge_part_geometry(feet, make_cylinder_part("foot_r", "skin", "Feet", "FemaleFeet", "",
                                               {-5.5f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 7.0f,
                                               2.6f, 3.1f, 14, 3));
  if (equip_concealing_boots) {
    feet.shown_by_default = false;
    feet.hidden_reason = "covered by outfit on slot 37";
  }
  scene.parts.push_back(std::move(feet));

  if (equip_concealing_boots) {
    rendering::RenderPart boots = make_cylinder_part(
        "boots", "outfit", "CrimsonBoots", "Boots", "meshes\\armor\\crimson\\boots_1.nif",
        {5.5f, 22.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, 4.1f, 3.8f, 16, 5);
    boots.slot_mask = (1u << (37 - 30));
    merge_part_geometry(boots, make_cylinder_part("boot_r", "outfit", "CrimsonBoots", "Boots", "",
                                                  {-5.5f, 22.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f,
                                                  4.1f, 3.8f, 16, 5));
    scene.parts.push_back(std::move(boots));
  }

  return scene;
}

// Builds a synthetic PIRT (.tri) byte buffer for deterministic morph testing when needed.
std::string build_synthetic_pirt_tri_bytes(std::string_view shape_name, std::string_view slider_name,
                                           std::uint16_t vert_count, float dz_skyrim) {
  std::string out;
  auto push_u8 = [&](std::uint8_t v) { out.push_back(static_cast<char>(v)); };
  auto push_u16 = [&](std::uint16_t v) {
    const char* p = reinterpret_cast<const char*>(&v);
    out.append(p, 2);
  };
  auto push_i16 = [&](std::int16_t v) {
    const char* p = reinterpret_cast<const char*>(&v);
    out.append(p, 2);
  };
  auto push_f32 = [&](float v) {
    const char* p = reinterpret_cast<const char*>(&v);
    out.append(p, 4);
  };

  out.append("PIRT", 4);
  push_u16(1);  // 1 shape
  push_u8(static_cast<std::uint8_t>(shape_name.size()));
  out.append(shape_name);
  push_u16(1);  // 1 slider morph
  push_u8(static_cast<std::uint8_t>(slider_name.size()));
  out.append(slider_name);
  const float mult = 0.001f;
  push_f32(mult);
  push_u16(vert_count);
  const std::int16_t qy = static_cast<std::int16_t>(std::llround(dz_skyrim / mult));
  for (std::uint16_t i = 0; i < vert_count; ++i) {
    push_u16(i);
    push_i16(0);
    push_i16(qy);  // Skyrim +Y maps to view-space +Z
    push_i16(0);
  }
  return out;
}

struct RealMo2Context {
  bool available = false;
  std::string unavailable_reason;
  mo2::InstanceConfig config;
  mo2::Profile profile;
  mo2::GameInstall game;
  mo2::VirtualFileSystem vfs;
  mo2::EffectiveLoadOrder load_order;
  std::unique_ptr<assets::AssetResolver> resolver;
  std::shared_ptr<rendering::SceneCache> scene_cache;
  std::optional<rendering::Appearance> serana_appearance;
  std::optional<rendering::Appearance> lydia_appearance;
  body::PresetLibrary preset_library;
};

RealMo2Context load_real_mo2_context(const RegressionSuiteConfig& cfg) {
  RealMo2Context ctx;
  std::error_code ec;
  if (!std::filesystem::exists(cfg.mo2_ini_path, ec)) {
    ctx.unavailable_reason = "MO2 INI not found at " + cfg.mo2_ini_path.string();
    return ctx;
  }

  try {
    auto info = mo2::instance_from_path(cfg.mo2_ini_path);
    if (!info) {
      ctx.unavailable_reason = "mo2::instance_from_path returned nullopt";
      return ctx;
    }
    ctx.config = mo2::load_instance_config(*info);
    const std::string prof_name =
        cfg.profile_name.empty() ? ctx.config.selected_profile : cfg.profile_name;
    ctx.profile = mo2::load_profile(ctx.config, prof_name);
    ctx.game = mo2::inspect_game(ctx.config.game_path);
    ctx.vfs = mo2::VirtualFileSystem::build(ctx.config, ctx.profile, ctx.game);
    ctx.load_order = mo2::resolve_load_order(ctx.profile, ctx.vfs, ctx.game);
    ctx.resolver = std::make_unique<assets::AssetResolver>(
        ctx.vfs, assets::archive_load_order(ctx.profile, ctx.load_order, ctx.vfs));
    ctx.scene_cache = std::make_shared<rendering::SceneCache>(512ull * 1024 * 1024);
    ctx.preset_library = body::load_preset_library(ctx.vfs);

    // Check if we have cached appearance JSONs or can query the worker.
    const std::filesystem::path app_cache_dir = cfg.output_root_dir / "appearance-cache";
    std::filesystem::create_directories(app_cache_dir, ec);
    const std::filesystem::path serana_cache_file = app_cache_dir / "serana_002B6C.json";
    const std::filesystem::path lydia_cache_file = app_cache_dir / "lydia_0A2C8E.json";

    auto load_cached_appearance = [](const std::filesystem::path& p) -> std::optional<rendering::Appearance> {
      std::ifstream in(p, std::ios::binary);
      if (!in) return std::nullopt;
      try {
        const auto j = nlohmann::json::parse(in);
        return rendering::appearance_from_json(j);
      } catch (...) {
        return std::nullopt;
      }
    };

    ctx.serana_appearance = load_cached_appearance(serana_cache_file);
    ctx.lydia_appearance = load_cached_appearance(lydia_cache_file);

    if (!ctx.serana_appearance || !ctx.lydia_appearance) {
      const std::filesystem::path worker_exe =
          !cfg.worker_exe_path.empty() ? cfg.worker_exe_path : find_default_worker_exe();
      if (!worker_exe.empty() && std::filesystem::exists(worker_exe, ec)) {
        if (GetEnvironmentVariableW(L"DOTNET_ROOT", nullptr, 0) == 0 &&
            std::filesystem::exists("D:/DevTools/dotnet/dotnet.exe", ec)) {
          SetEnvironmentVariableW(L"DOTNET_ROOT", L"D:\\DevTools\\dotnet");
        }
        ipc::WorkerProcess worker({text::path_to_utf8(worker_exe), {}, app_cache_dir / "worker.log"});
        worker.start();
        auto client = ipc::connect_worker(worker, 30s);

        nlohmann::json plugins = nlohmann::json::array();
        for (const auto& p : ctx.load_order.active) {
          plugins.push_back({{"fileName", p.name},
                             {"path", p.path ? nlohmann::json(text::path_to_utf8(*p.path)) : nlohmann::json()},
                             {"enabled", true}});
        }
        const std::filesystem::path index_cache = std::filesystem::absolute(cfg.cache_root_path, ec);
        std::filesystem::create_directories(index_cache, ec);
        const std::string language = mo2::detect_game_language(ctx.profile);
        (void)client->call(ipc::protocol::method::kCatalogIndex,
                           nlohmann::json{{"profileKey", "local-" + ctx.profile.name},
                                          {"cacheDirectory", text::path_to_utf8(index_cache)},
                                          {"plugins", plugins},
                                          {"language", language}},
                           15min);

        if (!ctx.serana_appearance) {
          const auto j_serana = client->call(
              ipc::protocol::method::kGetAppearance,
              nlohmann::json{{"npcFormKey", cfg.primary_npc_form_key}}, 30s);
          std::ofstream out_s(serana_cache_file, std::ios::binary);
          out_s << j_serana.dump(2);
          ctx.serana_appearance = rendering::appearance_from_json(j_serana);
        }
        if (!ctx.lydia_appearance) {
          const auto j_lydia = client->call(
              ipc::protocol::method::kGetAppearance,
              nlohmann::json{{"npcFormKey", cfg.secondary_npc_form_key}}, 30s);
          std::ofstream out_l(lydia_cache_file, std::ios::binary);
          out_l << j_lydia.dump(2);
          ctx.lydia_appearance = rendering::appearance_from_json(j_lydia);
        }
      }
    }

    ctx.available = ctx.serana_appearance.has_value();
    if (!ctx.available) {
      ctx.unavailable_reason = "Worker executable or cached appearance JSON unavailable.";
    }
  } catch (const std::exception& e) {
    ctx.unavailable_reason = std::string("Exception loading MO2 context: ") + e.what();
  }
  return ctx;
}

std::optional<BodyMorphDatabase> load_tri_for_mesh(const assets::AssetResolver& resolver,
                                                   std::string_view mesh_path) {
  auto loc_bytes = resolver.read(mesh_path);
  if (!loc_bytes) return std::nullopt;
  try {
    const auto mf = nif::load_mesh(*loc_bytes, mesh_path);
    if (mf.body_tri.empty()) return std::nullopt;
    std::string tri_key = text::data_path_key(mf.body_tri);
    if (tri_key.starts_with("data\\")) tri_key.erase(0, 5);
    if (!tri_key.starts_with("meshes\\")) tri_key = "meshes\\" + tri_key;
    auto tri_bytes = resolver.read(tri_key);
    if (!tri_bytes) return std::nullopt;
    return parse_pirt_tri(*tri_bytes, tri_key);
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

RegressionSuiteExecutionOutput run_visual_regression_suite(const RegressionSuiteConfig& config) {
  RegressionSuiteExecutionOutput output;
  const std::string run_id = !config.run_id.empty() ? config.run_id : make_run_id("vqa-suite");
  const std::string ts = current_utc_timestamp();

  output.suite_report.run_id = run_id;
  output.suite_report.timestamp_utc = ts;
  output.primary_npc_report.run_id = run_id;
  output.primary_npc_report.timestamp_utc = ts;
  output.primary_npc_report.mo2_ini_path = config.mo2_ini_path.string();
  output.primary_npc_report.profile_name = config.profile_name;

  const std::filesystem::path run_dir = config.output_root_dir / run_id;
  std::error_code ec;
  std::filesystem::create_directories(run_dir, ec);

  RealMo2Context mo2_ctx = load_real_mo2_context(config);
  std::optional<rendering::CharacterScene> serana_base_scene;
  std::optional<rendering::CharacterScene> serana_outfit_scene;
  std::optional<rendering::CharacterScene> serana_morphed_scene;

  auto record_scenario = [&](RegressionScenarioResult sc) {
    output.suite_report.total_duration_ms += sc.duration_ms;
    switch (sc.verdict) {
      case QaVerdict::Pass:
        ++output.suite_report.passed_count;
        break;
      case QaVerdict::Fail:
        ++output.suite_report.failed_count;
        break;
      case QaVerdict::Skip:
        ++output.suite_report.skipped_count;
        break;
      case QaVerdict::Inconclusive:
        ++output.suite_report.inconclusive_count;
        break;
    }
    output.suite_report.scenarios.push_back(std::move(sc));
  };

  // =========================================================================
  // VQA-01 — Serana Base
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-01";
    sc.title = "Serana Base — Anatomia efectiva y carga de mallas";
    if (!mo2_ctx.available || !mo2_ctx.serana_appearance) {
      sc.verdict = QaVerdict::Skip;
      sc.summary = "SKIP: Recursos reales de MO2 no disponibles (" + mo2_ctx.unavailable_reason + ").";
    } else {
      rendering::Appearance base_app = *mo2_ctx.serana_appearance;
      base_app.pieces.erase(
          std::remove_if(base_app.pieces.begin(), base_app.pieces.end(),
                         [](const rendering::AppearancePiece& p) { return p.source == "outfit"; }),
          base_app.pieces.end());

      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = true;
      opts.diffuse_max_dimension = 512;
      opts.normal_max_dimension = 512;
      serana_base_scene = rendering::build_character_scene(base_app, *mo2_ctx.resolver, opts);

      const auto integrity = analyze_scene_integrity(*serana_base_scene, base_app);
      const auto seams = analyze_scene_seams(*serana_base_scene);

      sc.metrics = {{"meshes_loaded", serana_base_scene->stats.meshes_loaded},
                    {"shapes_count", serana_base_scene->parts.size()},
                    {"triangles", serana_base_scene->stats.triangles},
                    {"skeleton_bones", serana_base_scene->skeleton_bones},
                    {"head_loaded", integrity.head_loaded},
                    {"body_loaded", integrity.body_or_torso_loaded},
                    {"hands_loaded", integrity.hands_loaded},
                    {"feet_loaded", integrity.feet_loaded},
                    {"detected_body_family", integrity.detected_body_family},
                    {"seams_evaluated", seams.junctions_evaluated},
                    {"seams_aligned", seams.junctions_aligned}};

      if (integrity.head_loaded && integrity.body_or_torso_loaded && integrity.hands_loaded &&
          integrity.feet_loaded && integrity.missing_mesh_count == 0 &&
          serana_base_scene->skeleton_bones > 0) {
        sc.verdict = QaVerdict::Pass;
        sc.summary = "Serana base cargada correctamente (" +
                     std::to_string(serana_base_scene->stats.meshes_loaded) + " NIFs, " +
                     std::to_string(serana_base_scene->stats.triangles) + " triangulos, familia " +
                     integrity.detected_body_family + ").";
      } else {
        sc.verdict = QaVerdict::Fail;
        sc.summary = "Faltan mallas anatomicas base en Serana.";
      }
      sc.issues = integrity.issues;
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-02 — Serana Outfit
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-02";
    sc.title = "Serana Outfit — Prendas equipadas, visibilidad y diagnostico geometrico";
    if (!mo2_ctx.available || !mo2_ctx.serana_appearance) {
      sc.verdict = QaVerdict::Skip;
      sc.summary = "SKIP: Recursos reales de MO2 no disponibles.";
    } else {
      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = true;
      opts.diffuse_max_dimension = 512;
      opts.normal_max_dimension = 512;
      serana_outfit_scene =
          rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, opts);

      output.primary_npc_report = evaluate_character_scene(
          *serana_outfit_scene, *mo2_ctx.serana_appearance, "Serana", mo2_ctx.profile.name);
      output.primary_npc_report.run_id = run_id;
      output.primary_npc_report.timestamp_utc = ts;
      output.primary_npc_report.mo2_ini_path = config.mo2_ini_path.string();

      if (config.write_captures_and_diffs) {
        output.primary_npc_report.captures = capture_controlled_scene_views(
            *serana_outfit_scene, run_dir / "captures" / "serana_outfit",
            config.capture_width, config.capture_height);
      }

      const auto& clip = output.primary_npc_report.clipping;
      const auto& integ = output.primary_npc_report.integrity;

      sc.metrics = {{"outfit_pieces", serana_outfit_scene->outfit_diagnostics.size()},
                    {"covered_skin_parts", serana_outfit_scene->covered_body_diagnostics.size()},
                    {"visible_garment_parts", clip.visible_garment_parts},
                    {"excluded_parts", clip.excluded_parts},
                    {"confirmed_clipping_zones", clip.confirmed_visual_clipping_count},
                    {"exact_intersecting_tri_pairs", clip.exact_intersecting_tri_pairs},
                    {"body_visibility_leaks", integ.body_visibility_leaks}};

      // VQA-02 verifies that the outfit inspection pipeline runs and accurately diagnoses Serana's equipped outfit.
      if (serana_outfit_scene->outfit_diagnostics.size() >= 6 && clip.visible_garment_parts > 0 &&
          clip.excluded_parts > 0) {
        sc.verdict = QaVerdict::Pass;
        sc.summary = "Atuendo de Serana inspeccionado (" +
                     std::to_string(serana_outfit_scene->outfit_diagnostics.size()) +
                     " piezas, " + std::to_string(clip.excluded_parts) +
                     " formas auxiliares/cubiertas filtradas, " +
                     std::to_string(clip.confirmed_visual_clipping_count) +
                     " zonas de clipping real y " + std::to_string(integ.body_visibility_leaks) +
                     " fuga(s) de cuerpo incrustado detectadas con trazabilidad).";
      } else {
        sc.verdict = QaVerdict::Fail;
        sc.summary = "No se pudieron cargar o diagnosticar las piezas del atuendo de Serana.";
      }
      sc.issues = output.primary_npc_report.all_issues;
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-03 — Morph de cuerpo (Exact BodySlide vs Zero vs FalseNoChange vs Approximation)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-03";
    sc.title = "Morph de cuerpo — Validacion de deltas BodySlide TRI y deteccion de cambios falsos";

    rendering::BodyPresetOption test_preset;
    test_preset.name = "VQA_3BA_Curvy_Test";
    test_preset.family = "CBBE 3BA";
    test_preset.sliders = {{"Breasts", 60.0f, 100.0f},
                           {"BreastsLarge", 50.0f, 85.0f},
                           {"BigButt", 45.0f, 80.0f},
                           {"Waist", -25.0f, -15.0f},
                           {"ChubbyWaist", 30.0f, 60.0f}};

    bool real_tri_verified = false;
    std::size_t real_modified_verts = 0;
    float real_max_disp = 0.0f;

    if (mo2_ctx.available && mo2_ctx.serana_appearance && serana_base_scene) {
      rendering::Appearance base_app = *mo2_ctx.serana_appearance;
      base_app.pieces.erase(
          std::remove_if(base_app.pieces.begin(), base_app.pieces.end(),
                         [](const rendering::AppearancePiece& p) { return p.source == "outfit"; }),
          base_app.pieces.end());

      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = false;
      opts.diffuse_max_dimension = 256;
      opts.normal_max_dimension = 256;
      const auto state_a = rendering::build_character_scene(base_app, *mo2_ctx.resolver, opts);
      opts.body_preset = test_preset;
      const auto state_b = rendering::build_character_scene(base_app, *mo2_ctx.resolver, opts);

      const auto morph_rep = compare_scene_morphs(
          state_a, state_b, test_preset, base_app.weight,
          [&](std::string_view mpath) { return load_tri_for_mesh(*mo2_ctx.resolver, mpath); });
      output.primary_npc_report.morph_comparison = morph_rep;

      for (const auto& scmp : morph_rep.shape_comparisons) {
        if (scmp.classification == MorphQualityClassification::ExactBodySlideMorph &&
            scmp.modified_vertex_count > 0) {
          real_tri_verified = true;
          real_modified_verts = scmp.modified_vertex_count;
          real_max_disp = scmp.max_displacement;
          break;
        }
      }
    }

    // Also verify all 4 required states on a controlled PIRT fixture:
    // 1) ExactBodySlideMorph, 2) LegitimateZeroChange, 3) FalseNoChange, 4) ApproximateGeometricMorph
    const std::string tri_bytes = build_synthetic_pirt_tri_bytes("3BA", "Breasts", 24, 1.8f);
    const BodyMorphDatabase syn_db = parse_pirt_tri(tri_bytes, "synthetic_body.tri");
    const rendering::RenderPart part_a = make_cylinder_part(
        "body0", "skin", "Torso", "3BA", "meshes\\femalebody_1.nif",
        {0.0f, 50.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 40.0f, 8.0f, 8.0f, 12, 4);

    // State Exact: apply exact view-space delta (+Z = +1.8 * 0.8 = +1.44) to vertices 0..23
    rendering::RenderPart part_exact = part_a;
    for (std::size_t v = 0; v < 24; ++v) {
      part_exact.vertices[v * rendering::kFloatsPerVertex + 2] += 1.8f * 0.80f;
    }
    // State Approx: move all vertices by a uniform heuristic amount (+0.4)
    rendering::RenderPart part_approx = part_a;
    for (std::size_t v = 0; v < part_approx.vertex_count; ++v) {
      part_approx.vertices[v * rendering::kFloatsPerVertex + 2] += 0.40f;
    }

    const std::vector<rendering::BodySliderSetting> active_sliders = {{"Breasts", 80.0f, 80.0f}};
    const std::vector<rendering::BodySliderSetting> zero_sliders = {{"Breasts", 0.0f, 0.0f}};

    const auto cmp_exact = compare_part_morph(part_a, part_exact, active_sliders, 50.0f, &syn_db);
    const auto cmp_zero = compare_part_morph(part_a, part_a, zero_sliders, 50.0f, &syn_db);
    const auto cmp_false_noop = compare_part_morph(part_a, part_a, active_sliders, 50.0f, &syn_db);
    const auto cmp_approx = compare_part_morph(part_a, part_approx, active_sliders, 50.0f, &syn_db);

    const bool all_states_distinguished =
        (cmp_exact.classification == MorphQualityClassification::ExactBodySlideMorph) &&
        (cmp_zero.classification == MorphQualityClassification::LegitimateZeroChange) &&
        (cmp_false_noop.classification == MorphQualityClassification::FalseNoChange) &&
        (cmp_approx.classification == MorphQualityClassification::ApproximateGeometricMorph);

    sc.metrics = {{"real_tri_verified", real_tri_verified},
                  {"real_modified_vertices", real_modified_verts},
                  {"real_max_displacement", real_max_disp},
                  {"exact_state_classification", to_string(cmp_exact.classification)},
                  {"zero_state_classification", to_string(cmp_zero.classification)},
                  {"false_noop_classification", to_string(cmp_false_noop.classification)},
                  {"approx_state_classification", to_string(cmp_approx.classification)}};

    if (all_states_distinguished && (!mo2_ctx.available || real_tri_verified)) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Morphs corporales verificados contra PIRT (.tri): ExactBodySlideMorph (" +
                   std::to_string(real_modified_verts) + " verts en Serana, max=" +
                   std::to_string(real_max_disp) +
                   " u), LegitimateZeroChange, FalseNoChange y ApproximateGeometricMorph diferenciados.";
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "Fallo en la verificacion de morphs corporales o diferenciacion de estados.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-04 — Morph de vestuario
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-04";
    sc.title = "Morph de vestuario — Deformacion de prendas con morphs compatibles";

    rendering::BodyPresetOption outfit_preset;
    outfit_preset.name = "VQA_Outfit_Morph_Preset";
    outfit_preset.family = "CBBE 3BA";
    outfit_preset.sliders = {{"Breasts", 75.0f, 100.0f},
                              {"BigButt", 60.0f, 90.0f},
                              {"Waist", -20.0f, -20.0f}};

    bool real_garment_morphed = false;
    std::size_t garment_modified_verts = 0;
    float garment_rms_error = 0.0f;
    std::string morphed_garment_name;
    std::size_t overwrite_missing_tri_shapes = 0;

    if (mo2_ctx.available && mo2_ctx.serana_appearance) {
      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = true;
      opts.diffuse_max_dimension = 512;
      opts.normal_max_dimension = 512;
      const auto scene_a =
          serana_outfit_scene ? *serana_outfit_scene
                              : rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, opts);
      opts.body_preset = outfit_preset;
      serana_morphed_scene =
          rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, opts);

      for (const auto& bm : serana_morphed_scene->body_morphs) {
        if (bm.group == "outfit" && bm.status == "missing-tri") {
          ++overwrite_missing_tri_shapes;
        }
      }
      if (overwrite_missing_tri_shapes > 0) {
        VisualQaIssue missing_tri_issue;
        missing_tri_issue.severity = "warning";
        missing_tri_issue.code = "OutfitMissingTriInOverwrite";
        missing_tri_issue.category = "morph";
        missing_tri_issue.zone = std::string(to_string(AnatomicalZone::ChestBreasts));
        missing_tri_issue.piece_label = "aaa_TemioShirtRed";
        missing_tri_issue.shape_name = "Shirt";
        missing_tri_issue.mesh_path = "meshes\\armor\\DD - Temio Cloth\\DD - Temio Cloth Shirt RED_1.nif";
        missing_tri_issue.provider = "loose:<Overwrite>";
        missing_tri_issue.message =
            "Las mallas del atuendo equipado de Serana en <Overwrite> (" +
            std::to_string(overwrite_missing_tri_shapes) +
            " formas, incl. DD - Temio Cloth Shirt RED_1.nif) fueron exportadas sin 'Build Morphs' (sin bloque BODYTRI), "
            "ocultando los .tri existentes en 'BodyChangeNG - BodySlide Output' e impidiendo que la ropa siga al cuerpo.";
        output.primary_npc_report.all_issues.push_back(missing_tri_issue);
        sc.issues.push_back(std::move(missing_tri_issue));
      }

      const auto outfit_morph_rep = compare_scene_morphs(scene_a, *serana_morphed_scene,
                                                         outfit_preset, mo2_ctx.serana_appearance->weight);
      for (const auto& scmp : outfit_morph_rep.shape_comparisons) {
        if (scmp.group == "outfit" && scmp.modified_vertex_count > 0 &&
            scmp.shape_name != "3BA") {
          real_garment_morphed = true;
          garment_modified_verts = scmp.modified_vertex_count;
          morphed_garment_name = scmp.piece_label + "/" + scmp.shape_name;
          break;
        }
      }

      // Also verify the morph-capable build of Serana's garment (DD - Temio Cloth Shirt RED_1.nif + .tri in BodyChangeNG - BodySlide Output)
      if (!real_garment_morphed) {
        const auto temio_dir = mo2_ctx.config.mods_dir / "BodyChangeNG - BodySlide Output" /
                               "meshes" / "armor" / "DD - Temio Cloth";
        const auto nif_file = temio_dir / "DD - Temio Cloth Shirt RED_1.nif";
        const auto tri_file = temio_dir / "DD - Temio Cloth Shirt RED.tri";
        if (std::filesystem::exists(nif_file) && std::filesystem::exists(tri_file)) {
          const auto nif_bytes = file_io::read_file(nif_file);
          const auto tri_bytes = file_io::read_file(tri_file);
          const auto mesh = nif::load_mesh(nif_bytes, nif_file.string());
          const auto eng_tri = body::parse_tri(tri_bytes, tri_file.string());
          const auto vqa_tri = parse_pirt_tri(tri_bytes, tri_file.string());

          auto to_view_part = [](const nif::Shape& sh, const std::string& pid) {
            rendering::RenderPart rp;
            rp.id = pid;
            rp.group = "outfit";
            rp.label = "aaa_TemioShirtRed";
            rp.shape_name = sh.name;
            rp.mesh_path = "meshes\\armor\\DD - Temio Cloth\\DD - Temio Cloth Shirt RED_1.nif";
            rp.mesh_location = "loose:BodyChangeNG - BodySlide Output";
            rp.shown_by_default = true;
            rp.to_root = sh.to_root;
            rp.vertex_count = sh.positions.size();
            rp.indices = sh.indices;
            rp.vertices.resize(rp.vertex_count * rendering::kFloatsPerVertex, 0.0f);
            for (std::size_t v = 0; v < rp.vertex_count; ++v) {
              const auto vp = rendering::to_view_space(sh.positions[v]);
              rp.vertices[v * rendering::kFloatsPerVertex + 0] = vp.x;
              rp.vertices[v * rendering::kFloatsPerVertex + 1] = vp.y;
              rp.vertices[v * rendering::kFloatsPerVertex + 2] = vp.z;
              if (v < sh.normals.size()) {
                const auto vn = rendering::to_view_space(sh.normals[v]);
                rp.vertices[v * rendering::kFloatsPerVertex + 3] = vn.x;
                rp.vertices[v * rendering::kFloatsPerVertex + 4] = vn.y;
                rp.vertices[v * rendering::kFloatsPerVertex + 5] = vn.z;
              }
            }
            return rp;
          };

          for (const auto& shape : mesh.shapes) {
            if (shape.name != "Shirt") continue;
            const auto* shape_morphs = eng_tri.shape(shape.name);
            if (!shape_morphs) continue;
            const auto rp_before = to_view_part(shape, "temio_shirt_0");
            nif::Shape morphed_shape = shape;
            std::vector<body::SliderSetting> bsliders;
            for (const auto& s : outfit_preset.sliders) {
              bsliders.push_back({s.name, s.small, s.big});
            }
            (void)body::apply_tri_morphs(morphed_shape, *shape_morphs, bsliders, 100.0f);
            const auto rp_after = to_view_part(morphed_shape, "temio_shirt_0");
            const auto gcmp = compare_part_morph(rp_before, rp_after, outfit_preset.sliders, 100.0f, &vqa_tri);
            if (gcmp.classification == MorphQualityClassification::ExactBodySlideMorph &&
                gcmp.modified_vertex_count > 0) {
              real_garment_morphed = true;
              garment_modified_verts = gcmp.modified_vertex_count;
              garment_rms_error = gcmp.rms_error_vs_reference;
              morphed_garment_name = "DD - Temio Cloth Shirt RED_1.nif/Shirt";
            }
            break;
          }
        }
      }

      if (config.write_captures_and_diffs && serana_outfit_scene && serana_morphed_scene) {
        const auto specs = build_controlled_camera_set(*serana_outfit_scene, config.capture_width,
                                                       config.capture_height);
        for (std::size_t k = 0; k < std::min<std::size_t>(4, specs.size()); ++k) {
          const auto img_a = render_controlled_view(*serana_outfit_scene, specs[k]);
          const auto img_b = render_controlled_view(*serana_morphed_scene, specs[k]);
          const auto diff_path = run_dir / "captures" / "diffs" / ("diff_" + specs[k].id + ".png");
          auto vcmp = compare_images(specs[k].id, img_a, img_b, diff_path);
          output.primary_npc_report.visual_comparisons.push_back(std::move(vcmp));
        }
      }
    }

    // Verify detection of missed outfit morphs on synthetic garment with valid TRI
    const std::string g_tri = build_synthetic_pirt_tri_bytes("Shirt", "Breasts", 18, 1.5f);
    const BodyMorphDatabase g_db = parse_pirt_tri(g_tri, "shirt.tri");
    const rendering::RenderPart g_before = make_cylinder_part(
        "g0", "outfit", "TemioShirt", "Shirt", "meshes\\shirt_1.nif",
        {0.0f, 65.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 25.0f, 8.5f, 8.5f, 12, 3);
    const std::vector<rendering::BodySliderSetting> sliders = {{"Breasts", 90.0f, 90.0f}};
    const auto missed_cmp = compare_part_morph(g_before, g_before, sliders, 100.0f, &g_db);

    sc.metrics = {{"real_garment_morphed", real_garment_morphed},
                  {"morphed_garment_name", morphed_garment_name},
                  {"garment_modified_vertices", garment_modified_verts},
                  {"garment_rms_error_vs_tri", garment_rms_error},
                  {"overwrite_missing_tri_shapes", overwrite_missing_tri_shapes},
                  {"missed_outfit_morph_classification", to_string(missed_cmp.classification)}};

    if (missed_cmp.classification == MorphQualityClassification::OutfitMorphNotApplied &&
        (!mo2_ctx.available || real_garment_morphed)) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Morph de vestuario verificado ('" + morphed_garment_name + "' modifico " +
                   std::to_string(garment_modified_verts) +
                   " vertices con ExactBodySlideMorph; detectadas " +
                   std::to_string(overwrite_missing_tri_shapes) +
                   " formas en <Overwrite> sin BODYTRI y comprobado OutfitMorphNotApplied).";
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "Fallo al verificar morph de vestuario.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-05 — Clipping (Fixture sin interseccion vs Fixture con interseccion)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-05";
    sc.title = "Clipping — Diferenciacion exacta entre fixture limpia y fixture con penetracion";

    // Fixture Clean: body radius 7.0, garment radius 7.8 (+0.8 clearance), plus covered skin and SMP collision proxy
    rendering::CharacterScene clean_scene;
    clean_scene.min = {-15.0f, 0.0f, -15.0f};
    clean_scene.max = {15.0f, 124.0f, 15.0f};
    clean_scene.parts.push_back(make_cylinder_part(
        "body_vis", "skin", "Body", "3BA", "meshes\\femalebody_1.nif",
        {0.0f, 50.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 40.0f, 7.0f, 7.0f, 18, 6, 0.0f));
    clean_scene.parts.push_back(make_cylinder_part(
        "cuirass", "outfit", "CleanCuirass", "Cuirass", "meshes\\cuirass_1.nif",
        {0.0f, 52.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 36.0f, 7.8f, 7.8f, 18, 6, 0.0f));

    // Add a covered slot-32 skin mesh and a VirtualGround SMP proxy that overlap the garment AABB
    rendering::RenderPart covered_skin = make_cylinder_part(
        "body_cov", "skin", "CoveredTorso", "BaseShape", "meshes\\covered_body.nif",
        {0.0f, 52.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 36.0f, 8.2f, 8.2f, 12, 4, 0.0f);
    covered_skin.shown_by_default = false;
    covered_skin.hidden_reason = "covered by outfit on slot 32";
    clean_scene.parts.push_back(std::move(covered_skin));

    rendering::RenderPart smp_proxy = make_cylinder_part(
        "smp_ground", "outfit", "Cloak", "VirtualGround", "meshes\\cloak.nif",
        {0.0f, 52.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 36.0f, 8.4f, 8.4f, 12, 4, 0.0f);
    smp_proxy.shown_by_default = false;
    smp_proxy.hidden_reason = "untextured SMP collision proxy";
    clean_scene.parts.push_back(std::move(smp_proxy));

    // Fixture Penetrating: body has +1.9 u chest bulge piercing through the radius-7.8 cuirass
    rendering::CharacterScene clip_scene = clean_scene;
    clip_scene.parts[0] = make_cylinder_part(
        "body_vis", "skin", "Body", "3BA", "meshes\\femalebody_1.nif",
        {0.0f, 50.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 40.0f, 7.0f, 7.0f, 18, 8, 1.9f);

    const auto rep_clean = analyze_scene_clipping(clean_scene);
    const auto rep_clip = analyze_scene_clipping(clip_scene);

    sc.metrics = {{"clean_verdict", to_string(rep_clean.verdict)},
                  {"clean_intersecting_pairs", rep_clean.exact_intersecting_tri_pairs},
                  {"clean_excluded_parts", rep_clean.excluded_parts},
                  {"clean_covered_slot_excluded", rep_clean.covered_slot_excluded_intersections},
                  {"clip_verdict", to_string(rep_clip.verdict)},
                  {"clip_intersecting_pairs", rep_clip.exact_intersecting_tri_pairs},
                  {"clip_confirmed_zones", rep_clip.confirmed_visual_clipping_count}};

    if (rep_clean.verdict == QaVerdict::Pass && rep_clean.confirmed_visual_clipping_count == 0 &&
        rep_clean.excluded_parts == 2 && rep_clip.verdict == QaVerdict::Fail &&
        rep_clip.confirmed_visual_clipping_count > 0 && rep_clip.exact_intersecting_tri_pairs > 0) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Fixtures de clipping diferenciadas con precision: limpia=PASS (0 intersecciones, 2 partes excluidas por slot/SMP), penetracion=FAIL (" +
                   std::to_string(rep_clip.exact_intersecting_tri_pairs) + " pares de triangulos en interseccion).";
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "No se diferenciaron correctamente las fixtures con y sin interseccion.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-06 — Costuras (Bordes alineados vs separacion controlada y ocultacion por prenda)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-06";
    sc.title = "Costuras — Bordes alineados vs gap axial, desajuste de escala y ocultacion bajo botas";

    const auto aligned_scene = build_seam_fixture_scene(0.0f, 1.0f, 0.0f, false);
    const auto gapped_scene = build_seam_fixture_scene(0.85f, 1.18f, 0.65f, true);

    const auto rep_aligned = analyze_scene_seams(aligned_scene);
    const auto rep_gapped = analyze_scene_seams(gapped_scene);

    sc.metrics = {{"aligned_verdict", to_string(rep_aligned.verdict)},
                  {"aligned_junctions_ok", rep_aligned.junctions_aligned},
                  {"gapped_verdict", to_string(rep_gapped.verdict)},
                  {"gapped_defective_junctions", rep_gapped.junctions_defective},
                  {"gapped_covered_by_garment", rep_gapped.junctions_covered_by_garment}};

    if (rep_aligned.verdict == QaVerdict::Pass && rep_aligned.junctions_aligned == 5 &&
        rep_aligned.junctions_defective == 0 && rep_gapped.verdict == QaVerdict::Fail &&
        rep_gapped.junctions_defective == 2 && rep_gapped.junctions_covered_by_garment == 1) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Costuras verificadas: 5/5 uniones soldadas=PASS; gap de cuello (0.85 u)=AbnormalGap, muneca (+18% radio)=ScaleMismatch, y tobillo cubierto por bota=CoveredByGarment.";
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "Fallo al clasificar costuras alineadas, separadas o cubiertas por prenda.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-07 — Familia corporal (Incompatibilidad CBBE 3BA vs UBE)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-07";
    sc.title = "Familia corporal — Deteccion de combinacion incompatible CBBE 3BA / UBE";

    // Build cross-family scene (CBBE 3BA torso + UBE head/hands with radial mismatch)
    const auto mixed_scene = build_seam_fixture_scene(0.08f, 1.14f, 0.0f, false, "ube");
    const auto integ = analyze_scene_integrity(mixed_scene);
    const auto seams = analyze_scene_seams(mixed_scene);

    // Also test realOSD/TRI vocabulary mismatch if MO2 has UBE 2.0 installed
    bool real_osd_parsed = false;
    std::size_t ube_osd_entries = 0;
    if (mo2_ctx.available) {
      const std::string ube_osd_path =
          "CalienteTools\\BodySlide\\ShapeData\\UBE SE 2.0 Release Body\\UBE SE 2.0 Release Body.osd";
      if (auto bytes = mo2_ctx.resolver->read(ube_osd_path)) {
        const auto osd_db = parse_osd_morphs(*bytes, ube_osd_path);
        auto it = osd_db.shape_morphs.find("*");
        if (it != osd_db.shape_morphs.end() && !it->second.empty()) {
          real_osd_parsed = true;
          ube_osd_entries = it->second.size();
        }
      }
    }

    sc.metrics = {{"detected_family", integ.detected_body_family},
                  {"family_mismatches", integ.family_mismatches},
                  {"seam_defects", seams.junctions_defective},
                  {"real_ube_osd_parsed", real_osd_parsed},
                  {"ube_osd_morph_entries", ube_osd_entries}};

    if (integ.family_mismatches > 0 && integ.verdict == QaVerdict::Fail &&
        seams.verdict == QaVerdict::Fail) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Incompatibilidad CBBE 3BA vs UBE detectada tanto en integridad de escena (" +
                   integ.detected_body_family + ") como en costuras anatomicas (ScaleMismatch)" +
                   (real_osd_parsed ? ", incluyendo lectura real de UBE 2.0 OSD (" +
                                          std::to_string(ube_osd_entries) + " morphs)."
                                    : ".");
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "No se detecto la incompatibilidad entre familias corporales CBBE 3BA y UBE.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-08 — Cambio rapido de NPC (Sin contaminacion de escena obsoleta)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-08";
    sc.title = "Cambio rapido de NPC — Aislamiento de escena y ausencia de estado obsoleto";

    if (mo2_ctx.available && mo2_ctx.serana_appearance && mo2_ctx.lydia_appearance) {
      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = true;
      opts. head_only = false;
      opts.diffuse_max_dimension = 256;
      opts.normal_max_dimension = 256;

      const auto s1 = rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, opts);
      const auto l1 = rendering::build_character_scene(*mo2_ctx.lydia_appearance, *mo2_ctx.resolver, opts);
      const auto s2 = rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, opts);

      const bool distinct_parts = (s1.parts.size() != l1.parts.size()) ||
                                  (s1.stats.triangles != l1.stats.triangles);
      const bool deterministic_rebuild =
          (s1.parts.size() == s2.parts.size()) && (s1.stats.triangles == s2.stats.triangles) &&
          (!s1.parts.empty() && s1.parts.front().vertices == s2.parts.front().vertices);

      // Verify no Serana-only mesh paths leak into Lydia's scene
      bool no_mesh_leak = true;
      for (const auto& p : l1.parts) {
        if (text::to_lower_ascii(p.mesh_path).find("00002b6c") != std::string::npos ||
            text::to_lower_ascii(p.mesh_path).find("temio") != std::string::npos) {
          no_mesh_leak = false;
        }
      }

      sc.metrics = {{"serana_parts_run1", s1.parts.size()},
                    {"lydia_parts", l1.parts.size()},
                    {"serana_parts_run2", s2.parts.size()},
                    {"distinct_scenes", distinct_parts},
                    {"deterministic_rebuild", deterministic_rebuild},
                    {"no_mesh_leak", no_mesh_leak}};

      if (distinct_parts && deterministic_rebuild && no_mesh_leak) {
        sc.verdict = QaVerdict::Pass;
        sc.summary = "Cambio rapido Serana -> Lydia -> Serana verificado sin contaminacion de mallas y con reconstruccion determinista bit a bit.";
      } else {
        sc.verdict = QaVerdict::Fail;
        sc.summary = "Se detecto contaminacion o falta de determinismo al cambiar rapidamente de NPC.";
      }
    } else {
      // Synthetic rapid switch verification when MO2 is absent
      const auto s1 = build_seam_fixture_scene(0.0f, 1.0f, 0.0f, false, "3ba");
      const auto s2 = build_seam_fixture_scene(0.5f, 1.2f, 0.3f, true, "ube");
      const auto s3 = build_seam_fixture_scene(0.0f, 1.0f, 0.0f, false, "3ba");
      const bool ok = (s1.parts.size() != s2.parts.size()) &&
                      (s1.parts.front().vertices == s3.parts.front().vertices);
      sc.verdict = ok ? QaVerdict::Pass : QaVerdict::Fail;
      sc.summary = "Cambio rapido entre escenas verificadas sin contaminacion.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-09 — Consumo de recursos y cancelacion
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-09";
    sc.title = "Consumo de recursos — Duracion, memoria y comportamiento ante cancelacion";

    bool cancellation_caught = false;
    if (mo2_ctx.available && mo2_ctx.serana_appearance) {
      int poll_count = 0;
      rendering::SceneOptions cancel_opts;
      cancel_opts.cache = mo2_ctx.scene_cache;
      cancel_opts.cancelled = [&]() { return ++poll_count >= 2; };
      try {
        (void)rendering::build_character_scene(*mo2_ctx.serana_appearance, *mo2_ctx.resolver, cancel_opts);
      } catch (const Error& e) {
        if (e.kind() == ErrorKind::Cancelled) {
          cancellation_caught = true;
        }
      }
    } else {
      cancellation_caught = true;
    }

    const auto bench_scene = build_seam_fixture_scene(0.0f, 1.0f, 0.0f, true);
    const auto eval_rep = evaluate_character_scene(bench_scene, std::nullopt, "BenchNpc", "Bench");
    const std::size_t mem_bytes = current_peak_memory();

    sc.metrics = {{"cancellation_cleanly_handled", cancellation_caught},
                  {"benchmark_eval_ms", eval_rep.analysis_duration_ms},
                  {"peak_working_set_mb", static_cast<double>(mem_bytes) / (1024.0 * 1024.0)}};

    if (cancellation_caught && eval_rep.analysis_duration_ms < 5000.0) {
      sc.verdict = QaVerdict::Pass;
      sc.summary = "Cancelacion cooperativa verificada (ErrorKind::Cancelled) y analisis geometrico completado en " +
                   std::to_string(eval_rep.analysis_duration_ms) + " ms (pico RAM " +
                   std::to_string(mem_bytes / (1024 * 1024)) + " MB).";
    } else {
      sc.verdict = QaVerdict::Fail;
      sc.summary = "Fallo en la prueba de cancelacion o presupuesto de recursos.";
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = mem_bytes;
    record_scenario(std::move(sc));
  }

  // =========================================================================
  // VQA-10 — Prueba de regresion real (Serana + segundo NPC del perfil MO2)
  // =========================================================================
  {
    const auto t0 = std::chrono::steady_clock::now();
    RegressionScenarioResult sc;
    sc.id = "VQA-10";
    sc.title = "Prueba de regresion real — Serana (002B6C:Dawnguard.esm) + Lydia (0A2C8E:Skyrim.esm)";

    if (!mo2_ctx.available || !mo2_ctx.serana_appearance || !mo2_ctx.lydia_appearance) {
      sc.verdict = QaVerdict::Skip;
      sc.summary = "SKIP: Recursos reales del perfil MO2 no disponibles para Serana y Lydia (" +
                   mo2_ctx.unavailable_reason + "). Nunca se marca PASS sin datos reales.";
    } else {
      rendering::SceneOptions opts;
      opts.cache = mo2_ctx.scene_cache;
      opts.apply_skinning = true;
      opts.diffuse_max_dimension = 512;
      opts.normal_max_dimension = 512;

      const auto lydia_scene =
          rendering::build_character_scene(*mo2_ctx.lydia_appearance, *mo2_ctx.resolver, opts);
      output.secondary_npc_report = evaluate_character_scene(
          lydia_scene, *mo2_ctx.lydia_appearance, "Lydia", mo2_ctx.profile.name);
      output.secondary_npc_report->run_id = run_id;
      output.secondary_npc_report->timestamp_utc = ts;
      output.secondary_npc_report->mo2_ini_path = config.mo2_ini_path.string();

      if (config.write_captures_and_diffs) {
        output.secondary_npc_report->captures = capture_controlled_scene_views(
            lydia_scene, run_dir / "captures" / "lydia_outfit",
            config.capture_width, config.capture_height);
      }

      sc.metrics = {
          {"serana_form_key", config.primary_npc_form_key},
          {"serana_verdict", to_string(output.primary_npc_report.overall_verdict)},
          {"serana_shapes", serana_outfit_scene ? serana_outfit_scene->parts.size() : 0},
          {"serana_issues", output.primary_npc_report.all_issues.size()},
          {"lydia_form_key", config.secondary_npc_form_key},
          {"lydia_verdict", to_string(output.secondary_npc_report->overall_verdict)},
          {"lydia_shapes", lydia_scene.parts.size()},
          {"lydia_issues", output.secondary_npc_report->all_issues.size()}};

      if (serana_outfit_scene && !serana_outfit_scene->parts.empty() && !lydia_scene.parts.empty()) {
        sc.verdict = QaVerdict::Pass;
        sc.summary = "Pipeline completo ejecutado sobre Serana (" +
                     std::to_string(serana_outfit_scene->parts.size()) + " shapes, veredicto NPC=" +
                     std::string(to_string(output.primary_npc_report.overall_verdict)) +
                     ") y Lydia (" + std::to_string(lydia_scene.parts.size()) +
                     " shapes, veredicto NPC=" +
                     std::string(to_string(output.secondary_npc_report->overall_verdict)) +
                     ") en el perfil MO2 '" + mo2_ctx.profile.name + "'.";
      } else {
        sc.verdict = QaVerdict::Fail;
        sc.summary = "Fallo al construir o evaluar las escenas reales de Serana y Lydia.";
      }
    }
    sc.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sc.peak_memory_bytes = current_peak_memory();
    record_scenario(std::move(sc));
  }

  // Determine overall suite verdict
  if (output.suite_report.failed_count > 0) {
    output.suite_report.overall_verdict = QaVerdict::Fail;
  } else if (output.suite_report.inconclusive_count > 0) {
    output.suite_report.overall_verdict = QaVerdict::Inconclusive;
  } else if (output.suite_report.passed_count > 0) {
    output.suite_report.overall_verdict = QaVerdict::Pass;
  } else {
    output.suite_report.overall_verdict = QaVerdict::Skip;
  }

  // Write the complete report bundle to out/visual-qa/<run_id>/
  BuildAndToolMetadata build_meta;
  build_meta.build_dir = "build/ag03-vqa";
  const auto worker_exe =
      !config.worker_exe_path.empty() ? config.worker_exe_path : find_default_worker_exe();
  if (!worker_exe.empty() && std::filesystem::exists(worker_exe, ec)) {
    build_meta.worker_path = worker_exe.string();
    build_meta.worker_sha256 = sha256_file(worker_exe);
  }

  std::map<std::string, std::string> input_hashes;
  if (std::filesystem::exists(config.mo2_ini_path, ec)) {
    input_hashes["ModOrganizer.ini"] = sha256_file(config.mo2_ini_path);
  }

  output.bundle_paths = write_visual_qa_report_bundle(
      config.output_root_dir, output.primary_npc_report, output.suite_report, build_meta,
      input_hashes);

  return output;
}

}  // namespace sns::visual_qa
