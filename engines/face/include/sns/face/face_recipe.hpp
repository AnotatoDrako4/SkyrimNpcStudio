#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sns/face/face_morphs.hpp"
#include "sns/face/head_parts.hpp"

namespace sns::face {

struct Rgb8 {
  std::uint8_t r = 255, g = 255, b = 255;
  bool operator==(const Rgb8&) const = default;
};

// HDPT NAM0: what a head part's TRI file is used for.
enum class TriKind : int { Race = 0, Expression = 1, Chargen = 2 };

struct TriRef {
  TriKind kind = TriKind::Chargen;
  std::string path;  // Data-relative "meshes\..."
  bool operator==(const TriRef&) const = default;
};

// A head part as the game resolves it for one NPC (winning HDPT record).
struct HeadPartData {
  std::string form_key;
  std::string editor_id;  // also the shape name inside FaceGen
  HeadPartType type = HeadPartType::Misc;
  std::string model;      // Data-relative "meshes\..." (empty: the part has no geometry, e.g. scars)
  std::vector<TriRef> tris;
  // HDPT TNAM resolved to its TXST paths ("textures\..."), slots 0..8; nullopt = keep the NIF's own.
  std::optional<std::array<std::string, 9>> texture_set;
  bool extra = false;       // reached through another part's "Extra Parts" list
  std::string plugin;       // plugin of the winning HDPT record
  bool official = false;    // Bethesda master or Creation Club (assets every install has)
  std::string valid_sex = "any";  // HDPT flags: "male", "female" or "any"
  bool valid_for_race = true;     // the HDPT valid-races list contains the NPC's race
  bool operator==(const HeadPartData&) const = default;
};

// One race tint mask (RACE TINI/TINT/TINP) of the NPC's sex.
struct TintMaskDef {
  std::uint16_t index = 0;
  std::string mask_path;  // "textures\..."
  std::uint16_t mask_type = 0;  // TINP; 6 = skin tone
  bool operator==(const TintMaskDef&) const = default;
};
inline constexpr std::uint16_t kTintMaskSkinTone = 6;

// One NPC tint layer (TINI/TINC/TINV/TIAS).
struct TintLayer {
  std::uint16_t index = 0;
  Rgb8 color;
  float value = 1.0f;  // TINV / 100
  std::int32_t preset = -1;
  bool operator==(const TintLayer&) const = default;
};

// RaceMenu sculpt data for one head part, addressed by the part's chargen TRI ("host").
struct SculptTarget {
  std::string host;  // chargen TRI path as RaceMenu stores it ("Actors\...\FemaleHeadCharGen.tri")
  std::uint32_t vertex_count = 0;
  std::vector<std::pair<std::uint32_t, std::array<float, 3>>> deltas;  // already divided by sculptDivisor
  bool operator==(const SculptTarget&) const = default;
};

// RaceMenu "facegenmorphs" extension: extra TRI files whose morphs extend a host chargen TRI.
struct MorphExtension {
  std::string host;  // chargen TRI as written in morphs.ini (without "meshes\")
  std::string tri;   // Data-relative "meshes\actors\character\facegenmorphs\morphs\..."
  bool operator==(const MorphExtension&) const = default;
};

// Everything the user can change about a face. Matches the NPC_ record fields one to one, plus the
// RaceMenu-only data (custom morphs and sculpt) that can be baked into FaceGen but has no record.
struct FaceValues {
  std::array<float, kSliderCount> sliders{};          // NAM9[0..17]
  std::array<std::int32_t, kPresetSlotCount> presets{{-1, -1, -1, -1}};  // NAMA
  float weight = 50.0f;                               // NAM7, 0..100
  Rgb8 skin_tone{255, 255, 255};                      // QNAM
  std::optional<Rgb8> hair_color;                     // color of the HCLF record
  std::vector<TintLayer> tints;
  std::map<std::string, float> custom_morphs;         // RaceMenu extension morphs (name -> weight)
  std::vector<SculptTarget> sculpt;
  bool operator==(const FaceValues&) const = default;
};

struct FaceRecipe {
  std::string npc_form_key;
  std::string source_plugin;       // record version the recipe was read from
  std::string facegen_mesh_path;   // "meshes\actors\character\facegendata\facegeom\<origin>\<id>.nif"
  std::string facegen_tint_path;   // "textures\actors\character\facegendata\facetint\<origin>\<id>.dds"
  bool female = false;
  float height = 1.0f;  // NPC height multiplier (body, not FaceGen)
  std::string race_form_key;
  std::string race_editor_id;
  std::string morph_race_editor_id;  // RACE NAM8 when set; the race morph uses it
  bool vampire = false;              // race has the Vampire keyword: "VampireMorph" is applied
  std::vector<HeadPartData> parts;   // effective head parts, in FaceGen shape order
  std::vector<std::string> npc_head_parts;  // PNAM list of the record (form keys), for round trips
  std::optional<std::array<std::string, 9>> face_texture_set;  // NPC FTST (or race default) TXST paths
  std::string face_texture_set_form_key;
  std::string npc_head_texture;  // the record's own FTST ("" = race default)
  std::vector<TintMaskDef> tint_masks;
  std::vector<MorphExtension> morph_extensions;  // RaceMenu morphs.ini entries, filled by the app
  FaceValues values;

  [[nodiscard]] const TintMaskDef* tint_mask(std::uint16_t index) const;
  [[nodiscard]] const HeadPartData* face_part() const;  // the part of type Face, if any
};

FaceRecipe recipe_from_json(const nlohmann::json& json);
nlohmann::json recipe_to_json(const FaceRecipe& recipe);
FaceValues values_from_json(const nlohmann::json& json);
nlohmann::json values_to_json(const FaceValues& values);

// Name of the race morph applied from race TRI files (morph race wins over the race itself).
std::string race_morph_name(const FaceRecipe& recipe);

}  // namespace sns::face
