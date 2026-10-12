#include "sns/outfit/outfit_draft.hpp"

#include <algorithm>

#include "sns/core/error.hpp"

namespace sns::outfit {

std::vector<int> slots_of(std::uint32_t mask) {
  std::vector<int> slots;
  for (int bit = 0; bit < 32; ++bit) {
    if (mask & (1u << bit)) slots.push_back(30 + bit);
  }
  return slots;
}

std::uint32_t OutfitDraft::combined_mask() const noexcept {
  std::uint32_t mask = 0;
  for (const auto& p : pieces_) mask |= p.slot_mask;
  return mask;
}

std::vector<SlotConflict> OutfitDraft::conflicts() const {
  std::vector<SlotConflict> out;
  for (std::size_t i = 0; i < pieces_.size(); ++i) {
    for (std::size_t j = i + 1; j < pieces_.size(); ++j) {
      for (int slot : slots_of(pieces_[i].slot_mask & pieces_[j].slot_mask)) {
        out.push_back({slot, pieces_[i].name, pieces_[j].name});
      }
    }
  }
  return out;
}

bool OutfitDraft::contains(const std::string& form_key) const { return index_of(form_key) >= 0; }

std::ptrdiff_t OutfitDraft::index_of(const std::string& form_key) const {
  const auto it = std::find_if(pieces_.begin(), pieces_.end(), [&](const Piece& p) { return p.form_key == form_key; });
  return it == pieces_.end() ? -1 : std::distance(pieces_.begin(), it);
}

void OutfitDraft::insert(std::size_t index, Piece piece) {
  pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(std::min(index, pieces_.size())), std::move(piece));
}

Piece OutfitDraft::erase(std::size_t index) {
  Piece removed = std::move(pieces_.at(index));
  pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(index));
  return removed;
}

namespace {

class SetPiecesCommand final : public projects::Command {
 public:
  SetPiecesCommand(OutfitDraft& draft, std::vector<Piece> after, std::string description)
      : draft_(draft), before_(draft.pieces()), after_(std::move(after)), description_(std::move(description)) {}
  void apply() override { draft_.assign(after_); }
  void revert() override { draft_.assign(before_); }
  [[nodiscard]] std::string description() const override { return description_; }

 private:
  OutfitDraft& draft_;
  std::vector<Piece> before_;
  std::vector<Piece> after_;
  std::string description_;
};

}  // namespace

std::unique_ptr<projects::Command> make_set_pieces(OutfitDraft& draft, std::vector<Piece> pieces, std::string description) {
  return std::make_unique<SetPiecesCommand>(draft, std::move(pieces), std::move(description));
}

std::unique_ptr<projects::Command> make_add_piece(OutfitDraft& draft, Piece piece, bool replace_conflicting) {
  if (draft.contains(piece.form_key)) throw Error(ErrorKind::InvalidArgument, "'" + piece.name + "' is already in the outfit");
  std::vector<Piece> after;
  std::vector<std::string> replaced;
  for (const auto& existing : draft.pieces()) {
    if (replace_conflicting && (existing.slot_mask & piece.slot_mask) != 0) {
      replaced.push_back(existing.name);
    } else {
      after.push_back(existing);
    }
  }
  std::string description = "Add " + piece.name;
  if (!replaced.empty()) {
    description += " (replaces ";
    for (std::size_t i = 0; i < replaced.size(); ++i) description += (i ? ", " : "") + replaced[i];
    description += ")";
  }
  after.push_back(std::move(piece));
  return make_set_pieces(draft, std::move(after), std::move(description));
}

std::unique_ptr<projects::Command> make_remove_piece(OutfitDraft& draft, const std::string& form_key) {
  const auto index = draft.index_of(form_key);
  if (index < 0) throw Error(ErrorKind::NotFound, "piece " + form_key + " is not in the outfit");
  auto after = draft.pieces();
  const auto name = after[static_cast<std::size_t>(index)].name;
  after.erase(after.begin() + index);
  return make_set_pieces(draft, std::move(after), "Remove " + name);
}

}  // namespace sns::outfit
