#include "sns/runtime/correlator.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "sns/core/error.hpp"

using nlohmann::json;

namespace sns::runtime {

namespace {

std::vector<std::string> string_array(const json& j, const char* key) {
  std::vector<std::string> out;
  if (!j.contains(key) || !j.at(key).is_array()) return out;
  for (const auto& item : j.at(key))
    if (item.is_string()) out.push_back(item.get<std::string>());
  return out;
}

bool has_code(const std::vector<std::string>& codes, const std::string& prefix) {
  return std::any_of(codes.begin(), codes.end(),
                     [&](const std::string& c) { return c.rfind(prefix, 0) == 0; });
}

bool version_compatible(const std::string& a, const std::string& b) {
  // Bind on the exact triplet when both are known ("1.6.1170" vs "1.6.1170.0" tolerated).
  if (a.empty() || b.empty()) return true;
  auto trim = [](std::string v) {
    while (v.size() > 2 && v.substr(v.size() - 2) == ".0") v.resize(v.size() - 2);
    return v;
  };
  return trim(a) == trim(b);
}

}  // namespace

StaticAuditRow parse_static_audit_row(const std::string& json_text) {
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::exception& e) {
    throw Error(ErrorKind::Parse, std::string("static audit row: JSON malformado: ") + e.what());
  }
  if (!j.is_object()) throw Error(ErrorKind::InvalidArgument, "static audit row: debe ser objeto");

  StaticAuditRow row;
  if (j.contains("formKey") && j.at("formKey").is_string())
    row.form_key = normalize_form_key(j.at("formKey").get<std::string>());
  // Fallback: findings[0].resource carries the FormKey in some scan versions.
  if (row.form_key.empty() && j.contains("findings") && j.at("findings").is_array()) {
    for (const auto& f : j.at("findings")) {
      if (f.contains("resource") && f.at("resource").is_string()) {
        row.form_key = normalize_form_key(f.at("resource").get<std::string>());
        if (!row.form_key.empty()) break;
      }
    }
  }
  if (j.contains("editorId") && j.at("editorId").is_string())
    row.editor_id = j.at("editorId").get<std::string>();
  if (j.contains("status") && j.at("status").is_string())
    row.status = j.at("status").get<std::string>();
  row.codes = string_array(j, "codes");
  row.exemptions = string_array(j, "exemptions");
  row.facegen_foreign_hair = string_array(j, "faceGenForeignHair");
  if (j.contains("faceGenParsed") && j.at("faceGenParsed").is_boolean())
    row.facegen_parsed = j.at("faceGenParsed").get<bool>();
  if (j.contains("faceGenHasHair") && j.at("faceGenHasHair").is_boolean())
    row.facegen_has_hair = j.at("faceGenHasHair").get<bool>();
  if (j.contains("findings") && j.at("findings").is_array()) {
    for (const auto& f : j.at("findings")) {
      if (f.contains("status") && f.at("status").is_string() &&
          f.at("status").get<std::string>() == "CONFIRMADO" && f.contains("code") &&
          f.at("code").is_string())
        row.confirmed_codes.push_back(f.at("code").get<std::string>());
    }
  }
  return row;
}

const char* to_string(CorrelationState value) {
  switch (value) {
    case CorrelationState::kStaticValid: return "STATIC_VALID";
    case CorrelationState::kRuntimeAssetMissing: return "RUNTIME_ASSET_MISSING";
    case CorrelationState::kRuntimeGeometryMissing: return "RUNTIME_GEOMETRY_MISSING";
    case CorrelationState::kSmpDependencyError: return "SMP_DEPENDENCY_ERROR";
    case CorrelationState::kDynamicAssignmentSuspected: return "DYNAMIC_ASSIGNMENT_SUSPECTED";
    case CorrelationState::kVisualFailureConfirmed: return "VISUAL_FAILURE_CONFIRMED";
    case CorrelationState::kInconclusive: return "INCONCLUSIVE";
    case CorrelationState::kValidatedInSkyrim: return "VALIDATED_IN_SKYRIM";
    case CorrelationState::kIdentityMismatch: return "IDENTITY_MISMATCH";
    case CorrelationState::kNoRuntimeSelection: return "NO_RUNTIME_SELECTION";
  }
  return "INCONCLUSIVE";
}

CorrelationResult correlate(const StaticAuditRow& static_row, const RuntimeReport& report,
                            const std::optional<VisualAttestation>& attestation,
                            const LogEvidence* logs) {
  CorrelationResult result;
  result.static_status = static_row.status;

  auto static_clean = [&] {
    return static_row.status == "SIN_ERROR_DETECTADO" || static_row.status.empty();
  };
  auto mark_false_negative = [&] {
    if (static_clean()) {
      result.false_negative = true;
      result.reasons.push_back(
          "FALSO NEGATIVO: la auditoría estática clasificó el NPC como SIN_ERROR_DETECTADO y la "
          "evidencia runtime/visual lo contradice.");
    }
  };

  // 1. Guard rails: selection and identity.
  if (report.selection != SelectionKind::kNpc || !report.npc) {
    result.state = CorrelationState::kNoRuntimeSelection;
    result.reasons.push_back("El informe runtime no contiene un NPC seleccionado.");
    return result;
  }
  const std::string runtime_key = report.npc->base_form_key;
  if (static_row.form_key.empty() || runtime_key != normalize_form_key(static_row.form_key)) {
    result.state = CorrelationState::kIdentityMismatch;
    result.reasons.push_back("La fila estática y el informe runtime no son el mismo NPC ('" +
                             static_row.form_key + "' vs '" + runtime_key + "').");
    return result;
  }
  result.evidence.push_back("Identidad común: " + runtime_key);

  // 2. Attestation binding (never silently accepted).
  bool attestation_binds = false;
  if (attestation) {
    attestation_binds = true;
    if (normalize_form_key(attestation->npc_form_key) != runtime_key) {
      attestation_binds = false;
      result.attestation_rejections.push_back("el atestado es de otro NPC (" +
                                              attestation->npc_form_key + ")");
    }
    if (!report.profile.mo2_profile.empty() && !attestation->profile.empty() &&
        attestation->profile != report.profile.mo2_profile) {
      attestation_binds = false;
      result.attestation_rejections.push_back("perfil distinto ('" + attestation->profile +
                                              "' vs '" + report.profile.mo2_profile + "')");
    }
    if (!version_compatible(attestation->skyrim_version, report.session.skyrim_version)) {
      attestation_binds = false;
      result.attestation_rejections.push_back("versión de Skyrim distinta ('" +
                                              attestation->skyrim_version + "' vs '" +
                                              report.session.skyrim_version + "')");
    }
    if (attestation->observation != "bald" && attestation->observation != "face-defect" &&
        attestation->observation != "ok") {
      attestation_binds = false;
      result.attestation_rejections.push_back("observación no reconocida: '" +
                                              attestation->observation + "'");
    }
  }

  // 3. Concrete cause detection (ordered by specificity).
  std::string cause;
  CorrelationState cause_state = CorrelationState::kInconclusive;

  // 3a. Dynamic distribution changed the hair at spawn time.
  if (!report.distribution.empty()) {
    cause_state = CorrelationState::kDynamicAssignmentSuspected;
    for (const auto& d : report.distribution)
      result.evidence.push_back("Distribución dinámica (" + d.source + "): " + d.detail +
                                (d.form_key.empty() ? "" : " [" + d.form_key + "]"));
    cause = "una distribución dinámica (SPID/HDF) modificó la apariencia al aparecer el actor";
  } else if (logs && logs->spid_distribution_mentions_npc) {
    cause_state = CorrelationState::kDynamicAssignmentSuspected;
    cause = "el log de SPID registra una distribución sobre este NPC en la sesión actual";
    result.evidence.push_back("po3_SpellPerkItemDistributor.log menciona al NPC (sesión actual)");
  }

  // 3b. Equipment hides hair (wig/hood) and its own mesh is missing or unresolved.
  if (cause_state == CorrelationState::kInconclusive) {
    for (const auto& eq : report.equipment) {
      if (!eq.hides_hair) continue;
      if (eq.mesh_resolved == Obs::kMissing) {
        cause_state = CorrelationState::kRuntimeAssetMissing;
        cause = "la peluca/capa equipada oculta el cabello y su propia malla no está disponible (" +
                (eq.mesh_path.empty() ? eq.editor_id : eq.mesh_path) + ")";
        result.evidence.push_back("Equipamiento slot " + std::to_string(eq.slot) + " " +
                                  eq.form_key + " hidesHair, malla MISSING");
        break;
      }
      if (eq.mesh_resolved == Obs::kObserved) {
        result.evidence.push_back("Equipamiento slot " + std::to_string(eq.slot) + " " +
                                  eq.form_key +
                                  " oculta el cabello pero su malla está disponible: el cabello "
                                  "oculto es esperado, no un defecto de assets");
      }
    }
  }

  // 3c. SMP dependency: physics declared but FSMP absent/erroring.
  if (cause_state == CorrelationState::kInconclusive) {
    bool fsmp_loaded = false;
    for (const auto& p : report.session.plugins)
      if (p.name == "hdtSMP64.dll" && p.loaded) fsmp_loaded = true;
    const bool smp_declared = report.hair.smp_status == SmpStatus::kActive ||
                              report.hair.smp_status == SmpStatus::kPresentNoPhysics;
    if (smp_declared && !fsmp_loaded) {
      cause_state = CorrelationState::kSmpDependencyError;
      cause = "el cabello declara física SMP pero hdtSMP64.dll no está cargado en la sesión";
      result.evidence.push_back("hair.smpStatus=" + std::string(to_string(report.hair.smp_status)) +
                                "; hdtSMP64.dll no cargado");
    } else if (logs && logs->fsmp_load_or_xml_error &&
               (logs->fsmp_error_mentions_npc || smp_declared)) {
      cause_state = CorrelationState::kSmpDependencyError;
      cause = logs->fsmp_error_mentions_npc
                  ? "el log de FSMP registra un error de carga/XML mencionando a este NPC"
                  : "el log de FSMP registra errores de carga/XML y el cabello declara SMP";
      result.evidence.push_back("hdtSMP64.log: error de carga/XML en la sesión actual");
    }
    for (const auto& c : report.checks) {
      if (c.id.rfind("SMP_", 0) == 0 && c.state == "FAIL") {
        cause_state = CorrelationState::kSmpDependencyError;
        cause = "comprobación runtime SMP fallida: " + c.id + " (" + c.detail + ")";
        result.evidence.push_back("check FAIL " + c.id + ": " + c.detail);
        break;
      }
    }
  }

  // 3d. Hair assigned but no geometry at runtime. Not a defect when a wig/hood with a drawable
  // mesh legitimately hides the hair head parts (the game does not draw them by design).
  const bool hair_hidden_by_equipment = std::any_of(
      report.equipment.begin(), report.equipment.end(), [](const EquipmentEntry& eq) {
        return eq.hides_hair && eq.mesh_resolved == Obs::kObserved;
      });
  if (cause_state == CorrelationState::kInconclusive && !hair_hidden_by_equipment &&
      report.hair.assigned == Obs::kObserved &&
      report.hair.geometry_observed == Obs::kMissing) {
    cause_state = CorrelationState::kRuntimeGeometryMissing;
    cause = "el NPC tiene cabello asignado (" + report.hair.assigned_form_key +
            ") pero no se observó geometría capilar cargada en la instancia 3D";
    result.evidence.push_back("hair.assigned=OBSERVED, hair.geometryObserved=MISSING");
  }

  // 4. Attestation-driven terminal states.
  if (attestation && attestation_binds) {
    if (attestation->observation == "bald" || attestation->observation == "face-defect") {
      if (cause_state != CorrelationState::kInconclusive) {
        result.state = CorrelationState::kVisualFailureConfirmed;
        result.reasons.push_back("El usuario confirmó el defecto visual en Skyrim (" +
                                 attestation->observation + ") y se identificó una causa: " +
                                 cause + ".");
        mark_false_negative();
        return result;
      }
      result.state = CorrelationState::kInconclusive;
      result.reasons.push_back(
          "El usuario confirmó el defecto visual (" + attestation->observation +
          ") pero ni la auditoría estática ni la evidencia runtime identifican una causa: caso "
          "abierto, se requiere más instrumentación.");
      mark_false_negative();
      return result;
    }
    // observation == "ok"
    const bool runtime_ok = report.hair.geometry_observed == Obs::kObserved ||
                            report.nodes.facegen_geometry == Obs::kObserved;
    if (cause_state == CorrelationState::kInconclusive && runtime_ok) {
      result.state = CorrelationState::kValidatedInSkyrim;
      result.reasons.push_back(
          "El usuario confirmó visualmente el NPC correcto en Skyrim; la evidencia runtime y la "
          "auditoría estática son coherentes (mismo NPC, perfil y versión).");
      return result;
    }
    result.state = CorrelationState::kInconclusive;
    result.reasons.push_back(
        "El usuario atestigua que el NPC se ve bien, pero la evidencia no es concluyente "
        "(geometría no observada o causa sospechosa presente): no se puede declarar "
        "VALIDATED_IN_SKYRIM.");
    return result;
  }
  if (attestation && !attestation_binds) {
    result.reasons.push_back(
        "Se ignoró un atestado visual por no estar vinculado a este NPC/perfil/versión.");
  }

  // 5. Non-attested terminal states.
  if (cause_state != CorrelationState::kInconclusive) {
    result.state = cause_state;
    result.reasons.push_back("Causa identificada: " + cause + ".");
    result.reasons.push_back(
        "La consecuencia visual sigue requiriendo confirmación humana en Skyrim.");
    mark_false_negative();
    return result;
  }

  // 6. Agreement / default.
  const bool runtime_ok = report.hair.geometry_observed == Obs::kObserved ||
                          report.nodes.facegen_geometry == Obs::kObserved;
  if (runtime_ok && static_clean()) {
    result.state = CorrelationState::kStaticValid;
    result.reasons.push_back(
        "La auditoría estática y la evidencia runtime coinciden; la confirmación visual sigue "
        "pendiente (REQUIRES_CONFIRMACION).");
    return result;
  }
  result.state = CorrelationState::kInconclusive;
  result.reasons.push_back(
      "Evidencia insuficiente para clasificar el estado del NPC; UNKNOWN no se convierte en OK.");
  return result;
}

}  // namespace sns::runtime
