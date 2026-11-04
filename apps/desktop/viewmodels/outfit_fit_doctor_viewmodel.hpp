#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
#include <vector>

#include "mo2_snapshot.hpp"
#include "sns/outfit/outfit_fit_doctor.hpp"

namespace sns::app {

class OutfitEditor;
class SceneViewModel;
class WorkerClient;

class OutfitFitDoctorViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool hasTarget READ has_target NOTIFY diagnosisChanged)
  Q_PROPERTY(QString npcFormKey READ npc_form_key NOTIFY diagnosisChanged)
  Q_PROPERTY(QString npcName READ npc_name NOTIFY diagnosisChanged)
  Q_PROPERTY(QString bodyFamily READ body_family NOTIFY diagnosisChanged)
  Q_PROPERTY(QString bodyConfidence READ body_confidence NOTIFY diagnosisChanged)
  Q_PROPERTY(QString bodyProvider READ body_provider NOTIFY diagnosisChanged)
  Q_PROPERTY(QString bodyMeshPath READ body_mesh_path NOTIFY diagnosisChanged)
  Q_PROPERTY(bool bodyHasTri READ body_has_tri NOTIFY diagnosisChanged)
  Q_PROPERTY(QString outfitName READ outfit_name NOTIFY diagnosisChanged)
  Q_PROPERTY(QString outfitFormKey READ outfit_form_key NOTIFY diagnosisChanged)
  Q_PROPERTY(QVariantList pieces READ pieces NOTIFY diagnosisChanged)
  Q_PROPERTY(int totalIssuesCount READ total_issues_count NOTIFY diagnosisChanged)
  Q_PROPERTY(int incompatiblePiecesCount READ incompatible_pieces_count NOTIFY diagnosisChanged)
  Q_PROPERTY(int fixableAlternativesCount READ fixable_alternatives_count NOTIFY diagnosisChanged)
  Q_PROPERTY(bool needsBodyslideRefit READ needs_bodyslide_refit NOTIFY diagnosisChanged)
  Q_PROPERTY(bool isDraftMode READ is_draft_mode NOTIFY draftChanged)
  Q_PROPERTY(QVariantList proposedRepairs READ proposed_repairs NOTIFY draftChanged)
  Q_PROPERTY(bool canUndoRepair READ can_undo_repair NOTIFY draftChanged)
  Q_PROPERTY(QString activeComparisonMode READ active_comparison_mode NOTIFY comparisonModeChanged)
  Q_PROPERTY(bool wireframeEnabled READ wireframe_enabled NOTIFY wireframeChanged)
  Q_PROPERTY(bool analyzing READ analyzing NOTIFY analyzingChanged)

 public:
  using SnapshotProvider = std::function<SnapshotPtr()>;

  explicit OutfitFitDoctorViewModel(QObject* parent = nullptr);
  OutfitFitDoctorViewModel(SnapshotProvider snapshot, OutfitEditor* outfit, SceneViewModel* scene,
                           WorkerClient* worker, QObject* parent = nullptr);

  void set_dependencies(SnapshotProvider snapshot, OutfitEditor* outfit, SceneViewModel* scene, WorkerClient* worker);
  Q_INVOKABLE void init(QObject* studio_obj);

  [[nodiscard]] bool has_target() const;
  [[nodiscard]] QString npc_form_key() const;
  [[nodiscard]] QString npc_name() const;
  [[nodiscard]] QString body_family() const;
  [[nodiscard]] QString body_confidence() const;
  [[nodiscard]] QString body_provider() const;
  [[nodiscard]] QString body_mesh_path() const;
  [[nodiscard]] bool body_has_tri() const;
  [[nodiscard]] QString outfit_name() const;
  [[nodiscard]] QString outfit_form_key() const;
  [[nodiscard]] QVariantList pieces() const;
  [[nodiscard]] int total_issues_count() const;
  [[nodiscard]] int incompatible_pieces_count() const;
  [[nodiscard]] int fixable_alternatives_count() const;
  [[nodiscard]] bool needs_bodyslide_refit() const;
  [[nodiscard]] bool is_draft_mode() const;
  [[nodiscard]] QVariantList proposed_repairs() const;
  [[nodiscard]] bool can_undo_repair() const;
  [[nodiscard]] QString active_comparison_mode() const { return comparison_mode_; }
  [[nodiscard]] bool wireframe_enabled() const { return wireframe_enabled_; }
  [[nodiscard]] bool analyzing() const { return analyzing_; }

  Q_INVOKABLE void runDiagnosis();
  Q_INVOKABLE void scheduleDiagnosis();
  Q_INVOKABLE void setComparisonMode(const QString& mode);
  Q_INVOKABLE void applyAlternativeRepair(int piece_index, int alternative_index);
  Q_INVOKABLE void undoLastRepair();
  Q_INVOKABLE void revertAllRepairs();
  Q_INVOKABLE bool exportDoctorMod(const QString& target_dir_or_url, const QString& mod_name = "OutfitFitDoctor_Fix");
  Q_INVOKABLE void isolatePieceInViewport(int piece_index, bool isolate);
  Q_INVOKABLE void setViewportCameraPreset(const QString& view_preset);
  Q_INVOKABLE void toggleWireframe(bool enabled);

 signals:
  void diagnosisChanged();
  void analyzingChanged();
  void draftChanged();
  void comparisonModeChanged();
  void wireframeChanged();
  void cameraPresetRequested(const QString& preset);
  void exportCompleted(const QString& outputDirectory, const QString& zipPath);
  void exportError(const QString& errorMessage);

 private:
  struct RepairAction {
    int piece_index = 0;
    QString original_mesh_path;
    QString repaired_mesh_path;
    QString mod_name;
    QString description;
  };

  void executeDiagnosis();
  void update_scene_preview();

  SnapshotProvider snapshot_;
  OutfitEditor* outfit_ = nullptr;
  SceneViewModel* scene_ = nullptr;
  WorkerClient* worker_ = nullptr;

  sns::outfit::OutfitFitReport report_;
  std::vector<RepairAction> repair_history_;
  QString comparison_mode_ = "A";  // "A" (Current) or "B" (Proposed/Repaired)
  bool wireframe_enabled_ = false;
  int isolated_piece_index_ = -1;
  int diagnosis_serial_ = 0;
  bool analyzing_ = false;
  QTimer debounce_timer_;
};

}  // namespace sns::app
