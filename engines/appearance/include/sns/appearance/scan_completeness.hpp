#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sns::appearance {

enum class NpcScopeCategory {
  InspectedClean,              // Examined and verified clean
  InspectedDefective,          // Examined and confirmed issues found
  InspectedSuspected,          // Examined with suspected issues
  DynamicUnverifiable,         // Dynamic distribution prevents static verification
  OutOfScopeTraitsTemplate,    // Inherits appearance from another NPC template
  OutOfScopeNonHumanoidRace,   // Creature / horse / dragon without humanoid FaceGen
  OutOfScopePlayer,            // Player reference (head comes from save)
  OutOfScopeChargenPreset,     // CharGen preset record
  OutOfScopeUnplaced,          // Filtered out or unplaced reference
  Uninspected                  // Queued or indexed but not yet examined (NEVER clean!)
};

struct NpcAccountingEntry {
  std::string form_key;
  std::string editor_id;
  NpcScopeCategory category = NpcScopeCategory::Uninspected;
  std::string reason;
};

struct ScanCompletenessReport {
  size_t total_indexed = 0;
  size_t total_inspected = 0;
  size_t total_out_of_scope = 0;
  size_t total_dynamic_unverifiable = 0;
  size_t total_confirmed_defects = 0;
  size_t total_suspected_defects = 0;
  size_t total_clean = 0;
  size_t total_uninspected = 0;

  // Breakdown of out-of-scope reasons
  std::map<std::string, size_t> out_of_scope_reasons;

  // Invariant verification: total_indexed must equal sum of all categories
  [[nodiscard]] bool is_mathematically_complete() const;

  // Explains excluded NPCs in a formatted text summary
  [[nodiscard]] std::string format_completeness_summary() const;
};

class ScanCompletenessGate {
 public:
  void register_indexed(const std::string& form_key, const std::string& editor_id);
  void mark_inspected_clean(const std::string& form_key);
  void mark_inspected_defective(const std::string& form_key, bool confirmed);
  void mark_dynamic_unverifiable(const std::string& form_key, const std::string& reason);
  void mark_out_of_scope(const std::string& form_key, NpcScopeCategory category, const std::string& reason);

  [[nodiscard]] ScanCompletenessReport build_report() const;
  [[nodiscard]] const std::vector<NpcAccountingEntry>& entries() const { return entries_; }

 private:
  std::vector<NpcAccountingEntry> entries_;
};

}  // namespace sns::appearance
