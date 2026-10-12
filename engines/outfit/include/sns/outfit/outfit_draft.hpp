#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"

namespace sns::outfit {

using Piece = projects::OutfitPieceRef;

struct SlotConflict {
  int slot = 0;  // 30..61
  std::string first;
  std::string second;
};

// The outfit being edited for one NPC. Mutated only through the commands below, so every change
// is undoable. Slot checks here give instant feedback; the worker's outfit.validate remains the
// authority (races, addons, templates) before export.
class OutfitDraft {
 public:
  [[nodiscard]] const std::vector<Piece>& pieces() const noexcept { return pieces_; }
  [[nodiscard]] std::uint32_t combined_mask() const noexcept;
  [[nodiscard]] std::vector<SlotConflict> conflicts() const;
  [[nodiscard]] bool contains(const std::string& form_key) const;

  // Low-level mutators used by commands (exposed for them and for loading a saved project).
  void insert(std::size_t index, Piece piece);
  Piece erase(std::size_t index);
  [[nodiscard]] std::ptrdiff_t index_of(const std::string& form_key) const;
  void assign(std::vector<Piece> pieces) { pieces_ = std::move(pieces); }

 private:
  std::vector<Piece> pieces_;
};

// Adds a piece. With `replace_conflicting`, pieces sharing a slot are removed first (what equipping
// does in game); otherwise the conflict is kept and reported.
std::unique_ptr<projects::Command> make_add_piece(OutfitDraft& draft, Piece piece, bool replace_conflicting);
std::unique_ptr<projects::Command> make_remove_piece(OutfitDraft& draft, const std::string& form_key);
std::unique_ptr<projects::Command> make_set_pieces(OutfitDraft& draft, std::vector<Piece> pieces, std::string description);

std::vector<int> slots_of(std::uint32_t mask);

}  // namespace sns::outfit
