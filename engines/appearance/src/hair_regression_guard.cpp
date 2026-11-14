#include "sns/appearance/hair_regression_guard.hpp"

#include <sstream>

namespace sns::appearance {

HairRegressionReport detect_hair_regressions(
    const ProfileHairBaseline& baseline,
    const std::map<std::string, NpcHairSnapshot>& current_npcs) {
  HairRegressionReport report;
  report.has_regressions = false;

  for (const auto& [form_key, current] : current_npcs) {
    const auto it = baseline.npcs.find(form_key);
    if (it == baseline.npcs.end()) {
      // New NPC, not a regression of existing baseline
      continue;
    }
    const auto& base = it->second;

    bool npc_affected = false;

    // 1. Check if winning provider changed
    if (base.winner_mod != current.winner_mod) {
      npc_affected = true;
      HairRegressionIssue issue;
      issue.form_key = form_key;
      issue.editor_id = current.editor_id;
      issue.change_kind = "PROVIDER_CHANGED";
      issue.baseline_val = base.winner_mod;
      issue.current_val = current.winner_mod;
      issue.responsible_mod = current.winner_mod;
      issue.description = "El mod ganador que provee la apariencia cambió de «" + base.winner_mod + "» a «" + current.winner_mod + "».";
      report.regressions.push_back(std::move(issue));
    }

    // 2. Check if assigned hair mesh changed or was stripped
    if (base.assigned_hair_model != current.assigned_hair_model) {
      npc_affected = true;
      HairRegressionIssue issue;
      issue.form_key = form_key;
      issue.editor_id = current.editor_id;
      issue.change_kind = "HAIR_OVERRIDDEN";
      issue.baseline_val = base.assigned_hair_model;
      issue.current_val = current.assigned_hair_model;
      issue.responsible_mod = current.winner_mod;
      issue.description = "La malla de peinado asignada cambió de «" + base.assigned_hair_model + "» a «" + current.assigned_hair_model + "».";
      report.regressions.push_back(std::move(issue));
    }

    // 3. Check if SPID distribution rule signature changed
    if (!base.spid_rule_signature.empty() && base.spid_rule_signature != current.spid_rule_signature) {
      npc_affected = true;
      HairRegressionIssue issue;
      issue.form_key = form_key;
      issue.editor_id = current.editor_id;
      issue.change_kind = "SPID_RULE_CHANGED";
      issue.baseline_val = base.spid_rule_signature;
      issue.current_val = current.spid_rule_signature;
      issue.responsible_mod = "SPID Distribuidor";
      issue.description = "La regla de distribución dinámica SPID para este NPC ha cambiado.";
      report.regressions.push_back(std::move(issue));
    }

    // 4. Check if SMP capability was lost
    if (base.smp_enabled && !current.smp_enabled) {
      npc_affected = true;
      HairRegressionIssue issue;
      issue.form_key = form_key;
      issue.editor_id = current.editor_id;
      issue.change_kind = "SMP_LOST";
      issue.baseline_val = "SMP_ENABLED";
      issue.current_val = "SMP_DISABLED";
      issue.responsible_mod = current.winner_mod;
      issue.description = "El peinado perdió simulación de físicas SMP frente a la baseline previa.";
      report.regressions.push_back(std::move(issue));
    }

    if (npc_affected) {
      report.has_regressions = true;
      report.invalidated_npc_keys.push_back(form_key);
    }
  }

  std::ostringstream ss;
  ss << "Evaluación de regresión frente a baseline (" << baseline.profile_name << "): "
     << report.invalidated_npc_keys.size() << " NPC afectados invalidados selectivamente.";
  report.summary = ss.str();

  return report;
}

}  // namespace sns::appearance
