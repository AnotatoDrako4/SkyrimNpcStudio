#pragma once

#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <QtQuick3D/private/qquick3dmodel_p.h>  // QQuick3DBounds3
#include <QtQuick3D/private/qquick3dnode_p.h>

namespace sns::app {

// Loads a model like Qt Quick 3D's RuntimeLoader (Qt 6.11 source: QQuick3DRuntimeLoader::loadSource), but the expensive
// half - QSSGAssetImportManager::importFile, which parses the file and builds every mesh - runs on a worker thread.
// Only QSSGRuntimeUtils::createScene (the Qt Quick 3D objects) runs on the GUI thread. RuntimeLoader does both there:
// a 172-part, 85 MB GLB froze the window for 35.8 s.
// Same properties and status values as RuntimeLoader (Empty 0, Success 1, Error 2), plus Loading 3. Differences on
// purpose: a failed import keeps `source` (RuntimeLoader clears it before reporting, so the error could not be matched
// to the file), and the registered mesh data is released when the loader is destroyed (RuntimeLoader only releases it
// on the next load). A source changed during an import does not stop that import; its result is dropped.
class ForgeModelLoader : public QQuick3DNode {
  Q_OBJECT
  QML_NAMED_ELEMENT(ForgeModelLoader)
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(Status status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
  Q_PROPERTY(QQuick3DBounds3 bounds READ bounds NOTIFY boundsChanged)
  Q_PROPERTY(double importMs READ import_ms NOTIFY statusChanged)    // worker thread: importFile
  Q_PROPERTY(double uiBlockMs READ ui_block_ms NOTIFY statusChanged)  // GUI thread: createScene + release

 public:
  enum class Status { Empty, Success, Error, Loading };
  Q_ENUM(Status)

  explicit ForgeModelLoader(QQuick3DNode* parent = nullptr);
  ~ForgeModelLoader() override;

  QUrl source() const { return source_; }
  void setSource(const QUrl& source);
  Status status() const { return status_; }
  QString errorString() const { return error_; }
  const QQuick3DBounds3& bounds() const;
  double import_ms() const { return import_ms_; }
  double ui_block_ms() const { return ui_block_ms_; }
  void componentComplete() override;

 signals:
  void sourceChanged();
  void statusChanged();
  void boundsChanged();

 protected:
  QSSGRenderGraphObject* updateSpatialNode(QSSGRenderGraphObject* node) override;

 private:
  struct Imported;
  void start();
  void finish(int serial, const std::shared_ptr<Imported>& result);
  void release();
  void calculate_bounds();

  QUrl source_;
  Status status_ = Status::Empty;
  QString error_;
  QPointer<QQuick3DNode> root_;
  QPointer<QQuick3DNode> imported_;
  QString asset_id_;  // mesh data registered by createScene, released by release()
  int serial_ = 0;
  bool complete_ = false;
  bool bounds_dirty_ = false;
  QQuick3DBounds3 bounds_;
  double import_ms_ = 0;
  double ui_block_ms_ = 0;
};

}  // namespace sns::app
