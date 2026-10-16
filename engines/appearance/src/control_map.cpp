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

// Index of the version that set the winning value of `field`: the last one whose value differs from
// the version before it; the origin (index 0) when no override changed it.
std::size_t controller_of(const std::vector<OverrideLink>& chain, const std::string& field) {
  for (std::size_t i = chain.size(); i-- > 1;) {
    if (contains(chain[i].changed_fields, field)) return i;
  }
  return 0;
}

std::string link_label(const OverrideLink& link) { return link.plugin + " (" + link.mod + ")"; }

std::string quoted(const std::string& s) { return "«" + s + "»"; }

}  // namespace

ControlReport analyze_appearance_control(const ControlInputs& in) {
  ControlReport report;
  const auto& chain = in.chain;

  // ---- record parts
  if (!chain.empty()) {
    for (const auto& group : record_groups()) {
      std::size_t controller = 0;
      for (const auto& f : group.fields) controller = std::max(controller, controller_of(chain, f));
      ComponentControl c{group.key, group.label, chain[controller].plugin, chain[controller].mod, "record", {}};
      for (std::size_t i = 0; i < controller; ++i) {
        const bool changed = i == 0 || std::any_of(group.fields.begin(), group.fields.end(),
                                                   [&](const std::string& f) { return contains(chain[i].changed_fields, f); });
        if (changed) c.overridden.push_back(link_label(chain[i]));
      }
      report.components.push_back(std::move(c));
    }
    std::size_t face = 0;
    for (const auto& f : geometry_fields()) face = std::max(face, controller_of(chain, f));
    report.face_plugin = chain[face].plugin;
    report.face_mod = chain[face].mod;
  }

  // ---- face files
  auto file_component = [&](const char* key, const char* label, const AssetSource& a) {
    ComponentControl c{key, label, {}, a.found ? a.winner_mod : std::string(), a.found ? a.kind : std::string("missing"), {}};
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

  // ---- FaceGen present and from the mod that set the face data
  if (!in.facegen.found && !in.facegen_expected) {
    finding("ok", "FaceGenNotUsed", "El NPC no usa FaceGen propio (toma los rasgos de una plantilla o su raza no usa FaceGen).",
            "El juego toma la cabeza de la plantilla o de la raza.", "");
  } else if (!in.facegen.found) {
    finding("warning", "NoFaceGen", "El NPC no tiene FaceGen en el perfil.",
            "Skyrim construye la cabeza al cargar el NPC; con frecuencia sale la «cara oscura» o un rostro distinto.",
            "Exporta el rostro con el estudio (genera un FaceGen coherente con el registro) o instala el FaceGen del mod que cambia su cara.");
  } else if (!report.face_mod.empty() && report.face_mod != in.facegen.winner_mod) {
    // The face plugin is a file-for-file replacement (translation, cleaned copy) of a plugin that the
    // FaceGen mod ships: the FaceGen belongs to that plugin; only its winning copy comes from elsewhere.
    const auto face_link = std::find_if(chain.rbegin(), chain.rend(), [&](const OverrideLink& l) { return l.plugin == report.face_plugin; });
    const bool replaced_copy = face_link != chain.rend() && contains(face_link->plugin_mods, in.facegen.winner_mod);
    // Geometry fields whose value comes from another mod than the FaceGen. Only head parts, all of them
    // present in the FaceGen: the head the game shows has the record's pieces.
    std::vector<std::string> foreign;
    for (const auto& f : geometry_fields()) {
      if (chain[controller_of(chain, f)].mod != in.facegen.winner_mod) foreign.push_back(f);
    }
    const bool head_parts_only = foreign == std::vector<std::string>{"HeadParts"} && in.facegen_has_record_head_parts.value_or(false);
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
      const auto facegen_plugin = std::find_if(chain.begin(), chain.end(), [&](const OverrideLink& l) { return l.mod == in.facegen.winner_mod; });
      if (facegen_plugin != chain.end()) {
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
              "Los datos faciales del registro los fija " + quoted(report.face_plugin) + " (" + report.face_mod +
                  ") y la geometría FaceGen la aporta " + quoted(in.facegen.winner_mod) + deviation_text + ".",
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
    const bool by_facegen_mod = !chain.empty() && in.facegen.found && chain[controller_of(chain, f)].mod == in.facegen.winner_mod;
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
