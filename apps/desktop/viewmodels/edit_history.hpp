#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "sns/projects/command_stack.hpp"

namespace sns::app {

// The single undo/redo history of the NPC being edited. Face, body and outfit commands all go
// through it, so "undo" always reverts the last change whatever panel made it.
class EditHistory : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(bool canUndo READ can_undo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ can_redo NOTIFY changed)
  Q_PROPERTY(QString undoText READ undo_text NOTIFY changed)
  Q_PROPERTY(QString redoText READ redo_text NOTIFY changed)
  Q_PROPERTY(QStringList entries READ entries NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)

 public:
  explicit EditHistory(QObject* parent = nullptr) : QObject(parent) {}

  // Applies and records the command. Exceptions from apply() leave the history unchanged.
  void push(std::unique_ptr<projects::Command> command);
  void clear();
  void mark_clean();

  [[nodiscard]] bool can_undo() const { return stack_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return stack_.can_redo(); }
  [[nodiscard]] QString undo_text() const { return QString::fromStdString(stack_.undo_text()); }
  [[nodiscard]] QString redo_text() const { return QString::fromStdString(stack_.redo_text()); }
  [[nodiscard]] QStringList entries() const;
  [[nodiscard]] bool dirty() const { return !stack_.is_clean(); }

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

 signals:
  void changed();

 private:
  projects::CommandStack stack_;
};

}  // namespace sns::app
