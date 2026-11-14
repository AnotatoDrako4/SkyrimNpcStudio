#include "sns/appearance/ube_smp_validator.hpp"
#include "sns/appearance/npc_audit.hpp"

#include <algorithm>
#include <cctype>

namespace sns::appearance {
namespace {

bool icontains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [](char ch1, char ch2) {
                          return std::tolower(static_cast<unsigned char>(ch1)) ==
                                 std::tolower(static_cast<unsigned char>(ch2));
                        });
  return it != haystack.end();
}

}  // namespace

UbeSmpReport validate_ube_smp_compatibility(const HairAssignmentGraph& graph, const UbeSmpEnvironment& env) {
  UbeSmpReport report;
  report.compatible = true;
  report.hdf_installed = env.hdf_installed;
  report.hair_suppression_fix_present = env.hair_suppression_fix_installed;

  // 1. Determine if NPC uses UBE
  report.is_ube = graph.is_ube || icontains(graph.effective_race, "ube") || icontains(graph.body_family, "ube");

  // 2. Check if HDF targets this NPC
  for (const auto& node : graph.nodes) {
    if (node.source_kind == HairSourceKind::Hdf ||
        (env.hdf_installed && node.stage == AssignmentStage::PlannedDynamic)) {
      report.hdf_targeted = true;
      break;
    }
  }

  // 3. If UBE and HDF are involved: verify effective UBE patch presence
  if (report.is_ube && (report.hdf_targeted || env.hdf_installed)) {
    report.ube_patch_present = env.ube_hdf_patch_installed;
    if (!env.ube_hdf_patch_installed) {
      report.compatible = false;
      UbeSmpValidationIssue issue;
      issue.code = "HAIR_SMP_009";
      issue.severity = "alta";
      issue.status = kStatusConfirmed;
      issue.message = "NPC con raza o cuerpo UBE y peinados HDF sin los recursos efectivos del parche UBE.";
      issue.cause = "Hairstyle Distribution Framework asigna peinados sin el parche de compatibilidad UBE o sus archivos no existen en el VFS de MO2.";
      issue.action = "Instala y activa los archivos del parche UBE para Hairstyle Distribution Framework en el perfil de MO2.";
      issue.resource = "meshes\\ube\\hair";
      issue.responsible = "Hairstyle Distribution Framework - UBE Patch";
      report.issues.push_back(std::move(issue));
    }
  }

  // 4. Check if meshes and dependencies resolve in VFS
  for (const auto& node : graph.nodes) {
    if (!node.suppressed_by_equipment && !node.model_path.empty() && !node.mesh_source.found) {
      report.compatible = false;
      UbeSmpValidationIssue issue;
      issue.code = "HAIR_SMP_001";
      issue.severity = "alta";
      issue.status = kStatusConfirmed;
      issue.message = "Malla de peinado no resuelta en VFS: " + node.model_path;
      issue.cause = "El archivo NIF no existe en el perfil de MO2 (ni suelto ni en BSA).";
      issue.action = "Instala o activa el mod " + node.record_mod + " que provee la malla.";
      issue.resource = node.model_path;
      issue.responsible = node.record_mod.empty() ? node.winning_plugin : node.record_mod;
      report.issues.push_back(std::move(issue));
    }
  }

  // 5. Check slot conflicts, helmets, or suppressed wigs
  for (const auto& node : graph.nodes) {
    if (node.suppressed_by_equipment) {
      report.compatible = false;
      UbeSmpValidationIssue issue;
      issue.code = "HAIR_SMP_006";
      issue.severity = "media";
      issue.status = kStatusConfirmed;
      issue.message = "Conflicto de slots: peinado bloqueado o suprimido por equipamiento (" + node.suppressing_equipment + ").";
      issue.cause = "El equipamiento o peluca entra en colisión de biped slots (slot 31/30) o carece de modelo para la raza.";
      issue.action = "Revisa los biped slots del equipamiento o proporciona un addon ARMA para la raza del NPC.";
      issue.resource = node.form_key;
      issue.responsible = node.winning_plugin;
      report.issues.push_back(std::move(issue));
    }
  }

  // 6. Check Hair Suppression Fix requirement
  bool needs_suppression_fix = false;
  for (const auto& node : graph.nodes) {
    if (node.requires_hair_suppression_fix) {
      needs_suppression_fix = true;
      break;
    }
  }
  if (needs_suppression_fix && !env.hair_suppression_fix_installed) {
    report.compatible = false;
    UbeSmpValidationIssue issue;
    issue.code = "HAIR_SMP_011";
    issue.severity = "media";
    issue.status = kStatusConfirmed;
    issue.message = "El peinado instalado requiere Hair Suppression Fix pero el plugin DLL no está presente.";
    issue.cause = "Peinado SMP largo o peluca que sufre clipping o desaparición bajo cascos/adornos sin la corrección SKSE.";
    issue.action = "Instala HairSuppressionFix en SKSE\\Plugins\\HairSuppressionFix.dll.";
    issue.resource = "SKSE\\Plugins\\HairSuppressionFix.dll";
    issue.responsible = "HairSuppressionFix";
    report.issues.push_back(std::move(issue));
  }

  // 7. Check collision configuration matches chosen body
  if (report.is_ube) {
    if (!env.active_body_collision_family.empty() && env.active_body_collision_family != "ube") {
      report.collision_matches_body = false;
      report.compatible = false;
      UbeSmpValidationIssue issue;
      issue.code = "HAIR_SMP_008";
      issue.severity = "alta";
      issue.status = kStatusConfirmed;
      issue.message = "Incompatibilidad de colisiones: el NPC utiliza cuerpo UBE pero la colisión SMP activa es para " +
                      env.active_body_collision_family + ".";
      issue.cause = "La configuración de colisiones de FSMP no corresponde a la topología física UBE.";
      issue.action = "Configura o instala las colisiones de cuerpo UBE para FSMP.";
      issue.resource = "meshes\\actors\\character\\...\\collision.xml";
      issue.responsible = "FSMP Collision Config";
      report.issues.push_back(std::move(issue));
    }
  }

  return report;
}

}  // namespace sns::appearance
