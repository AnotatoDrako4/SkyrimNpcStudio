#include <catch2/catch_test_macros.hpp>

#include "sns/appearance/facegen_smp_preservation.hpp"
#include "sns/appearance/fsmp_preflight.hpp"
#include "sns/appearance/hair_graph.hpp"
#include "sns/appearance/hair_regression_guard.hpp"
#include "sns/appearance/hair_safe_recovery.hpp"
#include "sns/appearance/npc_audit.hpp"
#include "sns/appearance/npc_runtime_inspector.hpp"
#include "sns/appearance/scan_completeness.hpp"
#include "sns/appearance/ube_smp_validator.hpp"

using namespace sns::appearance;

namespace {

AssetSource loose_asset(std::string path, std::string mod) {
  AssetSource a;
  a.path = std::move(path);
  a.found = true;
  a.kind = "loose";
  a.winner_mod = mod;
  a.loose_mods = {std::move(mod)};
  return a;
}

AssetSource missing_asset(std::string path) {
  AssetSource a;
  a.path = std::move(path);
  a.found = false;
  return a;
}

ControlInputs minimal_control() {
  ControlInputs in;
  in.facegen = loose_asset("meshes/actors/character/facegendata/facegeom/skyrim.esm/0001a696.nif", "Skyrim.esm");
  in.facetint = loose_asset("textures/actors/character/facegendata/facetint/skyrim.esm/0001a696.dds", "Skyrim.esm");
  return in;
}

}  // namespace

// ============================================================================
// 1. HDF + UBE sin parche correspondiente
// ============================================================================
TEST_CASE("P0 Hair SMP: 1. HDF + UBE sin parche correspondiente emite HAIR_SMP_009", "[smp][forensics]") {
  HairAssignmentGraph g;
  g.npc_form_key = "0001A696:Skyrim.esm";
  g.effective_race = "UBENordRace";
  g.body_family = "ube";
  g.is_ube = true;

  HairResourceNode node;
  node.form_key = "001234:HDF.esp";
  node.source_kind = HairSourceKind::Hdf;
  node.stage = AssignmentStage::PlannedDynamic;
  node.model_path = "meshes/hdf/hair/style01.nif";
  node.mesh_source = loose_asset(node.model_path, "HDF");
  g.nodes.push_back(node);

  UbeSmpEnvironment env;
  env.hdf_installed = true;
  env.ube_installed = true;
  env.ube_hdf_patch_installed = false;  // Falta parche UBE!

  const auto report = validate_ube_smp_compatibility(g, env);
  CHECK_FALSE(report.compatible);
  CHECK(report.is_ube);
  CHECK(report.hdf_targeted);
  CHECK_FALSE(report.ube_patch_present);
  REQUIRE_FALSE(report.issues.empty());
  CHECK(report.issues.front().code == "HAIR_SMP_009");
  CHECK(report.issues.front().severity == "alta");
}

// ============================================================================
// 2. HDF + UBE con parche válido
// ============================================================================
TEST_CASE("P0 Hair SMP: 2. HDF + UBE con parche válido resulta compatible", "[smp][forensics]") {
  HairAssignmentGraph g;
  g.npc_form_key = "0001A696:Skyrim.esm";
  g.effective_race = "UBENordRace";
  g.body_family = "ube";
  g.is_ube = true;

  HairResourceNode node;
  node.form_key = "001234:HDF.esp";
  node.source_kind = HairSourceKind::Hdf;
  node.stage = AssignmentStage::PlannedDynamic;
  node.model_path = "meshes/hdf/hair/style01.nif";
  node.mesh_source = loose_asset(node.model_path, "HDF");
  g.nodes.push_back(node);

  UbeSmpEnvironment env;
  env.hdf_installed = true;
  env.ube_installed = true;
  env.ube_hdf_patch_installed = true;  // Parche presente!
  env.hair_suppression_fix_installed = true;
  env.active_body_collision_family = "ube";

  const auto report = validate_ube_smp_compatibility(g, env);
  CHECK(report.compatible);
  CHECK(report.ube_patch_present);
  CHECK(report.issues.empty());
}

// ============================================================================
// 3. SMP con XML ausente
// ============================================================================
TEST_CASE("P0 Hair SMP: 3. SMP con XML ausente deja cabello visible pero estático", "[smp][forensics]") {
  AuditSubject s;
  s.form_key = "0001A696:Skyrim.esm";
  s.editor_id = "Aela";
  s.winning_plugin = "Skyrim.esm";

  AuditSmpFaceGenHair h;
  h.shape = "HairAela";
  h.source_mesh = "meshes/hair/aela.nif";
  h.source_physics_xml = "meshes/hair/aela_physics.xml";
  h.facegen_has_physics_xml = true;
  h.physics_xml = missing_asset(h.source_physics_xml);
  s.smp_facegen = {h};

  ControlInputs in = minimal_control();
  const auto audit = audit_npc_appearance(s, in, analyze_appearance_control(in));

  const auto it = std::find_if(audit.findings.begin(), audit.findings.end(), [](const AuditFinding& f) {
    return f.code == "HAIR_SMP_002";
  });
  REQUIRE(it != audit.findings.end());
  CHECK(it->status == kStatusConfirmed);
  CHECK(it->severity == "media");
  CHECK(it->evidence.find("visible") != std::string::npos);
}

// ============================================================================
// 4. Peinado estático sin XML porque no requiere física
// ============================================================================
TEST_CASE("P0 Hair SMP: 4. Peinado estático sin XML no emite alerta de falta de físicas", "[smp][forensics]") {
  AuditSubject s;
  s.form_key = "0001A696:Skyrim.esm";
  s.editor_id = "Aela";
  s.winning_plugin = "Skyrim.esm";

  AuditHeadPart hp;
  hp.type = "Hair";
  hp.model_path = "meshes/hair/static_hair.nif";
  hp.mesh = loose_asset(hp.model_path, "Vanilla");
  s.head_parts = {hp};
  s.facegen_has_hair = true;

  ControlInputs in = minimal_control();
  const auto audit = audit_npc_appearance(s, in, analyze_appearance_control(in));

  // No debe contener alertas de SMP
  for (const auto& f : audit.findings) {
    CHECK(f.code.find("HAIR_SMP_") == std::string::npos);
  }
}

// ============================================================================
// 5. Fallo de carga FSMP
// ============================================================================
TEST_CASE("P0 Hair SMP: 5. Fallo de carga o ausencia de DLL FSMP", "[smp][forensics]") {
  const auto report = run_fsmp_preflight_check(false, "1.6.1170", "2.2.6");
  CHECK_FALSE(report.fsmp_dll_found);
  CHECK(report.has_critical_errors);
  REQUIRE_FALSE(report.issues.empty());
  CHECK(report.issues.front().code == "HAIR_SMP_008");

  SECTION("Logs de FSMP con errores reportan issue") {
    const std::vector<std::string> logs = {
        "hdtSMP64 init ok",
        "ERROR: failed to resolve bone hierarchy for HeadPart hair",
        "crash avoided by fallback",
    };
    const auto log_rep = run_fsmp_preflight_check(true, "1.6.1170", "2.2.6", logs);
    CHECK(log_rep.fsmp_dll_found);
    CHECK_FALSE(log_rep.parsed_log_warnings.empty());
  }
}

// ============================================================================
// 6. Distribución SPID conflictiva
// ============================================================================
TEST_CASE("P0 Hair SMP: 6. Detección de distribución SPID modificada o conflictiva", "[smp][forensics]") {
  ProfileHairBaseline base;
  base.profile_name = "SNS-Cuerpos-Test";

  NpcHairSnapshot snap1;
  snap1.form_key = "0001A696:Skyrim.esm";
  snap1.editor_id = "Aela";
  snap1.winner_mod = "ModA";
  snap1.spid_rule_signature = "Outfit_Aela_01";
  base.npcs[snap1.form_key] = snap1;

  std::map<std::string, NpcHairSnapshot> current;
  NpcHairSnapshot snap2 = snap1;
  snap2.spid_rule_signature = "Outfit_Aela_02_Wig";  // SPID rule changed!
  current[snap2.form_key] = snap2;

  const auto reg = detect_hair_regressions(base, current);
  CHECK(reg.has_regressions);
  REQUIRE_FALSE(reg.invalidated_npc_keys.empty());
  CHECK(reg.invalidated_npc_keys.front() == "0001A696:Skyrim.esm");
  CHECK(reg.regressions.front().change_kind == "SPID_RULE_CHANGED");
}

// ============================================================================
// 7. Peluca bloqueada por otro equipamiento
// ============================================================================
TEST_CASE("P0 Hair SMP: 7. Peluca bloqueada por equipamiento o supresor", "[smp][forensics]") {
  HairAssignmentGraph g;
  g.npc_form_key = "0001A696:Skyrim.esm";

  HairResourceNode wig_node;
  wig_node.form_key = "005555:Wigs.esp";
  wig_node.source_kind = HairSourceKind::Wig;
  wig_node.slot_mask = (1u << 31);
  wig_node.suppressed_by_equipment = true;
  wig_node.suppressing_equipment = "CascoPesadoNordico";
  g.nodes.push_back(wig_node);

  UbeSmpEnvironment env;
  const auto rep = validate_ube_smp_compatibility(g, env);
  CHECK_FALSE(rep.compatible);
  REQUIRE_FALSE(rep.issues.empty());
  CHECK(rep.issues.front().code == "HAIR_SMP_006");
}

// ============================================================================
// 8. FaceGen con huesos SMP perdidos o rest pose desplazado
// ============================================================================
TEST_CASE("P0 Hair SMP: 8. FaceGen SMP preservation detecta perdida de metadatos o desalineacion osea", "[smp][forensics]") {
  AuditSmpFaceGenHair h;
  h.shape = "BDOrHairAlina";
  h.source_mesh = "meshes/hair/alina.nif";
  h.source_physics_xml = "meshes/hair/alina.xml";
  h.facegen_has_physics_xml = false;  // Stripped NiStringExtraData
  h.max_bone_offset = 5.2f;           // Rest pose offset > 0.5 units
  h.bones_missing_in_source = {"ExtraBone01"};

  const auto rep = audit_facegen_smp_preservation({h});
  CHECK_FALSE(rep.intact);
  REQUIRE(rep.issues.size() == 3);
  CHECK(rep.issues[0].code == "HAIR_SMP_003");
  CHECK(rep.issues[1].code == "HAIR_SMP_004");
  CHECK(rep.issues[2].code == "HAIR_SMP_005");
}

// ============================================================================
// 9. NPC intencionalmente calvo
// ============================================================================
TEST_CASE("P0 Hair SMP: 9. NPC intencionalmente calvo se mantiene limpio", "[smp][forensics]") {
  AuditSubject s;
  s.form_key = "000136C4:Skyrim.esm";
  s.editor_id = "Vignar";
  s.winning_plugin = "Skyrim.esm";
  // Sin headparts de cabello
  ControlInputs in = minimal_control();
  const auto audit = audit_npc_appearance(s, in, analyze_appearance_control(in));

  CHECK(audit.status == kStatusClean);
  const bool has_calvo_exemption = std::find(audit.exemptions.begin(), audit.exemptions.end(), "calvo-intencional") != audit.exemptions.end();
  CHECK(has_calvo_exemption);
}

// ============================================================================
// 10. NPC no inspeccionado (nunca clasificado como sano)
// ============================================================================
TEST_CASE("P0 Hair SMP: 10. ScanCompletenessGate previene marcar NPC no inspeccionados como sanos", "[smp][forensics]") {
  ScanCompletenessGate gate;
  gate.register_indexed("0001A696:Skyrim.esm", "Aela");
  gate.register_indexed("0001A697:Skyrim.esm", "Skjor");
  gate.register_indexed("0001A698:Skyrim.esm", "Vilkas");

  // Only Aela was inspected
  gate.mark_inspected_clean("0001A696:Skyrim.esm");

  const auto rep = gate.build_report();
  CHECK(rep.total_indexed == 3);
  CHECK(rep.total_inspected == 1);
  CHECK(rep.total_clean == 1);
  CHECK(rep.total_uninspected == 2);
  CHECK(rep.is_mathematically_complete());

  // Invariant check: uninspected are NOT clean
  for (const auto& entry : gate.entries()) {
    if (entry.category == NpcScopeCategory::Uninspected) {
      CHECK(entry.category != NpcScopeCategory::InspectedClean);
    }
  }
}

// ============================================================================
// 11. Cambio de prioridad MO2
// ============================================================================
TEST_CASE("P0 Hair SMP: 11. Cambio de prioridad MO2 activa regresion", "[smp][forensics]") {
  ProfileHairBaseline base;
  base.profile_name = "SNS-Cuerpos-Test";

  NpcHairSnapshot s1;
  s1.form_key = "0001A696:Skyrim.esm";
  s1.winner_mod = "HairMod_Priority10";
  s1.assigned_hair_model = "meshes/hair1.nif";
  base.npcs[s1.form_key] = s1;

  std::map<std::string, NpcHairSnapshot> current;
  NpcHairSnapshot s2 = s1;
  s2.winner_mod = "HairMod_Priority20_Overwrite";  // Changed winning mod
  current[s2.form_key] = s2;

  const auto reg = detect_hair_regressions(base, current);
  CHECK(reg.has_regressions);
  REQUIRE_FALSE(reg.regressions.empty());
  CHECK(reg.regressions.front().change_kind == "PROVIDER_CHANGED");
}

// ============================================================================
// 12. Cambio de distribuidor
// ============================================================================
TEST_CASE("P0 Hair SMP: 12. Cambio de distribuidor dinamico activa invalidacion diferencial", "[smp][forensics]") {
  ProfileHairBaseline base;
  base.profile_name = "SNS-Cuerpos-Test";

  NpcHairSnapshot s1;
  s1.form_key = "00028B07:Skyrim.esm";
  s1.spid_rule_signature = "DISTR_VANILLA";
  base.npcs[s1.form_key] = s1;

  std::map<std::string, NpcHairSnapshot> current;
  NpcHairSnapshot s2 = s1;
  s2.spid_rule_signature = "DISTR_HDF_WIGS_MODERN";
  current[s2.form_key] = s2;

  const auto reg = detect_hair_regressions(base, current);
  CHECK(reg.has_regressions);
  CHECK(reg.invalidated_npc_keys.front() == "00028B07:Skyrim.esm");
}

// ============================================================================
// 13. Cancelacion durante auditoria
// ============================================================================
TEST_CASE("P0 Hair SMP: 13. Cancelacion durante auditoria reporta balance matematico exacto", "[smp][forensics]") {
  ScanCompletenessGate gate;
  for (int i = 0; i < 100; ++i) {
    gate.register_indexed("FormKey_" + std::to_string(i), "Editor_" + std::to_string(i));
  }
  // User cancels after inspecting 15
  for (int i = 0; i < 15; ++i) {
    gate.mark_inspected_clean("FormKey_" + std::to_string(i));
  }
  const auto rep = gate.build_report();
  CHECK(rep.total_indexed == 100);
  CHECK(rep.total_inspected == 15);
  CHECK(rep.total_uninspected == 85);
  CHECK(rep.is_mathematically_complete());
}

// ============================================================================
// 14. Reparacion que requiere verificacion visual en Skyrim
// ============================================================================
TEST_CASE("P0 Hair SMP: 14. Reparacion queda REPARADO_EN_ARCHIVOS y REQUIERE_VALIDACION_EN_SKYRIM", "[smp][forensics]") {
  NpcAudit audit;
  audit.form_key = "0001A696:Skyrim.esm";
  AuditFinding finding;
  finding.code = "HAIR_SMP_007";
  finding.cause = "Peluca sin modelo ARMA para la raza";
  finding.action = "Crear parche de resolución";
  audit.findings.push_back(finding);

  const auto proposal = plan_hair_safe_recovery(audit, "0001A690:Skyrim.esm");
  CHECK(proposal.status == kStatusRepairedInFiles);
  CHECK(proposal.verification_state == kVerifyInGame);
  CHECK(proposal.can_restore_previous);
  REQUIRE_FALSE(proposal.steps.empty());
  CHECK(proposal.steps.front().is_reversible);
  CHECK(proposal.ready_for_export);
}

// ============================================================================
// 15. FIXTURE DE FALSO NEGATIVO DEL AUDITOR ANTERIOR & RUNTIME CORRELATION
// ============================================================================
TEST_CASE("P0 Hair SMP: 15. FIXTURE FALSO NEGATIVO: detectado y correlacionado con runtime", "[smp][forensics]") {
  // En el auditor anterior:
  // El NPC tiene un registro con HeadPart de cabello válido ("HairFemaleNord18") y FaceGen válido.
  // El auditor antiguo decía: "SIN_ERROR_DETECTADO".
  NpcAudit fake_old_audit;
  fake_old_audit.form_key = "0001A696:Skyrim.esm";
  fake_old_audit.status = kStatusClean;  // El falso negativo histórico!

  // Pero el inspector en runtime captura que la NPC se ve calva en Skyrim
  // porque una peluca equipada en slot 31 carece de ARMA:
  const std::string runtime_json = R"({
    "schemaVersion": 1,
    "captureTimestampUtc": "2026-11-07T15:20:00Z",
    "gameVersion": "1.6.1170.0",
    "skseVersion": "2.2.6",
    "profile": "SNS-Cuerpos-Test",
    "npc": {
      "baseFormKey": "0001A696:Skyrim.esm",
      "referenceFormId": "0001A697",
      "editorId": "AelaTheHuntress",
      "effectiveRace": "UBENordRace"
    },
    "hair": {
      "assignedSource": "WORN_WIG",
      "headPartFormKey": "0001A690:Skyrim.esm",
      "faceGenGeometryObserved": true,
      "wornWigFormKey": "0012DA:[Dint999] BDOr_Hairstyles.esp",
      "wornWigSlot": 31,
      "smpActive": true,
      "visualStatus": "BALD"
    },
    "headNodes": [],
    "equippedItems": [
      {
        "slot": 31,
        "formKey": "0012DA:[Dint999] BDOr_Hairstyles.esp",
        "name": "BDOrWigAlina",
        "hasAddonForRace": false
      }
    ]
  })";

  const auto parsed_opt = parse_npc_runtime_report(runtime_json);
  REQUIRE(parsed_opt.has_value());
  const auto& runtime_report = *parsed_opt;

  // Cruce y correlación:
  const auto correlation = correlate_static_and_runtime(fake_old_audit, runtime_report);
  CHECK(correlation.false_negative_detected);
  CHECK(correlation.state == CorrelationState::VisualFailureConfirmed);
  CHECK(correlation.false_negative_reason.find("Falso negativo por equipamiento") != std::string::npos);
  REQUIRE_FALSE(correlation.suggested_actions.empty());

  // Serialización y reconstrucción de reporte runtime:
  const std::string serialized = serialize_npc_runtime_report(runtime_report);
  CHECK(serialized.find("BDOrWigAlina") != std::string::npos);
}
