#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "appearance_editor.hpp"
#include "beauty_lab_engine.hpp"
#include "console_model.hpp"
#include "edit_history.hpp"
#include "mo2_snapshot.hpp"
#include "outfit_editor.hpp"
#include "scene_viewmodel.hpp"

namespace sns::app {

class BeautyLabViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")

  // FASE 1 — Comparador Original vs Propuesta, procedencia MO2 y estado de propuesta
  Q_PROPERTY(QVariantMap comparison READ comparison NOTIFY comparisonChanged)
  Q_PROPERTY(QVariantList provenance READ provenance NOTIFY comparisonChanged)
  Q_PROPERTY(QVariantList incompatibleAlerts READ incompatible_alerts NOTIFY comparisonChanged)
  Q_PROPERTY(QString proposalStatus READ proposal_status NOTIFY comparisonChanged)
  Q_PROPERTY(QString proposalStatusLabel READ proposal_status_label NOTIFY comparisonChanged)
  Q_PROPERTY(QString proposalStatusDetail READ proposal_status_detail NOTIFY comparisonChanged)
  Q_PROPERTY(bool hasChanges READ has_changes NOTIFY comparisonChanged)
  Q_PROPERTY(int changeCount READ change_count NOTIFY comparisonChanged)
  Q_PROPERTY(QString comparatorMode READ comparator_mode WRITE setComparatorMode NOTIFY comparatorModeChanged)

  // FASE 2 — Hair Studio & selector avanzado de HeadParts
  Q_PROPERTY(QString hairCategory READ hair_category WRITE setHairCategory NOTIFY hairStudioChanged)
  Q_PROPERTY(QString hairSearchText READ hair_search_text WRITE setHairSearchText NOTIFY hairStudioChanged)
  Q_PROPERTY(QString hairPackageFilter READ hair_package_filter WRITE setHairPackageFilter NOTIFY hairStudioChanged)
  Q_PROPERTY(QString hairCompatibilityFilter READ hair_compatibility_filter WRITE setHairCompatibilityFilter NOTIFY hairStudioChanged)
  Q_PROPERTY(QString eyeColorGroupFilter READ eye_color_group_filter WRITE setEyeColorGroupFilter NOTIFY hairStudioChanged)
  Q_PROPERTY(QVariantList filteredHeadParts READ filtered_head_parts NOTIFY hairStudioChanged)
  Q_PROPERTY(QVariantList availablePackages READ available_packages NOTIFY hairStudioChanged)
  Q_PROPERTY(QVariantMap selectedHeadPartInspection READ selected_head_part_inspection NOTIFY headPartInspectionChanged)

  // FASE 3 — Face Beauty Workshop capabilities & technical limits
  Q_PROPERTY(QVariantMap workshopCapabilities READ workshop_capabilities NOTIFY workshopCapabilitiesChanged)

  // FASE 4 — Next-Gen Visual Presets
  Q_PROPERTY(QVariantList stylePresets READ style_presets CONSTANT)
  Q_PROPERTY(QVariantMap lastStyleResult READ last_style_result NOTIFY styleResultChanged)

  // FASE 5 — Visual Quality Check
  Q_PROPERTY(QVariantMap qualityCheck READ quality_check NOTIFY qualityCheckChanged)
  Q_PROPERTY(QVariantMap lastCaptureReport READ last_capture_report NOTIFY captureReportChanged)

  // FASE 6 — NPC Beauty Library (persistente)
  Q_PROPERTY(QString libraryPath READ library_path CONSTANT)
  Q_PROPERTY(QVariantList libraryPresets READ library_presets NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList favoriteHairs READ favorite_hairs NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList headEyeCombos READ head_eye_combos NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList facialConfigs READ facial_configs NOTIFY libraryChanged)
  Q_PROPERTY(QVariantList importedResources READ imported_resources NOTIFY libraryChanged)
  Q_PROPERTY(QVariantMap lastLibraryStatus READ last_library_status NOTIFY libraryStatusChanged)

 public:
  using SnapshotGetter = std::function<SnapshotPtr()>;

  BeautyLabViewModel(AppearanceEditor* appearance,
                     OutfitEditor* outfit,
                     SceneViewModel* scene,
                     EditHistory* history,
                     ConsoleModel* console,
                     SnapshotGetter snapshot,
                     QObject* parent = nullptr);

  // Getters
  [[nodiscard]] QVariantMap comparison() const { return comparison_; }
  [[nodiscard]] QVariantList provenance() const { return comparison_.value("provenanceSummary").toList(); }
  [[nodiscard]] QVariantList incompatible_alerts() const { return comparison_.value("incompatibleAlerts").toList(); }
  [[nodiscard]] QString proposal_status() const { return comparison_.value("proposalStatus").toString(); }
  [[nodiscard]] QString proposal_status_label() const { return comparison_.value("proposalStatusLabel").toString(); }
  [[nodiscard]] QString proposal_status_detail() const { return comparison_.value("proposalStatusDetail").toString(); }
  [[nodiscard]] bool has_changes() const { return comparison_.value("hasChanges").toBool(); }
  [[nodiscard]] int change_count() const { return comparison_.value("changeCount").toInt(); }
  [[nodiscard]] QString comparator_mode() const { return comparator_mode_; }

  [[nodiscard]] QString hair_category() const { return hair_category_; }
  [[nodiscard]] QString hair_search_text() const { return hair_search_text_; }
  [[nodiscard]] QString hair_package_filter() const { return hair_package_filter_; }
  [[nodiscard]] QString hair_compatibility_filter() const { return hair_compatibility_filter_; }
  [[nodiscard]] QString eye_color_group_filter() const { return eye_color_group_filter_; }
  [[nodiscard]] QVariantList filtered_head_parts() const { return filtered_head_parts_; }
  [[nodiscard]] QVariantList available_packages() const { return available_packages_; }
  [[nodiscard]] QVariantMap selected_head_part_inspection() const { return selected_head_part_inspection_; }

  [[nodiscard]] QVariantMap workshop_capabilities() const { return workshop_capabilities_; }

  [[nodiscard]] QVariantList style_presets() const { return style_presets_; }
  [[nodiscard]] QVariantMap last_style_result() const { return last_style_result_; }

  [[nodiscard]] QVariantMap quality_check() const { return quality_check_; }
  [[nodiscard]] QVariantMap last_capture_report() const { return last_capture_report_; }

  [[nodiscard]] QString library_path() const { return library_.file_path(); }
  [[nodiscard]] QVariantList library_presets() const;
  [[nodiscard]] QVariantList favorite_hairs() const;
  [[nodiscard]] QVariantList head_eye_combos() const;
  [[nodiscard]] QVariantList facial_configs() const;
  [[nodiscard]] QVariantList imported_resources() const { return library_.imported_resources(); }
  [[nodiscard]] QVariantMap last_library_status() const { return last_library_status_; }

  // FASE 1 Invokables
  Q_INVOKABLE void setComparatorMode(const QString& mode);
  Q_INVOKABLE void revertSingleChange(const QString& category, const QString& key);
  Q_INVOKABLE void refreshAll();

  // FASE 2 Invokables
  Q_INVOKABLE void setHairCategory(const QString& category);
  Q_INVOKABLE void setHairSearchText(const QString& text);
  Q_INVOKABLE void setHairPackageFilter(const QString& packageFamily);
  Q_INVOKABLE void setHairCompatibilityFilter(const QString& filter);
  Q_INVOKABLE void setEyeColorGroupFilter(const QString& filter);
  Q_INVOKABLE void refreshHeadPartGallery();
  Q_INVOKABLE void inspectHeadPart(const QVariantMap& headPartRow);
  Q_INVOKABLE void selectAndApplyHeadPart(const QString& formKey, const QString& type = QString());
  Q_INVOKABLE void applyInspectedHeadPart();

  // FASE 4 Invokables
  Q_INVOKABLE QVariantMap applyStylePreset(const QString& styleId, const QVariantMap& options = {});

  // FASE 5 Invokables
  Q_INVOKABLE QVariantMap runQualityCheck();
  Q_INVOKABLE QVariantMap captureViewportWithMetadata(QObject* viewportOrWindow, const QString& notes = QString());

  // FASE 6 Invokables
  Q_INVOKABLE QVariantMap saveCurrentAsPreset(const QString& name,
                                              const QString& category = QStringLiteral("Custom"),
                                              const QString& notes = QString(),
                                              QObject* viewportOrWindow = nullptr);
  Q_INVOKABLE bool deletePreset(const QString& id);
  Q_INVOKABLE QVariantMap applyLibraryEntry(const QVariantMap& entry);
  Q_INVOKABLE bool toggleFavoriteHair(const QVariantMap& hairRow);
  Q_INVOKABLE bool isHairFavorite(const QString& formKey) const;
  Q_INVOKABLE QVariantMap saveCurrentHeadEyeCombo(const QString& name, const QString& notes = QString());
  Q_INVOKABLE bool deleteHeadEyeCombo(const QString& id);
  Q_INVOKABLE QVariantMap saveCurrentFacialConfig(const QString& name, const QString& notes = QString());
  Q_INVOKABLE bool deleteFacialConfig(const QString& id);
  Q_INVOKABLE QVariantMap importCompatibleResource(const QString& pathOrUrl,
                                                   const QString& categoryTag = QStringLiteral("Custom"));
  Q_INVOKABLE bool deleteImportedResource(const QString& id);
  Q_INVOKABLE bool reloadLibrary();

  // FASE 7 — Invalidación de caché y recarga limpia
  Q_INVOKABLE void invalidateCaches();

 signals:
  void comparisonChanged();
  void comparatorModeChanged();
  void hairStudioChanged();
  void headPartInspectionChanged();
  void workshopCapabilitiesChanged();
  void styleResultChanged();
  void qualityCheckChanged();
  void captureReportChanged();
  void libraryChanged();
  void libraryStatusChanged();

 private:
  void rebuild_comparison_and_capabilities();
  void rebuild_filtered_head_parts();
  void rebuild_quality_check();
  void set_library_status(bool ok, const QString& message, const QStringList& warnings = {});
  [[nodiscard]] QVariantList annotate_entries_compatibility(const QVariantList& entries) const;

  AppearanceEditor* appearance_ = nullptr;
  OutfitEditor* outfit_ = nullptr;
  SceneViewModel* scene_ = nullptr;
  EditHistory* history_ = nullptr;
  ConsoleModel* console_ = nullptr;
  SnapshotGetter snapshot_;

  BeautyLibraryStore library_;

  QVariantMap comparison_;
  QString comparator_mode_ = QStringLiteral("live");

  QString hair_category_ = QStringLiteral("Hair");
  QString hair_search_text_;
  QString hair_package_filter_ = QStringLiteral("all");
  QString hair_compatibility_filter_ = QStringLiteral("all");
  QString eye_color_group_filter_ = QStringLiteral("all");
  QVariantList filtered_head_parts_;
  QVariantList available_packages_;
  QVariantMap selected_head_part_inspection_;

  QVariantMap workshop_capabilities_;
  QVariantList style_presets_;
  QVariantMap last_style_result_;
  QVariantMap quality_check_;
  QVariantMap last_capture_report_;
  QVariantMap last_library_status_;
};

}  // namespace sns::app
