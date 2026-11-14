#include "sns/appearance/npc_audit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

namespace sns::appearance {
namespace {

constexpr const char* kGameData = "<Game Data>";

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool iequals(const std::string& a, const char* b) {
  const std::string bs(b);
  return a.size() == bs.size() &&
         std::equal(a.begin(), a.end(), bs.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

std::string quoted(const std::string& s) { return "«" + s + "»"; }

std::string joined(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
  return out;
}

const ControlFinding* control_finding(const ControlReport& r, const char* code) {
  const auto it = std::find_if(r.findings.begin(), r.findings.end(), [&](const ControlFinding& f) { return f.code == code; });
  return it == r.findings.end() ? nullptr : &*it;
}

int severity_rank(const std::string& s) { return s == "alta" ? 0 : s == "media" ? 1 : 2; }

std::string part_label(const AuditHeadPart& p) { return p.editor_id.empty() ? p.form_key : p.editor_id + " (" + p.form_key + ")"; }

class Builder {
 public:
  explicit Builder(NpcAudit& out) : out_(out) {}

  AuditFinding& add(const char* code, const char* status, const char* severity, double confidence, const char* verification) {
    AuditFinding f;
    f.code = code;
    f.title = audit_code_title(code);
    f.status = status;
    f.severity = severity;
    f.confidence = confidence;
    f.verification = verification;
    out_.findings.push_back(std::move(f));
    return out_.findings.back();
  }

 private:
  NpcAudit& out_;
};

}  // namespace

std::string audit_code_title(const std::string& code) {
  static const std::map<std::string, std::string> titles = {
      {"HAIR_001", "Cabello requerido con malla ausente"},
      {"HAIR_002", "HeadPart de cabello roto o referencia inválida"},
      {"HAIR_003", "Cabello presente pero no visible en el visor"},
      {"HAIR_004", "Texturas de cabello ausentes"},
      {"HAIR_005", "Cabello sobrescrito por otro proveedor"},
      {"HAIR_006", "FaceGen y cabello del registro no coinciden"},
      {"FACE_001", "FaceGeom ausente"},
      {"FACE_002", "FaceTint ausente"},
      {"FACE_003", "Registro ganador y FaceGen no coinciden"},
      {"FACE_004", "FaceGeom y FaceTint de procedencia incompatible"},
      {"FACE_005", "Recurso facial dañado o inválido"},
      {"FACE_006", "Pieza de cabeza incompatible (raza, High Poly Head u otros recursos)"},
      {"FACE_007", "Posible problema de tintes o texturas faciales"},
      {"NPC_001", "Un override posterior revierte la apariencia"},
      {"NPC_002", "Masters faltantes"},
      {"NPC_003", "FaceGen asociado a un FormID o plugin anterior"},
      {"NPC_004", "Requiere comprobación visual en Skyrim"},
      {"HAIR_SMP_001", "Malla de cabello o peluca SMP requerida ausente"},
      {"HAIR_SMP_002", "XML de física SMP ausente (cabello visible sin física)"},
      {"HAIR_SMP_003", "El FaceGen perdió la referencia de física SMP"},
      {"HAIR_SMP_004", "Huesos SMP del FaceGen con otra pose (cabello desplazado o invisible)"},
      {"HAIR_SMP_005", "FaceGen incompatible con el cabello SMP instalado"},
      {"HAIR_SMP_006", "HeadPart de cabello SMP sobrescrito"},
      {"HAIR_SMP_007", "Peluca o cabello en tiempo de ejecución sin resolver"},
      {"HAIR_SMP_008", "Dependencia FSMP ausente o incompatible"},
      {"HAIR_SMP_009", "Cabello renderizado incorrectamente"},
      {"HAIR_SMP_010", "Calvicie observada en Skyrim sin causa confirmada"},
  };
  const auto it = titles.find(code);
  return it == titles.end() ? std::string() : it->second;
}

NpcAudit audit_npc_appearance(const AuditSubject& s, const ControlInputs& in, const ControlReport& control) {
  NpcAudit out;
  out.form_key = s.form_key;
  Builder b(out);

  // Regenerating from the record only loses nothing when no mod authored the head the game loads.
  out.facegen_regeneration_safe = !in.facegen.found || in.facegen.winner_mod == kGameData;

  // ---- record-level checks (apply to every NPC)
  if (!s.missing_masters.empty()) {
    auto& f = b.add("NPC_002", kStatusConfirmed, "alta", 1.0, kVerifyOffline);
    f.evidence = quoted(s.winning_plugin) + " necesita " + joined(s.missing_masters) + ", que no están en el orden de carga.";
    f.cause = "Falta un master del plugin que define el registro ganador.";
    f.action = "Instala o activa el master en MO2 (el estudio no cambia el perfil).";
    f.resource = joined(s.missing_masters);
    f.responsible = s.winning_plugin;
  }
  if (!s.stale_facegen_paths.empty()) {
    auto& f = b.add("NPC_003", kStatusSuspected, "baja", 0.5, kVerifyOffline);
    f.evidence = "FaceGen de este NPC bajo otra carpeta de plugin o FormID: " + joined(s.stale_facegen_paths) + ".";
    f.cause = "El plugin se compactó (ESL) o se renumeró y quedaron los archivos con el FormID anterior.";
    f.action = "Comprueba que el FaceGen vigente (" + in.facegen.path + ") es la versión actual; los antiguos no se cargan.";
    f.resource = s.stale_facegen_paths.front();
    f.responsible = s.defining_plugin;
  }

  if (s.uses_traits_template) out.exemptions.push_back("plantilla-traits");
  if (!s.race_uses_facegen) out.exemptions.push_back("raza-sin-facegen");
  if (s.chargen_preset) out.exemptions.push_back("preset-chargen");
  if (s.is_player) out.exemptions.push_back("jugador");
  const bool head_exempt = s.uses_traits_template || !s.race_uses_facegen || s.is_player;

  if (!head_exempt) {
    const bool facegen_readable = in.facegen.found && in.facegen_readable != false;

    // ---- hair
    bool has_hair = false;
    for (const auto& p : s.head_parts) {
      const bool is_hair = iequals(p.type, "Hair");
      // An unresolved head part has no type: it may well be the hair (the usual bald case after a removed mod).
      if (!p.record_resolved && (is_hair || p.type.empty())) {
        has_hair = true;
        auto& f = b.add("HAIR_002", kStatusConfirmed, "alta", is_hair ? 0.95 : 0.7, kVerifyInGame);
        f.evidence = "El registro usa la pieza " + part_label(p) + (is_hair ? " (cabello)" : " (tipo desconocido)") +
                     ", que no está en el orden de carga.";
        f.cause = "Se desinstaló o desactivó el mod que define la pieza, o su plugin cambió de FormID.";
        f.action = "Activa el mod de la pieza o recupera el HeadPart correcto desde un mod instalado (parche de reparación).";
        f.resource = p.form_key;
        f.responsible = p.plugin.empty() ? s.winning_plugin : p.plugin;
        continue;
      }
      if (!is_hair) continue;
      has_hair = true;

      if (!p.model_path.empty() && !p.mesh.found) {
        // With a FaceGen that carries the hair the game draws that copy; only without it is the head bald.
        // A FaceGen with another hair is drawn instead of this mesh: then the missing mesh hides nothing.
        const bool confirmed = !in.facegen.found || (p.in_facegen == false && s.facegen_has_hair != true);
        auto& f = b.add("HAIR_001", confirmed ? kStatusConfirmed : kStatusSuspected, confirmed ? "alta" : "baja", confirmed ? 0.9 : 0.5,
                        kVerifyInGame);
        f.evidence = "La malla de " + part_label(p) + " (" + p.model_path + ") no está en el perfil (ni suelta ni en BSA)" +
                     (!in.facegen.found      ? "; el NPC no tiene FaceGen, así que el juego no tiene otra copia."
                      : p.in_facegen == true ? "; el FaceGen efectivo sí contiene la pieza."
                                             : ".");
        f.cause = "El mod de cabello está desactivado, incompleto o su malla se renombró.";
        f.action = "Activa o reinstala " + quoted(p.record_mod) + "; si no está, cambia la pieza por un cabello instalado (parche de reparación).";
        f.resource = p.model_path;
        f.responsible = p.record_mod.empty() ? p.plugin : p.record_mod;
      }
      if (p.mesh.found && p.mesh.kind == "loose" && !p.record_mod.empty() && contains(p.mesh.loose_mods, p.record_mod) &&
          p.mesh.winner_mod != p.record_mod) {
        auto& f = b.add("HAIR_005", kStatusSuspected, "baja", 0.6, kVerifyInGame);
        f.evidence = quoted(p.mesh.winner_mod) + " sobrescribe la malla de " + part_label(p) + " que trae " + quoted(p.record_mod) + " (" +
                     p.model_path + ").";
        f.cause = "Otro mod con más prioridad en MO2 trae un archivo con la misma ruta.";
        f.action = "Si el cabello se ve mal, da prioridad en MO2 a " + quoted(p.record_mod) + " o retira la copia de " + quoted(p.mesh.winner_mod) + ".";
        f.resource = p.model_path;
        f.responsible = p.mesh.winner_mod;
      }
      std::vector<std::string> missing_textures;
      for (const auto& t : p.textures) {
        if (!t.found && !t.path.empty()) missing_textures.push_back(t.path);
      }
      if (!missing_textures.empty()) {
        auto& f = b.add("HAIR_004", kStatusConfirmed, "media", 0.85, kVerifyInGame);
        f.evidence = "Texturas de " + part_label(p) + " ausentes del perfil: " + joined(missing_textures) + ".";
        f.cause = "Falta el paquete de texturas del cabello o su ruta cambió.";
        f.action = "Instala las texturas de " + quoted(p.record_mod) + " (el cabello puede verse morado, negro o invisible).";
        f.resource = missing_textures.front();
        f.responsible = p.record_mod.empty() ? p.plugin : p.record_mod;
      }
      if (p.visible_in_viewer == false) {
        auto& f = b.add("HAIR_003", kStatusSuspected, "media", 0.5, kVerifyInGame);
        f.evidence = "El visor del estudio no dibujó geometría para " + part_label(p) + ".";
        f.cause = "Malla vacía, sin shapes válidos o con alfa total; el visor no reproduce todos los shaders del juego.";
        f.action = "Compruébalo en Skyrim antes de reparar.";
        f.resource = p.model_path;
        f.responsible = p.record_mod;
      }
      if (facegen_readable && p.in_facegen == false) {
        // Only a FaceGen without any hair proves a bald head; one with another hair draws that hair instead.
        const bool bald = s.facegen_has_hair == false;
        auto& f = b.add("HAIR_006", bald ? kStatusConfirmed : kStatusSuspected, bald ? "alta" : "media", bald ? 0.9 : 0.6, kVerifyInGame);
        f.evidence = "El FaceGen efectivo (" + in.facegen.path + ", de " + quoted(in.facegen.winner_mod) + ") no contiene el cabello " +
                     part_label(p) + " que pide el registro" +
                     (bald ? " y no tiene ningún cabello."
                           : s.facegen_foreign_hair_shapes.empty() ? "." : "; trae en su lugar " + joined(s.facegen_foreign_hair_shapes) + ".");
        f.cause = bald ? "El FaceGen se generó sin cabello; el juego dibuja la cabeza del FaceGen: el NPC sale calvo."
                       : "El FaceGen se hizo con otro peinado (de otra versión del registro); el juego muestra el del FaceGen.";
        f.action = "Usa el FaceGen del mod que fijó el cabello o regenera el FaceGen (solo si es seguro para este NPC).";
        f.resource = in.facegen.path;
        f.responsible = in.facegen.winner_mod;
      }
    }
    // Hair shapes of a FaceGen whose record lists no hair (the per-part finding above names them otherwise).
    if (!has_hair && !s.facegen_foreign_hair_shapes.empty()) {
      has_hair = true;
      auto& f = b.add("HAIR_006", kStatusSuspected, "media", 0.6, kVerifyInGame);
      f.evidence = "El FaceGen efectivo (" + in.facegen.path + ") trae el cabello " + joined(s.facegen_foreign_hair_shapes) +
                   ", que el registro no usa.";
      f.cause = "El FaceGen se hizo para otra versión del registro.";
      f.action = "Comprueba qué peinado se ve en el juego; el registro y el FaceGen deberían venir del mismo mod.";
      f.resource = in.facegen.path;
      f.responsible = in.facegen.winner_mod;
    }
    if (!has_hair) out.exemptions.push_back("calvo-intencional");

    // ---- head parts the race cannot use
    for (const auto& p : s.head_parts) {
      if (!p.record_resolved || p.valid_for_race != false) continue;
      auto& f = b.add("FACE_006", kStatusConfirmed, "media", 0.9, kVerifyInGame);
      f.evidence = "Pieza " + part_label(p) + ": la raza del NPC no está en su lista de razas válidas.";
      f.cause = "La pieza es de otro sistema de cabeza (High Poly Head, otra raza).";
      f.action = "Usa una pieza válida para la raza o el FaceGen hecho con estas piezas.";
      f.resource = p.form_key;
      f.responsible = p.plugin;
    }

    // ---- record head parts in the FaceGen (shapes are named after the head part EditorIDs). Parts the FaceGen
    // lacks prove it was made for other parts -- unless none matches, which may be another naming convention.
    std::vector<std::string> absent, present;
    for (const auto& p : s.head_parts) {
      if (!p.record_resolved || p.model_path.empty() || !p.in_facegen) continue;
      (*p.in_facegen ? present : absent).push_back(p.editor_id.empty() ? p.form_key : p.editor_id);
    }
    const bool shapes_prove = facegen_readable && !absent.empty() && !present.empty();
    const std::string shape_evidence =
        facegen_readable && !absent.empty()
            ? "El FaceGen efectivo no contiene piezas del registro: " + joined(absent) +
                  (present.empty() ? " (ninguna pieza del registro coincide por nombre)." : " (sí contiene " + joined(present) + ").")
            : std::string();

    // ---- face files (from the control map)
    if (control_finding(control, "FaceGenUnreadable")) {
      auto& f = b.add("FACE_005", kStatusConfirmed, "alta", 0.95, kVerifyInGame);
      f.evidence = control_finding(control, "FaceGenUnreadable")->message;
      f.cause = "Archivo truncado, de otra versión del juego (LE) o que no es un NIF.";
      f.action = control_finding(control, "FaceGenUnreadable")->suggestion;
      f.resource = in.facegen.path;
      f.responsible = in.facegen.winner_mod;
      f.control_code = "FaceGenUnreadable";
    }
    if (const auto* c = control_finding(control, "NoFaceGen"); c && !s.chargen_preset) {
      const bool unplaced = s.placed_references == 0;
      auto& f = b.add("FACE_001", kStatusConfirmed, unplaced ? "baja" : "alta", 0.85, kVerifyInGame);
      f.evidence = "No existe " + in.facegen.path + " (ni suelto ni en BSA) y el NPC lo necesita" +
                   (unplaced ? "; no tiene referencias colocadas en el mundo (solo aparece por script o lista, o no se usa)." : ".");
      f.cause = "El mod que cambió la cara no trae FaceGen o está desactivado.";
      f.action = c->suggestion;
      f.resource = in.facegen.path;
      f.responsible = control.face_plugin;
      f.control_code = c->code;
    }
    if (const auto* c = control_finding(control, "NoFaceTint"); c && !s.chargen_preset) {
      auto& f = b.add("FACE_002", kStatusConfirmed, "media", 0.8, kVerifyInGame);
      f.evidence = "Hay FaceGeom (" + quoted(in.facegen.winner_mod) + ") pero no existe " + in.facetint.path + ".";
      f.cause = "El paquete del rostro está incompleto.";
      f.action = c->suggestion;
      f.resource = in.facetint.path;
      f.responsible = in.facegen.winner_mod;
      f.control_code = c->code;
    }
    if (const auto* c = control_finding(control, "FaceGenRecordMismatch")) {
      const bool measured = in.regeneration_deviation && *in.regeneration_deviation >= kFaceGenMatchesRecord;
      const bool confirmed = measured || shapes_prove;
      auto& f = b.add("FACE_003", confirmed ? kStatusConfirmed : kStatusSuspected, "alta", confirmed ? 0.9 : 0.65, kVerifyInGame);
      f.evidence = c->message + (shape_evidence.empty() ? "" : " " + shape_evidence);
      f.cause = c->consequence;
      f.action = c->suggestion;
      f.resource = in.facegen.path;
      f.responsible = control.resolution ? control.resolution->winner_plugin : control.face_plugin;
      f.control_code = c->code;
    } else if (!shape_evidence.empty()) {
      auto& f = b.add("FACE_003", shapes_prove ? kStatusConfirmed : kStatusSuspected, "alta", shapes_prove ? 0.85 : 0.5, kVerifyInGame);
      f.evidence = shape_evidence;
      f.cause = "El FaceGen se generó para otras piezas de cabeza que las del registro ganador; el juego dibuja las del FaceGen.";
      f.action = "Usa el FaceGen hecho con el registro ganador o regenera el FaceGen (solo si es seguro para este NPC).";
      f.resource = in.facegen.path;
      f.responsible = in.facegen.winner_mod;
    }
    // Geometry and tint from different mods is only broken when one of them ships its own half of the pair
    // and loses it: a mod split into "Meshes" and "Textures" packages provides one half each.
    if (in.facegen.found && in.facetint.found && in.facetint.winner_mod != in.facegen.winner_mod &&
        (contains(in.facetint.loose_mods, in.facegen.winner_mod) || contains(in.facegen.loose_mods, in.facetint.winner_mod))) {
      auto& f = b.add("FACE_004", kStatusConfirmed, "media", 0.85, kVerifyInGame);
      f.evidence = "Geometría de " + quoted(in.facegen.winner_mod) + " y tinte de " + quoted(in.facetint.winner_mod) +
                   ", pero cada uno trae también la otra mitad que pierde en MO2.";
      f.cause = "Orden de MO2 que mezcla dos paquetes de rostro distintos para el mismo NPC.";
      f.action = "Da a la pareja geometría + tinte del mismo mod la mayor prioridad en MO2.";
      f.resource = in.facetint.path;
      f.responsible = in.facetint.winner_mod;
      f.control_code = "FaceTintFromOtherMod";
    }
  }

  // ---- a later override that puts a face back (record-level: applies to templates too only through the chain)
  if (const auto* c = control_finding(control, "LostFaceEdits"); c && !head_exempt) {
    const bool revert = control.resolution && control.resolution->discards_own_edits.empty();
    auto& f = b.add("NPC_001", revert ? kStatusConfirmed : kStatusSuspected, "alta", revert ? 0.85 : 0.55, kVerifyInGame);
    f.evidence = c->message;
    f.cause = revert ? "Un plugin posterior copia valores faciales de una versión anterior (revierte la cara sin quererlo)."
                     : "Un plugin posterior cambia los campos faciales; puede ser deliberado.";
    f.action = control.resolution ? control.resolution->label + " (parche que copia solo los campos faciales)." : c->suggestion;
    f.resource = s.form_key;
    f.responsible = control.resolution ? control.resolution->winner_plugin : (in.chain.empty() ? "" : in.chain.back().plugin);
    f.control_code = c->code;
  }

  // ---- hair-slot armors of the default outfit (wigs): worn, they hide the NPC's hair head parts
  for (const auto& w : s.hair_armors) {
    const auto label = w.editor_id.empty() ? w.form_key : w.editor_id + " (" + w.form_key + ")";
    if (!w.addons_for_race || *w.addons_for_race == 0) {
      const bool known = w.addons_for_race.has_value();
      auto& f = b.add("HAIR_SMP_007", known ? kStatusConfirmed : kStatusSuspected, "alta", known ? 0.9 : 0.4, kVerifyInGame);
      f.evidence = "El atuendo por defecto pone " + label + " en la ranura de cabello (31)" +
                   (known ? ", y no tiene ningún ARMA con modelo para la raza y el sexo del NPC."
                          : "; no se pudo resolver qué ARMA dibuja para su raza.");
      f.cause = known ? "La peluca oculta el cabello del NPC y no dibuja nada en su lugar (p. ej. una peluca solo para razas UBE): sale calvo."
                      : "Una peluca oculta el cabello del NPC; si no tiene modelo para su raza, sale calvo.";
      f.action = "Añade la raza del NPC al ARMA de " + quoted(w.plugin) + " o quita la peluca de su atuendo (parche de reparación).";
      f.resource = w.form_key;
      f.responsible = w.plugin;
    }
    for (const auto& m : w.meshes) {
      if (m.found || m.path.empty()) continue;
      auto& f = b.add("HAIR_SMP_001", kStatusConfirmed, "alta", 0.9, kVerifyInGame);
      f.evidence = "La malla de la peluca " + label + " (" + m.path + ") no está en el perfil (ni suelta ni en BSA).";
      f.cause = "La peluca oculta el cabello y su malla falta: el NPC sale calvo.";
      f.action = "Activa o reinstala " + quoted(w.record_mod) + ".";
      f.resource = m.path;
      f.responsible = w.record_mod.empty() ? w.plugin : w.record_mod;
    }
    for (const auto& x : w.physics) {
      if (x.found || x.path.empty()) continue;
      auto& f = b.add("HAIR_SMP_002", kStatusConfirmed, "media", 0.85, kVerifyInGame);
      f.evidence = "La peluca " + label + " declara la física " + x.path + ", que no está en el perfil; la malla se ve visible, sin física.";
      f.cause = "Falta el XML de HDT-SMP del paquete de la peluca.";
      f.action = "Instala los XML de " + quoted(w.record_mod) + ".";
      f.resource = x.path;
      f.responsible = w.record_mod;
    }
  }

  // ---- SMP hair baked into the FaceGen (what the Creation Kit loses when it bakes a physics hair)
  if (!head_exempt) {
    for (const auto& h : s.smp_facegen) {
      const bool wants_physics = !h.source_physics_xml.empty();
      if (wants_physics && !h.facegen_has_physics_xml) {
        auto& f = b.add("HAIR_SMP_003", kStatusConfirmed, "media", 0.9, kVerifyInGame);
        f.evidence = "El cabello " + h.shape + " está pesado a huesos de física (" + joined(h.physics_bones) + "); su NIF (" + h.source_mesh +
                     ") declara " + h.source_physics_xml + " y el FaceGen efectivo no conserva esa referencia.";
        f.cause = "El Creation Kit no copia la referencia «HDT Skinned Mesh Physics Object» al generar el FaceGen: el cabello queda sin física.";
        f.action = "Añadir al FaceGen la referencia XML del cabello (copia del FaceGen en un mod de reparación; nunca sobre el original).";
        f.resource = in.facegen.path;
        f.responsible = in.facegen.winner_mod;
      }
      if (wants_physics && h.physics_xml && !h.physics_xml->found) {
        auto& f = b.add("HAIR_SMP_002", kStatusConfirmed, "media", 0.85, kVerifyInGame);
        f.evidence = "El cabello " + h.shape + " declara la física " + h.physics_xml->path +
                     ", que no está en el perfil; la malla se ve visible, sin física.";
        f.cause = "Falta el XML de HDT-SMP del paquete de cabello.";
        f.action = "Instala o activa el paquete que trae " + h.physics_xml->path + ".";
        f.resource = h.physics_xml->path;
      }
      if (h.max_bone_offset && *h.max_bone_offset > kSmpBoneTolerance) {
        char offset[32];
        std::snprintf(offset, sizeof(offset), "%.2f", *h.max_bone_offset);
        auto& f = b.add("HAIR_SMP_004", kStatusConfirmed, "alta", 0.85, kVerifyInGame);
        f.evidence = "Los huesos de física de " + h.shape + " en el FaceGen tienen otra pose de reposo que en " + h.source_mesh +
                     " (desviación máxima " + offset + " unidades).";
        f.cause = "El Creation Kit reescribe la jerarquía y la pose de reposo de los huesos SMP: el cabello se dibuja desplazado "
                  "(dentro de la cabeza o fuera de cámara) y el NPC parece calvo.";
        f.action = "Restaurar en una copia del FaceGen la jerarquía y la pose de esos huesos desde el NIF del cabello.";
        f.resource = in.facegen.path;
        f.responsible = in.facegen.winner_mod;
      }
      if (!h.bones_missing_in_source.empty()) {
        auto& f = b.add("HAIR_SMP_005", kStatusSuspected, "media", 0.6, kVerifyInGame);
        f.evidence = "El FaceGen pesa " + h.shape + " a huesos que su NIF (" + h.source_mesh + ") no tiene: " + joined(h.bones_missing_in_source) + ".";
        f.cause = "El FaceGen se generó con otra versión del cabello SMP que la instalada.";
        f.action = "Regenerar el FaceGen con la versión instalada del cabello (solo si es seguro) o instalar la versión con la que se hizo.";
        f.resource = in.facegen.path;
        f.responsible = in.facegen.winner_mod;
      }
    }
  }

  // ---- FSMP itself, when something of this NPC needs it
  const bool needs_smp = std::any_of(s.hair_armors.begin(), s.hair_armors.end(), [](const AuditHairArmor& w) { return w.smp; }) ||
                         (!head_exempt && std::any_of(s.smp_facegen.begin(), s.smp_facegen.end(),
                                                      [](const AuditSmpFaceGenHair& h) { return !h.source_physics_xml.empty(); }));
  if (needs_smp && s.fsmp_installed == false) {
    auto& f = b.add("HAIR_SMP_008", kStatusConfirmed, "media", 0.95, kVerifyOffline);
    f.evidence = "El cabello o la peluca de este NPC usa física HDT-SMP y el perfil no tiene SKSE\\Plugins\\hdtSMP64.dll.";
    f.cause = "FSMP no está instalado o está desactivado: el cabello se ve sin física.";
    f.action = "Instala o activa Faster HDT-SMP para la versión de Skyrim del perfil.";
    f.resource = "SKSE\\Plugins\\hdtSMP64.dll";
  }

  // ---- reported bald in game: open until a confirmed hair cause explains it
  if (s.observed_bald_in_game &&
      std::none_of(out.findings.begin(), out.findings.end(),
                   [](const AuditFinding& f) { return f.status == kStatusConfirmed && f.code.rfind("HAIR_", 0) == 0; })) {
    auto& f = b.add("HAIR_SMP_010", kStatusSuspected, "alta", 1.0, kVerifyInGame);
    f.evidence = "El propietario vio a este NPC calvo en Skyrim; ningún hallazgo de cabello confirmado lo explica todavía.";
    f.cause = "Causa no demostrada: distribución en tiempo de ejecución, peluca del inventario, override en partida o un caso que el auditor no cubre.";
    f.action = "Seleccionar el NPC en la consola (clic) y anotar RefID y BaseID; revisar inventario/atuendo equipado y distribuidores.";
    f.resource = s.form_key;
  }

  // ---- what only a look in game can settle
  std::vector<std::string> visual;
  for (const auto& f : out.findings) {
    if (f.status == kStatusSuspected && f.verification == kVerifyInGame) visual.push_back(f.code);
  }
  if (!visual.empty()) {
    auto& f = b.add("NPC_004", kStatusSuspected, "baja", 1.0, kVerifyInGame);
    f.evidence = "Hallazgos que solo se confirman viendo al NPC en el juego: " + joined(visual) + ".";
    f.cause = "Inferencia por atribución o comprobación parcial fuera del juego.";
    f.action = "Comprobar en Skyrim (consola: «player.placeatme» con su FormID) y registrar captura.";
    f.resource = s.form_key;
  }

  std::stable_sort(out.findings.begin(), out.findings.end(), [](const AuditFinding& a, const AuditFinding& b) {
    const bool ac = a.status == kStatusConfirmed, bc = b.status == kStatusConfirmed;
    if (ac != bc) return ac;
    return severity_rank(a.severity) < severity_rank(b.severity);
  });
  out.status = out.findings.empty()                       ? kStatusClean
               : out.findings.front().status == kStatusConfirmed ? kStatusConfirmed
                                                         : kStatusSuspected;
  return out;
}

}  // namespace sns::appearance

namespace sns::appearance {

WigRepairDecision plan_wig_repair(const WigRepairCandidate& c) {
  WigRepairDecision d;
  d.form_key = c.form_key;
  if (c.broken_wigs.empty()) {
    d.reason = "Sin peluca rota confirmada.";
    return d;
  }
  if (c.outfit_other_entries > 0) {
    d.reason = "El atuendo también tiene listas niveladas, que la exportación de atuendos no conserva: reparar a mano o instalar la dependencia.";
    return d;
  }
  if (c.facegen_hair_vertices < kFullHairVertices) {
    d.reason = "El FaceGen no trae un cabello completo propio: quitar la peluca no le devuelve el pelo. Instala la dependencia que falta.";
    return d;
  }
  for (const auto& a : c.outfit_armors) {
    (contains(c.broken_wigs, a) ? d.removed : d.keep_armors).push_back(a);
  }
  if (d.removed.empty()) {
    d.reason = "La peluca rota no está en el atuendo por defecto.";
    return d;
  }
  if (d.keep_armors.empty()) {
    d.reason = "El atuendo quedaría vacío sin la peluca: reparar a mano o instalar la dependencia.";
    return d;
  }
  d.repair = true;
  d.reason = "Se quita la peluca rota del atuendo por defecto; el FaceGen ya trae un cabello completo (" + std::to_string(c.facegen_hair_vertices) +
             " vértices), que vuelve a verse sin física. Reversible: desactivar el mod de reparación.";
  return d;
}

}  // namespace sns::appearance
