#include "sns/body/body_analysis.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <set>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"

namespace sns::body {

using Json = nlohmann::json;

namespace {

constexpr std::uintmax_t kMaxConfigBytes = 4ull * 1024 * 1024;
constexpr std::uintmax_t kMaxMeshBytes = 64ull * 1024 * 1024;

std::string str(const Json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool flag(const Json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}

std::vector<std::string> strings(const Json& j, const char* key) {
  std::vector<std::string> out;
  const auto it = j.find(key);
  if (it == j.end() || !it->is_array()) return out;
  for (const auto& v : *it) {
    if (v.is_string()) out.push_back(v.get<std::string>());
  }
  return out;
}

RecordLink link(const Json& j, const char* key) {
  RecordLink r;
  const auto it = j.find(key);
  if (it == j.end() || !it->is_object()) return r;
  r.form_key = str(*it, "formKey");
  r.editor_id = str(*it, "editorId");
  r.name = str(*it, "name");
  return r;
}

Json link_json(const RecordLink& r) {
  if (r.empty()) return nullptr;
  return Json{{"formKey", r.form_key}, {"editorId", r.editor_id}, {"name", r.name}};
}

// The plugins Bethesda ships (and Creation Club content): an NPC defined there is not a "custom follower".
bool vanilla_plugin(std::string_view plugin) {
  const auto p = text::to_lower_ascii(plugin);
  return p == "skyrim.esm" || p == "update.esm" || p == "dawnguard.esm" || p == "hearthfires.esm" || p == "dragonborn.esm" ||
         p.starts_with("cc");
}

bool contains_icase(std::string_view hay, std::string_view needle) {
  return text::to_lower_ascii(hay).find(text::to_lower_ascii(needle)) != std::string::npos;
}

// BCNG's preset classifier (Tokenize/DetectText) applied to an arbitrary text: one evidence at a time.
FamilyGuess classify_text(std::string_view text) {
  BodySlidePreset probe;
  probe.set = std::string(text);
  auto g = classify_family(probe);
  if (g.metadata_fallback) return {};  // only the "set" evidence counts; the empty name/file never decide
  return g;
}

std::string meshes_key(std::string_view model) {
  auto path = std::string(text::trim(model));
  std::replace(path.begin(), path.end(), '/', '\\');
  while (!path.empty() && path.front() == '\\') path.erase(0, 1);
  if (text::to_lower_ascii(path).starts_with("data\\")) path.erase(0, 5);
  if (!text::to_lower_ascii(path).starts_with("meshes\\")) path = "meshes\\" + path;
  return path;
}

void worsen(Assessment& a, Verdict v) {
  if (static_cast<int>(v) > static_cast<int>(a.verdict)) a.verdict = v;
}

void add(Assessment& a, Verdict v, std::string severity, std::string code, std::string message) {
  worsen(a, v);
  a.issues.push_back({std::move(severity), std::move(code), std::move(message)});
}

// Bones only physics bodies carry: 3BA and UBE both have them, plain CBBE/vanilla does not.
bool physics_bone(std::string_view bone) {
  static constexpr std::string_view kMarkers[] = {"Breast01", "NPC L Butt", "NPC R Butt", "NPC Belly", "FrontThigh", "RearThigh"};
  return std::any_of(std::begin(kMarkers), std::end(kMarkers), [&](auto m) { return bone.find(m) != std::string_view::npos; });
}

}  // namespace

std::string NpcBodyInfo::defining_plugin() const {
  const auto colon = form_key.find(':');
  return colon == std::string::npos ? std::string() : form_key.substr(colon + 1);
}

NpcBodyInfo npc_body_from_json(const Json& j) {
  if (!j.is_object()) throw Error(ErrorKind::Parse, "body.inspectNpcs: un NPC no es un objeto JSON.");
  NpcBodyInfo n;
  n.form_key = str(j, "formKey");
  if (n.form_key.empty()) throw Error(ErrorKind::Parse, "body.inspectNpcs: NPC sin formKey.");
  n.form_id = str(j, "formId");
  n.editor_id = str(j, "editorId");
  n.name = str(j, "name");
  n.sex = str(j, "sex");
  n.effective_sex = str(j, "effectiveSex");
  n.origin_plugin = str(j, "originPlugin");
  n.winning_plugin = str(j, "winningPlugin");
  n.is_unique = flag(j, "isUnique");
  n.uses_template = flag(j, "usesTemplate");
  n.template_unresolved = flag(j, "templateUnresolved");
  n.traits_from = str(j, "traitsFrom");
  n.template_flags = strings(j, "templateFlags");
  n.race = link(j, "race");
  n.effective_race = link(j, "effectiveRace");
  n.skin = link(j, "skin");
  n.effective_skin = link(j, "effectiveSkin");
  n.race_skin = link(j, "raceSkin");
  n.class_ = link(j, "class");
  n.combat_style = link(j, "combatStyle");
  n.is_child = flag(j, "isChild");
  n.is_playable = flag(j, "isPlayable");
  n.race_keywords = strings(j, "raceKeywords");
  n.skin_source = str(j, "skinSource");
  if (const auto it = j.find("keywords"); it != j.end() && it->is_array()) {
    for (const auto& k : *it) n.keywords.push_back({str(k, "formKey"), str(k, "editorId"), {}});
  }
  if (const auto it = j.find("factions"); it != j.end() && it->is_array()) {
    for (const auto& f : *it) {
      FactionLink l;
      l.faction = {str(f, "formKey"), str(f, "editorId"), {}};
      l.rank = f.value("rank", 0);
      n.factions.push_back(std::move(l));
    }
  }
  if (const auto it = j.find("skinAddons"); it != j.end() && it->is_array()) {
    for (const auto& a : *it) {
      SkinAddon s;
      s.form_key = str(a, "formKey");
      s.editor_id = str(a, "editorId");
      s.race = str(a, "race");
      s.additional_races = strings(a, "additionalRaces");
      s.female_model = str(a, "femaleModel");
      s.male_model = str(a, "maleModel");
      if (const auto slots = a.find("bodySlots"); slots != a.end() && slots->is_array()) {
        for (const auto& v : *slots) {
          if (v.is_number_integer()) s.body_slots.push_back(v.get<int>());
        }
      }
      s.applies_to_race = flag(a, "appliesToRace");
      n.skin_addons.push_back(std::move(s));
    }
  }
  if (const auto it = j.find("headParts"); it != j.end() && it->is_array()) {
    for (const auto& h : *it) n.head_parts.push_back({str(h, "formKey"), str(h, "editorId"), str(h, "partType"), str(h, "modelPath")});
  }
  if (const auto it = j.find("faceGen"); it != j.end() && it->is_object()) n.facegen_geometry = str(*it, "geometryPath");
  n.notes = strings(j, "notes");
  return n;
}

Json npc_body_to_json(const NpcBodyInfo& n) {
  Json addons = Json::array();
  for (const auto& s : n.skin_addons) {
    addons.push_back({{"formKey", s.form_key}, {"editorId", s.editor_id}, {"race", s.race}, {"additionalRaces", s.additional_races},
                      {"femaleModel", s.female_model}, {"maleModel", s.male_model}, {"bodySlots", s.body_slots},
                      {"appliesToRace", s.applies_to_race}});
  }
  Json keywords = Json::array();
  for (const auto& k : n.keywords) keywords.push_back({{"formKey", k.form_key}, {"editorId", k.editor_id}});
  Json factions = Json::array();
  for (const auto& f : n.factions) factions.push_back({{"formKey", f.faction.form_key}, {"editorId", f.faction.editor_id}, {"rank", f.rank}});
  Json parts = Json::array();
  for (const auto& h : n.head_parts) {
    parts.push_back({{"formKey", h.form_key}, {"editorId", h.editor_id}, {"partType", h.part_type}, {"modelPath", h.model_path}});
  }
  return {{"formKey", n.form_key}, {"formId", n.form_id}, {"editorId", n.editor_id}, {"name", n.name}, {"sex", n.sex},
          {"effectiveSex", n.effective_sex}, {"originPlugin", n.origin_plugin}, {"winningPlugin", n.winning_plugin},
          {"isUnique", n.is_unique}, {"usesTemplate", n.uses_template}, {"templateUnresolved", n.template_unresolved},
          {"traitsFrom", n.traits_from}, {"templateFlags", n.template_flags}, {"race", link_json(n.race)},
          {"effectiveRace", link_json(n.effective_race)}, {"skin", link_json(n.skin)}, {"effectiveSkin", link_json(n.effective_skin)},
          {"raceSkin", link_json(n.race_skin)}, {"class", link_json(n.class_)}, {"combatStyle", link_json(n.combat_style)},
          {"isChild", n.is_child}, {"isPlayable", n.is_playable}, {"raceKeywords", n.race_keywords}, {"skinSource", n.skin_source},
          {"keywords", keywords}, {"factions", factions}, {"skinAddons", addons}, {"headParts", parts},
          {"faceGen", Json{{"geometryPath", n.facegen_geometry}}}, {"notes", n.notes}};
}

// ------------------------------------------------------------------ environment

DataLookup loose_lookup(const mo2::VirtualFileSystem& vfs, FileReader read_file) {
  return [&vfs, read_file = std::move(read_file)](std::string_view data_path, bool want_bytes) -> std::optional<DataFile> {
    const auto resolved = vfs.resolve(data_path);
    if (!resolved) return std::nullopt;
    const auto& provider = vfs.providers()[resolved->provider];
    DataFile f;
    f.provider = provider.name;
    f.game_data = provider.kind == mo2::ProviderKind::GameData;
    if (want_bytes) {
      f.bytes = read_file ? read_file(text::path_to_utf8(resolved->absolute)) : file_io::read_file(resolved->absolute, kMaxMeshBytes);
    }
    return f;
  };
}

BodyEnvironment inspect_environment(const mo2::VirtualFileSystem& vfs, const mo2::EffectiveLoadOrder* load_order,
                                    const FileReader& read_file) {
  BodyEnvironment env;
  auto provider_of = [&](std::string_view path) -> std::string {
    const auto r = vfs.resolve(path);
    return r ? vfs.providers()[r->provider].name : std::string();
  };
  auto read_text = [&](std::string_view path) -> std::optional<std::string> {
    const auto r = vfs.resolve(path);
    if (!r) return std::nullopt;
    const auto bytes = read_file ? read_file(text::path_to_utf8(r->absolute)) : file_io::read_file(r->absolute, kMaxConfigBytes);
    return text::decode_text(bytes);
  };

  env.bcng_dll_provider = provider_of("SKSE\\Plugins\\BodyChangeNG.dll");
  env.obody_dll_provider = provider_of("SKSE\\Plugins\\OBody.dll");
  env.racemenu_dll_provider = provider_of("SKSE\\Plugins\\skee64.dll");
  env.skypatcher_dll_provider = provider_of("SKSE\\Plugins\\SkyPatcher.dll");
  try {
    if (const auto settings = read_text(bcng::kSettingsPath)) env.settings = bcng::parse_settings(*settings);
  } catch (const std::exception& e) {
    env.settings_error = e.what();
  }
  env.distribution_provider = provider_of(bcng::kDistributionPath);
  try {
    if (const auto dist = read_text(bcng::kDistributionPath)) env.distribution_text = *dist;
  } catch (const std::exception&) {
    // An unreadable distribution file shows up later as a parse error of an empty text; the provider is still named.
  }

  if (load_order) {
    for (const auto& p : load_order->active) {
      const auto name = text::to_lower_ascii(p.name);
      env.light_plugins[name] = p.header ? p.header->is_light : text::ends_with_icase(name, ".esl");
      if (name.starts_with("ube_") && name.find("race") != std::string::npos) {
        env.ube_race_plugin_active = true;
        env.installed_female_mask |= kFamUbe;
      }
      if (name == "racecompatibility.esm") env.race_compatibility_active = true;
      if (name == "cbbe.esp" || name == "3bbb.esp") env.installed_female_mask |= kFamCbbe;
      if (name.find("bhunp") != std::string::npos) env.installed_female_mask |= kFamUnp;
    }
  }
  return env;
}

// ------------------------------------------------------------------ inspector

BodyInspector::BodyInspector(const BodyEnvironment& env, DataLookup lookup) : env_(env), lookup_(std::move(lookup)) {}

BodyInspector::BodyInspector(const mo2::VirtualFileSystem& vfs, const BodyEnvironment& env, FileReader read_file)
    : env_(env), lookup_(loose_lookup(vfs, std::move(read_file))) {}

MeshEvidence BodyInspector::mesh(std::string_view arma_model) const {
  const auto key = text::data_path_key(meshes_key(arma_model));
  {
    std::lock_guard lock(mutex_);
    if (const auto it = cache_.find(key); it != cache_.end()) return it->second;
  }
  MeshEvidence m;
  m.model = std::string(arma_model);
  m.data_path = meshes_key(arma_model);
  std::optional<DataFile> file;
  try {
    file = lookup_(m.data_path, true);
  } catch (const std::exception& e) {
    m.error = e.what();
  }
  if (file) {
    m.provider = file->provider;
    m.archived = file->archived;
    m.game_data = file->game_data;
    try {
      const auto& bytes = file->bytes.value_or(std::string());
      const auto parsed = nif::load_mesh(bytes, m.data_path);
      std::set<std::string> diffuse, bones;
      for (const auto& s : parsed.shapes) {
        m.shape_names.push_back(s.name);
        if (!s.textures[nif::Diffuse].empty()) diffuse.insert(s.textures[nif::Diffuse]);
        for (const auto& b : s.bones) {
          if (physics_bone(b)) bones.insert(b);
        }
      }
      m.diffuse_textures.assign(diffuse.begin(), diffuse.end());
      m.physics_bones.assign(bones.begin(), bones.end());
      for (const auto& extra : nif::string_extra_data(bytes, m.data_path)) {
        if (extra.name.starts_with("BODYTRI") && !extra.value.empty()) {
          m.body_tri = extra.value;
          m.body_tri_path = meshes_key(extra.value);
          break;
        }
      }
      if (!m.body_tri_path.empty()) {
        if (const auto tri = lookup_(m.body_tri_path, false)) m.body_tri_provider = tri->provider;
      }
    } catch (const std::exception& e) {
      m.error = e.what();
    }
  }
  std::lock_guard lock(mutex_);
  return cache_.emplace(key, std::move(m)).first->second;
}

NpcBodyAnalysis BodyInspector::analyze(const NpcBodyInfo& npc) const {
  NpcBodyAnalysis a;
  a.form_key = npc.form_key;
  a.female = text::iequals_ascii(npc.sex_for_rules(), "female");
  a.child = npc.is_child;
  a.beast = std::any_of(npc.race_keywords.begin(), npc.race_keywords.end(), [](const auto& k) { return text::iequals_ascii(k, "IsBeastRace"); });
  const auto& race = npc.race_for_rules();
  a.elder = contains_icase(race.editor_id, "elder");
  const bool follower_faction = std::any_of(npc.factions.begin(), npc.factions.end(), [](const auto& f) {
    return text::iequals_ascii(f.faction.editor_id, "PotentialFollowerFaction") || text::iequals_ascii(f.faction.editor_id, "CurrentFollowerFaction");
  });
  a.follower_candidate = follower_faction && !vanilla_plugin(npc.defining_plugin());

  auto issue = [&](std::string severity, std::string code, std::string message) {
    a.issues.push_back({std::move(severity), std::move(code), std::move(message)});
  };

  if (npc.template_unresolved) {
    issue("warning", "TemplateUnresolved", "La cadena de plantillas del NPC no se puede resolver sin el juego: la raza, el sexo y la piel pueden variar.");
  } else if (!npc.traits_from.empty() && npc.traits_from != npc.form_key) {
    a.evidence.push_back(std::format("Raza, sexo y piel heredados de la plantilla {}.", npc.traits_from));
  }

  if (!npc.facegen_geometry.empty()) {
    try {
      if (const auto fg = lookup_(npc.facegen_geometry, false); fg && !fg->game_data) {
        a.custom_appearance = true;
        a.facegen_provider = fg->provider;
        a.evidence.push_back(std::format("Rostro (FaceGen) aportado por «{}».", fg->provider));
      }
    } catch (const std::exception&) {
    }
  }

  // The body: the slot 32 addon of the skin that applies to the NPC's race.
  const SkinAddon* addon = nullptr;
  for (const auto& s : npc.skin_addons) {
    if (s.applies_to_race && std::find(s.body_slots.begin(), s.body_slots.end(), 32) != s.body_slots.end()) {
      addon = &s;
      break;
    }
  }
  if (!addon) {
    issue("warning", "NoBodyAddon",
          std::format("La piel {} no tiene una pieza del cuerpo (ranura 32) para la raza {}: no se puede saber qué cuerpo usa.",
                      npc.effective_skin.empty() ? npc.skin.editor_id : npc.effective_skin.editor_id, race.editor_id));
  } else {
    a.has_body_addon = true;
    auto model = a.female ? addon->female_model : addon->male_model;
    if (model.empty() && a.female && !addon->male_model.empty()) {
      model = addon->male_model;
      a.evidence.push_back("La pieza del cuerpo no tiene modelo femenino: el juego usa el masculino.");
    }
    if (model.empty()) {
      issue("warning", "NoBodyModel", std::format("La pieza {} no tiene ruta de modelo.", addon->editor_id));
    } else {
      a.body = mesh(model);
      if (a.body.provider.empty() && a.body.error.empty()) {
        issue("error", "BodyMeshMissing", std::format("La malla del cuerpo «{}» no está en el perfil.", a.body.data_path));
      } else if (!a.body.error.empty()) {
        issue("error", "BodyMeshUnreadable", std::format("No se pudo leer «{}»: {}", a.body.data_path, a.body.error));
      } else {
        a.evidence.push_back(std::format("Cuerpo «{}» de «{}»{}.", a.body.data_path, a.body.provider, a.body.archived ? " (dentro de un BSA)" : ""));
        if (a.body.body_tri.empty()) {
          issue("warning", "NoBodyTri", "La malla del cuerpo no se construyó con «Build Morphs» (sin BODYTRI): un preset no deformará este cuerpo en el juego.");
        } else if (a.body.body_tri_provider.empty()) {
          issue("error", "BodyTriMissing", std::format("La malla pide el TRI «{}», que no está en el perfil.", a.body.body_tri_path));
        }
      }
    }
  }

  // --- the family, evidence by evidence; the first that names a family decides, a later contradiction is reported ---
  unsigned mask = 0;
  bool male_mask = false;
  std::string decided_by;
  auto consider = [&](const std::string& text, const std::string& label) {
    if (text.empty()) return;
    const auto g = classify_text(text);
    if (g.mask == 0) return;
    if (mask == 0) {
      mask = g.mask;
      male_mask = g.male;
      decided_by = label;
      // BCNG names the whole CBBE group "CBBE 3BA"; whether this body is 3BA is decided below from its own evidence.
      a.evidence.push_back(std::format("{}: {}.", label, g.mask == kFamCbbe ? std::string("grupo CBBE / CBBE 3BA de Body Change NG") : mask_label(g.mask)));
    } else if ((mask & g.mask) == 0) {
      issue("warning", "FamilyEvidenceConflict",
            std::format("{} indica {}, pero {} indica {}: la familia no es segura.", label, mask_label(g.mask), decided_by, mask_label(mask)));
      mask |= g.mask;
    }
  };
  std::string shapes;
  for (const auto& s : a.body.shape_names) shapes += s + ' ';
  consider(shapes, std::format("Formas de la malla «{}»", shapes.empty() ? std::string() : shapes.substr(0, shapes.size() - 1)));
  consider(a.body.data_path + ' ' + a.body.body_tri, "Ruta de la malla y del TRI");
  consider(race.editor_id, std::format("Raza «{}»", race.editor_id));
  if (mask == 0 && a.female && std::popcount(env_.installed_female_mask & kFemaleFamilies) == 1 && a.has_body_addon && !a.body.provider.empty() && !a.body.archived) {
    mask = env_.installed_female_mask & kFemaleFamilies;
    decided_by = "framework instalado";
    a.evidence.push_back(std::format("La malla no nombra su cuerpo; el único framework corporal activo es {} (evidencia débil).", mask_label(mask)));
    issue("info", "FamilyFromFramework", "Familia deducida del único framework corporal activo, no de la malla.");
  }
  // A woman's body named by male families (or the other way round) is not a usable reading.
  if (mask != 0 && male_mask == a.female) {
    issue("warning", "FamilySexMismatch", std::format("La evidencia nombra {} para un NPC {}.", mask_label(mask), a.female ? "femenino" : "masculino"));
    mask = 0;
  }
  a.family_mask = mask;
  if (std::popcount(mask) > 1) {
    a.family = BodyFamily::Unknown;
  } else if (mask == kFamCbbe) {
    const auto names_3ba = contains_icase(shapes, "3ba") || contains_icase(shapes, "3bbb");
    if (names_3ba) {
      a.family = BodyFamily::Cbbe3ba;
    } else if (!a.body.physics_bones.empty()) {
      a.family = BodyFamily::Cbbe3ba;
      a.evidence.push_back(std::format("Tiene huesos de física de 3BA ({}...): CBBE 3BA.", a.body.physics_bones.front()));
    } else {
      a.family = BodyFamily::Cbbe;
      a.evidence.push_back("Sin huesos de física de 3BA (pecho, glúteos, vientre, muslos): CBBE sin 3BA.");
    }
  } else if (mask == kFamUbe) {
    a.family = BodyFamily::Ube;
  } else if (mask != 0) {
    a.family = BodyFamily::Other;
  }
  if (mask == 0) {
    issue("warning", "FamilyUnknown",
          "Ninguna evidencia nombra el cuerpo del NPC (cuerpo vanilla u otro sin nombre): solo se permiten asignaciones manuales experimentales.");
  }

  // Textures: UBE keeps its own UV layout and texture folders.
  for (const auto& t : a.body.diffuse_textures) {
    if (contains_icase(t, "\\ube\\") || contains_icase(t, "!ube") || contains_icase(t, "_ube")) {
      a.layout = TextureLayout::Ube;
      break;
    }
    a.layout = TextureLayout::Standard;
  }
  if (a.layout == TextureLayout::Ube && a.family != BodyFamily::Ube && a.family != BodyFamily::Unknown) {
    issue("warning", "TextureLayoutMismatch", "La malla usa texturas UBE pero su forma no es UBE: revise el cuerpo en el juego.");
  }
  if (a.child) issue("info", "Child", "Es un niño: Body Change NG no le aplica presets y el estudio no los asigna.");
  return a;
}

// ------------------------------------------------------------------ assessments

std::string_view verdict_id(Verdict v) {
  switch (v) {
    case Verdict::Compatible: return "compatible";
    case Verdict::NeedsResources: return "needs-resources";
    case Verdict::Experimental: return "experimental";
    case Verdict::Blocked: break;
  }
  return "blocked";
}

std::string_view verdict_label(Verdict v) {
  switch (v) {
    case Verdict::Compatible: return "Compatible";
    case Verdict::NeedsResources: return "Requiere recursos adicionales";
    case Verdict::Experimental: return "Experimental";
    case Verdict::Blocked: break;
  }
  return "Bloqueada";
}

Assessment assess_preset(const NpcBodyAnalysis& npc, const BodySlidePreset& preset, const PresetLibrary& library,
                         const BodyEnvironment& env, bool automatic) {
  Assessment a;
  a.verdict = Verdict::Compatible;
  const auto& fam = preset.family;
  if (preset.refit) add(a, Verdict::Blocked, "error", "RefitPreset", "Los presets «-Refit» son de ajuste de BodySlide: Body Change NG nunca los reparte.");
  if (npc.child) add(a, Verdict::Blocked, "error", "Child", "Un niño no recibe presets corporales.");
  if (fam.conflict) add(a, Verdict::Blocked, "error", "PresetFamilyConflict", std::format("El preset nombra familias femeninas y masculinas a la vez ({}).", fam.reason));
  if (fam.mask != 0 && fam.male == npc.female) {
    add(a, Verdict::Blocked, "error", "PresetSexMismatch", std::format("El preset es para {} ({}) y el NPC es {}.", fam.male ? "cuerpos masculinos" : "cuerpos femeninos",
                                                                         mask_label(fam.mask), npc.female ? "mujer" : "hombre"));
  }
  if (fam.mask != 0 && npc.family_mask != 0 && (fam.mask & npc.family_mask) == 0) {
    add(a, Verdict::Blocked, "error", "FamilyMismatch",
        std::format("Preset de {} sobre un cuerpo {}: sus sliders no corresponden a esta malla (cambiar de familia es otra operación).",
                    mask_label(fam.mask), mask_label(npc.family_mask)));
  }
  if (fam.mask == 0) {
    if (automatic) add(a, Verdict::Blocked, "error", "PresetUnclassified", "No se sabe para qué cuerpo es el preset: no se reparte automáticamente.");
    else add(a, Verdict::Experimental, "warning", "PresetUnclassified", std::format("No se sabe para qué cuerpo es el preset ({}).", fam.reason));
  } else if (std::popcount(fam.mask) > 1) {
    add(a, Verdict::Compatible, "info", "PresetMultiFamily", std::format("El preset sirve a varias familias ({}).", mask_label(fam.mask)));
  }
  if (npc.family_mask == 0) {
    if (automatic) add(a, Verdict::Blocked, "error", "BodyUnknown", "No se sabe qué cuerpo usa el NPC: no se le asigna nada automáticamente.");
    else add(a, Verdict::Experimental, "warning", "BodyUnknown", "No se sabe qué cuerpo usa el NPC: el resultado solo se sabrá en el juego.");
  } else if (std::popcount(npc.family_mask) > 1) {
    add(a, Verdict::Experimental, "warning", "BodyAmbiguous", std::format("La evidencia del cuerpo del NPC es contradictoria ({}).", mask_label(npc.family_mask)));
  }
  if (automatic && env.settings.female_mask() != 0 && npc.female && fam.mask != 0 && (fam.mask & env.settings.female_mask()) == 0) {
    add(a, Verdict::Blocked, "error", "GlobalFamilyMismatch",
        std::format("Body Change NG reparte automáticamente solo presets de {} (settings.json).", env.settings.female_label()));
  }
  for (const auto& i : npc.issues) {
    if (i.code == "BodyMeshMissing" || i.code == "BodyMeshUnreadable" || i.code == "BodyTriMissing" || i.code == "NoBodyTri") {
      add(a, Verdict::NeedsResources, i.severity == "error" ? "error" : "warning", i.code, i.message);
    }
    // The mesh never named its body: the family is only the profile's framework, so a match proves little.
    if (i.code == "FamilyFromFramework") {
      if (automatic) add(a, Verdict::Blocked, "error", i.code, "La familia del NPC solo se dedujo del framework instalado: no se reparte automáticamente.");
      else add(a, Verdict::Experimental, "warning", i.code, "La familia del NPC solo se dedujo del framework instalado, no de su malla.");
    }
  }
  if (env.bcng_dll_provider.empty()) {
    add(a, Verdict::NeedsResources, "error", "BcngMissing", "Body Change NG (SKSE\\Plugins\\BodyChangeNG.dll) no está en el perfil: nadie aplicaría la regla.");
  }
  if (!env.obody_dll_provider.empty() && !env.bcng_dll_provider.empty()) {
    add(a, Verdict::NeedsResources, "error", "TwoBodyControllers",
        std::format("OBody NG («{}») y Body Change NG están activos a la vez: dos controladores sobre los mismos NPC. Desactive uno.", env.obody_dll_provider));
  }
  if (env.racemenu_dll_provider.empty()) {
    add(a, Verdict::NeedsResources, "warning", "BodyMorphMissing", "RaceMenu (skee64.dll, BodyMorph) no está en el perfil: los sliders no se aplicarían.");
  }
  if (const auto* dup = library.duplicate_of(preset.name)) {
    add(a, Verdict::Compatible, "info", "DuplicateName",
        std::format("Hay {} presets llamados «{}»{}: la regla nombrará el archivo «{}».", dup->uids.size(), preset.name,
                    dup->identical ? " (idénticos)" : " con valores distintos", preset.source));
  }
  if (npc.custom_appearance) {
    add(a, Verdict::Compatible, "info", "CustomFace", std::format("El rostro viene de «{}»; el preset solo cambia el cuerpo.", npc.facegen_provider));
  }
  return a;
}

Assessment assess_family_change(const NpcBodyAnalysis& npc, BodyFamily target, const BodyEnvironment& env) {
  Assessment a;
  // Never "compatible": it swaps the race or body mesh, hands, feet and the neck seam of the head; only Skyrim proves it.
  a.verdict = Verdict::Experimental;
  a.issues.push_back({"warning", "FamilyChangeExperimental",
                      "Cambiar de familia corporal sustituye malla del cuerpo, manos, pies y la unión del cuello (y a menudo la raza). "
                      "No es asignar un preset y solo se puede confirmar dentro del juego."});
  if (target != BodyFamily::Cbbe3ba && target != BodyFamily::Ube) {
    add(a, Verdict::Blocked, "error", "TargetUnsupported", "Solo se evalúan cambios entre CBBE 3BA y UBE.");
    return a;
  }
  if (!npc.female) add(a, Verdict::Blocked, "error", "NotFemale", "CBBE 3BA y UBE son cuerpos femeninos.");
  if (npc.child) add(a, Verdict::Blocked, "error", "Child", "No se cambia el cuerpo de un niño.");
  const auto current = npc.family == BodyFamily::Cbbe ? BodyFamily::Cbbe3ba : npc.family;
  if (current == target) add(a, Verdict::Blocked, "error", "SameFamily", std::format("El NPC ya usa {}.", family_label(npc.family)));
  if (npc.family_mask == 0 || std::popcount(npc.family_mask) > 1) {
    add(a, Verdict::Blocked, "error", "BodyUnknown", "Primero hay que saber qué cuerpo usa el NPC.");
  }
  if (npc.beast) add(a, Verdict::Blocked, "error", "BeastRace", "Las razas bestiales tienen mallas propias: no se convierten entre 3BA y UBE.");
  if (target == BodyFamily::Ube) {
    if (!env.ube_race_plugin_active) {
      add(a, Verdict::NeedsResources, "error", "UbeRaceMissing",
          "UBE necesita su plugin de razas (UBE_AllRace.esp o un parche de raza) activo; no lo está en este perfil.");
    }
    if (npc.custom_appearance) {
      add(a, Verdict::Blocked, "error", "CustomHeadNeck",
          std::format("El rostro de «{}» se hizo para el cuello de su cuerpo actual; con UBE quedaría una costura.", npc.facegen_provider));
    }
  } else if ((env.installed_female_mask & kFamCbbe) == 0) {
    add(a, Verdict::NeedsResources, "error", "CbbeMissing", "CBBE 3BA (CBBE.esp / 3BBB.esp) no está activo en este perfil.");
  }
  if (!env.skypatcher_dll_provider.empty()) {
    a.issues.push_back({"info", "SkyPatcherPresent", "SkyPatcher está instalado: sirve para asignar armaduras convertidas, no la malla desnuda del cuerpo."});
  }
  return a;
}

OutfitMeshCompatibility assess_outfit_mesh_compatibility(
    const NpcBodyAnalysis& npc,
    std::string_view outfit_mesh_path,
    const BodyInspector& inspector) {
  OutfitMeshCompatibility comp;
  comp.mesh_path = std::string(outfit_mesh_path);
  if (comp.mesh_path.empty()) {
    comp.verdict = Verdict::Blocked;
    comp.issues.push_back({"error", "EmptyMeshPath", "No se proporcionó la ruta de la malla de la prenda."});
    return comp;
  }

  const auto ev = inspector.mesh(comp.mesh_path);
  if (ev.provider.empty() && ev.error.empty()) {
    comp.verdict = Verdict::NeedsResources;
    comp.issues.push_back({"error", "OutfitMeshMissing", std::format("La malla «{}» no se encontró en el perfil.", comp.mesh_path)});
    return comp;
  }
  if (!ev.error.empty()) {
    comp.verdict = Verdict::Blocked;
    comp.issues.push_back({"error", "OutfitMeshUnreadable", std::format("No se pudo leer la malla «{}»: {}", comp.mesh_path, ev.error)});
    return comp;
  }

  // Weight variants check: look for _0 and _1 counterpart
  const std::string lower_path = text::to_lower_ascii(comp.mesh_path);
  const bool has_0 = lower_path.find("_0.nif") != std::string::npos;
  const bool has_1 = lower_path.find("_1.nif") != std::string::npos;
  if (has_0 || has_1) {
    std::string counterpart = comp.mesh_path;
    if (has_1) {
      const auto pos = counterpart.rfind("_1.nif");
      if (pos != std::string::npos) counterpart.replace(pos, 6, "_0.nif");
    } else {
      const auto pos = counterpart.rfind("_0.nif");
      if (pos != std::string::npos) counterpart.replace(pos, 6, "_1.nif");
    }
    const auto counter_ev = inspector.mesh(counterpart);
    if (!counter_ev.provider.empty()) {
      comp.weight_variants_found = true;
    } else {
      comp.weight_variants_found = false;
      comp.issues.push_back({"warning", "SingleWeightVariant",
                             std::format("La prenda solo cuenta con la variante «{}»; falta «{}», por lo que no interpolará según el peso del NPC.",
                                         comp.mesh_path, counterpart)});
    }
  } else {
    // Single static mesh (no _0/_1 suffix)
    comp.weight_variants_found = false;
    comp.issues.push_back({"info", "StaticWeightMesh", "La prenda no usa el sufijo _0/_1 (malla estática de peso único)."});
  }

  // Morph TRI check
  if (!ev.body_tri.empty()) {
    comp.morph_tri_found = true;
    if (ev.body_tri_provider.empty()) {
      comp.issues.push_back({"warning", "OutfitTriMissing", std::format("La prenda pide el archivo de morph «{}», pero no está presente en el perfil.", ev.body_tri_path)});
    }
  } else {
    comp.morph_tri_found = false;
    comp.issues.push_back({"info", "NoMorphTri", "La prenda no incluye BODYTRI (no responderá a sliders de BodySlide)."});
  }

  // Classify mesh family based on shape names, path, and physics bones
  std::string all_text = comp.mesh_path + " " + ev.data_path + " " + ev.body_tri;
  for (const auto& s : ev.shape_names) all_text += " " + s;

  if (contains_icase(all_text, "ube")) {
    comp.mesh_family = BodyFamily::Ube;
  } else if (contains_icase(all_text, "3ba") || contains_icase(all_text, "3bbb") || !ev.physics_bones.empty()) {
    comp.mesh_family = BodyFamily::Cbbe3ba;
  } else if (contains_icase(all_text, "cbbe")) {
    comp.mesh_family = BodyFamily::Cbbe;
  } else if (contains_icase(all_text, "unp") || contains_icase(all_text, "himbo")) {
    comp.mesh_family = BodyFamily::Other;
  } else {
    comp.mesh_family = BodyFamily::Unknown;
  }

  // Cross-family evaluation with NPC body
  const auto npc_fam = npc.family == BodyFamily::Cbbe ? BodyFamily::Cbbe3ba : npc.family;
  const auto mesh_fam = comp.mesh_family == BodyFamily::Cbbe ? BodyFamily::Cbbe3ba : comp.mesh_family;

  if (comp.mesh_family != BodyFamily::Unknown && npc.family != BodyFamily::Unknown) {
    if (npc_fam == BodyFamily::Ube && mesh_fam == BodyFamily::Cbbe3ba) {
      comp.verdict = Verdict::Experimental;
      comp.issues.push_back({"warning", "FamilyMismatch",
                             "Prenda diseñada para CBBE/3BA sobre cuerpo UBE: puede presentar costuras y deformaciones visibles en cuello y miembros."});
    } else if (npc_fam == BodyFamily::Cbbe3ba && mesh_fam == BodyFamily::Ube) {
      comp.verdict = Verdict::Experimental;
      comp.issues.push_back({"warning", "FamilyMismatch",
                             "Prenda diseñada para UBE sobre cuerpo CBBE/3BA: topologías y mapeos UV incompatibles."});
    } else if (npc.family == BodyFamily::Cbbe && comp.mesh_family == BodyFamily::Cbbe3ba) {
      comp.issues.push_back({"info", "PhysicsMeshOnStaticBody",
                             "La prenda contiene huesos o física 3BA sobre un cuerpo CBBE estático."});
    }
  }

  return comp;
}

}  // namespace sns::body
