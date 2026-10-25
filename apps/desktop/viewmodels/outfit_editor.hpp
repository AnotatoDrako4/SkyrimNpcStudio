#pragma once

#include <QColor>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "console_model.hpp"
#include "edit_history.hpp"
#include "mo2_snapshot.hpp"
#include "sns/appearance/appearance_draft.hpp"
#include "sns/appearance/compatibility_guardian.hpp"
#include "sns/outfit/outfit_draft.hpp"
#include "sns/outfit/outfit_presets.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"
#include "worker_client.hpp"

namespace sns::app {

// Outfit editing for the selected NPC. The outfit lives in the studio's shared AppearanceDraft and
// every change is a command of the shared EditHistory (one undo/redo for face, body and outfit);
// the worker re-validates the draft automatically, and export goes through the worker.
class OutfitEditor : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(QString npcFormKey READ npc_form_key NOTIFY targetChanged)
  Q_PROPERTY(QString npcLabel READ npc_label NOTIFY targetChanged)
  Q_PROPERTY(QString suggestedPluginName READ suggested_plugin_name NOTIFY targetChanged)
  Q_PROPERTY(QVariantList pieces READ pieces NOTIFY changed)
  Q_PROPERTY(QVariantList inventoryItems READ inventory_items NOTIFY changed)
  Q_PROPERTY(bool inventoryModified READ inventory_modified NOTIFY changed)
  Q_PROPERTY(QVariantList localConflicts READ local_conflicts NOTIFY changed)
  Q_PROPERTY(bool canUndo READ can_undo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ can_redo NOTIFY changed)
  Q_PROPERTY(QString undoText READ undo_text NOTIFY changed)
  Q_PROPERTY(QString redoText READ redo_text NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
  Q_PROPERTY(QStringList history READ history NOTIFY changed)
  Q_PROPERTY(QVariantMap validation READ validation NOTIFY validationChanged)
  Q_PROPERTY(bool validating READ validating NOTIFY validationChanged)
  Q_PROPERTY(bool exportable READ exportable NOTIFY validationChanged)
  Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
  Q_PROPERTY(QString projectPath READ project_path NOTIFY projectChanged)
  Q_PROPERTY(QString defaultExportDirectory READ default_export_directory CONSTANT)
  Q_PROPERTY(QVariantList projectNpcs READ project_npcs NOTIFY projectChanged)
  Q_PROPERTY(int projectNpcCount READ project_npc_count NOTIFY projectChanged)
  Q_PROPERTY(QVariantList outfitPresets READ outfit_presets NOTIFY presetsChanged)
  Q_PROPERTY(QVariantMap compatibilityReport READ compatibility_report NOTIFY changed)

 public:
  using SnapshotProvider = std::function<SnapshotPtr()>;
  using FaceProvider = std::function<std::optional<projects::FaceConfig>(const std::string& form_key)>;
  using FaceLoadedHandler = std::function<void(const std::string& form_key, const std::optional<projects::FaceConfig>& face)>;

  OutfitEditor(WorkerClient* worker, ConsoleModel* console, SnapshotProvider snapshot, appearance::AppearanceDraft& draft,
               EditHistory* history, QObject* parent = nullptr);

  void set_face_provider(FaceProvider provider) { face_provider_ = std::move(provider); }
  void set_face_loaded_handler(FaceLoadedHandler handler) { face_loaded_handler_ = std::move(handler); }

  [[nodiscard]] QString npc_form_key() const { return qs(target_.form_key); }
  [[nodiscard]] QString npc_label() const;
  [[nodiscard]] QString suggested_plugin_name() const;
  [[nodiscard]] QVariantList pieces() const;
  [[nodiscard]] QVariantList inventory_items() const { return inventory_; }
  [[nodiscard]] bool inventory_modified() const { return inventory_ != initial_inventory_; }
  [[nodiscard]] QVariantList local_conflicts() const;
  [[nodiscard]] bool can_undo() const { return history_->can_undo(); }
  [[nodiscard]] bool can_redo() const { return history_->can_redo(); }
  [[nodiscard]] QString undo_text() const { return history_->undo_text(); }
  [[nodiscard]] QString redo_text() const { return history_->redo_text(); }
  [[nodiscard]] bool dirty() const { return history_->dirty(); }
  [[nodiscard]] const std::vector<outfit::Piece>& current_pieces() const { return draft_.outfit().pieces(); }
  [[nodiscard]] const projects::NpcTarget& target() const { return target_; }
  [[nodiscard]] const std::optional<projects::Project>& project() const { return project_; }
  [[nodiscard]] QStringList history() const;
  [[nodiscard]] QVariantMap validation() const { return validation_; }
  [[nodiscard]] bool validating() const { return validating_; }
  [[nodiscard]] bool exportable() const;
  [[nodiscard]] bool exporting() const { return exporting_; }
  [[nodiscard]] QString project_path() const { return project_path_; }
  [[nodiscard]] QString default_export_directory() const;
  [[nodiscard]] QVariantList project_npcs() const;
  [[nodiscard]] int project_npc_count() const;

  // Takes the NPC details (catalog.getNpc + studio fields) and starts from its current outfit.
  void setTarget(const QVariantMap& npc_details);

  Q_INVOKABLE void createNewOutfit(const QString& name = {});
  Q_INVOKABLE void addPiece(const QVariantMap& armor, bool replace_conflicting = true);
  Q_INVOKABLE void removePiece(const QString& form_key);
  Q_INVOKABLE void resetToCurrent();
  Q_INVOKABLE void clearPieces();
  Q_INVOKABLE void addInventoryItem(const QVariantMap& item, int count = 1);
  Q_INVOKABLE void setInventoryItemCount(const QString& form_key, int count);
  Q_INVOKABLE void removeInventoryItem(const QString& form_key);
  Q_INVOKABLE void resetInventory();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void validate();
  Q_INVOKABLE void exportPatch(const QString& plugin_name, const QString& output_dir_or_url, bool esl, bool zip,
                               const QString& description, bool also_set_sleeping = false,
                               bool export_all_project_npcs = true, bool standalone = false,
                               const QString& standalone_name = {}, const QString& author = {});
  Q_INVOKABLE void exportStandaloneOutfit(const QString& name, const QString& plugin_name, const QString& output_dir_or_url,
                                          bool esl, bool zip, const QString& description);
  Q_INVOKABLE void addCurrentToProject();
  Q_INVOKABLE void removeNpcFromProject(const QString& form_key);
  Q_INVOKABLE void loadNpcFromProject(const QString& form_key);
  [[nodiscard]] QVariantList outfit_presets() const;
  [[nodiscard]] QVariantMap compatibility_report() const;

  Q_INVOKABLE void applyPreset(const QString& preset_id);
  Q_INVOKABLE bool saveCurrentPreset(const QString& name);
  Q_INVOKABLE bool duplicatePreset(const QString& preset_id, const QString& name);
  Q_INVOKABLE bool renamePreset(const QString& preset_id, const QString& name);
  Q_INVOKABLE bool deletePreset(const QString& preset_id);
  Q_INVOKABLE bool setPresetFavorite(const QString& preset_id, bool favorite);
  Q_INVOKABLE void setPieceMaterial(const QString& form_key, const QColor& tint_color, const QString& diffuse_path,
                                    const QString& normal_path, const QString& material_name);
  Q_INVOKABLE void clearPieceMaterial(const QString& form_key);
  Q_INVOKABLE bool saveProject(const QString& folder_or_url, const QString& name);
  Q_INVOKABLE bool openProject(const QString& folder_or_url);

 signals:
  void targetChanged();
  void changed();
  void validationChanged();
  void exportingChanged();
  void projectChanged();
  void presetsChanged();
  void exported(const QVariantMap& result);
  void exportFailed(const QString& message, const QVariantList& issues);
  // Asks the studio to load the NPC of a reopened project.
  void projectOpened(const QString& npc_form_key);

 private:
  void push(std::unique_ptr<projects::Command> command);
  void after_change();
  QVariantList asset_issues(const Json& validation) const;

  WorkerClient* worker_;
  ConsoleModel* console_;
  SnapshotProvider snapshot_;
  projects::NpcTarget target_;
  std::vector<outfit::Piece> current_;  // the NPC's outfit in the active load order
  QVariantList initial_inventory_;
  QVariantList inventory_;
  appearance::AppearanceDraft& draft_;
  EditHistory* history_;
  std::vector<outfit::Piece> last_pieces_;  // to re-validate only when the outfit itself changed
  QVariantMap validation_;
  bool validating_ = false;
  bool exporting_ = false;
  int validation_serial_ = 0;
  int preset_serial_ = 0;
  QTimer debounce_;
  QString project_path_;
  std::optional<projects::Project> project_;
  std::vector<outfit::Piece> pending_project_pieces_;
  FaceProvider face_provider_;
  FaceLoadedHandler face_loaded_handler_;
};

}  // namespace sns::app
