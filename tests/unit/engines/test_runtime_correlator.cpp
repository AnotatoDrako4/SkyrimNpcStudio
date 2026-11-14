#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "sns/runtime/correlator.hpp"

using namespace sns::runtime;

namespace {

// Static row as produced by the NPC Health scan for a clean NPC (the false-negative setup).
StaticAuditRow clean_row() {
  StaticAuditRow row;
  row.form_key = "013BB9:Skyrim.esm";
  row.editor_id = "AdrianneAvenicci";
  row.status = "SIN_ERROR_DETECTADO";
  row.facegen_parsed = true;
  row.facegen_has_hair = true;
  return row;
}

RuntimeReport base_report() {
  RuntimeReport report;
  report.generator_name = "sns-runtime-inspector";
  report.generator_version = "0.1.0";
  report.captured_at_local = "2026-11-07T15:04:31";
  report.session.skyrim_version = "1.6.1170.0";
  report.session.skse_version = "2.2.6";
  report.session.plugins.push_back({"hdtSMP64.dll", true, "2.5.1"});
  report.profile.mo2_profile = "SNS-Cuerpos-Test";
  report.profile.detection = "inferred";
  report.selection = SelectionKind::kNpc;
  NpcIdentity npc;
  npc.reference_form_id = "0xFF0012AB";
  npc.reference_is_dynamic = true;
  npc.base_form_key = "013BB9:Skyrim.esm";
  npc.base_plugin = "Skyrim.esm";
  npc.sex = "female";
  npc.has_3d = Obs::kObserved;
  report.npc = npc;
  report.hair.assigned = Obs::kObserved;
  report.hair.assigned_form_key = "0A1234:KS Hairdos.esp";
  report.hair.geometry_observed = Obs::kObserved;
  report.hair.geometry_triangles = 14202;
  report.hair.smp_status = SmpStatus::kActive;
  report.nodes.facegen_geometry = Obs::kObserved;
  return report;
}

}  // namespace

TEST_CASE("correlator: static clean + runtime geometry observed -> STATIC_VALID, visual pending",
          "[runtime-correlator]") {
  const auto result = correlate(clean_row(), base_report());
  CHECK(result.state == CorrelationState::kStaticValid);
  CHECK_FALSE(result.false_negative);
}

TEST_CASE("correlator: THE false negative - clean static, hair assigned, no runtime geometry",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.hair.geometry_observed = Obs::kMissing;
  report.hair.geometry_triangles = 0;
  report.hair.smp_status = SmpStatus::kUnknown;  // no SMP angle: pure geometry absence
  report.nodes.facegen_geometry = Obs::kMissing;

  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kRuntimeGeometryMissing);
  CHECK(result.false_negative);  // the auditor said SIN_ERROR_DETECTADO
  REQUIRE_FALSE(result.reasons.empty());
}

TEST_CASE("correlator: wig hides hair and wig mesh missing -> RUNTIME_ASSET_MISSING",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.hair.geometry_observed = Obs::kMissing;  // expected: the wig hides it
  report.nodes.facegen_geometry = Obs::kObserved;
  EquipmentEntry wig;
  wig.slot = 31;
  wig.form_key = "803:Wigs.esl";
  wig.editor_id = "WigLong";
  wig.kind = "wig";
  wig.hides_hair = true;
  wig.mesh_path = "meshes\\wigs\\long.nif";
  wig.mesh_resolved = Obs::kMissing;
  report.equipment.push_back(wig);

  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kRuntimeAssetMissing);
  CHECK(result.false_negative);
}

TEST_CASE("correlator: wig hides hair but its mesh draws -> fully explained, not a defect",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.hair.geometry_observed = Obs::kMissing;  // hidden by the wig, on purpose
  EquipmentEntry wig;
  wig.slot = 31;
  wig.form_key = "803:Wigs.esl";
  wig.hides_hair = true;
  wig.mesh_resolved = Obs::kObserved;
  report.equipment.push_back(wig);

  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kStaticValid);  // la peluca dibuja; el cabello oculto es esperado
  CHECK_FALSE(result.false_negative);
}

TEST_CASE("correlator: SMP hair declared but FSMP not loaded -> SMP_DEPENDENCY_ERROR",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.session.plugins.clear();  // FSMP absent in this session
  report.hair.geometry_observed = Obs::kMissing;
  report.hair.smp_status = SmpStatus::kPresentNoPhysics;

  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kSmpDependencyError);
  CHECK(result.false_negative);
}

TEST_CASE("correlator: FSMP log error mentioning the NPC -> SMP_DEPENDENCY_ERROR",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.hair.geometry_observed = Obs::kMissing;
  report.hair.smp_status = SmpStatus::kUnknown;

  LogEvidence logs;
  logs.fsmp_present_in_session = true;
  logs.fsmp_load_or_xml_error = true;
  logs.fsmp_error_mentions_npc = true;

  const auto result = correlate(clean_row(), report, std::nullopt, &logs);
  CHECK(result.state == CorrelationState::kSmpDependencyError);
}

TEST_CASE("correlator: dynamic distribution evidence -> DYNAMIC_ASSIGNMENT_SUSPECTED",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.distribution.push_back(
      {"SPID", "Hair distribution aplicó un peinado distinto al del registro", "0C45:HDF.esp"});

  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kDynamicAssignmentSuspected);
  CHECK(result.false_negative);
}

TEST_CASE("correlator: different base FormKey refuses the correlation", "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.npc->base_form_key = "0D801:OtherMod.esp";
  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kIdentityMismatch);
}

TEST_CASE("correlator: empty selection -> NO_RUNTIME_SELECTION", "[runtime-correlator]") {
  RuntimeReport report;
  report.selection = SelectionKind::kEmpty;
  const auto result = correlate(clean_row(), report);
  CHECK(result.state == CorrelationState::kNoRuntimeSelection);
}

TEST_CASE("correlator: attested bald + concrete cause -> VISUAL_FAILURE_CONFIRMED",
          "[runtime-correlator]") {
  RuntimeReport report = base_report();
  report.hair.geometry_observed = Obs::kMissing;
  report.nodes.facegen_geometry = Obs::kMissing;
  report.hair.smp_status = SmpStatus::kUnknown;

  VisualAttestation att;
  att.npc_form_key = "013BB9:Skyrim.esm";
  att.profile = "SNS-Cuerpos-Test";
  att.skyrim_version = "1.6.1170.0";
  att.observation = "bald";

  const auto result = correlate(clean_row(), report, att);
  CHECK(result.state == CorrelationState::kVisualFailureConfirmed);
  CHECK(result.false_negative);
}

TEST_CASE("correlator: attested bald without identified cause stays an open case",
          "[runtime-correlator]") {
  // Runtime sees hair geometry, static is clean, but the user swears the NPC is bald:
  // honest systems keep this open instead of inventing a cause.
  VisualAttestation att;
  att.npc_form_key = "013BB9:Skyrim.esm";
  att.profile = "SNS-Cuerpos-Test";
  att.skyrim_version = "1.6.1170.0";
  att.observation = "bald";

  const auto result = correlate(clean_row(), base_report(), att);
  CHECK(result.state == CorrelationState::kInconclusive);
  CHECK(result.false_negative);
}

TEST_CASE("correlator: attested ok with full binding -> VALIDATED_IN_SKYRIM",
          "[runtime-correlator]") {
  VisualAttestation att;
  att.npc_form_key = "013BB9:Skyrim.esm";
  att.profile = "SNS-Cuerpos-Test";
  att.skyrim_version = "1.6.1170";  // tolerated: same triplet
  att.observation = "ok";

  const auto result = correlate(clean_row(), base_report(), att);
  CHECK(result.state == CorrelationState::kValidatedInSkyrim);
  CHECK_FALSE(result.false_negative);
}

TEST_CASE("correlator: attestation from another profile is rejected", "[runtime-correlator]") {
  VisualAttestation att;
  att.npc_form_key = "013BB9:Skyrim.esm";
  att.profile = "Default";  // not the report's profile
  att.skyrim_version = "1.6.1170.0";
  att.observation = "ok";

  const auto result = correlate(clean_row(), base_report(), att);
  CHECK(result.state != CorrelationState::kValidatedInSkyrim);
  REQUIRE(result.attestation_rejections.size() == 1);
}

TEST_CASE("correlator: static row parser accepts the real NPC Health scan shape",
          "[runtime-correlator]") {
  // Trimmed excerpt of a real health.rows[] entry (findings[].resource carries the FormKey).
  const char* row_json = R"({
    "codes": ["NPC_001", "FACE_003", "HAIR_006", "NPC_004"],
    "definingPlugin": "Skyrim.esm",
    "editorId": "AdrianneAvenicci",
    "exemptions": [],
    "faceGenForeignHair": ["NITHIWR_Wing1111"],
    "faceGenHasHair": true,
    "faceGenParsed": true,
    "findings": [
      {"code": "NPC_001", "status": "CONFIRMADO", "resource": "013BB9:Skyrim.esm"},
      {"code": "HAIR_006", "status": "SOSPECHOSO", "resource": "013BB9:Skyrim.esm"}
    ]
  })";
  const StaticAuditRow row = parse_static_audit_row(row_json);
  CHECK(row.form_key == "013BB9:Skyrim.esm");
  CHECK(row.editor_id == "AdrianneAvenicci");
  REQUIRE(row.facegen_has_hair.has_value());
  CHECK(*row.facegen_has_hair);
  REQUIRE(row.confirmed_codes.size() == 1);
  CHECK(row.confirmed_codes[0] == "NPC_001");
  CHECK(std::find(row.codes.begin(), row.codes.end(), "HAIR_006") != row.codes.end());
}
