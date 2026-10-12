#pragma once

#include <QObject>
#include <QTimer>
#include <QVector3D>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "mo2_snapshot.hpp"
#include "outfit_editor.hpp"
#include "worker_client.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::app {

class SceneViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(QVariantList parts READ parts NOTIFY changed)
  Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
  Q_PROPERTY(QVector3D center READ center NOTIFY changed)
  Q_PROPERTY(float radius READ radius NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry* skeleton READ skeleton NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString status READ status NOTIFY loadingChanged)
  Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
  Q_PROPERTY(bool original READ original WRITE setOriginal NOTIFY originalChanged)
 public:
  SceneViewModel(WorkerClient* worker, OutfitEditor* outfit, std::function<SnapshotPtr()> snapshot, QObject* parent);
  ~SceneViewModel() override;
  QVariantList parts() const { return parts_; }
  QVariantList issues() const { return issues_; }
  QVariantMap stats() const { return stats_; }
  QVector3D center() const { return center_; }
  float radius() const { return radius_; }
  QQuick3DGeometry* skeleton() const { return skeleton_; }
  bool loading() const { return loading_; }
  QString status() const { return status_; }
  QString selected() const { return selected_; }
  bool original() const { return original_; }
  void setOriginal(bool value);
  Q_INVOKABLE void reload();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void setVisible(const QString& id, bool visible);
  Q_INVOKABLE void clear();
  Q_INVOKABLE QVariantMap selectedBounds() const;
 signals:
  void changed();
  void loadingChanged();
  void selectionChanged();
  void originalChanged();
  void ready();
 private:
  void install(const rendering::CharacterScene& scene);
  WorkerClient* worker_;
  OutfitEditor* outfit_;
  std::function<SnapshotPtr()> snapshot_;
  QTimer debounce_;
  QThreadPool pool_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  WorkerClient::CancelToken request_cancel_;
  int serial_ = 0;
  bool loading_ = false;
  bool original_ = false;
  QString status_;
  QString selected_;
  QVariantList parts_, issues_;
  QVariantMap stats_;
  QVector3D center_;
  float radius_ = 100;
  QQuick3DGeometry* skeleton_ = nullptr;
  QObject* resources_ = nullptr;
};
} // namespace sns::app
