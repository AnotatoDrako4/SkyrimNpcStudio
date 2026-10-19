#include "sns/appearance/review_case.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace sns::appearance {
namespace {

std::string joined(std::vector<std::string> items, bool sort) {
  if (sort) std::sort(items.begin(), items.end());
  std::string out;
  for (const auto& i : items) out += (out.empty() ? "" : ",") + i;
  return out;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
  return s;
}

std::string asset_line(const char* key, const AssetSource& a) {
  // Loose copies keep their MO2 priority order: which copy wins is part of the identity.
  return std::string(key) + "=" + lower(a.path) + "|" + (a.found ? "found" : "missing") + "|" + a.kind + "|" + a.winner_mod + "|loose:" +
         joined(a.loose_mods, false);
}

std::string quoted(const std::string& s) { return "«" + s + "»"; }

}  // namespace

std::string_view to_string(ReviewDecision decision) {
  switch (decision) {
    case ReviewDecision::Approve: return "approve";
    case ReviewDecision::Reject: return "reject";
    case ReviewDecision::Defer: return "defer";
    case ReviewDecision::Pending: break;
  }
  return "pending";
}

std::optional<ReviewDecision> review_decision_from(std::string_view text) {
  if (text == "pending") return ReviewDecision::Pending;
  if (text == "approve") return ReviewDecision::Approve;
  if (text == "reject") return ReviewDecision::Reject;
  if (text == "defer") return ReviewDecision::Defer;
  return std::nullopt;
}

bool requires_review(const FaceResolution& resolution) { return !resolution.discards_own_edits.empty(); }

std::string review_identity(const std::string& npc_form_key, const ControlInputs& in, const FaceResolution& resolution) {
  std::string out = "review-identity-v1\nnpc=" + npc_form_key + "\n";
  for (const auto& link : in.chain) {
    std::vector<std::string> copies;
    for (const auto& [field, from] : link.copied_from) copies.push_back(field + "=" + from);
    out += "link=" + link.plugin + "|" + link.mod + "|" + (link.is_origin ? "origin" : "-") + "|" + (link.is_winner ? "winner" : "-") +
           "|changed:" + joined(link.changed_fields, true) + "|copied:" + joined(copies, true) + "|files:" + joined(link.plugin_mods, false) + "\n";
  }
  out += asset_line("facegen", in.facegen) + "\n";
  out += asset_line("facetint", in.facetint) + "\n";
  out += "source=" + resolution.source_plugin + "|" + resolution.source_mod + "\n";
  out += "winner=" + resolution.winner_plugin + "|" + resolution.winner_mod + "\n";
  out += "fields=" + joined(resolution.fields, false) + "\n";
  out += "discards=" + joined(resolution.discards_own_edits, true) + "\n";
  return out;
}

std::string review_book_to_json(const ReviewBook& book) {
  nlohmann::json records = nlohmann::json::array();
  for (const auto& [key, r] : book.records) {
    records.push_back({{"npcFormKey", r.npc_form_key},
                       {"decision", std::string(to_string(r.decision))},
                       {"inputDigest", r.input_digest},
                       {"decidedAt", r.decided_at},
                       {"notes", r.notes}});
  }
  return nlohmann::json{{"schema", ReviewBook::kSchema}, {"profileKey", book.profile_key}, {"records", records}}.dump(2);
}

ReviewBook review_book_from_json(const std::string& text) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error(std::string("review book is not valid JSON: ") + e.what());
  }
  if (!j.is_object() || j.value("schema", 0) != ReviewBook::kSchema) {
    throw std::runtime_error("review book has an unknown schema (expected " + std::to_string(ReviewBook::kSchema) + ")");
  }
  ReviewBook book;
  book.profile_key = j.value("profileKey", std::string());
  for (const auto& item : j.value("records", nlohmann::json::array())) {
    ReviewRecord r;
    r.npc_form_key = item.value("npcFormKey", std::string());
    const auto decision = review_decision_from(item.value("decision", std::string()));
    if (r.npc_form_key.empty() || !decision) throw std::runtime_error("review book has a record without FormKey or with an unknown decision");
    r.decision = *decision;
    r.input_digest = item.value("inputDigest", std::string());
    r.decided_at = item.value("decidedAt", std::string());
    r.notes = item.value("notes", std::string());
    book.records[r.npc_form_key] = std::move(r);
  }
  return book;
}

ReviewGate review_gate(const FaceResolution& resolution, const std::string& npc_form_key, const std::string& current_digest,
                       const ReviewBook& book) {
  if (!requires_review(resolution)) return {true, "not-required", {}};
  std::string own;
  for (const auto& p : resolution.discards_own_edits) own += (own.empty() ? "" : ", ") + quoted(p);
  const auto it = book.records.find(npc_form_key);
  if (it == book.records.end() || it->second.decision == ReviewDecision::Pending) {
    return {false, "pending",
            "Revisión individual pendiente: este parche descartaría ediciones propias de " + own +
                ". Apruébalo, recházalo o aplázalo en la pestaña Origen antes de exportar."};
  }
  const auto& r = it->second;
  if (r.decision == ReviewDecision::Reject) return {false, "rejected", "Rechazado en la revisión individual (" + r.decided_at + "): no se exporta."};
  if (r.decision == ReviewDecision::Defer) return {false, "deferred", "Aplazado en la revisión individual (" + r.decided_at + "): no se exporta."};
  if (r.input_digest.empty() || r.input_digest != current_digest) {
    return {false, "stale",
            "La aprobación del " + r.decided_at +
                " ya no vale: cambió la cadena del registro, el FaceGen efectivo o la propuesta desde entonces. Revísalo de nuevo."};
  }
  return {true, "approved", {}};
}

}  // namespace sns::appearance
