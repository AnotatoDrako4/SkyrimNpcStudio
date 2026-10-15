#include "sns/projects/project.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <nlohmann/json.hpp>
#include <random>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"

namespace sns::projects {
namespace fs = std::filesystem;
using Json = nlohmann::json;

// ---- JSON mapping (explicit, so the on-disk format is reviewable and stable) -------------------

void to_json(Json& j, const EnvironmentIdentity& v) {
  j = {{"instanceName", v.instance_name}, {"instanceIni", v.instance_ini}, {"profile", v.profile}, {"gameEdition", v.game_edition}};
}
void from_json(const Json& j, EnvironmentIdentity& v) {
  v.instance_name = j.value("instanceName", "");
  v.instance_ini = j.value("instanceIni", "");
  v.profile = j.value("profile", "");
  v.game_edition = j.value("gameEdition", "");
}
void to_json(Json& j, const NpcTarget& v) {
  j = {{"formKey", v.form_key}, {"editorId", v.editor_id}, {"name", v.name}, {"winningPlugin", v.winning_plugin}, {"overrideCount", v.override_count}};
}
void from_json(const Json& j, NpcTarget& v) {
  v.form_key = j.at("formKey").get<std::string>();
  v.editor_id = j.value("editorId", "");
  v.name = j.value("name", "");
  v.winning_plugin = j.value("winningPlugin", "");
  v.override_count = j.value("overrideCount", 0);
}
void to_json(Json& j, const ColorRgb& v) {
  j = {{"r", v.r}, {"g", v.g}, {"b", v.b}};
}
void from_json(const Json& j, ColorRgb& v) {
  v.r = j.value("r", static_cast<std::uint8_t>(255));
  v.g = j.value("g", static_cast<std::uint8_t>(255));
  v.b = j.value("b", static_cast<std::uint8_t>(255));
}
void to_json(Json& j, const PieceMaterialOverride& v) {
  j = {{"name", v.name}, {"glossiness", v.glossiness}, {"specularStrength", v.specular_strength}};
  if (v.tint_color) j["tintColor"] = *v.tint_color;
  if (!v.diffuse_override.empty()) j["diffuseOverride"] = v.diffuse_override;
  if (!v.normal_override.empty()) j["normalOverride"] = v.normal_override;
}
void from_json(const Json& j, PieceMaterialOverride& v) {
  v.name = j.value("name", "");
  v.glossiness = j.value("glossiness", 1.0f);
  v.specular_strength = j.value("specularStrength", 1.0f);
  if (j.contains("tintColor") && !j["tintColor"].is_null()) {
    v.tint_color = j["tintColor"].get<ColorRgb>();
  }
  v.diffuse_override = j.value("diffuseOverride", "");
  v.normal_override = j.value("normalOverride", "");
}
void to_json(Json& j, const OutfitPieceRef& v) {
  j = {{"formKey", v.form_key}, {"name", v.name}, {"plugin", v.plugin}, {"slotMask", v.slot_mask}};
  if (v.material) j["material"] = *v.material;
}
void from_json(const Json& j, OutfitPieceRef& v) {
  v.form_key = j.at("formKey").get<std::string>();
  v.name = j.value("name", "");
  v.plugin = j.value("plugin", "");
  v.slot_mask = j.value("slotMask", 0u);
  if (j.contains("material") && !j["material"].is_null()) {
    v.material = j["material"].get<PieceMaterialOverride>();
  }
}
void to_json(Json& j, const OutfitConfig& v) {
  j = {{"outfitEditorId", v.outfit_editor_id}, {"pieces", v.pieces}, {"alsoSleeping", v.also_sleeping}};
}
void from_json(const Json& j, OutfitConfig& v) {
  v.outfit_editor_id = j.value("outfitEditorId", "");
  v.pieces = j.value("pieces", std::vector<OutfitPieceRef>{});
  v.also_sleeping = j.value("alsoSleeping", false);
}
void to_json(Json& j, const ExportOptions& v) {
  j = {{"pluginName", v.plugin_name}, {"eslFlag", v.esl_flag}, {"createZip", v.create_zip}, {"outputDirectory", v.output_directory}};
}
void from_json(const Json& j, ExportOptions& v) {
  v.plugin_name = j.value("pluginName", "");
  v.esl_flag = j.value("eslFlag", false);
  v.create_zip = j.value("createZip", true);
  v.output_directory = j.value("outputDirectory", "");
}
void to_json(Json& j, const HeadPartRef& v) {
  j = {{"formKey", v.form_key}, {"editorId", v.editor_id}, {"type", v.type}, {"meshPath", v.mesh_path}};
}
void from_json(const Json& j, HeadPartRef& v) {
  v.form_key = j.value("formKey", "");
  v.editor_id = j.value("editorId", "");
  v.type = j.value("type", "");
  v.mesh_path = j.value("meshPath", "");
}
void to_json(Json& j, const TintLayerConfig& v) {
  j = {{"index", v.index}, {"color", v.color}, {"alpha", v.alpha}, {"preset", v.preset}};
}
void from_json(const Json& j, TintLayerConfig& v) {
  v.index = j.value("index", 0u);
  if (j.contains("color")) v.color = j["color"].get<ColorRgb>();
  v.alpha = j.value("alpha", 1.0f);
  v.preset = j.value("preset", std::int32_t{0});  // -1 (no TINP) since 2026-10-14; older files clamped it to 0
}
void to_json(Json& j, const SculptConfig& v) {
  j = {{"host", v.host}, {"vertexCount", v.vertex_count}, {"deltas", v.deltas}};
}
void from_json(const Json& j, SculptConfig& v) {
  v.host = j.value("host", "");
  v.vertex_count = j.value("vertexCount", 0u);
  v.deltas = j.value("deltas", std::vector<std::array<float, 4>>{});
}
void to_json(Json& j, const FaceConfig& v) {
  j = {{"presetId", v.preset_id},
       {"presetName", v.preset_name},
       {"presetSource", v.preset_source},
       {"donorNpcFormKey", v.donor_npc_form_key},
       {"donorPlugin", v.donor_plugin},
       {"donorFacegenMesh", v.donor_facegen_mesh},
       {"donorFacegenTint", v.donor_facegen_tint},
       {"headParts", v.head_parts},
       {"morphSliders", v.morph_sliders},
       {"tintLayers", v.tint_layers},
       {"weight", v.weight},
       {"height", v.height},
       {"complete", v.complete},
       {"facePresets", v.face_presets},
       {"customMorphs", v.custom_morphs},
       {"sculpt", v.sculpt},
       {"geometryMode", v.geometry_mode},
       {"headTexture", v.head_texture}};
  if (v.hair_color) j["hairColor"] = *v.hair_color;
  if (v.skin_tone) j["skinTone"] = *v.skin_tone;
}
void from_json(const Json& j, FaceConfig& v) {
  v.preset_id = j.value("presetId", "");
  v.preset_name = j.value("presetName", "");
  v.preset_source = j.value("presetSource", "");
  v.donor_npc_form_key = j.value("donorNpcFormKey", "");
  v.donor_plugin = j.value("donorPlugin", "");
  v.donor_facegen_mesh = j.value("donorFacegenMesh", "");
  v.donor_facegen_tint = j.value("donorFacegenTint", "");
  v.head_parts = j.value("headParts", std::vector<HeadPartRef>{});
  v.morph_sliders = j.value("morphSliders", std::map<std::string, float>{});
  v.tint_layers = j.value("tintLayers", std::vector<TintLayerConfig>{});
  v.weight = j.value("weight", 100.0f);
  v.height = j.value("height", 1.0f);
  if (j.contains("hairColor")) v.hair_color = j["hairColor"].get<ColorRgb>();
  if (j.contains("skinTone")) v.skin_tone = j["skinTone"].get<ColorRgb>();
  // Format 2 files have none of these: they hold partial edits (complete = false).
  v.complete = j.value("complete", false);
  v.face_presets = j.value("facePresets", std::vector<std::int32_t>{});
  v.custom_morphs = j.value("customMorphs", std::map<std::string, float>{});
  v.sculpt = j.value("sculpt", std::vector<SculptConfig>{});
  v.geometry_mode = j.value("geometryMode", "");
  v.head_texture = j.value("headTexture", "");
}
void to_json(Json& j, const HistoryEntry& v) { j = {{"utc", v.utc}, {"action", v.action}}; }
void from_json(const Json& j, HistoryEntry& v) {
  v.utc = j.value("utc", "");
  v.action = j.value("action", "");
}

namespace {

constexpr const char* kProjectFile = "project.json";
constexpr const char* kNpcFile = "npc.json";
constexpr const char* kOutfitFile = "outfit.json";
constexpr const char* kFaceFile = "face.json";
constexpr const char* kDependenciesFile = "dependencies.json";

Json read_json(const fs::path& path) {
  auto json = Json::parse(file_io::read_text_file(path), nullptr, false);
  if (json.is_discarded()) throw Error(ErrorKind::Parse, "invalid JSON in " + text::path_to_utf8(path));
  return json;
}

void write_json(const fs::path& path, const Json& json) { file_io::write_file_atomic(path, json.dump(2) + "\n"); }

std::string new_id() {
  std::random_device rd;
  return std::format("{:08x}{:08x}", rd(), rd());
}

void snapshot_previous(const fs::path& dir) {
  std::error_code ec;
  if (!fs::exists(dir / kProjectFile, ec)) return;
  auto stamp = utc_now_iso8601();
  std::replace(stamp.begin(), stamp.end(), ':', '-');
  const auto target = dir / "versions" / text::path_from_utf8(stamp);
  fs::create_directories(target, ec);
  for (const char* name : {kProjectFile, kNpcFile, kOutfitFile, kFaceFile, kDependenciesFile}) {
    if (fs::exists(dir / name, ec)) fs::copy_file(dir / name, target / name, fs::copy_options::overwrite_existing, ec);
  }
  auto versions = ProjectStore::list_versions(dir);
  while (versions.size() > ProjectStore::kMaxVersions) {
    fs::remove_all(dir / "versions" / text::path_from_utf8(versions.front()), ec);
    versions.erase(versions.begin());
  }
}

}  // namespace

std::string utc_now_iso8601() {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

Project ProjectStore::create(std::string name, EnvironmentIdentity environment) {
  Project p;
  p.id = new_id();
  p.name = std::move(name);
  p.created_utc = utc_now_iso8601();
  p.modified_utc = p.created_utc;
  p.environment = std::move(environment);
  p.history.push_back({p.created_utc, "Project created"});
  return p;
}

void ProjectStore::save(const fs::path& dir, Project& project) {
  if (project.id.empty()) throw Error(ErrorKind::InvalidArgument, "project has no id");
  snapshot_previous(dir);
  project.modified_utc = utc_now_iso8601();

  std::vector<std::string> deps;
  Json npcs = Json::array();
  Json outfits = Json::object();
  Json faces = Json::object();
  bool has_faces = false;
  for (const auto& edit : project.npcs) {
    npcs.push_back(edit.target);
    deps.push_back(edit.target.form_key.substr(edit.target.form_key.find(':') + 1));
    if (edit.outfit) {
      outfits[edit.target.form_key] = *edit.outfit;
      for (const auto& piece : edit.outfit->pieces) deps.push_back(piece.form_key.substr(piece.form_key.find(':') + 1));
    }
    if (edit.face) {
      has_faces = true;
      faces[edit.target.form_key] = *edit.face;
      if (!edit.face->donor_plugin.empty()) deps.push_back(edit.face->donor_plugin);
      for (const auto& part : edit.face->head_parts) {
        if (const auto pos = part.form_key.find(':'); pos != std::string::npos) {
          deps.push_back(part.form_key.substr(pos + 1));
        }
      }
    }
  }
  std::sort(deps.begin(), deps.end());
  deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
  project.dependencies = deps;

  write_json(dir / kNpcFile, {{"formatVersion", kProjectFormatVersion}, {"npcs", npcs}});
  write_json(dir / kOutfitFile, {{"formatVersion", kProjectFormatVersion}, {"outfits", outfits}});
  if (has_faces) {
    write_json(dir / kFaceFile, {{"formatVersion", kProjectFormatVersion}, {"faces", faces}});
  } else {
    // No face edits any more: a face.json left from an earlier save would bring them back on load.
    std::error_code removed;
    fs::remove(dir / kFaceFile, removed);
  }
  write_json(dir / kDependenciesFile, {{"formatVersion", kProjectFormatVersion}, {"plugins", deps}});
  // project.json last: it is the file that marks a complete save.
  write_json(dir / kProjectFile, {{"format", "skyrim-npc-studio/project"},
                                  {"formatVersion", kProjectFormatVersion},
                                  {"id", project.id},
                                  {"name", project.name},
                                  {"createdUtc", project.created_utc},
                                  {"modifiedUtc", project.modified_utc},
                                  {"environment", project.environment},
                                  {"exportOptions", project.export_options},
                                  {"history", project.history}});
  std::error_code ec;
  fs::create_directories(dir / "exports", ec);
}

Project ProjectStore::load(const fs::path& dir) {
  const auto head = read_json(dir / kProjectFile);
  if (head.value("format", "") != "skyrim-npc-studio/project") {
    throw Error(ErrorKind::Parse, text::path_to_utf8(dir) + " is not a Skyrim NPC Studio project");
  }
  const int version = head.value("formatVersion", 0);
  if (version < 1 || version > kProjectFormatVersion) {
    throw Error(ErrorKind::Parse, "unsupported project format version " + std::to_string(version));
  }

  Project p;
  p.format_version = version;
  p.id = head.at("id").get<std::string>();
  p.name = head.value("name", "");
  p.created_utc = head.value("createdUtc", "");
  p.modified_utc = head.value("modifiedUtc", "");
  p.environment = head.value("environment", EnvironmentIdentity{});
  p.export_options = head.value("exportOptions", ExportOptions{});
  p.history = head.value("history", std::vector<HistoryEntry>{});

  std::error_code ec;
  if (fs::exists(dir / kNpcFile, ec)) {
    for (const auto& target : read_json(dir / kNpcFile).value("npcs", Json::array())) {
      p.npcs.push_back({target.get<NpcTarget>(), std::nullopt, std::nullopt});
    }
  }
  if (fs::exists(dir / kOutfitFile, ec)) {
    const auto outfits = read_json(dir / kOutfitFile).value("outfits", Json::object());
    for (auto& edit : p.npcs) {
      if (outfits.contains(edit.target.form_key)) edit.outfit = outfits[edit.target.form_key].get<OutfitConfig>();
    }
  }
  if (fs::exists(dir / kFaceFile, ec)) {
    const auto faces = read_json(dir / kFaceFile).value("faces", Json::object());
    for (auto& edit : p.npcs) {
      if (faces.contains(edit.target.form_key)) edit.face = faces[edit.target.form_key].get<FaceConfig>();
    }
  }
  if (fs::exists(dir / kDependenciesFile, ec)) {
    p.dependencies = read_json(dir / kDependenciesFile).value("plugins", std::vector<std::string>{});
  }
  return p;
}

std::vector<std::string> ProjectStore::list_versions(const fs::path& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir / "versions", ec)) {
    if (entry.is_directory(ec)) out.push_back(text::path_to_utf8(entry.path().filename()));
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace sns::projects
