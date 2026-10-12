#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sns::projects {

inline constexpr int kProjectFormatVersion = 3;
inline constexpr const char* kProjectExtension = ".snps";

// Which MO2 setup the project was made against. Paths are informational: the project resolves
// records by FormKey and plugin name, so it survives moving the instance or switching profiles.
struct EnvironmentIdentity {
  std::string instance_name;
  std::string instance_ini;  // last known location, may be stale
  std::string profile;
  std::string game_edition;
};

struct NpcTarget {
  std::string form_key;  // "0A2C8E:Skyrim.esm" - the identity; never the display name
  std::string editor_id;
  std::string name;
  std::string winning_plugin;  // winner when the edit was made
  int override_count = 0;
};

struct ColorRgb {
  std::uint8_t r = 255;
  std::uint8_t g = 255;
  std::uint8_t b = 255;

  bool operator==(const ColorRgb&) const = default;
};

struct PieceMaterialOverride {
  std::string name;
  std::optional<ColorRgb> tint_color;
  std::string diffuse_override;
  std::string normal_override;
  float glossiness = 1.0f;
  float specular_strength = 1.0f;

  bool operator==(const PieceMaterialOverride&) const = default;
};

struct OutfitPieceRef {
  std::string form_key;
  std::string name;
  std::string plugin;
  std::uint32_t slot_mask = 0;
  std::optional<PieceMaterialOverride> material;

  bool operator==(const OutfitPieceRef&) const = default;
};

struct OutfitConfig {
  std::string outfit_editor_id;
  std::vector<OutfitPieceRef> pieces;
  bool also_sleeping = false;
};

struct HeadPartRef {
  std::string form_key;
  std::string editor_id;
  std::string type;
  std::string mesh_path;

  bool operator==(const HeadPartRef&) const = default;
};

struct TintLayerConfig {
  std::uint32_t index = 0;
  ColorRgb color;
  float alpha = 1.0f;
  std::uint32_t preset = 0;

  bool operator==(const TintLayerConfig&) const = default;
};

// RaceMenu sculpt offsets for one head part, addressed by its chargen TRI ("host").
struct SculptConfig {
  std::string host;
  std::uint32_t vertex_count = 0;
  std::vector<std::array<float, 4>> deltas;  // vertex index, dx, dy, dz (already divided)

  bool operator==(const SculptConfig&) const = default;
};

// Face of one NPC as the export writes it. Format 3 stores the COMPLETE state (every NAM9 slider,
// the NAMA presets, the whole PNAM list, tints, colors) so a reopened project rebuilds exactly the
// same FaceGen. Format 2 files stored partial edits; see `complete`.
struct FaceConfig {
  std::string preset_id;
  std::string preset_name;
  std::string preset_source;  // "donor", "jslot", "custom"
  std::string donor_npc_form_key;
  std::string donor_plugin;
  std::string donor_facegen_mesh;
  std::string donor_facegen_tint;
  std::optional<ColorRgb> hair_color;
  std::optional<ColorRgb> skin_tone;
  std::vector<HeadPartRef> head_parts;
  std::map<std::string, float> morph_sliders;  // NAM9 field name -> value
  std::vector<TintLayerConfig> tint_layers;
  float weight = 100.0f;
  float height = 1.0f;
  // ---- format 3
  bool complete = false;            // head_parts/morph_sliders/tints are the full record state
  std::vector<std::int32_t> face_presets;  // NAMA (4 values) when known
  std::map<std::string, float> custom_morphs;  // RaceMenu extension morphs baked into FaceGen
  std::vector<SculptConfig> sculpt;
  std::string geometry_mode;        // "regenerate" or "preserve" (keep the existing FaceGen's sculpt)
  std::string head_texture;         // FTST FormKey ("" = race default)
};

struct NpcEdit {
  NpcTarget target;
  std::optional<OutfitConfig> outfit;
  std::optional<FaceConfig> face;
};

struct ExportOptions {
  std::string plugin_name;
  bool esl_flag = false;
  bool create_zip = true;
  std::string output_directory;
};

struct HistoryEntry {
  std::string utc;
  std::string action;
};

struct Project {
  int format_version = kProjectFormatVersion;
  std::string id;
  std::string name;
  std::string created_utc;
  std::string modified_utc;
  EnvironmentIdentity environment;
  std::vector<NpcEdit> npcs;
  ExportOptions export_options;
  std::vector<std::string> dependencies;  // plugins referenced by the edits
  std::vector<HistoryEntry> history;
};

// A project is a folder "<Name>.snps/" with project.json, npc.json, outfit.json,
// dependencies.json, versions/ (previous saves) and exports/. Every file is written atomically.
class ProjectStore {
 public:
  static constexpr std::size_t kMaxVersions = 20;

  static void save(const std::filesystem::path& project_dir, Project& project);
  static Project load(const std::filesystem::path& project_dir);
  static Project create(std::string name, EnvironmentIdentity environment);
  static std::vector<std::string> list_versions(const std::filesystem::path& project_dir);
};

std::string utc_now_iso8601();

}  // namespace sns::projects
