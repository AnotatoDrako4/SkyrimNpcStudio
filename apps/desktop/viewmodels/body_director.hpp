#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <vector>

#include "mo2_snapshot.hpp"
#include "sns/body/bcng_rules.hpp"
#include "worker_client.hpp"

namespace sns::app {

// Body preset director (Body Change NG schema 8 over BodySlide presets of the active MO2 profile).
// Reads the profile's presets, BCNG settings/distribution and the selected NPC's effective body (worker body.inspectNpcs +
// the winning body NIF), and keeps two operations apart:
//   A. give the NPC a preset of its own body family (a "npc" rule in a draft distribution), and
//   B. evaluate a body family change (3BA <-> UBE): read-only, never exported from here.
// The draft is merged in front of the profile's current BodyChangeNGdistribution.json and exported through the worker
// (export.bodyDistribution: staging, read-back, manifest, optional ZIP) into a NEW mod folder chosen by the user. Nothing in
// MO2 or the game is written, and nothing here was seen inside Skyrim: the report always says PENDIENTE_SKYRIM.
class BodyDirector : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  // {state: "none"|"loading"|"ready"|"error", error, bcng, obody, racemenu, skypatcher (MO2 providers, "" = absent), globalFamily,
  //  settingsFromFile, settingsError, distributionProvider, distributionRules, distributionError, installedFamilies,
  //  ubeRacePlugin, presets, files, unreadableFiles, duplicates, families: [{id, label, count}]}
  Q_PROPERTY(QVariantMap environment READ environment NOTIFY environmentChanged)
  // {state: "none"|"inspecting"|"ready"|"error", error, formKey, name, editorId, sex, race, family, familyLabel, familyMask,
  //  layout, child, beast, follower, customFace, faceProvider, body: {path, provider, archived, shapes, tri, triProvider,
  //  physicsBones}, evidence: [...], issues: [{severity, code, message}], changes: [{target, label, verdict, verdictLabel, issues}]}
  Q_PROPERTY(QVariantMap npc READ npc NOTIFY npcChanged)
  // Presets after the filter, each with its verdict for the current NPC (manual assignment):
  // [{uid, name, source, file, provider, family, familyLabel, reason, groups, sliders, refit, duplicate, verdict, verdictLabel,
  //   issues, assigned}]
  Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
  Q_PROPERTY(int presetTotal READ preset_total NOTIFY presetsChanged)
  // {rules: [{id, formKey, npcName, preset, file, provider, family, verdict, verdictLabel, experimental}], baseRules, baseProvider,
  //  baseError, mergedRules, errors: [...], text}
  Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
  // {state: "none"|"exporting"|"done"|"error", error, modDirectory, zipPath, manifestPath, files: [{path, bytes, sha256}],
  //  verification: [...], status: "PAQUETE_MO2_VERIFICADO"|"", inGame: "PENDIENTE_SKYRIM"}
  Q_PROPERTY(QVariantMap exportResult READ export_result NOTIFY exportResultChanged)
  Q_PROPERTY(QString defaultExportDirectory READ default_export_directory CONSTANT)

 public:
  explicit BodyDirector(WorkerClient* worker, QObject* parent = nullptr);
  ~BodyDirector() override;

  [[nodiscard]] QVariantMap environment() const { return environment_; }
  [[nodiscard]] QVariantMap npc() const { return npc_; }
  [[nodiscard]] QVariantList presets() const { return presets_; }
  [[nodiscard]] int preset_total() const { return preset_total_; }
  [[nodiscard]] QVariantMap draft() const { return draft_; }
  [[nodiscard]] QVariantMap export_result() const { return export_result_; }
  [[nodiscard]] QString default_export_directory() const;

  // A new MO2 profile (or none): presets, environment, NPC and draft are dropped and the library is read again.
  void set_profile(SnapshotPtr snapshot);

  // Inspects one NPC (FormKey "0A2C8E:Skyrim.esm"); stale answers of a previous NPC or profile are ignored.
  Q_INVOKABLE void inspect(const QString& form_key);
  // family: "" (all) or a family id ("3ba", "cbbe", "ube", "other", "unknown"); compatibleOnly hides blocked presets.
  Q_INVOKABLE void setFilter(const QString& text, const QString& family, bool compatible_only);
  // Adds (or replaces) the current NPC's rule with this preset. Experimental verdicts need `accept_experimental`; blocked and
  // needs-resources verdicts are refused. Returns "" on success, else the reason.
  Q_INVOKABLE QString assign(const QString& uid, bool accept_experimental);
  Q_INVOKABLE void removeRule(const QString& id);
  Q_INVOKABLE void clearDraft();
  // Exports the merged distribution as a new mod "<modName>" (and ZIP) inside `outputDir` (a folder outside MO2 and the game).
  Q_INVOKABLE void exportDistribution(const QString& mod_name, const QString& output_dir_or_url, bool create_zip);
  Q_INVOKABLE void reload();

 signals:
  void environmentChanged();
  void npcChanged();
  void presetsChanged();
  void draftChanged();
  void exportResultChanged();

 private:
  struct Library;
  struct Analysis;
  struct DraftRule {
    QString id, form_key, npc_name, uid, preset, file, provider, family;
    QString verdict, verdict_label;
    bool experimental = false;
    body::bcng::Rule rule;
  };

  void start_load();
  void rebuild_presets();
  void rebuild_draft();

  WorkerClient* worker_;
  SnapshotPtr snapshot_;
  std::shared_ptr<Library> library_;
  std::shared_ptr<Analysis> analysis_;
  int profile_serial_ = 0;
  int npc_serial_ = 0;
  QString filter_text_, filter_family_;
  bool filter_compatible_ = false;
  std::vector<DraftRule> rules_;
  std::string merged_text_;
  QVariantMap environment_, npc_, draft_, export_result_;
  QVariantList presets_;
  int preset_total_ = 0;
};

}  // namespace sns::app
