#include "sns/appearance/scan_completeness.hpp"

#include <algorithm>
#include <sstream>

namespace sns::appearance {

bool ScanCompletenessReport::is_mathematically_complete() const {
  const size_t accounted = total_inspected + total_out_of_scope + total_dynamic_unverifiable + total_uninspected;
  return accounted == total_indexed &&
         (total_clean + total_confirmed_defects + total_suspected_defects == total_inspected);
}

std::string ScanCompletenessReport::format_completeness_summary() const {
  std::ostringstream ss;
  ss << "Total Indexados: " << total_indexed << "\n"
     << "  - Total Realmente Inspeccionados: " << total_inspected << "\n"
     << "      * Limpios sin errores: " << total_clean << "\n"
     << "      * Problemas confirmados: " << total_confirmed_defects << "\n"
     << "      * Problemas sospechosos: " << total_suspected_defects << "\n"
     << "  - Dinámicos no verificables: " << total_dynamic_unverifiable << "\n"
     << "  - Fuera de alcance: " << total_out_of_scope << "\n";
  for (const auto& [reason, count] : out_of_scope_reasons) {
    ss << "      * " << reason << ": " << count << "\n";
  }
  if (total_uninspected > 0) {
    ss << "  - No inspeccionados (PENDIENTES): " << total_uninspected << "\n";
  }
  return ss.str();
}

void ScanCompletenessGate::register_indexed(const std::string& form_key, const std::string& editor_id) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const NpcAccountingEntry& e) {
    return e.form_key == form_key;
  });
  if (it == entries_.end()) {
    NpcAccountingEntry e;
    e.form_key = form_key;
    e.editor_id = editor_id;
    e.category = NpcScopeCategory::Uninspected;
    e.reason = "Indexado pero no auditado";
    entries_.push_back(std::move(e));
  }
}

void ScanCompletenessGate::mark_inspected_clean(const std::string& form_key) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const NpcAccountingEntry& e) {
    return e.form_key == form_key;
  });
  if (it != entries_.end()) {
    it->category = NpcScopeCategory::InspectedClean;
    it->reason.clear();
  }
}

void ScanCompletenessGate::mark_inspected_defective(const std::string& form_key, bool confirmed) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const NpcAccountingEntry& e) {
    return e.form_key == form_key;
  });
  if (it != entries_.end()) {
    it->category = confirmed ? NpcScopeCategory::InspectedDefective : NpcScopeCategory::InspectedSuspected;
    it->reason = confirmed ? "Defecto confirmado" : "Defecto sospechoso";
  }
}

void ScanCompletenessGate::mark_dynamic_unverifiable(const std::string& form_key, const std::string& reason) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const NpcAccountingEntry& e) {
    return e.form_key == form_key;
  });
  if (it != entries_.end()) {
    it->category = NpcScopeCategory::DynamicUnverifiable;
    it->reason = reason;
  }
}

void ScanCompletenessGate::mark_out_of_scope(const std::string& form_key, NpcScopeCategory category, const std::string& reason) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const NpcAccountingEntry& e) {
    return e.form_key == form_key;
  });
  if (it != entries_.end()) {
    it->category = category;
    it->reason = reason;
  }
}

ScanCompletenessReport ScanCompletenessGate::build_report() const {
  ScanCompletenessReport r;
  r.total_indexed = entries_.size();

  for (const auto& e : entries_) {
    switch (e.category) {
      case NpcScopeCategory::InspectedClean:
        r.total_inspected++;
        r.total_clean++;
        break;
      case NpcScopeCategory::InspectedDefective:
        r.total_inspected++;
        r.total_confirmed_defects++;
        break;
      case NpcScopeCategory::InspectedSuspected:
        r.total_inspected++;
        r.total_suspected_defects++;
        break;
      case NpcScopeCategory::DynamicUnverifiable:
        r.total_dynamic_unverifiable++;
        break;
      case NpcScopeCategory::OutOfScopeTraitsTemplate:
      case NpcScopeCategory::OutOfScopeNonHumanoidRace:
      case NpcScopeCategory::OutOfScopePlayer:
      case NpcScopeCategory::OutOfScopeChargenPreset:
      case NpcScopeCategory::OutOfScopeUnplaced:
        r.total_out_of_scope++;
        r.out_of_scope_reasons[e.reason.empty() ? "Sin razón especificada" : e.reason]++;
        break;
      case NpcScopeCategory::Uninspected:
        r.total_uninspected++;
        break;
    }
  }

  return r;
}

}  // namespace sns::appearance
