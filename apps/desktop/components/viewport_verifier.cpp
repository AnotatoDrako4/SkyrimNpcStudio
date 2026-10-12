#include "viewport_verifier.hpp"
#include "viewmodels/scene_viewmodel.hpp"
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <memory>

namespace sns::app {
namespace {
// Native Qt verification: sample only the View3D, never the surrounding editor chrome.
struct Run : std::enable_shared_from_this<Run> {
  QPointer<QQuickWindow> window;
  QPointer<SceneViewModel> scene;
  QPointer<QObject> controls;
  QPointer<QQuickItem> view;
  QString path;
  std::function<void(bool)> completed;
  QImage previous;
  int step = 0;
  bool passed = true;
  void next() {
    if (!window || !scene || !controls || !view) { completed(false); return; }
    const auto full = window->grabWindow();
    const auto origin = view->mapToScene(QPointF{});
    const auto dpr = full.devicePixelRatio();
    const auto image = full.copy(QRect(qRound(origin.x()*dpr), qRound(origin.y()*dpr),
      qRound(view->width()*dpr), qRound(view->height()*dpr)));
    std::size_t pixels = 0;
    for (int y = 0; y < image.height(); y += 2) for (int x = 0; x < image.width(); x += 2) {
      const auto c = image.pixelColor(x, y);
      if (std::abs(c.red()-25) + std::abs(c.green()-27) + std::abs(c.blue()-32) > 30) ++pixels;
    }
    static const char* names[] = {"front", "side", "hidden", "uv", "compact"};
    const bool changed = previous.isNull() || image != previous;
    const bool nonblank = step == 2 ? pixels < 50 : pixels > 300;
    passed &= changed && nonblank;
    std::fprintf(stderr, "viewport: %s size=%dx%d pixels=%zu changed=%d valid=%d\n",
      names[step], image.width(), image.height(), pixels, changed, nonblank);
    if (!path.isEmpty()) {
      const QFileInfo file(path);
      passed &= full.save(file.path()+"/"+file.completeBaseName()+"-"+names[step]+".png");
    }
    previous = image;
    if (step == 0) {
      QVariant hit;
      passed &= QMetaObject::invokeMethod(controls, "pickCharacter", Q_RETURN_ARG(QVariant, hit));
      passed &= !hit.toString().isEmpty() && scene->selected() == hit.toString();
      std::fprintf(stderr, "viewport: picked=%s\n", qPrintable(hit.toString()));
      QMetaObject::invokeMethod(controls, "applyCameraPreset", Q_ARG(QVariant, 2));
    } else if (step == 1) {
      for (const auto& p : scene->parts()) scene->setVisible(p.toMap().value("id").toString(), false);
    } else if (step == 2) {
      for (const auto& p : scene->parts()) scene->setVisible(p.toMap().value("id").toString(), true);
      controls->setProperty("uvMode", true);
      QMetaObject::invokeMethod(controls, "frame", Q_ARG(QVariant, QVariant::fromValue(QVector3D{})), Q_ARG(QVariant, 90), Q_ARG(QVariant, 0));
    } else if (step == 3) {
      window->resize(1100, 700);
      QMetaObject::invokeMethod(controls, "applyCameraPreset", Q_ARG(QVariant, 0));
    } else {
      window->resize(1600, 960);
      QMetaObject::invokeMethod(controls, "reset");
      scene->select("");
      QTimer::singleShot(500, window, [self=shared_from_this()] { self->completed(self->passed); });
      return;
    }
    ++step;
    QTimer::singleShot(700, window, [self=shared_from_this()] { self->next(); });
  }
};
}
void verify_viewport(QQuickWindow* window, SceneViewModel* scene, const QString& screenshot, std::function<void(bool)> completed) {
  auto run = std::make_shared<Run>();
  run->window = window; run->scene = scene; run->path = screenshot; run->completed = std::move(completed);
  run->controls = window->findChild<QObject*>("characterViewport");
  run->view = window->findChild<QQuickItem*>("characterView3D");
  run->next();
}
}
