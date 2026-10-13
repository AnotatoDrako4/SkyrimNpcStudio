#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include "worker_client.hpp"

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
  Q_PROPERTY(QVariantMap npc READ npc NOTIFY changed)
  Q_PROPERTY(QVariantMap exportOptions READ export_options NOTIFY changed)
  Q_PROPERTY(QVariantMap validation READ validation NOTIFY changed)
  Q_PROPERTY(QVariantList recordChoices READ record_choices NOTIFY recordsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool catalogReady READ catalog_ready NOTIFY changed)
  Q_PROPERTY(QVariantMap workspace READ workspace NOTIFY changed)
  Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
  Q_PROPERTY(bool canExportTechnical READ can_export_technical NOTIFY changed)

 public:
  using EnvironmentProvider = std::function<creation::ForgeEnvironment()>;
  explicit CharacterForgeViewModel(EnvironmentProvider environment, WorkerClient* worker, QObject* parent = nullptr);

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
  QVariantMap npc() const;
  QVariantMap export_options() const;
  QVariantMap validation() const { return validation_; }
  QVariantList record_choices() const { return record_choices_; }
  bool busy() const { return busy_; }
  bool catalog_ready() const { return catalog_ready_; }
  QVariantMap workspace() const;
  QVariantMap capabilities() const;
  bool can_export_technical() const { return catalog_ready_ && !busy_ && validation_.value("technicallyExportable").toBool(); }

  Q_INVOKABLE bool newProject(const QString& name);
  Q_INVOKABLE bool openProject(const QString& folder_or_url);
  Q_INVOKABLE bool saveProject(const QString& folder_or_url);
  Q_INVOKABLE void setProjectName(const QString& value);
  Q_INVOKABLE void setCharacterName(const QString& value);
  Q_INVOKABLE void setEditorId(const QString& value);
  Q_INVOKABLE void setDescription(const QString& value);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void setNpcValue(const QString& field, const QVariant& value);
  Q_INVOKABLE void setExportOption(const QString& field, const QVariant& value);
  Q_INVOKABLE void searchRecords(const QString& kind, const QString& text);
  Q_INVOKABLE void validateNpc();
  Q_INVOKABLE void exportTechnical(const QString& folder_or_url);
  Q_INVOKABLE void cancelOperation();
  Q_INVOKABLE void setWorkspaceValue(const QString& field, const QVariant& value);
  Q_INVOKABLE void selectModelFile(const QString& url);
  Q_INVOKABLE void selectBlenderExecutable(const QString& url);
  Q_INVOKABLE void detectBlender();
  Q_INVOKABLE void selectReferenceImage(const QString& role, const QString& url);
  void invalidate_validation();
  void set_catalog_ready(bool ready);

 signals:
  void changed();
  void recordsChanged();

 private:
  template <class F> void edit(F&& change, bool affects_validation = true);
  EnvironmentProvider environment_;
  creation::ForgeDraft draft_;
  bool has_project_ = false;
  QString project_path_;
  QString last_error_;
  WorkerClient* worker_;
  QVariantMap validation_;
  QVariantList record_choices_;
  bool busy_ = false;
  bool catalog_ready_ = false;
  int operation_serial_ = 0, search_serial_ = 0;
  WorkerClient::CancelToken operation_cancel_;
};

}  // namespace sns::app
