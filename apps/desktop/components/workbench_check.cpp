#include "workbench_check.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>
#include <QGuiApplication>
#include <QAccessible>
#include <qpa/qwindowsysteminterface.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "viewmodels/workbench_state.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>  // GetProcessMemoryInfo (K32GetProcessMemoryInfo in kernel32, no extra library)
#undef near  // windef.h defines near/far as empty macros; this file has a near() helper
#undef far
#include <algorithm>

namespace sns::app {
namespace {

constexpr int kHandle = 8;     // Theme.splitHandle
constexpr int kBodyText = 13;  // Theme.normal base size (window font)
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
  int saved_left = 0, saved_right = 0;
  QSize original_size;
  QPointer<QQuickItem> viewport;
  QPointer<QQuickItem> focus_before;
  QList<QPointer<QQuickItem>> tab_chain;
  QJsonObject a11y;

  // The first visible item with this objectName (several workspaces reuse names: the hidden ones are skipped).
  QQuickItem* visible_named(const QString& name) const {
    for (auto* i : window->findChildren<QQuickItem*>(name))
      if (i->isVisible()) return i;
    return nullptr;
  }
  // A visible item under `root` whose `text` is exactly `text` (buttons of an EmptyState are Repeater children).
  static QQuickItem* visible_text(QQuickItem* root, const QString& text) {
    for (auto* c : root->childItems()) {
      if (c->isVisible() && c->property("text").toString() == text) return c;
      if (auto* found = visible_text(c, text)) return found;
    }
    return nullptr;
  }
  void click(QQuickItem* target) {
    window->requestActivate();
    const QPoint at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    QTest::mouseClick(window.data(), Qt::LeftButton, Qt::NoModifier, at);
  }
  QString header_context() const {
    auto* label = item("headerContextLabel");
    return label ? label->property("text").toString() : QString();
  }

  // Working set and private bytes of this process, in MB.
  static QJsonObject process_memory() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) return {};
    return {{"workingSetMB", double(pmc.WorkingSetSize) / (1024 * 1024)}, {"privateMB", double(pmc.PrivateUsage) / (1024 * 1024)}};
  }
  static double percentile(QList<double> v, double p) {
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min<qsizetype>(v.size() - 1, qsizetype(std::ceil(p * v.size())) - 1)];
  }
  static QJsonObject stats(const QList<double>& samples, const QJsonObject& mem_before, const QJsonObject& mem_after) {
    QJsonArray raw;
    for (double s : samples) raw.append(std::round(s * 10) / 10);
    return {{"n", samples.size()}, {"medianMs", percentile(samples, 0.5)}, {"p95Ms", percentile(samples, 0.95)},
            {"samplesMs", raw}, {"memoryBefore", mem_before}, {"memoryAfter", mem_after}};
  }

  // Every visible action of the header ends inside the window (labels give way first, then "Más").
  void check_header(const QString& when) {
    auto* row = item("headerRow");
    bool inside = row != nullptr;
    QString offender;
    if (row) {
      for (auto* child : row->childItems()) {
        if (!child->isVisible() || child->width() <= 0) continue;
        const auto right = child->mapToScene(QPointF(child->width(), 0)).x();
        if (right > window->width() + 1) {
          inside = false;
          offender = child->objectName().isEmpty() ? QString::fromLatin1(child->metaObject()->className()) : child->objectName();
        }
      }
    }
    check(inside, QStringLiteral("%1: every header action inside the window%2")
                      .arg(when, offender.isEmpty() ? QString() : QStringLiteral(" (%1 outside)").arg(offender)));
  }

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
      // Layout (SplitView polish) is applied with the next frame: render one first, or a step measures the widths
      // of before the last change (seen at 1100 px with a heavy 3D scene; the captures showed the right layout).
      self->window->grabWindow();
      self->steps[self->next++]();
      self->run_next();
    });
  }

  QQuickItem* item(const QString& name) const { return window->findChild<QQuickItem*>(name); }
  // The Character Creator viewer (Outfit Studio has another "characterViewport" of its own).
  QQuickItem* creator_viewport() const {
    auto* split = item("creatorSplit");
    return split ? split->findChild<QQuickItem*>("characterViewport") : nullptr;
  }
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
    // Window shortcuts (Action.shortcut, Shortcut) fire only for Qt's focus window. When another application holds
    // the desktop foreground, Windows refuses requestActivate(); tell Qt the window is active so the check does not
    // depend on what the user has in front (test-only; nothing else reads it).
    if (QGuiApplication::focusWindow() != window.data()) {
      QWindowSystemInterface::handleFocusWindowChanged(window.data(), Qt::ActiveWindowFocusReason);
      QWindowSystemInterface::flushWindowSystemEvents();
    }
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
      viewport = creator_viewport();
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
      check(viewport && creator_viewport() == viewport.data(), "the same viewer item stays (no second viewer)");
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
      check(viewport && creator_viewport() == viewport.data(), "viewer item unchanged after focus");
      key(Qt::Key_Plus, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(wb->ui_scale() == 110, QStringLiteral("Ctrl++ raises the interface scale to %1 %").arg(wb->ui_scale()));
      check(font_px() == WorkbenchState::legibleFontPixels(kBodyText, 110, 100),
            QStringLiteral("window text follows: %1 px (expected %2)").arg(font_px()).arg(WorkbenchState::legibleFontPixels(kBodyText, 110, 100)));
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
      check(font_px() == WorkbenchState::legibleFontPixels(kBodyText, 150, 160),
            QStringLiteral("UI 150 % + text 160 %: window text %1 px (expected %2)").arg(font_px()).arg(WorkbenchState::legibleFontPixels(kBodyText, 150, 160)));
      auto* left = item("creatorLeftPanel");
      auto* right = item("creatorRightPanel");
      // A panel put away for lack of room (UX06 auto-hide) keeps a stale geometry: it cannot overlap anything.
      const auto l_edge = left->isVisible() ? left->mapToScene(QPointF(left->width(), 0)).x() : 0.0;
      const auto r_edge = right->isVisible() ? right->mapToScene(QPointF(0, 0)).x() : qreal(window->width());
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
      // 0.68 x would give 9 px: the legibility floor holds body text at 11 px while the preference stays 85/80.
      check(font_px() == WorkbenchState::legibleFontPixels(kBodyText, 85, 80) && font_px() >= 11,
            QStringLiteral("UI 85 % + text 80 %: window text held at %1 px (legible minimum)").arg(font_px()));
      check(wb->ui_scale() == 85 && wb->font_scale() == 80 && wb->legibility_floor_active(),
            "the 85/80 preference is kept and reported as limited");
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
    // UX06: a double click on the explorer divider puts its default width back; the inspector is untouched.
    steps.push_back([this] {
      check_header(QStringLiteral("%1 px at 100 %").arg(window->width()));
      saved_left = layout("creator")->left_width();
      saved_right = layout("creator")->right_width();
      const QPoint at = divider_right_of(item("creatorLeftPanel")).toPoint();
      // What is under the pointer, topmost first at every level (popups and tooltips live in the overlay).
      QStringList chain;
      for (QQuickItem* it = window->contentItem(); it;) {
        chain << QStringLiteral("%1(%2)").arg(QString::fromLatin1(it->metaObject()->className()), it->objectName());
        const QPointF local = it->mapFromScene(QPointF(at));
        it = it->childAt(local.x(), local.y());
      }
      log(QStringLiteral("under the divider point (%1, %2): %3").arg(at.x()).arg(at.y()).arg(chain.join(" > ")));
      QTest::mouseDClick(window.data(), Qt::LeftButton, Qt::NoModifier, at);
    });
    steps.push_back([this] {
      check(layout("creator")->left_width() == layout("creator")->default_left_width() ||
                near(width_of("creatorLeftPanel"), layout("creator")->default_left_width() * wb->ui_scale() / 100.0, 2),
            QStringLiteral("double click on the explorer divider restores its default width: stored %1, default %2")
                .arg(layout("creator")->left_width()).arg(layout("creator")->default_left_width()));
      check(layout("creator")->right_width() == saved_right, "the double click leaves the inspector width alone");
      capture("creator-divider-reset");
      QTest::mouseDClick(window.data(), Qt::LeftButton, Qt::NoModifier, divider_left_of(item("creatorRightPanel")).toPoint());
    });
    steps.push_back([this] {
      check(layout("creator")->right_width() == layout("creator")->default_right_width(),
            QStringLiteral("double click on the inspector divider restores its default width: stored %1, default %2")
                .arg(layout("creator")->right_width()).arg(layout("creator")->default_right_width()));
      check(layout("creator")->left_width() == layout("creator")->default_left_width(), "... and leaves the explorer as it was");
      // Narrow window at a large scale: the inspector is put away on screen, the stored choice is kept.
      original_size = window->size();
      wb->set_ui_scale(150);
      window->resize(1100, window->height());
    });
    steps.push_back([this] {
      auto* l = layout("creator");
      check(l->right_auto_hidden() && l->right_visible() && !shown("creatorRightPanel"),
            QStringLiteral("1100 px at 150 %: inspector put away on screen (auto %1), the user's choice kept (visible %2)")
                .arg(l->right_auto_hidden()).arg(l->right_visible()));
      check(viewport && viewport->width() >= 380 * 1.5 - 2,
            QStringLiteral("1100 px at 150 %: viewer keeps %1 px").arg(viewport ? viewport->width() : -1));
      check(l->right_width() == l->default_right_width(), "auto-hiding does not rewrite the stored inspector width");
      check_header(QStringLiteral("1100 px at 150 %"));
      capture("creator-1100-150-autohide");
      wb->set_ui_scale(100);
      window->resize(original_size);
    });
    steps.push_back([this] {
      check(!layout("creator")->right_auto_hidden() && shown("creatorRightPanel"), "with room again the inspector comes back by itself");
      layout("creator")->set_left_width(saved_left);  // the restart run expects the arrangement dragged above
      layout("creator")->set_right_width(saved_right);
      // UX06-07 command palette, by keyboard: Ctrl+Shift+P from the NPC search field, type, Enter.
      focus_before = nullptr;
      for (auto* candidate : window->findChildren<QQuickItem*>("npcSearchField"))
        if (candidate->isVisible()) { focus_before = candidate; break; }
      if (focus_before) QMetaObject::invokeMethod(focus_before, "forceActiveFocus");
      key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    });
    steps.push_back([this] {
      auto* palette = window->findChild<QObject*>("commandPalette");
      check(palette && palette->property("opened").toBool(), "Ctrl+Shift+P opens the command palette");
      auto* query = window->findChild<QQuickItem*>("commandPaletteQuery");
      check(query && query->hasActiveFocus(), "the palette's query field has the keyboard focus");
      for (const char c : std::string("consola")) QTest::keyClick(window.data(), c);  // keyClicks() exists for QWidget only
      QTest::keyClick(window.data(), Qt::Key_Return);
    });
    steps.push_back([this] {
      auto* palette = window->findChild<QObject*>("commandPalette");
      check(palette && !palette->property("opened").toBool() && shown("bottomPanel"),
            QStringLiteral("typing «consola» + Enter runs %1 and shows the console").arg(palette ? palette->property("lastRun").toString() : "?"));
      check(focus_before && window->activeFocusItem() == focus_before.data(), "the focus goes back to the NPC search field");
      key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    });
    steps.push_back([this] {
      auto* palette = window->findChild<QObject*>("commandPalette");
      check(palette && palette->property("opened").toBool(), "Ctrl+Shift+P opens it again");
      key(Qt::Key_Escape);
    });
    steps.push_back([this] {
      auto* palette = window->findChild<QObject*>("commandPalette");
      check(palette && !palette->property("opened").toBool(), "Esc closes the palette without running anything");
      check(focus_before && window->activeFocusItem() == focus_before.data(), "after Esc the focus is back where it was");
      // Twelve commands through the palette, each checked by what it changes on screen.
      auto run = [this, palette](const char* id) {
        QVariant ok;
        QMetaObject::invokeMethod(palette, "runCommand", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, QString::fromLatin1(id)));
        return ok.toBool();
      };
      auto* tabs = item("workspaceTabs");
      check(run("panel.bottom") && !shown("bottomPanel"), "palette: panel.bottom hides the console again");
      check(run("ws.outfit") && tabs && tabs->property("currentIndex").toInt() == 1, "palette: ws.outfit switches workspace");
      check(run("ws.forge") && tabs && tabs->property("currentIndex").toInt() == 2, "palette: ws.forge switches workspace");
      check(run("ws.creator") && tabs && tabs->property("currentIndex").toInt() == 0, "palette: ws.creator switches back");
      check(run("view.focus") && layout("creator")->maximized(), "palette: view.focus focuses the viewer");
      check(run("view.focus") && !layout("creator")->maximized(), "palette: view.focus again leaves the focus");
      check(run("panel.right") && !layout("creator")->right_visible(), "palette: panel.right hides the inspector");
      check(run("panel.right") && layout("creator")->right_visible(), "palette: panel.right shows it again");
      const bool scaled = run("scale.in");
      check(scaled && wb->ui_scale() == 110, QStringLiteral("palette: scale.in -> %1 %").arg(wb->ui_scale()));
      check(run("scale.reset") && wb->ui_scale() == 100, "palette: scale.reset -> 100 %");
      check(run("readability.compact") && wb->ui_scale() == 90 && wb->density() == WorkbenchState::Compact,
            "palette: readability.compact applies interface 90 %, compact density");
      check(run("readability.balanced") && wb->ui_scale() == 100 && wb->font_scale() == 100 && wb->density() == WorkbenchState::Normal,
            "palette: readability.balanced back to 100 % / 100 % / normal");
      check(run("help.shortcuts"), "palette: help.shortcuts runs");
      check(palette && palette->property("ranCount").toInt() >= 14, QStringLiteral("commands run through the palette: %1").arg(palette ? palette->property("ranCount").toInt() : 0));
    });
    steps.push_back([this] {
      auto* dialog = window->findChild<QObject*>("shortcutsDialog");
      check(dialog && dialog->property("opened").toBool(), "the shortcut list opens (bindings taken from the command list)");
      capture("shortcuts-dialog");
      if (dialog) QMetaObject::invokeMethod(dialog, "close");
    });
    // UX06 P01 / P02 / P06: workspace switches, panel toggles and layout save/apply, timed, with the process memory.
    steps.push_back([this] {
      QJsonObject perf;
      auto* tabs = item("workspaceTabs");
      auto* view = creator_viewport();
      const auto view_state = [view]() {
        QVariant state;  // an untyped QML function returns a QVariant
        if (view) QMetaObject::invokeMethod(view, "viewState", Q_RETURN_ARG(QVariant, state));
        return state.toString();
      };
      const QString camera_before = view_state();
      const auto mem0 = process_memory();
      QList<double> switch_ms;
      for (int i = 1; i <= 30; ++i) {
        QElapsedTimer t;
        t.start();
        if (tabs) tabs->setProperty("currentIndex", i % 3);
        QCoreApplication::processEvents();
        window->grabWindow();  // the frame of the new workspace is part of the cost
        switch_ms << t.nsecsElapsed() / 1e6;
      }
      if (tabs) tabs->setProperty("currentIndex", 0);
      QCoreApplication::processEvents();
      const auto mem1 = process_memory();
      perf["P01_workspaceSwitch"] = stats(switch_ms, mem0, mem1);
      check(tabs && tabs->property("currentIndex").toInt() == 0 && layout("creator")->left_width() == saved_left,
            QStringLiteral("P01: 30 workspace switches, median %1 ms, p95 %2 ms, layout kept")
                .arg(percentile(switch_ms, 0.5), 0, 'f', 1).arg(percentile(switch_ms, 0.95), 0, 'f', 1));

      QList<double> toggle_ms;
      auto* l = layout("creator");
      for (int i = 0; i < 50; ++i) {
        QElapsedTimer t;
        t.start();
        if (i % 2 == 0) l->set_left_visible(!l->left_visible()); else l->set_right_visible(!l->right_visible());
        QCoreApplication::processEvents();
        window->grabWindow();
        toggle_ms << t.nsecsElapsed() / 1e6;
      }
      l->set_left_visible(true);
      l->set_right_visible(true);
      QCoreApplication::processEvents();
      const QString camera_after = view_state();
      const auto mem2 = process_memory();
      perf["P02_panelToggle"] = stats(toggle_ms, mem1, mem2);
      const auto cam = [](const QString& s) { return s.mid(s.indexOf("cam=")); };  // camera and pivot part of the state
      check(!camera_before.isEmpty() && cam(camera_before) == cam(camera_after),
            QStringLiteral("P02: 50 panel toggles, median %1 ms, p95 %2 ms; camera unchanged (%3)")
                .arg(percentile(toggle_ms, 0.5), 0, 'f', 1).arg(percentile(toggle_ms, 0.95), 0, 'f', 1).arg(cam(camera_after)));

      // P06: save and apply the same layout five times; the widths must not drift.
      const int left = l->left_width(), right = l->right_width();
      bool stable = true;
      for (int i = 0; i < 5; ++i) {
        wb->saveLayout(QStringLiteral("ux06-drift"));
        l->set_left_width(left + 37);  // something else in between, so apply really has work to do
        wb->applyLayout(QStringLiteral("ux06-drift"));
        QCoreApplication::processEvents();
        stable &= l->left_width() == left && l->right_width() == right;
      }
      wb->deleteLayout(QStringLiteral("ux06-drift"));
      perf["P06_layoutDrift"] = QJsonObject{{"rounds", 5}, {"left", l->left_width()}, {"right", l->right_width()}, {"stable", stable}};
      check(stable, QStringLiteral("P06: 5 save/apply rounds, widths stay %1 / %2").arg(l->left_width()).arg(l->right_width()));
      QFile file(QDir(dir).filePath("perf.json"));
      if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(perf).toJson());
      log(QStringLiteral("perf: %1").arg(QString::fromUtf8(QJsonDocument(perf).toJson(QJsonDocument::Compact))));
    });

    // ---- UX06 U11: Tab / Shift+Tab move a visible focus inside the window; Configuración gives the focus back.
    steps.push_back([this] {
      select_workspace(0);
      if (auto* search = visible_named("npcSearchField")) QMetaObject::invokeMethod(search, "forceActiveFocus");
    });
    steps.push_back([this] {
      tab_chain.clear();
      tab_chain << window->activeFocusItem();
      bool inside = true;
      for (int i = 0; i < 5; ++i) {
        QTest::keyClick(window.data(), Qt::Key_Tab);
        QCoreApplication::processEvents();
        auto* f = window->activeFocusItem();
        tab_chain << f;
        if (!f || !f->isVisible() || f->mapToScene(QPointF(0, 0)).x() > window->width()) inside = false;
      }
      QStringList names;
      for (const auto& p : tab_chain) {
        auto* f = p.data();
        names << (f ? QString::fromLatin1(f->metaObject()->className()).section('_', 0, 0) + "(" + f->objectName() + ")" : QStringLiteral("null"));
      }
      const bool moved = tab_chain.size() == 6 && tab_chain[1] != tab_chain[0] && tab_chain[5] != tab_chain[4];
      check(moved && inside, QStringLiteral("U11: Tab moves a visible focus: %1").arg(names.join(" > ")));
      QTest::keyClick(window.data(), Qt::Key_Tab, Qt::ShiftModifier);
    });
    steps.push_back([this] {
      check(tab_chain.size() == 6 && window->activeFocusItem() == tab_chain[4].data(), "U11: Shift+Tab goes back to the previous control");
      if (auto* search = visible_named("npcSearchField")) QMetaObject::invokeMethod(search, "forceActiveFocus");
      focus_before = window->activeFocusItem();
      key(Qt::Key_Comma, Qt::ControlModifier);
    });
    steps.push_back([this] {
      auto* center = window->findChild<QObject*>("settingsCenter");
      check(center && center->property("opened").toBool(), "U11: Ctrl+, opens Configuración");
      key(Qt::Key_Escape);
    });
    steps.push_back([this] {
      auto* center = window->findChild<QObject*>("settingsCenter");
      check(center && !center->property("opened").toBool(), "U11: Esc closes Configuración");
      check(focus_before && window->activeFocusItem() == focus_before.data(), "U11: the focus is back on the NPC search field");
      select_workspace(1);
    });
    // ---- UX06 U20: the empty Outfit viewer's "Buscar NPC" shows the NPC explorer with its search focused.
    steps.push_back([this] {
      auto* empty = visible_named("viewportEmptyState");
      auto* button = empty ? visible_text(empty, QStringLiteral("Buscar NPC")) : nullptr;
      check(button != nullptr, "U20: the empty Outfit viewer offers 'Buscar NPC'");
      if (button) click(button);
    });
    steps.push_back([this] {
      auto* browser = item("outfitNpcBrowser");
      auto* search = browser ? browser->findChild<QQuickItem*>("npcSearchField") : nullptr;
      check(browser && browser->isVisible() && search && search->hasActiveFocus(),
            "U20: 'Buscar NPC' shows the Outfit NPC explorer with its search focused");
      capture("outfit-empty-viewer-action");
      select_workspace(2);
    });
    // ---- UX06 U21: the Forge start card creates a real project (CharacterForgeViewModel.newProject).
    steps.push_back([this] {
      auto* card = visible_named("forgeStartCard");
      check(card && card->property("title").toString().contains(QStringLiteral("Empezar")), "U21: Forge without project shows the start card");
      auto* button = card ? visible_text(card, QStringLiteral("Nuevo proyecto")) : nullptr;
      check(button != nullptr, "U21: the start card offers 'Nuevo proyecto'");
      if (button) click(button);
    });
    steps.push_back([this] {
      auto* card = visible_named("forgeStartCard");
      check(card && card->property("title").toString().contains(QStringLiteral("Proyecto listo")),
            QStringLiteral("U21: after 'Nuevo proyecto' the card shows the next steps (%1)").arg(card ? card->property("title").toString() : "-"));
      check(header_context() == QStringLiteral("Nuevo personaje"), "U21: the header names the new project: " + header_context());
      capture("forge-new-project");
      // U12: edit the project name in the inspector, then Ctrl+Z / Ctrl+Y through the Editar menu shortcuts.
      QQuickItem* field = nullptr;
      std::function<void(QQuickItem*)> scan = [&](QQuickItem* root) {
        for (auto* c : root->childItems()) {
          if (!field && c->isVisible() && c->property("placeholderText").toString() == QStringLiteral("Nombre del proyecto")) field = c;
          scan(c);
        }
      };
      scan(window->contentItem());
      check(field != nullptr, "U12: the project name field is on screen");
      if (field) {
        click(field);
        QTest::keyClick(window.data(), Qt::Key_A, Qt::ControlModifier);
        for (const char c : std::string("UX06 Undo")) QTest::keyClick(window.data(), c);
        QTest::keyClick(window.data(), Qt::Key_Tab);  // editingFinished -> setProjectName
      }
    });
    steps.push_back([this] {
      check(header_context() == QStringLiteral("UX06 Undo"), "U12: the edited name reaches the project: " + header_context());
      if (auto* tabs = item("workspaceTabs")) QMetaObject::invokeMethod(tabs, "forceActiveFocus");  // not in a text field
      key(Qt::Key_Z, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(header_context() == QStringLiteral("Nuevo personaje"), "U12: Ctrl+Z undoes the edit: " + header_context());
      key(Qt::Key_Y, Qt::ControlModifier);
    });
    steps.push_back([this] {
      check(header_context() == QStringLiteral("UX06 Undo"), "U12: Ctrl+Y redoes it: " + header_context());
      select_workspace(0);
    });
    // ---- UX06 P03: search latency on the real catalog (typing -> result count changed), debounce included.
    steps.push_back([this] {
      QJsonObject p03;
      const auto measure = [this](QQuickItem* field, QQuickItem* count, const QStringList& queries) {
        QList<double> ms;
        for (const auto& q : queries) {
          const QString before = count ? count->property("text").toString() : QString();
          QElapsedTimer t;
          t.start();
          field->setProperty("text", q);
          while (t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (count && count->property("text").toString() != before) break;
          }
          ms << t.nsecsElapsed() / 1e6;
        }
        field->setProperty("text", QString());
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return ms;
      };
      auto* creator = item("creatorSplit");
      auto* npcField = creator ? creator->findChild<QQuickItem*>("npcSearchField") : nullptr;
      auto* npcCount = creator ? creator->findChild<QQuickItem*>("npcResultCount") : nullptr;
      if (npcField && npcCount) {
        const auto ms = measure(npcField, npcCount, {"lydia", "serana", "nord", "000A2C8E", "guard"});
        p03["npcSearch"] = QJsonObject{{"n", ms.size()}, {"medianMs", percentile(ms, 0.5)}, {"p95Ms", percentile(ms, 0.95)},
                                       {"note", "includes the search debounce of Configuración (AppSettings.searchDebounceMs)"}};
        check(percentile(ms, 0.95) < 5000, QStringLiteral("P03: NPC search answered, median %1 ms, p95 %2 ms")
                                                 .arg(percentile(ms, 0.5), 0, 'f', 0).arg(percentile(ms, 0.95), 0, 'f', 0));
      } else check(false, "P03: NPC search field and count found");
      select_workspace(1);
      QMetaObject::invokeMethod(window.data(), "showCatalog");  // same handler as the palette's search.armor
      QCoreApplication::processEvents();
      window->grabWindow();
      auto* armorField = visible_named("armorSearchField");
      auto* armorCount = visible_named("armorResultCount");
      if (armorField && armorCount) {
        const auto ms = measure(armorField, armorCount, {"iron", "daedric", "robe", "cloak", "glass"});
        p03["armorSearch"] = QJsonObject{{"n", ms.size()}, {"medianMs", percentile(ms, 0.5)}, {"p95Ms", percentile(ms, 0.95)},
                                         {"note", "includes the 220 ms debounce of ArmorCatalog"}};
        check(percentile(ms, 0.95) < 5000, QStringLiteral("P03: armor search answered, median %1 ms, p95 %2 ms")
                                                 .arg(percentile(ms, 0.5), 0, 'f', 0).arg(percentile(ms, 0.95), 0, 'f', 0));
      } else check(false, "P03: armor search field and count found (Outfit catalog)");
      QFile file(QDir(dir).filePath("perf-p03.json"));
      if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(p03).toJson());
      log(QStringLiteral("perf-p03: %1").arg(QString::fromUtf8(QJsonDocument(p03).toJson(QJsonDocument::Compact))));
      select_workspace(0);
    });
    // ---- UX06 U16: a catalog row's tooltip carries the full identity (name, EditorID, FormKey, slots, plugin).
    steps.push_back([this] {
      select_workspace(1);
      QMetaObject::invokeMethod(window.data(), "showCatalog");  // the Outfit armor catalog on screen
    });
    steps.push_back([this] {
      auto* list = visible_named("armorListView");
      QQuickItem* row = nullptr;
      if (list)
        for (auto* c : list->property("contentItem").value<QQuickItem*>()->childItems())
          if (c->isVisible() && !c->property("formKey").toString().isEmpty()) { row = c; break; }
      QObject* catalog = list;
      while (catalog && catalog->metaObject()->indexOfMethod("detailsOf(QVariant)") < 0) catalog = catalog->parent();
      QVariant details;
      if (row && catalog) QMetaObject::invokeMethod(catalog, "detailsOf", Q_RETURN_ARG(QVariant, details), Q_ARG(QVariant, QVariant::fromValue<QObject*>(row)));
      const QString tip = details.toString();
      const bool complete = row && tip.contains(row->property("editorId").toString()) && tip.contains(row->property("formKey").toString()) &&
                            tip.contains(row->property("winningPlugin").toString()) &&
                            (row->property("name").toString().isEmpty() || tip.contains(row->property("name").toString()));
      check(complete, QStringLiteral("U16: the armor row tooltip names it in full: %1").arg(QString(tip).replace('\n', " | ")));
      select_workspace(0);
    });
    // ---- UX06 U26: every interactive control of the redesigned areas has an accessible name (what a screen reader reads).
    steps.push_back([this] {
      QAccessible::setActive(true);
      const auto audit = [](QQuickItem* root, QStringList* missing) {
        int controls = 0;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
          for (auto* c : i->childItems()) {
            if (!c->isVisible()) continue;
            // The text field inside a ComboBox is its display, read through the ComboBox itself.
            if (c->inherits("QQuickComboBox")) {
              ++controls;
              QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(c);
              if (!iface || iface->text(QAccessible::Name).trimmed().isEmpty())
                *missing << QStringLiteral("QQuickComboBox(%1)").arg(c->objectName());
              continue;
            }
            if (c->inherits("QQuickAbstractButton") || c->inherits("QQuickTextInput") ||
                c->inherits("QQuickSlider") || c->inherits("QQuickSpinBox") || c->objectName() == QLatin1String("npcViewSwitch") ||
                c->objectName() == QLatin1String("armorViewSwitch") || c->objectName() == QLatin1String("armorTypeFilter")) {
              ++controls;
              QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(c);
              const QString name = iface ? iface->text(QAccessible::Name).trimmed() : QString();
              if (name.isEmpty())
                *missing << QStringLiteral("%1(%2)").arg(QString::fromLatin1(c->metaObject()->className()).section('_', 0, 0), c->objectName());
            }
            walk(c);
          }
        };
        walk(root);
        return controls;
      };
      QJsonObject result;
      const auto run_area = [&](const QString& area, QQuickItem* root, bool required) {
        if (!root) { check(!required, "U26: area found: " + area); return; }
        QStringList missing;
        const int n = audit(root, &missing);
        result[area] = QJsonObject{{"controls", n}, {"missingName", missing.size()}, {"offenders", QJsonArray::fromStringList(missing)}};
        if (required)
          check(missing.isEmpty() && n > 0, QStringLiteral("U26: %1: %2 controls, %3 without an accessible name %4")
                                               .arg(area).arg(n).arg(missing.size()).arg(missing.join(", ")));
        else
          log(QStringLiteral("U26 (legacy, informative): %1: %2 controls, %3 without an accessible name %4")
                  .arg(area).arg(n).arg(missing.size()).arg(missing.mid(0, 12).join(", ")));
      };
      run_area("header", item("headerRow"), true);
      run_area("creator explorer", item("creatorLeftPanel"), true);
      run_area("creator viewport toolbar", visible_named("viewportToolbar"), true);
      run_area("creator inspector (pre-UX06 pages)", item("creatorRightPanel"), false);
      a11y = result;
      select_workspace(2);
    });
    steps.push_back([this] {
      QAccessible::setActive(true);
      QJsonObject result = a11y;
      QStringList missing;
      int n = 0;
      for (const char* name : {"forgeLeftPanel", "forgeRightPanel"}) {
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
          for (auto* c : i->childItems()) {
            if (!c->isVisible()) continue;
            if (c->inherits("QQuickAbstractButton") || c->inherits("QQuickTextInput") || c->inherits("QQuickComboBox")) {
              ++n;
              QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(c);
              if (!iface || iface->text(QAccessible::Name).trimmed().isEmpty())
                missing << QStringLiteral("%1(%2)").arg(QString::fromLatin1(c->metaObject()->className()).section('_', 0, 0), c->objectName());
            }
            walk(c);
          }
        };
        if (auto* root = item(QString::fromLatin1(name))) walk(root);
      }
      result["forge panels"] = QJsonObject{{"controls", n}, {"missingName", missing.size()}, {"offenders", QJsonArray::fromStringList(missing)}};
      check(missing.isEmpty() && n > 0, QStringLiteral("U26: forge panels: %1 controls, %2 without an accessible name %3")
                                           .arg(n).arg(missing.size()).arg(missing.join(", ")));
      QFile file(QDir(dir).filePath("a11y.json"));
      if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(result).toJson());
      select_workspace(0);
      // U15 + P04: the gallery of the NPC explorer.
      if (auto* browser = item("creatorNpcBrowser")) QMetaObject::invokeMethod(browser, "setViewMode", Q_ARG(QVariant, QStringLiteral("gallery")));
    });
    // ---- UX06 U15: a card without a portrait shows a diagnostic code and the renderer's reason, never an image.
    steps.push_back([this] {
      auto* browser = item("creatorNpcBrowser");
      QQuickItem* failed = nullptr;
      int ready = 0, readyWithImage = 0;
      QElapsedTimer t;
      t.start();
      while (!failed && t.elapsed() < 45000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (!browser) break;
        ready = readyWithImage = 0;
        for (auto* thumb : browser->findChildren<QQuickItem*>("npcCardPortrait")) {
          if (!thumb->isVisible()) continue;
          const auto state = thumb->property("portrait").toMap().value("state").toString();
          auto* image = thumb->findChild<QQuickItem*>("npcCardPortraitImage");
          if (state == QLatin1String("ready")) { ++ready; if (image && image->isVisible()) ++readyWithImage; }
          if (state == QLatin1String("error") && !failed) failed = thumb;
        }
      }
      check(ready == readyWithImage, QStringLiteral("U15: %1 ready portrait(s), each drawn from its render").arg(ready));
      if (failed) {
        auto* image = failed->findChild<QQuickItem*>("npcCardPortraitImage");
        auto* placeholder = failed->findChild<QQuickItem*>("npcCardPortraitPlaceholder");
        const auto diagnosis = failed->property("diagnosis").toList();
        const QString code = diagnosis.isEmpty() ? QString() : diagnosis.first().toString();
        const QString reason = failed->property("reason").toString();
        check(image && !image->isVisible() && placeholder && placeholder->isVisible() && !code.isEmpty() && !reason.isEmpty(),
              QStringLiteral("U15: card without portrait: code %1, no image, renderer reason kept for the tooltip: %2").arg(code, reason));
        capture("gallery-portrait-diagnosis");
      } else {
        log("U15: no portrait failed within 45 s on the visible cards (nothing to assert for the error placeholder)");
      }
    });
    // ---- UX06 P04: long scroll through the NPC gallery: time per screen and recycled delegates.
    steps.push_back([this] {
      auto* browser = item("creatorNpcBrowser");
      auto* grid = browser ? browser->findChild<QQuickItem*>("npcGallery") : nullptr;
      if (!grid) { check(false, "P04: NPC gallery found"); return; }
      auto* content = grid->property("contentItem").value<QQuickItem*>();
      QList<double> ms;
      int maxDelegates = 0;
      const double page = grid->height();
      for (int i = 0; i < 40; ++i) {
        QElapsedTimer t;
        t.start();
        const double maxY = std::max(0.0, grid->property("contentHeight").toDouble() - page);
        grid->setProperty("contentY", std::min(maxY, grid->property("contentY").toDouble() + page));
        QCoreApplication::processEvents();
        window->grabWindow();
        ms << t.nsecsElapsed() / 1e6;
        maxDelegates = std::max(maxDelegates, content ? int(content->childItems().size()) : 0);
      }
      const int count = grid->property("count").toInt();
      QJsonObject p04{{"screens", 40}, {"medianMs", percentile(ms, 0.5)}, {"p95Ms", percentile(ms, 0.95)},
                      {"maxLiveDelegates", maxDelegates}, {"modelRows", count}};
      QFile file(QDir(dir).filePath("perf-p04.json"));
      if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(p04).toJson());
      check(maxDelegates < 200 && count > maxDelegates,
            QStringLiteral("P04: 40 screens scrolled, median %1 ms, p95 %2 ms; at most %3 live delegates for %4 rows (virtualized)")
                .arg(percentile(ms, 0.5), 0, 'f', 1).arg(percentile(ms, 0.95), 0, 'f', 1).arg(maxDelegates).arg(count));
      capture("gallery-scrolled");
      grid->setProperty("contentY", 0);
      QMetaObject::invokeMethod(browser, "setViewMode", Q_ARG(QVariant, QStringLiteral("list")));
    });
    // ---- UX06 memory: three rounds of 50 panel toggles, JS garbage collected before each sample. A leak grows every
    // round by about the same; a cache grows in the first round and then stays.
    steps.push_back([this] {
      QQmlEngine* engine = ::qmlEngine(static_cast<const QObject*>(item("workspaceTabs")));
      auto* l = layout("creator");
      // deleteLater() objects are only deleted back in the loop that posted them: inside this step (nested
      // processEvents) they pile up and look like a leak. Flush them, as QTest::qWait does, before sampling.
      const auto flush_deletes = []() {
        for (int i = 0; i < 3; ++i) {
          QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
          QCoreApplication::processEvents();
        }
      };
      const auto sample = [engine, flush_deletes]() {
        if (engine) engine->collectGarbage();
        flush_deletes();
        return process_memory().value("privateMB").toDouble();
      };
      // Phase A: frame grabs alone (what the timing samples add). Phase B: panel toggles alone, frames left to the
      // render loop (about 2 frames per toggle) - the cost of the UI itself.
      const auto rounds_of = [&](bool toggles, bool grabs) {
        QJsonArray rounds;
        rounds.append(sample());
        for (int r = 0; r < 3; ++r) {
          for (int i = 0; i < 50; ++i) {
            if (toggles) { if (i % 2 == 0) l->set_left_visible(!l->left_visible()); else l->set_right_visible(!l->right_visible()); }
            if (grabs) window->grabWindow();
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < (grabs ? 0 : 35)) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QCoreApplication::processEvents();
          }
          l->set_left_visible(true);
          l->set_right_visible(true);
          rounds.append(sample());
        }
        return rounds;
      };
      // Object census: if hidden/shown panels leave objects behind, the counts grow with the toggles.
      const auto census = [this]() {
        int handles = 0, tooltips = 0, all = 0;
        for (auto* o : window->findChildren<QObject*>()) {
          ++all;
          const QString cls = QString::fromLatin1(o->metaObject()->className());
          if (cls.startsWith(QLatin1String("SplitHandleBar"))) ++handles;
          if (cls.contains(QLatin1String("ToolTip"))) ++tooltips;
        }
        return QStringLiteral("objects %1, SplitHandleBar %2, ToolTip %3").arg(all).arg(handles).arg(tooltips);
      };
      const auto histogram = [this]() {
        QMap<QString, int> h;
        for (auto* o : window->findChildren<QObject*>()) ++h[QString::fromLatin1(o->metaObject()->className())];
        return h;
      };
      const QString census_before = census();
      const auto hist_before = histogram();
      const auto grabs = rounds_of(false, true);
      const auto toggles = rounds_of(true, false);
      flush_deletes();
      log("census before toggles: " + census_before + " | after 150 toggles: " + census());
      const auto hist_after = histogram();
      QList<QPair<int, QString>> grown;
      for (auto it = hist_after.cbegin(); it != hist_after.cend(); ++it)
        if (const int d = it.value() - hist_before.value(it.key()); d > 0) grown.append({d, it.key()});
      std::sort(grown.begin(), grown.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
      QStringList top;
      for (int i = 0; i < std::min<qsizetype>(10, grown.size()); ++i) top << QStringLiteral("%1 +%2").arg(grown[i].second).arg(grown[i].first);
      log("classes that grew: " + top.join(", "));
      const auto delta = [](const QJsonArray& a, int i) { return a[i + 1].toDouble() - a[i].toDouble(); };
      QFile file(QDir(dir).filePath("perf-memory.json"));
      if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(QJsonObject{{"grabsOnlyPrivateMB", grabs}, {"togglesOnlyPrivateMB", toggles},
                                             {"note", "private bytes after JS GC; 3 rounds of 50 each"}}).toJson());
      log(QStringLiteral("memory grabs-only rounds: +%1 +%2 +%3 MB; toggles-only rounds: +%4 +%5 +%6 MB")
              .arg(delta(grabs, 0), 0, 'f', 0).arg(delta(grabs, 1), 0, 'f', 0).arg(delta(grabs, 2), 0, 'f', 0)
              .arg(delta(toggles, 0), 0, 'f', 0).arg(delta(toggles, 1), 0, 'f', 0).arg(delta(toggles, 2), 0, 'f', 0));
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
