#include "workbench_check.hpp"

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
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "viewmodels/workbench_state.hpp"

namespace sns::app {
namespace {

constexpr int kHandle = 6;     // Theme.splitHandle
constexpr int kStepMs = 450;   // layout, bindings and a frame between steps

struct Check : std::enable_shared_from_this<Check> {
  QPointer<QQuickWindow> window;
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
  // Values carried between steps.
  qreal left0 = 0, right0 = 0, left1 = 0, right1 = 0, right_before = 0, left_shown = 0, right_shown = 0;
  int stored_left1 = 0, stored_right1 = 0;
  QPointer<QQuickItem> viewport;

  void log(const QString& text) const {
    std::fprintf(stderr, "workbench: [%6lld ms] %s\n", clock.elapsed(), qPrintable(text));
    std::fflush(stderr);
  }
  void check(bool ok, const QString& what) {
    log(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what));
    passed &= ok;
  }
  void finish() {
    if (done) return;
    done = true;
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

  QQuickItem* item(const QString& name) const { return window->findChild<QQuickItem*>(name); }
  qreal width_of(const QString& name) const { auto* i = item(name); return i ? i->width() : -1; }
  bool shown(const QString& name) const { auto* i = item(name); return i && i->isVisible(); }
  WorkspaceLayout* layout(const char* ws) const { return wb->layout(QString::fromLatin1(ws)); }
  int font_px() const { return window->property("font").value<QFont>().pixelSize(); }
  static bool near(qreal a, qreal b, qreal tolerance = 3) { return std::abs(a - b) <= tolerance; }

  void capture(const QString& name) {
    if (dir.isEmpty()) return;
    const auto file = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(++shot, 2, 10, QChar('0')).arg(name));
    const bool saved = window->grabWindow().save(file);
    log(QStringLiteral("capture %1 %2").arg(saved ? "saved" : "FAILED", file));
  }
  void key(Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    window->requestActivate();
    QTest::keyClick(window.data(), k, mods);
  }
  // Presses on `from` (scene coordinates), moves in small steps and releases `dx` further.
  void drag(QPointF from, qreal dx) {
    const QPoint start = from.toPoint();
    QTest::mousePress(window.data(), Qt::LeftButton, Qt::NoModifier, start);
    for (int i = 1; i <= 8; ++i) QTest::mouseMove(window.data(), start + QPoint(int(std::lround(dx * i / 8.0)), 0), 10);
    QTest::mouseRelease(window.data(), Qt::LeftButton, Qt::NoModifier, start + QPoint(int(std::lround(dx)), 0), 10);
  }
  QPointF divider_right_of(QQuickItem* pane) const {
    return pane->mapToScene(QPointF(pane->width() + kHandle / 2.0, pane->height() / 2));
  }
  QPointF divider_left_of(QQuickItem* pane) const { return pane->mapToScene(QPointF(-kHandle / 2.0, pane->height() / 2)); }
  void select_workspace(int index) {
    if (auto* tabs = item("workspaceTabs")) tabs->setProperty("currentIndex", index);
  }

  // ------------------------------------------------------------------ restart: what an earlier run left
  void build_expect_steps() {
    steps.push_back([this] {
      QFile file(expect_file);
      if (!file.open(QIODevice::ReadOnly)) { check(false, "expect file readable: " + expect_file); return; }
      const auto e = QJsonDocument::fromJson(file.readAll()).object();
      auto* creator = layout("creator");
      auto* outfit = layout("outfit");
      check(creator->left_width() == e["creatorLeft"].toInt(), QStringLiteral("restart: creator left width stored %1 (expected %2)")
                                                                    .arg(creator->left_width()).arg(e["creatorLeft"].toInt()));
      check(creator->right_width() == e["creatorRight"].toInt(), QStringLiteral("restart: creator right width stored %1 (expected %2)")
                                                                      .arg(creator->right_width()).arg(e["creatorRight"].toInt()));
      check(outfit->right_visible() == e["outfitRightVisible"].toBool(), "restart: outfit right panel visibility kept");
      check(outfit->left_width() == e["outfitLeft"].toInt(), "restart: outfit left width independent and kept");
      select_workspace(0);
    });
    steps.push_back([this] {
      const auto e_left = layout("creator")->left_width() * wb->ui_scale() / 100.0;
      check(near(width_of("creatorLeftPanel"), e_left, 2),
            QStringLiteral("restart: creator left panel on screen %1 px (stored %2)").arg(width_of("creatorLeftPanel")).arg(e_left));
      const auto e_right = layout("creator")->right_width() * wb->ui_scale() / 100.0;
      check(near(width_of("creatorRightPanel"), e_right, 2),
            QStringLiteral("restart: creator right panel on screen %1 px (stored %2)").arg(width_of("creatorRightPanel")).arg(e_right));
      capture("restart-creator");
      select_workspace(1);
    });
    steps.push_back([this] {
      check(!shown("outfitRightPanel"), "restart: outfit right panel still hidden");
      capture("restart-outfit");
    });
  }

  // ------------------------------------------------------------------ full run
  void build_steps() {
    steps.push_back([this] {
      wb->resetAll();
      select_workspace(0);
    });
    steps.push_back([this] {
      left0 = width_of("creatorLeftPanel");
      right0 = width_of("creatorRightPanel");
      viewport = item("creatorViewport");
      check(left0 > 0 && right0 > 0 && viewport, QStringLiteral("creator panels found (left %1, right %2, viewer %3)")
                                                     .arg(left0).arg(right0).arg(viewport ? viewport->width() : -1));
      capture("creator-initial");
      drag(divider_right_of(item("creatorLeftPanel")), 60);  // explorer 60 px wider
    });
    steps.push_back([this] {
      left1 = width_of("creatorLeftPanel");
      stored_left1 = layout("creator")->left_width();
      // 60 px unless the window caps the panel (the inspector keeps its minimum).
      check(left1 >= left0 + 30, QStringLiteral("mouse drag widens the left panel: %1 -> %2 px").arg(left0).arg(left1));
      check(near(stored_left1, left1, 1), QStringLiteral("left width stored for creator: %1").arg(stored_left1));
      right_before = width_of("creatorRightPanel");
      drag(divider_left_of(item("creatorRightPanel")), -50);  // inspector 50 px wider
    });
    steps.push_back([this] {
      right1 = width_of("creatorRightPanel");
      stored_right1 = layout("creator")->right_width();
      check(near(right1, right_before + 50), QStringLiteral("mouse drag widens the right panel: %1 -> %2 px").arg(right_before).arg(right1));
      check(near(stored_right1, right1, 1), QStringLiteral("right width stored for creator: %1").arg(stored_right1));
      check(viewport && viewport->width() >= 380 - 1,
            QStringLiteral("viewer keeps its minimum: %1 px (left now %2)").arg(viewport ? viewport->width() : -1).arg(width_of("creatorLeftPanel")));
      left_shown = width_of("creatorLeftPanel");
      right_shown = width_of("creatorRightPanel");
      // A narrow window makes the explorer give way; what is on screen is what stays stored.
      stored_left1 = layout("creator")->left_width();
      check(near(stored_left1, left_shown, 1), QStringLiteral("stored arrangement matches the screen: left %1").arg(stored_left1));
      capture("creator-resized");
      key(Qt::Key_B, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(!shown("creatorLeftPanel"), "Ctrl+B hides the explorer");
      check(layout("creator")->left_width() == stored_left1, "hiding keeps the stored left width");
      key(Qt::Key_B, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(shown("creatorLeftPanel") && near(width_of("creatorLeftPanel"), left_shown, 1),
            QStringLiteral("Ctrl+B shows it again at %1 px").arg(width_of("creatorLeftPanel")));
      key(Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
    });
    steps.push_back([this] {
      check(!shown("creatorRightPanel"), "Ctrl+Alt+B hides the inspector");
      key(Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
    });
    steps.push_back([this] {
      check(shown("creatorRightPanel") && near(width_of("creatorRightPanel"), right_shown, 1), "Ctrl+Alt+B shows it at the same width");
      key(Qt::Key_J, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(shown("bottomPanel"), "Ctrl+J shows the console and diagnostics");
      capture("creator-console");
      key(Qt::Key_J, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(!shown("bottomPanel"), "Ctrl+J hides it again");
      key(Qt::Key_F11);
    });
    steps.push_back([this] {
      auto* split = item("creatorSplit");
      check(layout("creator")->maximized(), "F11 focuses the viewer");
      check(!shown("creatorLeftPanel") && !shown("creatorRightPanel") && !shown("bottomPanel"),
            "focus hides explorer, inspector and console");
      check(viewport && item("creatorViewport") == viewport.data(), "the same viewer item stays (no second viewer)");
      check(viewport && split && viewport->width() >= split->width() - 1,
            QStringLiteral("viewer takes the whole width: %1 of %2 px").arg(viewport ? viewport->width() : -1).arg(split ? split->width() : -1));
      check(layout("creator")->left_width() == stored_left1 && layout("creator")->right_width() == stored_right1,
            "focus does not overwrite the stored layout");
      capture("creator-focus");
      key(Qt::Key_F11);
    });
    steps.push_back([this] {
      check(!layout("creator")->maximized(), "F11 again leaves the focus");
      check(near(width_of("creatorLeftPanel"), left_shown, 1) && near(width_of("creatorRightPanel"), right_shown, 1),
            QStringLiteral("previous layout back: left %1, right %2").arg(width_of("creatorLeftPanel")).arg(width_of("creatorRightPanel")));
      check(viewport && item("creatorViewport") == viewport.data(), "viewer item unchanged after focus");
      key(Qt::Key_Plus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 110, QStringLiteral("Ctrl++ raises the interface scale to %1 %").arg(wb->ui_scale()));
      check(font_px() == 13, QStringLiteral("window text follows: %1 px").arg(font_px()));
      // The panels grow with the scale but give way so the viewer keeps its (scaled) minimum.
      check(viewport && viewport->width() >= 380 * 1.1 - 2,
            QStringLiteral("panels follow the scale (left %1 px), viewer keeps its scaled minimum: %2 px")
                .arg(width_of("creatorLeftPanel")).arg(viewport ? viewport->width() : -1));
      check(layout("creator")->left_width() == stored_left1, "scaling does not rewrite the stored widths");
      capture("creator-110");
      key(Qt::Key_Minus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 100, "Ctrl+- goes back to 100 %");
      key(Qt::Key_Minus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 90, QStringLiteral("Ctrl+- again: %1 %").arg(wb->ui_scale()));
      for (int i = 0; i < 6; ++i) key(Qt::Key_Minus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 85, QStringLiteral("never below 85 %: %1 %").arg(wb->ui_scale()));
      key(Qt::Key_0, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 100, "Ctrl+0 resets the interface scale");
      for (int i = 0; i < 8; ++i) key(Qt::Key_Plus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 150, QStringLiteral("never above 150 %: %1 %").arg(wb->ui_scale()));
      wb->set_font_scale(160);
      wb->set_density(WorkbenchState::Comfortable);
    });
    steps.push_back([this] {
      check(font_px() == 29, QStringLiteral("UI 150 % + text 160 %: window text %1 px").arg(font_px()));
      auto* left = item("creatorLeftPanel");
      auto* right = item("creatorRightPanel");
      const auto l_edge = left->mapToScene(QPointF(left->width(), 0)).x();
      const auto r_edge = right->mapToScene(QPointF(0, 0)).x();
      check(viewport && l_edge <= viewport->mapToScene(QPointF(0, 0)).x() + 1 &&
                r_edge >= viewport->mapToScene(QPointF(viewport->width(), 0)).x() - 1,
            "extreme scale: panels do not overlap the viewer");
      check(viewport && viewport->width() >= 380 * 1.5 - 2,
            QStringLiteral("extreme scale: viewer keeps %1 px").arg(viewport ? viewport->width() : -1));
      for (const auto* name : {"focusViewerButton", "headerExportButton", "scaleStatusButton"}) {
        auto* button = item(QString::fromLatin1(name));
        const auto right_edge = button ? button->mapToScene(QPointF(button->width(), 0)).x() : 1e9;
        check(right_edge <= window->width() + 1,
              QStringLiteral("extreme scale: %1 stays inside the window (right edge %2 of %3)").arg(name).arg(right_edge).arg(window->width()));
      }
      capture("creator-150-160-comfortable");
      wb->set_ui_scale(85);
      wb->set_font_scale(80);
      wb->set_density(WorkbenchState::Compact);
    });
    steps.push_back([this] {
      check(font_px() == 8, QStringLiteral("UI 85 % + text 80 %: window text %1 px").arg(font_px()));
      capture("creator-85-80-compact");
      wb->resetAppearance();
      select_workspace(1);
    });
    steps.push_back([this] {
      check(layout("outfit")->left_width() != stored_left1 && width_of("outfitLeftPanel") > 0 &&
                width_of("outfitLeftPanel") <= layout("outfit")->left_width() + 1,
            QStringLiteral("outfit keeps its own widths (left %1)").arg(width_of("outfitLeftPanel")));
      key(Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
    });
    steps.push_back([this] {
      check(!shown("outfitRightPanel") && !layout("outfit")->right_visible(), "Ctrl+Alt+B hides the outfit editor");
      check(layout("creator")->right_visible(), "the creator inspector is untouched");
      capture("outfit-right-hidden");
      select_workspace(2);
    });
    steps.push_back([this] {
      check(shown("forgeLeftPanel") && shown("forgeRightPanel"), "forge panels shown");
      key(Qt::Key_B, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(!shown("forgeLeftPanel") && !layout("forge")->left_visible(), "Ctrl+B hides the forge tools panel");
      key(Qt::Key_B, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(shown("forgeLeftPanel"), "Ctrl+B shows it again");
      capture("forge");
      select_workspace(0);
    });
    steps.push_back([this] {
      check(near(width_of("creatorLeftPanel"), left_shown, 1) && near(width_of("creatorRightPanel"), right_shown, 1),
            "back in Character Creator: its layout is unchanged");
      capture("creator-final");
      const QJsonObject e{{"creatorLeft", layout("creator")->left_width()},
                          {"creatorRight", layout("creator")->right_width()},
                          {"outfitLeft", layout("outfit")->left_width()},
                          {"outfitRightVisible", layout("outfit")->right_visible()}};
      QFile file(QDir(dir).filePath("expect.json"));
      check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(e).toJson()) > 0, "expect.json written for the restart run");
    });
  }
};

}  // namespace

void run_workbench_check(QQuickWindow* window, WorkbenchState* workbench, const QString& dir, const QString& expect_file,
                         std::function<void(bool)> completed) {
  auto check = std::make_shared<Check>();
  check->window = window;
  check->wb = workbench;
  check->dir = dir;
  check->expect_file = expect_file;
  check->completed = std::move(completed);
  check->clock.start();
  if (!window || !workbench) {
    std::fprintf(stderr, "workbench: no window or Workbench singleton\n");
    check->completed(false);
    return;
  }
  if (!dir.isEmpty()) QDir().mkpath(dir);
  if (expect_file.isEmpty()) check->build_steps();
  else check->build_expect_steps();
  window->requestActivate();
  check->run_next();
}

}  // namespace sns::app
