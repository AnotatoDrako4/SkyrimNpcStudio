#include <catch2/catch_test_macros.hpp>

#include "sns/core/error.hpp"
#include "sns/runtime/runtime_report.hpp"

using namespace sns;
using namespace sns::runtime;

// A complete, valid report as the SKSE plugin would emit it for a persistent NPC with SMP hair.
static const char* kValidReport = R"({
  "schemaVersion": 1,
  "kind": "sns.npc-runtime-report",
  "generator": {"name": "sns-runtime-inspector", "version": "0.1.0"},
  "capturedAtLocal": "2026-11-07T15:04:31",
  "session": {
    "skyrimVersion": "1.6.1170.0",
    "skseVersion": "2.2.6",
    "plugins": [
      {"name": "hdtSMP64.dll", "loaded": true, "version": "2.5.1"},
      {"name": "po3_SpellPerkItemDistributor.dll", "loaded": true, "version": ""}
    ]
  },
  "profile": {"mo2Profile": "SNS-Cuerpos-Test", "detection": "inferred"},
  "selection": "npc",
  "npc": {
    "referenceFormId": "0xFF0012AB",
    "referenceIsDynamic": true,
    "baseFormKey": "013bb9:Skyrim.esm",
    "basePlugin": "Skyrim.esm",
    "editorId": "AdrianneAvenicci",
    "displayName": "Adrianne Avenicci",
    "raceFormKey": "013746:Skyrim.esm",
    "raceEditorId": "NordRace",
    "sex": "female",
    "has3d": "OBSERVED"
  },
  "headParts": [
    {"formKey": "0A1234:KS Hairdos.esp", "editorId": "KSHair01", "type": "Hair"},
    {"formKey": "0642:Skyrim.esm", "editorId": "FemaleEyesHumanBlue", "type": "Eyes"}
  ],
  "hair": {
    "assigned": "OBSERVED",
    "assignedFormKey": "0A1234:KS Hairdos.esp",
    "geometryObserved": "OBSERVED",
    "geometryTriangles": 14202,
    "smpStatus": "ACTIVE",
    "visualStatus": "REQUIRES_CONFIRMACION"
  },
  "nodes": {
    "headNodes": ["NPC Head", "BSFaceGenNiNodeSkinned"],
    "hairNodes": [{"name": "KSHair01", "triangles": 14202, "cullFlagged": false}],
    "faceGenGeometry": "OBSERVED"
  },
  "equipment": [
    {"slot": 31, "formKey": "0B77:Wigs.esl", "editorId": "WigLong", "kind": "wig",
     "hidesHair": true, "meshPath": "meshes\\wigs\\long.nif", "meshResolved": "OBSERVED"}
  ],
  "distribution": [],
  "checks": [{"id": "HAIR_NODE_PRESENT", "state": "PASS", "detail": "KSHair01"}],
  "warnings": [],
  "unavailableChecks": ["MO2_PROFILE_DIRECT"]
})";

TEST_CASE("runtime report: valid report parses and round-trips", "[runtime-report]") {
  const RuntimeReport report = parse_runtime_report(kValidReport);
  CHECK(report.schema_version == 1);
  CHECK(report.selection == SelectionKind::kNpc);
  REQUIRE(report.npc.has_value());
  CHECK(report.npc->base_form_key == "013BB9:Skyrim.esm");  // hex normalizado a mayúsculas
  CHECK(report.npc->reference_is_dynamic);
  CHECK(report.npc->has_3d == Obs::kObserved);
  REQUIRE(report.head_parts.size() == 2);
  CHECK(report.head_parts[0].type == "Hair");
  CHECK(report.hair.assigned == Obs::kObserved);
  CHECK(report.hair.smp_status == SmpStatus::kActive);
  CHECK(report.hair.visual_status == "REQUIRES_CONFIRMACION");
  REQUIRE(report.equipment.size() == 1);
  CHECK(report.equipment[0].hides_hair);
  CHECK(report.equipment[0].mesh_resolved == Obs::kObserved);
  REQUIRE(report.unavailable_checks.size() == 1);

  const RuntimeReport reparsed = parse_runtime_report(serialize_runtime_report(report));
  CHECK(reparsed.npc->base_form_key == report.npc->base_form_key);
  CHECK(reparsed.hair.smp_status == report.hair.smp_status);
  CHECK(reparsed.equipment.size() == report.equipment.size());
}

TEST_CASE("runtime report: ESL form keys are accepted and kept", "[runtime-report]") {
  CHECK(is_valid_form_key("803:Wigs.esl"));
  CHECK(is_valid_form_key("013BB9:Skyrim.esm"));
  CHECK_FALSE(is_valid_form_key("Skyrim.esm:013BB9"));
  CHECK_FALSE(is_valid_form_key("013BB9"));
  CHECK_FALSE(is_valid_form_key("0x013BB9:Skyrim.esm"));
  CHECK_FALSE(is_valid_form_key("ZZZ:Skyrim.esm"));
  CHECK(form_key_local_id("013bb9:Skyrim.esm") == "013BB9");
  CHECK(form_key_plugin("013BB9:Skyrim.esm") == "Skyrim.esm");
}

TEST_CASE("runtime report: empty selection is a valid report without npc block", "[runtime-report]") {
  const char* text = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report",
    "generator": {"name": "sns-runtime-inspector", "version": "0.1.0"},
    "selection": "empty", "warnings": ["ninguna referencia seleccionada en consola"]
  })";
  const RuntimeReport report = parse_runtime_report(text);
  CHECK(report.selection == SelectionKind::kEmpty);
  CHECK_FALSE(report.npc.has_value());
  REQUIRE(report.warnings.size() == 1);
}

TEST_CASE("runtime report: selection=npc without npc block is rejected", "[runtime-report]") {
  const char* text = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report", "selection": "npc"
  })";
  CHECK_THROWS_AS(parse_runtime_report(text), Error);
}

TEST_CASE("runtime report: malformed JSON throws Parse error", "[runtime-report]") {
  try {
    parse_runtime_report("{ not json !!");
    FAIL("se esperaba excepción");
  } catch (const Error& e) {
    CHECK(e.kind() == ErrorKind::Parse);
  }
}

TEST_CASE("runtime report: unsupported schema version is rejected", "[runtime-report]") {
  const char* text = R"({"schemaVersion": 2, "kind": "sns.npc-runtime-report", "selection": "empty"})";
  try {
    parse_runtime_report(text);
    FAIL("se esperaba excepción");
  } catch (const Error& e) {
    CHECK(e.kind() == ErrorKind::InvalidArgument);
  }
}

TEST_CASE("runtime report: invalid FormKey in npc block is rejected", "[runtime-report]") {
  const char* text = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report", "selection": "npc",
    "npc": {"baseFormKey": "no-es-un-formkey"}
  })";
  CHECK_THROWS_AS(parse_runtime_report(text), Error);
}

TEST_CASE("runtime report: UNKNOWN is never upgraded implicitly", "[runtime-report]") {
  const char* text = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report", "selection": "npc",
    "npc": {"baseFormKey": "013BB9:Skyrim.esm"},
    "hair": {}
  })";
  const RuntimeReport report = parse_runtime_report(text);
  CHECK(report.hair.assigned == Obs::kUnknown);
  CHECK(report.hair.geometry_observed == Obs::kUnknown);
  CHECK(report.hair.smp_status == SmpStatus::kUnknown);
  CHECK(report.hair.visual_status == "REQUIRES_CONFIRMACION");
}

TEST_CASE("runtime report: invalid enum and check states are rejected", "[runtime-report]") {
  const char* bad_obs = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report", "selection": "npc",
    "npc": {"baseFormKey": "013BB9:Skyrim.esm"},
    "hair": {"assigned": "OK"}
  })";
  CHECK_THROWS_AS(parse_runtime_report(bad_obs), Error);

  const char* bad_check = R"({
    "schemaVersion": 1, "kind": "sns.npc-runtime-report", "selection": "empty",
    "checks": [{"id": "X", "state": "BIEN", "detail": ""}]
  })";
  CHECK_THROWS_AS(parse_runtime_report(bad_check), Error);
}
