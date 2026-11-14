#pragma once

#include <string>
#include <vector>

#include "sns/appearance/npc_audit.hpp"

namespace sns::appearance {

struct HairRecoveryStep {
  int step_order = 1;
  std::string description;
  std::string action_type;  // "GENERATE_RESOLUTION_PATCH", "RESTORE_PREVIOUS_HEADPART", "TRANSFER_SMP_METADATA", "INSTALL_MO2_PATCH"
  std::string target_file;
  std::string previous_state;
  std::string proposed_state;
  bool is_reversible = true;
};

struct HairRecoveryProposal {
  std::string form_key;
  std::string editor_id;
  std::string root_cause;
  std::string minimal_fix_summary;
  std::string status = kStatusRepairedInFiles;
  std::string verification_state = kVerifyInGame;
  bool can_restore_previous = false;
  std::string previous_hair_form_key;
  std::vector<HairRecoveryStep> steps;
  std::vector<std::string> validated_dependencies;
  bool ready_for_export = false;
};

// Formulates a safe, minimal, reversible repair plan for an NPC with hair defects
HairRecoveryProposal plan_hair_safe_recovery(
    const NpcAudit& audit,
    const std::string& previous_hair_form_key = "");

}  // namespace sns::appearance
