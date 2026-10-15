#include "sns/appearance/easy_mod.hpp"

#include <algorithm>

namespace sns::appearance {

namespace {

bool blocked(const EasyModInputs& in, std::string_view op) {
  return std::find(in.blocked_operations.begin(), in.blocked_operations.end(), op) != in.blocked_operations.end();
}

}  // namespace

std::string_view to_string(EasyModMode mode) {
  switch (mode) {
    case EasyModMode::OutfitPatch: return "outfit-patch";
    case EasyModMode::FaceOverhaul: return "face-overhaul";
    case EasyModMode::CompleteOverhaul: return "complete-overhaul";
    case EasyModMode::None: break;
  }
  return "";
}

std::string_view to_string(EasyModState state) {
  switch (state) {
    case EasyModState::Ok: return "ok";
    case EasyModState::Warning: return "warning";
    case EasyModState::Waiting: return "waiting";
    case EasyModState::Blocked: return "blocked";
    case EasyModState::Info: break;
  }
  return "info";
}

EasyModPlan plan_easy_mod(const EasyModInputs& in) {
  EasyModPlan plan;
  auto check = [&](std::string key, EasyModState state, std::string label, std::string detail = {}) {
    plan.checks.push_back({std::move(key), state, std::move(label), std::move(detail)});
  };

  if (!in.npc_loaded) {
    check("npc", EasyModState::Blocked, "Ningún NPC seleccionado", "Elige un NPC en la lista de la izquierda.");
    plan.summary = "Selecciona un NPC para empezar.";
    return plan;
  }
  check("npc", EasyModState::Ok, "NPC: " + in.npc_label);

  // ---- what the mod will contain
  const bool hair_only = in.face_kind == FaceBuildKind::HairColorSafe;
  const bool full_face = in.face_kind == FaceBuildKind::Full;
  const bool face_or_body = hair_only || full_face || in.height_changed;
  const bool outfit = in.outfit_changed && in.outfit_pieces > 0;
  if (hair_only) {
    plan.changes.push_back({"hair", EasyModState::Ok, "Color de cabello",
                            "Hair Color Safe Mode: el registro solo cambia HCLF y el FaceGen ganador solo cambia el color del "
                            "cabello; la geometría de la cabeza no se toca."});
  }
  if (full_face) {
    plan.changes.push_back({"face", EasyModState::Ok, "Rostro",
                            "Campos del rostro del NPC y su FaceGen generado con la vista previa."});
  }
  if (in.height_changed) {
    plan.changes.push_back({"height", EasyModState::Ok, "Altura (NAM6)", "Solo el campo de altura del NPC."});
  }
  if (outfit) {
    plan.changes.push_back({"outfit", EasyModState::Ok, "Atuendo: " + std::to_string(in.outfit_pieces) + " pieza(s)",
                            "Un atuendo (OTFT) nuevo asignado al NPC; el FaceGen no se toca."});
  }
  plan.mode = face_or_body && outfit ? EasyModMode::CompleteOverhaul
              : face_or_body         ? EasyModMode::FaceOverhaul
              : outfit               ? EasyModMode::OutfitPatch
                                     : EasyModMode::None;

  // ---- checks
  if (in.face_kind == FaceBuildKind::WaitForAnalysis) {
    check("analysis", EasyModState::Waiting, "Analizando el FaceGen original",
          "Las ediciones del rostro se activan cuando termina el análisis.");
  }
  if (hair_only || full_face) {
    if (in.face_building) {
      check("facegen", EasyModState::Waiting, "Generando la cabeza", "Espera a que termine la vista previa.");
    } else if (!in.face_exportable) {
      check("facegen", EasyModState::Blocked, "El FaceGen no es exportable", "Revisa el panel de problemas del rostro.");
    } else if (hair_only && in.face_integrity != "hair-tint-only") {
      check("facegen", EasyModState::Blocked, "Integridad del FaceGen sin confirmar",
            "El guard no confirmó que el FaceGen sea el ganador con otro color de cabello.");
    } else {
      check("facegen", EasyModState::Ok, hair_only ? "FaceGen = el ganador salvo el color del cabello" : "FaceGen generado y exportable",
            hair_only ? "Guard de integridad superado: vértices, UV, normales, pesos y texturas idénticos." : "");
    }
  }
  if (hair_only && blocked(in, "hair-color")) {
    check("compat", EasyModState::Blocked, "Color de cabello no disponible para este NPC", "Ver «Protección facial».");
  }
  if (full_face && (blocked(in, "face-morphs") || blocked(in, "head-parts"))) {
    check("compat", EasyModState::Blocked, "Edición del rostro no verificada para esta cabeza",
          "Sus morphs no coinciden con la malla; ver «Protección facial».");
  }
  if (in.outfit_changed) {
    if (in.outfit_pieces == 0) {
      check("outfit", EasyModState::Blocked, "Atuendo vacío", "Un atuendo sin piezas no se puede exportar.");
    } else if (in.outfit_preview_material) {
      check("outfit", EasyModState::Blocked, "Variante de material solo previsualizable",
            "Restaura el material original antes de exportar.");
    } else if (in.outfit_validating) {
      check("outfit", EasyModState::Waiting, "Validando el atuendo");
    } else if (!in.outfit_exportable) {
      check("outfit", EasyModState::Blocked, "El atuendo tiene errores de validación",
            "Slots, raza, sexo o plantilla: revisa la validación en Outfit Studio.");
    } else {
      check("outfit", EasyModState::Ok, "Atuendo validado", "Slots, raza, sexo y plantilla comprobados.");
    }
  }
  if (!in.provenance_conflict.empty()) {
    check("provenance", EasyModState::Warning, "Conflicto en MO2", in.provenance_conflict);
  }
  if (plan.mode == EasyModMode::None && in.face_kind != FaceBuildKind::WaitForAnalysis) {
    check("changes", EasyModState::Blocked, "No hay cambios que exportar",
          "Cambia el color de cabello, el rostro o el atuendo primero.");
  }
  check("worker", EasyModState::Info, "Al crear el mod se comprueba",
        "Plugin releído: masters, ESL, que el NPC solo cambie en lo pedido (Safe Patch) y ZIP verificado.");
  check("skyrim", EasyModState::Info, "Pendiente de verificación en Skyrim",
        "La exportación se comprueba en archivos, no dentro del juego.");

  plan.can_export = plan.mode != EasyModMode::None &&
                    std::none_of(plan.checks.begin(), plan.checks.end(), [](const EasyModItem& c) {
                      return c.state == EasyModState::Blocked || c.state == EasyModState::Waiting;
                    });
  switch (plan.mode) {
    case EasyModMode::OutfitPatch: plan.summary = "Parche de atuendo (modo A)."; break;
    case EasyModMode::FaceOverhaul: plan.summary = hair_only && !in.height_changed ? "Cambio de color de cabello (modo B, cambio mínimo)." : "Rostro (modo B)."; break;
    case EasyModMode::CompleteOverhaul: plan.summary = "Rostro y atuendo en un mod (modo C)."; break;
    case EasyModMode::None: plan.summary = "Nada que exportar todavía."; break;
  }
  return plan;
}

}  // namespace sns::appearance
