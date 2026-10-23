#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include "forge_scene_viewmodel.hpp"
#include "worker_client.hpp"

#include "sns/creation/forge_project.hpp"

namespace sns::app {

class ForgeBlenderBridge;

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
  Q_PROPERTY(bool blenderBusy READ blender_busy NOTIFY changed)
  Q_PROPERTY(bool catalogReady READ catalog_ready NOTIFY changed)
  Q_PROPERTY(QVariantMap workspace READ workspace NOTIFY changed)
  Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
  Q_PROPERTY(bool canExportTechnical READ can_export_technical NOTIFY changed)
  // ---- Follower behavior (AG04): every answer comes from the profile the worker has indexed.
  // {state: "none" | "loading" | "ready" | "error", reason, loadOrderFingerprint, archetypes: [{id, displayName, roleSummary, classFormKey,
  //  combatStyleFormKey, allLinksResolved, requiredMasters, issues}], classes, combatStyles, voices}
  Q_PROPERTY(QVariantMap behaviorCatalog READ behavior_catalog NOTIFY behaviorChanged)
  // The last Vista previa / Aplicar: {state: "idle" | "loading" | "ready" | "conflict" | "notApplicable" | "stale" | "error" | "cancelled" |
  //  "applied", kind: "preview" | "apply", archetypeId, message, changes: [{category, field, before, after}], appliedCategories,
  //  allLinksResolved, unresolved: [...], issues: [...], loadOrderFingerprint}. A preview never touches the draft.
  Q_PROPERTY(QVariantMap archetypePreview READ archetype_preview NOTIFY behaviorChanged)
  // Static evidence of the NPC's voice (VoicesFollower / VoicesMarriage FormLists of the profile): {state: "none" | "loading" | "ready" |
  //  "error", reason, analysis: {...creation.analyzeVoice VoiceAnalysis}, loadOrderFingerprint}. Refreshed whenever voice, sex or
  //  factions change, so a value for another voice is never shown. It is evidence, not an in-game guarantee.
  Q_PROPERTY(QVariantMap voiceAnalysis READ voice_analysis NOTIFY behaviorChanged)
  Q_PROPERTY(bool behaviorBusy READ behavior_busy NOTIFY behaviorChanged)
  // Empty while the worker is ready; otherwise what the studio knows about it ("connection lost", "starting"...). The controls that need the
  // worker say this instead of a generic "index the profile".
  Q_PROPERTY(QString workerMessage READ worker_message NOTIFY changed)
  // False when the worker does not know the behavior methods (older worker): the controls say so instead of doing nothing.
  Q_PROPERTY(bool behaviorSupported READ behavior_supported NOTIFY behaviorChanged)
  // The last record search: {state: "idle" | "loading" | "ready" | "error", kind, count, code, error}. 0 results, an unindexed profile and
  // a refused kind are told apart.
  Q_PROPERTY(QVariantMap recordSearch READ record_search NOTIFY recordsChanged)
  // The 3D preview of the selected model (independent from the NPC scenes of the other workspaces).
  Q_PROPERTY(ForgeSceneViewModel* scene READ scene CONSTANT)

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
  bool blender_busy() const;
  bool catalog_ready() const { return catalog_ready_; }
  QVariantMap workspace() const;
  QVariantMap capabilities() const;
  bool can_export_technical() const { return catalog_ready_ && !busy_ && validation_.value("technicallyExportable").toBool(); }
  ForgeSceneViewModel* scene() const { return scene_; }
  QVariantMap behavior_catalog() const { return behavior_catalog_.isEmpty() ? QVariantMap{{"state", "none"}} : behavior_catalog_; }
  QVariantMap archetype_preview() const { return archetype_preview_.isEmpty() ? QVariantMap{{"state", "idle"}} : archetype_preview_; }
  QVariantMap voice_analysis() const { return voice_analysis_.isEmpty() ? QVariantMap{{"state", "none"}} : voice_analysis_; }
  bool behavior_busy() const { return behavior_busy_; }
  QString worker_message() const { return worker_->state() == WorkerClient::State::Ready ? QString() : worker_->statusText(); }
  bool behavior_supported() const { return behavior_supported_; }
  QVariantMap record_search() const { return record_search_.isEmpty() ? QVariantMap{{"state", "idle"}} : record_search_; }
  // Identifies the profile the studio has indexed (the key the worker was indexed with). Behavior requests carry it, so the worker
  // refuses one made for another profile instead of answering it with this profile's evidence.
  void set_profile_key(const QString& key);
  [[nodiscard]] QString profile_key() const { return profile_key_; }
  // How long a behavior request (catalog, preview, apply, voice) may take before it fails with a visible error. 60 s by default; the
  // real-window harness shortens it to prove that a timeout leaves the draft alone and the controls usable.
  void set_behavior_timeout(std::chrono::milliseconds timeout) { behavior_timeout_ = timeout; }

  // Loads the archetypes, classes, combat styles and voices of the indexed profile (once per profile; the controls call it when they appear).
  Q_INVOKABLE void loadBehaviorCatalog();
  // Dry run of an archetype on the current draft: shows what would change, edits nothing. `categories` empty = every authorized one.
  Q_INVOKABLE void previewArchetype(const QString& archetype_id, const QStringList& categories = {});
  // Asks again (the draft and the profile may have changed since a preview), checks the result and applies it as ONE undoable edit.
  Q_INVOKABLE void applyArchetype(const QString& archetype_id, const QStringList& categories = {});
  Q_INVOKABLE void discardArchetypePreview();
  Q_INVOKABLE void refreshVoiceAnalysis();

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
  Q_INVOKABLE void copySelectedModelToProject();
  Q_INVOKABLE void selectLibraryModel(int index);
  Q_INVOKABLE void selectBlenderExecutable(const QString& url);
  Q_INVOKABLE void detectBlender();
  Q_INVOKABLE void convertSelectedModelWithBlender();
  Q_INVOKABLE void selectReferenceImage(const QString& role, const QString& url);
  void invalidate_validation();
  void set_catalog_ready(bool ready);

 signals:
  void changed();
  void recordsChanged();
  void behaviorChanged();

 private:
  template <class F> void edit(F&& change, bool affects_validation = true);
  void run_archetype(const QString& archetype_id, const QStringList& categories, bool commit);
  void set_preview(QVariantMap preview);
  void mark_preview_stale(const QString& why);
  void reset_behavior();
  void merge_behavior_validation(const Json& behavior);
  void preview_selected_model();
  EnvironmentProvider environment_;
  ForgeSceneViewModel* scene_ = nullptr;
  ForgeBlenderBridge* blender_bridge_ = nullptr;
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
  // Follower behavior. One serial per kind of request: a newer request, a profile change or a draft edit makes an older answer stale.
  QString profile_key_;
  QVariantMap behavior_catalog_, archetype_preview_, voice_analysis_, record_search_;
  std::chrono::milliseconds behavior_timeout_{std::chrono::seconds(60)};
  bool behavior_busy_ = false;
  bool behavior_supported_ = true;
  int behavior_serial_ = 0, catalog_serial_ = 0, voice_serial_ = 0;
  WorkerClient::CancelToken behavior_cancel_;
};

}  // namespace sns::app
