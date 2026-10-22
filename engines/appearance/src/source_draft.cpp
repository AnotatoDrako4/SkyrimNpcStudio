#include "sns/appearance/source_draft.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace sns::appearance {
namespace {

std::string required_string(const nlohmann::json& j, const char* key) {
  const auto it = j.find(key);
  if (it == j.end() || !it->is_string()) throw std::runtime_error(std::string("source draft: missing or ill-typed field '") + key + "'");
  return it->get<std::string>();
}

bool known(const SourceMatrix& matrix, const std::string& id) { return id.empty() || matrix.find(id) != nullptr; }

}  // namespace

std::string_view to_string(DraftState state) {
  switch (state) {
    case DraftState::Empty: return "empty";
    case DraftState::Fresh: return "fresh";
    case DraftState::StaleProfile: return "stale-profile";
    case DraftState::StaleInputs: return "stale-inputs";
    case DraftState::UnknownCandidate: return "unknown-candidate";
  }
  return "empty";
}

DraftState evaluate_draft(const SourceDraftBook& book, const SourceMatrix& matrix, const std::string& current_input_digest) {
  const auto it = book.entries.find(matrix.npc_form_key);
  if (it == book.entries.end()) return DraftState::Empty;
  const auto& e = it->second;
  if (e.profile_digest != matrix.profile_digest) return DraftState::StaleProfile;
  if (e.input_digest != current_input_digest) return DraftState::StaleInputs;
  if (!known(matrix, e.candidate_a) || !known(matrix, e.candidate_b) || !known(matrix, e.chosen)) return DraftState::UnknownCandidate;
  return DraftState::Fresh;
}

bool set_draft(SourceDraftBook& book, const SourceMatrix& matrix, const std::string& current_input_digest, std::string candidate_a,
               std::string candidate_b, std::string chosen, std::string saved_at, std::string notes, std::string* error) {
  for (const auto* id : {&candidate_a, &candidate_b, &chosen}) {
    if (!known(matrix, *id)) {
      if (error) *error = "La fuente «" + *id + "» ya no la ofrece el perfil.";
      return false;
    }
  }
  SourceDraftEntry e;
  e.npc_form_key = matrix.npc_form_key;
  e.candidate_a = std::move(candidate_a);
  e.candidate_b = std::move(candidate_b);
  e.chosen = std::move(chosen);
  e.profile_digest = matrix.profile_digest;
  e.input_digest = current_input_digest;
  e.saved_at = std::move(saved_at);
  e.notes = std::move(notes);
  book.entries[matrix.npc_form_key] = std::move(e);
  return true;
}

void clear_draft(SourceDraftBook& book, const std::string& npc_form_key) { book.entries.erase(npc_form_key); }

std::string source_draft_to_json(const SourceDraftBook& book) {
  nlohmann::json entries = nlohmann::json::object();
  for (const auto& [key, e] : book.entries) {
    entries[key] = {{"npcFormKey", e.npc_form_key}, {"candidateA", e.candidate_a}, {"candidateB", e.candidate_b}, {"chosen", e.chosen},
                    {"profileDigest", e.profile_digest}, {"inputDigest", e.input_digest}, {"savedAt", e.saved_at}, {"notes", e.notes}};
  }
  const nlohmann::json root = {{"schema", SourceDraftBook::kSchema}, {"profileKey", book.profile_key}, {"entries", entries}};
  return root.dump(2) + "\n";
}

SourceDraftBook source_draft_from_json(const std::string& text) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("source draft: malformed JSON: ") + e.what());
  }
  if (!root.is_object() || root.value("schema", 0) != SourceDraftBook::kSchema) throw std::runtime_error("source draft: unknown schema");
  SourceDraftBook book;
  book.profile_key = required_string(root, "profileKey");
  const auto entries = root.find("entries");
  if (entries == root.end() || !entries->is_object()) throw std::runtime_error("source draft: missing 'entries'");
  for (const auto& [key, j] : entries->items()) {
    if (!j.is_object()) throw std::runtime_error("source draft: entry is not an object");
    SourceDraftEntry e;
    e.npc_form_key = required_string(j, "npcFormKey");
    e.candidate_a = required_string(j, "candidateA");
    e.candidate_b = required_string(j, "candidateB");
    e.chosen = required_string(j, "chosen");
    e.profile_digest = required_string(j, "profileDigest");
    e.input_digest = required_string(j, "inputDigest");
    e.saved_at = required_string(j, "savedAt");
    e.notes = required_string(j, "notes");
    book.entries[key] = std::move(e);
  }
  return book;
}

}  // namespace sns::appearance
