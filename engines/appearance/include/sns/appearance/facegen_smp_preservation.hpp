#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sns/appearance/npc_audit.hpp"

namespace sns::appearance {

struct FacegenSmpPreservationIssue {
  std::string code;         // e.g. "HAIR_SMP_003", "HAIR_SMP_004", "HAIR_SMP_005"
  std::string severity;     // "alta", "media"
  std::string message;
  std::string shape_name;
  std::string cause;
  std::string suggestion;
};

struct FacegenSmpPreservationReport {
  bool intact = true;
  std::vector<FacegenSmpPreservationIssue> issues;
  std::string summary;
};

// Analyzes the SMP hair shapes in an NPC's FaceGen compared against source headpart NIFs
FacegenSmpPreservationReport audit_facegen_smp_preservation(const std::vector<AuditSmpFaceGenHair>& smp_shapes);

}  // namespace sns::appearance
