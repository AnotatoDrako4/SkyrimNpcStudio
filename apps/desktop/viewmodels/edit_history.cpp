#include "edit_history.hpp"

namespace sns::app {

void EditHistory::push(std::unique_ptr<projects::Command> command) {
  stack_.push(std::move(command));
  emit changed();
}

void EditHistory::clear() {
  stack_.clear();
  emit changed();
}

void EditHistory::mark_clean() {
  stack_.mark_clean();
  emit changed();
}

QStringList EditHistory::entries() const {
  QStringList out;
  for (const auto& h : stack_.history()) out << QString::fromStdString(h);
  return out;
}

void EditHistory::undo() {
  if (stack_.undo()) emit changed();
}

void EditHistory::redo() {
  if (stack_.redo()) emit changed();
}

}  // namespace sns::app
