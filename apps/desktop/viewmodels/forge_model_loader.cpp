#include "forge_model_loader.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QtQuick3DAssetImport/private/qssgassetimportmanager_p.h>
#include <QtQuick3DAssetUtils/private/qssgrtutilities_p.h>
#include <QtQuick3DAssetUtils/private/qssgscenedesc_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrenderbuffermanager_p.h>
#include <memory>

namespace sns::app {

namespace {
// One import at a time: the importer is not shared between threads, and two large imports would double the memory.
// Not owned by a loader, so destroying a viewport never waits for an import in flight.
QThreadPool& import_pool() {
  static QThreadPool* pool = [] {
    auto* p = new QThreadPool(QCoreApplication::instance());
    p->setMaxThreadCount(1);
    return p;
  }();
  return *pool;
}

// As QQuick3DRuntimeLoader's boxBoundsRecursive (Qt 6.11): every Model's local box, mapped to `base`.
void bounds_recursive(const QQuick3DNode* base, const QQuick3DNode* node, QQuick3DBounds3& acc) {
  if (!node) return;
  if (const auto* model = qobject_cast<const QQuick3DModel*>(node)) {
    const auto b = model->bounds();
    for (const QVector3D point : b.bounds.toQSSGBoxPoints()) {
      const auto p = model->mapPositionToNode(const_cast<QQuick3DNode*>(base), point);
      if (Q_UNLIKELY(acc.bounds.isEmpty())) acc.bounds = {p, p};
      else acc.bounds.include(p);
    }
  }
  for (auto* child : node->childItems()) bounds_recursive(base, qobject_cast<const QQuick3DNode*>(child), acc);
}
}  // namespace

// The worker's result. The scene description is freed with cleanup() on whichever thread drops the last reference
// (the GUI thread right after createScene, or with a dropped result).
struct ForgeModelLoader::Imported {
  std::shared_ptr<QSSGSceneDesc::Scene> scene{new QSSGSceneDesc::Scene, [](QSSGSceneDesc::Scene* s) {
                                                s->cleanup();
                                                delete s;
                                              }};
  QSSGAssetImportManager::ImportState state = QSSGAssetImportManager::ImportState::IoError;
  QString error = QStringLiteral("Unknown error");
  qint64 ms = 0;
};

ForgeModelLoader::ForgeModelLoader(QQuick3DNode* parent) : QQuick3DNode(parent) {}

ForgeModelLoader::~ForgeModelLoader() {
  ++serial_;  // an import in flight is dropped when it reports
  release();
}

void ForgeModelLoader::componentComplete() {
  QQuick3DNode::componentComplete();
  complete_ = true;
  if (!source_.isEmpty()) start();
}

void ForgeModelLoader::setSource(const QUrl& source) {
  if (source_ == source) return;
  source_ = source;
  emit sourceChanged();
  if (complete_) start();
}

void ForgeModelLoader::release() {
  delete root_.data();  // the Models, materials and textures of the shown asset
  root_.clear();
  imported_.clear();
  if (!asset_id_.isEmpty()) {
    QSSGBufferManager::unregisterMeshData(asset_id_);
    asset_id_.clear();
  }
  bounds_.bounds.setEmpty();
  bounds_dirty_ = false;
}

void ForgeModelLoader::start() {
  const int serial = ++serial_;
  QElapsedTimer held;
  held.start();
  release();  // like RuntimeLoader: the previous asset goes as soon as another one is asked for
  import_ms_ = 0;
  ui_block_ms_ = static_cast<double>(held.elapsed());
  if (!source_.isValid() || source_.isEmpty()) {
    status_ = Status::Empty;
    error_ = QStringLiteral("No file selected");
    emit statusChanged();
    return;
  }
  status_ = Status::Loading;
  error_.clear();
  emit statusChanged();
  QPointer<ForgeModelLoader> self(this);
  import_pool().start([self, serial, url = source_] {
    auto result = std::make_shared<Imported>();
    QElapsedTimer clock;
    clock.start();
    {
      QSSGAssetImportManager manager;  // its importers are created and destroyed on this thread
      result->state = manager.importFile(url, *result->scene, &result->error);
    }
    result->ms = clock.elapsed();
    // qApp as context: whether the loader still exists is checked on the GUI thread, where it is destroyed.
    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, serial, result] {
      if (self) self->finish(serial, result);
    }, Qt::QueuedConnection);
  });
}

void ForgeModelLoader::finish(int serial, const std::shared_ptr<Imported>& result) {
  if (serial != serial_) return;  // another source was asked for meanwhile
  import_ms_ = static_cast<double>(result->ms);
  QElapsedTimer held;
  held.start();
  using State = QSSGAssetImportManager::ImportState;
  if (result->state == State::Success && result->scene->root) {
    // A dummy root, as in RuntimeLoader: the first-level nodes and resources go when the next asset is loaded.
    root_ = new QQuick3DNode(this);
    imported_ = QSSGRuntimeUtils::createScene(*root_, *result->scene);
    asset_id_ = result->scene->id;
    bounds_dirty_ = true;
    status_ = Status::Success;
    error_ = QStringLiteral("Success!");
  } else {
    status_ = Status::Error;
    error_ = (result->state == State::Unsupported ? QStringLiteral("Unsupported: ") : QStringLiteral("IO Error: ")) + result->error;
  }
  result->scene.reset();  // scene description freed here, after createScene copied what it needs
  ui_block_ms_ += static_cast<double>(held.elapsed());
  emit statusChanged();
  update();  // updateSpatialNode announces the bounds once the new Models are in the scene
}

const QQuick3DBounds3& ForgeModelLoader::bounds() const {
  if (bounds_dirty_) const_cast<ForgeModelLoader*>(this)->calculate_bounds();
  return bounds_;
}

void ForgeModelLoader::calculate_bounds() {
  if (!imported_ || !bounds_dirty_) return;
  bounds_.bounds.setEmpty();
  bounds_recursive(imported_, imported_, bounds_);
  bounds_dirty_ = false;
}

QSSGRenderGraphObject* ForgeModelLoader::updateSpatialNode(QSSGRenderGraphObject* node) {
  auto* result = QQuick3DNode::updateSpatialNode(node);
  if (bounds_dirty_) QMetaObject::invokeMethod(this, &ForgeModelLoader::boundsChanged, Qt::QueuedConnection);
  return result;
}

}  // namespace sns::app
