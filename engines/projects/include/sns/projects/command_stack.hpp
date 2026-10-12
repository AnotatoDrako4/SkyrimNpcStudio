#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace sns::projects {

// Every user modification goes through a Command so it can be undone, redone and recorded.
class Command {
 public:
  virtual ~Command() = default;
  virtual void apply() = 0;
  virtual void revert() = 0;
  [[nodiscard]] virtual std::string description() const = 0;
  // Called with a command that was just applied right after this one. Returning true means this
  // command now also covers `next` (its revert restores the state before both) and `next` is
  // dropped. Default: never merge.
  virtual bool absorb(const Command& next) {
    (void)next;
    return false;
  }
};

class CommandStack {
 public:
  explicit CommandStack(std::size_t limit = 500) : limit_(limit) {}

  // Applies the command and records it; discards the redo branch.
  void push(std::unique_ptr<Command> command);
  bool undo();
  bool redo();
  void clear();

  [[nodiscard]] bool can_undo() const noexcept { return index_ > 0; }
  [[nodiscard]] bool can_redo() const noexcept { return index_ < commands_.size(); }
  [[nodiscard]] std::string undo_text() const;
  [[nodiscard]] std::string redo_text() const;

  // Dirty tracking against the last save.
  void mark_clean() noexcept { clean_index_ = static_cast<std::ptrdiff_t>(index_); }
  [[nodiscard]] bool is_clean() const noexcept { return clean_index_ == static_cast<std::ptrdiff_t>(index_); }

  [[nodiscard]] std::vector<std::string> history() const;

 private:
  std::vector<std::unique_ptr<Command>> commands_;
  std::size_t index_ = 0;  // commands_[0, index_) are applied
  std::ptrdiff_t clean_index_ = 0;
  std::size_t limit_;
};

}  // namespace sns::projects
