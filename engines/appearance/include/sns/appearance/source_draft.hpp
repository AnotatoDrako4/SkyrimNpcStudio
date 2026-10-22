#pragma once

#include <map>
#include <string>
#include <string_view>

#include "sns/appearance/source_matrix.hpp"

// Non-destructive draft of the appearance source the user wants to look at / prefers for an NPC (the A/B view
// of the Appearance Source Matrix). It is a note about a view: it never changes load order, projects, plugins
// or FaceGen, and nothing exports from it. A draft is bound to the profile digest and to the digest of the
// matrix inputs it was made on; any change of either (another profile, a mod moved in MO2, a FaceGen replaced)
// makes it stale, and a stale draft is shown as such, never applied. Pure: the studio supplies digests and
// dates, reads and writes the file.
namespace sns::appearance {

struct SourceDraftEntry {
  std::string npc_form_key;
  std::string candidate_a;  // candidate ids of the matrix
  std::string candidate_b;
  std::string chosen;       // preferred candidate, or empty
  std::string profile_digest;
  std::string input_digest;  // SHA-256 of SourceMatrix::identity when the entry was saved
  std::string saved_at;      // ISO-8601, supplied by the caller
  std::string notes;
};

struct SourceDraftBook {
  static constexpr int kSchema = 1;
  std::string profile_key;
  std::map<std::string, SourceDraftEntry> entries;  // by NPC FormKey
};

enum class DraftState {
  Empty,             // no entry for this NPC
  Fresh,             // made on this profile and these inputs, every candidate still exists
  StaleProfile,      // the MO2 profile or its load order changed
  StaleInputs,       // the same profile, but the NPC's chain or FaceGen files changed
  UnknownCandidate,  // a candidate of the draft is no longer offered by the matrix
};
[[nodiscard]] std::string_view to_string(DraftState state);

// The state of the NPC's entry against the matrix built now (`current_input_digest` = SHA-256 of its identity).
[[nodiscard]] DraftState evaluate_draft(const SourceDraftBook& book, const SourceMatrix& matrix, const std::string& current_input_digest);

// Sets (or replaces) the NPC's entry. Refuses (returns false, `error` in Spanish) an unknown candidate id.
bool set_draft(SourceDraftBook& book, const SourceMatrix& matrix, const std::string& current_input_digest, std::string candidate_a,
               std::string candidate_b, std::string chosen, std::string saved_at, std::string notes, std::string* error);
void clear_draft(SourceDraftBook& book, const std::string& npc_form_key);

// Deterministic (keys sorted, 2-space indent, trailing newline): the same book is always the same bytes.
[[nodiscard]] std::string source_draft_to_json(const SourceDraftBook& book);
// Throws std::runtime_error for malformed JSON, an unknown schema or a missing/ill-typed field.
[[nodiscard]] SourceDraftBook source_draft_from_json(const std::string& text);

}  // namespace sns::appearance
