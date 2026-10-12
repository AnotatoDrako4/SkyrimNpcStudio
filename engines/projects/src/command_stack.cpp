#include "sns/projects/command_stack.hpp"

namespace sns::projects {

void CommandStack::push(std::unique_ptr<Command> command) {
  command->apply();  // if apply throws, the stack is unchanged
  commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index_), commands_.end());
  if (clean_index_ > static_cast<std::ptrdiff_t>(index_)) clean_index_ = -1;  // saved state is no longer reachable
  commands_.push_back(std::move(command));
  ++index_;
  if (commands_.size() > limit_) {
    commands_.erase(commands_.begin());
    --index_;
    --clean_index_;
  }
}

bool CommandStack::undo() {
  if (!can_undo()) return false;
  commands_[index_ - 1]->revert();
  --index_;
  return true;
}

bool CommandStack::redo() {
  if (!can_redo()) return false;
  commands_[index_]->apply();
  ++index_;
  return true;
}

void CommandStack::clear() {
  commands_.clear();
  index_ = 0;
  clean_index_ = 0;
}

std::string CommandStack::undo_text() const { return can_undo() ? commands_[index_ - 1]->description() : std::string{}; }

std::string CommandStack::redo_text() const { return can_redo() ? commands_[index_]->description() : std::string{}; }

std::vector<std::string> CommandStack::history() const {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < index_; ++i) out.push_back(commands_[i]->description());
  return out;
}

}  // namespace sns::projects
