#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector3D>
#include <QHash>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3D/QQuick3DTextureData>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>
#include <optional>

#include "appearance_editor.hpp"
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
  // Color of the HairTint parts (hair, brows, beards). A hair color edit changes only this: the
  // materials recolor at once, without rebuilding the scene or touching GPU buffers.
  Q_PROPERTY(QColor hairTint READ hair_tint NOTIFY hairTintChanged)
  // A viewport that is not on screen does not rebuild on every edit; it catches up when shown.
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
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
  QColor hair_tint() const { return hair_tint_; }
  bool active() const { return active_; }
  void setPose(const QString& pose);
  void setOriginal(bool value);
  void setActive(bool active);
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
  // Portrait renderer: this scene shows one fixed NPC, independent of the editors (empty: it follows the
  // outfit editor again), and builds with a resource cache of its own, so rendering many NPCs neither
  // grows nor evicts the main viewer's cache.
  void set_npc_override(const QString& form_key);
  void set_scene_cache(std::shared_ptr<rendering::SceneCache> cache) { own_cache_ = std::move(cache); }
  // Portraits: only the head is built, with textures no larger than a portrait needs.
  void set_portrait_mode(bool on) { portrait_mode_ = on; }
  [[nodiscard]] QString npc_override() const { return npc_override_; }
 signals:
  void changed();
  void visibilityChanged();
  void loadingChanged();
  void selectionChanged();
  void originalChanged();
  void previewModeChanged();
  void poseChanged();
  void hairTintChanged();
  void activeChanged();
  void ready();
 private:
  struct Prepared;
  struct PooledTexture {
    QQuick3DTextureData* gpu = nullptr;
    // Keeps the decoded image alive while its GPU copy is pooled: the pool key uses the image's
    // address, which therefore cannot be reused by another image in the meantime.
    std::shared_ptr<const textures::DdsImage> source;
  };
  void install(Prepared& prepared);
  void schedule(int delay_ms);
  void on_face_changed();
  void sync_hair_tint();
  void apply_hair_vertex_tint();
  QString effective_npc() const;
  // The face draft's preview when it belongs to the NPC this viewport shows; empty otherwise.
  FacePreview current_preview() const;
  WorkerClient* worker_;
  OutfitEditor* outfit_;
  AppearanceEditor* appearance_ = nullptr;
  std::function<SnapshotPtr()> snapshot_;
  QTimer debounce_;
  QThreadPool pool_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  WorkerClient::CancelToken request_cancel_;
  int serial_ = 0;
  int reload_delay_ms_ = 200;
  bool loading_ = false;
  bool original_ = false;
  bool armor_preview_ = false;
  bool active_ = true;
  bool stale_ = false;
  QString preview_mode_ = "piece";
  QString selected_armor_;
  QString pose_ = "reference";
  QString status_;
  QString selected_;
  std::optional<QStringList> preview_armor_;
  QString npc_override_;
  std::shared_ptr<rendering::SceneCache> own_cache_;
  bool portrait_mode_ = false;
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
  // What the installed scene was built from.
  bool installed_ = false;
  QString installed_npc_;
  FacePreview installed_preview_;
  QColor record_hair_;
  QColor hair_tint_ = QColor(Qt::white);
  // HairTint parts with vertex colors: Skyrim tints them by lerp(1, tint, vertex color green), so the
  // tint is written per vertex (the material stays white) and rewritten when the hair color changes.
  struct HairVertexTint {
    QPointer<QQuick3DGeometry> geometry;
    std::vector<float> vertices;  // the part's vertex data, with the NIF vertex colors
  };
  std::vector<HairVertexTint> hair_vertex_tint_;
  // GPU-side objects reused across builds when a part's data did not change.
  QObject* pool_owner_ = nullptr;
  QHash<QString, QQuick3DGeometry*> geometry_pool_, uv_pool_;
  QHash<QString, PooledTexture> texture_pool_;
};
} // namespace sns::app
