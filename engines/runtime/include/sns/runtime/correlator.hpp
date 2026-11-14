#pragma once

// Static (MO2 audit) vs runtime (SKSE inspector) vs visual (human) correlation.
//
// Inputs:
//   - one row of the NPC Health scan (health.rows[] of npc-health-scan.json), parsed tolerantly;
//   - one runtime report produced by the SKSE plugin;
//   - optionally, a human visual attestation recorded in Skyrim.
//
// Rules:
//  - Identity is the base NPC FormKey on both sides. A mismatch refuses the correlation
//    (IDENTITY_MISMATCH), it never "best-effort" merges.
//  - VALIDATED_IN_SKYRIM requires a human attestation bound to the same NPC, profile and Skyrim
//    version, and a consistent runtime+static state. Nothing else produces it.
//  - A static "SIN_ERROR_DETECTADO" contradicted by runtime evidence is a false negative and is
//    flagged as such; it is never silently reclassified as OK.

#include <optional>
#include <string>
#include <vector>

#include "sns/runtime/runtime_report.hpp"

namespace sns::runtime {

enum class CorrelationState {
  kStaticValid,                // static audit and runtime agree, nothing anomalous
  kRuntimeAssetMissing,        // a resource the runtime needs (wig mesh, hair mesh) is not there
  kRuntimeGeometryMissing,     // hair assigned but no geometry evidence at runtime
  kSmpDependencyError,         // SMP declared/required but FSMP missing or erroring
  kDynamicAssignmentSuspected, // runtime head parts differ from the static record (SPID/HDF...)
  kVisualFailureConfirmed,     // human attestation of a visual defect + a concrete identified cause
  kInconclusive,               // not enough observable evidence
  kValidatedInSkyrim,          // human attestation of correct visuals, bound to npc+profile+version
  kIdentityMismatch,           // static row and runtime report are not the same NPC
  kNoRuntimeSelection,         // the runtime report has no NPC selected
};

struct StaticAuditRow {
  std::string form_key;     // normalized; empty when the row could not provide one
  std::string editor_id;
  std::string status;       // "CONFIRMADO" | "SOSPECHOSO" | "SIN_ERROR_DETECTADO" | ...
  std::vector<std::string> codes;
  std::vector<std::string> confirmed_codes;
  std::vector<std::string> exemptions;
  std::optional<bool> facegen_parsed;
  std::optional<bool> facegen_has_hair;
  std::vector<std::string> facegen_foreign_hair;
};

// Tolerant parser for one health.rows[] entry of npc-health-scan.json.
// Throws sns::Error(Parse/InvalidArgument) on malformed input.
StaticAuditRow parse_static_audit_row(const std::string& json_text);

struct VisualAttestation {
  std::string npc_form_key;   // required; must match both sides
  std::string profile;        // MO2 profile where it was observed
  std::string skyrim_version; // e.g. "1.6.1170.0"
  std::string observation;    // "bald" | "face-defect" | "ok"
  std::string captured_at;    // free text (ISO-8601 recommended)
};

// Advisory evidence distilled from session logs (produced via log_reader). Only strong,
// session-fresh mentions should set these flags; stale or ambiguous evidence must stay false.
struct LogEvidence {
  bool fsmp_present_in_session = false;        // FSMP_VERSION line seen in hdtSMP64.log
  bool fsmp_load_or_xml_error = false;         // any FSMP_LOAD_FAILED / FSMP_XML_PARSE_ERROR line
  bool fsmp_error_mentions_npc = false;        // the error line mentions the NPC's form id
  bool spid_distribution_mentions_npc = false; // SPID distributed/added something to this NPC
};

struct CorrelationResult {
  CorrelationState state = CorrelationState::kInconclusive;
  bool false_negative = false;  // static said clean; runtime/visual evidence says otherwise
  std::string static_status;    // as reported by the static audit
  std::vector<std::string> reasons;   // ordered, human-readable, Spanish
  std::vector<std::string> evidence;  // concrete facts: form keys, node names, paths
  std::vector<std::string> attestation_rejections;  // why an attestation was not binding
};

CorrelationResult correlate(const StaticAuditRow& static_row, const RuntimeReport& report,
                            const std::optional<VisualAttestation>& attestation = std::nullopt,
                            const LogEvidence* logs = nullptr);

const char* to_string(CorrelationState value);

}  // namespace sns::runtime
