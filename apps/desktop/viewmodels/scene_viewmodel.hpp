#pragma once

#include <QObject>
#include <QTimer>
#include <QVector3D>
#include <QHash>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3D/QQuick3DTextureData>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>

#include "json_bridge.hpp"
#include "mo2_snapshot.hpp"
#include "outfit_editor.hpp"
#include "worker_client.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::app {

class OutfitEditor;
class AppearanceEditor;

class SceneViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(QVariantList parts READ parts NOTIFY changed)
  Q_PROPERTY(QVariantMap visibility READ visibility NOTIFY visibilityChanged)
  Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
  Q_PROPERTY(QVector3D center READ center NOTIFY changed)
  Q_PROPERTY(float radius READ radius NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry* skeleton READ skeleton NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString status READ status NOTIFY loadingChanged)
  Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
  Q_PROPERTY(bool original READ original WRITE setOriginal NOTIFY originalChanged)
  Q_PROPERTY(QString previewMode READ preview_mode NOTIFY previewModeChanged)
  Q_PROPERTY(QString selectedArmor READ selected_armor NOTIFY previewModeChanged)
  Q_PROPERTY(QString pose READ pose WRITE setPose NOTIFY poseChanged)
 public:
  SceneViewModel(WorkerClient* worker, OutfitEditor* outfit, AppearanceEditor* appearance, std::function<SnapshotPtr()> snapshot, QObject* parent,
                 bool armor_preview = false);
  ~SceneViewModel() override;
  QVariantList parts() const { return parts_; }
  QVariantMap visibility() const { return visibility_; }
  QVariantList issues() const { return issues_; }
  QVariantMap stats() const { return stats_; }
  QVector3D center() const { return center_; }
  float radius() const { return radius_; }
  QQuick3DGeometry* skeleton() const { return skeleton_; }
  bool loading() const { return loading_; }
  QString status() const { return status_; }
  QString selected() const { return selected_; }
  bool original() const { return original_; }
  QString preview_mode() const { return preview_mode_; }
  QString selected_armor() const { return selected_armor_; }
  QString pose() const { return pose_; }
  void setPose(const QString& pose);
  void setOriginal(bool value);
  Q_INVOKABLE void reload();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void setVisible(const QString& id, bool visible);
  Q_INVOKABLE void clear();
  Q_INVOKABLE QVariantMap selectedBounds() const;
  // Inspector data of the selected part (empty when nothing is selected).
  Q_INVOKABLE QVariantMap selectedPart() const;
  // Every part back to its default visibility (shapes the game does not draw stay hidden).
  Q_INVOKABLE void restoreVisibility();
  // Preview-only armor list (FormKeys; [] = skin only); null follows the outfit editor again.
  // Used by viewport verification; it never touches the project, the outfit or undo/redo.
  Q_INVOKABLE void setPreviewArmor(const QVariant& formKeys);
  Q_INVOKABLE void selectArmor(const QString& formKey);
  Q_INVOKABLE void setPreviewMode(const QString& mode);
 signals:
  void changed();
  void visibilityChanged();
  void loadingChanged();
  void selectionChanged();
  void originalChanged();
  void previewModeChanged();
  void poseChanged();
  void ready();
 private:
  void install(const rendering::CharacterScene& scene);
  void schedule(int delay_ms);
  WorkerClient* worker_;
  OutfitEditor* outfit_;
  AppearanceEditor* appearance_ = nullptr;
  std::function<SnapshotPtr()> snapshot_;
  QTimer debounce_;
  QThreadPool pool_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  WorkerClient::CancelToken request_cancel_;
  int serial_ = 0;
  bool loading_ = false;
  bool original_ = false;
  bool armor_preview_ = false;
  QString preview_mode_ = "piece";
  QString selected_armor_;
  QString pose_ = "reference";
  QString status_;
  QString selected_;
  std::optional<QStringList> preview_armor_;
  QVariantList parts_, issues_;
  QVariantMap stats_;
  QVariantMap visibility_;
  QVector3D center_;
  float radius_ = 100;
  QQuick3DGeometry* skeleton_ = nullptr;
  QObject* resources_ = nullptr;
  // Last catalog.getAppearance result: face/body edits rebuild the scene from it without a worker
  // round trip (the outfit, which is what the worker resolves, did not change).
  Json last_appearance_;
  QString last_request_key_;
  // GPU-side objects reused across builds when a part's data did not change.
  QObject* pool_owner_ = nullptr;
  QHash<QString, QQuick3DGeometry*> geometry_pool_, uv_pool_;
  QHash<QString, QQuick3DTextureData*> texture_pool_;
};
} // namespace sns::app
