#pragma once

#include <map>
#include <string>
#include <vector>

namespace sns::appearance {

struct NpcHairSnapshot {
  std::string form_key;
  std::string editor_id;
  std::string assigned_hair_model;
  std::string winner_mod;
  std::string spid_rule_signature;
  bool smp_enabled = false;
};

struct ProfileHairBaseline {
  std::string profile_name;
  std::string timestamp_utc;
  std::map<std::string, NpcHairSnapshot> npcs;
};

struct HairRegressionIssue {
  std::string form_key;
  std::string editor_id;
  std::string change_kind;  // "PROVIDER_CHANGED", "HAIR_OVERRIDDEN", "SPID_RULE_CHANGED", "SMP_LOST"
  std::string baseline_val;
  std::string current_val;
  std::string responsible_mod;
  std::string description;
};

struct HairRegressionReport {
  bool has_regressions = false;
  std::vector<std::string> invalidated_npc_keys;
  std::vector<HairRegressionIssue> regressions;
  std::string summary;
};

// Compares current NPC states against baseline snapshot to isolate regressions
HairRegressionReport detect_hair_regressions(
    const ProfileHairBaseline& baseline,
    const std::map<std::string, NpcHairSnapshot>& current_npcs);

}  // namespace sns::appearance
