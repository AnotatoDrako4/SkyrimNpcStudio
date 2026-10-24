#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sns/body/bcng_rules.hpp"
#include "sns/body/bodyslide_presets.hpp"

namespace sns::mo2 {
class VirtualFileSystem;
struct EffectiveLoadOrder;
}  // namespace sns::mo2

namespace sns::body {

// ---- NPC data as the worker's body.inspectNpcs returns it (protocol 1.7, camelCase) ----
struct RecordLink {
  std::string form_key;
  std::string editor_id;
  std::string name;
  [[nodiscard]] bool empty() const { return form_key.empty(); }
};

struct FactionLink {
  RecordLink faction;
  int rank = 0;
};

struct SkinAddon {
  std::string form_key;
  std::string editor_id;
  std::string race;
  std::vector<std::string> additional_races;
  std::string female_model;  // as stored in ARMA, relative to meshes\ ("Actors\Character\...\FemaleBody_1.nif")
  std::string male_model;
  std::vector<int> body_slots;
  bool applies_to_race = false;
};

struct HeadPartLink {
  std::string form_key;
  std::string editor_id;
  std::string part_type;
  std::string model_path;
};

struct NpcBodyInfo {
  std::string form_key;  // "0A2C8E:Skyrim.esm": the ActorBase identity
  std::string form_id;
  std::string editor_id;
  std::string name;
  std::string sex;            // own record
  std::string effective_sex;  // after a Traits template ("" = same as sex)
  std::string origin_plugin;
  std::string winning_plugin;
  bool is_unique = false;
  bool uses_template = false;
  bool template_unresolved = false;
  std::string traits_from;  // NPC that supplied race/sex/skin (itself when there is no Traits template)
  std::vector<std::string> template_flags;
  RecordLink race, effective_race, skin, effective_skin, race_skin, class_, combat_style;
  bool is_child = false;
  bool is_playable = false;
  std::vector<std::string> race_keywords;
  std::string skin_source;  // "npc" | "race" | "none"
  std::vector<RecordLink> keywords;
  std::vector<FactionLink> factions;
  std::vector<SkinAddon> skin_addons;
  std::vector<HeadPartLink> head_parts;
  std::string facegen_geometry;
  std::vector<std::string> notes;

  [[nodiscard]] std::string sex_for_rules() const { return effective_sex.empty() ? sex : effective_sex; }
  [[nodiscard]] const RecordLink& race_for_rules() const { return effective_race.empty() ? race : effective_race; }
  [[nodiscard]] std::string defining_plugin() const;  // the FormKey's plugin
};

NpcBodyInfo npc_body_from_json(const nlohmann::json& j);
nlohmann::json npc_body_to_json(const NpcBodyInfo& npc);

// ---- What the profile offers (computed once per MO2 snapshot) ----
struct BodyEnvironment {
  bcng::Settings settings;
  std::string settings_error;            // settings.json present but unreadable
  std::string bcng_dll_provider;         // "" = SKSE\Plugins\BodyChangeNG.dll not in the virtual Data
  std::string distribution_provider;     // winner of SKSE\Plugins\BodyChangeNGdistribution.json
  std::string distribution_text;         // its content ("" when absent)
  std::string obody_dll_provider;        // OBody NG active alongside BCNG = two controllers on the same actors
  std::string racemenu_dll_provider;     // skee64.dll (BodyMorph)
  std::string skypatcher_dll_provider;
  bool ube_race_plugin_active = false;   // UBE_AllRace.esp or another UBE race plugin in the active load order
  bool race_compatibility_active = false;
  unsigned installed_female_mask = 0;    // framework plugins in the active load order (BCNG evidence 5)
  std::map<std::string, bool> light_plugins;  // lower-case active plugin name -> ESL
};

using FileReader = std::function<std::string(const std::string& absolute_utf8_path)>;
BodyEnvironment inspect_environment(const mo2::VirtualFileSystem& vfs, const mo2::EffectiveLoadOrder* load_order,
                                    const FileReader& read_file = {});

// ---- Body mesh evidence and the NPC's detected family ----
struct MeshEvidence {
  std::string model;              // ARMA path as stored
  std::string data_path;          // "meshes\..." key looked up in the VFS
  std::string provider;           // winning MO2 provider, "" when the mesh is missing
  std::vector<std::string> shape_names;
  std::vector<std::string> diffuse_textures;
  std::string body_tri;           // BODYTRI value, "" = the mesh was not built with "Build Morphs"
  std::string body_tri_path;      // "meshes\..." key
  std::string body_tri_provider;  // "" = the TRI it names is not in the virtual Data
  std::string error;              // unreadable NIF
};

enum class TextureLayout { Unknown, Standard, Ube };

struct Issue {
  std::string severity;  // "error" (blocks), "warning", "info"
  std::string code;
  std::string message;
};

struct NpcBodyAnalysis {
  std::string form_key;
  bool female = false;
  bool child = false;
  bool beast = false;
  bool elder = false;               // BCNG: race EditorID contains "elder" (the voice part is not available offline)
  bool follower_candidate = false;  // non-vanilla defining plugin + Potential/CurrentFollowerFaction
  bool custom_appearance = false;   // FaceGen geometry provided by a mod (replacer)
  std::string facegen_provider;
  unsigned family_mask = 0;         // detected like BCNG ResolveActorFamily; 0 = unknown (BCNG: no filter)
  BodyFamily family = BodyFamily::Unknown;
  TextureLayout layout = TextureLayout::Unknown;
  std::vector<std::string> evidence;  // plain-language trail of the decision
  MeshEvidence body;                  // slot 32 addon of the skin valid for the race
  bool has_body_addon = false;
  std::vector<Issue> issues;
};

// Reads body NIFs through the VFS (cached per mesh path; thread-safe) and decides each NPC's family.
class BodyInspector {
 public:
  BodyInspector(const mo2::VirtualFileSystem& vfs, const BodyEnvironment& env, FileReader read_file = {});
  [[nodiscard]] NpcBodyAnalysis analyze(const NpcBodyInfo& npc) const;
  [[nodiscard]] MeshEvidence mesh(std::string_view arma_model) const;

 private:
  const mo2::VirtualFileSystem& vfs_;
  const BodyEnvironment& env_;
  FileReader read_file_;
  mutable std::mutex mutex_;
  mutable std::map<std::string, MeshEvidence> cache_;
};

// ---- Operation A: give the NPC a BodySlide preset of its own family ----
enum class Verdict { Compatible, NeedsResources, Experimental, Blocked };
std::string_view verdict_id(Verdict v);     // "compatible", "needs-resources", "experimental", "blocked"
std::string_view verdict_label(Verdict v);  // "Compatible", "Requiere recursos adicionales", "Experimental", "Bloqueada"

struct Assessment {
  Verdict verdict = Verdict::Blocked;
  std::vector<Issue> issues;
};

// `automatic`: the preset would be handed out by a group rule (stricter: unknown families are refused).
Assessment assess_preset(const NpcBodyAnalysis& npc, const BodySlidePreset& preset, const PresetLibrary& library,
                         const BodyEnvironment& env, bool automatic);

// ---- Operation B: change the NPC's body family (3BA <-> UBE). Never a preset assignment. ----
Assessment assess_family_change(const NpcBodyAnalysis& npc, BodyFamily target, const BodyEnvironment& env);

}  // namespace sns::body
