#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "sns/creation/forge_project.hpp"

namespace sns::app {

// Creation-only state. Neither an NPC catalog selection nor an AppearanceDraft is required.
class CharacterForgeViewModel final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(bool hasProject READ has_project NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
  Q_PROPERTY(bool canUndo READ can_undo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ can_redo NOTIFY changed)
  Q_PROPERTY(QString projectName READ project_name NOTIFY changed)
  Q_PROPERTY(QString characterName READ character_name NOTIFY changed)
  Q_PROPERTY(QString editorId READ editor_id NOTIFY changed)
  Q_PROPERTY(QString description READ description NOTIFY changed)
  Q_PROPERTY(QString projectPath READ project_path NOTIFY changed)
  Q_PROPERTY(QString lastError READ last_error NOTIFY changed)

 public:
  using EnvironmentProvider = std::function<creation::ForgeEnvironment()>;
  explicit CharacterForgeViewModel(EnvironmentProvider environment, QObject* parent = nullptr);

  [[nodiscard]] bool has_project() const { return has_project_; }
  [[nodiscard]] bool dirty() const { return has_project_ && draft_.dirty(); }
  [[nodiscard]] bool can_undo() const { return has_project_ && draft_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return has_project_ && draft_.can_redo(); }
  [[nodiscard]] QString project_name() const;
  [[nodiscard]] QString character_name() const;
  [[nodiscard]] QString editor_id() const;
  [[nodiscard]] QString description() const;
  [[nodiscard]] QString project_path() const { return project_path_; }
  [[nodiscard]] QString last_error() const { return last_error_; }

  Q_INVOKABLE bool newProject(const QString& name);
  Q_INVOKABLE bool openProject(const QString& folder_or_url);
  Q_INVOKABLE bool saveProject(const QString& folder_or_url);
  Q_INVOKABLE void setProjectName(const QString& value);
  Q_INVOKABLE void setCharacterName(const QString& value);
  Q_INVOKABLE void setEditorId(const QString& value);
  Q_INVOKABLE void setDescription(const QString& value);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

 signals:
  void changed();

 private:
  template <class F> void edit(F&& change);
  EnvironmentProvider environment_;
  creation::ForgeDraft draft_;
  bool has_project_ = false;
  QString project_path_;
  QString last_error_;
};

}  // namespace sns::app
