#include "settings_check.hpp"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

#include "viewmodels/settings_service.hpp"
#include "viewmodels/workbench_state.hpp"

namespace sns::app {
namespace {

constexpr int kStepMs = 500;  // bindings, layout and a frame between steps

struct Check : std::enable_shared_from_this<Check> {
  QPointer<QQuickWindow> window;
  QPointer<SettingsService> settings;
  QPointer<WorkbenchState> wb;
  QString dir;
  QString expect_file;
  std::function<void(bool)> completed;
  QElapsedTimer clock;
  std::vector<std::function<void()>> steps;
  size_t next = 0;
  bool passed = true;
  bool done = false;
  int shot = 0;
  int font_before = 0;

  void log(const QString& text) const {
    std::fprintf(stderr, "settings: [%6lld ms] %s\n", clock.elapsed(), qPrintable(text));
    std::fflush(stderr);
  }
  void check(bool ok, const QString& what) {
    log(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what));
    passed &= ok;
  }
  void finish() {
    if (done) return;
    done = true;
    if (settings) settings->flush();
    if (wb) wb->flush();
    log(QStringLiteral("summary: %1").arg(passed ? "PASSED" : "FAILED"));
    completed(passed);
  }
  void run_next() {
    if (done) return;
    if (next >= steps.size()) { finish(); return; }
    QTimer::singleShot(kStepMs, window, [self = shared_from_this()] {
      if (self->done || !self->window) return;
      self->steps[self->next++]();
      self->run_next();
    });
  }

  // Items are searched in the item tree (the dialogs live in the window's overlay, delegates under their views).
  static QQuickItem* find_in(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name && root->isVisible()) return root;
    for (auto* child : root->childItems())
      if (auto* hit = find_in(child, name)) return hit;
    return nullptr;
  }
  QQuickItem* item(const QString& name) const { return find_in(window->contentItem(), name); }
  QObject* center() const { return window->findChild<QObject*>(QStringLiteral("settingsCenter")); }
  bool center_open() const { auto* c = center(); return c && c->property("opened").toBool(); }
  QColor window_color() const { return window->property("color").value<QColor>(); }
  int font_px() const { return window->property("font").value<QFont>().pixelSize(); }
  QColor viewer_clear_color() const {
    auto* split = item(QStringLiteral("creatorSplit"));
    auto* view = split ? find_in(split, QStringLiteral("characterView3D")) : item(QStringLiteral("characterView3D"));
    auto* env = view ? view->property("environment").value<QObject*>() : nullptr;
    return env ? env->property("clearColor").value<QColor>() : QColor();
  }
  void capture(const QString& what) {
    const auto path = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(++shot, 2, 10, QLatin1Char('0')).arg(what));
    log(QStringLiteral("capture %1: %2").arg(window->grabWindow().save(path) ? "saved" : "FAILED", path));
  }
  void click(QQuickItem* target) {
    const auto p = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p);
  }
  QVariant invoke(const char* method, const QVariant& a = {}, const QVariant& b = {}) {
    QVariant result;
    auto* c = center();
    if (!c) return result;
    if (!b.isValid() && !a.isValid()) QMetaObject::invokeMethod(c, method, Q_RETURN_ARG(QVariant, result));
    else if (!b.isValid()) QMetaObject::invokeMethod(c, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a));
    else QMetaObject::invokeMethod(c, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    return result;
  }
  // A ComboBox of the Settings Center moved to an option and "activated" (its onActivated handler runs).
  bool choose(const QString& id, const QString& value) {
    auto* combo = item(QStringLiteral("control:") + id);
    if (!combo) return false;
    const auto options = settings->descriptor(id).value("options").toList();
    int index = -1;
    for (int i = 0; i < options.size(); ++i)
      if (options[i].toMap().value("value").toString() == value) index = i;
    if (index < 0) return false;
    combo->setProperty("currentIndex", index);
    return QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
  }
  // A Slider of the Settings Center moved to a value ("moved" runs its handler, as a drag or a key would).
  bool slide(const QString& id, int value) {
    auto* slider = item(QStringLiteral("control:") + id);
    if (!slider) return false;
    slider->setProperty("value", value);
    return QMetaObject::invokeMethod(slider, "moved");
  }
};

}  // namespace

void run_settings_check(QQuickWindow* window, SettingsService* settings, WorkbenchState* workbench, const QString& dir,
                        const QString& expect_file, std::function<void(bool)> completed) {
  auto c = std::make_shared<Check>();
  c->window = window;
  c->settings = settings;
  c->wb = workbench;
  c->dir = dir;
  c->expect_file = expect_file;
  c->completed = std::move(completed);
  c->clock.start();
  QDir().mkpath(dir);
  auto* s = c.get();
  if (!window || !settings || !workbench) {
    s->check(false, "window, AppSettings and Workbench available");
    s->finish();
    return;
  }
  window->requestActivate();

  // ---- after a restart: only what the first run stored must be back
  if (!expect_file.isEmpty()) {
    c->steps.push_back([s] {
      QFile f(s->expect_file);
      if (!f.open(QIODevice::ReadOnly)) { s->check(false, "expect file readable: " + s->expect_file); return; }
      const auto e = QJsonDocument::fromJson(f.readAll()).object();
      s->check(s->settings->theme() == e.value("theme").toString(), "theme came back: " + s->settings->theme());
      s->check(s->window_color() == QColor(e.value("windowColor").toString()),
               "window painted with the stored theme: " + s->window_color().name());
      s->check(s->wb->font_scale() == e.value("fontScale").toInt(), QStringLiteral("text size came back: %1 %").arg(s->wb->font_scale()));
      s->check(s->font_px() == e.value("fontPx").toInt(), QStringLiteral("window font %1 px (stored %2)").arg(s->font_px()).arg(e.value("fontPx").toInt()));
      s->check(s->settings->value("viewport.background").toString() == e.value("background").toString(), "viewer background came back");
      s->check(s->viewer_clear_color() == QColor(e.value("clearColor").toString()), "View3D clears with it: " + s->viewer_clear_color().name());
      s->check(s->settings->camera_fov() == e.value("fov").toInt(), QStringLiteral("camera FOV came back: %1").arg(s->settings->camera_fov()));
      s->check(s->wb->layout("creator")->left_width() == e.value("leftWidth").toInt(), "Character Creator left panel width came back");
      s->check(s->settings->load_issues().isEmpty(), "no invalid stored setting at start");
      s->capture("restart");
    });
    c->run_next();
    return;
  }

  // ---- first run
  c->steps.push_back([s] {
    s->check(!s->center_open(), "Settings Center closed at start");
    s->check(s->settings->theme() == "graphite" && s->window_color() == QColor("#16171a"), "original theme at start: " + s->window_color().name());
    QTest::keyClick(s->window, Qt::Key_Comma, Qt::ControlModifier);
  });
  c->steps.push_back([s] {
    s->check(s->center_open(), "Ctrl+, opens the Settings Center (SET-01)");
    s->check(s->invoke("searchFocused").toBool(), "the search has the keyboard focus (SET-01)");
    for (const char ch : std::string_view("escala")) QTest::keyClick(s->window, ch);  // real key events, one by one
  });
  c->steps.push_back([s] {
    const int n = s->invoke("searchCount").toInt();
    s->check(n > 0, QStringLiteral("typing 'escala' lists %1 setting(s) (SET-02)").arg(n));
    s->capture("search-escala");
    QStringList found;
    for (const auto& text : {QStringLiteral("tema"), QStringLiteral("retrato"), QStringLiteral("MO2"), QStringLiteral("cámara")}) {
      s->invoke("setSearch", text);
      found << QStringLiteral("%1=%2").arg(text).arg(s->invoke("searchCount").toInt());
    }
    s->check(!found.join(' ').contains(QStringLiteral("=0")), "real settings found: " + found.join(' '));
    s->invoke("setSearch", QStringLiteral("SMP"));
    s->check(s->invoke("searchCount").toInt() == 0, "'SMP' finds nothing: no inert switch for physics (SET-02)");
    s->invoke("setSearch", QString());
  });
  // Every category page: how long building it blocks the GUI thread (measured), and a capture of it.
  for (const auto* key : {"general", "appearance", "workspace", "viewport", "npc", "performance", "mo2", "system"}) {
    c->steps.push_back([s, key] {
      QElapsedTimer t;
      t.start();
      s->invoke("openAt", QString::fromLatin1(key), QString());
      const auto ms = t.elapsed();
      s->check(ms < 1500, QStringLiteral("page '%1' built in %2 ms").arg(QString::fromLatin1(key)).arg(ms));
      s->capture(QStringLiteral("page-%1").arg(QString::fromLatin1(key)));
    });
  }
  c->steps.push_back([s] { s->invoke("openAt", QStringLiteral("appearance"), QStringLiteral("appearance.theme")); });
  c->steps.push_back([s] {
    s->check(s->choose("appearance.theme", "steel"), "theme combo activated: Azul acero");
  });
  c->steps.push_back([s] {
    s->check(s->settings->theme() == "steel", "theme is now steel");
    s->check(s->window_color() == QColor("#121821"), "window repainted live with the new theme (SET-04): " + s->window_color().name());
    s->check(s->settings->preview_active(), "keep-or-revert banner shown");
    s->capture("theme-steel-preview");
    auto* keep = s->item(QStringLiteral("keepPreviewButton"));
    s->check(keep != nullptr, "banner 'Mantener' button on screen");
    if (keep) s->click(keep);
  });
  c->steps.push_back([s] {
    s->check(!s->settings->preview_active(), "a real click on 'Mantener' keeps the theme");
    s->font_before = s->font_px();
    s->check(s->slide("appearance.fontScale", 130), "text size slider moved to 130 %");
  });
  c->steps.push_back([s] {
    s->check(s->wb->font_scale() == 130, QStringLiteral("WorkbenchState text size %1 % (one source of truth)").arg(s->wb->font_scale()));
    s->check(s->font_px() > s->font_before, QStringLiteral("window font grew: %1 -> %2 px (SET-06)").arg(s->font_before).arg(s->font_px()));
    s->capture("font-130-preview");
    auto* revert = s->item(QStringLiteral("revertPreviewButton"));
    s->check(revert != nullptr, "banner 'Revertir' button on screen");
    if (revert) s->click(revert);
  });
  c->steps.push_back([s] {
    s->check(s->wb->font_scale() == 100 && s->font_px() == s->font_before, "a real click on 'Revertir' gives the text size back (SET-05)");
    s->check(s->settings->theme() == "steel", "revert only undoes what changed after 'Mantener'");
    s->check(s->slide("appearance.fontScale", 120), "text size slider moved to 120 %");
  });
  c->steps.push_back([s] {
    auto* keep = s->item(QStringLiteral("keepPreviewButton"));
    if (keep) s->click(keep);
  });
  c->steps.push_back([s] {
    s->check(s->wb->font_scale() == 120 && !s->settings->preview_active(), "120 % kept");
    s->invoke("openAt", QStringLiteral("viewport"), QStringLiteral("viewport.background"));
  });
  c->steps.push_back([s] {
    s->check(s->choose("viewport.background", "black"), "viewer background combo activated: Negro");
    s->check(s->slide("viewport.cameraFov", 60), "camera FOV slider moved to 60°");
  });
  c->steps.push_back([s] {
    s->check(s->viewer_clear_color() == QColor("#000000"), "View3D clears with black now (SET-14): " + s->viewer_clear_color().name());
    s->check(s->settings->camera_fov() == 60, "camera FOV stored as 60");
    s->capture("viewer-black");
    s->invoke("openAt", QStringLiteral("workspace"), QStringLiteral("workspace.panels"));
  });
  c->steps.push_back([s] {
    auto* spin = s->item(QStringLiteral("settingsLeftWidth"));
    s->check(spin != nullptr, "panel width control on the Workspace page");
    if (spin) {
      spin->setProperty("value", 420);
      QMetaObject::invokeMethod(spin, "valueModified");
    }
  });
  c->steps.push_back([s] {
    s->check(s->wb->layout("creator")->left_width() == 420, QStringLiteral("Character Creator left panel width %1 (SET-07)").arg(s->wb->layout("creator")->left_width()));
    s->check(s->settings->changed_count() >= 4, QStringLiteral("%1 settings differ from their defaults").arg(s->settings->changed_count()));
    s->capture("workspace-page");
    QTest::keyClick(s->window, Qt::Key_Escape);
  });
  c->steps.push_back([s] {
    s->check(!s->center_open(), "Escape closes the Settings Center");
    s->capture("closed");
    QJsonObject e{{"theme", s->settings->theme()},
                  {"windowColor", s->window_color().name()},
                  {"fontScale", s->wb->font_scale()},
                  {"fontPx", s->font_px()},
                  {"background", s->settings->value("viewport.background").toString()},
                  {"clearColor", s->viewer_clear_color().name()},
                  {"fov", s->settings->camera_fov()},
                  {"leftWidth", s->wb->layout("creator")->left_width()}};
    QFile f(QDir(s->dir).filePath("expect.json"));
    s->check(f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(e).toJson()) > 0, "expect.json written for the restart");
  });
  c->run_next();
}

}  // namespace sns::app
