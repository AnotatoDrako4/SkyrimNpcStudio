#pragma once

#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

namespace sns::app {

// The 3D preview of Character Forge: one external model (.glb / .gltf) shown with Qt Quick 3D's
// RuntimeLoader. Independent from the NPC scene of Character Creator and Outfit Studio.
//
// A load goes: validate (sns::gltf, worker thread) -> stage the exact validated bytes in a private
// temporary folder -> hand that folder's file to RuntimeLoader -> the viewport reports the loader
// status and the rendered bounds back. RuntimeLoader "does not sandbox or validate asset contents"
// (Qt documentation), so nothing reaches it without passing the validation first. Every load has
// an operation number; results and viewport reports of an older operation are ignored.
class ForgeSceneViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by CharacterForgeViewModel")
  // empty, validating, loading, loaded, error, cancelled, unsupported
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)  // what RuntimeLoader must load ("" = nothing)
  Q_PROPERTY(QString fileName READ file_name NOTIFY changed)
  Q_PROPERTY(QString filePath READ file_path NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)   // short, for the viewport
  Q_PROPERTY(QString details READ details NOTIFY changed)   // full technical report (advanced inspector)
  Q_PROPERTY(QVariantMap inspection READ inspection NOTIFY changed)
  Q_PROPERTY(QVariantMap rendered READ rendered NOTIFY changed)  // bounds reported by the viewport
  Q_PROPERTY(bool hasModel READ has_model NOTIFY changed)        // loaded and drawn with non-empty bounds
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(int operation READ operation NOTIFY changed)

 public:
  explicit ForgeSceneViewModel(QObject* parent = nullptr);
  ~ForgeSceneViewModel() override;

  [[nodiscard]] QString state() const { return state_; }
  [[nodiscard]] QUrl source() const { return source_; }
  [[nodiscard]] QString file_name() const;
  [[nodiscard]] QString file_path() const { return path_; }
  [[nodiscard]] QString message() const { return message_; }
  [[nodiscard]] QString details() const { return details_; }
  [[nodiscard]] QVariantMap inspection() const { return inspection_; }
  [[nodiscard]] QVariantMap rendered() const { return rendered_; }
  [[nodiscard]] bool has_model() const { return state_ == QStringLiteral("loaded") && !rendered_.isEmpty(); }
  [[nodiscard]] bool busy() const { return state_ == QStringLiteral("validating") || state_ == QStringLiteral("loading"); }
  [[nodiscard]] int operation() const { return operation_; }

  // Formats the preview can validate and render (RuntimeLoader also reads .obj, but OBJ is not
  // validated here: it goes through the Blender bridge like FBX and BLEND).
  [[nodiscard]] static bool previewable(const QString& path);

  Q_INVOKABLE void load(const QString& path_or_url);
  Q_INVOKABLE void reload();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void reset();
  // Called by the viewport for the RuntimeLoader showing `loaded_source`.
  // status: 0 Empty, 1 Success, 2 Error (RuntimeLoader.Status).
  Q_INVOKABLE void reportLoaderStatus(const QUrl& loaded_source, int status, const QString& error_string);
  Q_INVOKABLE void reportBounds(const QUrl& loaded_source, const QVector3D& minimum, const QVector3D& maximum);

 signals:
  void changed();
  void sourceChanged();
  void frameRequested();  // new geometry: the viewport frames the camera on it

 private:
  struct Result;
  void finish(const std::shared_ptr<Result>& result);
  void set_state(const QString& state, const QString& message, const QString& details = {});
  void clear_source();
  void remove_staging(const QString& folder);

  QThreadPool pool_;
  int operation_ = 0;
  std::shared_ptr<std::atomic<bool>> cancel_;
  QString state_ = QStringLiteral("empty");
  QString path_;
  QUrl source_;
  QString staging_;  // folder of the staged copy currently shown
  QString message_;
  QString details_;
  QVariantMap inspection_;
  QVariantMap rendered_;
  QString root_;  // per-process staging root
};

}  // namespace sns::app
