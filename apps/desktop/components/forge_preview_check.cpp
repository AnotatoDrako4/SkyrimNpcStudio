#include "forge_preview_check.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "sns/gltf/gltf_inspect.hpp"
#include "viewmodels/studio_controller.hpp"

namespace sns::app {
namespace {

struct Check : std::enable_shared_from_this<Check> {
  QPointer<QQuickWindow> window;
  QPointer<StudioController> studio;
  QStringList files;
  QString dir;
  std::function<void(bool)> completed;
  QElapsedTimer clock;
  int index = -1;
  bool passed = true;
  bool done = false;

  ForgeSceneViewModel* scene() const { return studio->forge()->scene(); }

  void log(const QString& text) const {
    std::fprintf(stderr, "forge: [%6lld ms] %s\n", clock.elapsed(), qPrintable(text));
    std::fflush(stderr);
  }
  void check(bool ok, const QString& what) {
    log(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what));
    passed &= ok;
  }
  void finish() {
    if (done) return;
    done = true;
    log(QStringLiteral("summary: %1").arg(passed ? "PASSED" : "FAILED"));
    completed(passed);
  }
  void after(int ms, std::function<void()> next) {
    QTimer::singleShot(ms, studio, [self = shared_from_this(), next = std::move(next)] {
      if (!self->done && self->studio && self->window) next();
    });
  }

  QQuickItem* find_text(const QString& text) const {
    for (auto* item : window->findChildren<QQuickItem*>()) {
      if (item->isVisible() && item->property("text").toString() == text) return item;
    }
    return nullptr;
  }
  // A left click delivered to the window like a real one (press + release at a scene position).
  void click_at(QPointF pos) {
    const auto global = window->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
  }
  bool click(const QString& text) {
    auto* item = find_text(text);
    if (!item) return false;
    click_at(item->mapToScene(QPointF(item->width() / 2, item->height() / 2)));
    return true;
  }
  QQuickItem* view() const {
    for (auto* item : window->findChildren<QQuickItem*>("forgeView3D")) {
      if (item->isVisible() && item->width() > 50) return item;
    }
    return nullptr;
  }
  // Share of the View3D's pixels that differ from its clear colour (sampled in a corner).
  double coverage(const QImage& full, QQuickItem* v) const {
    const auto origin = v->mapToScene(QPointF{});
    const auto dpr = full.devicePixelRatio();
    const auto image = full.copy(QRect(qRound(origin.x() * dpr), qRound(origin.y() * dpr), qRound(v->width() * dpr),
                                       qRound(v->height() * dpr)));
    if (image.isNull()) return 0;
    const auto bg = image.pixelColor(2, 2);
    std::size_t covered = 0, total = 0;
    for (int y = 0; y < image.height(); y += 2) {
      for (int x = 0; x < image.width(); x += 2) {
        const auto px = image.pixelColor(x, y);
        covered += std::abs(px.red() - bg.red()) + std::abs(px.green() - bg.green()) + std::abs(px.blue() - bg.blue()) > 12;
        ++total;
      }
    }
    return total ? static_cast<double>(covered) / static_cast<double>(total) : 0;
  }

  void start() {
    log("closing the MO2 dialog with \"Continuar en Character Forge sin perfil\"");
    check(click(QString::fromUtf8("Continuar en Character Forge sin perfil")), "the welcome dialog offers Character Forge without a profile");
    after(600, [self = shared_from_this()] {
      auto* tabs = self->window->findChild<QObject*>("workspaceTabs");
      self->check(tabs && tabs->property("currentIndex").toInt() == 2, "Character Forge workspace shown");
      self->check(self->studio->forge()->newProject(QStringLiteral("ForgePreviewCheck")), "new .sncf project");
      self->check(self->view() != nullptr, "forge View3D visible");
      self->next();
    });
  }

  void next() {
    if (++index >= files.size()) {
      const QDir staging(QDir::temp().filePath(QStringLiteral("sns-forge-preview-%1").arg(QCoreApplication::applicationPid())));
      const auto folders = staging.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      check(folders.size() <= 1, QStringLiteral("only the shown model stays staged (%1 folder(s))").arg(folders.size()));
      finish();
      return;
    }
    const auto file = QFileInfo(files[index]).absoluteFilePath();
    const bool expected = gltf::load_and_inspect(std::filesystem::path(file.toStdWString())).inspection.loadable();
    log(QStringLiteral("=== %1/%2 %3 (validator: %4)").arg(index + 1).arg(files.size()).arg(file, expected ? "loadable" : "rejected"));
    if (expected) {
      studio->forge()->selectModelFile(file);
      check(studio->forge()->last_error().isEmpty(), "importer accepted the file " + studio->forge()->last_error());
    } else {
      scene()->load(file);
    }
    wait(expected, 0);
  }

  void wait(bool expected, int waited) {
    const auto state = scene()->state();
    const bool settled = (state == "loaded" && scene()->has_model()) || state == "error" || state == "cancelled" || state == "unsupported";
    if (!settled && waited < 60000) {
      after(100, [self = shared_from_this(), expected, waited] { self->wait(expected, waited + 100); });
      return;
    }
    // Two more frames after framing the camera.
    after(800, [self = shared_from_this(), expected] { self->evaluate(expected); });
  }

  void evaluate(bool expected) {
    auto* v = view();
    const auto full = window->grabWindow();
    const double share = v ? coverage(full, v) : 0;
    const auto name = QFileInfo(files[index]).completeBaseName();
    if (!dir.isEmpty()) full.save(QDir(dir).filePath(QStringLiteral("forge-%1-%2.png").arg(index + 1).arg(name)));
    const auto inspection = scene()->inspection();
    const auto validated = inspection.value("validated").toMap();
    const auto rendered = scene()->rendered();
    log(QStringLiteral("state=%1 message='%2' vertices=%3 triangles=%4 indicesChecked=%5 pixels=%6%")
            .arg(scene()->state(), scene()->message()).arg(validated.value("vertices").toLongLong())
            .arg(validated.value("triangles").toLongLong()).arg(validated.value("indicesChecked").toBool())
            .arg(share * 100, 0, 'f', 1));
    if (expected) {
      const auto size = rendered.value("size").value<QVector3D>();
      log(QStringLiteral("rendered bounds size (%1, %2, %3)").arg(size.x()).arg(size.y()).arg(size.z()));
      check(scene()->state() == "loaded" && scene()->has_model(), "RuntimeLoader loaded the staged model and reported bounds");
      check(share > 0.02, QStringLiteral("model pixels in the View3D (%1%)").arg(share * 100, 0, 'f', 1));
      check(!scene()->source().isEmpty(), "viewport shows the staged copy " + scene()->source().toLocalFile());
      if (scene()->has_model()) {
        parts_and_selection();
        return;
      }
    } else {
      check(scene()->state() == "error" && !scene()->message().isEmpty(), "rejected with a visible message: " + scene()->message());
      check(scene()->source().isEmpty() && !scene()->has_model(), "nothing handed to RuntimeLoader");
    }
    next();
  }

  QString part_label(int i) const {
    const auto parts = scene()->parts();
    return i >= 0 && i < parts.size() ? parts[i].toMap().value("label").toString() : QStringLiteral("(none)");
  }

  // Parts reported by the viewport, a click on the model and on empty space, hiding and showing.
  void parts_and_selection() {
    const auto parts = scene()->parts();
    const auto nodes = scene()->inspection().value("meshNodes").toList();
    QStringList labels;
    for (const auto& p : parts) labels << p.toMap().value("label").toString();
    log(QStringLiteral("parts: %1 rendered, %2 glTF mesh node(s), match '%3': %4")
            .arg(parts.size()).arg(nodes.size()).arg(scene()->part_match(), labels.join(", ")));
    check(!parts.isEmpty(), "the viewport reported the Models RuntimeLoader created");
    check(parts.size() == nodes.size() && scene()->part_match() == "order",
          "every part matched to its glTF mesh node by scene order (materials, skin, morph targets, hierarchy)");
    auto* v = view();
    click_at(v->mapToScene(QPointF(v->width() / 2, v->height() / 2)));
    after(400, [self = shared_from_this()] {
      auto* v = self->view();
      const int hit = self->scene()->selected_part();
      self->check(hit >= 0, "a click on the model centre selects the part under the pointer: " + self->part_label(hit));
      auto* box = self->window->findChild<QObject*>("forgeSelectionBox");
      self->check(box && box->property("visible").toBool(), "selection box shown around the selected part");
      self->click_at(v->mapToScene(QPointF(4, 4)));
      self->after(400, [self] {
        self->check(self->scene()->selected_part() == -1, "a click on empty space clears the selection");
        self->hide_and_show();
      });
    });
  }

  void hide_and_show() {
    const auto parts = scene()->parts();
    // A part with a parent part: hiding the parent must mark it hidden by that parent.
    int child = -1;
    for (int i = 0; i < parts.size() && child < 0; ++i) {
      if (parts[i].toMap().value("parent").toInt() >= 0) child = i;
    }
    if (child >= 0) {
      const int parent = parts[child].toMap().value("parent").toInt();
      scene()->setPartVisible(parent, false);
      const auto c = scene()->parts()[child].toMap();
      check(c.value("visible").toBool() && !c.value("shown").toBool() && c.value("hiddenBy").toString() == part_label(parent),
            QStringLiteral("hiding %1 also hides its child %2 (hiddenBy '%3')").arg(part_label(parent), part_label(child), c.value("hiddenBy").toString()));
      scene()->setPartVisible(parent, true);
    }
    for (int i = 0; i < parts.size(); ++i) scene()->setPartVisible(i, false);
    after(500, [self = shared_from_this()] {
      const double hidden = self->coverage(self->window->grabWindow(), self->view());
      self->check(hidden < 0.005, QStringLiteral("all parts hidden: the View3D shows only its background (%1%)").arg(hidden * 100, 0, 'f', 2));
      self->scene()->showAllParts();
      self->after(500, [self] {
        const double shown = self->coverage(self->window->grabWindow(), self->view());
        self->check(shown > 0.02, QStringLiteral("all parts shown again (%1%)").arg(shown * 100, 0, 'f', 1));
        self->next();
      });
    });
  }
};

}  // namespace

void run_forge_preview_check(QQuickWindow* window, StudioController* studio, const QStringList& files,
                             const QString& capture_dir, std::function<void(bool)> completed) {
  auto check = std::make_shared<Check>();
  check->window = window;
  check->studio = studio;
  check->files = files;
  check->dir = capture_dir;
  check->completed = std::move(completed);
  check->clock.start();
  if (!capture_dir.isEmpty()) QDir().mkpath(capture_dir);
  check->start();
}

}  // namespace sns::app
