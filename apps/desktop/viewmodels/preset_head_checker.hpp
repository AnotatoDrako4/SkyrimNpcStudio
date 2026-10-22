#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <vector>

#include "mo2_snapshot.hpp"
#include "sns/presets/export_head_match.hpp"
#include "worker_client.hpp"

namespace sns::app {

// Preset <-> Export Head readiness of a RaceMenu .jslot (sns::presets::match_export_head): which Export Head NIF (RaceMenu
// "Export Head", found under SKSE/Plugins/CharGen of the profile's mods) belongs to the preset and whether it can be a face
// source. Diagnosis only: it reads the preset and the heads (confined to each mod's folder, opened by the safe NIF loader), asks
// the worker for the EditorIDs of the preset's head parts, and exports nothing; a manual choice is remembered per profile in a small
// JSON file of the studio (the only thing written), and ignored again (flagged `manualStale`) when its head is gone.
// A present NIF is not a safe FaceGen and nothing here was seen inside Skyrim (the report always says UNVERIFIED_IN_SKYRIM).
class PresetHeadChecker : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  // {state: "none" | "indexing" | "checking" | "ready" | "error", preset: {name, path}, verdict, codes, message, caveats,
  //  chosen: {index, path, mod, modified, faceVertices, missingTextures}, needsManualChoice, ranked: [{index, path, mod, score,
  //  overlap, common, exactName, nearName, reason}], allHeads: [{index, label, readable}], parts: [{formKey, editorId, type, modelPath}],
  //  unresolved: [...], faceVertices, error, timings: {indexMs, checkMs}}
  Q_PROPERTY(QVariantMap report READ report NOTIFY reportChanged)
  // {state: "none" | "indexing" | "ready", count, readable, unreadable, bytes, ms}: the Export Heads of the profile.
  Q_PROPERTY(QVariantMap heads READ heads NOTIFY headsChanged)

 public:
  explicit PresetHeadChecker(WorkerClient* worker, QObject* parent = nullptr);
  ~PresetHeadChecker() override;
  [[nodiscard]] QVariantMap report() const { return report_; }
  [[nodiscard]] QVariantMap heads() const { return heads_; }

  // A new MO2 profile (or none): the head index, the report and the manual choices are dropped.
  // `profile_key` names the file the manual choices are kept in (AppLocalData/preset-head-choices/<key>.json, or
  // SNS_PRESET_HEAD_DIR); with an empty key they live only in memory.
  void set_profile(SnapshotPtr snapshot, const QString& profile_key = {});
  // Where the manual choices of this profile are stored ("" without a profile key).
  [[nodiscard]] QString choices_file() const { return store_path_; }

  Q_INVOKABLE void check(const QString& jslot_path);
  // Manual choice among the heads of the profile (`index` of report.allHeads); re-runs the pairing. False when unknown.
  Q_INVOKABLE bool choose(int index);
  Q_INVOKABLE void clearChoice();
  // Reads the choices file again (what a new session would find) and re-runs the pairing of the current preset.
  Q_INVOKABLE void reloadChoices();
  Q_INVOKABLE void reindex();
  Q_INVOKABLE QVariantMap metrics() const;

 signals:
  void reportChanged();
  void headsChanged();

 private:
  struct Head;
  struct Index;
  struct Parts;
  void start_index();
  void on_index(int serial, const std::shared_ptr<Index>& index);
  void start_check(const QString& jslot_path);
  void run_match();
  void set_report(QVariantMap report);
  void load_choices();
  // Atomic write; a choices file that could not be read is never overwritten (the choice then only lasts the session).
  QString save_choices();

  QString store_path_, profile_key_;
  QHash<QString, QString> chosen_at_;  // preset path (lower case) -> when the choice was made (ISO 8601 with offset)
  bool store_unreadable_ = false;
  QString persist_error_;
  WorkerClient* worker_;
  SnapshotPtr snapshot_;
  std::shared_ptr<Index> index_;
  bool indexing_ = false;
  int profile_serial_ = 0;
  int check_serial_ = 0;
  QString current_path_;            // the preset of the current report
  std::shared_ptr<Parts> parts_;    // what the preset and the worker said about it (kept for choose())
  QHash<QString, QString> manual_;  // preset path (lower case) -> chosen head path
  QVariantMap report_, heads_;
  int checks_ = 0;
  qint64 last_check_ms_ = 0, max_check_ms_ = 0, last_index_ms_ = 0;
};

}  // namespace sns::app
