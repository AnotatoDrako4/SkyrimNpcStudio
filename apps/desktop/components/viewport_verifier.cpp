#include "viewport_verifier.hpp"
#include "viewmodels/scene_viewmodel.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <memory>
#include <optional>

namespace sns::app {
namespace {
// Native Qt verification: sample only the View3D, never the surrounding editor chrome.
QImage grab_view(QQuickWindow* window, QQuickItem* view, QImage* full_out = nullptr) {
  const auto full = window->grabWindow();
  const auto origin = view->mapToScene(QPointF{});
  const auto dpr = full.devicePixelRatio();
  if (full_out) *full_out = full;
  return full.copy(QRect(qRound(origin.x()*dpr), qRound(origin.y()*dpr), qRound(view->width()*dpr), qRound(view->height()*dpr)));
}
struct Coverage {
  std::size_t pixels = 0;  // differs from the clear colour
  std::size_t white = 0;   // near-white (all channels >= 235): untextured or failed materials
};
Coverage coverage(const QImage& image, int step) {
  Coverage c;
  if (image.isNull()) return c;
  // The clear colour reaches the image after colour conversion (#191b20 is stored as 22,24,30),
  // so it is read from the corner instead of assumed; a small tolerance keeps near-black armour
  // counted as covered.
  const auto bg = image.pixelColor(1, 1);
  // The camera controls and status strip overlap the View3D in the window grab.
  // Sample its central area so those controls cannot make an empty model pass.
  for (int y = image.height() / 10; y < image.height() * 9 / 10; y += step)
    for (int x = image.width() / 10; x < image.width() * 9 / 10; x += step) {
      const auto px = image.pixelColor(x, y);
      if (std::abs(px.red()-bg.red()) + std::abs(px.green()-bg.green()) + std::abs(px.blue()-bg.blue()) > 6) ++c.pixels;
      if (px.red() >= 235 && px.green() >= 235 && px.blue() >= 235) ++c.white;
    }
  return c;
}

struct Run : std::enable_shared_from_this<Run> {
  QPointer<QQuickWindow> window;
  QPointer<SceneViewModel> scene;
  QPointer<QObject> controls;
  QPointer<QQuickItem> view;
  QString path;
  std::function<void(bool)> completed;
  QImage previous;
  Coverage previous_coverage;
  int step = 0;
  bool passed = true;
  void diagnostic(const char* mode) { QMetaObject::invokeMethod(controls, "setDiagnostic", Q_ARG(QVariant, QString::fromLatin1(mode))); }
  void next() {
    if (!window || !scene || !controls || !view) { completed(false); return; }
    QImage full;
    const auto image = grab_view(window, view, &full);
    const auto c = coverage(image, 2);
    static const char* names[] = {"front", "side", "hidden", "uv", "alpha", "normals", "wireframe", "compact"};
    const bool changed = previous.isNull() || image != previous;
    // The empty View3D still draws its background and overlay. Compare coverage with
    // the preceding visible view and check the scene state instead of requiring zero pixels.
    bool no_shown_parts = true;
    if (step == 2) {
      const auto visibility = scene->visibility();
      for (const auto& part : scene->parts())
        no_shown_parts &= !visibility.value(part.toMap().value("id").toString()).toBool();
    }
    const bool nonblank = step == 2 ? no_shown_parts && c.pixels < previous_coverage.pixels : c.pixels > 300;
    passed &= changed && nonblank;
    std::fprintf(stderr, "viewport: %s size=%dx%d pixels=%zu white=%zu changed=%d valid=%d\n",
      names[step], image.width(), image.height(), c.pixels, c.white, changed, nonblank);
    QVariant state;
    QMetaObject::invokeMethod(controls, "viewState", Q_RETURN_ARG(QVariant, state));
    std::fprintf(stderr, "viewport:   state %s\n", qPrintable(state.toString()));
    if (!path.isEmpty()) {
      const QFileInfo file(path);
      passed &= full.save(file.path()+"/"+file.completeBaseName()+"-"+names[step]+".png");
    }
    previous = image;
    previous_coverage = c;
    if (step == 0) {
      QVariant hit;
      if (window->isExposed()) {
        passed &= QMetaObject::invokeMethod(controls, "pickCharacter", Q_RETURN_ARG(QVariant, hit));
        passed &= !hit.toString().isEmpty() && scene->selected() == hit.toString();
      } else {
        // A never-exposed window renders only through grabWindow(), whose separate render
        // context is not the one View3D.pick() queries: picking cannot be tested here.
        std::fprintf(stderr, "viewport: pick SKIPPED (window not exposed; run smoke-test.ps1 -ShowWindow)\n");
        if (!scene->parts().isEmpty()) scene->select(scene->parts().front().toMap().value("id").toString());
      }
      const auto inspected = scene->selectedPart();
      const bool inspector = !inspected.value("nif").toString().isEmpty() && !inspected.value("alphaMode").toString().isEmpty();
      passed &= inspector;
      std::fprintf(stderr, "viewport: picked=%s inspector=%d shape=%s alpha=%s\n", qPrintable(hit.toString()), inspector,
        qPrintable(inspected.value("shape").toString()), qPrintable(inspected.value("alphaMode").toString()));
      QMetaObject::invokeMethod(controls, "applyCameraPreset", Q_ARG(QVariant, 2));
    } else if (step == 1) {
      for (const auto& p : scene->parts()) scene->setVisible(p.toMap().value("id").toString(), false);
    } else if (step == 2) {
      scene->restoreVisibility();
      controls->setProperty("uvMode", true);
      QMetaObject::invokeMethod(controls, "frame", Q_ARG(QVariant, QVariant::fromValue(QVector3D{})), Q_ARG(QVariant, 90), Q_ARG(QVariant, 0));
    } else if (step == 3) {
      controls->setProperty("uvMode", false);
      scene->select("");
      QMetaObject::invokeMethod(controls, "reset");
      diagnostic("alpha");
    } else if (step == 4) {
      diagnostic("normals");
    } else if (step == 5) {
      diagnostic("wireframe");
    } else if (step == 6) {
      QMetaObject::invokeMethod(controls, "restore");
      const bool restored = controls->property("diagnostic").toString() == "none" && controls->property("isolated").toString().isEmpty();
      std::size_t hidden_by_default = 0, wrong = 0;
      const auto visibility = scene->visibility();
      for (const auto& p : scene->parts()) {
        const auto row = p.toMap();
        hidden_by_default += !row.value("shownByDefault").toBool();
        wrong += visibility.value(row.value("id").toString()).toBool() != row.value("shownByDefault").toBool();
      }
      passed &= restored && wrong == 0;
      std::fprintf(stderr, "viewport: restore=%d hiddenByDefault=%zu wrongVisibility=%zu\n", restored, hidden_by_default, wrong);
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
    QTimer::singleShot(900, window, [self=shared_from_this()] { self->next(); });
  }
};

struct CaptureRun : std::enable_shared_from_this<CaptureRun> {
  enum class Show { Face, Head, All };
  struct Scenario {
    const char* name;
    std::optional<QStringList> armor;  // nullopt = the NPC's own outfit
    Show show;
    bool face_camera;
  };
  QPointer<QQuickWindow> window;
  QPointer<SceneViewModel> scene;
  QPointer<QObject> controls;
  QPointer<QQuickItem> view;
  QDir dir;
  std::function<void(bool)> completed;
  std::vector<Scenario> scenarios;
  QStringList report;
  std::size_t index = 0;
  bool passed = true;

  void start() {
    if (index >= scenarios.size()) { finish(); return; }
    auto self = shared_from_this();
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(scene, &SceneViewModel::ready, window, [self, connection] {
      QObject::disconnect(*connection);
      QTimer::singleShot(2500, self->window, [self] { self->arrange(); });
    });
    const auto& s = scenarios[index];
    scene->setPreviewArmor(s.armor ? QVariant(*s.armor) : QVariant());
  }
  void arrange() {
    if (!window || !scene || !controls || !view) { completed(false); return; }
    const auto& s = scenarios[index];
    static const QRegularExpression hair("hair|scalp", QRegularExpression::CaseInsensitiveOption);
    for (const auto& p : scene->parts()) {
      const auto row = p.toMap();
      const bool by_default = row.value("shownByDefault", true).toBool();
      const bool head = row.value("group").toString() == "head";
      const bool visible = by_default && (s.show == Show::All || (head && (s.show == Show::Head || !row.value("label").toString().contains(hair))));
      scene->setVisible(row.value("id").toString(), visible);
    }
    scene->select("");
    controls->setProperty("uvMode", false);
    // Frame the actual loaded scene: head heights and overall bounds vary by race and outfit.
    passed &= QMetaObject::invokeMethod(controls, "applyCameraPreset", Q_ARG(QVariant, s.face_camera ? 4 : 0));
    QTimer::singleShot(1200, window, [self = shared_from_this()] { self->capture(); });
  }
  void capture() {
    if (!window || !scene || !view) { completed(false); return; }
    const auto& s = scenarios[index];
    QImage full;
    const auto image = grab_view(window, view, &full);
    const auto c = coverage(image, 1);
    int shown = 0;
    QStringList pieces;
    // Builds that keep visibility inside the rows have no "visibility" property.
    const auto visibility = scene->property("visibility").toMap();
    for (const auto& p : scene->parts()) {
      const auto row = p.toMap();
      const auto id = row.value("id").toString();
      if (!(visibility.isEmpty() ? row.value("visible").toBool() : visibility.value(id).toBool())) continue;
      ++shown;
      const auto piece = row.value("label").toString().section(" / ", 0, 0);
      if (!pieces.contains(piece)) pieces << piece;
    }
    const double white_pct = c.pixels ? 100.0 * static_cast<double>(c.white) / static_cast<double>(c.pixels) : 0.0;
    const auto line = QStringLiteral("capture: %1 shapes=%2 pixels=%3 white=%4 (%5% of covered) pieces=[%6]")
                          .arg(s.name).arg(shown).arg(c.pixels).arg(c.white).arg(white_pct, 0, 'f', 2).arg(pieces.join("; "));
    std::fprintf(stderr, "%s\n", qPrintable(line));
    QVariant state;
    QMetaObject::invokeMethod(controls, "viewState", Q_RETURN_ARG(QVariant, state));
    std::fprintf(stderr, "capture:   state %s center=(%g,%g,%g) radius=%g\n", qPrintable(state.toString()),
                 scene->center().x(), scene->center().y(), scene->center().z(), scene->radius());
    report << line;
    passed &= shown > 0 && c.pixels > 300;
    passed &= image.save(dir.filePath(QString::fromLatin1(s.name) + ".png"));
    passed &= full.save(dir.filePath(QString::fromLatin1(s.name) + "-window.png"));
    ++index;
    start();
  }
  void finish() {
    QFile file(dir.filePath("captures.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&file);
      for (const auto& line : report) out << line << "\n";
    } else {
      passed = false;
    }
    scene->setPreviewArmor(QVariant());
    QTimer::singleShot(500, window, [self = shared_from_this()] { self->completed(self->passed); });
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
void capture_set(QQuickWindow* window, SceneViewModel* scene, const QString& directory, std::function<void(bool)> completed) {
  auto run = std::make_shared<CaptureRun>();
  run->window = window; run->scene = scene; run->completed = std::move(completed);
  run->controls = window->findChild<QObject*>("characterViewport");
  run->view = window->findChild<QQuickItem*>("characterView3D");
  QDir().mkpath(directory);
  run->dir = QDir(directory);
  if (!run->controls || !run->view) { run->completed(false); return; }
  window->resize(1600, 960);
  // Vanilla Steel Armor, Boots and Gauntlets (no helmet, so the face stays visible).
  const QStringList steel{"013952:Skyrim.esm", "013951:Skyrim.esm", "013953:Skyrim.esm"};
  using S = CaptureRun::Show;
  run->scenarios = {{"1-face", QStringList{}, S::Face, true},
                    {"2-hair", QStringList{}, S::Head, true},
                    {"3-body", QStringList{}, S::All, false},
                    {"4-armor", steel, S::All, false},
                    {"5-outfit", std::nullopt, S::All, false}};
  run->start();
}
}
