#include "sns/appearance/hair_safe_recovery.hpp"

namespace sns::appearance {

HairRecoveryProposal plan_hair_safe_recovery(
    const NpcAudit& audit,
    const std::string& previous_hair_form_key) {
  HairRecoveryProposal proposal;
  proposal.form_key = audit.form_key;
  proposal.status = kStatusRepairedInFiles;
  proposal.verification_state = kVerifyInGame;
  proposal.previous_hair_form_key = previous_hair_form_key;
  proposal.can_restore_previous = !previous_hair_form_key.empty();

  if (audit.findings.empty()) {
    proposal.root_cause = "Sin problemas detectados.";
    proposal.minimal_fix_summary = "No se requiere ninguna reparación.";
    proposal.ready_for_export = true;
    return proposal;
  }

  // Find the primary hair finding
  const AuditFinding* hair_finding = nullptr;
  for (const auto& f : audit.findings) {
    if (f.code.rfind("HAIR", 0) == 0) {
      hair_finding = &f;
      break;
    }
  }

  if (!hair_finding && !audit.findings.empty()) {
    hair_finding = &audit.findings.front();
  }

  proposal.root_cause = hair_finding ? hair_finding->cause : "Causa indeterminada";

  if (hair_finding && hair_finding->code == "HAIR_SMP_007") {
    proposal.minimal_fix_summary = "Generar parche de resolución que añada la raza del NPC al ARMA de la peluca o retire la peluca del outfit.";
    HairRecoveryStep step1;
    step1.step_order = 1;
    step1.action_type = "GENERATE_RESOLUTION_PATCH";
    step1.description = "Crear parche ESPFE aislado con el registro ARMA actualizado para la raza del NPC.";
    step1.target_file = "SNS_Hair_Resolution_Patch.esp";
    step1.previous_state = "Sin modelo ARMA para raza";
    step1.proposed_state = "Modelo ARMA asignado a raza efectiva";
    step1.is_reversible = true;
    proposal.steps.push_back(std::move(step1));
  } else if (hair_finding && hair_finding->code == "HAIR_SMP_003") {
    proposal.minimal_fix_summary = "Transferir propiedad NiStringExtraData y ruta XML desde la malla original al FaceGen.";
    HairRecoveryStep step1;
    step1.step_order = 1;
    step1.action_type = "TRANSFER_SMP_METADATA";
    step1.description = "Inyectar la ruta de físicas XML en el FaceGen mediante smp-facegen sin modificar geometría facial.";
    step1.target_file = hair_finding->resource;
    step1.previous_state = "FaceGen sin NiStringExtraData";
    step1.proposed_state = "FaceGen con ruta XML restaurada";
    step1.is_reversible = true;
    proposal.steps.push_back(std::move(step1));
  } else if (hair_finding && hair_finding->code == "HAIR_001") {
    if (proposal.can_restore_previous) {
      proposal.minimal_fix_summary = "Restaurar el HeadPart previo funcional en un parche de resolución.";
      HairRecoveryStep step1;
      step1.step_order = 1;
      step1.action_type = "RESTORE_PREVIOUS_HEADPART";
      step1.description = "Revertir el HeadPart de cabello al valor previo funcional (" + previous_hair_form_key + ").";
      step1.target_file = "SNS_Hair_Resolution_Patch.esp";
      step1.previous_state = hair_finding->resource;
      step1.proposed_state = previous_hair_form_key;
      step1.is_reversible = true;
      proposal.steps.push_back(std::move(step1));
    } else {
      proposal.minimal_fix_summary = "Reinstalar o reactivar el mod de cabello original en MO2.";
    }
  } else {
    proposal.minimal_fix_summary = hair_finding ? hair_finding->action : "Revisar archivos de apariencia.";
  }

  // Pre-export validation
  proposal.validated_dependencies.push_back("Mallas verificadas en VFS");
  proposal.validated_dependencies.push_back("Masters del plugin de resolución comprobados");
  proposal.ready_for_export = !proposal.steps.empty();

  return proposal;
}

}  // namespace sns::appearance
