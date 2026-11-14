#pragma once

#include <string>
#include <vector>

#include "sns/appearance/hair_graph.hpp"

namespace sns::appearance {

struct UbeSmpValidationIssue {
  std::string code;         // e.g. "HAIR_SMP_009", "HAIR_SMP_006", "HAIR_SMP_011"
  std::string severity;     // "alta", "media", "baja"
  std::string status;       // kStatusConfirmed / kStatusSuspected
  std::string message;
  std::string cause;
  std::string action;
  std::string resource;
  std::string responsible;
};

struct UbeSmpReport {
  bool compatible = true;
  bool is_ube = false;
  bool hdf_installed = false;
  bool hdf_targeted = false;
  bool ube_patch_present = false;
  bool hair_suppression_fix_present = false;
  bool collision_matches_body = true;
  std::vector<UbeSmpValidationIssue> issues;
};

// Context passed to validator describing global profile state
struct UbeSmpEnvironment {
  bool hdf_installed = false;
  bool ube_installed = false;
  bool ube_hdf_patch_installed = false;       // Effective files of the UBE patch
  bool hair_suppression_fix_installed = false; // SKSE/Plugins/HairSuppressionFix.dll
  std::string active_body_collision_family;   // e.g. "ube", "cbbe3ba", "vanilla"
};

UbeSmpReport validate_ube_smp_compatibility(const HairAssignmentGraph& graph, const UbeSmpEnvironment& env);

}  // namespace sns::appearance
