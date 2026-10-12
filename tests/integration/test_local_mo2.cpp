// LOCAL integration tests against a real MO2 instance and a real Skyrim SE/AE install.
// They never modify the instance: everything is read-only, and the optional export goes to a
// separate folder. Enable them with environment variables:
//   SNS_LOCAL_MO2_INI     path to ModOrganizer.ini (required, otherwise the tests are skipped)
//   SNS_LOCAL_PROFILE     profile name (default: the instance's selected profile)
//   SNS_LOCAL_EXPORT_DIR  if set, Lydia's outfit patch is exported there for manual MO2/in-game testing
//   SNS_LOCAL_OUTFIT      comma-separated armor EditorIDs (default: vanilla steel set)

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

#include "sns/core/text.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/ipc/worker_process.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/rendering/scene.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::ipc;
using namespace std::chrono_literals;
namespace method = protocol::method;

namespace {

std::optional<std::string> env(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return std::nullopt;
  std::string s(value);
  free(value);
  return s.empty() ? std::nullopt : std::optional(s);
}

struct LocalSetup {
  mo2::InstanceConfig config;
  mo2::Profile profile;
  mo2::GameInstall game;
  mo2::VirtualFileSystem vfs;
  mo2::EffectiveLoadOrder load_order;
  long long vfs_ms = 0;
};

LocalSetup& local_setup() {
  static std::unique_ptr<LocalSetup> setup;
  if (setup) return *setup;
  const auto ini = env("SNS_LOCAL_MO2_INI");
  if (!ini) SKIP("SNS_LOCAL_MO2_INI is not set");

  setup = std::make_unique<LocalSetup>();
  auto info = mo2::instance_from_path(text::path_from_utf8(*ini));
  REQUIRE(info);
  setup->config = mo2::load_instance_config(*info);
  if (const auto game = env("SNS_LOCAL_GAME_DIR")) setup->config.game_path = text::path_from_utf8(*game);
  setup->profile = mo2::load_profile(setup->config, env("SNS_LOCAL_PROFILE").value_or(setup->config.selected_profile));
  setup->game = mo2::inspect_game(setup->config.game_path);
  const auto t0 = std::chrono::steady_clock::now();
  setup->vfs = mo2::VirtualFileSystem::build(setup->config, setup->profile, setup->game);
  setup->vfs_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  setup->load_order = mo2::resolve_load_order(setup->profile, setup->vfs, setup->game);
  return *setup;
}

}  // namespace

TEST_CASE("[local] Real MO2 profile: VFS and effective load order", "[local]") {
  auto& s = local_setup();
  REQUIRE(mo2::is_supported_game(s.config));
  REQUIRE(s.game.exists);
  REQUIRE_FALSE(s.load_order.active.empty());
  CHECK(s.load_order.active.front().name == "Skyrim.esm");

  std::map<std::string, int> codes;
  for (const auto& d : s.load_order.diagnostics) ++codes[d.code];
  std::cout << "[local] profile '" << s.profile.name << "': " << s.profile.mods.size() << " mod entries, "
            << s.vfs.providers().size() << " providers, " << s.vfs.file_count() << " files (VFS " << s.vfs_ms
            << " ms); game " << s.game.exe_version << " (" << mo2::to_string(s.game.edition) << "); "
            << s.load_order.active.size() << " active / " << s.load_order.inactive.size() << " inactive plugins\n";
  for (const auto& [code, n] : codes) std::cout << "[local]   diagnostic " << code << " x" << n << "\n";
}

TEST_CASE("[local] Real load order indexed by the worker; Lydia found", "[local]") {
  auto& s = local_setup();
  test::TempDir dir;
  WorkerProcess worker({SNS_WORKER_EXE, {}, dir.path() / "worker.log"});
  worker.start();
  auto client = connect_worker(worker, 30s);

  Json plugins = Json::array();
  std::map<std::string, std::string> providers;
  for (const auto& p : s.load_order.active) {
    plugins.push_back({{"fileName", p.name}, {"path", p.path ? Json(text::path_to_utf8(*p.path)) : Json()}, {"enabled", true}});
    providers[p.name] = p.provider;
  }
  const auto cache = text::path_from_utf8(env("SNS_LOCAL_CACHE_DIR").value_or(text::path_to_utf8(dir.path() / "cache")));
  const auto t0 = std::chrono::steady_clock::now();
  const auto language = mo2::detect_game_language(s.profile);
  std::cout << "[local] game language: " << language << "\n";
  const auto index = client->call(method::kCatalogIndex,
                                  Json{{"profileKey", "local-" + s.profile.name},
                                       {"cacheDirectory", text::path_to_utf8(cache)},
                                       {"plugins", plugins},
                                       {"language", language}},
                                  30min);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "[local] indexed " << index["pluginCount"] << " plugins (" << index["parsedPlugins"] << " parsed, "
            << index["cachedPlugins"] << " cached): " << index["npcCount"] << " NPC, " << index["armorCount"]
            << " ARMO, " << index["outfitCount"] << " OTFT in " << ms << " ms; " << index["issues"].size() << " issues\n";
  std::map<std::string, int> issue_codes;
  std::map<std::string, std::string> issue_example;
  for (const auto& i : index["issues"]) {
    const auto code = i["severity"].get<std::string>() + "/" + i["code"].get<std::string>();
    if (issue_codes[code]++ == 0) issue_example[code] = i.value("subject", "") + ": " + i.value("message", "");
  }
  for (const auto& [code, n] : issue_codes) std::cout << "[local]   issue " << code << " x" << n << " e.g. " << issue_example[code] << "\n";
  CHECK(index["npcCount"].get<int>() > 1000);

  const auto lydia = client->call(method::kGetNpc, Json{{"formKey", "0A2C8E:Skyrim.esm"}}, 30s);
  CHECK(lydia["summary"]["editorId"] == "HousecarlWhiterun");
  std::cout << "[local] Lydia: winner " << lydia["summary"]["winningPlugin"] << " (" << lydia["summary"]["formId"]
            << "), overrides:";
  for (const auto& o : lydia["overrides"]) std::cout << " " << o["plugin"].get<std::string>();
  std::cout << "\n[local] Lydia default outfit: " << lydia["defaultOutfit"]["outfit"].value("editorId", "?") << " ->";
  for (const auto& a : lydia["defaultOutfit"]["armors"]) std::cout << " [" << a.value("name", "?") << "]";
  std::cout << "\n[local] Lydia conflicts: " << lydia["conflicts"].size() << "\n";

  const auto facegen = lydia["faceGen"]["geometryPath"].get<std::string>();
  const auto appearance = client->call(method::kGetAppearance, Json{{"npcFormKey", "0A2C8E:Skyrim.esm"}}, 30s);
  assets::AssetResolver resolver(s.vfs, assets::archive_load_order(s.profile, s.load_order, s.vfs));
  rendering::SceneOptions scene_options;
  scene_options.apply_skinning = true;
  const auto scene = rendering::build_character_scene(rendering::appearance_from_json(appearance), resolver, scene_options);
  std::cout << "[local] real scene: " << scene.stats.meshes_loaded << " meshes, " << scene.stats.shapes << " shapes, "
            << scene.stats.triangles << " triangles, " << scene.stats.textures_loaded << " textures, " << scene.skeleton_bones << " bones\n";
  for (const auto& issue : scene.issues) std::cout << "[local] scene " << issue.code << ": " << issue.subject << " " << issue.message << "\n";
  CHECK(scene.stats.shapes > 0);
  CHECK(scene.stats.textures_loaded > 0);
  CHECK(scene.skeleton_bones > 0);
  CHECK(appearance["faceGeometryPath"].get<std::string>().find("Skyrim.esm") != std::string::npos);
  if (const auto file = s.vfs.resolve(facegen)) {
    std::cout << "[local] FaceGen " << facegen << " provided by " << s.vfs.providers()[file->provider].name << "\n";
  } else {
    std::cout << "[local] FaceGen " << facegen << " not found as a loose file (may be inside a BSA)\n";
  }

  if (const auto export_dir = env("SNS_LOCAL_EXPORT_DIR")) {
    const auto wanted = env("SNS_LOCAL_OUTFIT").value_or("ArmorSteelCuirassA,ArmorSteelBootsA,ArmorSteelGauntletsA");
    Json armor_keys = Json::array();
    size_t start = 0;
    while (start <= wanted.size()) {
      auto end = wanted.find(',', start);
      if (end == std::string::npos) end = wanted.size();
      const auto editor_id = std::string(text::trim(std::string_view(wanted).substr(start, end - start)));
      const auto found = client->call(method::kSearchArmors, Json{{"text", editor_id}, {"limit", 50}}, 30s);
      for (const auto& item : found["items"]) {
        if (item.value("editorId", "") == editor_id) {
          armor_keys.push_back(item["formKey"]);
          break;
        }
      }
      start = end + 1;
    }
    REQUIRE(armor_keys.size() >= 1);

    const auto validation = client->call(method::kValidateOutfit, Json{{"npcFormKey", "0A2C8E:Skyrim.esm"}, {"armorFormKeys", armor_keys}}, 30s);
    for (const auto& i : validation["issues"]) std::cout << "[local] validate " << i["severity"] << " " << i["code"] << "\n";
    REQUIRE(validation["exportable"] == true);

    Json plugin_providers = Json::object();
    for (const auto& [plugin, provider] : providers) plugin_providers[plugin] = provider;
    const auto name = "SkyrimNpcStudio_Lydia_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 100000) + ".esp";
    const auto exported = client->call(method::kExportOutfitPatch,
                                       Json{{"pluginName", name},
                                            {"outputDirectory", *export_dir},
                                            {"eslFlag", true},
                                            {"description", "Prueba local: Lydia con armadura de acero"},
                                            {"pluginProviders", plugin_providers},
                                            {"assignments", Json::array({{{"npcFormKey", "0A2C8E:Skyrim.esm"}, {"armorFormKeys", armor_keys}}})}},
                                       5min);
    std::cout << "[local] exported " << exported["pluginPath"].get<std::string>() << "\n[local] zip "
              << exported.value("zipPath", "") << "\n[local] masters " << exported["masters"].dump() << "\n";
  }
  client->call(method::kShutdown, nullptr, 10s);
}

// Diagnostic dump of Lydia's real scene: one line per render part with its effective textures,
// their pixel statistics, alpha flags and bounds, plus the raw texture slots of the FaceGen NIF.
TEST_CASE("[local] Lydia scene diagnostics", "[local][diag]") {
  auto& s = local_setup();
  test::TempDir dir;
  WorkerProcess worker({SNS_WORKER_EXE, {}, dir.path() / "worker.log"});
  worker.start();
  auto client = connect_worker(worker, 30s);
  Json plugins = Json::array();
  for (const auto& p : s.load_order.active) {
    plugins.push_back({{"fileName", p.name}, {"path", p.path ? Json(text::path_to_utf8(*p.path)) : Json()}, {"enabled", true}});
  }
  const auto cache = text::path_from_utf8(env("SNS_LOCAL_CACHE_DIR").value_or(text::path_to_utf8(dir.path() / "cache")));
  client->call(method::kCatalogIndex, Json{{"profileKey", "local-" + s.profile.name}, {"cacheDirectory", text::path_to_utf8(cache)},
                                           {"plugins", plugins}, {"language", mo2::detect_game_language(s.profile)}}, 30min);
  const auto npc = env("SNS_LOCAL_NPC").value_or("0A2C8E:Skyrim.esm");
  const auto appearance = client->call(method::kGetAppearance, Json{{"npcFormKey", npc}}, 30s);
  std::cout << "[diag] appearance " << appearance.dump() << "\n";

  assets::AssetResolver resolver(s.vfs, assets::archive_load_order(s.profile, s.load_order, s.vfs));
  auto where = [&](const std::string& path) -> std::string {
    if (path.empty()) return "-";
    const auto loc = resolver.locate(path);
    if (!loc) return "NOT FOUND";
    return loc->kind == assets::AssetLocation::Kind::Loose ? "loose:" + loc->provider : "bsa:" + loc->provider;
  };

  // Raw FaceGen NIF content, independent of the scene builder.
  const auto face = appearance.value("faceGeometryPath", "");
  if (auto bytes = resolver.read(face)) {
    const auto mesh = nif::load_mesh(*bytes, face);
    std::cout << "[diag] FaceGen " << face << " <- " << where(face) << ", " << mesh.shapes.size() << " shapes\n";
    for (const auto& sh : mesh.shapes) {
      std::cout << "[diag]   shape '" << sh.name << "' shader=" << sh.shader_type << " verts=" << sh.positions.size()
                << " tris=" << sh.indices.size() / 3 << " skinned=" << sh.skinned << " alpha(blend=" << sh.alpha_blend
                << ",test=" << sh.alpha_test << ",thr=" << int(sh.alpha_threshold) << ") 2side=" << sh.double_sided
                << " msn=" << sh.model_space_normals << " vcol=" << sh.vertex_colors << " uv=" << sh.uvs.size() << "\n";
      for (std::size_t slot = 0; slot < sh.textures.size(); ++slot) {
        if (!sh.textures[slot].empty()) std::cout << "[diag]     slot" << slot << " " << sh.textures[slot] << " <- " << where(sh.textures[slot]) << "\n";
      }
    }
  }

  rendering::SceneOptions options;
  options.apply_skinning = true;
  const auto scene = rendering::build_character_scene(rendering::appearance_from_json(appearance), resolver, options);
  auto info_of = [&](const std::string& key) -> std::string {
    if (key.empty()) return "-";
    const auto it = scene.texture_info.find(key);
    if (it == scene.texture_info.end()) return "not requested";
    const auto& i = it->second;
    if (!i.error.empty()) return "ERROR " + i.error + " [" + i.location + "]";
    char buf[320];
    std::snprintf(buf, sizeof buf, "%s %ux%u (file %ux%u, mip %u) [%s] alpha: uses=%d min=%u mean=%.0f translucent=%.1f%%%s%s",
                  i.format.c_str(), i.width, i.height, i.source_width, i.source_height, i.mip_level, i.location.c_str(),
                  i.alpha.uses_alpha, i.alpha.min, i.alpha.mean, 100.0 * i.alpha.translucent_fraction,
                  i.derived_from.empty() ? "" : " from ", i.derived_from.c_str());
    return buf;
  };
  for (const auto& p : scene.parts) {
    std::string stats = "no-texture";
    if (const auto it = scene.textures.find(p.diffuse); it != scene.textures.end()) {
      const auto& img = *it->second;
      double r = 0, g = 0, b = 0, a = 0;
      std::size_t white = 0, below = 0;
      const auto n = img.data.size() / 4;
      for (std::size_t i = 0; i < img.data.size(); i += 4) {
        const auto* px = reinterpret_cast<const unsigned char*>(img.data.data() + i);
        r += px[0]; g += px[1]; b += px[2]; a += px[3];
        if (px[0] > 235 && px[1] > 235 && px[2] > 235) ++white;
        if (px[3] < p.alpha_cutoff * 255) ++below;
      }
      char buf[256];
      std::snprintf(buf, sizeof buf, "%ux%u %s alpha=%d mean=(%.0f,%.0f,%.0f,a%.0f) white=%.1f%% belowCutoff=%.1f%%",
                    img.width, img.height, std::string(textures::to_string(img.format)).c_str(), img.has_alpha, r / n, g / n, b / n,
                    a / n, 100.0 * white / n, 100.0 * below / n);
      stats = std::string(buf) + "\n[diag]       " + info_of(p.diffuse);
    }
    std::cout << "[diag] part " << p.id << " [" << p.group << "] " << p.label << " / " << p.shape_name << " shader=" << p.shader_type
              << " alphaFlags=" << p.alpha_flags << " mode=" << rendering::to_string(p.alpha_mode) << " (" << p.alpha_reason << ")"
              << " shown=" << p.shown_by_default << (p.hidden_reason.empty() ? "" : " [" + p.hidden_reason + "]")
              << " 2side=" << p.double_sided << " vcol=" << p.vertex_colors
              << " tint=" << (p.tint ? std::to_string(p.tint->r) + "," + std::to_string(p.tint->g) + "," + std::to_string(p.tint->b) : "-")
              << " skin=" << p.skinning_applied << " meshBones=" << p.bones_from_mesh.size() << " missingBones=" << p.bones_missing.size()
              << " tswap=" << p.tangents_swapped << " tris=" << p.indices.size() / 3 << " min=(" << p.min[0] << "," << p.min[1] << "," << p.min[2]
              << ") max=(" << p.max[0] << "," << p.max[1] << "," << p.max[2] << ") mesh=" << p.mesh_location << "\n"
              << "[diag]     diffuse " << (p.diffuse.empty() ? "-" : p.diffuse) << " :: " << stats << "\n"
              << "[diag]     normal  " << (p.normal.empty() ? "-" : p.normal) << " :: " << info_of(p.normal) << "\n";
  }
  // Raw shapes of every equipped mesh (including the ones the scene skips).
  std::set<std::string> meshes;
  for (const auto& p : scene.parts) meshes.insert(p.mesh_path);
  for (const auto& path : meshes) {
    const auto bytes = resolver.read(path);
    if (!bytes) continue;
    const auto mesh = nif::load_mesh(*bytes, path);
    std::cout << "[diag] mesh " << path << " physics=" << (mesh.physics_xml.empty() ? "-" : mesh.physics_xml) << "\n";
    for (const auto& sh : mesh.shapes) {
      int textures = 0;
      for (const auto& t : sh.textures) textures += !t.empty();
      std::cout << "[diag]   raw '" << sh.name << "' shader=" << sh.has_shader << "/" << sh.shader_type << " hidden=" << sh.hidden
                << " textures=" << textures << " alphaFlags=" << sh.alpha_flags << " skinned=" << sh.skinned << " bones=" << sh.bones.size()
                << " parent=" << (sh.parent_node.empty() ? "-" : sh.parent_node) << " block=" << (sh.shader_block.empty() ? "-" : sh.shader_block)
                << " texset=" << sh.has_texture_set << " sf1=" << std::hex << sh.shader_flags1 << " sf2=" << sh.shader_flags2 << std::dec
                << " matAlpha=" << sh.material_alpha << " uv=(" << sh.uv_offset.u << "," << sh.uv_offset.v << ")x(" << sh.uv_scale.u << ","
                << sh.uv_scale.v << ") tris=" << sh.indices.size() / 3 << " partTris="
                << (sh.partition_triangles ? std::to_string(*sh.partition_triangles) : std::string("-")) << "\n";
    }
  }
  for (const auto& issue : scene.issues) {
    if (issue.code != "BoneMissing") std::cout << "[diag] issue " << issue.severity << " " << issue.code << ": " << issue.subject << " " << issue.message << "\n";
  }
  client->call(method::kShutdown, nullptr, 10s);
}
