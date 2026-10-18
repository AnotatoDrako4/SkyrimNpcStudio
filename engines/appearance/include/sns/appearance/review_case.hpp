#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "sns/appearance/control_map.hpp"

// Individual human review of ambiguous face resolutions (decision D1). A resolution that would discard a
// mod's own face edits (FaceResolution::discards_own_edits) is a choice, not a repair: it may only be
// exported after a person approved THAT proposal for THAT NPC (identified by FormKey, never by name or
// list position). The approval is bound to the identity of the inputs it was made on; any change of the
// override chain, the effective FaceGen/FaceTint or the proposal makes it stale. Pure, no I/O: the studio
// hashes the identity, persists the book and asks the gate before exporting.
namespace sns::appearance {

enum class ReviewDecision { Pending, Approve, Reject, Defer };

std::string_view to_string(ReviewDecision decision);
std::optional<ReviewDecision> review_decision_from(std::string_view text);

// True when the resolution needs an individual review (it discards edits a mod introduced itself).
bool requires_review(const FaceResolution& resolution);

// Canonical text of everything an approval depends on: the NPC's FormKey, every version of its override
// chain (plugin, mod, changed fields, inferred copies, copies of the plugin file in MO2), the effective
// FaceGen and FaceTint (path, found, kind, winning mod, loose copies) and the proposal (source, winner,
// fields, discarded own edits). Deterministic; the studio stores its SHA-256 as the input digest.
std::string review_identity(const std::string& npc_form_key, const ControlInputs& in, const FaceResolution& resolution);

struct ReviewRecord {
  std::string npc_form_key;
  ReviewDecision decision = ReviewDecision::Pending;
  std::string input_digest;  // digest of review_identity when the decision was taken
  std::string decided_at;    // ISO-8601, supplied by the caller
  std::string notes;
};

struct ReviewBook {
  static constexpr int kSchema = 1;
  std::string profile_key;
  std::map<std::string, ReviewRecord> records;  // by NPC FormKey
};

std::string review_book_to_json(const ReviewBook& book);
// Throws std::runtime_error for malformed JSON, an unknown schema or an unknown decision.
ReviewBook review_book_from_json(const std::string& text);

struct ReviewGate {
  bool allowed = false;
  std::string state;   // "not-required", "pending", "approved", "rejected", "deferred" or "stale"
  std::string reason;  // Spanish, empty when allowed
};

// Whether the resolution of `npc_form_key` may be exported now, given the digest of its current inputs.
ReviewGate review_gate(const FaceResolution& resolution, const std::string& npc_form_key, const std::string& current_digest,
                       const ReviewBook& book);

}  // namespace sns::appearance
