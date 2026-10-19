#include "sns/appearance/control_map.hpp"

#include <algorithm>
#include <cstdio>

namespace sns::appearance {
namespace {

struct Group {
  const char* key;
  const char* label;
  std::vector<std::string> fields;
};

const std::vector<Group>& record_groups() {
  static const std::vector<Group> groups = {
      {"face-shape", "Forma de la cara", {"FaceMorph", "FaceParts"}},
      {"head-parts", "Piezas de cabeza", {"HeadParts"}},
      {"face-texture", "Textura de cara", {"HeadTexture"}},
      {"skin-tint", "Tono de piel", {"TextureLighting", "TintLayers"}},
      {"hair-color", "Color de pelo", {"HairColor"}},
      {"race", "Raza y sexo", {"Race", "Sex"}},
      {"body", "Altura y peso", {"Height", "Weight"}},
      {"outfit", "Atuendo", {"DefaultOutfit", "SleepingOutfit", "WornArmor"}},
  };
  return groups;
}

// Fields the FaceGen geometry is built from.
const std::vector<std::string>& geometry_fields() {
  static const std::vector<std::string> fields = {"HeadParts", "FaceMorph", "FaceParts", "Race", "Weight"};
  return fields;
}

const std::vector<std::string>& face_fields() {
  static const std::vector<std::string> fields = {"HeadParts", "FaceMorph", "FaceParts", "Race",       "Weight",
                                                  "HeadTexture", "TextureLighting", "TintLayers", "HairColor"};
  return fields;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

// Index of the version that introduced the value `field` has at version `at`: the last one up to `at`
// whose value differs from the version before it (the origin when none did); a change that is a copy
// of an earlier version's value belongs to that version.
std::size_t introducer(const std::vector<OverrideLink>& chain, std::size_t at, const std::string& field) {
  for (std::size_t i = at + 1; i-- > 1;) {
    if (!contains(chain[i].changed_fields, field)) continue;
    const auto copy = chain[i].copied_from.find(field);
    if (copy == chain[i].copied_from.end()) return i;
    for (std::size_t j = i; j-- > 0;) {
      if (chain[j].plugin == copy->second) return j;
    }
    return i;  // the named plugin is not earlier in the chain: keep the version that wrote it
  }
  return 0;
}

// Index of the last version that changed `field` (the origin when none did), copy or not.
std::size_t writer_of(const std::vector<OverrideLink>& chain, const std::string& field) {
  for (std::size_t i = chain.size(); i-- > 1;) {
    if (contains(chain[i].changed_fields, field)) return i;
  }
  return 0;
}

// The version a field's value is attributed to: the one that introduced it, except that a value the
// mod of the effective FaceGen writes itself is that mod's, even if an earlier version had it too (a
// replacer whose record keeps some vanilla values built its FaceGen from them).
std::size_t controller_of(const std::vector<OverrideLink>& chain, const std::string& field, const std::string& facegen_mod) {
  if (chain.empty()) return 0;
  const auto writer = writer_of(chain, field);
  if (!facegen_mod.empty() && chain[writer].mod == facegen_mod) return writer;
  return introducer(chain, chain.size() - 1, field);
}

// Last version that changed any of `fields` (the origin when none did): the one that carries the value.
std::size_t last_writer(const std::vector<OverrideLink>& chain, const std::vector<std::string>& fields) {
  for (std::size_t i = chain.size(); i-- > 1;) {
    if (std::any_of(fields.begin(), fields.end(), [&](const std::string& f) { return contains(chain[i].changed_fields, f); })) return i;
  }
  return 0;
}

std::string link_label(const OverrideLink& link) { return link.plugin + " (" + link.mod + ")"; }

std::string joined(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& i : items) out += (out.empty() ? "" : ", ") + i;
  return out;
}

// Geometry fields whose value at the winner differs from the value at the last version provided by the
// effective FaceGen's mod (compared through the versions that introduced each value). Empty when that
// mod has no version of the record, or is the winner.
std::vector<std::string> diverged_geometry(const ControlInputs& in) {
  const auto& chain = in.chain;
  std::vector<std::string> out;
  if (chain.size() < 2 || !in.facegen.found || in.facegen.winner_mod.empty()) return out;
  std::size_t source = chain.size();
  for (std::size_t i = chain.size(); i-- > 0;) {
    if (chain[i].mod == in.facegen.winner_mod) { source = i; break; }
  }
  const std::size_t winner = chain.size() - 1;
  if (source >= winner) return out;
  for (const auto& f : geometry_fields()) {
    if (introducer(chain, source, f) != introducer(chain, winner, f)) out.push_back(f);
  }
  return out;
}

std::string quoted(const std::string& s) { return "«" + s + "»"; }

// «Usar el rostro de X»: X is the last version of the chain provided by the mod of the effective FaceGen.
// Offered when X is not the winner, race and sex already agree with X (they are never copied), and at
// least one face field of the winner differs from X's.
std::optional<FaceResolution> plan_resolution(const ControlInputs& in) {
  const auto& chain = in.chain;
  if (chain.size() < 2 || !in.facegen.found || in.facegen.winner_mod.empty()) return std::nullopt;
  std::size_t source = chain.size();
  for (std::size_t i = chain.size(); i-- > 0;) {
    if (chain[i].mod == in.facegen.winner_mod) { source = i; break; }
  }
  const std::size_t winner = chain.size() - 1;
  if (source >= winner) return std::nullopt;
  const auto same_value = [&](const std::string& f) { return introducer(chain, source, f) == introducer(chain, winner, f); };
  if (!same_value("Race") || !same_value("Sex")) return std::nullopt;
  FaceResolution r;
  for (const auto& f : kResolutionFields) {
    if (!same_value(f)) r.fields.push_back(f);
  }
  if (r.fields.empty()) return std::nullopt;
  r.source_plugin = chain[source].plugin;
  r.source_mod = chain[source].mod;
  r.winner_plugin = chain[winner].plugin;
  r.winner_mod = chain[winner].mod;
  r.load_after = {r.source_plugin, r.winner_plugin};
  r.label = "Usar el rostro de " + quoted(r.source_plugin);
  std::string fields;
  for (const auto& f : r.fields) fields += (fields.empty() ? "" : ", ") + f;
  r.detail = "Crea un parche que copia de " + quoted(r.source_plugin) + " solo los campos faciales (" + fields + ") sobre el registro de " +
             quoted(r.winner_plugin) + ", que conserva todo lo demás, y se carga después de ambos. No copia ni regenera FaceGen: el de " +
             quoted(r.source_mod) + " ya es el que usa el juego.";

  // What stays: every tracked non-face value the winner carries, by the version that wrote it.
  std::vector<std::vector<std::string>> kept(chain.size());
  std::vector<std::string> seen;
  for (const auto& link : chain) {
    for (const auto& f : link.changed_fields) {
      if (contains(kResolutionFields, f) || contains(seen, f)) continue;
      seen.push_back(f);
      if (const auto w = writer_of(chain, f); w > 0) kept[w].push_back(f);
    }
  }
  for (std::size_t i = 1; i < chain.size(); ++i) {
    if (kept[i].empty()) continue;
    std::string list;
    for (const auto& f : kept[i]) list += (list.empty() ? "" : ", ") + f;
    r.keeps.push_back(quoted(chain[i].plugin) + " (" + chain[i].mod + "): " + list);
  }
  // What stops applying: the value each copied field has now, and who wrote it.
  std::vector<std::string> own_fields;
  for (const auto& f : r.fields) {
    const auto w = writer_of(chain, f);
    std::string line = f + ": valor " + (w == 0 ? "original de " : "de ") + quoted(chain[w].plugin);
    if (const auto copy = chain[w].copied_from.find(f); copy != chain[w].copied_from.end()) {
      line += " (inferido: el mismo valor que " + quoted(copy->second) + ")";
    } else if (w > 0) {
      // A value this version introduced itself: discarding it is a choice, not a repair.
      if (!contains(r.discards_own_edits, chain[w].plugin)) r.discards_own_edits.push_back(chain[w].plugin);
      own_fields.push_back(f);
    }
    r.replaced.push_back(line);
  }
  if (!r.discards_own_edits.empty()) {
    std::string who, what;
    for (const auto& p : r.discards_own_edits) who += (who.empty() ? "" : ", ") + quoted(p);
    for (const auto& f : own_fields) what += (what.empty() ? "" : ", ") + f;
    r.pending.push_back("Decisión necesaria: " + who + " cambió por su cuenta " + what +
                        " sin traer FaceGen propio; puede ser una corrección intencionada. En el juego se ve el FaceGen de " +
                        quoted(r.source_mod) + "; el parche hace que el registro coincida con él descartando esos cambios.");
  }
  // Other faces the user could prefer: mods whose loose FaceGen loses in MO2 and that have a version of the NPC.
  for (const auto& m : in.facegen.loose_mods) {
    if (m == in.facegen.winner_mod) continue;
    const auto other = std::find_if(chain.begin(), chain.end(), [&](const OverrideLink& l) { return l.mod == m; });
    if (other != chain.end()) {
      r.alternatives.push_back(quoted(m) + " (" + quoted(other->plugin) + ") también trae FaceGen para este NPC, pero pierde en MO2");
    }
  }
  r.explanation = "Se usará el rostro de " + quoted(r.source_mod) + " (" + quoted(r.source_plugin) + ") y se conservará el resto del registro de " +
                  quoted(r.winner_plugin);
  if (!kept.back().empty()) {
    std::string list;
    for (const auto& f : kept.back()) list += (list.empty() ? "" : ", ") + f;
    r.explanation += ", incluidos sus cambios de " + list;
  }
  r.explanation += ".";
  return r;
}

}  // namespace

std::size_t field_controller(const std::vector<OverrideLink>& chain, const std::string& field, const std::string& facegen_mod) {
  return controller_of(chain, field, facegen_mod);
}

ControlReport analyze_appearance_control(const ControlInputs& in) {
  ControlReport report;
  const auto& chain = in.chain;
  const std::string facegen_mod = in.facegen.found ? in.facegen.winner_mod : std::string();

  // ---- record parts
  if (!chain.empty()) {
    for (const auto& group : record_groups()) {
      std::size_t controller = 0;
      for (const auto& f : group.fields) controller = std::max(controller, controller_of(chain, f, facegen_mod));
      ComponentControl c{group.key, group.label, chain[controller].plugin, chain[controller].mod, "record", {}, {}};
      const auto carrier = last_writer(chain, group.fields);
      if (carrier != controller) c.via = chain[carrier].plugin;
      // Every other version that set these fields lost: the earlier ones and, when the value is a copy,
      // the ones between the original and the copy (only copies of the controller's value are not lost).
      for (std::size_t i = 0; i < chain.size(); ++i) {
        if (i == controller) continue;
        const bool changed = std::any_of(group.fields.begin(), group.fields.end(), [&](const std::string& f) {
          return contains(chain[i].changed_fields, f) && chain[i].copied_from.count(f) == 0;
        });
        if ((i == 0 && i < controller) || changed) c.overridden.push_back(link_label(chain[i]));
      }
      report.components.push_back(std::move(c));
    }
    std::size_t face = 0;
    for (const auto& f : geometry_fields()) face = std::max(face, controller_of(chain, f, facegen_mod));
    report.face_plugin = chain[face].plugin;
    report.face_mod = chain[face].mod;
    if (const auto carrier = last_writer(chain, geometry_fields()); carrier != face) report.face_via = chain[carrier].plugin;
  }

  // ---- face files
  auto file_component = [&](const char* key, const char* label, const AssetSource& a) {
    ComponentControl c{key, label, {}, a.found ? a.winner_mod : std::string(), a.found ? a.kind : std::string("missing"), {}, {}};
    for (const auto& m : a.loose_mods) {
      if (m != a.winner_mod) c.overridden.push_back(m);
    }
    report.components.push_back(std::move(c));
  };
  file_component("facegen", "FaceGen (geometría)", in.facegen);
  file_component("facetint", "FaceGen (tinte)", in.facetint);

  auto finding = [&](const char* severity, const char* code, std::string message, std::string consequence, std::string suggestion) {
    report.findings.push_back({severity, code, std::move(message), std::move(consequence), std::move(suggestion)});
  };
  char deviation_text[96] = "";
  if (in.regeneration_deviation) std::snprintf(deviation_text, sizeof(deviation_text), " (desviación medida %.3f)", *in.regeneration_deviation);
  const bool facegen_matches_record = in.regeneration_deviation && *in.regeneration_deviation < kFaceGenMatchesRecord;

  // ---- The effective FaceGen was opened and could not be parsed (audit F-11: it no longer throws; say why)
  if (in.facegen.found && in.facegen_readable == false) {
    finding("warning", "FaceGenUnreadable",
            "El FaceGen que carga el juego (" + in.facegen.path + ", de " + quoted(in.facegen.winner_mod) +
                ") está dañado o no es un NIF de Skyrim.",
            "En el juego la cabeza puede salir sin geometría o con la «cara oscura», o el juego puede cerrarse al cargar el NPC.",
            "Reinstala o actualiza el mod que lo aporta; si sigue igual, exporta el rostro con el estudio para regenerar el FaceGen.");
  }

  // ---- FaceGen present and from the mod that set the face data
  if (!in.facegen.found && !in.facegen_expected) {
    finding("ok", "FaceGenNotUsed", "El NPC no usa FaceGen propio (toma los rasgos de una plantilla o su raza no usa FaceGen).",
            "El juego toma la cabeza de la plantilla o de la raza.", "");
  } else if (!in.facegen.found) {
    finding("warning", "NoFaceGen", "El NPC no tiene FaceGen en el perfil.",
            "Skyrim construye la cabeza al cargar el NPC; con frecuencia sale la «cara oscura» o un rostro distinto.",
            "Exporta el rostro con el estudio (genera un FaceGen coherente con el registro) o instala el FaceGen del mod que cambia su cara.");
  } else if (const auto diverged = diverged_geometry(in);
             (!report.face_mod.empty() && report.face_mod != in.facegen.winner_mod) || !diverged.empty()) {
    // diverged: the FaceGen's mod has a version of the record and some geometry field of the winner has
    // another value than that version (e.g. a later mod puts back the vanilla morphs but keeps the head
    // parts: the newest controller is still the FaceGen's mod, yet the face no longer matches it).
    // The face plugin is a file-for-file replacement (translation, cleaned copy) of a plugin that the
    // FaceGen mod ships: the FaceGen belongs to that plugin; only its winning copy comes from elsewhere.
    const auto face_link = std::find_if(chain.rbegin(), chain.rend(), [&](const OverrideLink& l) { return l.plugin == report.face_plugin; });
    const bool by_controller = !report.face_mod.empty() && report.face_mod != in.facegen.winner_mod;
    // Only when another mod controls the face: in the diverged case the face plugin is the FaceGen mod's own.
    const bool replaced_copy = by_controller && face_link != chain.rend() && contains(face_link->plugin_mods, in.facegen.winner_mod);
    // Geometry fields whose value comes from another mod than the FaceGen. Only head parts, all of them
    // present in the FaceGen: the head the game shows has the record's pieces.
    std::vector<std::string> foreign;
    for (const auto& f : geometry_fields()) {
      if (chain[controller_of(chain, f, facegen_mod)].mod != in.facegen.winner_mod) foreign.push_back(f);
    }
    const bool head_parts_only = (by_controller ? foreign : diverged) == std::vector<std::string>{"HeadParts"} &&
                                 in.facegen_has_record_head_parts.value_or(false);
    if (facegen_matches_record) {
      finding("ok", "FaceGenFromOtherModMatches",
              "La geometría FaceGen la aporta " + quoted(in.facegen.winner_mod) + " y los datos faciales " + quoted(report.face_plugin) +
                  " (" + report.face_mod + "), pero coinciden: el FaceGen es el que produce el registro" + deviation_text + ".",
              "Registro y cabeza encajan aunque vengan de mods distintos.", "");
    } else if (replaced_copy) {
      finding("info", "FaceGenFromReplacedPluginCopy",
              quoted(report.face_plugin) + " lo trae " + quoted(in.facegen.winner_mod) + " junto con su FaceGen, pero gana la copia de " +
                  quoted(report.face_mod) + " (traducción o copia del mismo archivo)" + deviation_text + ".",
              "Normalmente encaja: las traducciones no cambian los rostros. Si " + quoted(report.face_mod) +
                  " cambió la cara, el FaceGen del mod original ya no le corresponde.",
              "Si en el juego ves la cara oscura o un rostro distinto, exporta el rostro con el estudio para regenerar el FaceGen.");
    } else if (head_parts_only) {
      finding("info", "FaceGenHasRecordHeadParts",
              quoted(report.face_plugin) + " (" + report.face_mod + ") solo cambia las piezas de cabeza, y el FaceGen de " +
                  quoted(in.facegen.winner_mod) + " contiene todas las piezas del registro" + deviation_text + ".",
              "Forma de la cara, raza y peso vienen del mismo mod que el FaceGen; las piezas coinciden por nombre.",
              "Si una pieza se ve distinta en el juego (misma pieza con otra malla), exporta el rostro con el estudio.");
    } else {
      std::string suggestion;
      report.resolution = plan_resolution(in);
      const auto facegen_plugin = std::find_if(chain.begin(), chain.end(), [&](const OverrideLink& l) { return l.mod == in.facegen.winner_mod; });
      if (report.resolution) {
        suggestion = quoted(report.resolution->source_plugin) + " (de " + quoted(in.facegen.winner_mod) + ") trae los datos faciales de ese FaceGen, pero los anula " +
                     quoted(report.face_plugin) + ": usa el botón " + report.resolution->label +
                     " (un parche que copia solo sus campos faciales y conserva el resto del registro ganador) o cárgalo después.";
      } else if (facegen_plugin != chain.end()) {
        suggestion = quoted(facegen_plugin->plugin) + " (de " + quoted(in.facegen.winner_mod) + ") trae sus propios datos faciales, pero los anula " +
                     quoted(report.face_plugin) + ": cárgalo después (o usa un parche que copie sus campos faciales) para que registro y FaceGen sean del mismo mod.";
      } else if (contains(in.facegen.loose_mods, report.face_mod)) {
        suggestion = quoted(report.face_mod) + " trae su propio FaceGen para este NPC, pero " + quoted(in.facegen.winner_mod) +
                     " tiene más prioridad en MO2: coloca " + quoted(report.face_mod) + " por encima.";
      } else {
        suggestion = "Comprueba que el FaceGen de " + quoted(in.facegen.winner_mod) + " se generó para los datos de " + quoted(report.face_plugin) +
                     "; si no, exporta el rostro con el estudio para regenerarlo.";
      }
      finding("warning", "FaceGenRecordMismatch",
              by_controller
                  ? "Los datos faciales del registro los fija " + quoted(report.face_plugin) + " (" + report.face_mod +
                        (report.face_via.empty() ? std::string() : ", copiados por " + quoted(report.face_via)) +
                        ") y la geometría FaceGen la aporta " + quoted(in.facegen.winner_mod) + deviation_text + "."
                  : "El registro ganador no conserva los datos faciales de " + quoted(in.facegen.winner_mod) + ", que aporta el FaceGen: " +
                        joined(diverged) + " tienen otros valores" + deviation_text + ".",
              "Si no se hicieron juntos, la cabeza del juego no corresponde al registro: cara oscura, cuello de otro tono o un rostro distinto al del editor.",
              suggestion);
    }
  }

  // ---- FaceTint from the same mod as the geometry
  if (in.facegen.found && !in.facetint.found) {
    finding("info", "NoFaceTint", "Hay FaceGen pero no tinte FaceGen (.dds).",
            "La cara se ve solo con su textura base: puede verse más clara que el cuello o sin maquillaje.",
            "Instala el FaceTint del mismo mod que el FaceGen o exporta el rostro con el estudio.");
  } else if (in.facegen.found && in.facetint.found && in.facetint.winner_mod != in.facegen.winner_mod) {
    finding("warning", "FaceTintFromOtherMod",
            "El tinte FaceGen lo aporta " + quoted(in.facetint.winner_mod) + " y la geometría " + quoted(in.facegen.winner_mod) + ".",
            "El color de la cara puede no coincidir con el cuello ni con la geometría.",
            "Da prioridad en MO2 al tinte de " + quoted(in.facegen.winner_mod) + " (el mismo mod que la geometría).");
  }

  // ---- replacers whose face loses in MO2
  for (const auto& m : in.facegen.loose_mods) {
    if (m == in.facegen.winner_mod) continue;
    finding("info", "ShadowedFaceGen", quoted(m) + " trae FaceGen para este NPC, pero queda por debajo de " + quoted(in.facegen.winner_mod) + " en MO2.",
            "Su cara no se usa en el juego.",
            "Si es la cara que quieres, sube " + quoted(m) + " por encima de " + quoted(in.facegen.winner_mod) + " (y que su plugin fije los datos faciales).");
  }

  // ---- facial edits of an intermediate version that did not survive. When the version that took the
  // field over belongs to the mod that also ships the effective FaceGen, it is a complete face replacer
  // (record + head from one author): the earlier face is replaced on purpose. Otherwise the edit is lost
  // (typically a later patch made from an older copy of the record).
  std::string lost, replaced;
  for (const auto& f : in.lost_fields) {
    if (!contains(face_fields(), f)) continue;
    const bool by_facegen_mod = !chain.empty() && in.facegen.found && chain[controller_of(chain, f, facegen_mod)].mod == in.facegen.winner_mod;
    auto& list = by_facegen_mod ? replaced : lost;
    list += (list.empty() ? "" : ", ") + f;
  }
  if (!replaced.empty()) {
    finding("info", "FaceReplaced",
            quoted(report.face_plugin) + " sustituye el rostro que fijaban versiones anteriores (" + replaced + ").",
            "Se ve el rostro de " + quoted(in.facegen.winner_mod) + " con su propio FaceGen: es lo habitual con un replacer.",
            "Si prefieres el rostro de otro mod, mira los FaceGen anulados y el orden de carga.");
  }
  if (!lost.empty()) {
    finding("warning", "LostFaceEdits", "Campos faciales que cambió un plugin intermedio y el ganador no conserva: " + lost + ".",
            "Esa parte del rostro del mod intermedio no se ve en el juego.",
            "Revisa el orden de carga o crea un parche de compatibilidad para esos campos.");
  }

  // What the resolution leaves unresolved: every other warning of the map.
  if (report.resolution) {
    auto& r = *report.resolution;
    for (const auto& f : report.findings) {
      // Lost face edits are what the patch brings back: only those it does not copy stay pending.
      if (f.severity != "warning" || f.code == "FaceGenRecordMismatch" || f.code == "LostFaceEdits") continue;
      r.pending.push_back(f.message);
    }
    std::string unrecovered;
    for (const auto& f : in.lost_fields) {
      if (contains(face_fields(), f) && !contains(r.fields, f)) unrecovered += (unrecovered.empty() ? "" : ", ") + f;
    }
    if (!unrecovered.empty()) r.pending.push_back("Ediciones faciales perdidas que este parche no recupera: " + unrecovered + ".");
  }

  const bool warnings = std::any_of(report.findings.begin(), report.findings.end(), [](const ControlFinding& f) { return f.severity == "warning"; });
  if (!warnings && in.facegen.found && report.face_mod == in.facegen.winner_mod) {
    // Same author for both; a large deviation only means the head was sculpted beyond the record.
    const bool sculpted = in.regeneration_deviation && *in.regeneration_deviation >= kFaceGenMatchesRecord;
    finding("ok", "FaceSourcesConsistent",
            "Datos faciales y FaceGen vienen del mismo mod (" + quoted(report.face_mod) + ")" + deviation_text + ".",
            sculpted ? "Registro y cabeza son del mismo autor; el FaceGen lleva esculpido o morphs propios además del registro, "
                       "habitual en replacers."
                     : "Registro y cabeza encajan.",
            "");
  }
  return report;
}

}  // namespace sns::appearance
