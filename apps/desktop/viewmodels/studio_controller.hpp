#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

#include "console_model.hpp"
#include "character_forge_viewmodel.hpp"
#include "edit_history.hpp"
#include "mo2_snapshot.hpp"
#include "sns/appearance/appearance_draft.hpp"
#include "sns/appearance/control_map.hpp"
#include "outfit_editor.hpp"
#include "appearance_editor.hpp"
#include "variant_list_model.hpp"
#include "worker_client.hpp"
#include "scene_viewmodel.hpp"

namespace sns::app {

// Root view model: MO2 instance/profile selection, scanning, indexing through the worker, and the
// browsers (mods, NPCs, armors). QML never touches files or plugins directly; it calls into here.
class StudioController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created in main()")
  Q_PROPERTY(WorkerClient* worker READ worker CONSTANT)
  Q_PROPERTY(ConsoleModel* console READ console CONSTANT)
  Q_PROPERTY(OutfitEditor* outfit READ outfit CONSTANT)
  Q_PROPERTY(CharacterForgeViewModel* forge READ forge CONSTANT)
  Q_PROPERTY(AppearanceEditor* appearance READ appearance CONSTANT)
  Q_PROPERTY(EditHistory* history READ history CONSTANT)
  Q_PROPERTY(SceneViewModel* scene READ scene CONSTANT)
  Q_PROPERTY(SceneViewModel* armorPreview READ armor_preview CONSTANT)
  Q_PROPERTY(VariantListModel* instances READ instances CONSTANT)
  Q_PROPERTY(VariantListModel* mods READ mods CONSTANT)
  Q_PROPERTY(VariantListModel* diagnostics READ diagnostics CONSTANT)
  Q_PROPERTY(VariantListModel* npcResults READ npc_results CONSTANT)
  Q_PROPERTY(VariantListModel* armorResults READ armor_results CONSTANT)
  Q_PROPERTY(int npcTotal READ npc_total NOTIFY npcResultsChanged)
  Q_PROPERTY(int armorTotal READ armor_total NOTIFY armorResultsChanged)
  // Plugins that define or override armors, for the catalog's mod filter: {plugin, mod, armors},
  // sorted by MO2 mod name.
  Q_PROPERTY(QVariantList armorPlugins READ armor_plugins NOTIFY armorPluginsChanged)
  Q_PROPERTY(QString instancePath READ instance_path NOTIFY instanceChanged)
  Q_PROPERTY(QString instanceName READ instance_name NOTIFY instanceChanged)
  Q_PROPERTY(QStringList profiles READ profiles NOTIFY instanceChanged)
  Q_PROPERTY(QString selectedProfile READ selected_profile NOTIFY instanceChanged)
  Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
  Q_PROPERTY(QString progressText READ progress_text NOTIFY progressChanged)
  Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
  Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
  Q_PROPERTY(QVariantMap npc READ npc NOTIFY npcChanged)
  Q_PROPERTY(bool npcLoading READ npc_loading NOTIFY npcChanged)
  // Appearance control map of the selected NPC (sns::appearance::analyze_appearance_control): which
  // plugin/mod controls each part, what it overrides, and whether record and FaceGen fit together.
  // {facePlugin, faceMod, worst, deviation, analysisUs, components: [...], findings: [...]}.
  Q_PROPERTY(QVariantMap appearanceControl READ appearance_control NOTIFY appearanceControlChanged)
  // The same map for every unique NPC of the profile (without the facial measurement, which needs a
  // FaceGen regeneration per NPC): {running, stage, listed, analysed, total, rpcCalls, errors,
  // elapsedMs, counts: {warning, info, ok}, codes: {code: n}, rows: [{formKey, name, worst, codes,
  // facePlugin, faceMod, faceGenMod, message}]} — rows only for NPCs with notes or warnings.
  Q_PROPERTY(QVariantMap appearanceScan READ appearance_scan NOTIFY appearanceScanChanged)

 public:
  enum class Phase { Idle, Scanning, Indexing, Ready, Error };
  Q_ENUM(Phase)

  explicit StudioController(QString worker_executable, QObject* parent = nullptr);
  ~StudioController() override;

  [[nodiscard]] WorkerClient* worker() const { return worker_; }
  [[nodiscard]] ConsoleModel* console() const { return console_; }
  [[nodiscard]] OutfitEditor* outfit() const { return outfit_; }
  [[nodiscard]] CharacterForgeViewModel* forge() const { return forge_; }
  [[nodiscard]] AppearanceEditor* appearance() const { return appearance_; }
  [[nodiscard]] EditHistory* history() const { return history_; }
  [[nodiscard]] SceneViewModel* scene() const { return scene_; }
  [[nodiscard]] SceneViewModel* armor_preview() const { return armor_preview_; }
  [[nodiscard]] VariantListModel* instances() const { return instances_; }
  [[nodiscard]] VariantListModel* mods() const { return mods_; }
  [[nodiscard]] VariantListModel* diagnostics() const { return diagnostics_; }
  [[nodiscard]] VariantListModel* npc_results() const { return npc_results_; }
  [[nodiscard]] VariantListModel* armor_results() const { return armor_results_; }
  [[nodiscard]] int npc_total() const { return npc_total_; }
  [[nodiscard]] int armor_total() const { return armor_total_; }
  [[nodiscard]] QVariantList armor_plugins() const { return armor_plugins_; }
  [[nodiscard]] QString instance_path() const { return instance_path_; }
  [[nodiscard]] QString instance_name() const { return instance_name_; }
  [[nodiscard]] QStringList profiles() const { return profiles_; }
  [[nodiscard]] QString selected_profile() const { return selected_profile_; }
  [[nodiscard]] Phase phase() const { return phase_; }
  [[nodiscard]] QString progress_text() const { return progress_text_; }
  [[nodiscard]] double progress() const { return progress_; }
  [[nodiscard]] QVariantMap summary() const { return summary_; }
  [[nodiscard]] QVariantMap npc() const { return npc_; }
  [[nodiscard]] bool npc_loading() const { return npc_loading_; }
  [[nodiscard]] QVariantMap appearance_control() const { return appearance_control_; }
  [[nodiscard]] QVariantMap appearance_scan() const;
  [[nodiscard]] SnapshotPtr snapshot() const { return snapshot_; }

  Q_INVOKABLE void discoverInstances();
  // Accepts a ModOrganizer.ini path, its folder, or a file:/// URL from a dialog.
  Q_INVOKABLE bool openInstance(const QString& path_or_url);
  Q_INVOKABLE bool setGamePath(const QString& path_or_url);
  Q_PROPERTY(QString gamePath READ game_path NOTIFY instanceChanged)
  [[nodiscard]] QString game_path() const { return game_path_; }
  Q_PROPERTY(QString mo2ModsDirectory READ mo2_mods_directory NOTIFY instanceChanged)
  [[nodiscard]] QString mo2_mods_directory() const;
  Q_INVOKABLE bool isMo2Running() const;
  Q_INVOKABLE QVariantMap installModToMo2(const QString& mod_dir);
  Q_INVOKABLE void loadProfile(const QString& profile);
  Q_INVOKABLE void reindex();
  Q_INVOKABLE void cancel();

  Q_INVOKABLE void searchNpcs(const QString& text, const QString& sex, const QString& kind, const QString& plugin);
  Q_INVOKABLE void selectNpc(const QString& form_key);
  Q_INVOKABLE void scanAppearanceControl();
  Q_INVOKABLE void cancelAppearanceScan();
  // Hands the control map's «Usar el rostro de X» of the selected NPC to the Easy Mod Creator, which
  // then plans (and exports) that patch. Returns false when the map offers no resolution.
  Q_INVOKABLE bool prepareFaceResolution();
  Q_INVOKABLE void searchArmors(const QString& text, int slot, const QString& plugin, const QString& armor_type = {}, bool smp_only = false);
  Q_INVOKABLE void filterMods(const QString& text, bool enabled_only);
  Q_INVOKABLE QVariantMap modDetails(const QString& mod_name) const;
  Q_INVOKABLE QString providerOf(const QString& plugin) const;
  Q_INVOKABLE QVariantMap resolveAsset(const QString& data_path) const;
  Q_INVOKABLE void openFolder(const QString& path) const;
  Q_INVOKABLE void copyToClipboard(const QString& text);
  Q_INVOKABLE void log(const QString& level, const QString& text);

 signals:
  void instanceChanged();
  void phaseChanged();
  void progressChanged();
  void summaryChanged();
  void npcResultsChanged();
  void armorResultsChanged();
  void armorPluginsChanged();
  void npcChanged();
  void appearanceControlChanged();
  void appearanceScanChanged();
  // selectNpc could not load the NPC (unknown FormKey, worker error); the message says why.
  void npcLoadFailed(const QString& message);
  void loaded();

 private:
  void set_phase(Phase phase);
  void set_progress(const QString& text, double value);
  void scan_finished(SnapshotPtr snapshot, const QString& error);
  void start_index();
  void rebuild_mod_rows();
  void refresh_plugin_counts();
  QVariantMap npc_row(const QVariantMap& item) const;
  // Recomputes appearance_control_ from npc_, the profile VFS and the facial analysis.
  void refresh_appearance_control();
  // Control map inputs from getNpc details (overrides with "mod", conflicts, faceGen paths), without the
  // facial measurement.
  [[nodiscard]] appearance::ControlInputs control_inputs(const QVariantMap& details) const;
  // Whether the NPC should have a FaceGen of its own: not with template traits, nor for a race that no
  // unique NPC of the profile has a FaceGen for (known after a profile scan; creatures).
  [[nodiscard]] bool facegen_expected(const QVariantMap& details) const;
  struct AppearanceScan;
  void scan_page(int offset);
  void scan_next();
  void scan_singles();
  void request_scan_npc(const std::shared_ptr<AppearanceScan>& scan, const QString& form_key);
  void scan_record(const QVariantMap& details);
  void scan_record_report(const QVariantMap& details, const appearance::ControlReport& report);
  void scan_finish();

  WorkerClient* worker_;
  ConsoleModel* console_;
  // The NPC being edited (face, body, outfit) and its single undo/redo history.
  appearance::AppearanceDraft draft_;
  EditHistory* history_ = nullptr;
  OutfitEditor* outfit_;
  CharacterForgeViewModel* forge_ = nullptr;
  AppearanceEditor* appearance_ = nullptr;
  SceneViewModel* scene_ = nullptr;
  SceneViewModel* armor_preview_ = nullptr;
  VariantListModel* instances_;
  VariantListModel* mods_;
  VariantListModel* diagnostics_;
  VariantListModel* npc_results_;
  VariantListModel* armor_results_;
  QList<QVariantMap> all_mod_rows_;
  QVariantList armor_plugins_;
  QHash<QString, QVariantMap> plugin_counts_;  // plugin (lower-case) -> {npcRecords, armorRecords, outfitRecords}
  int npc_total_ = 0;
  int armor_total_ = 0;
  QString instance_path_;
  QString game_path_;
  QString instance_name_;
  QStringList profiles_;
  QString selected_profile_;
  Phase phase_ = Phase::Idle;
  QString progress_text_;
  double progress_ = 0.0;
  QVariantMap summary_;
  QVariantMap npc_;
  QVariantMap appearance_control_;
  std::optional<appearance::FaceResolution> control_resolution_;  // of appearance_control_
  std::shared_ptr<AppearanceScan> scan_;
  QSet<QString> races_with_facegen_;  // race FormKeys (lower-case) with a FaceGen file for some unique NPC
  bool npc_loading_ = false;
  QString pending_npc_;
  SnapshotPtr snapshot_;
  std::shared_ptr<std::atomic<bool>> scan_cancel_;
  WorkerClient::CancelToken index_cancel_;
  WorkerClient::CancelToken npc_search_cancel_;
  WorkerClient::CancelToken armor_search_cancel_;
  int npc_search_serial_ = 0;
  int armor_search_serial_ = 0;
};

}  // namespace sns::app
