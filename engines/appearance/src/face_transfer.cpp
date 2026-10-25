#include "sns/appearance/face_transfer.hpp"

namespace sns::appearance {
namespace {

std::string quoted(const std::string& s) { return "«" + s + "»"; }

void add_messages(TransferPlan& plan, const SourceCandidate& c) {
  for (const auto& m : c.messages) plan.reasons.push_back(m);
}

bool archived(const SourceCandidate& c) { return c.id.rfind("bsa:", 0) == 0; }

}  // namespace

std::string_view to_string(TransferVerdict verdict) {
  switch (verdict) {
    case TransferVerdict::InUse: return "IN_USE";
    case TransferVerdict::PatchReady: return "PATCH_READY";
    case TransferVerdict::NeedsReview: return "NEEDS_REVIEW";
    case TransferVerdict::NeedsLoadOrder: return "NEEDS_LOAD_ORDER";
    case TransferVerdict::Blocked: return "BLOCKED";
  }
  return "BLOCKED";
}

TransferPlan plan_face_transfer(const SourceMatrix& matrix, std::string_view candidate_id, DraftState draft, std::string_view draft_chosen) {
  TransferPlan plan;
  plan.candidate_id = std::string(candidate_id);
  const auto* c = matrix.find(candidate_id);
  if (!c) {
    plan.reasons.push_back("La fuente " + quoted(std::string(candidate_id)) + " no existe en la matriz actual del NPC.");
    plan.actions.push_back("Vuelve a examinar los rostros del NPC.");
    return plan;
  }
  plan.chosen = draft == DraftState::Fresh && draft_chosen == candidate_id;
  const auto* effective = matrix.effective();
  const std::string winner = effective && !effective->facegen_mod.empty() ? effective->facegen_mod : std::string("el ganador actual");

  // ---- a version of the record without its own FaceGen
  if (c->facegen_mod.empty() && !c->effective) {
    plan.verdict = TransferVerdict::Blocked;
    plan.requirement = "REQUIERE_CREATION_KIT";
    add_messages(plan, *c);
    plan.reasons.push_back("Sin FaceGen propio el rostro no existe como archivo: haría falta generarlo, y el estudio no genera FaceGen.");
    plan.actions.push_back("Genera el FaceGen de ese registro en Creation Kit (Ctrl+F4) en un perfil de prueba, o elige otra fuente.");
    return plan;
  }

  // ---- the face the game loads
  if (c->effective) {
    if (c->status == SourceStatus::Missing) {
      plan.verdict = TransferVerdict::Blocked;
      plan.requirement = "REQUIERE_CREATION_KIT";
      plan.reasons.push_back("El NPC no tiene FaceGen en el perfil: no hay rostro que transferir.");
      plan.actions.push_back("Instala un mod que traiga el FaceGen del NPC o genéralo en Creation Kit en un perfil de prueba.");
      return plan;
    }
    if (!c->renderable) {
      plan.verdict = TransferVerdict::Blocked;
      plan.reasons.push_back(c->render_note);
      plan.actions.push_back("Reinstala el mod " + quoted(winner) + " o elige otra fuente.");
      return plan;
    }
    add_messages(plan, *c);
    if (c->resolution_available) {
      plan.verdict = c->requires_review ? TransferVerdict::NeedsReview : TransferVerdict::PatchReady;
      plan.reasons.push_back("El registro que gana no coincide con el FaceGen de " + quoted(winner) +
                             ": el parche copia los datos faciales del plugin de ese FaceGen al NPC (sin tocar FaceGen ni el mod original).");
      if (!plan.chosen) plan.actions.push_back("Guarda esta fuente como tu elección (paso «Seleccionar»).");
      if (c->requires_review) plan.actions.push_back("Aprueba la revisión del caso (D1) cuando el asistente la pida; sin ella no se exporta.");
      plan.actions.push_back("Crea el parche: un mod aparte que instalas tú en MO2, por encima de los mods del NPC.");
      plan.can_create_patch = plan.chosen;
      return plan;
    }
    if (c->status == SourceStatus::Conflict || c->status == SourceStatus::Ambiguous) {
      plan.verdict = TransferVerdict::Blocked;
      plan.reasons.push_back("El mapa de control avisa de un problema que no tiene resolución asistida: no se escribe ningún parche.");
      plan.actions.push_back("Revisa los avisos en la pestaña Origen; elige otra fuente o corrige los mods del NPC.");
      return plan;
    }
    plan.verdict = TransferVerdict::InUse;
    plan.reasons.push_back("Es el rostro que carga el juego y su registro coincide: no hace falta ningún parche.");
    return plan;
  }

  // ---- a FaceGen that exists but does not win in MO2 (loose copy of another mod, or a copy inside a BSA)
  if (!c->renderable) {
    plan.verdict = TransferVerdict::Blocked;
    plan.reasons.push_back(c->render_note);
    plan.actions.push_back("Elige otra fuente: este FaceGen no se puede leer.");
    return plan;
  }
  plan.verdict = TransferVerdict::NeedsLoadOrder;
  if (archived(*c)) {
    plan.reasons.push_back("El FaceGen de " + quoted(c->facegen_mod) + " está dentro de un BSA: cualquier copia suelta del mismo archivo lo tapa (" +
                           quoted(winner) + " la trae).");
    plan.actions.push_back("En MO2, oculta en " + quoted(winner) + " los archivos " + c->geometry_path +
                           (c->tint_path.empty() ? std::string() : " y " + c->tint_path) + " (o desactiva ese mod en un perfil de prueba).");
  } else {
    plan.reasons.push_back("El juego carga el FaceGen de " + quoted(winner) + "; el de " + quoted(c->facegen_mod) +
                           " queda debajo en la prioridad de MO2.");
    plan.actions.push_back("En MO2, coloca " + quoted(c->facegen_mod) + " por debajo de " + quoted(winner) +
                           " en la lista de mods (más prioridad), u oculta en " + quoted(winner) + " los archivos de FaceGen del NPC.");
  }
  plan.reasons.push_back("Un plugin no puede elegir qué FaceGen se carga: la ruta del archivo es la misma para todos los mods. El estudio no cambia el orden de MO2.");
  if (c->status == SourceStatus::Conflict || c->status == SourceStatus::Ambiguous) {
    plan.reasons.push_back("Con esa prioridad, el mapa de control avisaría:");
    add_messages(plan, *c);
  }
  if (c->resolution_available) {
    plan.actions.push_back("Después vuelve a abrir el NPC: si su registro no coincide con ese FaceGen, el estudio ofrecerá crear el parche que lo alinea" +
                           std::string(c->requires_review ? " (con revisión D1)." : "."));
  } else {
    plan.actions.push_back("Después vuelve a abrir el NPC para comprobar que ese rostro es el que gana y que su registro coincide.");
  }
  return plan;
}

}  // namespace sns::appearance
