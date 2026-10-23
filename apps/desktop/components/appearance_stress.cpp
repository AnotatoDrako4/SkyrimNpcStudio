#include "appearance_stress.hpp"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTest>
#include <QPainter>
#include <QSet>
#include <QUrl>
#include <QPointer>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQuick3D/QQuick3DGeometry>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include "viewmodels/resource_context.hpp"
#include "viewmodels/studio_controller.hpp"
#include "viewmodels/portrait_cache.hpp"
#include "viewmodels/portrait_renderer.hpp"
#include "sns/core/text.hpp"
#include "sns/rendering/scene.hpp"
#include "sns/face/facegen.hpp"
#include "sns/appearance/control_map.hpp"

namespace sns::app {
namespace {

const char* const kPalette[] = {"#111111", "#3B2219", "#5A3825", "#8B5A2B", "#C2B280", "#E6C280",
                                "#A53A1E", "#C0392B", "#808080", "#ECECEC", "#2980B9", "#8E44AD"};

// Private bytes of this process (logged, not asserted: allocator caches make one-off growth normal).
qint64 process_memory_mb() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return -1;
  return static_cast<qint64>(counters.PrivateUsage / (1024 * 1024));
}

// AG05: kills the worker process that THIS studio started (a child of this process), never another one: the way a crash looks from outside.
bool kill_child_worker() {
  const DWORD me = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool killed = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (entry.th32ParentProcessID != me || !QString::fromWCharArray(entry.szExeFile).contains(QLatin1String("BethesdaWorker"), Qt::CaseInsensitive)) continue;
      if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID)) {
        killed |= TerminateProcess(process, 1) != 0;
        CloseHandle(process);
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return killed;
}

struct Stress : std::enable_shared_from_this<Stress> {
  QPointer<QQuickWindow> window;
  QPointer<StudioController> studio;
  QStringList scenarios;
  QString second_npc;
  QString work_dir;
  std::function<void(bool)> completed;
  QElapsedTimer clock;
  qint64 last_activity = 0;
  int scenario = -1;
  bool passed = true;
  bool finished = false;
  QStringList failures;
  QStringList skipped;
  QString primary_npc;
  bool head_part_skip = false;
  bool baseline_outfit_exportable = false;
  std::deque<std::function<void()>> steps;  // the current scenario, one action per entry

  AppearanceEditor* appearance() const { return studio->appearance(); }
  SceneViewModel* scene() const { return studio->scene(); }

  void log(const QString& text) const {
    std::fprintf(stderr, "stress: [%6lld ms] %s\n", clock.elapsed(), qPrintable(text));
    std::fflush(stderr);
  }
  void check(bool ok, const QString& what) {
    log(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what));
    if (!ok) {
      passed = false;
      failures << scenarios.value(scenario) + ": " + what;
    }
  }
  // A check this run cannot measure: neither a pass nor a failure, listed apart in the summary.
  void skip(const QString& what) {
    log(QStringLiteral("SKIP %1").arg(what));
    skipped << scenarios.value(scenario) + ": " + what;
  }
  bool busy() const {
    // !loaded(): right after the NPC is selected nothing is "loading" yet and the face is not
    // analysing until it has been read, so edits started in that gap would be refused.
    return !appearance()->loaded() || appearance()->building() || appearance()->loading() || appearance()->analysing() ||
           scene()->loading() || studio->npc_loading();
  }
  // Hair Color Safe Mode: the previewed FaceGen is the profile's winning FaceGen with only the
  // HairTint colors rewritten (byte for byte), and the report says so.
  void check_hair_safe(const QColor& expected) {
    const auto report = appearance()->facegen_report();
    check(report.value("kind").toString() == "hair-safe", "build kind " + report.value("kind").toString() + " (expected hair-safe)");
    check(report.value("integrity").toString() == "hair-tint-only",
          "integrity '" + report.value("integrity").toString() + "', recolored: " + report.value("recoloredShapes").toStringList().join(", "));
    const auto original = appearance()->winning_facegen();
    const auto preview = appearance()->preview();
    const auto* recipe = appearance()->recipe();
    if (!original || !preview.overlay || !recipe) {
      check(false, "winning FaceGen and previewed FaceGen available");
      return;
    }
    const auto it = preview.overlay->find(text::data_path_key(recipe->facegen_mesh_path));
    if (it == preview.overlay->end() || !it->second) {
      check(false, "previewed FaceGen in the overlay");
      return;
    }
    const auto integrity = face::verify_hair_tint_integrity(*original, *it->second);
    check(integrity.byte_identical_outside_hair_tint && integrity.unchanged_except_hair_tint,
          QStringLiteral("preview == winning FaceGen except HairTint (%1 bytes): %2").arg(original->size()).arg(qs(integrity.summary())));
    check_hair_view(expected);
    check_exportable();
  }

  void watch() {
    const auto mark = [self = weak_from_this()] {
      if (const auto s = self.lock()) s->last_activity = s->clock.elapsed();
    };
    QObject::connect(appearance(), &AppearanceEditor::appearanceChanged, studio, mark);
    QObject::connect(appearance(), &AppearanceEditor::faceGenChanged, studio, mark);
    QObject::connect(appearance(), &AppearanceEditor::recipeChanged, studio, mark);
    QObject::connect(scene(), &SceneViewModel::loadingChanged, studio, mark);
    QObject::connect(scene(), &SceneViewModel::changed, studio, mark);
    QObject::connect(studio, &StudioController::npcChanged, studio, mark);
  }

  // Runs `next` once nothing has been loading or building for `quiet` ms.
  void when_idle(std::function<void()> next, int quiet = 900, qint64 timeout = 180000) {
    const auto start = clock.elapsed();
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [self = shared_from_this(), next = std::move(next), quiet, timeout, start, poll] {
      if (self->finished || !self->studio) return;
      if (self->clock.elapsed() - start > timeout) {
        self->check(false, QStringLiteral("idle wait timed out after %1 ms").arg(timeout));
        self->next_scenario();
        return;
      }
      if (self->busy() || self->clock.elapsed() - self->last_activity < quiet) {
        QTimer::singleShot(50, self->studio, *poll);
        return;
      }
      next();
    };
    QTimer::singleShot(50, studio, *poll);
  }
  void after(int ms, std::function<void()> next) {
    QTimer::singleShot(ms, studio, [self = shared_from_this(), next = std::move(next)] {
      if (!self->finished && self->studio) next();
    });
  }
  void step() {
    if (finished) return;
    if (steps.empty()) {
      next_scenario();
      return;
    }
    auto s = std::move(steps.front());
    steps.pop_front();
    s();
  }
  void then_idle() { when_idle([self = shared_from_this()] { self->step(); }); }

  // ---------------------------------------------------------------- checks on the live state
  QVariantList hair_parts() const {
    QVariantList out;
    for (const auto& p : scene()->parts()) {
      const auto row = p.toMap();
      if (row.value("hair").toBool() && row.value("shownByDefault").toBool()) out << row;
    }
    return out;
  }
  int hidden_hair_parts() const {
    int n = 0;
    for (const auto& p : scene()->parts()) {
      const auto row = p.toMap();
      if (row.value("hair").toBool() && !row.value("shownByDefault").toBool()) ++n;
    }
    return n;
  }
  // Exports the current face edit and checks the worker's report: success, only face fields in the
  // semantic diff, `required` among them, and the exported FaceGen files identical to the preview.
  void export_face_and_check(const QString& dir, const QString& plugin, const QString& required) {
    auto self = shared_from_this();
    if (!appearance()->exportable()) {
      check_exportable_or_explained();
      appearance()->resetFace();
      then_idle();
      return;
    }
    appearance()->exportFaceOverhaul(plugin, dir, true, false);
    auto wait = std::make_shared<std::function<void()>>();
    *wait = [self, wait, required] {
      if (self->appearance()->exporting()) { self->after(100, *wait); return; }
      const auto r = self->appearance()->last_export();
      self->check(r.value("success").toBool(), "export: " + r.value("message").toString());
      QStringList fields;
      for (const auto& d : r.value("semanticDiff").toList()) {
        for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
      }
      static const QStringList face{"HairColor", "FaceMorph", "FaceParts", "HeadParts", "HeadTexture", "Weight", "Height", "TextureLighting", "TintLayers"};
      bool only_face = !fields.isEmpty();
      for (const auto& f : fields) only_face &= face.contains(f);
      self->check(only_face, "diff only has face fields: " + fields.join(", "));
      if (!required.isEmpty()) self->check(fields.contains(required), "diff contains " + required);
      self->check(r.value("verificationProblems").toStringList().isEmpty(),
                  "exported FaceGen identical to the preview (" + r.value("verifiedFiles").toStringList().join(", ") + ")");
      self->appearance()->resetFace();
      self->then_idle();
    };
    after(100, *wait);
  }
  void check_hair(const QColor& expected) {
    check_hair_view(expected);
    check_exportable();
  }
  // Editor value == what the viewport draws == what the regenerated FaceGen stores (2x color).
  void check_hair_view(const QColor& expected) {
    check(appearance()->hair_color() == expected,
          QStringLiteral("editor hair color %1 == %2").arg(appearance()->hair_color().name(), expected.name()));
    const auto hair = hair_parts();
    if (hair.isEmpty() && hidden_hair_parts() > 0) {
      // Hair under a headgear slot is listed but not drawn, as in the game: nothing to see in the viewport.
      log(QStringLiteral("viewport: %1 HairTint part(s) hidden under headgear, as in game").arg(hidden_hair_parts()));
    } else {
      check(!hair.isEmpty(), QStringLiteral("scene has %1 visible HairTint part(s)").arg(hair.size()));
    }
    for (const auto& h : hair) {
      const auto row = h.toMap();
      // What the HairTint materials draw (CharacterViewport binds them to scene.hairTint).
      const auto tint = scene()->hair_tint();
      const bool same = std::abs(tint.red() - expected.red()) <= 1 && std::abs(tint.green() - expected.green()) <= 1 &&
                        std::abs(tint.blue() - expected.blue()) <= 1;
      check(same, QStringLiteral("viewport tint of %1 is %2 (expected %3)")
                      .arg(row.value("shape").toString(), tint.name(), expected.name()));
      // Hair with vertex colors: every vertex carries lerp(1, linear tint, green), as Skyrim's Hair shader.
      if (row.value("hairVertexTint").toBool()) {
        auto* g = row.value("geometry").value<QQuick3DGeometry*>();
        if (!g) { check(false, "hair geometry for " + row.value("shape").toString()); continue; }
        auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
        const float lt[3] = {linear(tint.redF()), linear(tint.greenF()), linear(tint.blueF())};
        const auto data = g->vertexData();
        const auto* f = reinterpret_cast<const float*>(data.constData());
        const std::size_t count = static_cast<std::size_t>(data.size()) / sizeof(float) / rendering::kFloatsPerVertex;
        std::size_t off_segment = 0, full = 0, untinted = 0;
        for (std::size_t v = 0; v < count; ++v) {
          const float* c = f + v * rendering::kFloatsPerVertex + rendering::kColorOffset;
          // Shared green weight from the channel with the largest tint delta.
          int k = 0;
          for (int i = 1; i < 3; ++i) if (std::abs(lt[i] - 1) > std::abs(lt[k] - 1)) k = i;
          const float w = std::abs(lt[k] - 1) < 1e-4f ? 1.0f : (c[k] - 1) / (lt[k] - 1);
          bool on = w > -0.01f && w < 1.01f;
          for (int i = 0; i < 3; ++i) on = on && std::abs(c[i] - (1 + w * (lt[i] - 1))) < 0.01f;
          off_segment += on ? 0 : 1;
          full += w > 0.99f ? 1 : 0;
          untinted += w < 0.01f ? 1 : 0;
        }
        check(count > 0 && off_segment == 0 && full > 0,
              QStringLiteral("%1 vertex tint: %2 vertices, %3 fully tinted, %4 untinted (green 0), %5 off lerp(1, tint, green)")
                  .arg(row.value("shape").toString()).arg(count).arg(full).arg(untinted).arg(off_segment));
      }
    }
    // Preview == export: the regenerated FaceGen bytes shown by the viewport carry the CK hair tint.
    const auto preview = appearance()->preview();
    const auto* recipe = appearance()->recipe();
    if (preview.overlay && recipe) {
      const auto it = preview.overlay->find(text::data_path_key(recipe->facegen_mesh_path));
      if (it != preview.overlay->end() && it->second) {
        const auto info = face::inspect_facegen(*it->second);
        check(info.readable, QStringLiteral("regenerated FaceGen readable (%1 shapes)").arg(info.shapes.size()));
        for (const auto& [name, rgb] : info.hair_tints) {
          const bool ok = std::abs(rgb[0] - 2.0f * expected.red() / 255.0f) < 0.01f &&
                          std::abs(rgb[1] - 2.0f * expected.green() / 255.0f) < 0.01f &&
                          std::abs(rgb[2] - 2.0f * expected.blue() / 255.0f) < 0.01f;
          check(ok, QStringLiteral("FaceGen hair tint of %1 = (%2, %3, %4), 2x color").arg(qs(name)).arg(rgb[0]).arg(rgb[1]).arg(rgb[2]));
        }
      }
    }
  }
  void check_exportable() {
    const bool ok = appearance()->exportable();
    check(ok, QStringLiteral("draft exportable (FaceGen %1)").arg(appearance()->facegen_report().value("status").toString()));
    if (ok) return;
    log_errors();
  }
  int log_errors() {
    int errors = 0;
    for (const auto& i : appearance()->issues()) {
      const auto m = i.toMap();
      if (m.value("severity").toString() != "error") continue;
      ++errors;
      log(QStringLiteral("  issue %1: %2 [%3]").arg(m.value("code").toString(), m.value("message").toString(), m.value("subject").toString()));
    }
    return errors;
  }
  // Slider edits on a head whose TRI files do not fit its mesh cannot be built: that must be a
  // refusal with the reason (TriVertexMismatch), never a silent failure or a crash.
  void check_exportable_or_explained() {
    if (appearance()->exportable()) {
      check(true, "draft exportable");
      return;
    }
    bool explained = false;
    for (const auto& i : appearance()->issues()) {
      const auto m = i.toMap();
      explained |= m.value("severity").toString() == "error" && !m.value("message").toString().isEmpty();
    }
    log_errors();
    check(explained, "draft not exportable, with the reason shown in the problems panel");
  }

  // ---------------------------------------------------------------- real input on QML controls
  QQuickItem* find_item(QQuickItem* root, const QString& text) const {
    if (!root || !root->isVisible()) return nullptr;
    if (root->property("text").toString() == text) return root;
    for (auto* child : root->childItems()) {
      if (auto* found = find_item(child, text)) return found;
    }
    return nullptr;
  }
  bool find_any(const QString& text) const {
    if (!window) return false;
    if (find_item(window->contentItem(), text)) return true;
    for (auto* candidate : window->findChildren<QQuickItem*>()) {
      if (candidate->isVisible() && candidate->property("text").toString() == text) return true;
    }
    return false;
  }
  // Mouse press/release at the centre of the visible control showing `text`, through the window's
  // normal event delivery (pointer handlers, focus, the control's own clicked() handler).
  bool click(const QString& text) {
    QQuickItem* item = window ? find_item(window->contentItem(), text) : nullptr;
    // Popups (dialogs, menus) live in the window overlay, outside the content tree.
    if (!item && window) {
      for (auto* candidate : window->findChildren<QQuickItem*>()) {
        if (candidate->isVisible() && candidate->property("text").toString() == text) item = candidate;
      }
    }
    if (!item) {
      check(false, "visible control '" + text + "' to click");
      return false;
    }
    const auto pos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const auto global = window->mapToGlobal(pos);
    QMouseEvent move(QEvent::MouseMove, pos, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &move);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return true;
  }
  // Runs `actions` one after another, `gap` ms apart (chained, so their order is guaranteed).
  void chain(std::vector<std::function<void()>> actions, int gap, std::function<void()> done) {
    auto list = std::make_shared<std::vector<std::function<void()>>>(std::move(actions));
    auto index = std::make_shared<std::size_t>(0);
    auto tick = std::make_shared<std::function<void()>>();
    *tick = [self = shared_from_this(), list, index, gap, done = std::move(done), tick] {
      if (*index >= list->size()) {
        done();
        return;
      }
      (*list)[(*index)++]();
      self->after(gap, *tick);
    };
    (*tick)();
  }

  // The assistant page on screen: inside the window, no text elided or spilling sideways out of the
  // assistant, buttons wide enough for their label. Saves the page as wizard-<size>-<page>.png.
  // Visible descendants of `container` whose text spills sideways out of it, buttons too narrow for their
  // label (the style elides it) and, with `elided_fails`, any elided text (panels elide long names on
  // purpose, so they pass false).
  static QStringList layout_problems(QQuickItem* container, bool elided_fails, int* inspected) {
    QStringList problems;
    // Inside a view that scrolls sideways (a strip of cards with a scroll bar) what lies beyond the edge is reached by scrolling: not a spill.
    std::function<void(QQuickItem*, bool)> visit = [&](QQuickItem* item, bool scrolls) {
      for (auto* child : item->childItems()) {
        if (!child->isVisible() || child->width() <= 0) continue;
        const QByteArray cls = child->metaObject()->className();
        const QRectF r = child->mapRectToItem(container, QRectF(0, 0, child->width(), child->height()));
        const QString text = child->property("text").toString().left(40);
        if (elided_fails && child->property("truncated").toBool()) problems << QStringLiteral("elided %1 '%2'").arg(QString(cls), text);
        const bool child_scrolls = scrolls || (cls.contains("ListView") || cls.contains("Flickable")) && child->property("contentWidth").toReal() > child->width() + 1;
        if (!scrolls && child->metaObject()->indexOfProperty("text") >= 0 && !text.isEmpty() && r.right() > container->width() + 1)
          problems << QStringLiteral("spills %1px '%2' (%3 at x %4, w %5)").arg(int(r.right() - container->width())).arg(text, QString(cls)).arg(int(r.x())).arg(int(r.width()));
        // Buttons with a label (custom-content buttons such as color swatches have none to elide).
        if (!text.isEmpty() && child->metaObject()->indexOfProperty("implicitContentWidth") >= 0 && child->metaObject()->indexOfProperty("checkable") >= 0) {
          const qreal need = child->property("implicitContentWidth").toReal() + child->property("leftPadding").toReal() +
                             child->property("rightPadding").toReal();
          if (need > child->width() + 0.5) problems << QStringLiteral("button '%1' needs %2px, has %3px").arg(text).arg(int(need)).arg(int(child->width()));
        }
        if (inspected) ++*inspected;
        visit(child, child_scrolls);
      }
    };
    visit(container, false);
    problems.removeDuplicates();
    return problems;
  }

  void check_wizard_layout(QObject* wizard, int page, const QString& size) {
    const QString where = QStringLiteral("%1 page %2").arg(size).arg(page + 1);
    // Grab first: a hidden window lays out (polishes) only when it renders, so measuring before the
    // grab would read a stale layout instead of what the user sees.
    const QImage shot = window->grabWindow();
    const QRectF box(wizard->property("x").toReal(), wizard->property("y").toReal(), wizard->property("width").toReal(),
                     wizard->property("height").toReal());
    check(box.width() >= 320 && box.height() >= 320 && box.left() >= 0 && box.top() >= 0 && box.right() <= window->width() + 0.5 && box.bottom() <= window->height() + 0.5,
          QStringLiteral("%1: assistant inside the window (%2,%3 %4x%5)")
              .arg(where).arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height()));
    auto* content = qvariant_cast<QQuickItem*>(wizard->property("contentItem"));
    if (!content) { check(false, where + ": assistant content item"); return; }
    int inspected = 0;
    const auto problems = layout_problems(content, true, &inspected);
    check(problems.isEmpty(), QStringLiteral("%1: %2 items in %3 px, layout %4").arg(where).arg(inspected).arg(content->width()).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
    const auto file = QDir(work_dir).filePath(QStringLiteral("wizard-%1-%2.png").arg(size).arg(page + 1));
    check(shot.save(file), "capture " + file);
  }

  // ---------------------------------------------------------------- scenarios
  void next_scenario() {
    if (finished) return;
    steps.clear();
    ++scenario;
    if (scenario >= scenarios.size()) {
      finish();
      return;
    }
    const auto name = scenarios[scenario];
    log(QStringLiteral("=== scenario %1/%2: %3").arg(scenario + 1).arg(scenarios.size()).arg(name));
    build(name);
    then_idle();
  }

  void build(const QString& name) {
    auto self = shared_from_this();
    auto hair = [self](const QColor& c) {
      self->log(QStringLiteral("setHairColor %1 -> %2").arg(self->appearance()->hair_color().name(), c.name()));
      self->appearance()->setHairColor(c);
    };
    if (name == "hair-single") {
      steps.push_back([self, hair] { hair(QColor("#A53A1E")); self->then_idle(); });
      steps.push_back([self] { self->check_hair(QColor("#A53A1E")); self->step(); });
    } else if (name == "hair-sequence") {
      for (int i = 0; i < 10; ++i) {
        const QColor c(kPalette[i % std::size(kPalette)]);
        steps.push_back([self, hair, c] { hair(c); self->then_idle(); });
        steps.push_back([self, c] { self->check_hair(c); self->step(); });
      }
    } else if (name == "hair-rapid") {
      // Faster than the build debounce and the viewport reload: superseded results must be dropped.
      steps.push_back([self, hair] {
        std::vector<std::function<void()>> actions;
        for (int i = 0; i < 40; ++i) actions.push_back([hair, i] { hair(QColor(kPalette[(i * 5) % std::size(kPalette)])); });
        self->chain(std::move(actions), 15, [self] { self->then_idle(); });
      });
      steps.push_back([self] { self->check_hair(QColor(kPalette[(39 * 5) % std::size(kPalette)])); self->step(); });
    } else if (name == "ui-hair-clicks") {
      // The user's path: the "Pelo/Ojos" tab and the palette buttons, with real mouse events.
      static const std::pair<const char*, const char*> palette[] = {
          {"Pelirrojo / Cobrizo", "#A53A1E"}, {"Negro Azabache", "#111111"}, {"Rubio Dorado", "#E6C280"},
          {"Azul Mágico", "#2980B9"}, {"Blanco Platino", "#ECECEC"}, {"Violeta Nocturno", "#8E44AD"},
          {"Rojo Fuego", "#C0392B"}, {"Castaño Medio", "#5A3825"}, {"Gris / Ceniza", "#808080"}, {"Rubio Ceniza", "#C2B280"}};
      steps.push_back([self] { self->click(QStringLiteral("Pelo/Ojos")); self->after(300, [self] { self->step(); }); });
      for (const auto& [label, color] : palette) {
        const QString text = QString::fromUtf8(label);
        const QColor c(color);
        steps.push_back([self, text] { self->log("click '" + text + "'"); self->click(text); self->then_idle(); });
        steps.push_back([self, c] { self->check_hair(c); self->step(); });
      }
      steps.push_back([self] {
        std::vector<std::function<void()>> actions;
        for (int i = 0; i < 30; ++i) {
          actions.push_back([self, i] { self->click(QString::fromUtf8(palette[i % std::size(palette)].first)); });
        }
        self->log("30 palette clicks, 40 ms apart");
        self->chain(std::move(actions), 40, [self] { self->then_idle(); });
      });
      steps.push_back([self] { self->check_hair(QColor(palette[29 % std::size(palette)].second)); self->step(); });
    } else if (name == "color-dialog") {
      // "Selector libre…": the Quick Dialogs color picker, accepted with its own OK button.
      steps.push_back([self] { self->click(QStringLiteral("Pelo/Ojos")); self->after(300, [self] { self->step(); }); });
      for (const auto* color : {"#3E8E41", "#B03A2E", "#F4D03F"}) {
        const QColor c(color);
        steps.push_back([self, c] {
          self->click(QString::fromUtf8("Selector libre…"));
          self->after(400, [self, c] {
            // The declared ColorDialog (QQuickPlatformColorDialog) and its QML implementation popup.
            QObject* popup = nullptr;
            for (auto* o : self->window->findChildren<QObject*>()) {
              const QByteArray cls = o->metaObject()->className();
              if (cls.startsWith("ColorDialog_QMLTYPE") && o->property("visible").toBool()) popup = o;
            }
            self->check(popup != nullptr, "color dialog open");
            if (!popup) { self->step(); return; }
            // What the picker writes when the user drags in it; OK then reports it as selectedColor.
            popup->setProperty("color", c);
            self->log("color dialog: " + c.name() + ", OK");
            bool clicked = false;
            for (const auto* text : {"&OK", "OK", "&Ok", "Ok"}) {
              if (!clicked && self->find_any(QString::fromLatin1(text))) clicked = self->click(QString::fromLatin1(text));
            }
            if (!clicked) QMetaObject::invokeMethod(popup, "accept");
            self->then_idle();
          });
        });
        steps.push_back([self, c] { self->check_hair(c); self->step(); });
      }
    } else if (name == "viewport-during-build") {
      // Orbit drags and picking clicks on the 3D view while the head is regenerated and reinstalled.
      steps.push_back([self, hair] {
        QQuickItem* view = nullptr;
        for (auto* item : self->window->findChildren<QQuickItem*>("characterView3D")) {
          if (item->isVisible() && item->width() > 50) view = item;
        }
        self->check(view != nullptr, "visible 3D view");
        if (!view) { self->step(); return; }
        QPointer<QQuickItem> target(view);
        std::vector<std::function<void()>> actions;
        for (int i = 0; i < 24; ++i) {
          actions.push_back([self, hair, target, i] {
            if (!target) return;
            if (i % 4 == 0) hair(QColor(kPalette[i % std::size(kPalette)]));
            const auto centre = target->mapToScene(QPointF(target->width() * (0.4 + 0.02 * (i % 6)), target->height() * 0.35));
            const auto global = self->window->mapToGlobal(centre);
            const auto button = i % 2 ? Qt::LeftButton : Qt::RightButton;
            QMouseEvent press(QEvent::MouseButtonPress, centre, global, button, button, Qt::NoModifier);
            QCoreApplication::sendEvent(self->window, &press);
            const auto moved = centre + QPointF(i % 2 ? 0 : 25, i % 2 ? 0 : 8);
            QMouseEvent move(QEvent::MouseMove, moved, self->window->mapToGlobal(moved), Qt::NoButton, button, Qt::NoModifier);
            QCoreApplication::sendEvent(self->window, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, moved, self->window->mapToGlobal(moved), button, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(self->window, &release);
          });
        }
        self->chain(std::move(actions), 60, [self] { self->then_idle(); });
      });
      steps.push_back([self] { self->check_hair(QColor(kPalette[20 % std::size(kPalette)])); self->step(); });
    } else if (name == "early-edit") {
      // Edits while the NPC is still loading: recipe request, FaceGen analysis, first scene load.
      steps.push_back([self, hair] {
        self->log("selectNpc " + self->primary_npc + " and edit while it loads");
        self->studio->selectNpc(self->primary_npc);
        std::vector<std::function<void()>> actions;
        for (const auto* c : {"#A53A1E", "#2980B9", "#ECECEC", "#111111", "#C0392B", "#8E44AD", "#5A3825"}) {
          actions.push_back([self, hair, c] {
            self->log(QStringLiteral("loaded=%1 building=%2 sceneLoading=%3").arg(self->appearance()->loaded())
                          .arg(self->appearance()->building()).arg(self->scene()->loading()));
            hair(QColor(c));
          });
        }
        self->chain(std::move(actions), 150, [self] { self->then_idle(); });
      });
      steps.push_back([self, hair] {
        // Edits requested before the face was read are refused; once loaded, one more must work.
        hair(QColor("#5A3825"));
        self->then_idle();
      });
      steps.push_back([self] { self->check_hair(QColor("#5A3825")); self->step(); });
    } else if (name == "sliders") {
      steps.push_back([self] {
        const QStringList keys{"NoseLongVsShort", "JawUpVsDown", "CheeksUpVsDown", "EyesInVsOut", "LipsUpVsDown"};
        std::vector<std::function<void()>> actions;
        for (int i = 0; i < 50; ++i) {
          actions.push_back([self, keys, i] { self->appearance()->setSlider(keys[i % keys.size()], std::sin(i * 0.37f)); });
        }
        self->chain(std::move(actions), 20, [self] { self->then_idle(); });
      });
      steps.push_back([self] { self->check_exportable_or_explained(); self->step(); });
    } else if (name == "undo-redo") {
      steps.push_back([self] {
        for (int i = 0; i < 6; ++i) self->after(30 * i, [self] { self->appearance()->undo(); });
        for (int i = 0; i < 4; ++i) self->after(180 + 30 * i, [self] { self->appearance()->redo(); });
        self->after(400, [self] { self->then_idle(); });
      });
      steps.push_back([self] {
        const auto c = self->appearance()->hair_color();
        if (c.isValid()) self->check_hair_view(c);
        self->check_exportable_or_explained();
        self->step();
      });
    } else if (name == "reset") {
      steps.push_back([self, hair] { hair(QColor("#2980B9")); self->after(40, [self] { self->appearance()->resetFace(); self->then_idle(); }); });
      steps.push_back([self] {
        self->check(!self->appearance()->has_face_edits(), QStringLiteral("face equals the record after reset"));
        self->check(self->appearance()->exportable(), QStringLiteral("reset draft is exportable (nothing to regenerate)"));
        self->step();
      });
    } else if (name == "original-toggle") {
      steps.push_back([self, hair] {
        hair(QColor("#C0392B"));
        for (int i = 0; i < 12; ++i) self->after(25 * i, [self, i] { self->scene()->setOriginal(i % 2 == 0); });
        self->after(25 * 12 + 30, [self] { self->scene()->setOriginal(false); self->then_idle(); });
      });
      steps.push_back([self] { self->check_hair(QColor("#C0392B")); self->step(); });
    } else if (name == "outfit-during-build") {
      steps.push_back([self, hair] {
        const auto pieces = self->studio->outfit()->pieces();
        hair(QColor("#8E44AD"));
        self->after(10, [self, pieces] {
          if (pieces.isEmpty()) {
            self->log("NPC has no outfit pieces: skipping the outfit edit");
          } else {
            const auto key = pieces.first().toMap().value("formKey").toString();
            self->log("removePiece " + key + " while the head rebuilds");
            self->studio->outfit()->removePiece(key);
            self->after(30, [self] { self->studio->outfit()->undo(); });
          }
          self->then_idle();
        });
      });
      steps.push_back([self] { self->check_hair(QColor("#8E44AD")); self->step(); });
    } else if (name == "material-preview-block") {
      steps.push_back([self] {
        const auto pieces = self->studio->outfit()->pieces();
        self->check(!pieces.isEmpty(), "NPC has an outfit piece for material validation");
        if (pieces.isEmpty()) { self->step(); return; }
        self->baseline_outfit_exportable = self->studio->outfit()->exportable();
        const auto key = pieces.first().toMap().value("formKey").toString();
        self->studio->outfit()->setPieceMaterial(key, QColor("#402010"), {}, {}, "Unverified tint");
        self->after(1000, [self] { self->step(); });
      });
      steps.push_back([self] {
        auto* outfit = self->studio->outfit();
        self->check(!outfit->exportable(), "material request blocks outfit export before clicking Export");
        self->check(!outfit->compatibility_report().value("canExport").toBool(), "Guardian blocks material request");
        const auto issues = outfit->validation().value("issues").toList();
        const auto has_issue = std::any_of(issues.begin(), issues.end(), [](const QVariant& issue) {
          return issue.toMap().value("code") == "MaterialVariantPreviewOnly";
        });
        self->check(has_issue, "validation explains missing DDS and NIF mapping");
        bool refused = false;
        const auto connection = QObject::connect(outfit, &OutfitEditor::exportFailed, self->studio,
                                                  [&refused](const QString&, const QVariantList&) { refused = true; });
        outfit->exportPatch("SNS_UnverifiedMaterial.esp", self->work_dir, false, false, {}, false, false);
        QObject::disconnect(connection);
        self->check(refused, "Mode A refuses material request before writing a mod");
        const auto key = outfit->pieces().first().toMap().value("formKey").toString();
        outfit->clearPieceMaterial(key);
        self->after(1000, [self] { self->step(); });
      });
      steps.push_back([self] {
        const auto pieces = self->studio->outfit()->pieces();
        self->check(!pieces.first().toMap().value("hasMaterial").toBool(), "restoring original material clears request");
        const auto issues = self->studio->outfit()->validation().value("issues").toList();
        const auto has_material_issue = std::any_of(issues.begin(), issues.end(), [](const QVariant& issue) {
          return issue.toMap().value("code") == "MaterialVariantPreviewOnly";
        });
        self->check(!has_material_issue, "material issue disappears after restoring original");
        self->check(self->studio->outfit()->exportable() == self->baseline_outfit_exportable,
                    "outfit returns to its prior exportability (other slot issues remain)");
        self->step();
      });
    } else if (name == "switch-npc") {
      steps.push_back([self, hair] {
        hair(QColor("#ECECEC"));
        self->after(5, [self] {
          self->log("selectNpc " + self->second_npc + " during the rebuild");
          self->studio->selectNpc(self->second_npc);
          self->then_idle();
        });
      });
      steps.push_back([self, hair] {
        self->check(self->appearance()->npc_form_key().compare(self->second_npc, Qt::CaseInsensitive) == 0,
                    "editor follows the second NPC: " + self->appearance()->npc_form_key());
        if (self->appearance()->loaded()) hair(QColor("#3B2219"));
        self->after(5, [self] {
          self->log("selectNpc " + self->primary_npc + " during the rebuild");
          self->studio->selectNpc(self->primary_npc);
          self->then_idle();
        });
      });
      steps.push_back([self] {
        self->check(self->appearance()->npc_form_key() == self->primary_npc, "back on " + self->appearance()->npc_form_key());
        self->check(!self->appearance()->has_face_edits(), "a newly selected NPC starts with the record's face");
        self->step();
      });
    } else if (name == "workspace-during-build") {
      // "Closing" the Character Creator: switch to the outfit studio while the head rebuilds, then back.
      steps.push_back([self, hair] {
        hair(QColor("#16A085"));
        self->after(10, [self] {
          self->log("switch to the outfit studio during the rebuild");
          self->click(QString::fromUtf8("👗 ESTUDIO DE ATUENDOS"));
          self->then_idle();
        });
      });
      steps.push_back([self] {
        self->check(!self->scene()->active(), "main viewport inactive (no rebuilds) while the outfit studio is shown");
        self->click(QString::fromUtf8("🎭 PERSONALIZAR PERSONAJE"));
        self->then_idle();
      });
      steps.push_back([self] { self->check_hair(QColor("#16A085")); self->step(); });
    } else if (name == "replacer-facegen") {
      // A replacer's FaceGen: a hair color edit keeps it as it is (Hair Color Safe Mode) and stays exportable.
      steps.push_back([self, hair] { hair(QColor("#7B241C")); self->then_idle(); });
      steps.push_back([self] {
        self->log("geometry mode " + self->appearance()->geometry_mode() + ": " + self->appearance()->geometry_mode_reason());
        self->check_hair_safe(QColor("#7B241C"));
        self->step();
      });
    } else if (name == "hair-safe") {
      // Hair Color Safe Mode on the NPC's real winning FaceGen: first edit, twenty colors, undo/redo,
      // export, and the exported NIF and plugin read back.
      steps.push_back([self, hair] { hair(QColor("#A53A1E")); self->then_idle(); });
      steps.push_back([self] { self->check_hair_safe(QColor("#A53A1E")); self->step(); });
      for (int i = 0; i < 20; ++i) {
        // 8-bit RGB like the editor's color: QColor::operator== compares spec and 16-bit components.
        const auto hsv = QColor::fromHsv((i * 37) % 360, 160 + (i * 5) % 90, 60 + (i * 9) % 180);
        const QColor c(hsv.red(), hsv.green(), hsv.blue());
        steps.push_back([self, hair, c] { hair(c); self->then_idle(); });
        steps.push_back([self, c] { self->check_hair_safe(c); self->step(); });
      }
      steps.push_back([self] {
        self->appearance()->undo();
        self->appearance()->redo();
        self->then_idle();
      });
      steps.push_back([self] { self->check_hair_safe(self->appearance()->hair_color()); self->step(); });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("hair-safe-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        self->log("exportFaceOverhaul to " + dir);
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_HairSafe.esp"), dir, true, true);  // ZIP: the in-game test pack
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "export: " + r.value("message").toString());
          const auto mod_dir = r.value("modDirectory").toString();
          const auto* recipe = self->appearance()->recipe();
          const auto original = self->appearance()->winning_facegen();
          if (!recipe || !original || mod_dir.isEmpty()) {
            self->check(false, "exported mod directory and original FaceGen available");
            self->step();
            return;
          }
          QFile nif(QDir(mod_dir).filePath(qs(text::data_path_key(recipe->facegen_mesh_path)).replace('\\', '/')));
          const bool opened = nif.open(QIODevice::ReadOnly);
          self->check(opened, "exported FaceGen present: " + nif.fileName());
          if (opened) {
            const auto bytes = nif.readAll().toStdString();
            const auto integrity = face::verify_hair_tint_integrity(*original, bytes);
            self->check(integrity.byte_identical_outside_hair_tint && integrity.unchanged_except_hair_tint,
                        "exported FaceGen == winning FaceGen except HairTint: " + qs(integrity.summary()));
          }
          QFile tint(QDir(mod_dir).filePath(qs(text::data_path_key(recipe->facegen_tint_path)).replace('\\', '/')));
          self->check(!tint.exists(), "no FaceTint exported for a hair color edit");
          QFile manifest(QDir(mod_dir).filePath("manifest.json"));
          QStringList changed, above;
          if (manifest.open(QIODevice::ReadOnly)) {
            const auto faces = QJsonDocument::fromJson(manifest.readAll()).object().value("faces").toArray();
            for (const auto& f : faces) {
              for (const auto& field : f.toObject().value("changedFields").toArray()) changed << field.toString();
              for (const auto& mod : f.toObject().value("installAbove").toArray()) above << mod.toString();
            }
          }
          self->check(changed == QStringList{"HCLF"}, "record fields changed by the plugin: " + changed.join(", ") + " (expected HCLF)");
          // The mod must win over every MO2 mod with this NPC's loose FaceGen/FaceTint (archives never compete).
          const auto competitors = self->appearance()->face_compatibility().value("provenance").toMap().value("faceGenCompetitors").toStringList();
          self->check(above == competitors, "manifest installAbove = MO2 mods with the NPC's loose FaceGen: [" + above.join(", ") + "] (diagnosis: [" +
                                                competitors.join(", ") + "])");
          self->step();
        };
        self->after(100, *wait);
      });
    } else if (name == "easy-mod") {
      // Easy Mod Creator: the plan the assistant shows, the assistant on the real window, and a mod
      // created from that plan with author, description and ZIP, checked through the worker's report.
      auto wizard = [self]() -> QObject* {
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (QByteArray(o->metaObject()->className()).startsWith("ExportWizardDialog_QMLTYPE")) return o;
        }
        return nullptr;
      };
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, wizard] {
        const auto plan = self->appearance()->easy_mod_plan();
        self->check(plan.value("mode").toString().isEmpty() && !plan.value("canExport").toBool(),
                    "no edits: nothing to export (" + plan.value("summary").toString() + ")");
        auto* w = wizard();
        self->check(w != nullptr, "assistant present in the window");
        if (w) QMetaObject::invokeMethod(w, "open");
        self->after(300, [self, w] {
          if (!w) { self->step(); return; }
          self->check(w->property("visible").toBool(), "assistant opens");
          // Every page of the assistant renders, one after another, ending on "Comprobación".
          std::vector<std::function<void()>> pages;
          for (const int page : {1, 2, 4, 3}) {
            pages.push_back([self, w, page] {
              self->log(QStringLiteral("assistant page %1").arg(page + 1));
              w->setProperty("step", page);
            });
          }
          self->chain(std::move(pages), 150, [self, w] {
            self->check(w->property("step").toInt() == 3, "assistant pages render");
            self->step();
          });
        });
      });
      const QColor color("#3B7A57");
      steps.push_back([self, hair, color] { hair(color); self->then_idle(); });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("easy-mod-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, wizard, dir] {
        const auto plan = self->appearance()->easy_mod_plan();
        const auto changes = plan.value("changes").toList();
        self->check(plan.value("mode").toString() == "face-overhaul", "hair color edit plans " + plan.value("mode").toString() + " (expected face-overhaul)");
        self->check(plan.value("canExport").toBool(), "plan can export: " + plan.value("summary").toString());
        self->check(changes.size() == 1 && changes.front().toMap().value("key").toString() == "hair", "the only change is the hair color");
        if (auto* w = wizard()) {
          self->check(w->property("plan").toMap().value("mode").toString() == "face-overhaul", "the assistant shows the same plan");
        }
        for (const auto& c : plan.value("checks").toList()) {
          const auto m = c.toMap();
          self->log(QStringLiteral("  check %1 %2: %3").arg(m.value("state").toString(), m.value("key").toString(), m.value("label").toString()));
        }
        self->log("Easy Mod Creator export to " + dir);
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_EasyMod.esp"), dir, true, true, QStringLiteral("SNS Tester"),
                                               QStringLiteral("Easy Mod Creator stress"));
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, wizard] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "export: " + r.value("message").toString());
          const auto diff = r.value("semanticDiff").toList();
          QStringList fields;
          for (const auto& d : diff) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          self->check(fields == QStringList{"HairColor"}, "semantic diff: " + fields.join(", ") + " (expected HairColor)");
          self->check(!r.value("files").toList().isEmpty(), QStringLiteral("files listed: %1").arg(r.value("files").toList().size()));
          bool zip_verified = false;
          for (const auto& v : r.value("verification").toList()) zip_verified |= v.toString().startsWith(QStringLiteral("ZIP releído"));
          self->check(zip_verified, "ZIP verified by the worker");
          self->check(!r.value("pendingVerification").toList().isEmpty(), "in-game checks listed as pending");
          self->check(QFile::exists(r.value("zipPath").toString()), "ZIP written: " + r.value("zipPath").toString());
          auto* w = wizard();
          if (!w) { self->appearance()->resetFace(); self->then_idle(); return; }
          // The assistant shows the result on its last page (the export ran through the plan's exporter,
          // which the assistant listens to only when it started it, so hand it the result like it would).
          w->setProperty("modResult", r);
          w->setProperty("step", 4);
          self->after(300, [self, w] {
            self->check(w->property("step").toInt() == 4 && w->property("visible").toBool(), "assistant shows the created mod");
            QMetaObject::invokeMethod(w, "close");
            self->appearance()->resetFace();
            self->then_idle();
          });
        };
        self->after(100, *wait);
      });
    } else if (name == "easy-mod-layout") {
      // Easy Mod Creator at the current window size: every page (with a real plan and, on the last
      // page, a real export result) must fit the window, wrap instead of spilling out of the
      // assistant, and show full button labels. Each page is saved as a capture for a human review.
      auto wizard = [self]() -> QObject* {
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (QByteArray(o->metaObject()->className()).startsWith("ExportWizardDialog_QMLTYPE")) return o;
        }
        return nullptr;
      };
      const QColor color("#3B7A57");
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, hair, color] { hair(color); self->then_idle(); });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("easy-mod-layout-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        // A real result for page 5 (paths, masters, semantic diff, files): the longest content it shows.
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_EasyLayout.esp"), dir, true, false, QStringLiteral("SNS Tester"), {});
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          self->check(self->appearance()->last_export().value("success").toBool(),
                      "export for the result page: " + self->appearance()->last_export().value("message").toString());
          self->step();
        };
        self->after(100, *wait);
      });
      steps.push_back([self, wizard] {
        auto* w = wizard();
        self->check(w != nullptr, "assistant present in the window");
        if (!w) { self->step(); return; }
        QMetaObject::invokeMethod(w, "open");
        const auto size = QStringLiteral("%1x%2").arg(self->window->width()).arg(self->window->height());
        std::vector<std::function<void()>> pages;
        for (int page = 0; page < 5; ++page) {
          pages.push_back([self, w, page] {
            if (page == 4) w->setProperty("modResult", self->appearance()->last_export());
            w->setProperty("step", page);
          });
          pages.push_back([self, w, page, size] { self->check_wizard_layout(w, page, size); });
        }
        self->chain(std::move(pages), 400, [self, w] {
          QMetaObject::invokeMethod(w, "close");
          self->appearance()->resetFace();
          self->then_idle();
        });
      });
    } else if (name == "easy-mod-outfit") {
      // Easy Mod Creator, modes A and C: an outfit edit alone, then the outfit plus a hair color. The
      // export runs through the exporter the plan names; the worker's semantic diff must list exactly
      // the fields of those edits. An outfit the validator rejects must block the plan instead.
      auto fields_of = [](const QVariantMap& r) {
        QStringList fields;
        for (const auto& d : r.value("semanticDiff").toList()) {
          for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
        }
        fields.sort();
        return fields;
      };
      auto wait_outfit = [](const std::shared_ptr<Stress>& self, std::function<void()> next) {
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [self, poll, next] {
          if (self->studio->outfit()->validating() || self->busy()) { self->after(100, *poll); return; }
          next();
        };
        self->after(200, *poll);
      };
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, wait_outfit] {
        const auto pieces = self->studio->outfit()->pieces();
        if (pieces.size() < 2) {
          self->log("NPC outfit has fewer than 2 pieces: mode A/C export not exercised");
          self->step();
          return;
        }
        const auto key = pieces.first().toMap().value("formKey").toString();
        self->log("removePiece " + key);
        self->studio->outfit()->removePiece(key);
        wait_outfit(self, [self] { self->step(); });
      });
      const auto dir_a = QDir(work_dir).filePath(QStringLiteral("easy-mod-a-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir_a, fields_of] {
        auto* outfit = self->studio->outfit();
        if (outfit->pieces().isEmpty() || !outfit->can_undo()) {
          self->step();  // no outfit edit was made (fewer than 2 pieces)
          return;
        }
        const auto plan = self->appearance()->easy_mod_plan();
        self->check(plan.value("mode").toString() == "outfit-patch", "outfit edit plans " + plan.value("mode").toString() + " (expected outfit-patch)");
        if (!outfit->exportable()) {
          for (const auto& i : outfit->validation().value("issues").toList()) {
            const auto m = i.toMap();
            self->log(QStringLiteral("  outfit issue %1 %2: %3 [%4]").arg(m.value("severity").toString(), m.value("code").toString(),
                                                                         m.value("message").toString(), m.value("subject").toString()));
          }
          self->check(!plan.value("canExport").toBool(), "an outfit the validator rejects blocks the plan");
          self->log("outfit not exportable for this NPC: mode A export skipped");
          self->step();
          return;
        }
        self->check(plan.value("canExport").toBool(), "plan can export: " + plan.value("summary").toString());
        auto done = std::make_shared<bool>(false);
        auto result = std::make_shared<QVariantMap>();
        auto c1 = std::make_shared<QMetaObject::Connection>();
        auto c2 = std::make_shared<QMetaObject::Connection>();
        *c1 = QObject::connect(outfit, &OutfitEditor::exported, self->studio, [done, result](const QVariantMap& r) { *result = r; *done = true; });
        *c2 = QObject::connect(outfit, &OutfitEditor::exportFailed, self->studio, [done, result](const QString& m, const QVariantList&) {
          (*result)["failure"] = m;
          *done = true;
        });
        self->log("Easy Mod Creator mode A export to " + dir_a);
        outfit->exportPatch(QStringLiteral("SNS_EasyOutfit.esp"), dir_a, true, true, QStringLiteral("Easy outfit"), false, false, false, {},
                            QStringLiteral("SNS Tester"));
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, done, result, c1, c2, fields_of] {
          if (!*done) { self->after(100, *wait); return; }
          QObject::disconnect(*c1);
          QObject::disconnect(*c2);
          self->check(!result->contains("failure"), "mode A export: " + result->value("failure").toString());
          self->check(fields_of(*result) == QStringList{"DefaultOutfit"}, "mode A semantic diff: " + fields_of(*result).join(", ") + " (expected DefaultOutfit)");
          self->step();
        };
        self->after(100, *wait);
      });
      const QColor color("#6C3483");
      steps.push_back([self, hair, color] { hair(color); self->then_idle(); });
      const auto dir_c = QDir(work_dir).filePath(QStringLiteral("easy-mod-c-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir_c, fields_of] {
        const auto plan = self->appearance()->easy_mod_plan();
        const auto changes = plan.value("changes").toList();
        const bool outfit_edit = std::any_of(changes.begin(), changes.end(),
                                             [](const QVariant& c) { return c.toMap().value("key").toString() == "outfit"; });
        if (!outfit_edit) {
          self->check(plan.value("mode").toString() == "face-overhaul", "hair edit without outfit edit plans face-overhaul");
          self->step();
          return;
        }
        self->check(plan.value("mode").toString() == "complete-overhaul", "hair + outfit plans " + plan.value("mode").toString() + " (expected complete-overhaul)");
        if (!plan.value("canExport").toBool()) {
          self->check(!self->studio->outfit()->exportable(), "mode C is blocked only because the outfit is not exportable");
          self->step();
          return;
        }
        self->log("Easy Mod Creator mode C export to " + dir_c);
        self->appearance()->exportCompleteOverhaul(QStringLiteral("SNS_EasyComplete.esp"), dir_c, true, true, QStringLiteral("SNS Tester"), {});
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, fields_of] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "mode C export: " + r.value("message").toString());
          self->check(fields_of(r) == QStringList({"DefaultOutfit", "HairColor"}),
                      "mode C semantic diff: " + fields_of(r).join(", ") + " (expected DefaultOutfit, HairColor)");
          self->step();
        };
        self->after(100, *wait);
      });
      steps.push_back([self] {
        self->studio->outfit()->undo();  // the hair color, then the removed piece (shared history)
        self->studio->outfit()->undo();
        self->appearance()->resetFace();
        self->then_idle();
      });
    } else if (name == "head-part" || name == "head-part-eyes") {
      const QString part_type = name == "head-part" ? QStringLiteral("Hair") : QStringLiteral("Eyes");
      // Beta item 5: change a head part to one valid for the NPC (worker query), rebuild, export, and
      // check through the worker's semantic diff that PNAM changed and nothing outside the face did.
      // A head whose diagnosis blocks head parts must refuse the edit instead.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, part_type] {
        if (self->appearance()->operation_blocked(QStringLiteral("head-parts"))) {
          const auto before = self->appearance()->head_parts();
          self->appearance()->setHeadPart(QStringLiteral("000001:Skyrim.esm"), QStringLiteral("Hair"));
          self->check(self->appearance()->head_parts() == before, "head parts blocked for this head: the edit is refused");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->appearance()->searchHeadParts(part_type);
        auto poll = std::make_shared<std::function<void()>>();
        auto started = self->clock.elapsed();
        *poll = [self, poll, started, part_type] {
          if (self->appearance()->head_part_options().isEmpty() && self->clock.elapsed() - started < 15000) {
            self->after(100, *poll);
            return;
          }
          const auto options = self->appearance()->head_part_options();
          self->check(!options.isEmpty(), part_type + QStringLiteral(" head parts valid for this NPC: %1").arg(options.size()));
          QStringList current;
          for (const auto& h : self->appearance()->head_parts()) current << h.toMap().value("formKey").toString();
          const bool same_type = std::all_of(options.begin(), options.end(), [&](const QVariant& o) {
            return o.toMap().value("type").toString().compare(part_type, Qt::CaseInsensitive) == 0;
          });
          self->check(same_type, "every option is of type " + part_type);
          // Prefer a vanilla part: mod parts can ship broken TRI files (the build then refuses them).
          QString pick;
          for (const bool vanilla_only : {true, false}) {
            for (const auto& o : options) {
              const auto m = o.toMap();
              const auto fk = m.value("formKey").toString();
              if (current.contains(fk) || (vanilla_only && m.value("plugin").toString().compare("Skyrim.esm", Qt::CaseInsensitive) != 0)) continue;
              pick = fk;
              break;
            }
            if (!pick.isEmpty()) break;
          }
          self->check(!pick.isEmpty(), "a different " + part_type + " to try: " + pick);
          if (!pick.isEmpty()) {
            self->log("setHeadPart " + pick + " (" + part_type + ")");
            self->appearance()->setHeadPart(pick, part_type);
          }
          self->then_idle();
        };
        self->after(100, *poll);
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("head-part-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        const auto plan = self->appearance()->easy_mod_plan();
        self->check(plan.value("mode").toString() == "face-overhaul", "head part edit plans " + plan.value("mode").toString());
        self->check_exportable();
        self->log("exportFaceOverhaul to " + dir);
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_HeadPart.esp"), dir, true, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "head part export: " + r.value("message").toString());
          QStringList fields;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          self->check(fields.contains("HeadParts"), "semantic diff contains HeadParts: " + fields.join(", "));
          const QStringList face{"HairColor", "FaceMorph", "FaceParts", "HeadParts", "HeadTexture", "Weight", "Height", "TextureLighting", "TintLayers"};
          bool only_face = true;
          for (const auto& f : fields) only_face &= face.contains(f);
          self->check(only_face, "only face fields changed");
          self->check(r.value("verificationProblems").toStringList().isEmpty(),
                      "exported FaceGen identical to the preview (" + r.value("verifiedFiles").toStringList().join(", ") + ")");
          self->appearance()->resetFace();
          self->then_idle();
        };
        self->after(100, *wait);
      });
    } else if (name == "sliders-export") {
      // Face sliders exported: the record's NAM9 (FaceMorph) changes, the FaceGen is regenerated and
      // identical to the preview, nothing outside the face changes. Heads whose diagnosis blocks
      // sliders must refuse the edit instead.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self] {
        if (self->appearance()->operation_blocked(QStringLiteral("face-morphs"))) {
          const auto before = self->appearance()->morphs();
          self->appearance()->setSlider(QStringLiteral("NoseLongVsShort"), 0.8f);
          self->check(self->appearance()->morphs() == before, "sliders blocked for this head: the edit is refused");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->appearance()->setSlider(QStringLiteral("NoseLongVsShort"), 0.8f);
        self->appearance()->setSlider(QStringLiteral("JawNarrowVsWide"), -0.5f);
        self->then_idle();
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("sliders-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        self->check(self->appearance()->easy_mod_plan().value("mode").toString() == "face-overhaul", "slider edit plans face-overhaul");
        self->check_exportable();
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_Sliders.esp"), dir, true, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "sliders export: " + r.value("message").toString());
          QStringList fields;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          self->check(fields.contains("FaceMorph"), "semantic diff contains FaceMorph: " + fields.join(", "));
          const QStringList face{"HairColor", "FaceMorph", "FaceParts", "HeadParts", "HeadTexture", "Weight", "Height", "TextureLighting", "TintLayers"};
          bool only_face = true;
          for (const auto& f : fields) only_face &= face.contains(f);
          self->check(only_face, "only face fields changed");
          self->check(r.value("verificationProblems").toStringList().isEmpty(),
                      "exported FaceGen identical to the preview (" + r.value("verifiedFiles").toStringList().join(", ") + ")");
          self->appearance()->resetFace();
          self->then_idle();
        };
        self->after(100, *wait);
      });
    } else if (name == "skin-export") {
      // Skin tone exported: QNAM changes and the mod carries the regenerated FaceTint DDS, identical
      // to the previewed bytes, next to the FaceGen NIF.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self] {
        if (self->appearance()->operation_blocked(QStringLiteral("skin-tone"))) {
          const auto before = self->appearance()->skin_tone();
          self->appearance()->setSkinTone(QColor("#C68E6B"));
          self->check(self->appearance()->skin_tone() == before, "skin tone blocked for this head (own face texture): the edit is refused");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->appearance()->setSkinTone(QColor("#C68E6B"));
        self->then_idle();
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("skin-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        if (!self->appearance()->exportable()) {
          self->check_exportable_or_explained();
          self->appearance()->resetFace();
          self->then_idle();
          return;
        }
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_Skin.esp"), dir, true, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "skin export: " + r.value("message").toString());
          QStringList fields;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          self->check(fields.contains("TextureLighting"), "semantic diff contains TextureLighting (QNAM): " + fields.join(", "));
          const auto verified = r.value("verifiedFiles").toStringList();
          const bool tint = std::any_of(verified.begin(), verified.end(), [](const QString& f) { return f.endsWith(".dds", Qt::CaseInsensitive); });
          self->check(tint, "FaceTint DDS exported and identical to the preview (" + verified.join(", ") + ")");
          self->check(r.value("verificationProblems").toStringList().isEmpty(), "no verification problems");
          self->appearance()->resetFace();
          self->then_idle();
        };
        self->after(100, *wait);
      });
    } else if (name == "preset-export") {
      // A RaceMenu preset from the profile (SKSE/Plugins/CharGen/Presets): every preset's verdict is
      // logged; the first exportable one is applied and exported, and the worker's diff may only
      // contain face fields, with the FaceGen identical to the preview.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self] {
        self->appearance()->loadPresetsFromVfs();
        QString pick, pick_name;
        int count = 0;
        for (const auto& p : self->appearance()->presets_list()) {
          const auto m = p.toMap();
          const auto c = self->appearance()->evaluatePresetCompatibility(m.value("diskPath").toString());
          ++count;
          self->log(QStringLiteral("  preset %1 (%2): %3").arg(m.value("name").toString(), m.value("sourceMod").toString(), c.value("verdict").toString()));
          if (pick.isEmpty() && c.value("exportable").toBool()) {
            pick = m.value("diskPath").toString();
            pick_name = m.value("name").toString();
          }
        }
        self->check(count > 0, QStringLiteral("RaceMenu presets in the profile: %1").arg(count));
        if (!self->appearance()->donor_blocked_reason().isEmpty()) {
          // Protected head: every preset must be refused and applying one must change nothing.
          self->check(pick.isEmpty(), "no preset is exportable on a protected head");
          self->head_part_skip = true;
          if (!self->appearance()->presets_list().isEmpty()) {
            self->appearance()->applyJslotPreset(self->appearance()->presets_list().front().toMap().value("diskPath").toString());
          }
          self->after(800, [self] {
            self->check(!self->appearance()->has_face_edits(), "protected head unchanged after a refused preset");
            self->then_idle();
          });
          return;
        }
        if (pick.isEmpty()) {
          self->log("no preset is exportable for this NPC: nothing applied");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->log("applyJslotPreset " + pick_name);
        self->appearance()->applyJslotPreset(pick);
        self->then_idle();
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("preset-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        if (!self->appearance()->exportable()) {
          self->check_exportable_or_explained();
          self->appearance()->resetFace();
          self->then_idle();
          return;
        }
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_Preset.esp"), dir, true, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "preset export: " + r.value("message").toString());
          for (const auto& i : r.value("issues").toList()) {
            const auto m = i.toMap();
            self->log(QStringLiteral("  export issue %1 %2: %3").arg(m.value("severity").toString(), m.value("code").toString(), m.value("message").toString()));
          }
          QStringList fields;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          const QStringList face{"HairColor", "FaceMorph", "FaceParts", "HeadParts", "HeadTexture", "Weight", "Height", "TextureLighting", "TintLayers"};
          bool only_face = !fields.isEmpty();
          for (const auto& f : fields) only_face &= face.contains(f);
          self->check(only_face, "preset diff only has face fields: " + fields.join(", "));
          self->check(r.value("verificationProblems").toStringList().isEmpty(),
                      "exported FaceGen identical to the preview (" + r.value("verifiedFiles").toStringList().join(", ") + ")");
          self->appearance()->resetFace();
          self->then_idle();
        };
        self->after(100, *wait);
      });
    } else if (name == "donor-export") {
      // Character Creator "apply the look of another NPC": the face is rebuilt from the donor's record
      // (never its FaceGen copied) and exported; only face fields may change.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self] {
        const auto donor = qEnvironmentVariable("SNS_STRESS_DONOR", QStringLiteral("013640:Skyrim.esm"));
        if (self->appearance()->operation_blocked(QStringLiteral("face-morphs")) || self->appearance()->operation_blocked(QStringLiteral("head-parts"))) {
          // A donor rewrites morphs and Head Parts: on a protected head the editor must refuse it.
          self->head_part_skip = true;
          self->check(!self->appearance()->donor_blocked_reason().isEmpty(), "donor blocked for this head: " + self->appearance()->donor_blocked_reason());
          self->log("applyDonor " + donor + " on a protected head (must be refused)");
          self->appearance()->applyDonor(donor, QStringLiteral("donante"));
          self->after(1500, [self] {
            self->check(!self->appearance()->has_face_edits() && self->appearance()->face_source().isEmpty(),
                        "protected head unchanged after the refused donor");
            self->then_idle();
          });
          return;
        }
        self->head_part_skip = false;
        self->log("applyDonor " + donor);
        self->appearance()->applyDonor(donor, QStringLiteral("donante"));
        self->after(500, [self] { self->then_idle(); });
      });
      steps.push_back([self] {
        // The toolbar shows where the face comes from; regression: it read properties that did not exist.
        if (!self->head_part_skip) self->check(self->appearance()->face_source() == QStringLiteral("Donante: donante"), "face source shown: " + self->appearance()->face_source());
        self->step();
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("donor-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        self->export_face_and_check(dir, QStringLiteral("SNS_Donor.esp"), {});
      });
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self] {
        self->check(self->appearance()->face_source().isEmpty(), "reset clears the face source: '" + self->appearance()->face_source() + "'");
        self->step();
      });
    } else if (name == "body-export") {
      // Body tab: height alone is a minimal export (NAM6 only, no FaceGen); weight changes the head
      // (SkinnyMorph), so it regenerates the FaceGen.
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      auto original_height = std::make_shared<float>(1.0f);
      steps.push_back([self, original_height] {
        *original_height = self->appearance()->height();
        self->appearance()->setHeight(*original_height + 0.06f);
        self->then_idle();
      });
      const auto dir_h = QDir(work_dir).filePath(QStringLiteral("height-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir_h, original_height] {
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_Height.esp"), dir_h, true, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, original_height] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "height export: " + r.value("message").toString());
          QStringList fields;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) fields << c.toMap().value("field").toString();
          }
          self->check(fields == QStringList{"Height"}, "height export diff: " + fields.join(", ") + " (expected Height)");
          self->check(r.value("verifiedFiles").toStringList().isEmpty(), "no FaceGen exported for a height change");
          self->appearance()->resetFace();
          self->appearance()->setHeight(*original_height);  // resetFace keeps the body height
          self->then_idle();
        };
        self->after(100, *wait);
      });
      steps.push_back([self] {
        if (self->appearance()->operation_blocked(QStringLiteral("face-morphs"))) {
          self->log("weight: face morphs blocked for this head; weight export not exercised");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->appearance()->setWeight(self->appearance()->weight() > 50 ? 10.0f : 90.0f);
        self->then_idle();
      });
      const auto dir_w = QDir(work_dir).filePath(QStringLiteral("weight-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir_w] {
        if (self->head_part_skip) { self->step(); return; }
        self->export_face_and_check(dir_w, QStringLiteral("SNS_Weight.esp"), QStringLiteral("Weight"));
      });
    } else if (name == "export-reopen") {
      const QColor color("#2E86C1");
      const auto dir = QDir(work_dir).filePath(QStringLiteral("export-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, hair, color] { hair(color); self->then_idle(); });
      steps.push_back([self, dir] {
        self->check_exportable();
        self->log("exportFaceOverhaul to " + dir);
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_StressFace.esp"), dir, false, false);
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "export: " + r.value("message").toString());
          for (const auto& i : r.value("issues").toList()) {
            const auto m = i.toMap();
            self->log(QStringLiteral("  export issue %1 %2: %3 [%4]").arg(m.value("severity").toString(), m.value("code").toString(),
                                                                         m.value("message").toString(), m.value("subject").toString()));
          }
          self->check(r.value("verificationProblems").toStringList().isEmpty(),
                      "exported FaceGen identical to the preview (" + r.value("verifiedFiles").toStringList().join(", ") + ")");
          self->check(!r.value("inGameVerified").toBool(), "export not marked as verified in game");
          self->step();
        };
        self->after(100, *wait);
      });
      steps.push_back([self, color] {
        const auto project = QDir(self->work_dir).filePath("StressProject.snps");
        const bool saved = self->studio->outfit()->saveProject(project, QStringLiteral("StressProject"));
        self->check(saved, "project saved to " + project);
        self->log("reset the face, then reopen the project");
        self->appearance()->resetFace();
        self->after(50, [self, project] {
          self->check(self->studio->outfit()->openProject(project), "project reopened");
          self->then_idle();
        });
      });
      steps.push_back([self, color] {
        self->check(self->appearance()->npc_form_key() == self->primary_npc, "reopened project selects " + self->appearance()->npc_form_key());
        // A reopened hair color edit is still a hair color edit of the record (Hair Color Safe Mode).
        self->check_hair_safe(color);
        self->step();
      });
    } else if (name == "undo-redo-exact") {
      // Undo/redo land on exact states. Consecutive hair colors merge into one entry (like dragging the
      // picker), so a height edit separates them: hair A, height, hair B = three entries.
      const QColor a("#8E44AD"), b("#16A085");
      auto base_hair = std::make_shared<QColor>();
      auto base_height = std::make_shared<float>(0.f);
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, base_hair, base_height] {
        *base_hair = self->appearance()->hair_color();
        *base_height = self->appearance()->height();
        self->step();
      });
      steps.push_back([self, hair, a] { hair(a); self->then_idle(); });
      steps.push_back([self, base_height] { self->appearance()->setHeight(*base_height + 0.05f); self->then_idle(); });
      steps.push_back([self, hair, b] { hair(b); self->then_idle(); });
      struct Expect { bool undo; QColor hair; bool raised; const char* what; };
      const std::vector<Expect> expected = {{true, a, true, "undo: hair A, raised"}, {true, a, false, "undo: hair A, record height"},
                                            {true, QColor(), false, "undo: record"}, {false, a, false, "redo: hair A"},
                                            {false, a, true, "redo: raised"}, {false, b, true, "redo: hair B, raised"}};
      for (const auto& e : expected) {
        steps.push_back([self, e] { e.undo ? self->appearance()->undo() : self->appearance()->redo(); self->then_idle(); });
        steps.push_back([self, e, base_hair, base_height] {
          const QColor want = e.hair.isValid() ? e.hair : *base_hair;
          const float want_h = e.raised ? *base_height + 0.05f : *base_height;
          const auto got = self->appearance()->hair_color();
          self->check(got.name() == want.name() && std::abs(self->appearance()->height() - want_h) < 1e-4f,
                      QStringLiteral("%1 (hair %2, height %3)").arg(QString::fromLatin1(e.what), got.name()).arg(self->appearance()->height()));
          self->step();
        });
      }
      steps.push_back([self] {
        self->check(!self->appearance()->can_redo(), "nothing left to redo");
        self->check_hair_view(self->appearance()->hair_color());
        self->check_exportable_or_explained();
        self->appearance()->resetFace();
        self->then_idle();
      });
      // Two consecutive colors are one entry: one undo goes back to before both.
      steps.push_back([self, hair] {
        hair(QColor("#C0392B"));
        self->after(30, [self, hair] { hair(QColor("#2980B9")); self->then_idle(); });
      });
      steps.push_back([self] { self->appearance()->undo(); self->then_idle(); });
      steps.push_back([self, base_hair] {
        self->check(self->appearance()->hair_color().name() == base_hair->name(),
                    "consecutive colors undo as one entry: " + self->appearance()->hair_color().name());
        self->appearance()->resetFace();
        self->then_idle();
      });
    } else if (name == "switch-npc-rapid") {
      // Thirty actions per round across three NPCs (selections, plus hair edits fired while one loads),
      // 15 or 60 ms apart. The editor must end on the last selected NPC, loaded, with its record face.
      const QStringList cycle = {second_npc, QStringLiteral("01363F:Skyrim.esm"), primary_npc};
      for (int round = 0; round < 3; ++round) {
        steps.push_back([self, hair, cycle, round] {
          self->log(QStringLiteral("round %1, memory %2 MB").arg(round + 1).arg(process_memory_mb()));
          std::vector<std::function<void()>> actions;
          for (int i = 0; i < 30; ++i) {
            actions.push_back([self, hair, cycle, i] {
              if (i % 4 == 1 && self->appearance()->loaded()) hair(QColor(kPalette[i % std::size(kPalette)]));
              else self->studio->selectNpc(cycle[i % cycle.size()]);
            });
          }
          self->chain(std::move(actions), round % 2 == 0 ? 15 : 60, [self, cycle] {
            self->studio->selectNpc(cycle.back());
            self->then_idle();
          });
        });
        steps.push_back([self, round] {
          self->check(self->appearance()->npc_form_key() == self->primary_npc && self->appearance()->loaded(),
                      QStringLiteral("round %1 ends on %2, loaded").arg(round + 1).arg(self->appearance()->npc_form_key()));
          self->check(!self->appearance()->has_face_edits(), "the last selected NPC starts with the record's face");
          self->step();
        });
      }
      steps.push_back([self, hair] {
        self->log(QStringLiteral("after 3 rounds, memory %1 MB").arg(process_memory_mb()));
        hair(QColor("#E67E22"));
        self->then_idle();
      });
      steps.push_back([self] {
        self->check_hair_view(QColor("#E67E22"));
        self->check_exportable_or_explained();
        self->appearance()->resetFace();
        self->then_idle();
      });
    } else if (name == "project-cycle") {
      // A two-NPC project saved once, then opened five times; each time both NPCs come back with their
      // own hair color, including switches fired while one is still loading.
      const QString other = QStringLiteral("01363F:Skyrim.esm");
      const QColor a("#1ABC9C"), b("#D35400");
      const auto project = QDir(work_dir).filePath(QStringLiteral("CycleProject.snps"));
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, hair, a] { hair(a); self->then_idle(); });
      steps.push_back([self, other] { self->studio->outfit()->addCurrentToProject(); self->studio->selectNpc(other); self->then_idle(); });
      steps.push_back([self, hair, b] { hair(b); self->then_idle(); });
      steps.push_back([self, project] {
        self->studio->outfit()->addCurrentToProject();
        self->check(self->studio->outfit()->saveProject(project, QStringLiteral("CycleProject")), "project with two NPCs saved: " + project);
        self->studio->selectNpc(self->second_npc);  // leave both NPCs before reopening
        self->then_idle();
      });
      for (int i = 0; i < 5; ++i) {
        steps.push_back([self, project, i] {
          self->log(QStringLiteral("open %1, memory %2 MB").arg(i + 1).arg(process_memory_mb()));
          self->check(self->studio->outfit()->openProject(project), QStringLiteral("open %1").arg(i + 1));
          self->then_idle();
        });
        steps.push_back([self, other, a, i] {
          self->check(self->appearance()->npc_form_key() == self->primary_npc && self->appearance()->hair_color().name() == a.name(),
                      QStringLiteral("open %1: %2 with hair %3").arg(i + 1).arg(self->appearance()->npc_form_key(), self->appearance()->hair_color().name()));
          self->studio->outfit()->loadNpcFromProject(other);
          self->after(20, [self] { self->studio->outfit()->loadNpcFromProject(self->primary_npc); });
          self->after(40, [self, other] { self->studio->outfit()->loadNpcFromProject(other); self->then_idle(); });
        });
        steps.push_back([self, other, b, i] {
          self->check(self->appearance()->npc_form_key() == other && self->appearance()->hair_color().name() == b.name(),
                      QStringLiteral("open %1: %2 with hair %3").arg(i + 1).arg(self->appearance()->npc_form_key(), self->appearance()->hair_color().name()));
          self->step();
        });
      }
      steps.push_back([self] {
        self->log(QStringLiteral("after 5 opens, memory %1 MB").arg(process_memory_mb()));
        self->studio->selectNpc(self->primary_npc);
        self->then_idle();
      });
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
    } else if (name == "project-blocked-face") {
      // A face saved in a project with an edit that is blocked for this head (an older project, or one
      // made before a replacer was installed): loading it must keep the allowed edit (hair color) and
      // put the blocked field (weight, a morph operation) back to the record.
      const QColor color("#7D3C98");
      steps.push_back([self] { self->appearance()->resetFace(); self->then_idle(); });
      steps.push_back([self, hair, color] { hair(color); self->then_idle(); });
      steps.push_back([self] {
        if (!self->appearance()->operation_blocked(QStringLiteral("face-morphs"))) {
          self->log("face morphs allowed on this head: scenario needs a protected head");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        auto config = self->appearance()->face_config_for_project();
        self->check(config.has_value(), "project face available for the hair edit");
        if (!config) { self->step(); return; }
        config->weight = config->weight > 50.0f ? config->weight - 30.0f : config->weight + 30.0f;
        self->appearance()->queue_saved_face(self->primary_npc.toStdString(), config);
        self->log("reload the NPC with a saved face whose weight is blocked for this head");
        self->studio->selectNpc(self->second_npc);
        self->then_idle();
      });
      steps.push_back([self] {
        if (self->head_part_skip) { self->step(); return; }
        self->studio->selectNpc(self->primary_npc);
        self->then_idle();
      });
      steps.push_back([self, color] {
        if (self->head_part_skip) { self->step(); return; }
        const auto plan = self->appearance()->easy_mod_plan();
        QStringList keys;
        for (const auto& c : plan.value("changes").toList()) keys << c.toMap().value("key").toString();
        self->check(keys == QStringList{"hair"}, "blocked weight reverted, hair color kept: changes [" + keys.join(", ") + "]");
        self->check(self->appearance()->hair_color().name() == color.name(), "hair color from the project: " + self->appearance()->hair_color().name());
        self->appearance()->resetFace();
        self->then_idle();
      });
    } else if (name == "creator-layout") {
      // Character Creator right panel at the current window size: every tab must keep its texts inside
      // the panel and full labels on its buttons and tabs. One capture per tab.
      steps.push_back([self] {
        QQuickItem* panel = nullptr;
        QObject* tabs = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorRightPanel")) panel = qobject_cast<QQuickItem*>(o);
          if (o->objectName() == QLatin1String("creatorRightTabs")) tabs = o;
        }
        self->check(panel && tabs, "Character Creator right panel found");
        if (!panel || !tabs) { self->step(); return; }
        const auto size = QStringLiteral("%1x%2").arg(self->window->width()).arg(self->window->height());
        // The viewport's floating toolbar stays inside the viewport (it used to spill over the panels).
        for (auto* o : self->window->findChildren<QQuickItem*>()) {
          if (o->objectName() != QLatin1String("viewportToolbar") || !o->isVisible() || !o->parentItem()) continue;
          self->window->grabWindow();
          const QRectF r = o->mapRectToItem(o->parentItem(), QRectF(0, 0, o->width(), o->height()));
          self->check(r.left() >= -0.5 && r.right() <= o->parentItem()->width() + 0.5,
                      QStringLiteral("%1 viewport toolbar inside the viewport (%2..%3 of %4 px)").arg(size).arg(int(r.left())).arg(int(r.right())).arg(int(o->parentItem()->width())));
          break;
        }
        const QStringList names = {"presets", "rostro", "pelo-ojos", "cuerpo", "vestuario", "origen"};
        std::vector<std::function<void()>> actions;
        for (int i = 0; i < names.size(); ++i) {
          actions.push_back([tabs, i] { tabs->setProperty("currentIndex", i); });
          actions.push_back([self, panel, size, name = names[i]] {
            const QImage shot = self->window->grabWindow();  // lays the page out (hidden window)
            int inspected = 0;
            const auto problems = layout_problems(panel, false, &inspected);
            self->check(problems.isEmpty(), QStringLiteral("%1 %2: %3 items in %4 px, layout %5")
                                                .arg(size, name).arg(inspected).arg(panel->width()).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
            shot.save(QDir(self->work_dir).filePath(QStringLiteral("creator-%1-%2.png").arg(size, name)));
          });
        }
        self->chain(std::move(actions), 400, [self] { self->then_idle(); });
      });
    } else if (name == "armor-mod-filter") {
      // Outfit catalog filtered by mod: the plugin list comes from the index, the selector shows it, and
      // choosing a plugin returns exactly the armors that plugin defines or overrides.
      auto search = [](const std::shared_ptr<Stress>& self, const QString& plugin, std::function<void()> next) {
        auto done = std::make_shared<QMetaObject::Connection>();
        *done = QObject::connect(self->studio, &StudioController::armorResultsChanged, self->studio, [done, next] {
          QObject::disconnect(*done);
          next();
        });
        self->studio->searchArmors({}, 0, plugin);
      };
      auto all_total = std::make_shared<int>(0);
      steps.push_back([self, search, all_total] {
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, search, all_total] {
          if (self->studio->armor_plugins().isEmpty()) { self->after(200, *wait); return; }
          search(self, {}, [self, all_total] { *all_total = self->studio->armor_total(); self->step(); });
        };
        self->after(100, *wait);
      });
      steps.push_back([self, search, all_total] {
        const auto plugins = self->studio->armor_plugins();
        self->check(!plugins.isEmpty(), QStringLiteral("%1 plugin(s) with armors listed for the mod filter").arg(plugins.size()));
        QObject* box = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("armorModFilter")) { box = o; break; }
        }
        self->check(box && box->property("count").toInt() == plugins.size() + 1, "catalog selector lists every plugin plus 'Todos los mods'");
        // A plugin with several armors, preferring the outfit mod of the test NPCs.
        QVariantMap pick = plugins.front().toMap();
        for (const auto& p : plugins) {
          const auto m = p.toMap();
          if (m.value("plugin").toString().contains(QLatin1String("Runes of Roots"), Qt::CaseInsensitive)) { pick = m; break; }
          if (m.value("armors").toInt() > pick.value("armors").toInt() && m.value("armors").toInt() < 200) pick = m;
        }
        const auto plugin = pick.value("plugin").toString();
        const int expected = pick.value("armors").toInt();
        self->log("filter by " + plugin + " (" + pick.value("mod").toString() + ")");
        search(self, plugin, [self, search, plugin, expected, all_total] {
          self->check(self->studio->armor_total() == expected,
                      QStringLiteral("%1: %2 armor(s) (index says %3)").arg(plugin).arg(self->studio->armor_total()).arg(expected));
          search(self, {}, [self, all_total] {
            self->check(self->studio->armor_total() == *all_total, QStringLiteral("'Todos los mods' back to %1 armor(s)").arg(*all_total));
            self->step();
          });
        });
      });
    } else if (name == "appearance-control") {
      // Appearance control map of the loaded NPC on the real profile, cross-checked against the data it
      // is built from (override chain, MO2 VFS, the facial analysis) and shown in the «Origen» tab.
      steps.push_back([self] {
        const auto map = self->studio->appearance_control();
        const auto npc = self->studio->npc();
        const auto chain = npc.value("overrides").toList();
        self->check(map.value("formKey").toString() == self->primary_npc, "control map belongs to " + self->primary_npc);
        QHash<QString, QVariantMap> parts;
        for (const auto& c : map.value("components").toList()) parts.insert(c.toMap().value("key").toString(), c.toMap());
        const QStringList keys = {"face-shape", "head-parts", "face-texture", "skin-tint", "hair-color", "race", "body", "outfit", "facegen", "facetint"};
        QStringList missing;
        for (const auto& k : keys) if (!parts.contains(k)) missing << k;
        self->check(missing.isEmpty(), QStringLiteral("%1 parts described (missing: %2)").arg(parts.size()).arg(missing.join(", ")));

        // Record parts: the controller is a version of the chain, its mod is MO2's provider of that
        // plugin, and no later version changed the part's fields again (except to copy its value back,
        // which the part shows as "via").
        const QHash<QString, QStringList> fields = {{"face-shape", {"FaceMorph", "FaceParts"}}, {"head-parts", {"HeadParts"}},
                                                    {"hair-color", {"HairColor"}}, {"outfit", {"DefaultOutfit", "SleepingOutfit", "WornArmor"}}};
        for (auto it = fields.begin(); it != fields.end(); ++it) {
          const auto part = parts.value(it.key());
          int at = -1;
          for (int i = 0; i < chain.size(); ++i) {
            if (chain[i].toMap().value("plugin").toString().compare(part.value("plugin").toString(), Qt::CaseInsensitive) == 0) at = i;
          }
          // The last version that changed any of the fields is the controller, or only copies its values.
          int last = 0;
          for (int i = 1; i < chain.size(); ++i) {
            for (const auto& f : chain[i].toMap().value("changedFields").toList()) {
              if (it.value().contains(f.toString())) last = i;
            }
          }
          bool later_change = false;
          if (at >= 0 && last != at) {
            const auto copied = chain[last].toMap().value("copiedFrom").toMap();
            for (const auto& f : chain[last].toMap().value("changedFields").toList()) {
              const bool copy_of_controller =
                  copied.value(f.toString()).toString().compare(part.value("plugin").toString(), Qt::CaseInsensitive) == 0;
              later_change |= it.value().contains(f.toString()) && !copy_of_controller;
            }
          }
          self->check(at >= 0 && !later_change && part.value("mod").toString() == self->studio->providerOf(part.value("plugin").toString()),
                      QStringLiteral("%1 controlled by %2 «%3» (version %4 of %5)%7, overrides [%6]")
                          .arg(it.key(), part.value("plugin").toString(), part.value("mod").toString()).arg(at + 1).arg(chain.size())
                          .arg(part.value("overridden").toStringList().join(", "))
                          .arg(part.value("via").toString().isEmpty() ? QString() : " via " + part.value("via").toString()));
        }

        // FaceGen files: the same source the facial analysis located, and every losing loose copy in MO2.
        const auto compat = self->appearance()->face_compatibility();
        const auto prov = compat.value("provenance").toMap();
        const auto facegen = parts.value("facegen");
        const auto at = prov.value("faceGen").toMap();
        const auto analysed_mod = at.value("kind").toString() == "suelto" ? at.value("provider").toString() : at.value("archiveProvider").toString();
        self->check(facegen.value("mod").toString() == analysed_mod,
                    QStringLiteral("FaceGen «%1» (%2) matches the facial analysis «%3»").arg(facegen.value("mod").toString(), facegen.value("kind").toString(), analysed_mod));
        const auto geometry_path = npc.value("faceGen").toMap().value("geometryPath").toString();
        const auto loose = self->studio->snapshot()->vfs.providers_of(geometry_path.toStdString());
        const int losing = facegen.value("kind").toString() == "loose" ? static_cast<int>(loose.size()) - 1 : static_cast<int>(loose.size());
        self->check(facegen.value("overridden").toStringList().size() == losing,
                    QStringLiteral("FaceGen loose copies that lose: %1 (VFS has %2 loose)").arg(facegen.value("overridden").toStringList().size()).arg(loose.size()));
        const double deviation = compat.value("deviation").toDouble();
        if (deviation >= 0.0) {
          self->check(std::abs(map.value("deviation").toDouble() - deviation) < 1e-4, QStringLiteral("deviation %1 from the facial analysis").arg(deviation, 0, 'f', 3));
        } else {
          self->check(!map.value("deviation").isValid(), "no deviation when the analysis could not compare");
        }

        // Evidence for the head-part check: the record's head parts against the FaceGen shape names.
        if (const auto ctx = ResourceContext::for_snapshot(self->studio->snapshot()); ctx && ctx->resolver) {
          if (const auto bytes = ctx->resolver->read(geometry_path.toStdString())) {
            const auto info = sns::face::inspect_facegen(*bytes);
            QStringList shapes, head_parts, absent;
            for (const auto& [shape, vertices] : info.shapes) shapes << QString::fromStdString(shape);
            for (const auto& p : npc.value("headParts").toList()) {
              const auto id = p.toMap().value("record").toMap().value("editorId").toString();
              const bool mesh = !p.toMap().value("modelPath").toString().isEmpty();
              head_parts << (id.isEmpty() ? p.toMap().value("record").toMap().value("formKey").toString() + "(no EditorID)" : id) + (mesh ? "" : "(no mesh)");
              if (mesh && (id.isEmpty() || !shapes.contains(id, Qt::CaseInsensitive))) absent << head_parts.back();
            }
            self->log("  head parts [" + head_parts.join(", ") + "], FaceGen shapes [" + shapes.join(", ") + "], absent [" + absent.join(", ") + "]");
          }
        }

        // Findings agree with the facts they state.
        QStringList codes;
        for (const auto& f : map.value("findings").toList()) {
          const auto m = f.toMap();
          codes << m.value("severity").toString() + ":" + m.value("code").toString();
          self->log("  finding " + m.value("severity").toString() + " " + m.value("code").toString() + ": " + m.value("message").toString()
                    + (m.value("suggestion").toString().isEmpty() ? QString() : " -> " + m.value("suggestion").toString()));
        }
        const bool same_mod = map.value("faceMod").toString() == facegen.value("mod").toString();
        if (codes.contains("warning:FaceGenRecordMismatch")) {
          self->check(!same_mod && (deviation < 0.0 || deviation >= sns::appearance::kFaceGenMatchesRecord), "mismatch reported only for different mods without a matching measurement");
        }
        if (codes.contains("ok:FaceSourcesConsistent")) self->check(same_mod, "consistent only when record face and FaceGen come from the same mod");
        self->check(!codes.isEmpty(), "findings: " + codes.join(", "));
        self->log(QStringLiteral("control map: face %1 «%2», worst %3, analysis %4 us")
                      .arg(map.value("facePlugin").toString(), map.value("faceMod").toString(), map.value("worst").toString())
                      .arg(map.value("analysisUs").toLongLong()));
        QFile out(QDir(self->work_dir).filePath("appearance-control.json"));
        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson());
        self->step();
      });
      // The «Origen» tab shows it: verdict and every part, inside the panel.
      steps.push_back([self] {
        QObject* tabs = nullptr;
        QQuickItem* panel = nullptr;
        QObject* verdict = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorRightTabs")) tabs = o;
          if (o->objectName() == QLatin1String("appearanceControlPanel")) panel = qobject_cast<QQuickItem*>(o);
          if (o->objectName() == QLatin1String("appearanceControlVerdict")) verdict = o;
        }
        self->check(tabs && panel && verdict, "«Origen» tab and control panel found");
        if (!tabs || !panel || !verdict) { self->step(); return; }
        tabs->setProperty("currentIndex", 5);
        self->after(400, [self, panel, verdict] {
          const QImage shot = self->window->grabWindow();
          int inspected = 0;
          const auto problems = layout_problems(panel, false, &inspected);
          self->check(panel->isVisible() && !verdict->property("text").toString().isEmpty(),
                      "«Origen» verdict: " + verdict->property("text").toString());
          self->check(problems.isEmpty(), QStringLiteral("«Origen» %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
          shot.save(QDir(self->work_dir).filePath("appearance-control.png"));
          self->step();
        });
      });
    } else if (name == "damaged-facegen-scan") {
      // Audit F-11 on a profile: ONLY for the synthetic instance of tests/tools/synthetic_mo2.cpp (run by
      // scripts/damaged-facegen-check.ps1; not part of "all"), whose single defect is the damaged FaceGen of one
      // NPC. The scan must finish and report that NPC as a damaged FaceGen, instead of the studio closing (an
      // exception out of the scan thread) or calling it a FaceGen that does not match the record. The synthetic
      // load order has one unique NPC, so "the rest goes on" is shown by a second scan that finishes the same.
      auto first = std::make_shared<QVariantMap>();
      for (int pass = 0; pass < 2; ++pass) {
        steps.push_back([self, pass, first] {
          const auto damaged = qEnvironmentVariable("SNS_DAMAGED_FACEGEN_NPC", QStringLiteral("0A2C8E:Skyrim.esm"));
          self->studio->scanAppearanceControl();
          auto wait = std::make_shared<std::function<void()>>();
          auto started = std::make_shared<QElapsedTimer>();
          started->start();
          *wait = [self, wait, started, damaged, pass, first] {
            const auto scan = self->studio->appearance_scan();
            if (scan.value("running").toBool() && started->elapsed() < 5 * 60 * 1000) { self->after(200, *wait); return; }
            const int total = scan.value("total").toInt(), analysed = scan.value("analysed").toInt(), errors = scan.value("errors").toInt();
            const auto tag = QStringLiteral("scan %1: ").arg(pass + 1);
            self->check(scan.value("stage").toString() == "done", tag + "synthetic profile scan finished: " + scan.value("stage").toString());
            self->check(total >= 1 && scan.value("listed").toInt() == total && analysed == total && errors == 0,
                        tag + QStringLiteral("%1 unique NPCs listed, %2 analysed, %3 errors").arg(total).arg(analysed).arg(errors));
            QVariantMap row;
            for (const auto& r : scan.value("rows").toList()) {
              if (r.toMap().value("formKey").toString().compare(damaged, Qt::CaseInsensitive) == 0) row = r.toMap();
            }
            const auto codes = row.value("codes").toStringList();
            self->check(!row.isEmpty(), tag + damaged + " has a row in the scan");
            self->check(codes.contains(QStringLiteral("FaceGenUnreadable")),
                        tag + damaged + " reported as a damaged FaceGen (codes: " + codes.join(", ") + ")");
            if (pass == 0) {
              *first = scan;
            } else {
              self->check(scan.value("counts") == first->value("counts") && scan.value("codes") == first->value("codes"),
                          tag + "the studio still scans after the damaged FaceGen: same counts and codes as scan 1");
            }
            QFile out(QDir(self->work_dir).filePath(QStringLiteral("damaged-facegen-scan-%1.json").arg(pass + 1)));
            if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(scan)).toJson());
            self->step();
          };
          self->after(200, *wait);
        });
      }
    } else if (name == "appearance-scan") {
      // Control map of every unique NPC of the real profile: totals add up, flagged NPCs keep their
      // findings when opened one by one (where the facial measurement may only downgrade a mismatch),
      // and the «Origen» tab lists them.
      auto flagged = std::make_shared<QVariantList>();
      steps.push_back([self, flagged] {
        self->studio->scanAppearanceControl();
        auto wait = std::make_shared<std::function<void()>>();
        auto started = std::make_shared<QElapsedTimer>();
        started->start();
        *wait = [self, wait, started, flagged] {
          const auto scan = self->studio->appearance_scan();
          if (scan.value("running").toBool() && started->elapsed() < 20 * 60 * 1000) { self->after(500, *wait); return; }
          const auto counts = scan.value("counts").toMap();
          const int warnings = counts.value("warning").toInt(), infos = counts.value("info").toInt(), oks = counts.value("ok").toInt();
          const int analysed = scan.value("analysed").toInt();
          self->check(scan.value("stage").toString() == "done", "profile scan finished: " + scan.value("stage").toString());
          self->check(analysed + scan.value("errors").toInt() == scan.value("total").toInt() && scan.value("listed").toInt() == scan.value("total").toInt(),
                      QStringLiteral("%1 unique NPCs listed, %2 analysed, %3 errors").arg(scan.value("total").toInt()).arg(analysed).arg(scan.value("errors").toInt()));
          self->check(warnings + infos + oks == analysed && scan.value("rows").toList().size() == warnings + infos,
                      QStringLiteral("%1 with warnings, %2 with notes, %3 ok; %4 rows").arg(warnings).arg(infos).arg(oks).arg(scan.value("rows").toList().size()));
          QStringList codes;
          const auto code_map = scan.value("codes").toMap();
          for (auto it = code_map.begin(); it != code_map.end(); ++it) codes << it.key() + "=" + QString::number(it.value().toInt());
          self->log(QStringLiteral("profile scan: %1 ms total, %2 ms listing, %3 worker calls, %4 us per NPC; codes %5; %6 with «Usar el rostro de X»")
                        .arg(scan.value("elapsedMs").toLongLong()).arg(scan.value("listingMs").toLongLong()).arg(scan.value("rpcCalls").toInt())
                        .arg(analysed > 0 ? scan.value("elapsedMs").toLongLong() * 1000 / analysed : 0).arg(codes.join(", "))
                        .arg(scan.value("resolutions").toInt()));
          self->log(QStringLiteral("profile scan UI thread: longest block %1 ms (%2), %3 ms analysing in the studio, the rest waiting for the worker")
                        .arg(scan.value("maxBlockMs").toDouble(), 0, 'f', 1).arg(scan.value("maxBlockWhere").toString())
                        .arg(scan.value("analysisMs").toLongLong()));
          for (const auto& page : scan.value("listingPages").toStringList()) self->log("  listing page " + page);
          // Every UI-thread step of the scan is sliced (~20 ms measured on the test profile, debug build); a whole
          // listing page or 40 analyses at once blocked the interface ~100 ms. Generous bound: a guard, not a benchmark.
          self->check(scan.value("maxBlockMs").toDouble() < 60.0,
                      QStringLiteral("longest UI-thread block %1 ms (%2) < 60 ms").arg(scan.value("maxBlockMs").toDouble(), 0, 'f', 1)
                          .arg(scan.value("maxBlockWhere").toString()));
          QFile out(QDir(self->work_dir).filePath("appearance-scan.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(scan)).toJson());
          // Review cases (D1): every ambiguous resolution of the real profile, generated from this scan with the
          // digest of its inputs and no decision; a human reviews them one by one in «Origen».
          QJsonArray cases;
          int with_resolution = 0, approved = 0;
          bool consistent = true;
          for (const auto& r : scan.value("rows").toList()) {
            const auto row = r.toMap();
            if (!row.value("resolution").toString().isEmpty()) ++with_resolution;
            const bool ambiguous = !row.value("resolutionDiscardsOwn").toStringList().isEmpty();
            consistent = consistent && ambiguous == row.value("requiresReview").toBool();
            if (!ambiguous) continue;
            consistent = consistent && row.value("inputDigest").toString().size() == 64;
            if (row.value("reviewState").toString() == "approved") ++approved;
            cases.append(QJsonObject{{"npcFormKey", row.value("formKey").toString()},
                                     {"name", row.value("name").toString()},
                                     {"proposal", row.value("resolution").toString()},
                                     {"winnerPlugin", row.value("resolutionWinner").toString()},
                                     {"facePlugin", row.value("facePlugin").toString()},
                                     {"faceMod", row.value("faceMod").toString()},
                                     {"faceGenMod", row.value("faceGenMod").toString()},
                                     {"fields", QJsonArray::fromStringList(row.value("resolutionFields").toStringList())},
                                     {"keeps", QJsonArray::fromStringList(row.value("resolutionKeeps").toStringList())},
                                     {"discardsOwnEdits", QJsonArray::fromStringList(row.value("resolutionDiscardsOwn").toStringList())},
                                     {"codes", QJsonArray::fromStringList(row.value("codes").toStringList())},
                                     {"inputDigest", row.value("inputDigest").toString()},
                                     {"reviewState", row.value("reviewState").toString()},
                                     {"decision", QJsonValue()}});
          }
          self->check(with_resolution == scan.value("resolutions").toInt(),
                      QStringLiteral("%1 rows with «Usar el rostro de X» == %2 resolutions").arg(with_resolution).arg(scan.value("resolutions").toInt()));
          self->check(consistent, "every resolution that discards own edits needs a review and carries the digest of its inputs");
          if (!QFile::exists(self->studio->review_book_path())) self->check(approved == 0, "without a review book nothing is approved");
          QFile cases_file(QDir(self->work_dir).filePath("review-cases.json"));
          if (cases_file.open(QIODevice::WriteOnly)) {
            cases_file.write(QJsonDocument(QJsonObject{{"schema", 1},
                                                       {"generatedAt", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
                                                       {"reviewBook", self->studio->review_book_path()},
                                                       {"historicalBaseline", 53},
                                                       {"note", "53 is a historical baseline, not a quota; decisions are taken one by one in the studio"},
                                                       {"ambiguous", cases.size()},
                                                       {"approved", approved},
                                                       {"cases", cases}})
                                 .toJson());
          }
          self->log(QStringLiteral("review cases: %1 ambiguous resolution(s) need an individual review, %2 approved (historical baseline 53, not a quota)")
                        .arg(cases.size()).arg(approved));
          // Up to three flagged NPCs to open: one per warning code first, then notes.
          QStringList seen;
          for (const auto& r : scan.value("rows").toList()) {
            const auto row = r.toMap();
            const auto first = row.value("codes").toStringList().value(0);
            if (flagged->size() >= 3 || seen.contains(first)) continue;
            seen << first;
            *flagged << row;
          }
          self->step();
        };
        self->after(500, *wait);
      });
      for (int i = 0; i < 3; ++i) {
        steps.push_back([self, flagged, i] {
          if (i >= flagged->size()) { self->step(); return; }
          const auto form_key = flagged->at(i).toMap().value("formKey").toString();
          self->studio->selectNpc(form_key);
          // Not when_idle(): the face editor never loads NPCs it cannot edit (template traits), so wait
          // for this NPC's control map and for its facial analysis, if any, to finish.
          auto wait = std::make_shared<std::function<void()>>();
          auto started = std::make_shared<QElapsedTimer>();
          started->start();
          *wait = [self, wait, started, form_key] {
            const bool mine = self->studio->appearance_control().value("formKey").toString() == form_key;
            const bool settling = self->studio->npc_loading() || self->appearance()->loading() || self->appearance()->analysing();
            if ((!mine || settling || started->elapsed() < 1500) && started->elapsed() < 60000) { self->after(200, *wait); return; }
            self->step();
          };
          self->after(200, *wait);
        });
        steps.push_back([self, flagged, i] {
          if (i >= flagged->size()) { self->step(); return; }
          const auto row = flagged->at(i).toMap();
          const auto map = self->studio->appearance_control();
          QStringList now;
          for (const auto& f : map.value("findings").toList()) now << f.toMap().value("code").toString();
          QStringList missing;
          for (const auto& c : row.value("codes").toStringList()) {
            const bool downgraded = c == "FaceGenRecordMismatch" && now.contains("FaceGenFromOtherModMatches");
            if (!now.contains(c) && !downgraded) missing << c;
          }
          self->check(map.value("formKey") == row.value("formKey") && missing.isEmpty(),
                      QStringLiteral("%1 %2: scan [%3], opened [%4] (deviation %5)")
                          .arg(row.value("formKey").toString(), row.value("name").toString(), row.value("codes").toStringList().join(", "), now.join(", "))
                          .arg(map.value("deviation").isValid() ? QString::number(map.value("deviation").toDouble(), 'f', 3) : QStringLiteral("n/a")));
          self->step();
        });
      }
      steps.push_back([self] {
        self->studio->selectNpc(self->primary_npc);
        self->then_idle();
      });
      steps.push_back([self] {
        QObject* tabs = nullptr;
        QQuickItem* panel = nullptr;
        QObject* status = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorRightTabs")) tabs = o;
          if (o->objectName() == QLatin1String("appearanceControlPanel")) panel = qobject_cast<QQuickItem*>(o);
          if (o->objectName() == QLatin1String("appearanceScanStatus")) status = o;
        }
        self->check(tabs && panel && status, "«Origen» scan section found");
        if (!tabs || !panel || !status) { self->step(); return; }
        tabs->setProperty("currentIndex", 5);
        self->after(400, [self, panel, status] {
          const QImage shot = self->window->grabWindow();
          int inspected = 0;
          const auto problems = layout_problems(panel, false, &inspected);
          self->check(status->property("text").toString().startsWith(QStringLiteral("Terminado")), "scan status: " + status->property("text").toString());
          self->check(problems.isEmpty(), QStringLiteral("«Origen» with the scan: %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
          shot.save(QDir(self->work_dir).filePath("appearance-scan.png"));
          self->step();
        });
      });
    } else if (name == "face-resolution") {
      // «Usar el rostro de X» on the real profile (read-only): the loaded NPC must be a FaceGenRecordMismatch
      // whose FaceGen mod has a version of the record (AI Overhaul over NITHI on the test profile). The
      // button prepares the Easy Mod plan, the patch goes to a work folder, and the checks re-read it:
      // the worker with Mutagen (verification + chain with the patch last), here the TES4 header, the
      // manifest and the control map projected with the patch loaded last.
      auto res = std::make_shared<QVariantMap>();
      auto facegen_hash = std::make_shared<QByteArray>();
      // Main.qml and the Character Creator each have an assistant; the button opens the creator's one.
      auto wizard = [self]() -> QObject* {
        QObject* any = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (!QByteArray(o->metaObject()->className()).startsWith("ExportWizardDialog_QMLTYPE")) continue;
          if (o->property("visible").toBool()) return o;
          if (!any) any = o;
        }
        return any;
      };
      auto read_facegen = [self]() -> QByteArray {
        const auto path = self->studio->npc().value("faceGen").toMap().value("geometryPath").toString().toStdString();
        const auto ctx = ResourceContext::for_snapshot(self->studio->snapshot());
        const auto bytes = ctx && ctx->resolver ? ctx->resolver->read(path) : std::nullopt;
        return bytes ? QCryptographicHash::hash(QByteArray::fromStdString(*bytes), QCryptographicHash::Sha256).toHex() : QByteArray();
      };
      steps.push_back([self, res, facegen_hash, read_facegen] {
        const auto map = self->studio->appearance_control();
        QStringList codes;
        for (const auto& f : map.value("findings").toList()) codes << f.toMap().value("code").toString();
        self->check(map.value("formKey").toString() == self->primary_npc, "control map belongs to " + self->primary_npc);
        if (!codes.contains("FaceGenRecordMismatch")) {
          // Nothing to resolve (run it with --npc on an AI Overhaul/NITHI NPC such as 01334E:Skyrim.esm).
          self->check(!map.contains("resolution"), "no mismatch (" + codes.join(", ") + "): no resolution offered");
          self->steps.clear();
          self->step();
          return;
        }
        self->log("before: " + codes.join(", "));
        *res = map.value("resolution").toMap();
        self->check(!res->isEmpty(), "the map offers a resolution: " + res->value("label").toString());
        if (res->isEmpty()) { self->steps.clear(); self->step(); return; }
        QString facegen_mod;
        for (const auto& c : map.value("components").toList()) {
          if (c.toMap().value("key").toString() == "facegen") facegen_mod = c.toMap().value("mod").toString();
        }
        const auto source = res->value("sourcePlugin").toString();
        const auto winner = self->studio->npc().value("summary").toMap().value("winningPlugin").toString();
        QStringList chain;
        for (const auto& o : self->studio->npc().value("overrides").toList()) chain << o.toMap().value("plugin").toString();
        self->check(res->value("sourceMod").toString() == facegen_mod && self->studio->providerOf(source) == facegen_mod,
                    QStringLiteral("source %1 «%2» provides the effective FaceGen «%3»").arg(source, res->value("sourceMod").toString(), facegen_mod));
        self->check(chain.contains(source, Qt::CaseInsensitive) && res->value("winnerPlugin").toString().compare(winner, Qt::CaseInsensitive) == 0,
                    QStringLiteral("source in the chain [%1], winner %2").arg(chain.join(" > "), winner));
        const auto fields = res->value("fields").toStringList();
        bool known = !fields.isEmpty();
        for (const auto& f : fields) {
          known = known && std::find(appearance::kResolutionFields.begin(), appearance::kResolutionFields.end(), f.toStdString()) !=
                               appearance::kResolutionFields.end();
        }
        self->check(known, "fields to copy: " + fields.join(", "));
        self->check(res->value("loadAfter").toStringList() == QStringList{source, res->value("winnerPlugin").toString()},
                    "loads after: " + res->value("loadAfter").toStringList().join(", "));
        *facegen_hash = read_facegen();
        self->check(!facegen_hash->isEmpty(), "effective FaceGen read: sha256 " + QString::fromLatin1(facegen_hash->left(16)));
        self->step();
      });
      // Hair Color Safe Mode keeps its own export: with a pending hair edit the resolution cannot be created.
      steps.push_back([self, hair] { hair(QColor("#8B5A2B")); self->then_idle(); });
      steps.push_back([self] {
        self->check(self->studio->prepareFaceResolution(), "prepareFaceResolution");
        const auto plan = self->appearance()->easy_mod_plan();
        bool edits_blocked = false;
        for (const auto& c : plan.value("checks").toList()) {
          edits_blocked |= c.toMap().value("key").toString() == "edits" && c.toMap().value("state").toString() == "blocked";
        }
        self->check(plan.value("mode").toString() == "face-resolution" && !plan.value("canExport").toBool() && edits_blocked,
                    "with a pending hair edit the resolution is blocked (" + plan.value("mode").toString() + ")");
        self->appearance()->clearFaceResolution();
        self->appearance()->resetFace();
        self->then_idle();
      });
      // Individual review (D1): an ambiguous resolution (it discards a mod's own face edits) is not exportable
      // until a human decides for this NPC. The harness decides only in a temporary book (SNS_REVIEW_DIR),
      // never in the user's one, and shows that pending, deferred and rejected block the export itself.
      auto review_state = [](const QVariantMap& plan) {
        for (const auto& c : plan.value("checks").toList()) {
          if (c.toMap().value("key").toString() == "review") return c.toMap().value("state").toString();
        }
        return QString();
      };
      const auto blocked_dir = QDir(work_dir).filePath(QStringLiteral("face-resolution-blocked-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, res, review_state, blocked_dir] {
        if (!res->value("requiresReview").toBool()) {
          self->check(self->studio->prepareFaceResolution() && review_state(self->appearance()->easy_mod_plan()).isEmpty(),
                      "clear resolution: no individual review");
          self->appearance()->clearFaceResolution();
          self->step();
          return;
        }
        const auto review_dir = qEnvironmentVariable("SNS_REVIEW_DIR");
        const auto book_dir = QFileInfo(self->studio->review_book_path()).absolutePath();
        const bool temporary = !review_dir.isEmpty() &&
                               QDir::cleanPath(book_dir).compare(QDir::cleanPath(QFileInfo(review_dir).absoluteFilePath()), Qt::CaseInsensitive) == 0;
        self->check(temporary, "review book in the temporary SNS_REVIEW_DIR: " + self->studio->review_book_path());
        if (!temporary) { self->steps.clear(); self->step(); return; }
        self->check(res->value("reviewState").toString() == "pending" && res->value("inputDigest").toString().size() == 64,
                    "ambiguous resolution, pending: " + res->value("reviewReason").toString());
        self->check(self->studio->prepareFaceResolution(), "prepareFaceResolution (pending review)");
        const auto plan = self->appearance()->easy_mod_plan();
        self->check(!plan.value("canExport").toBool() && review_state(plan) == "blocked", "a pending review blocks the assistant");
        // The export itself refuses it (not only the assistant's button).
        QDir().mkpath(blocked_dir);
        self->appearance()->exportFaceResolution(plan.value("suggestedPluginName").toString(), blocked_dir, true, true, QStringLiteral("SNS Tester"), {});
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, review_state, blocked_dir] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(!r.value("success").toBool() && r.value("message").toString().contains(QStringLiteral("Revisión individual pendiente")),
                      "the export refuses it: " + r.value("message").toString());
          self->check(QDir(blocked_dir).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(), "nothing written while pending");
          const std::pair<const char*, const char*> decisions[] = {{"defer", "deferred"}, {"reject", "rejected"}};
          for (const auto& [decision, state] : decisions) {
            self->check(self->studio->setReviewDecision(QString::fromLatin1(decision), QStringLiteral("harness")),
                        QStringLiteral("setReviewDecision %1").arg(QString::fromLatin1(decision)));
            const auto now = self->studio->appearance_control().value("resolution").toMap().value("reviewState").toString();
            const auto p = self->appearance()->easy_mod_plan();
            self->check(now == QLatin1String(state) && !p.value("canExport").toBool() && review_state(p) == "blocked",
                        QStringLiteral("%1: state %2, the assistant stays blocked").arg(QString::fromLatin1(decision), now));
          }
          self->appearance()->clearFaceResolution();
          self->step();
        };
        self->after(100, *wait);
      });
      // Approval through the «Origen» panel (notes field and button), as the user does it.
      steps.push_back([self, res, review_state] {
        if (!res->value("requiresReview").toBool()) { self->step(); return; }
        QObject* tabs = nullptr;
        QObject* approve = nullptr;
        QObject* notes = nullptr;
        QObject* panel = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorRightTabs")) tabs = o;
          if (o->objectName() == QLatin1String("faceReviewApprove")) approve = o;
          if (o->objectName() == QLatin1String("faceReviewNotes")) notes = o;
          if (o->objectName() == QLatin1String("faceReviewPanel")) panel = o;
        }
        self->check(tabs && approve && notes && panel, "«Origen» review controls found");
        if (!tabs || !approve || !notes || !panel) { self->step(); return; }
        tabs->setProperty("currentIndex", 5);
        self->after(400, [self, res, review_state, approve, notes, panel] {
          self->check(panel->property("visible").toBool(), "the review panel is shown for the ambiguous resolution");
          const auto note = QStringLiteral("harness: temporary approval (SNS_REVIEW_DIR)");
          notes->setProperty("text", note);
          QMetaObject::invokeMethod(approve, "clicked");
          self->after(300, [self, res, review_state, note] {
            const auto review = self->studio->appearance_control().value("resolution").toMap();
            self->check(review.value("reviewState").toString() == "approved" && review.value("inputDigest") == res->value("inputDigest"),
                        "approved for the current inputs: " + review.value("inputDigest").toString().left(16));
            QFile book(self->studio->review_book_path());
            const auto json = book.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(book.readAll()).object() : QJsonObject{};
            const auto records = json.value("records").toArray();
            const auto record = records.isEmpty() ? QJsonObject{} : records.first().toObject();
            self->check(records.size() == 1 && record.value("npcFormKey").toString() == self->primary_npc &&
                            record.value("decision").toString() == "approve" &&
                            record.value("inputDigest").toString() == review.value("inputDigest").toString() &&
                            record.value("notes").toString() == note && !record.value("decidedAt").toString().isEmpty(),
                        QStringLiteral("review book: %1 record(s), %2 %3").arg(records.size()).arg(record.value("npcFormKey").toString(),
                                                                                                  record.value("decision").toString()));
            self->check(self->studio->prepareFaceResolution() && review_state(self->appearance()->easy_mod_plan()) == "ok",
                        "the assistant shows the approval");
            self->appearance()->clearFaceResolution();
            *res = review;
            self->step();
          });
        });
      });
      // The button in «Origen» prepares the plan and opens the assistant.
      steps.push_back([self, res, wizard] {
        QObject* tabs = nullptr;
        QObject* button = nullptr;
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorRightTabs")) tabs = o;
          if (o->objectName() == QLatin1String("faceResolutionButton")) button = o;
        }
        self->check(tabs && button, "«Origen» resolution button found");
        if (!tabs || !button) { self->step(); return; }
        tabs->setProperty("currentIndex", 5);
        self->after(400, [self, res, wizard, button] {
          self->check(button->property("visible").toBool() && button->property("text").toString() == res->value("label").toString(),
                      "button: " + button->property("text").toString());
          QMetaObject::invokeMethod(button, "clicked");
          self->after(500, [self, wizard] {
            auto* w = wizard();
            self->check(w && w->property("visible").toBool(), "the button opens the assistant");
            const auto plan = self->appearance()->easy_mod_plan();
            self->check(plan.value("mode").toString() == "face-resolution" && plan.value("canExport").toBool(),
                        "plan: " + plan.value("summary").toString());
            for (const auto& c : plan.value("checks").toList()) {
              const auto m = c.toMap();
              self->log(QStringLiteral("  check %1 %2: %3").arg(m.value("state").toString(), m.value("key").toString(), m.value("label").toString()));
            }
            if (!w) { self->step(); return; }
            self->check(w->property("plan").toMap().value("mode").toString() == "face-resolution", "the assistant shows the same plan");
            const auto size = QStringLiteral("resolution-%1x%2").arg(self->window->width()).arg(self->window->height());
            std::vector<std::function<void()>> pages;
            for (int page = 0; page < 4; ++page) {
              pages.push_back([w, page] { w->setProperty("step", page); });
              pages.push_back([self, w, page, size] { self->check_wizard_layout(w, page, size); });
            }
            self->chain(std::move(pages), 400, [self] { self->step(); });
          });
        });
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("face-resolution-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, res, facegen_hash, read_facegen, wizard, dir] {
        const auto plugin = self->appearance()->easy_mod_plan().value("suggestedPluginName").toString();
        self->log("face resolution export " + plugin + " to " + dir);
        QDir().mkpath(dir);
        self->appearance()->exportFaceResolution(plugin, dir, true, true, QStringLiteral("SNS Tester"), {});
        auto wait = std::make_shared<std::function<void()>>();
        *wait = [self, wait, res, facegen_hash, read_facegen, wizard, plugin] {
          if (self->appearance()->exporting()) { self->after(100, *wait); return; }
          const auto r = self->appearance()->last_export();
          self->check(r.value("success").toBool(), "export: " + r.value("message").toString());
          QStringList files;
          for (const auto& f : r.value("files").toList()) files << f.toMap().value("path").toString();
          self->check(files == QStringList{plugin}, "mod files: [" + files.join(", ") + "] (only the plugin, no FaceGen)");
          const auto masters = r.value("masters").toStringList();
          for (const auto& p : res->value("loadAfter").toStringList()) self->check(masters.contains(p, Qt::CaseInsensitive), "master " + p);
          // Safe patch: exactly the resolution's fields differ from the winner, each copied from the source.
          QStringList changed, reasons;
          for (const auto& d : r.value("semanticDiff").toList()) {
            for (const auto& c : d.toMap().value("changes").toList()) {
              changed << c.toMap().value("field").toString();
              reasons << c.toMap().value("reason").toString();
            }
          }
          auto wanted = res->value("fields").toStringList();
          changed.sort();
          wanted.sort();
          self->check(changed == wanted, "semantic diff [" + changed.join(", ") + "] == resolution fields");
          const auto copied_prefix = "Copiado de " + res->value("sourcePlugin").toString();
          self->check(!reasons.isEmpty() && std::all_of(reasons.begin(), reasons.end(), [&](const QString& s) { return s.startsWith(copied_prefix); }),
                      "every change says where it was copied from");
          // Re-read by the worker with Mutagen: the chain with the patch loaded last.
          const auto chain = r.value("projectedChain").toMap();
          const auto overrides = chain.value("overrides").toList();
          const auto last = overrides.isEmpty() ? QVariantMap{} : overrides.back().toMap();
          auto last_changed = last.value("changedFields").toStringList();
          last_changed.sort();
          bool all_copied = !last_changed.isEmpty();
          for (const auto& f : last_changed) {
            all_copied = all_copied && last.value("copiedFrom").toMap().value(f).toString() == res->value("sourcePlugin").toString();
          }
          self->check(last.value("plugin").toString() == plugin && last.value("isWinner").toBool() && last_changed == wanted && all_copied,
                      QStringLiteral("re-read chain ends with %1: changed [%2], all copied from the source: %3")
                          .arg(last.value("plugin").toString(), last_changed.join(", ")).arg(all_copied));
          // The control map with the patch loaded last: record and FaceGen from the same mod.
          const auto after = r.value("projectedControl").toMap();
          QStringList codes;
          for (const auto& f : after.value("findings").toList()) {
            codes << f.toMap().value("severity").toString() + ":" + f.toMap().value("code").toString();
          }
          self->check(!codes.contains("warning:FaceGenRecordMismatch") && codes.contains("ok:FaceSourcesConsistent") &&
                          after.value("faceMod").toString() == res->value("sourceMod").toString() &&
                          after.value("faceVia").toString() == plugin && !after.contains("resolution"),
                      QStringLiteral("after the patch: face %1 «%2» via %3; %4")
                          .arg(after.value("facePlugin").toString(), after.value("faceMod").toString(), after.value("faceVia").toString(), codes.join(", ")));
          // TES4 header of the written plugin, read here (not through Mutagen): ESL flag and masters.
          QFile esp(r.value("pluginPath").toString());
          QStringList header_masters;
          quint32 flags = 0;
          if (esp.open(QIODevice::ReadOnly)) {
            const auto bytes = esp.read(1 << 16);
            if (bytes.size() >= 24 && bytes.startsWith("TES4")) {
              quint32 size = 0;
              std::memcpy(&size, bytes.constData() + 4, 4);
              std::memcpy(&flags, bytes.constData() + 8, 4);
              const qsizetype end = std::min<qsizetype>(24 + static_cast<qsizetype>(size), bytes.size());
              for (qsizetype at = 24; at + 6 <= end;) {
                quint16 len = 0;
                std::memcpy(&len, bytes.constData() + at + 4, 2);
                if (bytes.mid(at, 4) == "MAST" && len > 0) header_masters << QString::fromUtf8(bytes.mid(at + 6, len - 1));
                at += 6 + len;
              }
            }
          }
          self->check(header_masters == masters && (flags & 0x200) != 0,
                      QStringLiteral("TES4: masters [%1], ESL %2").arg(header_masters.join(", ")).arg((flags & 0x200) != 0));
          // Manifest and README.
          QFile manifest_file(QDir(r.value("modDirectory").toString()).filePath("manifest.json"));
          const auto manifest = manifest_file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(manifest_file.readAll()).object() : QJsonObject{};
          QStringList load_after;
          for (const auto& p : manifest.value("loadAfter").toArray()) load_after << p.toString();
          const auto face = manifest.value("faces").toArray().first().toObject();
          self->check(load_after.size() == 2 && face.value("faceGenMode").toString() == "source-facegen" &&
                          face.value("effectiveFaceGenMod").toString() == res->value("sourceMod").toString() &&
                          manifest.value("inGameVerified") == QJsonValue(false) && manifest.value("readyForGame") == QJsonValue(false) &&
                          manifest.value("dependencyMode").toString() == "dependent",
                      "manifest: loadAfter [" + load_after.join(", ") + "], faceGenMode " + face.value("faceGenMode").toString());
          self->check(r.value("readyForGame") == QVariant(false) && r.value("inGameVerified") == QVariant(false), "not claimed ready for the game");
          QFile readme(QDir(r.value("modDirectory").toString()).filePath("README.md"));
          self->check(readme.open(QIODevice::ReadOnly) && QString::fromUtf8(readme.readAll()).contains("Debe cargarse después de"), "README: load order note");
          self->check(QFile::exists(r.value("zipPath").toString()), "ZIP written: " + r.value("zipPath").toString());
          // FaceGen Integrity Guard: the profile's FaceGen is untouched.
          self->check(read_facegen() == *facegen_hash, "the profile's FaceGen is byte-identical after the export");
          QFile out(QDir(self->work_dir).filePath("face-resolution.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson());
          auto* w = wizard();
          if (!w) { self->step(); return; }
          w->setProperty("modResult", r);
          w->setProperty("step", 4);
          self->after(400, [self, w] {
            self->check_wizard_layout(w, 4, QStringLiteral("resolution-%1x%2").arg(self->window->width()).arg(self->window->height()));
            QMetaObject::invokeMethod(w, "close");
            self->after(200, [self] {
              self->check(self->appearance()->easy_mod_plan().value("mode").toString() != "face-resolution", "closing the assistant clears the resolution");
              self->step();
            });
          });
        };
        self->after(100, *wait);
      });
    } else if (name == "npc-gallery") {
      // The NPC browser of the Character Creator as a gallery: cards for the same search results, each card
      // the NPC of its row, click selects it, double click and Enter open the editor (Rostro), Space selects,
      // the appearance filter keeps exactly the NPCs the profile scan flagged, and the main viewer still loads.
      struct Gallery {
        QQuickItem* browser = nullptr;
        QQuickItem* grid = nullptr;
        QObject* tabs = nullptr;
        QElapsedTimer opened;
      };
      auto g = std::make_shared<Gallery>();
      // The card of row `index` among the delegates the view has created (null when it has none).
      auto item_at = [g](int index) -> QQuickItem* {
        auto* content = g->grid ? g->grid->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!content) return nullptr;
        for (auto* child : content->childItems()) {
          if (child->property("formKey").isValid() && child->property("index").toInt() == index) return child;
        }
        return nullptr;
      };
      // QTest sends what the platform sends (a double click is press, release, press, double click, release,
      // with time stamps), after a frame that lays out pending changes as a visible window would.
      auto mouse = [self](QQuickItem* item, bool twice) {
        self->window->grabWindow();
        const auto pos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        if (twice) QTest::mouseDClick(self->window.data(), Qt::LeftButton, Qt::NoModifier, pos);
        else QTest::mouseClick(self->window.data(), Qt::LeftButton, Qt::NoModifier, pos);
      };
      auto key = [self](int k) {
        QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier);
        QCoreApplication::sendEvent(self->window, &press);
        QCoreApplication::sendEvent(self->window, &release);
      };
      // Waits until `done()` (or `ms`), then runs `next(ok)`.
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(100, *tick);
        };
        (*tick)();
      };
      auto selected_key = [self] { return self->studio->npc().value("summary").toMap().value("formKey").toString(); };
      steps.push_back([self, g, item_at, wait_for] {
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorNpcBrowser")) g->browser = qobject_cast<QQuickItem*>(o);
          if (o->objectName() == QLatin1String("creatorRightTabs")) g->tabs = o;
        }
        self->check(g->browser && g->tabs, "Character Creator NPC browser found");
        if (!g->browser || !g->tabs) { self->steps.clear(); self->step(); return; }
        for (auto* item : self->window->findChildren<QQuickItem*>()) {
          if (item->objectName() == QLatin1String("npcGallery") && g->browser->isAncestorOf(item)) g->grid = item;
        }
        self->check(g->grid != nullptr, "gallery view inside the browser");
        if (!g->grid) { self->steps.clear(); self->step(); return; }
        // Unique NPCs first (the gallery is for NPCs with a face of their own), then the gallery.
        self->studio->searchNpcs({}, {}, QStringLiteral("unique"), {});
        auto only_unique = [self] {
          const auto* rows = self->studio->npc_results();
          if (rows->count() == 0) return false;
          for (int i = 0; i < std::min(rows->count(), 50); ++i) {
            if (!rows->get(i).value("isUnique").toBool()) return false;
          }
          return true;
        };
        wait_for(only_unique, 30000, [self, g, item_at, wait_for](bool unique) {
          self->check(unique, QStringLiteral("search of unique NPCs: %1 rows").arg(self->studio->npc_results()->count()));
          g->opened.start();
          g->browser->setProperty("viewMode", "gallery");
          self->window->grabWindow();  // a frame: the hidden window lays the cards out only when it renders
          wait_for([g, item_at] { return g->grid->isVisible() && g->grid->property("count").toInt() > 0 && item_at(0) != nullptr; }, 30000,
                   [self, g, item_at, wait_for](bool ok) {
                     self->check(ok, QStringLiteral("first card after %1 ms (%2 results, %3 cards)")
                                         .arg(g->opened.elapsed()).arg(self->studio->npc_results()->count()).arg(g->grid->property("count").toInt()));
                     const int wanted = std::min(4, g->grid->property("count").toInt());
                     wait_for([item_at, wanted] { return item_at(wanted - 1) != nullptr; }, 10000, [self](bool) { self->step(); });
                   });
        });
      });
      // Every card created shows the NPC of its row.
      steps.push_back([self, g, item_at] {
        int compared = 0, wrong = 0;
        for (int i = 0; i < std::min(g->grid->property("count").toInt(), 24); ++i) {
          auto* card = item_at(i);
          if (!card) continue;
          ++compared;
          if (card->property("formKey").toString() != self->studio->npc_results()->get(i).value("formKey").toString()) ++wrong;
        }
        self->check(compared > 0 && wrong == 0, QStringLiteral("%1 cards compared with their rows, %2 wrong").arg(compared).arg(wrong));
        self->step();
      });
      // Click selects the NPC of the card.
      steps.push_back([self, g, item_at, mouse, wait_for, selected_key] {
        auto* card = item_at(2);
        self->check(card != nullptr, "third card exists");
        if (!card) { self->step(); return; }
        const auto key = card->property("formKey").toString();
        g->tabs->setProperty("currentIndex", 0);
        mouse(card, false);
        wait_for([self, key, selected_key] { return selected_key() == key && !self->studio->npc_loading(); }, 30000, [self, g, key, selected_key](bool ok) {
          self->check(ok && g->grid->property("currentIndex").toInt() == 2,
                      "click selects " + key + " (selected " + selected_key() + ", current card " + g->grid->property("currentIndex").toString() + ")");
          self->step();
        });
      });
      // Double click opens the editor of that NPC (the Rostro tab of the creator).
      steps.push_back([self, g, item_at, mouse] {
        auto* card = item_at(2);
        if (!card) { self->step(); return; }
        g->tabs->setProperty("currentIndex", 0);
        mouse(card, true);
        self->after(300, [self, g] {
          self->check(g->tabs->property("currentIndex").toInt() == 1, "double click opens the editor (tab " + g->tabs->property("currentIndex").toString() + ")");
          self->step();
        });
      });
      // Keyboard: Right moves, Space selects, Return opens the editor.
      steps.push_back([self, g, item_at, key, wait_for, selected_key] {
        g->tabs->setProperty("currentIndex", 0);
        g->grid->setProperty("currentIndex", 2);
        g->grid->forceActiveFocus();
        key(Qt::Key_Right);
        auto* card = item_at(3);
        const auto expected = card ? card->property("formKey").toString() : QString();
        self->check(g->grid->property("currentIndex").toInt() == 3 && !expected.isEmpty(),
                    "Right moves to card 4 (current " + g->grid->property("currentIndex").toString() + ", focus " +
                        (g->grid->hasActiveFocus() ? QStringLiteral("yes") : QStringLiteral("no")) + ")");
        key(Qt::Key_Space);
        wait_for([self, expected, selected_key] { return selected_key() == expected && !self->studio->npc_loading(); }, 30000,
                 [self, g, expected, key, selected_key](bool ok) {
                   self->check(ok, "Space selects " + expected + " (selected " + selected_key() + ")");
                   key(Qt::Key_Return);
                   self->after(300, [self, g] {
                     self->check(g->tabs->property("currentIndex").toInt() == 1, "Return opens the editor (tab " + g->tabs->property("currentIndex").toString() + ")");
                     g->tabs->setProperty("currentIndex", 0);
                     self->step();
                   });
                 });
      });
      // The main viewer still builds the selected NPC.
      steps.push_back([self, wait_for, selected_key] {
        self->studio->selectNpc(self->primary_npc);
        wait_for([self, selected_key] { return selected_key() == self->primary_npc && !self->scene()->loading() && !self->scene()->parts().isEmpty(); }, 60000,
                 [self](bool ok) {
                   QStringList issues;
                   for (const auto& i : self->scene()->issues()) issues << i.toMap().value("code").toString();
                   self->check(ok, QStringLiteral("main viewer shows %1: %2 parts (%3; issues %4)")
                                       .arg(self->primary_npc).arg(self->scene()->parts().size()).arg(self->scene()->status(), issues.join(", ")));
                   self->step();
                 });
      });
      // Appearance filter: before a scan nothing is analysed; after it, exactly the flagged NPCs.
      steps.push_back([self] {
        const int loaded = self->studio->npc_loaded();
        self->studio->setAppearanceFilter("unanalysed");
        const int unanalysed = self->studio->npc_results()->count();
        self->studio->setAppearanceFilter("");
        self->check(self->studio->npc_results()->count() == loaded, QStringLiteral("filter off: %1 of %2 rows").arg(self->studio->npc_results()->count()).arg(loaded));
        self->log(QStringLiteral("before the scan: %1 of %2 loaded rows unanalysed").arg(unanalysed).arg(loaded));
        self->studio->scanAppearanceControl();
        self->step();
      });
      steps.push_back([self, wait_for] {
        wait_for([self] { return self->studio->appearance_scan().value("stage").toString() == "done"; }, 20 * 60 * 1000, [self](bool ok) {
          self->check(ok, "profile scan finished");
          for (const auto& [filter, test] : std::vector<std::pair<QString, std::function<bool(const QVariantMap&)>>>{
                   {"warning", [](const QVariantMap& s) { return s.value("worst").toString() == "warning"; }},
                   {"review", [](const QVariantMap& s) { return s.value("requiresReview").toBool(); }},
                   {"unanalysed", [](const QVariantMap& s) { return s.isEmpty(); }}}) {
            self->studio->setAppearanceFilter(filter);
            const int shown = self->studio->npc_results()->count();
            int wrong = 0;
            for (int i = 0; i < shown; ++i) {
              if (!test(self->studio->appearanceStateOf(self->studio->npc_results()->get(i).value("formKey").toString()))) ++wrong;
            }
            // Every loaded row that passes the test must be shown, and only those.
            self->studio->setAppearanceFilter("");
            int expected = 0;
            for (int i = 0; i < self->studio->npc_results()->count(); ++i) {
              if (test(self->studio->appearanceStateOf(self->studio->npc_results()->get(i).value("formKey").toString()))) ++expected;
            }
            self->check(wrong == 0 && expected == shown,
                        QStringLiteral("filter %1: %2 shown, %3 expected from the scan, %4 wrong").arg(filter).arg(shown).arg(expected).arg(wrong));
          }
          self->step();
        });
      });
      steps.push_back([self, g] {
        self->after(400, [self, g] {
          int inspected = 0;
          const auto problems = layout_problems(g->browser, false, &inspected);
          self->check(problems.isEmpty(), QStringLiteral("gallery: %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
          self->window->grabWindow().save(QDir(self->work_dir).filePath("npc-gallery.png"));
          g->browser->setProperty("viewMode", "list");
          self->step();
        });
      });
    } else if (name == "donor-gallery") {
      // Presets › Transferencia de apariencia as a face gallery: a donor search fills the gallery (not the browser's
      // rows), each card asks for its portrait and ends with one or a stated reason, and "Usar" is the same action as
      // the list's button. Nothing is applied here (the donor export scenario covers applyDonor).
      auto state = std::make_shared<QElapsedTimer>();
      auto browser_rows = std::make_shared<int>(0);
      auto gallery = [self]() -> QQuickItem* {
        for (auto* item : self->window->findChildren<QQuickItem*>())
          if (item->objectName() == QLatin1String("donorGallery") && item->isVisible()) return item;
        return nullptr;
      };
      auto donor_cards = [gallery]() {
        QStringList keys;
        auto* g = gallery();
        auto* content = g ? g->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!content) return keys;
        for (auto* child : content->childItems())
          if (child->property("formKey").isValid()) keys << child->property("formKey").toString();
        return keys;
      };
      steps.push_back([self, state, browser_rows] {
        *browser_rows = self->studio->npc_results()->count();
        state->start();
        self->studio->searchDonors(qEnvironmentVariable("SNS_STRESS_DONOR_SEARCH", QStringLiteral("Lydia")));
        self->step();
      });
      steps.push_back([self, state, browser_rows, gallery, donor_cards] {
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, state, browser_rows, gallery, donor_cards, tick] {
          if (self->studio->donor_results()->count() > 0 && gallery() && !donor_cards().isEmpty()) {
            self->check(true, QStringLiteral("donor search: %1 donors, %2 face cards in the gallery after %3 ms")
                                  .arg(self->studio->donor_results()->count()).arg(donor_cards().size()).arg(state->elapsed()));
            self->check(self->studio->npc_results()->count() == *browser_rows, "the donor search leaves the browser's rows alone");
            self->step();
            return;
          }
          if (state->elapsed() > 60000) {
            self->check(false, QStringLiteral("donor gallery shows cards (donors %1, gallery %2)")
                                   .arg(self->studio->donor_results()->count()).arg(gallery() ? "visible" : "not visible"));
            self->step();
            return;
          }
          self->after(50, *tick);
        };
        (*tick)();
      });
      steps.push_back([self, donor_cards] {
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, donor_cards, clock, tick] {
          const auto keys = donor_cards();
          int final = 0, ready = 0;
          QStringList reasons;
          for (const auto& fk : keys) {
            const auto s = self->studio->portraits()->stateOf(fk, 0);
            const auto st = s.value("state").toString();
            if (st == QLatin1String("ready")) { ++ready; ++final; }
            else if (st == QLatin1String("error")) { ++final; reasons << fk + ": " + s.value("reason").toString(); }
          }
          if ((!keys.isEmpty() && final == keys.size()) || clock->elapsed() > 4 * 60000) {
            self->check(final == keys.size() && ready > 0, QStringLiteral("%1 of %2 donor cards show a studio portrait, %3 a stated reason (%4 ms)")
                                                             .arg(ready).arg(keys.size()).arg(final - ready).arg(clock->elapsed()));
            for (const auto& r : reasons.mid(0, 4)) self->log("  no portrait: " + r);
            const auto shot = QDir(self->work_dir).filePath("donor-gallery.png");
            self->log(QStringLiteral("capture %1: %2").arg(self->window->grabWindow().save(shot) ? "saved" : "FAILED", shot));
            self->step();
            return;
          }
          self->after(100, *tick);
        };
        (*tick)();
      });
    } else if (name == "npc-portraits-instant") {
      // Portraits already made in an earlier session show at once: the cache index is read while the profile is
      // indexed, cards show the stored render (marked provisional) or the verified one immediately, the verdicts
      // run off the GUI thread, a card never stays provisional once it is final, and the rows ahead are handed to
      // the prefetch. Run three times over one cache folder: cold, warm, and warm with SNS_PORTRAIT_DIGEST_SALT
      // (every stored portrait out of date, as after a load order change). Report: portraits-instant.json.
      struct Instant {
        QQuickItem* browser = nullptr;
        QQuickItem* grid = nullptr;
        QElapsedTimer opened;
        QJsonObject report;
        qint64 max_gap = 0;
        QElapsedTimer beat;
        QTimer* heartbeat = nullptr;
      };
      auto p = std::make_shared<Instant>();
      auto renderer = [self] { return self->studio->portraits(); };
      auto keys_on_screen = [p](int n) {
        QStringList keys;
        auto* content = p->grid ? p->grid->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!content) return keys;
        QList<QQuickItem*> cards;
        for (auto* child : content->childItems())
          if (child->property("formKey").isValid()) cards << child;
        std::sort(cards.begin(), cards.end(), [](QQuickItem* a, QQuickItem* b) { return a->property("index").toInt() < b->property("index").toInt(); });
        for (auto* c : cards) {
          if (keys.size() == n) break;
          keys << c->property("formKey").toString();
        }
        return keys;
      };
      auto shows_image = [renderer](const QString& fk) {
        const auto s = renderer()->stateOf(fk, 0);
        return s.value("state").toString() == QLatin1String("ready") || !s.value("provisionalUrl").toString().isEmpty();
      };
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(20, *tick);
        };
        (*tick)();
      };
      const bool salted = qEnvironmentVariableIsSet("SNS_PORTRAIT_DIGEST_SALT");
      const bool warm = qEnvironmentVariableIsSet("SNS_PORTRAIT_EXPECT_CACHE");
      steps.push_back([self, p, renderer, wait_for, salted, warm] {
        for (auto* o : self->window->findChildren<QObject*>())
          if (o->objectName() == QLatin1String("creatorNpcBrowser")) p->browser = qobject_cast<QQuickItem*>(o);
        for (auto* item : self->window->findChildren<QQuickItem*>())
          if (p->browser && item->objectName() == QLatin1String("npcGallery") && p->browser->isAncestorOf(item)) p->grid = item;
        self->check(p->browser && p->grid, "creator NPC browser and its gallery found");
        if (!p->browser || !p->grid) { self->steps.clear(); self->step(); return; }
        const auto s0 = renderer()->stats();
        self->log(QStringLiteral("mode: %1; cache index %2 (%3 stored renders read in %4 ms) before the gallery opens")
                      .arg(salted ? "warm, profile changed (salt)" : warm ? "warm" : "cold")
                      .arg(s0.value("indexReady").toBool() ? "ready" : "not ready")
                      .arg(s0.value("indexed").toInt()).arg(s0.value("indexMs").toLongLong()));
        p->report["mode"] = salted ? "salted" : warm ? "warm" : "cold";
        p->report["indexedBeforeGallery"] = s0.value("indexed").toInt();
        p->report["indexReadyBeforeGallery"] = s0.value("indexReady").toBool();
        p->report["indexMs"] = s0.value("indexMs").toLongLong();
        if (warm) self->check(s0.value("indexReady").toBool() && s0.value("indexed").toInt() > 0,
                              "the stored renders were indexed while the profile loaded (before the gallery opened)");
        self->studio->searchNpcs({}, {}, QStringLiteral("unique"), {});
        // The same rows in every run: wait for the unique-only page, not the full list shown at load.
        wait_for([self] {
          const auto* rows = self->studio->npc_results();
          if (rows->count() == 0) return false;
          for (int i = 0; i < std::min(rows->count(), 50); ++i) if (!rows->get(i).value("isUnique").toBool()) return false;
          return true;
        }, 30000, [self, p](bool) {
          p->beat.start();
          p->heartbeat = new QTimer(self->window);
          p->heartbeat->setInterval(10);
          QObject::connect(p->heartbeat, &QTimer::timeout, p->heartbeat, [p] { p->max_gap = std::max(p->max_gap, p->beat.restart()); });
          p->heartbeat->start();
          p->opened.start();
          p->browser->setProperty("viewMode", "gallery");
          self->window->grabWindow();
          self->step();
        });
      });
      // The first image on a card, and how many of the first visible cards show one right away.
      steps.push_back([self, p, renderer, wait_for, keys_on_screen, shows_image, warm] {
        wait_for([keys_on_screen, shows_image] { const auto k = keys_on_screen(8); return std::any_of(k.begin(), k.end(), shows_image); },
                 180000, [self, p, renderer, keys_on_screen, shows_image, warm](bool ok) {
                   const auto first_ms = p->opened.elapsed();
                   const auto keys = keys_on_screen(8);
                   int with_image = 0, provisional = 0;
                   for (const auto& fk : keys) {
                     const auto s = renderer()->stateOf(fk, 0);
                     if (shows_image(fk)) ++with_image;
                     if (s.value("state").toString() != QLatin1String("ready") && !s.value("provisionalUrl").toString().isEmpty()) ++provisional;
                   }
                   self->check(ok, QStringLiteral("first image on a card after %1 ms; %2 of %3 visible cards show an image then (%4 provisional)")
                                       .arg(first_ms).arg(with_image).arg(keys.size()).arg(provisional));
                   p->report["firstImageMs"] = first_ms;
                   p->report["cardsWithImageAtFirst"] = with_image;
                   p->report["provisionalAtFirst"] = provisional;
                   p->report["visibleCards"] = static_cast<int>(keys.size());
                   if (warm) self->check(first_ms < 1500, QStringLiteral("warm cache: an image within 1.5 s of opening the gallery (%1 ms)").arg(first_ms));
                   self->step();
                 });
      });
      // Every visible card ends with a verified portrait or a stated error, and none stays provisional.
      steps.push_back([self, p, renderer, wait_for, keys_on_screen, salted] {
        const auto keys = keys_on_screen(8);
        auto final_state = [renderer](const QString& fk) {
          const auto s = renderer()->stateOf(fk, 0).value("state").toString();
          return s == QLatin1String("ready") || s == QLatin1String("error");
        };
        wait_for([keys, final_state] { return std::all_of(keys.begin(), keys.end(), final_state); }, 6 * 60000,
                 [self, p, renderer, keys, salted](bool ok) {
                   int ready = 0, errors = 0, still_provisional = 0, rendered_now = 0, from_cache = 0;
                   for (const auto& fk : keys) {
                     const auto s = renderer()->stateOf(fk, 0);
                     const auto state = s.value("state").toString();
                     if (state == QLatin1String("ready")) ++ready;
                     if (state == QLatin1String("error")) ++errors;
                     if (state == QLatin1String("ready") && s.contains("provisionalUrl")) ++still_provisional;
                     if (s.value("source").toString() == QLatin1String("render")) ++rendered_now;
                     if (s.value("source").toString() == QLatin1String("cache")) ++from_cache;
                   }
                   p->report["allFinalMs"] = p->opened.elapsed();
                   self->check(ok, QStringLiteral("visible cards final after %1 ms: %2 ready (%3 from the cache, %4 rendered now), %5 with a stated error")
                                       .arg(p->opened.elapsed()).arg(ready).arg(from_cache).arg(rendered_now).arg(errors));
                   self->check(still_provisional == 0, "no card keeps the provisional mark once its portrait is verified");
                   if (salted) self->check(rendered_now > 0, "with the profile changed, out-of-date portraits are redone (not served as valid)");
                   else if (qEnvironmentVariableIsSet("SNS_PORTRAIT_EXPECT_CACHE"))
                     self->check(rendered_now == 0 && from_cache > 0, QStringLiteral("warm cache: %1 visible portraits from the cache, none redone").arg(from_cache));
                   p->report["readyFromCache"] = from_cache;
                   p->report["renderedNow"] = rendered_now;
                   p->report["errors"] = errors;
                   self->step();
                 });
      });
      // Idle: the rows ahead were handed to the prefetch and verified/rendered at low priority.
      steps.push_back([self, p, renderer] {
        self->after(8000, [self, p, renderer] {
          if (p->heartbeat) p->heartbeat->stop();
          const auto s = renderer()->stats();
          self->log(QStringLiteral("stats: provisional %1, verified %2 in %3 batches (slowest %4 ms), cache hits %5, rendered %6, stale %7, "
                                   "prefetch calls %8, prefetched renders %9, render queue %10")
                        .arg(s.value("provisional").toInt()).arg(s.value("verified").toInt()).arg(s.value("lookupBatches").toInt())
                        .arg(s.value("maxLookupBatchMs").toLongLong()).arg(s.value("cacheHits").toInt()).arg(s.value("rendered").toInt())
                        .arg(s.value("stale").toInt()).arg(s.value("prefetchRequests").toInt()).arg(s.value("prefetched").toInt())
                        .arg(s.value("renderQueue").toInt()));
          self->check(s.value("prefetchRequests").toInt() > 0, "the gallery handed the rows ahead to the prefetch");
          self->check(p->max_gap < 2000, QStringLiteral("longest event-loop pause while the gallery opened: %1 ms").arg(p->max_gap));
          p->report["maxEventLoopGapMs"] = p->max_gap;
          p->report["stats"] = QJsonObject::fromVariantMap(s);
          QFile out(QDir(self->work_dir).filePath("portraits-instant.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(p->report).toJson());
          self->step();
        });
      });
    } else if (name == "npc-portraits") {
      // Gallery portraits from the profile's own resources: the visible cards get real renders (a grab of the
      // off-screen stage) stored in the portrait cache under their NPC's FormKey; a fast scroll keeps the queue
      // bounded; cancelling empties it; a changed resource makes a portrait stale and it is rendered again; an
      // NPC that cannot be built is an error state that does not stop the others; the main viewer still works.
      // With SNS_PORTRAIT_EXPECT_CACHE (a second run over the same cache folder) the portraits come from the cache.
      struct Portraits {
        QQuickItem* browser = nullptr;
        QQuickItem* grid = nullptr;
        QElapsedTimer opened;
        qint64 memory_before = 0;
        qint64 first_ms = -1;
        QString first_source;
        QStringList ready;
        QJsonObject report;
        bool viewport_measurable = false;
        // Event-loop pauses from the gallery's opening until its first portraits are made (the cold start).
        QElapsedTimer cold_beat;
        qint64 cold_max_gap = 0;
        QTimer* cold_heartbeat = nullptr;
      };
      auto p = std::make_shared<Portraits>();
      auto renderer = [self] { return self->studio->portraits(); };
      auto final_state = [renderer](const QString& fk) {
        const auto s = renderer()->stateOf(fk, 0).value("state").toString();
        return s == QLatin1String("ready") || s == QLatin1String("error");
      };
      // Created cards, by row.
      auto cards = [p]() {
        QList<QQuickItem*> out;
        auto* content = p->grid ? p->grid->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!content) return out;
        for (auto* child : content->childItems()) {
          if (child->property("formKey").isValid()) out << child;
        }
        std::sort(out.begin(), out.end(), [](QQuickItem* a, QQuickItem* b) { return a->property("index").toInt() < b->property("index").toInt(); });
        return out;
      };
      auto first_keys = [cards](int n) {
        QStringList keys;
        for (auto* c : cards()) {
          if (keys.size() == n) break;
          keys << c->property("formKey").toString();
        }
        return keys;
      };
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(100, *tick);
        };
        (*tick)();
      };
      auto peak_mb = [] {
        PROCESS_MEMORY_COUNTERS c{};
        return K32GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ? static_cast<qint64>(c.PeakWorkingSetSize / (1024 * 1024)) : qint64(-1);
      };
      auto entry_path = [renderer](const QString& fk) {
        return QDir(renderer()->cache_directory()).filePath(PortraitCache::entry_name(fk) + ".json");
      };
      const bool expect_cache = qEnvironmentVariableIsSet("SNS_PORTRAIT_EXPECT_CACHE");
      // Share of the main viewport's central area that differs from its clear colour (read at a corner), in a
      // grab of the window; saved as `file` for the evidence.
      auto viewport_drawn = [self](const QString& file) {
        QQuickItem* view = nullptr;
        for (auto* item : self->window->findChildren<QQuickItem*>()) {
          if (item->objectName() == QLatin1String("characterView3D") && item->isVisible()) view = item;
        }
        if (!view) return -1.0;
        const QImage full = self->window->grabWindow();
        const qreal dpr = full.devicePixelRatio();
        const auto origin = view->mapToScene(QPointF(0, 0));
        const QImage image = full.copy(QRect(qRound(origin.x() * dpr), qRound(origin.y() * dpr), qRound(view->width() * dpr), qRound(view->height() * dpr)));
        image.save(QDir(self->work_dir).filePath(file));
        if (image.isNull()) return -1.0;
        const auto bg = image.pixelColor(1, 1);
        qint64 drawn = 0, total = 0;
        // Where the character stands: left of the inspector panel, between the toolbar and the status strip.
        for (int y = image.height() / 10; y < image.height() * 88 / 100; y += 2) {
          for (int x = image.width() * 12 / 100; x < image.width() * 62 / 100; x += 2) {
            const auto px = image.pixelColor(x, y);
            ++total;
            if (std::abs(px.red() - bg.red()) + std::abs(px.green() - bg.green()) + std::abs(px.blue() - bg.blue()) > 6) ++drawn;
          }
        }
        return total ? static_cast<double>(drawn) / total : 0.0;
      };
      auto viewport_before = std::make_shared<double>(-1.0);
      steps.push_back([self, p, renderer, wait_for, cards, viewport_drawn, viewport_before] {
        *viewport_before = viewport_drawn("viewport-before-portraits.png");
        self->log(QStringLiteral("main viewport before the portraits: %1 % drawn").arg(*viewport_before * 100, 0, 'f', 1));
        // The harness's own control: a second grab before any portrait. A hidden window is grabbed through a
        // temporary offscreen QRhi; when that cannot draw the scene twice, the after-check measures the harness,
        // not the portraits, and is skipped (run with -ShowWindow).
        const double again = viewport_drawn("viewport-before-portraits-2.png");
        p->viewport_measurable = *viewport_before > 0.03 && again > *viewport_before * 0.5;
        self->log(QStringLiteral("main viewport grabbed again before the portraits: %1 % drawn (%2)")
                      .arg(again * 100, 0, 'f', 1)
                      .arg(p->viewport_measurable ? "repeatable" : "not repeatable in this window"));
        p->report["viewportDrawnBeforeAgain"] = again;
        p->report["windowExposed"] = self->window->isExposed();
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorNpcBrowser")) p->browser = qobject_cast<QQuickItem*>(o);
        }
        for (auto* item : self->window->findChildren<QQuickItem*>()) {
          if (p->browser && item->objectName() == QLatin1String("npcGallery") && p->browser->isAncestorOf(item)) p->grid = item;
        }
        self->check(p->browser && p->grid, "creator NPC browser and its gallery found");
        if (!p->browser || !p->grid) { self->steps.clear(); self->step(); return; }
        wait_for([renderer] { return renderer()->stats().value("ready").toBool(); }, 30000, [self, p, renderer, wait_for, cards](bool ok) {
          self->check(ok, "portrait renderer ready (stage attached, profile digest computed): " + renderer()->cache_directory());
          p->memory_before = process_memory_mb();
          self->studio->searchNpcs({}, {}, QStringLiteral("unique"), {});
          wait_for([self] {
            const auto* rows = self->studio->npc_results();
            if (rows->count() == 0) return false;
            for (int i = 0; i < std::min(rows->count(), 50); ++i) if (!rows->get(i).value("isUnique").toBool()) return false;
            return true;
          }, 30000, [self, p, wait_for, cards](bool) {
            p->opened.start();
            p->cold_heartbeat = new QTimer(self->window);
            p->cold_heartbeat->setInterval(10);
            p->cold_beat.start();
            QObject::connect(p->cold_heartbeat, &QTimer::timeout, p->cold_heartbeat, [p] { p->cold_max_gap = std::max(p->cold_max_gap, p->cold_beat.restart()); });
            p->cold_heartbeat->start();
            p->browser->setProperty("viewMode", "gallery");
            self->window->grabWindow();  // a frame: the hidden window lays the cards out only when it renders
            wait_for([cards] { return !cards().isEmpty(); }, 30000, [self, p](bool ok) {
              self->check(ok, QStringLiteral("first card after %1 ms").arg(p->opened.elapsed()));
              self->step();
            });
          });
        });
      });
      // The first portrait on screen.
      steps.push_back([self, p, renderer, wait_for, first_keys] {
        wait_for([p, renderer, first_keys] {
          for (const auto& fk : first_keys(8)) {
            const auto s = renderer()->stateOf(fk, 0);
            if (s.value("state").toString() == QLatin1String("ready")) {
              p->first_source = s.value("source").toString();
              return true;
            }
          }
          return false;
        }, 180000, [self, p](bool ok) {
          p->first_ms = p->opened.elapsed();
          self->check(ok, QStringLiteral("first portrait after %1 ms (from %2)").arg(p->first_ms).arg(p->first_source));
          self->step();
        });
      });
      // The first visible cards: each one ends with a portrait or a stated error.
      steps.push_back([self, p, renderer, wait_for, first_keys, final_state, expect_cache, peak_mb] {
        const auto keys = first_keys(8);
        wait_for([keys, final_state] { return std::all_of(keys.begin(), keys.end(), final_state); }, 6 * 60000,
                 [self, p, renderer, keys, expect_cache, peak_mb](bool ok) {
                   int errors = 0;
                   QStringList reasons;
                   for (const auto& fk : keys) {
                     const auto s = renderer()->stateOf(fk, 0);
                     if (s.value("state").toString() == QLatin1String("ready")) p->ready << fk;
                     else if (s.value("state").toString() == QLatin1String("error")) { ++errors; reasons << fk + ": " + s.value("reason").toString(); }
                   }
                   const auto stats = renderer()->stats();
                   self->check(ok && !p->ready.isEmpty(), QStringLiteral("%1 of %2 visible cards have a portrait, %3 with a stated error%4")
                                                              .arg(p->ready.size()).arg(keys.size()).arg(errors)
                                                              .arg(reasons.isEmpty() ? QString() : " [" + reasons.join("; ") + "]"));
                   self->log(QStringLiteral("portraits: %1 rendered (average %2 ms, longest %3 ms, grabs %4 ms in total), %5 from the cache, %6 stale, %7 errors")
                                 .arg(stats.value("rendered").toInt()).arg(stats.value("averageRenderMs").toLongLong()).arg(stats.value("maxRenderMs").toLongLong())
                                 .arg(stats.value("totalCaptureMs").toLongLong()).arg(stats.value("cacheHits").toInt()).arg(stats.value("stale").toInt())
                                 .arg(stats.value("errors").toInt()));
                   if (p->cold_heartbeat) {
                     p->cold_heartbeat->stop();
                     p->cold_heartbeat->deleteLater();
                     p->cold_heartbeat = nullptr;
                     self->log(QStringLiteral("cold start: longest pause of the event loop while the first portraits were made %1 ms").arg(p->cold_max_gap));
                     p->report["coldStartMaxGapMs"] = p->cold_max_gap;
                   }
                   {
                     const auto ph = stats.value("phases").toMap();
                     self->log(QStringLiteral("portrait frame phases (longest so far): polish %1 ms, beginFrame %2 ms, sync %3 ms, render %4 ms, "
                                              "endFrame+readback %5 ms; longest GUI block %6 ms; rendering on the %7")
                                   .arg(ph.value("polishMs").toLongLong()).arg(ph.value("beginFrameMs").toLongLong())
                                   .arg(ph.value("syncMs").toLongLong()).arg(ph.value("renderMs").toLongLong())
                                   .arg(ph.value("endFrameMs").toLongLong()).arg(stats.value("maxCaptureMs").toLongLong())
                                   .arg(stats.value("renderThread").toBool() ? QStringLiteral("render thread") : QStringLiteral("GUI thread")));
                     p->report["phasesAfterVisible"] = QJsonObject::fromVariantMap(ph);
                   }
                   if (expect_cache) {
                     self->check(p->first_source == QLatin1String("cache") && stats.value("cacheHits").toInt() >= p->ready.size() && stats.value("rendered").toInt() == 0,
                                 QStringLiteral("second opening served from the cache: %1 hits, %2 rendered, first portrait from %3")
                                     .arg(stats.value("cacheHits").toInt()).arg(stats.value("rendered").toInt()).arg(p->first_source));
                   }
                   p->report["visibleCards"] = static_cast<int>(keys.size());
                   p->report["readyCards"] = static_cast<int>(p->ready.size());
                   p->report["errorCards"] = errors;
                   p->report["firstPortraitMs"] = p->first_ms;
                   p->report["firstSource"] = p->first_source;
                   p->report["memoryBeforeMb"] = p->memory_before;
                   p->report["memoryAfterMb"] = process_memory_mb();
                   p->report["peakWorkingSetMb"] = peak_mb();
                   p->report["statsAfterVisible"] = QJsonObject::fromVariantMap(stats);
                   self->log(QStringLiteral("memory: %1 MB private before the gallery, %2 MB after, peak working set %3 MB")
                                 .arg(p->memory_before).arg(process_memory_mb()).arg(peak_mb()));
                   self->step();
                 });
      });
      // Each portrait is the cache's image of its own NPC; different NPCs, different images.
      steps.push_back([self, p, renderer, entry_path] {
        QSet<QString> hashes;
        int good = 0;
        QStringList problems;
        QList<QImage> sheet;
        for (const auto& fk : std::as_const(p->ready)) {
          const auto url = renderer()->stateOf(fk, 0).value("url").toString();
          const QImage image(QUrl(url).toLocalFile());
          QFile file(entry_path(fk));
          const auto entry = file.open(QIODevice::ReadOnly) ? portrait_entry_from_json(file.readAll(), nullptr) : std::nullopt;
          if (!entry) { problems << fk + ": no cache entry"; continue; }
          if (PortraitCache::entry_name(entry->form_key) != PortraitCache::entry_name(fk)) { problems << fk + ": entry of " + entry->form_key; continue; }
          if (image.isNull() || image.width() < 128) { problems << fk + ": image unreadable"; continue; }
          if (entry->source != QLatin1String("studio-render") || entry->resources.isEmpty()) { problems << fk + ": source or resources"; continue; }
          ++good;
          hashes.insert(entry->image_sha256);
          sheet << image;
        }
        self->check(good == p->ready.size() && problems.isEmpty(),
                    QStringLiteral("%1 of %2 portraits are the cache image of their own NPC%3").arg(good).arg(p->ready.size())
                        .arg(problems.isEmpty() ? QString() : " [" + problems.join("; ") + "]"));
        if (good >= 2) self->check(hashes.size() == good, QStringLiteral("portraits of different NPCs differ: %1 distinct of %2").arg(hashes.size()).arg(good));
        if (!sheet.isEmpty()) {
          const int side = 256;
          QImage contact(side * std::min<int>(4, sheet.size()), side * ((sheet.size() + 3) / 4), QImage::Format_RGB32);
          contact.fill(Qt::black);
          QPainter painter(&contact);
          for (int i = 0; i < sheet.size(); ++i) painter.drawImage(QRect((i % 4) * side, (i / 4) * side, side, side), sheet[i]);
          painter.end();
          contact.save(QDir(self->work_dir).filePath("portraits-contact-sheet.png"));
        }
        self->after(500, [self] {
          self->window->grabWindow().save(QDir(self->work_dir).filePath("npc-portraits-gallery.png"));
          self->step();
        });
      });
      // Fast scroll: the queue stays bounded and the interface keeps running.
      steps.push_back([self, p, renderer] {
        auto beat = std::make_shared<QElapsedTimer>();
        auto max_gap = std::make_shared<qint64>(0);
        auto* heartbeat = new QTimer(self->window);
        heartbeat->setInterval(10);
        beat->start();
        QObject::connect(heartbeat, &QTimer::timeout, heartbeat, [beat, max_gap] { *max_gap = std::max(*max_gap, beat->restart()); });
        heartbeat->start();
        const int columns = std::max(1, p->grid->property("columns").toInt());
        const int rows = (p->grid->property("count").toInt() + columns - 1) / columns;
        const double cell = p->grid->property("cellHeight").toDouble();
        std::vector<std::function<void()>> jumps;
        for (int k = 1; k <= 16; ++k) {
          jumps.push_back([self, p, rows, cell, k] {
            p->grid->setProperty("contentY", ((k * 7) % std::max(1, rows - 3)) * cell);
            self->window->grabWindow();  // the frame that lays out the cards scrolled into view
          });
        }
        self->chain(std::move(jumps), 40, [self, p, renderer, heartbeat, max_gap] {
          heartbeat->stop();
          heartbeat->deleteLater();
          const auto stats = renderer()->stats();
          self->check(stats.value("queue").toInt() <= PortraitRenderer::kMaxQueue && stats.value("maxQueue").toInt() <= PortraitRenderer::kMaxQueue,
                      QStringLiteral("fast scroll: queue %1 (most %2, limit %3), %4 dropped, %5 requests; longest pause of the event loop %6 ms")
                          .arg(stats.value("queue").toInt()).arg(stats.value("maxQueue").toInt()).arg(PortraitRenderer::kMaxQueue)
                          .arg(stats.value("dropped").toInt()).arg(stats.value("requested").toInt()).arg(*max_gap));
          p->report["scrollMaxGapMs"] = *max_gap;
          p->report["statsAfterScroll"] = QJsonObject::fromVariantMap(stats);
          self->step();
        });
      });
      // Responsiveness while portraits are being made: the event loop alone (no frames forced by the harness).
      steps.push_back([self, p, renderer] {
        auto beat = std::make_shared<QElapsedTimer>();
        auto max_gap = std::make_shared<qint64>(0);
        auto* heartbeat = new QTimer(self->window);
        heartbeat->setInterval(10);
        beat->start();
        QObject::connect(heartbeat, &QTimer::timeout, heartbeat, [beat, max_gap] { *max_gap = std::max(*max_gap, beat->restart()); });
        heartbeat->start();
        const int rendered_before = renderer()->stats().value("rendered").toInt();
        self->after(20000, [self, p, renderer, heartbeat, max_gap, rendered_before] {
          heartbeat->stop();
          heartbeat->deleteLater();
          const auto stats = renderer()->stats();
          const int made = stats.value("rendered").toInt() - rendered_before;
          self->log(QStringLiteral("responsiveness: %1 portraits made in 20 s; longest pause of the event loop %2 ms; longest GUI block by a portrait %3 ms "
                                   "(longest frame %4 ms, PNG and cache %5 ms)")
                        .arg(made).arg(*max_gap).arg(stats.value("maxCaptureMs").toLongLong())
                        .arg(stats.value("maxFrameMs").toLongLong()).arg(stats.value("maxSaveMs").toLongLong()));
          p->report["renderingMaxGapMs"] = *max_gap;
          p->report["renderingPortraitsIn20s"] = made;
          self->step();
        });
      });
      // Cancelling empties the queue; back at the top the portraits come back.
      steps.push_back([self, p, renderer, wait_for, first_keys, final_state] {
        renderer()->cancelAll();
        const auto stats = renderer()->stats();
        self->check(stats.value("queue").toInt() == 0 && stats.value("rendering").toString().isEmpty(),
                    QStringLiteral("cancel: queue %1, rendering '%2'").arg(stats.value("queue").toInt()).arg(stats.value("rendering").toString()));
        p->grid->setProperty("contentY", 0);
        self->window->grabWindow();
        self->after(300, [self, p, wait_for, first_keys, final_state] {
          const auto keys = first_keys(4);
          wait_for([keys, final_state] { return !keys.isEmpty() && std::all_of(keys.begin(), keys.end(), final_state); }, 3 * 60000, [self, keys](bool ok) {
            self->check(ok, QStringLiteral("after cancelling, the %1 cards at the top have their portraits again").arg(keys.size()));
            self->step();
          });
        });
      });
      // A changed resource makes the portrait stale: it is rendered again and the entry records the resource anew.
      steps.push_back([self, p, renderer, wait_for, entry_path] {
        if (p->ready.isEmpty()) { self->step(); return; }
        const auto fk = p->ready.first();
        QFile file(entry_path(fk));
        auto entry = file.open(QIODevice::ReadOnly) ? portrait_entry_from_json(file.readAll(), nullptr) : std::nullopt;
        file.close();
        if (!entry || entry->resources.isEmpty()) { self->check(false, "cache entry to alter for " + fk); self->step(); return; }
        const auto real = entry->resources.first();
        entry->resources.first().size += 1;  // as if the file were now one byte larger
        QFile out(entry_path(fk));
        const bool written = out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(portrait_entry_to_json(*entry)) > 0;
        out.close();
        const int stale_before = renderer()->stats().value("stale").toInt();
        const int rendered_before = renderer()->stats().value("rendered").toInt();
        renderer()->refresh(fk);
        wait_for([renderer, fk, rendered_before] {
          return renderer()->stats().value("rendered").toInt() > rendered_before && renderer()->stateOf(fk, 0).value("state").toString() == QLatin1String("ready");
        }, 3 * 60000, [self, renderer, fk, entry_path, real, stale_before, written](bool ok) {
          QFile again(entry_path(fk));
          const auto now = again.open(QIODevice::ReadOnly) ? portrait_entry_from_json(again.readAll(), nullptr) : std::nullopt;
          const auto stats = renderer()->stats();
          self->check(written && ok && stats.value("stale").toInt() > stale_before && stats.value("lastStaleReason").toString().contains("resource changed") &&
                          now && now->resources.first() == real,
                      "changed resource: " + stats.value("lastStaleReason").toString() + "; rendered again from " +
                          renderer()->stateOf(fk, 0).value("source").toString());
          // The render it replaced is kept for comparison, with the resource that differed between them.
          const auto versions = renderer()->versionsOf(fk, 0);
          const auto changed = versions.value("changedResources").toStringList();
          self->check(versions.contains("previous") && versions.value("comparable").toBool() && changed.contains(real.path, Qt::CaseInsensitive),
                      QStringLiteral("comparison kept for %1: previous render %2, same renderer %3, changed %4, identical image %5")
                          .arg(fk, versions.contains("previous") ? "yes" : "no", versions.value("comparable").toBool() ? "yes" : "no",
                               changed.join(", "), versions.value("identicalImage").toBool() ? "yes" : "no"));
          self->step();
        });
      });
      // A capture attached as "taken in Skyrim" stays apart: the card keeps the studio render, the inspector
      // tells the two apart, and removing it leaves the render. The input is a file the harness writes itself
      // (a flat test image, not a real Skyrim screenshot), copied into the cache with its origin.
      steps.push_back([self, p, renderer] {
        if (p->ready.isEmpty()) { self->step(); return; }
        const auto fk = p->ready.first();
        const auto file = QDir(self->work_dir).filePath("harness-capture-input.png");
        QImage input(320, 240, QImage::Format_RGB32);
        input.fill(QColor("#305070"));
        const bool saved = input.save(file, "PNG");
        const auto before = renderer()->stateOf(fk, 0);
        const auto refused = renderer()->attachGameCapture(fk, QUrl::fromLocalFile(file));
        const auto versions = renderer()->versionsOf(fk, 0);
        const auto capture = versions.value("skyrimCapture").toMap();
        const auto after = renderer()->stateOf(fk, 0);
        self->check(saved && refused.isEmpty() && !capture.isEmpty() && capture.value("source").toString() == PortraitCache::kSkyrimCapture &&
                        capture.value("url").toString() != after.value("url").toString() && after.value("url") == before.value("url") &&
                        versions.value("current").toMap().value("source").toString() == PortraitCache::kStudioRender,
                    QStringLiteral("Skyrim capture kept apart from the studio render of %1 (%2)").arg(fk, refused.isEmpty() ? capture.value("file").toString() : refused));
        const auto not_image = QDir(self->work_dir).filePath("harness-not-an-image.png");
        if (QFile f(not_image); f.open(QIODevice::WriteOnly)) f.write("not an image");
        const auto why = renderer()->attachGameCapture(fk, QUrl::fromLocalFile(not_image));
        self->check(!why.isEmpty() && renderer()->versionsOf(fk, 0).contains("skyrimCapture"), "a file that is not an image is refused: " + why);
        // The inspector of this NPC with its three versions on screen (evidence of the interface), then removal.
        self->studio->selectNpc(fk);
        auto started = std::make_shared<QElapsedTimer>();
        started->start();
        auto shot = std::make_shared<std::function<void()>>();
        *shot = [self, renderer, fk, started, shot] {
          if (self->studio->appearance_control().value("formKey").toString() != fk && started->elapsed() < 30000) {
            self->after(250, *shot);
            return;
          }
          self->after(800, [self, renderer, fk, shot] {
            self->window->grabWindow().save(QDir(self->work_dir).filePath("inspector-versions.png"));
            renderer()->removeGameCapture(fk);
            self->check(!renderer()->versionsOf(fk, 0).contains("skyrimCapture") && renderer()->versionsOf(fk, 0).contains("current"),
                        "removing the capture leaves the studio render");
            *shot = nullptr;
            self->step();
          });
        };
        (*shot)();
      });
      // An NPC that cannot be built ends in an error state; the next portrait is still made.
      steps.push_back([self, p, renderer, wait_for] {
        const QString missing = QStringLiteral("FFFFFF:Skyrim.esm");
        renderer()->request(missing);
        wait_for([renderer, missing] { return renderer()->stateOf(missing, 0).value("state").toString() == QLatin1String("error"); }, 120000,
                 [self, p, renderer, wait_for, missing](bool ok) {
                   self->check(ok && !renderer()->stateOf(missing, 0).value("reason").toString().isEmpty(),
                               "unknown NPC: error state (" + renderer()->stateOf(missing, 0).value("reason").toString() + ")");
                   if (p->ready.size() < 2) { self->step(); return; }
                   const auto next = p->ready.at(1);
                   QFile::remove(QDir(renderer()->cache_directory()).filePath(PortraitCache::entry_name(next) + ".json"));
                   const int rendered_before = renderer()->stats().value("rendered").toInt();
                   renderer()->refresh(next);
                   wait_for([renderer, next, rendered_before] {
                     return renderer()->stats().value("rendered").toInt() > rendered_before && renderer()->stateOf(next, 0).value("state").toString() == QLatin1String("ready");
                   }, 3 * 60000, [self, next](bool done) {
                     self->check(done, "after the error, the next portrait is made: " + next);
                     self->step();
                   });
                 });
      });
      // A profile reload (a new MO2 snapshot of the same profile; no setting changes) while portraits are being
      // made: the work stops at once, and once the profile is indexed again the visible cards get theirs back.
      steps.push_back([self, p, renderer, wait_for, cards, entry_path] {
        if (p->ready.size() < 2) { self->step(); return; }
        for (int i = 0; i < 2; ++i) {  // two portraits to make again, so something is in flight
          QFile::remove(entry_path(p->ready.at(i)));
          renderer()->refresh(p->ready.at(i));
        }
        auto clock = std::make_shared<QElapsedTimer>();
        wait_for([renderer] { return !renderer()->stats().value("rendering").toString().isEmpty(); }, 30000,
                 [self, p, renderer, wait_for, cards, clock](bool busy) {
                   const int cancelled_before = renderer()->stats().value("cancelled").toInt();
                   clock->start();
                   self->studio->reindex();
                   const auto stats = renderer()->stats();
                   self->check(busy && stats.value("queue").toInt() == 0 && stats.value("rendering").toString().isEmpty() &&
                                   stats.value("cancelled").toInt() > cancelled_before && !stats.value("ready").toBool(),
                               QStringLiteral("profile reload cancels portrait work at once: queue %1, rendering '%2', %3 cancelled")
                                   .arg(stats.value("queue").toInt()).arg(stats.value("rendering").toString())
                                   .arg(stats.value("cancelled").toInt() - cancelled_before));
                   // Every visible card of the new profile ends with a portrait or a stated error, and some have one.
                   auto first_ready_ms = std::make_shared<qint64>(-1);
                   wait_for([self, renderer, cards, clock, first_ready_ms] {
                     if (self->studio->phase() != StudioController::Phase::Ready || !renderer()->stats().value("ready").toBool()) return false;
                     const auto visible = cards();
                     if (visible.isEmpty()) return false;
                     int done = 0, ready = 0;
                     for (auto* c : visible.mid(0, 8)) {
                       const auto state = renderer()->stateOf(c->property("formKey").toString(), 0).value("state").toString();
                       if (state == QLatin1String("ready")) ++ready;
                       if (state == QLatin1String("ready") || state == QLatin1String("error")) ++done;
                     }
                     if (ready > 0 && *first_ready_ms < 0) *first_ready_ms = clock->elapsed();
                     return ready > 0 && done == std::min<int>(8, visible.size());
                   }, 5 * 60000, [self, p, renderer, cards, clock, first_ready_ms](bool back) {
                     QStringList states;
                     for (auto* c : cards().mid(0, 8)) {
                       const auto fk = c->property("formKey").toString();
                       states << fk + "=" + renderer()->stateOf(fk, 0).value("state").toString();
                     }
                     self->check(back, QStringLiteral("after the reload the visible cards end with a portrait or a stated error after %1 ms "
                                                      "(first portrait after %2 ms): %3")
                                           .arg(clock->elapsed()).arg(*first_ready_ms).arg(states.join(", ")));
                     p->report["profileReloadToFirstPortraitMs"] = *first_ready_ms;
                     p->report["profileReloadToVisibleDoneMs"] = clock->elapsed();
                     self->step();
                   });
                 });
      });
      // The main viewer is not affected by the portrait scene: it still holds and draws the character.
      steps.push_back([self, p, renderer, wait_for, viewport_drawn, viewport_before] {
        self->studio->selectNpc(self->primary_npc);
        wait_for([self] {
          return self->studio->npc().value("summary").toMap().value("formKey").toString() == self->primary_npc && !self->scene()->loading() &&
                 !self->scene()->parts().isEmpty();
        }, 60000, [self, p, renderer, viewport_drawn, viewport_before](bool ok) {
          self->check(ok, QStringLiteral("main viewer shows %1: %2 parts").arg(self->primary_npc).arg(self->scene()->parts().size()));
          const double after = viewport_drawn("viewport-after-portraits.png");
          const auto what = QStringLiteral("main viewport still draws the character after the portraits: %1 % drawn (%2 % before)")
                                .arg(after * 100, 0, 'f', 1).arg(*viewport_before * 100, 0, 'f', 1);
          if (p->viewport_measurable) {
            self->check(after > 0.03 && after > *viewport_before * 0.5, what);
          } else {
            self->skip(what + QStringLiteral(" - the window cannot be grabbed twice here (hidden); use -ShowWindow"));
          }
          p->report["viewportDrawnBefore"] = *viewport_before;
          p->report["viewportDrawnAfter"] = after;
          QJsonObject missing;
          for (const auto& fk : p->ready) {  // from the cache entries: the reload cleared the states
            QFile file(QDir(renderer()->cache_directory()).filePath(PortraitCache::entry_name(fk) + ".json"));
            const auto entry = file.open(QIODevice::ReadOnly) ? portrait_entry_from_json(file.readAll(), nullptr) : std::nullopt;
            const auto files = entry ? PortraitRenderer::missing_of(entry->resources) : QStringList{};
            if (!files.isEmpty()) missing[fk] = QJsonArray::fromStringList(files);
          }
          self->log(QStringLiteral("portraits drawn without some files of the profile: %1 of %2 %3")
                        .arg(missing.size()).arg(p->ready.size()).arg(QString::fromUtf8(QJsonDocument(missing).toJson(QJsonDocument::Compact))));
          p->report["missingResources"] = missing;
          p->report["readyKeys"] = QJsonArray::fromStringList(p->ready);
          p->report["statsFinal"] = QJsonObject::fromVariantMap(renderer()->stats());
          p->report["cacheDirectory"] = renderer()->cache_directory();
          QFile out(QDir(self->work_dir).filePath("portraits-report.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(p->report).toJson());
          p->browser->setProperty("viewMode", "list");
          self->step();
        });
      });
    } else if (name == "npc-gallery-paging") {
      // The whole catalogue through the gallery: reaching the end fetches the next page of the same search
      // (appended, never reset) until every NPC of the profile is loaded; rows stay unique and each card the NPC
      // of its row; the interface keeps running; donor searches never touch the browser's rows.
      struct Paging {
        QQuickItem* browser = nullptr;
        QQuickItem* grid = nullptr;
        QElapsedTimer clock;
        int pages = 0;
        int last_loaded = 0;
        qint64 memory_before = 0;
      };
      auto g = std::make_shared<Paging>();
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); *tick = nullptr; return; }
          if (timer->elapsed() > ms) { next(false); *tick = nullptr; return; }
          self->after(100, *tick);
        };
        (*tick)();
      };
      steps.push_back([self, g, wait_for] {
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String("creatorNpcBrowser")) g->browser = qobject_cast<QQuickItem*>(o);
        }
        for (auto* item : self->window->findChildren<QQuickItem*>()) {
          if (g->browser && item->objectName() == QLatin1String("npcGallery") && g->browser->isAncestorOf(item)) g->grid = item;
        }
        self->check(g->browser && g->grid, "creator NPC browser and its gallery found");
        if (!g->browser || !g->grid) { self->steps.clear(); self->step(); return; }
        self->studio->searchNpcs({}, {}, {}, {});
        wait_for([self] { return self->studio->npc_total() > StudioController::kNpcPage && self->studio->npc_loaded() == StudioController::kNpcPage; },
                 30000, [self, g](bool ok) {
                   self->check(ok, QStringLiteral("search of every NPC: %1 in the profile, first page %2 rows")
                                       .arg(self->studio->npc_total()).arg(self->studio->npc_loaded()));
                   g->browser->setProperty("viewMode", "gallery");
                   self->window->grabWindow();
                   self->step();
                 });
      });
      // Scroll to the end again and again: each time the next page arrives, until all are loaded.
      steps.push_back([self, g] {
        auto beat = std::make_shared<QElapsedTimer>();
        auto max_gap = std::make_shared<qint64>(0);
        auto* heartbeat = new QTimer(self->window);
        heartbeat->setInterval(10);
        beat->start();
        QObject::connect(heartbeat, &QTimer::timeout, heartbeat, [beat, max_gap] { *max_gap = std::max(*max_gap, beat->restart()); });
        heartbeat->start();
        g->memory_before = process_memory_mb();
        g->last_loaded = self->studio->npc_loaded();
        g->clock.start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, g, heartbeat, max_gap, tick] {
          const int loaded = self->studio->npc_loaded();
          if (loaded > g->last_loaded) { ++g->pages; g->last_loaded = loaded; }
          const bool all = loaded >= self->studio->npc_total();
          if (all || g->clock.elapsed() > 5 * 60000) {
            heartbeat->stop();
            heartbeat->deleteLater();
            const auto* rows = self->studio->npc_results();
            QSet<QString> keys;
            QHash<QString, int> first_row;
            QStringList duplicates;
            int across_pages = 0;  // the same NPC on two pages: paging would be broken
            for (int i = 0; i < rows->count(); ++i) {
              const auto row = rows->get(i);
              const auto key = PortraitCache::entry_name(row.value("formKey").toString());
              if (keys.contains(key)) {
                duplicates << QStringLiteral("%1 rows %2/%3 '%4'").arg(row.value("formKey").toString()).arg(first_row.value(key)).arg(i).arg(row.value("name").toString());
                if (first_row.value(key) / StudioController::kNpcPage != i / StudioController::kNpcPage) ++across_pages;
              } else {
                first_row.insert(key, i);
              }
              keys.insert(key);
            }
            // The same FormKey twice inside one page comes from the worker's catalogue itself (e.g. a master named
            // with another letter case by an override): a finding of the catalogue, not of the paging.
            if (!duplicates.isEmpty()) self->log(QStringLiteral("catalogue finding: %1 FormKey(s) listed twice (letter case of the master): %2")
                                                     .arg(duplicates.size()).arg(duplicates.join("; ")));
            self->log(QStringLiteral("gallery at the check: contentY %1, contentHeight %2, height %3, count %4")
                          .arg(g->grid->property("contentY").toDouble()).arg(g->grid->property("contentHeight").toDouble())
                          .arg(g->grid->property("height").toDouble()).arg(g->grid->property("count").toInt()));
            self->check(all && rows->count() == self->studio->npc_total() && across_pages == 0 &&
                            g->grid->property("count").toInt() == rows->count(),
                        QStringLiteral("whole catalogue through the gallery: %1 of %2 rows in %3 more page(s), %4 distinct, %5 repeated across pages, %6 cards in the view, %7 ms")
                            .arg(rows->count()).arg(self->studio->npc_total()).arg(g->pages).arg(keys.size()).arg(across_pages)
                            .arg(g->grid->property("count").toInt()).arg(g->clock.elapsed()));
            // The cards on screen at the end are the NPCs of their rows.
            auto* content = g->grid->property("contentItem").value<QQuickItem*>();
            int cards = 0, wrong = 0, lowest = -1;
            for (auto* c : content ? content->childItems() : QList<QQuickItem*>{}) {
              if (!c->property("formKey").isValid()) continue;
              // Only the cards in the viewport (the view also keeps its current item alive, row 0 after a reset).
              const double top = g->grid->property("contentY").toDouble();
              if (c->y() + c->height() <= top || c->y() >= top + g->grid->height()) continue;
              ++cards;
              const int index = c->property("index").toInt();
              lowest = lowest < 0 ? index : std::min(lowest, index);
              if (c->property("formKey").toString() != rows->get(index).value("formKey").toString()) ++wrong;
            }
            self->check(cards > 0 && wrong == 0 && lowest > StudioController::kNpcPage,
                        QStringLiteral("%1 cards at the end of the catalogue compared with their rows (from row %2), %3 wrong").arg(cards).arg(lowest).arg(wrong));
            const auto stats = self->studio->portraits()->stats();
            self->check(stats.value("maxQueue").toInt() <= PortraitRenderer::kMaxQueue,
                        QStringLiteral("portrait queue while paging: most %1 (limit %2), %3 requests")
                            .arg(stats.value("maxQueue").toInt()).arg(PortraitRenderer::kMaxQueue).arg(stats.value("requested").toInt()));
            self->log(QStringLiteral("paging: longest pause of the event loop %1 ms (longest portrait capture on the GUI thread %2 ms); memory %3 MB private before, %4 MB after")
                          .arg(*max_gap).arg(stats.value("maxCaptureMs").toLongLong()).arg(g->memory_before).arg(process_memory_mb()));
            self->window->grabWindow().save(QDir(self->work_dir).filePath("npc-gallery-end.png"));
            *tick = nullptr;
            self->step();
            return;
          }
          g->grid->setProperty("contentY", std::max(0.0, g->grid->property("contentHeight").toDouble() - g->grid->property("height").toDouble()));
          self->window->grabWindow();  // the frame that lays out the cards (and reports atYEnd)
          self->after(150, *tick);
        };
        (*tick)();
      });
      // Donor searches have their own model: the browser keeps its rows.
      steps.push_back([self, wait_for] {
        const auto* rows = self->studio->npc_results();
        const int count = rows->count();
        const auto first = rows->get(0).value("formKey").toString();
        const auto last = rows->get(count - 1).value("formKey").toString();
        self->studio->searchDonors(QStringLiteral("Lydia"));
        wait_for([self] { return self->studio->donor_results()->count() > 0; }, 30000, [self, count, first, last](bool ok) {
          const auto* now = self->studio->npc_results();
          self->check(ok && now->count() == count && now->get(0).value("formKey").toString() == first &&
                          now->get(count - 1).value("formKey").toString() == last,
                      QStringLiteral("donor search (%1 rows) leaves the browser's %2 rows as they were").arg(self->studio->donor_results()->count()).arg(now->count()));
          self->step();
        });
      });
      // A new search starts again from its first page.
      steps.push_back([self, g, wait_for] {
        self->studio->searchNpcs({}, {}, QStringLiteral("unique"), {});
        wait_for([self] { return self->studio->npc_loaded() > 0 && self->studio->npc_results()->get(0).value("isUnique").toBool(); }, 30000, [self, g](bool ok) {
          self->check(ok && self->studio->npc_loaded() == std::min(StudioController::kNpcPage, self->studio->npc_total()),
                      QStringLiteral("a new search starts from its first page: %1 of %2").arg(self->studio->npc_loaded()).arg(self->studio->npc_total()));
          self->studio->portraits()->cancelAll();
          g->browser->setProperty("viewMode", "list");
          self->step();
        });
      });
    } else if (name == "source-compare") {
      // Reference integration A1: the Appearance Source Matrix of the NPC on screen and its A/B view. Read-only: it checks
      // that every source has an honest verdict, that the «Origen» section lists them, that the A/B dialog draws the two
      // sources with the same stage and camera (two different pictures of the same size and framing), that the draft is
      // written only to an isolated folder (SNS_SOURCE_DRAFT_DIR; otherwise those checks are skipped), and that the
      // profile's FaceGen files are byte for byte what they were. Captures and metrics go to the work folder. Not part
      // of "all": it needs an NPC with two drawable sources.
      struct SourceRun {
        QString a_id, b_id;
        QByteArray before_hash;
      };
      auto run = std::make_shared<SourceRun>();
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(100, *tick);
        };
        (*tick)();
      };
      auto find_object = [self](const char* object_name) -> QObject* {
        for (auto* o : self->window->findChildren<QObject*>()) {
          if (o->objectName() == QLatin1String(object_name)) return o;
        }
        return nullptr;
      };
      // Hash of every file a candidate would be drawn from: the proof that nothing in the profile was written.
      auto candidates_hash = [self] {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto& c : self->studio->sources()->matrix().value("candidates").toList()) {
          for (const auto& o : c.toMap().value("overlay").toList()) {
            QFile file(o.toMap().value("file").toString());
            hash.addData(o.toMap().value("file").toString().toUtf8());
            if (file.open(QIODevice::ReadOnly)) hash.addData(file.readAll());
          }
        }
        return hash.result();
      };
      steps.push_back([self, run, wait_for, candidates_hash] {
        wait_for([self] { return self->studio->sources()->matrix().value("state").toString() == QLatin1String("ready"); }, 90000,
                 [self, run, candidates_hash](bool ok) {
          const auto matrix = self->studio->sources()->matrix();
          self->check(ok, "source matrix ready for " + matrix.value("formKey").toString());
          const auto candidates = matrix.value("candidates").toList();
          self->check(!candidates.isEmpty() && candidates.first().toMap().value("effective").toBool(), "the effective source is the first candidate");
          QSet<QString> ids;
          bool honest = true;
          static const QSet<QString> statuses{"VALID", "UNVERIFIED", "CONFLICT", "AMBIGUOUS", "MISSING"};
          for (const auto& c : candidates) {
            const auto m = c.toMap();
            ids.insert(m.value("id").toString());
            honest = honest && statuses.contains(m.value("status").toString());
            // Nothing unverified is presented as available: drawable => it says what the picture is; missing => never drawable.
            if (m.value("renderable").toBool()) honest = honest && !m.value("renderNote").toString().isEmpty();
            if (m.value("status").toString() == QLatin1String("MISSING")) honest = honest && !m.value("renderable").toBool();
            self->log(QStringLiteral("source %1 [%2] record %3 (%4) facegen «%5» drawable=%6 codes=%7")
                          .arg(m.value("id").toString(), m.value("status").toString(), m.value("recordPlugin").toString(), m.value("recordMod").toString(),
                               m.value("facegenMod").toString()).arg(m.value("renderable").toBool()).arg(m.value("codes").toStringList().join(",")));
          }
          self->check(ids.size() == candidates.size(), QStringLiteral("%1 candidate ids, all unique").arg(candidates.size()));
          self->check(honest, "every verdict is one of the five statuses and no unverified source is presented as drawable");
          self->check(matrix.value("profileDigest").toString().size() == 64 && matrix.value("inputDigest").toString().size() == 64,
                      "profile digest and input digest are SHA-256 (64 hex)");
          self->log(QStringLiteral("source matrix: %1 ms, %2 B read, %3 drawable, skipped: %4")
                        .arg(matrix.value("gatherMs").toLongLong()).arg(matrix.value("readBytes").toLongLong()).arg(matrix.value("renderable").toInt())
                        .arg(matrix.value("skipped").toStringList().join(" | ")));
          QFile out(QDir(self->work_dir).filePath("source-matrix.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(matrix)).toJson());
          run->before_hash = candidates_hash();
          self->step();
        });
      });
      // The «Origen» tab lists them.
      steps.push_back([self, find_object] {
        auto* tabs = find_object("creatorRightTabs");
        self->check(tabs != nullptr, "«Origen» tab found");
        if (!tabs) { self->step(); return; }
        tabs->setProperty("currentIndex", 5);
        self->after(500, [self] {
          QQuickItem* section = nullptr;
          QQuickItem* panel = nullptr;
          for (auto* item : self->window->findChildren<QQuickItem*>()) {
            if (item->objectName() == QLatin1String("sourceSection")) section = item;
            if (item->objectName() == QLatin1String("appearanceControlPanel")) panel = item;
          }
          // Repeater delegates are not QObject children of the window: walk the visual hierarchy.
          int rows = 0;
          std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
            for (auto* child : item->childItems()) {
              if (child->objectName().startsWith(QLatin1String("sourceRow_")) && child->isVisible()) ++rows;
              walk(child);
            }
          };
          if (section) walk(section);
          const int expected = self->studio->sources()->matrix().value("candidates").toList().size();
          self->check(section && section->isVisible(), "«Fuentes de apariencia» section visible in «Origen»");
          self->check(rows == expected, QStringLiteral("%1 source rows for %2 candidates").arg(rows).arg(expected));
          // Scroll the tab to the section so the capture shows it.
          if (section && panel) {
            if (auto* flick = qvariant_cast<QQuickItem*>(panel->property("contentItem"))) {
              if (auto* content = qvariant_cast<QQuickItem*>(flick->property("contentItem"))) {
                flick->setProperty("contentY", std::max(0.0, section->mapToItem(content, QPointF(0, 0)).y() - 24.0));
              }
            }
          }
          self->after(300, [self] {
            self->window->grabWindow().save(QDir(self->work_dir).filePath("source-section.png"));
            self->step();
          });
        });
      });
      // The A/B dialog: two sources drawn with the same stage and camera.
      steps.push_back([self, run, wait_for, find_object] {
        const auto matrix = self->studio->sources()->matrix();
        if (matrix.value("renderable").toInt() < 2) {
          self->skip(QStringLiteral("A/B view: this NPC has %1 drawable source(s); it needs two (use an NPC with a shadowed FaceGen)").arg(matrix.value("renderable").toInt()));
          self->step();
          return;
        }
        auto* dialog = find_object("sourceCompareDialog");
        self->check(dialog != nullptr, "A/B dialog found");
        if (!dialog) { self->step(); return; }
        QMetaObject::invokeMethod(dialog, "open");
        const auto a = self->studio->sources()->candidate_a(), b = self->studio->sources()->candidate_b();
        run->a_id = a;
        run->b_id = b;
        self->check(!a.isEmpty() && !b.isEmpty() && a != b, "A and B are two different sources: " + a + " vs " + b);
        // Settled = both states are final AND the pictures are loaded by the dialog's (asynchronous) images, so the capture is not blank.
        auto settled = [self, a, b, dialog] {
          const auto ia = self->studio->sources()->imageOf(a), ib = self->studio->sources()->imageOf(b);
          const auto done = [](const QVariantMap& i) {
            const auto s = i.value("state").toString();
            return s == QLatin1String("ready") || s == QLatin1String("error") || s == QLatin1String("unavailable");
          };
          if (!done(ia) || !done(ib)) return false;
          auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
          if (!content) return false;
          std::function<QQuickItem*(QQuickItem*, const QString&)> find_in = [&](QQuickItem* root, const QString& name) -> QQuickItem* {
            for (auto* child : root->childItems()) {
              if (child->objectName() == name) return child;
              if (auto* found = find_in(child, name)) return found;
            }
            return nullptr;
          };
          const auto loaded = [&](const QString& name, const QVariantMap& image) {
            if (image.value("state").toString() != QLatin1String("ready")) return true;  // an explicit error needs no picture
            auto* item = find_in(content, name);
            return item && item->property("status").toInt() == 1;  // QQuickImage::Ready
          };
          return loaded(QStringLiteral("sourceImage_A"), ia) && loaded(QStringLiteral("sourceImage_B"), ib);
        };
        wait_for(settled, 180000, [self, run, dialog, a, b](bool ok) {
          self->check(ok, "both pictures settled (ready or an explicit error)");
          const auto ia = self->studio->sources()->imageOf(a), ib = self->studio->sources()->imageOf(b);
          self->check(ia.value("state").toString() == QLatin1String("ready") && ib.value("state").toString() == QLatin1String("ready"),
                      QStringLiteral("A %1 (%2), B %3 (%4)").arg(ia.value("state").toString(), ia.value("reason").toString(), ib.value("state").toString(), ib.value("reason").toString()));
          const QImage img_a(QUrl(ia.value("url").toString()).toLocalFile()), img_b(QUrl(ib.value("url").toString()).toLocalFile());
          if (img_a.isNull() || img_b.isNull()) { self->check(false, "both pictures are readable PNG files"); self->step(); return; }
          self->check(img_a.size() == img_b.size() && img_a.width() >= 64, QStringLiteral("same size: %1x%2 vs %3x%4").arg(img_a.width()).arg(img_a.height()).arg(img_b.width()).arg(img_b.height()));
          // Same stage and camera => same background and a head framed alike; the faces differ (different FaceGen files).
          const auto fa = img_a.convertToFormat(QImage::Format_RGB32), fb = img_b.convertToFormat(QImage::Format_RGB32);
          const QRgb bg_a = fa.pixel(0, 0), bg_b = fb.pixel(0, 0);
          qint64 differ = 0, total = 0;
          QRect box_a, box_b;
          auto grow = [](QRect& box, int x, int y) { box = box.isNull() ? QRect(x, y, 1, 1) : box.united(QRect(x, y, 1, 1)); };
          for (int y = 0; y < fa.height() && y < fb.height(); ++y) {
            for (int x = 0; x < fa.width() && x < fb.width(); ++x) {
              const QRgb pa = fa.pixel(x, y), pb = fb.pixel(x, y);
              if (std::abs(qRed(pa) - qRed(pb)) + std::abs(qGreen(pa) - qGreen(pb)) + std::abs(qBlue(pa) - qBlue(pb)) > 24) ++differ;
              if (std::abs(qRed(pa) - qRed(bg_a)) + std::abs(qGreen(pa) - qGreen(bg_a)) + std::abs(qBlue(pa) - qBlue(bg_a)) > 24) grow(box_a, x, y);
              if (std::abs(qRed(pb) - qRed(bg_b)) + std::abs(qGreen(pb) - qGreen(bg_b)) + std::abs(qBlue(pb) - qBlue(bg_b)) > 24) grow(box_b, x, y);
              ++total;
            }
          }
          const double diff_share = total > 0 ? 100.0 * static_cast<double>(differ) / static_cast<double>(total) : 0.0;
          self->check(bg_a == bg_b, "same stage background in both pictures");
          // One camera for both sides, proved by the numbers: the NPC's own framing exists, every variant render took it and none
          // framed itself. (The pixel boxes below only catch gross mis-framing: hair differs between the two faces.)
          const auto frame = self->studio->portraits()->frameOf(self->studio->sources()->matrix().value("formKey").toString());
          const auto stats = self->studio->portraits()->stats();
          const auto source_metrics = self->studio->sources()->metrics();
          if (source_metrics.value("variantsRendered").toInt() == 0 && source_metrics.value("variantCacheHits").toInt() > 0) {
            // Nothing was drawn in this run: the picture comes from the verified cache, rendered earlier under the shared framing.
            self->skip("shared-camera proof: B came from the verified cache (the proof is made by the run that renders it)");
          } else {
            self->check(!frame.isEmpty() && frame.value("distance").toFloat() > 0.0f,
                        QStringLiteral("the NPC's own framing is known: distance %1").arg(frame.value("distance").toFloat()));
            self->check(stats.value("variantsFramed").toInt() >= 1 && stats.value("variantsSelfFramed").toInt() == 0,
                        QStringLiteral("variant renders with the shared camera: %1, framing themselves: %2")
                            .arg(stats.value("variantsFramed").toInt()).arg(stats.value("variantsSelfFramed").toInt()));
          }
          const QRect overlap = box_a.intersected(box_b), joined = box_a.united(box_b);
          const double iou = joined.isEmpty() ? 0.0 : static_cast<double>(overlap.width()) * overlap.height() / (static_cast<double>(joined.width()) * joined.height());
          self->check(!box_a.isNull() && !box_b.isNull() && iou > 0.5,
                      QStringLiteral("heads overlap alike (IoU %1): A box %2,%3 %4x%5, B box %6,%7 %8x%9").arg(iou, 0, 'f', 2).arg(box_a.x()).arg(box_a.y())
                          .arg(box_a.width()).arg(box_a.height()).arg(box_b.x()).arg(box_b.y()).arg(box_b.width()).arg(box_b.height()));
          self->log(QStringLiteral("A/B pictures differ in %1 % of the pixels (%2 of %3)").arg(diff_share, 0, 'f', 2).arg(differ).arg(total));
          self->check(diff_share > 0.05, QStringLiteral("the two sources produce different faces (%1 % of pixels differ)").arg(diff_share, 0, 'f', 2));
          const QImage shot = self->window->grabWindow();
          shot.save(QDir(self->work_dir).filePath("source-compare-ab.png"));
          img_a.save(QDir(self->work_dir).filePath("source-picture-A.png"));
          img_b.save(QDir(self->work_dir).filePath("source-picture-B.png"));
          int inspected = 0;
          auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
          const auto problems = content ? layout_problems(content, false, &inspected) : QStringList{QStringLiteral("no content item")};
          self->check(problems.isEmpty(), QStringLiteral("A/B dialog %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
          // Keyboard: the dialog's content holds the focus; Space alternates between A and B.
          QTest::keyClick(self->window, Qt::Key_Space);
          self->check(dialog->property("blink").toBool() && dialog->property("showingB").toBool(), "Space switches to the alternate view showing B");
          QTest::keyClick(self->window, Qt::Key_Space);
          self->check(!dialog->property("showingB").toBool(), "Space again shows A");
          // Alternate view and shared zoom.
          dialog->setProperty("blink", true);
          dialog->setProperty("showingB", true);
          self->after(300, [self, dialog] {
            self->window->grabWindow().save(QDir(self->work_dir).filePath("source-compare-blink-B.png"));
            dialog->setProperty("showingB", false);
            QMetaObject::invokeMethod(dialog, "applyZoom", Q_ARG(QVariant, 2.5));
            self->after(300, [self, dialog] {
              self->check(dialog->property("zoom").toReal() > 2.4, "zoom shared by both sides: " + dialog->property("zoom").toString());
              self->window->grabWindow().save(QDir(self->work_dir).filePath("source-compare-blink-A-zoom.png"));
              dialog->setProperty("blink", false);
              QMetaObject::invokeMethod(dialog, "resetView");
              self->step();
            });
          });
        });
      });
      // The grid of 3-4 sources at once (needs an NPC with three drawable sources).
      steps.push_back([self, wait_for, find_object] {
        const auto matrix = self->studio->sources()->matrix();
        if (matrix.value("renderable").toInt() < 3) {
          self->skip(QStringLiteral("grid of several sources: this NPC has %1 drawable source(s); it needs three").arg(matrix.value("renderable").toInt()));
          self->step();
          return;
        }
        auto* dialog = find_object("sourceCompareDialog");
        self->check(dialog != nullptr, "the compare dialog is still there");
        auto* content = dialog ? qvariant_cast<QQuickItem*>(dialog->property("contentItem")) : nullptr;
        if (!content) { self->step(); return; }
        auto find_in = std::make_shared<std::function<QQuickItem*(QQuickItem*, const QString&)>>();
        *find_in = [find_in](QQuickItem* root, const QString& name) -> QQuickItem* {
          for (auto* child : root->childItems()) {
            if (child->objectName() == name) return child;
            if (auto* found = (*find_in)(child, name)) return found;
          }
          return nullptr;
        };
        // The way a person does it: the switch of the dialog.
        auto* toggle = (*find_in)(content, QStringLiteral("sourceMultiSwitch"));
        self->check(toggle != nullptr && toggle->isEnabled(), "the 'several at once' switch is enabled with three drawable sources");
        if (!toggle) { self->step(); return; }
        QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        self->check(dialog->property("multi").toBool(), "the dialog is in grid mode");
        auto ids = [dialog] { return dialog->property("multiIds").toStringList(); };
        self->check(ids().size() >= 3 && ids().size() <= 4, QStringLiteral("the grid starts with %1 sources (3-4)").arg(ids().size()));
        auto settled = [self, dialog, content, find_in, ids] {
          const auto list = ids();
          for (int i = 0; i < list.size(); ++i) {
            const auto image = self->studio->sources()->imageOf(list[i]);
            const auto state = image.value("state").toString();
            if (state != QLatin1String("ready") && state != QLatin1String("error") && state != QLatin1String("unavailable")) return false;
            if (state == QLatin1String("ready")) {
              auto* item = (*find_in)(content, QStringLiteral("sourceImage_%1").arg(i + 1));
              if (!item || item->property("status").toInt() != 1) return false;
            }
          }
          return !list.isEmpty();
        };
        wait_for(settled, 240000, [self, dialog, content, find_in, ids](bool ok) {
          self->check(ok, "every picture of the grid settled");
          const auto list = ids();
          std::vector<QImage> images;
          for (const auto& id : list) {
            const auto image = self->studio->sources()->imageOf(id);
            self->check(image.value("state").toString() == QLatin1String("ready"), QStringLiteral("%1: %2 %3").arg(id, image.value("state").toString(), image.value("reason").toString()));
            images.emplace_back(QUrl(image.value("url").toString()).toLocalFile());
          }
          bool same_size = true, all_readable = true;
          for (const auto& image : images) {
            all_readable &= !image.isNull();
            same_size &= !image.isNull() && image.size() == images.front().size();
          }
          self->check(all_readable && same_size, "all pictures of the grid are readable and the same size");
          // Every pair of sources produces a different face, over one background (same stage, camera and lights).
          bool distinct = true;
          QStringList shares;
          for (std::size_t i = 0; i < images.size() && all_readable && same_size; ++i) {
            for (std::size_t j = i + 1; j < images.size(); ++j) {
              const auto fa = images[i].convertToFormat(QImage::Format_RGB32), fb = images[j].convertToFormat(QImage::Format_RGB32);
              qint64 differ = 0, total = 0;
              for (int y = 0; y < fa.height(); ++y)
                for (int x = 0; x < fa.width(); ++x, ++total) {
                  const QRgb pa = fa.pixel(x, y), pb = fb.pixel(x, y);
                  if (std::abs(qRed(pa) - qRed(pb)) + std::abs(qGreen(pa) - qGreen(pb)) + std::abs(qBlue(pa) - qBlue(pb)) > 24) ++differ;
                }
              const double share = total > 0 ? 100.0 * static_cast<double>(differ) / static_cast<double>(total) : 0.0;
              shares << QStringLiteral("%1-%2: %3 %").arg(i + 1).arg(j + 1).arg(share, 0, 'f', 1);
              distinct &= share > 0.05;
              distinct &= fa.pixel(0, 0) == fb.pixel(0, 0);
            }
          }
          self->log("grid pairs differ: " + shares.join(", "));
          self->check(distinct, "every pair of sources in the grid differs and shares the stage background");
          // Panes on screen, one per source; zoom is shared by all of them.
          int panes = 0;
          for (int i = 1; i <= 4; ++i) {
            if (auto* pane = (*find_in)(content, QStringLiteral("sourcePane_%1").arg(i)); pane && pane->isVisible()) ++panes;
          }
          self->check(panes == list.size(), QStringLiteral("%1 panes are visible for %2 sources").arg(panes).arg(list.size()));
          auto* note = (*find_in)(content, QStringLiteral("sourceGridNote"));
          self->check(note && note->isVisible(), "the 'not a Skyrim capture' notice is on screen in grid mode");
          int inspected = 0;
          const auto problems = layout_problems(content, false, &inspected);
          self->check(problems.isEmpty(), QStringLiteral("grid dialog %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
          self->window->grabWindow().save(QDir(self->work_dir).filePath("source-compare-grid.png"));
          QMetaObject::invokeMethod(dialog, "applyZoom", Q_ARG(QVariant, 2.0));
          self->after(300, [self, dialog, content, find_in, list] {
            bool shared = true;
            for (int i = 1; i <= list.size(); ++i) {
              auto* pane = (*find_in)(content, QStringLiteral("sourcePane_%1").arg(i));
              shared &= pane && pane->property("zoom").toReal() > 1.9;
            }
            self->check(shared, "zoom is shared by every pane of the grid");
            self->window->grabWindow().save(QDir(self->work_dir).filePath("source-compare-grid-zoom.png"));
            QMetaObject::invokeMethod(dialog, "resetView");
            // Back to the A/B view.
            QMetaObject::invokeMethod(dialog, "setMulti", Q_ARG(QVariant, false));
            self->after(300, [self, dialog, content, find_in] {
              auto* pane_a = (*find_in)(content, QStringLiteral("sourcePane_A"));
              self->check(!dialog->property("multi").toBool() && pane_a && pane_a->isVisible(), "switching the grid off brings the A/B view back");
              self->step();
            });
          });
        });
      });
      // Draft: only in an isolated folder, never the user's.
      steps.push_back([self] {
        const auto isolated = qEnvironmentVariable("SNS_SOURCE_DRAFT_DIR");
        const auto path = self->studio->sources()->draft().value("path").toString();
        if (isolated.isEmpty() || !QDir::fromNativeSeparators(path).startsWith(QDir::fromNativeSeparators(isolated), Qt::CaseInsensitive)) {
          self->skip("draft checks: SNS_SOURCE_DRAFT_DIR is not set (the user's own draft file is never written by a harness)");
          self->step();
          return;
        }
        auto* s = self->studio->sources();
        self->check(s->draft().value("state").toString() == QLatin1String("empty"), "no draft at the start: " + s->draft().value("state").toString());
        const auto error = s->saveDraft(s->candidate_b(), QStringLiteral("harness"));
        self->check(error.isEmpty(), "draft saved: " + error);
        self->check(s->draft().value("state").toString() == QLatin1String("fresh") && QFile::exists(path), "draft is fresh and its file exists: " + path);
        self->check(s->draft().value("chosen").toString() == s->candidate_b(), "the preferred source is stored");
        // A draft names only sources the matrix offers.
        self->check(!s->setCompare(QStringLiteral("facegen:no-such-mod"), s->candidate_b()), "an unknown source is refused");
        s->clearDraft();
        self->check(s->draft().value("state").toString() == QLatin1String("empty"), "draft discarded");
        self->step();
      });
      steps.push_back([self, run, candidates_hash] {
        auto* s = self->studio->sources();
        const auto m = s->metrics();
        self->log(QStringLiteral("source metrics: gathers %1, last gather %2 ms (max %3), read %4 B, variants requested %5 / rendered %6 / cache hits %7 / errors %8, "
                                 "variant render avg %9 ms max %10 ms, GUI-thread handlers p95 %11 ms max %12 ms (%13 samples)")
                      .arg(m.value("gathers").toInt()).arg(m.value("lastGatherMs").toLongLong()).arg(m.value("maxGatherMs").toLongLong())
                      .arg(m.value("lastReadBytes").toLongLong()).arg(m.value("variantsRequested").toInt()).arg(m.value("variantsRendered").toInt())
                      .arg(m.value("variantCacheHits").toInt()).arg(m.value("variantErrors").toInt()).arg(m.value("averageVariantRenderMs").toLongLong())
                      .arg(m.value("maxVariantRenderMs").toLongLong()).arg(m.value("guiP95Ms").toLongLong()).arg(m.value("guiMaxMs").toLongLong())
                      .arg(m.value("guiSamples").toInt()));
        QFile out(QDir(self->work_dir).filePath("source-metrics.json"));
        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson());
        self->check(m.value("variantErrors").toInt() == 0, "no variant render failed");
        // Read-only: the profile's FaceGen files are what they were (no write path exists, this proves it held).
        self->check(candidates_hash() == run->before_hash, "the FaceGen files the sources were drawn from are unchanged");
        self->step();
      });
    } else if (name == "preset-head-check") {
      // Reference integration A3: Preset <-> Export Head readiness of EVERY .jslot of the real profile. Diagnosis only: nothing is
      // applied or exported. Checks the invariants (a valid verdict, the UNVERIFIED_IN_SKYRIM caveat, never READY with an unreadable
      // head, a chosen head that exists), the manual choice, and writes one row per preset plus captures of the dialog. Not in "all".
      auto rows = std::make_shared<QJsonArray>();
      auto presets = std::make_shared<QVariantList>();
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(50, *tick);
        };
        (*tick)();
      };
      auto settled = [self] {
        const auto s = self->studio->preset_heads()->report().value("state").toString();
        return s == QLatin1String("ready") || s == QLatin1String("error");
      };
      steps.push_back([self, presets] {
        self->studio->appearance()->loadPresetsFromVfs();
        *presets = self->studio->appearance()->property("presetsList").toList();
        self->check(!presets->isEmpty(), QStringLiteral("%1 .jslot presets found in the profile").arg(presets->size()));
        self->step();
      });
      steps.push_back([self, rows, presets, wait_for, settled] {
        auto index = std::make_shared<int>(0);
        auto next = std::make_shared<std::function<void()>>();
        *next = [self, rows, presets, wait_for, settled, index, next] {
          if (*index >= presets->size()) { self->step(); return; }
          const auto preset = presets->at((*index)++).toMap();
          self->studio->preset_heads()->check(preset.value("diskPath").toString());
          // The report of the PREVIOUS preset is still there for a moment: wait for this one's.
          wait_for([self, preset, settled] {
                     const auto r = self->studio->preset_heads()->report();
                     return settled() && r.value("preset").toMap().value("path").toString() == preset.value("diskPath").toString();
                   }, 120000,
                   [self, rows, preset, next](bool ok) {
            const auto r = self->studio->preset_heads()->report();
            const auto verdict = r.value("verdict").toString();
            static const QSet<QString> verdicts{"READY_FOR_REVIEW", "NEEDS_ASSET", "NEEDS_CHOICE", "FACEGEN_UNREADABLE", "HEAD_PART_MISMATCH", "UNSUPPORTED_TOPOLOGY"};
            const auto name = preset.value("name").toString();
            self->check(ok && r.value("state").toString() == QLatin1String("ready"),
                        QStringLiteral("%1: checked (%2) %3").arg(name, r.value("state").toString(), r.value("error").toString()));
            self->check(verdicts.contains(verdict), QStringLiteral("%1: verdict %2").arg(name, verdict));
            self->check(r.value("caveats").toStringList().contains(QStringLiteral("UNVERIFIED_IN_SKYRIM")), name + ": carries the UNVERIFIED_IN_SKYRIM caveat");
            const auto chosen = r.value("chosen").toMap();
            if (verdict == QLatin1String("READY_FOR_REVIEW")) {
              self->check(!chosen.isEmpty() && chosen.value("readable").toBool(), name + ": READY only with a readable chosen head");
            }
            if (verdict == QLatin1String("NEEDS_CHOICE")) self->check(r.value("needsManualChoice").toBool() && chosen.isEmpty(), name + ": NEEDS_CHOICE has no head chosen");
            self->log(QStringLiteral("preset %1 -> %2 [%3] head %4 (%5 ranked) :: %6")
                          .arg(name, verdict, r.value("codes").toStringList().join(","), chosen.value("path").toString(),
                               QString::number(r.value("ranked").toList().size()), r.value("message").toString().left(140)));
            rows->append(QJsonObject{{"preset", name}, {"mod", preset.value("sourceMod").toString()}, {"verdict", verdict},
                                     {"codes", QJsonArray::fromStringList(r.value("codes").toStringList())}, {"head", chosen.value("path").toString()},
                                     {"ranked", r.value("ranked").toList().size()}, {"message", r.value("message").toString()},
                                     {"checkMs", r.value("timings").toMap().value("checkMs").toLongLong()}});
            self->after(0, *next);
          });
        };
        (*next)();
      });
      steps.push_back([self, rows] {
        QMap<QString, int> counts;
        for (const auto& v : *rows) ++counts[v.toObject().value("verdict").toString()];
        QStringList parts;
        for (auto it = counts.begin(); it != counts.end(); ++it) parts << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
        const auto heads = self->studio->preset_heads()->heads();
        self->log(QStringLiteral("preset heads: %1 presets, verdicts: %2; %3 export heads read (%4 unreadable) in %5 ms")
                      .arg(rows->size()).arg(parts.join(", ")).arg(heads.value("count").toInt()).arg(heads.value("unreadable").toInt()).arg(heads.value("ms").toLongLong()));
        QFile out(QDir(self->work_dir).filePath("preset-head-report.json"));
        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(*rows).toJson());
        self->check(heads.value("state").toString() == QLatin1String("ready") && heads.value("count").toInt() > 0, "the export heads of the profile were indexed");
        self->step();
      });
      // Manual choice on the first preset, then the dialog captured.
      steps.push_back([self, presets, wait_for, settled] {
        if (presets->isEmpty()) { self->step(); return; }
        auto* checker = self->studio->preset_heads();
        const auto preset = presets->first().toMap();
        checker->check(preset.value("diskPath").toString());
        wait_for(settled, 120000, [self, checker, preset, wait_for, settled](bool) {
          const auto all = checker->report().value("allHeads").toList();
          if (all.isEmpty()) { self->skip("manual choice: the profile has no export heads"); self->step(); return; }
          const int pick = all.last().toMap().value("index").toInt();
          self->check(checker->choose(pick), "choose() accepts a head of the profile");
          self->check(!checker->choose(100000), "choose() refuses an unknown head");
          const auto head_label = all.last().toMap().value("label").toString();
          const auto preset_key = preset.value("diskPath").toString().toLower();
          wait_for([checker, pick] { const auto r = checker->report(); return r.value("manual").toBool() && r.value("chosen").toMap().value("index").toInt() == pick; }, 60000,
                   [self, checker, pick, head_label, preset_key, wait_for](bool ok) {
            self->check(ok, QStringLiteral("the manual choice (head #%1) became the chosen one and is flagged manual").arg(pick));
            // The choice is remembered per profile. Only in an isolated folder: a harness never writes the user's own choices file.
            const auto file = checker->choices_file();
            const auto isolated = qEnvironmentVariable("SNS_PRESET_HEAD_DIR");
            if (file.isEmpty() || isolated.isEmpty() || !QDir::fromNativeSeparators(file).startsWith(QDir::fromNativeSeparators(isolated), Qt::CaseInsensitive)) {
              self->skip("remembered choices: SNS_PRESET_HEAD_DIR is not set (the user's own choices file is never written by a harness)");
              checker->clearChoice();
              self->step();
              return;
            }
            const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
            const auto write = [](const QString& path, const QByteArray& bytes) {
              QFile f(path);
              if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(bytes);
            };
            const auto original = read(file);
            const auto doc = QJsonDocument::fromJson(original).object();
            bool found = false;
            for (const auto& v : doc.value("choices").toArray()) {
              const auto o = v.toObject();
              found |= o.value("preset").toString() == preset_key && o.value("head").toString() == head_label && !o.value("chosenAt").toString().isEmpty();
            }
            self->check(doc.value("schema").toInt() == 1 && found, "the manual choice was written to the choices file with its date");
            const auto manual_pick = [checker, pick] {
              const auto r = checker->report();
              return r.value("state").toString() == QLatin1String("ready") && r.value("manual").toBool() && !r.value("manualStale").toBool() &&
                     r.value("chosen").toMap().value("index").toInt() == pick;
            };
            // A remembered head that is no longer in the profile is ignored and said so.
            write(file, QJsonDocument(QJsonObject{{"schema", 1}, {"profileKey", "harness"},
                                                  {"choices", QJsonArray{QJsonObject{{"preset", preset_key}, {"head", "Gone mod: SKSE\\Plugins\\CharGen\\gone.nif"}, {"chosenAt", "2026-01-01T00:00:00.000+00:00"}}}}})
                      .toJson());
            checker->reloadChoices();
            wait_for([checker] {
              const auto r = checker->report();
              return r.value("state").toString() == QLatin1String("ready") && r.value("manualStale").toBool() && !r.value("manual").toBool();
            }, 60000, [self, checker, file, original, read, write, manual_pick, wait_for](bool stale_ok) {
              self->check(stale_ok, "a remembered choice whose head is gone is ignored and flagged (manualStale)");
              write(file, original);
              checker->reloadChoices();  // what the next session finds
              wait_for(manual_pick, 60000, [self, checker, file, original, read, write, manual_pick, wait_for](bool back_ok) {
                self->check(back_ok, "after reloading, the remembered choice pairs the same head again");
                write(file, QByteArray("{ not json"));
                checker->reloadChoices();
                wait_for([checker] { return !checker->report().value("persistError").toString().isEmpty(); }, 60000,
                         [self, checker, file, original, read, write, manual_pick, wait_for](bool bad_ok) {
                  self->check(bad_ok, "an unreadable choices file is reported to the person");
                  checker->choose(0);
                  self->after(600, [self, checker, file, original, read, write, manual_pick, wait_for] {
                    self->check(read(file) == QByteArray("{ not json"), "an unreadable choices file is never overwritten");
                    write(file, original);
                    checker->reloadChoices();
                    wait_for(manual_pick, 60000, [self, checker, file, read, wait_for](bool) {
                      checker->clearChoice();
                      wait_for([checker] { return !checker->report().value("manual").toBool(); }, 60000, [self, file, read](bool cleared) {
                        self->check(cleared && QJsonDocument::fromJson(read(file)).object().value("choices").toArray().isEmpty(), "removing the choice removes it from the file");
                        self->step();
                      });
                    });
                  });
                });
              });
            });
          });
        });
      });
      steps.push_back([self, rows, presets, wait_for, settled] {
        // Capture the dialog for the first READY preset and the first one that is not (if any).
        QVariantList wanted;
        QSet<QString> seen;
        for (std::size_t i = 0; i < static_cast<std::size_t>(rows->size()) && wanted.size() < 2; ++i) {
          const auto verdict = rows->at(static_cast<int>(i)).toObject().value("verdict").toString();
          const bool ready = verdict == QLatin1String("READY_FOR_REVIEW");
          if (seen.contains(ready ? "r" : "o")) continue;
          seen.insert(ready ? "r" : "o");
          wanted << presets->at(static_cast<int>(i));
        }
        auto index = std::make_shared<int>(0);
        auto next = std::make_shared<std::function<void()>>();
        *next = [self, wanted, wait_for, settled, index, next] {
          if (*index >= wanted.size()) { self->step(); return; }
          const auto preset = wanted.at((*index)++).toMap();
          QObject* dialog = nullptr;
          for (auto* o : self->window->findChildren<QObject*>()) {
            if (o->objectName() == QLatin1String("presetHeadDialog")) dialog = o;
          }
          self->check(dialog != nullptr, "the Export Head dialog exists");
          if (!dialog) { self->step(); return; }
          self->studio->preset_heads()->check(preset.value("diskPath").toString());
          QMetaObject::invokeMethod(dialog, "open");
          wait_for([self, preset, settled] {
                     return settled() && self->studio->preset_heads()->report().value("preset").toMap().value("path").toString() == preset.value("diskPath").toString();
                   }, 120000,
                   [self, dialog, preset, next, index](bool) {
            self->after(500, [self, dialog, preset, next, index] {
              QString file = preset.value("name").toString();
              file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
              self->window->grabWindow().save(QDir(self->work_dir).filePath(QStringLiteral("preset-head-dialog-%1.png").arg(file)));
              QMetaObject::invokeMethod(dialog, "close");
              self->after(150, *next);
            });
          });
        };
        (*next)();
      });
    } else if (name == "cache-inspector") {
      // Reference integration A2: the portrait cache inspector against the REAL profile. Only runs in an isolated portrait cache
      // folder (SNS_PORTRAIT_CACHE_DIR): it seeds known cases there (valid, stale for each reason, damaged, previous, capture, orphan,
      // a foreign file, A/B variants), checks the counts and what may be removed, opens the dialog from the Settings Center, removes
      // the unusable and proves that everything else survived. Not in "all".
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(50, *tick);
        };
        (*tick)();
      };
      auto find_item = std::make_shared<std::function<QQuickItem*(QQuickItem*, const QString&)>>();
      *find_item = [find_item](QQuickItem* root, const QString& name) -> QQuickItem* {
        if (!root) return nullptr;
        if (root->objectName() == name && root->isVisible()) return root;
        for (auto* child : root->childItems())
          if (auto* hit = (*find_item)(child, name)) return hit;
        return nullptr;
      };
      auto ready = [self] { return self->studio->portraits()->cache_report().value("state").toString() == QLatin1String("ready"); };
      // The inspector dialog, from any visible item of its content: the QObject ancestor named portraitCacheInspector.
      auto dialog_of = [](QQuickWindow* window, const std::function<QQuickItem*(QQuickItem*, const QString&)>& find) -> QObject* {
        for (QObject* o = find(window->contentItem(), QStringLiteral("inspectorSummary")); o; o = o->parent()) {
          if (o->objectName() == QLatin1String("portraitCacheInspector")) return o;
        }
        return nullptr;
      };
      steps.push_back([self, wait_for] {
        const auto isolated = qEnvironmentVariable("SNS_PORTRAIT_CACHE_DIR");
        auto* portraits = self->studio->portraits();
        const auto dir = portraits->cache_directory();
        if (isolated.isEmpty() || dir.isEmpty() || !QDir::fromNativeSeparators(dir).startsWith(QDir::fromNativeSeparators(isolated), Qt::CaseInsensitive)) {
          self->skip("cache-inspector: SNS_PORTRAIT_CACHE_DIR is not set (the user's own portrait cache is never seeded or emptied by a harness)");
          self->finish();
          return;
        }
        wait_for([portraits] { return !portraits->snapshot_digest().isEmpty(); }, 120000, [self](bool ok) {
          self->check(ok, "the profile's digest is ready");
          self->step();
        });
      });
      // The empty folder first.
      steps.push_back([self, wait_for, ready] {
        self->studio->portraits()->inspectCache();
        wait_for(ready, 60000, [self](bool ok) {
          const auto r = self->studio->portraits()->cache_report();
          self->check(ok, "an empty cache is inspected");
          self->check(r.value("total").toInt() == 0 && r.value("unusableRows").toInt() == 0, "an empty cache has nothing to decide");
          self->step();
        });
      });
      steps.push_back([self] {
        auto* portraits = self->studio->portraits();
        const auto dir = portraits->cache_directory();
        const auto digest = portraits->snapshot_digest();
        PortraitCache cache(dir);
        QString error;
        const auto png = [](QColor color) {
          QImage image(64, 64, QImage::Format_RGB32);
          image.fill(color);
          QByteArray bytes;
          QBuffer buffer(&bytes);
          buffer.open(QIODevice::WriteOnly);
          image.save(&buffer, "PNG");
          return bytes;
        };
        const QString mesh = QStringLiteral("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif");
        const auto real = PortraitRenderer::fingerprint_of(portraits->snapshot(), mesh);
        const auto stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        const auto make = [&](const QString& form_key, const QString& renderer, const QString& snapshot_digest, QList<PortraitResource> resources) {
          PortraitEntry e;
          e.form_key = form_key;
          e.renderer_version = renderer;
          e.snapshot_digest = snapshot_digest;
          e.resources = std::move(resources);
          e.width = 64;
          e.height = 64;
          e.created_at = stamp;
          return e;
        };
        const auto renderer = PortraitRenderer::kRendererVersion;
        bool stored = true;
        stored &= cache.store(make("0A2C8E:Skyrim.esm", renderer, digest, {real}), png(Qt::red), &error);            // valid, real fingerprint
        stored &= cache.store(make("013364:Skyrim.esm", renderer, digest, {}), png(Qt::green), &error);               // valid
        stored &= cache.store(make("01A690:Skyrim.esm", renderer, digest, {}), png(Qt::blue), &error);                // valid + previous
        stored &= cache.store(make("01A690:Skyrim.esm", renderer, digest, {}), png(Qt::yellow), &error);
        stored &= cache.store(make("000001:Skyrim.esm", QStringLiteral("portrait-v0"), digest, {}), png(Qt::cyan), &error);        // stale: renderer
        stored &= cache.store(make("000002:Skyrim.esm", renderer, QStringLiteral("another-profile"), {}), png(Qt::magenta), &error);  // stale: profile
        auto moved = real;
        moved.size += 7;
        stored &= cache.store(make("000003:Skyrim.esm", renderer, digest, {moved}), png(Qt::gray), &error);                // stale: a resource
        stored &= cache.store(make("000004:Skyrim.esm", renderer, digest, {}), png(Qt::darkRed), &error);                  // damaged: image gone
        self->check(stored, "the cache was seeded: " + error);
        if (const auto damaged = cache.current("000004:Skyrim.esm")) QFile::remove(damaged->image_path);
        const QDir folder(dir);
        QFile orphan(folder.filePath("00000005_skyrim.esm-bbbbbbbbbbbb.png"));
        if (orphan.open(QIODevice::WriteOnly)) orphan.write("orphan");
        QFile foreign(folder.filePath("notes.txt"));
        if (foreign.open(QIODevice::WriteOnly)) foreign.write("not the studio's");
        const auto shot = QDir(self->work_dir).filePath("game-shot.png");
        QImage picture(32, 32, QImage::Format_RGB32);
        picture.fill(Qt::white);
        picture.save(shot, "PNG");
        self->check(cache.store_game_capture("0A2C8E:Skyrim.esm", shot, stamp, &error), "a Skyrim capture was attached: " + error);
        PortraitCache variants(folder.filePath("variants"));
        self->check(variants.store(make("variant:test", renderer, digest, {}), png(Qt::darkBlue), &error), "an A/B variant was stored: " + error);
        self->step();
      });
      steps.push_back([self, wait_for, ready] {
        self->studio->portraits()->inspectCache();
        wait_for(ready, 60000, [self](bool ok) {
          auto* portraits = self->studio->portraits();
          const auto r = portraits->cache_report();
          const auto c = r.value("counts").toMap();
          self->check(ok, "the seeded cache is inspected");
          self->log(QStringLiteral("cache report: %1 rows in %2 ms, counts valid=%3 stale=%4 damaged=%5 kept=%6 captures=%7 orphans=%8 unknown=%9, unusable %10 rows / %11 B")
                        .arg(r.value("total").toInt()).arg(r.value("ms").toLongLong()).arg(c.value("valid").toInt()).arg(c.value("stale").toInt())
                        .arg(c.value("damaged").toInt()).arg(c.value("kept").toInt()).arg(c.value("captures").toInt()).arg(c.value("orphans").toInt())
                        .arg(c.value("unknown").toInt()).arg(r.value("unusableRows").toInt()).arg(r.value("unusableBytes").toLongLong()));
          self->check(c.value("valid").toInt() == 3, "3 renders are valid (one with a real fingerprint of the profile)");
          self->check(c.value("stale").toInt() == 3, "3 renders are stale");
          self->check(c.value("damaged").toInt() == 1, "1 render is damaged");
          self->check(c.value("kept").toInt() == 1 && c.value("captures").toInt() == 1, "the previous render and the Skyrim capture are kept");
          self->check(c.value("orphans").toInt() == 1 && c.value("unknown").toInt() == 1, "1 orphan image and 1 foreign file");
          self->check(r.value("unusableRows").toInt() == 5, "5 rows can be removed: 3 stale, 1 damaged, 1 orphan");
          QSet<QString> reasons;
          bool protected_ok = true;
          for (const auto& v : r.value("rows").toList()) {
            const auto row = v.toMap();
            reasons.insert(row.value("code").toString());
            const auto kind = row.value("kind").toString();
            if (row.value("unusable").toBool() && (row.value("state").toString() == QLatin1String("valid") || kind == QLatin1String("capture") ||
                                                  kind == QLatin1String("previous") || row.value("code").toString() == QLatin1String("unknown-file"))) {
              protected_ok = false;
            }
          }
          self->check(protected_ok, "a valid render, a previous render, a capture and a foreign file are never removable");
          for (const auto& code : {"renderer-changed", "profile-changed", "resource-changed", "image-missing", "orphan-image", "unknown-file", "ok"}) {
            self->check(reasons.contains(QString::fromLatin1(code)), QStringLiteral("a row explains itself with %1").arg(code));
          }
          self->check(r.value("variants").toMap().value("files").toInt() == 2, "the A/B variant folder is counted apart (entry + image)");
          // The inspector agrees with the cache's own lookup() on every render.
          PortraitCache cache(portraits->cache_directory());
          const auto snapshot = portraits->snapshot();
          int disagree = 0;
          for (const auto& v : r.value("rows").toList()) {
            const auto row = v.toMap();
            if (row.value("kind").toString() != QLatin1String("render") || row.value("formKey").toString().isEmpty()) continue;
            const auto found = cache.lookup(row.value("formKey").toString(), PortraitRenderer::kRendererVersion, portraits->snapshot_digest(),
                                            [&snapshot](const QString& p) { return PortraitRenderer::fingerprint_of(snapshot, p); });
            if ((found.state == PortraitCacheState::Valid) != (row.value("state").toString() == QLatin1String("valid"))) ++disagree;
          }
          self->check(disagree == 0, "the inspector and PortraitCache::lookup() agree on every render");
          QFile out(QDir(self->work_dir).filePath("cache-report.json"));
          if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson());
          self->step();
        });
      });
      // The dialog, reached the way a person reaches it: Settings Center -> portrait cache -> "Inspeccionar la caché…".
      steps.push_back([self, wait_for, find_item] {
        auto* center = self->window->findChild<QObject*>(QStringLiteral("settingsCenter"));
        self->check(center != nullptr, "the Settings Center exists");
        if (!center) { self->step(); return; }
        QMetaObject::invokeMethod(center, "openAt", Q_ARG(QVariant, QStringLiteral("npc")), Q_ARG(QVariant, QStringLiteral("npc.portraitCache")));
        wait_for([self, find_item] { return (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton")) != nullptr; }, 15000,
                 [self, find_item, wait_for](bool ok) {
          self->check(ok, "the Settings Center shows the portrait cache panel with its inspector button");
          if (!ok) { self->step(); return; }
          auto* button = (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton"));
          self->check(button->isEnabled(), "the inspector button is enabled with a profile loaded");
          // The Settings Center scrolls to the setting it was asked for: click when the button has stopped moving and is on screen.
          auto last = std::make_shared<QPointF>(-1, -1);
          auto stable = std::make_shared<int>(0);
          wait_for([self, find_item, last, stable] {
            auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton"));
            if (!b) return false;
            const auto at = b->mapToScene(QPointF(b->width() / 2, b->height() / 2));
            const bool on_screen = at.y() > 140 && at.y() < self->window->height() - 110 && at.x() > 0 && at.x() < self->window->width();
            *stable = (on_screen && at == *last) ? *stable + 1 : 0;
            *last = at;
            return *stable >= 6;  // 300 ms without moving
          }, 15000, [self, find_item, wait_for](bool settled) {
            self->check(settled, "the inspector button is on screen and still");
            auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton"));
            if (!settled || !b) { self->step(); return; }
            self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-settings-panel.png"));
            const auto p = b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint();
            QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, p);
            self->step();
          });
        });
      });
      steps.push_back([self, wait_for, ready, find_item, dialog_of] {
        // The dialog lives inside a Component of the Settings Center (not in the window's QObject tree): it is reached through
        // an item of its content, which is only in the item tree while the popup is open.
        wait_for([self, find_item] { return (*find_item)(self->window->contentItem(), QStringLiteral("inspectorSummary")) != nullptr; }, 10000,
                 [self, wait_for, ready, find_item, dialog_of](bool opened) {
          QObject* dialog = dialog_of(self->window, *find_item);
          self->check(opened && dialog != nullptr, "clicking the button opened the inspector dialog");
          if (!dialog) { self->step(); return; }
          wait_for([dialog] { return dialog->property("opened").toBool(); }, 10000, [self, dialog, wait_for, ready](bool ok) {
          self->check(ok, "the inspector reports itself opened");
          wait_for(ready, 60000, [self, dialog, wait_for](bool) {
            self->after(400, [self, dialog, wait_for] {
              self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-inspector-attention.png"));
              const auto shown = dialog->property("shown").toList();
              self->check(shown.size() == 5, QStringLiteral("the default filter lists the 5 rows that need a decision (%1)").arg(shown.size()));
              int inspected = 0;
              auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
              const auto problems = content ? layout_problems(content, false, &inspected) : QStringList{QStringLiteral("no content item")};
              self->check(problems.isEmpty(), QStringLiteral("inspector %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
              dialog->setProperty("filter", QStringLiteral("all"));
              self->after(400, [self, dialog, wait_for] {
                self->check(dialog->property("shown").toList().size() == 11, "the 'all' filter lists the 11 rows");
                // The names of the NPCs come from the catalog of the real profile (Lydia and Ingun are in the seeded cache).
                const auto key_of = [self](const QString& form_key) { return self->studio->npcKey(form_key); };
                wait_for([self, key_of] {
                  const auto names = self->studio->npc_names();
                  return !names.value(key_of("0A2C8E:Skyrim.esm")).toString().isEmpty() && !names.value(key_of("013364:Skyrim.esm")).toString().isEmpty();
                }, 30000, [self, dialog, key_of](bool named) {
                  const auto names = self->studio->npc_names();
                  const auto lydia = names.value(key_of("0A2C8E:Skyrim.esm")).toString();
                  const auto ingun = names.value(key_of("013364:Skyrim.esm")).toString();
                  self->log("npc names: 0A2C8E = '" + lydia + "', 013364 = '" + ingun + "', 000001 = '" + names.value(key_of("000001:Skyrim.esm")).toString() + "'");
                  self->check(named && lydia.contains(QStringLiteral("Lydia"), Qt::CaseInsensitive) && ingun.contains(QStringLiteral("Ingun"), Qt::CaseInsensitive),
                              "the catalog's names appear for the NPCs of the cache (Lydia, Ingun)");
                  self->check(names.contains(key_of("000001:Skyrim.esm")) && names.value(key_of("000001:Skyrim.esm")).toString().isEmpty(), "an NPC the catalog does not have is remembered as unknown (not asked again)");
                  self->after(400, [self, dialog] {
                    self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-inspector-all.png"));
                    dialog->setProperty("filter", QStringLiteral("attention"));
                    self->step();
                  });
                });
              });
            });
          });
          });
        });
      });
      // What the confirm dialog of the Settings Center asks for: remove the unusable, nothing else.
      steps.push_back([self, wait_for, find_item, dialog_of] {
        auto* portraits = self->studio->portraits();
        const QDir folder(portraits->cache_directory());
        const auto outside = QDir(self->work_dir).filePath("game-shot.png");
        const auto good = portraits->cache_report().value("rows").toList();
        QStringList survivors;
        for (const auto& v : good) {
          const auto row = v.toMap();
          if (!row.value("unusable").toBool()) survivors << row.value("files").toStringList();
        }
        portraits->removeUnusable();
        wait_for([portraits] { const auto r = portraits->cache_report(); return r.value("state").toString() == QLatin1String("ready") && r.value("lastPurge").isValid() && !r.value("lastPurge").isNull(); }, 60000,
                 [self, portraits, folder, outside, survivors, find_item, dialog_of](bool ok) {
          const auto r = portraits->cache_report();
          const auto purge = r.value("lastPurge").toMap();
          const auto c = r.value("counts").toMap();
          self->check(ok, "the removal finished");
          self->log(QStringLiteral("purge: %1 rows, %2 files, %3 B, error '%4'; after: valid=%5 stale=%6 damaged=%7 kept=%8 captures=%9 orphans=%10 unknown=%11")
                        .arg(purge.value("rows").toInt()).arg(purge.value("files").toInt()).arg(purge.value("bytes").toLongLong()).arg(purge.value("error").toString())
                        .arg(c.value("valid").toInt()).arg(c.value("stale").toInt()).arg(c.value("damaged").toInt()).arg(c.value("kept").toInt())
                        .arg(c.value("captures").toInt()).arg(c.value("orphans").toInt()).arg(c.value("unknown").toInt()));
          self->check(purge.value("rows").toInt() == 5 && purge.value("files").toInt() == 8 && purge.value("error").toString().isEmpty(),
                      "5 rows / 8 files were removed (3 stale entries and images, 1 damaged entry, 1 orphan) without errors");
          self->check(c.value("stale").toInt() == 0 && c.value("damaged").toInt() == 0 && c.value("orphans").toInt() == 0 && r.value("unusableRows").toInt() == 0, "nothing unusable is left");
          self->check(c.value("valid").toInt() == 3 && c.value("kept").toInt() == 1 && c.value("captures").toInt() == 1 && c.value("unknown").toInt() == 1,
                      "the 3 valid renders, the previous one, the capture and the foreign file survived");
          bool all_there = true;
          for (const auto& f : survivors) all_there &= QFile::exists(folder.filePath(f));
          self->check(all_there, QStringLiteral("every file of a row that stays is still on disk (%1 files)").arg(survivors.size()));
          self->check(QFile::exists(folder.filePath("notes.txt")) && QFile::exists(outside), "the foreign file and the user's own image were not touched");
          self->check(r.value("variants").toMap().value("files").toInt() == 2, "the A/B variants were not touched by this removal");
          QObject* dialog = dialog_of(self->window, *find_item);
          self->after(400, [self, dialog] {
            self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-inspector-after-purge.png"));
            if (dialog) QMetaObject::invokeMethod(dialog, "close");
            self->step();
          });
        });
      });
      steps.push_back([self, wait_for] {
        auto* portraits = self->studio->portraits();
        self->check(portraits->clearVariants().isEmpty(), "the A/B images were removed");
        wait_for([portraits] { return portraits->cache_report().value("variants").toMap().value("files").toInt() == 0; }, 30000, [self, portraits](bool ok) {
          self->check(ok, "the report shows no A/B files any more");
          self->check(portraits->cache_report().value("counts").toMap().value("valid").toInt() == 3, "the gallery's renders were not touched by clearing the A/B images");
          self->step();
        });
      });
    } else if (name == "skin-match") {
      // Reference integration A4: the head <-> body skin match (an ESTIMATE of the seam at the neck) of several real NPCs of the profile.
      // Checks the invariants of every result (verdict, thresholds, ΔE of each part, the ESTIMATE / UNVERIFIED_IN_SKYRIM caveats, an
      // unmeasured body said as such) and captures the card in «Origen». Writes skin-match-report.json. Not in "all".
      auto rows = std::make_shared<QJsonArray>();
      QStringList npcs = {self->primary_npc, QStringLiteral("013364:Skyrim.esm"), QStringLiteral("013BB4:Skyrim.esm"), QStringLiteral("0136BE:Skyrim.esm"),
                          QStringLiteral("01412C:Skyrim.esm"), QStringLiteral("01325C:Skyrim.esm"), QStringLiteral("013B99:Skyrim.esm"),
                          QStringLiteral("01361F:Skyrim.esm"), QStringLiteral("0918E2:Skyrim.esm")};
      if (qEnvironmentVariableIsSet("SNS_STRESS_NPCS")) npcs = qEnvironmentVariable("SNS_STRESS_NPCS").split(',', Qt::SkipEmptyParts);
      auto index = std::make_shared<int>(0);
      auto next = std::make_shared<std::function<void()>>();
      auto captured = std::make_shared<bool>(false);
      *next = [self, rows, npcs, index, next, captured] {
        if (*index >= npcs.size()) { self->step(); return; }
        const auto form_key = npcs.at((*index)++);
        self->studio->selectNpc(form_key);
        self->when_idle([self, rows, form_key, next, captured] {
          const auto m = self->scene()->skin_match();
          const auto verdict = m.value("verdict").toString();
          static const QSet<QString> verdicts{"CONSISTENT", "NOTICEABLE", "LIKELY_SEAM", "UNKNOWN"};
          self->check(m.value("state").toString() == QLatin1String("ready") && verdicts.contains(verdict), form_key + ": a skin match with verdict " + verdict);
          const auto caveats = m.value("caveats").toStringList();
          self->check(caveats.contains(QStringLiteral("ESTIMATE")) && caveats.contains(QStringLiteral("UNVERIFIED_IN_SKYRIM")), form_key + ": carries ESTIMATE and UNVERIFIED_IN_SKYRIM");
          const auto pairs = m.value("pairs").toList();
          const double consistent = m.value("consistentMax").toDouble(), noticeable = m.value("noticeableMax").toDouble();
          double worst = 0;
          QStringList parts;
          bool finite = true;
          for (const auto& p : pairs) {
            const auto d = p.toMap().value("deltaE").toDouble();
            finite &= std::isfinite(d) && d >= 0.0;
            worst = std::max(worst, d);
            parts << QStringLiteral("%1 %2 %3 «%4»").arg(p.toMap().value("role").toString()).arg(d, 0, 'f', 1, QLatin1Char(' ')).arg(p.toMap().value("color").toString(), p.toMap().value("label").toString().left(60));
          }
          self->check(finite, form_key + ": every ΔE is finite and not negative");
          if (verdict == QLatin1String("UNKNOWN")) {
            self->check(pairs.isEmpty() && !m.value("codes").toStringList().isEmpty(), form_key + ": UNKNOWN names why (" + m.value("codes").toStringList().join(",") + ")");
          } else {
            self->check(!pairs.isEmpty() && std::abs(worst - m.value("worst").toDouble()) < 1e-9, form_key + ": the worst ΔE is the largest of the parts");
            const bool matches = verdict == QLatin1String("CONSISTENT") ? worst <= consistent
                               : verdict == QLatin1String("NOTICEABLE") ? worst > consistent && worst <= noticeable : worst > noticeable;
            self->check(matches, QStringLiteral("%1: the verdict %2 agrees with its thresholds (worst ΔE %3)").arg(form_key, verdict).arg(worst, 0, 'f', 1));
          }
          // Which textures each skin shape was measured on (to explain a surprising number).
          for (const auto& part : self->scene()->parts()) {
            const auto row = part.toMap();
            if (row.value("group").toString() != QLatin1String("skin") && row.value("group").toString() != QLatin1String("head")) continue;
            const auto insp = row.value("inspector").toMap();
            QStringList textures;
            for (const auto& t : insp.value("textures").toList()) {
              const auto tm = t.toMap();
              if (tm.value("role").toString().startsWith(QLatin1String("Effective")) || tm.value("role").toString() == QLatin1String("Diffuse"))
                textures << tm.value("role").toString() + "=" + tm.value("path").toString().right(60) + " @ " + tm.value("location").toString().left(40);
            }
            self->log(QStringLiteral("  skin part %1 [%2] slots %3 shader %4 shown %5 :: %6")
                          .arg(row.value("label").toString().left(50), row.value("group").toString(), insp.value("slots").toString(), insp.value("shader").toString().right(30))
                          .arg(row.value("shownByDefault").toBool()).arg(textures.join(" | ")));
          }
          self->log(QStringLiteral("skin match %1 (%2): %3 [%4] head %5 :: %6")
                        .arg(form_key, self->appearance()->npc_label(), verdict, parts.join(", "), m.value("head").toMap().value("color").toString(), m.value("message").toString().left(110)));
          rows->append(QJsonObject{{"npc", form_key}, {"name", self->appearance()->npc_label()}, {"verdict", verdict}, {"worst", worst},
                                   {"codes", QJsonArray::fromStringList(m.value("codes").toStringList())}, {"parts", QJsonArray::fromStringList(parts)}});
          // Capture the card for the first NPC that could be measured (and one that could not, to show the honest message).
          if (!*captured && verdict != QLatin1String("UNKNOWN")) {
            *captured = true;
            if (auto* tabs = self->window->findChild<QObject*>(QStringLiteral("creatorRightTabs"))) tabs->setProperty("currentIndex", 5);
            self->after(600, [self, next] {
              QQuickItem* card = nullptr;
              std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
                for (auto* child : item->childItems()) {
                  if (child->objectName() == QLatin1String("skinMatchCard") && child->isVisible()) card = child;
                  walk(child);
                }
              };
              walk(self->window->contentItem());
              self->check(card != nullptr, "the skin match card is on screen in «Origen»");
              if (card) {
                int inspected = 0;
                const auto problems = layout_problems(card, false, &inspected);
                self->check(problems.isEmpty(), QStringLiteral("skin match card %1 items, layout %2").arg(inspected).arg(problems.isEmpty() ? "ok" : problems.join("; ")));
              }
              self->window->grabWindow().save(QDir(self->work_dir).filePath("skin-match-card.png"));
              self->after(0, *next);
            });
            return;
          }
          self->after(0, *next);
        });
      };
      steps.push_back([next] { (*next)(); });
      steps.push_back([self, rows] {
        QMap<QString, int> counts;
        for (const auto& v : *rows) ++counts[v.toObject().value("verdict").toString()];
        QStringList parts;
        for (auto it = counts.begin(); it != counts.end(); ++it) parts << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
        self->log(QStringLiteral("skin match: %1 NPCs, verdicts: %2").arg(rows->size()).arg(parts.join(", ")));
        QFile out(QDir(self->work_dir).filePath("skin-match-report.json"));
        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(*rows).toJson());
        self->check(rows->size() >= 6, "several NPCs were measured");
        self->step();
      });
    } else if (name == "cache-redo") {
      // Reference integration A2 (redo): "Rehacer los desactualizados" with the REAL profile. Seeds stale portraits of real NPCs (they must
      // really be drawn again) and of an NPC that does not exist (a render error must be counted, not hang), clicks the button in the
      // inspector, follows the progress, and then proves cancellation with more stale entries than the window. Isolated cache only. Not in "all".
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(50, *tick);
        };
        (*tick)();
      };
      auto find_item = std::make_shared<std::function<QQuickItem*(QQuickItem*, const QString&)>>();
      *find_item = [find_item](QQuickItem* root, const QString& name) -> QQuickItem* {
        if (!root) return nullptr;
        if (root->objectName() == name && root->isVisible()) return root;
        for (auto* child : root->childItems())
          if (auto* hit = (*find_item)(child, name)) return hit;
        return nullptr;
      };
      auto report_ready = [self] { return self->studio->portraits()->cache_report().value("state").toString() == QLatin1String("ready"); };
      auto seed = [self](const QStringList& form_keys) {
        auto* portraits = self->studio->portraits();
        PortraitCache cache(portraits->cache_directory());
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::darkGray);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        QString error;
        bool ok = true;
        for (const auto& form_key : form_keys) {
          PortraitEntry e;
          e.form_key = form_key;
          e.renderer_version = QStringLiteral("portrait-v0");  // an old renderer: stale
          e.snapshot_digest = portraits->snapshot_digest();
          e.width = 64;
          e.height = 64;
          e.created_at = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
          ok &= cache.store(e, png, &error);
        }
        return ok;
      };
      auto redo_state = [self] { return self->studio->portraits()->redo_progress().value("state").toString(); };
      steps.push_back([self, wait_for] {
        const auto isolated = qEnvironmentVariable("SNS_PORTRAIT_CACHE_DIR");
        auto* portraits = self->studio->portraits();
        const auto dir = portraits->cache_directory();
        if (isolated.isEmpty() || dir.isEmpty() || !QDir::fromNativeSeparators(dir).startsWith(QDir::fromNativeSeparators(isolated), Qt::CaseInsensitive)) {
          self->skip("cache-redo: SNS_PORTRAIT_CACHE_DIR is not set (the user's own portrait cache is never seeded or redone by a harness)");
          self->finish();
          return;
        }
        wait_for([portraits] { return !portraits->snapshot_digest().isEmpty(); }, 120000, [self](bool ok) {
          self->check(ok, "the profile's digest is ready");
          self->step();
        });
      });
      steps.push_back([self, seed, wait_for, report_ready] {
        self->check(seed({"0A2C8E:Skyrim.esm", "013364:Skyrim.esm", "01A690:Skyrim.esm", "000001:Skyrim.esm"}), "4 stale portraits were seeded (3 real NPCs, 1 that does not exist)");
        self->studio->portraits()->inspectCache();
        wait_for(report_ready, 60000, [self](bool ok) {
          const auto c = self->studio->portraits()->cache_report().value("counts").toMap();
          self->check(ok && c.value("stale").toInt() == 4, QStringLiteral("the inspection finds the 4 stale portraits (%1)").arg(c.value("stale").toInt()));
          self->step();
        });
      });
      // Open the inspector the way a person does.
      steps.push_back([self, wait_for, find_item] {
        auto* center = self->window->findChild<QObject*>(QStringLiteral("settingsCenter"));
        self->check(center != nullptr, "the Settings Center exists");
        if (!center) { self->finish(); return; }
        QMetaObject::invokeMethod(center, "openAt", Q_ARG(QVariant, QStringLiteral("npc")), Q_ARG(QVariant, QStringLiteral("npc.portraitCache")));
        auto last = std::make_shared<QPointF>(-1, -1);
        auto stable = std::make_shared<int>(0);
        wait_for([self, find_item, last, stable] {
          auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton"));
          if (!b) return false;
          const auto at = b->mapToScene(QPointF(b->width() / 2, b->height() / 2));
          const bool on_screen = at.y() > 140 && at.y() < self->window->height() - 110 && at.x() > 0 && at.x() < self->window->width();
          *stable = (on_screen && at == *last) ? *stable + 1 : 0;
          *last = at;
          return *stable >= 6;
        }, 20000, [self, find_item](bool ok) {
          self->check(ok, "the inspector button is on screen and still");
          auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectPortraitsButton"));
          if (ok && b) QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint());
          self->step();
        });
      });
      steps.push_back([self, wait_for, find_item, report_ready] {
        wait_for([self, find_item] { return (*find_item)(self->window->contentItem(), QStringLiteral("inspectorRedoStale")) != nullptr; }, 10000, [self, wait_for, find_item, report_ready](bool opened) {
          self->check(opened, "the inspector is open with its redo button");
          if (!opened) { self->finish(); return; }
          wait_for([self, find_item, report_ready] {
            auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectorRedoStale"));
            return report_ready() && b && b->isEnabled();
          }, 30000, [self, find_item, wait_for](bool enabled) {
            self->check(enabled, "the redo button is enabled when there is something stale");
            auto* b = (*find_item)(self->window->contentItem(), QStringLiteral("inspectorRedoStale"));
            if (!enabled || !b) { self->finish(); return; }
            self->log("redo button: " + b->property("text").toString());
            QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, b->mapToScene(QPointF(b->width() / 2, b->height() / 2)).toPoint());
            // Follow the progress: it never goes back, and it is shown on screen.
            auto last_done = std::make_shared<int>(0);
            auto monotonic = std::make_shared<bool>(true);
            auto captured = std::make_shared<bool>(false);
            wait_for([self, last_done, monotonic, captured] {
              const auto p = self->studio->portraits()->redo_progress();
              const int done = p.value("done").toInt();
              if (done < *last_done) *monotonic = false;
              *last_done = done;
              if (!*captured && p.value("state").toString() == QLatin1String("running")) {
                *captured = true;
                self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-redo-running.png"));
              }
              return p.value("state").toString() == QLatin1String("done");
            }, 300000, [self, monotonic, captured](bool finished) {
              const auto p = self->studio->portraits()->redo_progress();
              self->log(QStringLiteral("redo: state %1, %2 of %3 done, %4 errors, %5 ms, last error '%6'")
                            .arg(p.value("state").toString()).arg(p.value("done").toInt()).arg(p.value("total").toInt()).arg(p.value("errors").toInt())
                            .arg(p.value("elapsedMs").toLongLong()).arg(p.value("lastError").toString().left(120)));
              self->check(finished, "the redo finished");
              self->check(*monotonic, "the progress never went backwards");
              self->check(*captured, "the progress was visible while it ran");
              self->check(p.value("total").toInt() == 4 && p.value("done").toInt() == 4, "all 4 portraits were handled");
              self->check(p.value("errors").toInt() == 1, "the NPC that does not exist counted as 1 error instead of hanging");
              self->step();
            });
          });
        });
      });
      steps.push_back([self, wait_for, report_ready] {
        // The 3 real NPCs have a new, valid, 256x256 render made from the real profile; the old one is kept for comparison.
        auto* portraits = self->studio->portraits();
        wait_for([portraits] { return portraits->cache_report().value("state").toString() == QLatin1String("ready") && portraits->cache_report().value("counts").toMap().value("valid").toInt() >= 3; }, 60000,
                 [self, portraits](bool ok) {
          const auto c = portraits->cache_report().value("counts").toMap();
          self->log(QStringLiteral("after redo: valid=%1 stale=%2 damaged=%3 kept=%4").arg(c.value("valid").toInt()).arg(c.value("stale").toInt()).arg(c.value("damaged").toInt()).arg(c.value("kept").toInt()));
          self->check(ok && c.value("valid").toInt() == 3, "3 portraits are valid again");
          self->check(c.value("kept").toInt() == 3, "the 3 replaced renders are kept for comparison");
          self->check(c.value("stale").toInt() == 1, "only the NPC that does not exist is still stale");
          PortraitCache cache(portraits->cache_directory());
          const auto snapshot = portraits->snapshot();
          for (const auto& fk : {"0A2C8E:Skyrim.esm", "013364:Skyrim.esm", "01A690:Skyrim.esm"}) {
            const auto form_key = QString::fromLatin1(fk);
            const auto found = cache.lookup(form_key, PortraitRenderer::kRendererVersion, portraits->snapshot_digest(),
                                            [&snapshot](const QString& p) { return PortraitRenderer::fingerprint_of(snapshot, p); });
            self->check(found.state == PortraitCacheState::Valid, form_key + ": the cache lookup is valid with the real fingerprints");
            QImage image(found.image_path);
            self->check(!image.isNull() && image.width() == PortraitRenderer::kSize, form_key + QStringLiteral(": the new portrait is %1 px").arg(PortraitRenderer::kSize));
            const auto prev = cache.previous(form_key);
            self->check(prev && prev->entry.renderer_version == QLatin1String("portrait-v0"), form_key + ": the previous render (old renderer) is kept");
          }
          self->window->grabWindow().save(QDir(self->work_dir).filePath("cache-redo-done.png"));
          self->step();
        });
      });
      // Cancellation: more stale entries than the window, cancelled after the first one finishes.
      steps.push_back([self, seed, wait_for, report_ready, redo_state] {
        QStringList many;
        for (int i = 0; i < 9; ++i) many << QStringLiteral("%1:Skyrim.esm").arg(0x20 + i, 6, 16, QLatin1Char('0'));
        self->check(seed(many), "9 stale portraits of NPCs that do not exist were seeded");
        auto* portraits = self->studio->portraits();
        portraits->inspectCache();
        wait_for(report_ready, 60000, [self, portraits, wait_for, redo_state](bool) {
          portraits->redoStale();
          wait_for([portraits] { return portraits->redo_progress().value("done").toInt() >= 1; }, 120000, [self, portraits, wait_for, redo_state](bool started) {
            self->check(started, "the redo started and finished its first portrait");
            portraits->cancelRedo();
            wait_for([redo_state] { return redo_state() == QLatin1String("cancelled"); }, 120000, [self, portraits](bool cancelled) {
              const auto p = portraits->redo_progress();
              self->log(QStringLiteral("cancelled: %1 of %2 handled, active %3, pending %4").arg(p.value("done").toInt()).arg(p.value("total").toInt()).arg(p.value("active").toInt()).arg(p.value("pending").toInt()));
              self->check(cancelled, "the redo reports cancelled once what was in flight finished");
              self->check(p.value("total").toInt() >= 9 && p.value("done").toInt() < p.value("total").toInt(), "cancelling stopped it before every portrait was asked for");
              self->check(p.value("active").toInt() == 0 && p.value("pending").toInt() == 0, "nothing is left in flight or waiting");
              self->step();
            });
          });
        });
      });
    } else if (name == "forge-behavior") {
      // AG04: the Character Forge behavior controls on the REAL window against the REAL indexed MO2 profile (no fixture anywhere on this
      // path): archetype catalog, Vista previa (touches nothing), Aplicar (one undoable step), Deshacer / Rehacer, the spell and perk
      // pickers through their dialogs, the voice evidence following the voice, and a validation that keeps the playable export blocked.
      // Every click is a platform mouse click on the control found by objectName. Not in "all": it needs an indexed profile.
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(50, *tick);
        };
        (*tick)();
      };
      auto named = std::make_shared<std::function<QQuickItem*(QQuickItem*, const QString&)>>();
      *named = [named](QQuickItem* root, const QString& object_name) -> QQuickItem* {
        if (!root || !root->isVisible()) return nullptr;
        if (root->objectName() == object_name) return root;
        for (auto* child : root->childItems())
          if (auto* hit = (*named)(child, object_name)) return hit;
        return nullptr;
      };
      auto item_named = [self, named](const char* object_name) { return (*named)(self->window->contentItem(), QString::fromLatin1(object_name)); };
      auto click_item = [self](QQuickItem* item) {
        QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
      };
      auto shot = [self](const char* file) { self->window->grabWindow().save(QDir(self->work_dir).filePath(QString::fromLatin1(file))); };
      auto forge = [self] { return self->studio->forge(); };
      auto search = [self, wait_for](const QString& kind, const QString& text, std::function<void(bool, QVariantList)> next) {
        self->studio->forge()->searchRecords(kind, text);
        wait_for([self, kind] {
          const auto s = self->studio->forge()->record_search();
          const auto st = s.value("state").toString();
          return s.value("kind").toString() == kind && (st == QLatin1String("ready") || st == QLatin1String("error"));
        }, 60000, [self, next](bool ok) { next(ok, self->studio->forge()->record_choices()); });
      };
      auto preview_settled = [forge] {
        const auto st = forge()->archetype_preview().value("state").toString();
        return st != QLatin1String("loading") && st != QLatin1String("idle");
      };
      static const QStringList protected_keys = {"name", "editorId", "description", "raceFormKey", "female", "voiceFormKey", "height", "weight", "face",
                                                 "headPartFormKeys", "hairColorFormKey", "headTextureFormKey", "level", "levelMultiplier", "minLevel", "maxLevel", "placement"};
      auto state = std::make_shared<QVariantMap>();  // base draft, applied draft and the chosen archetype, shared between the steps
      steps.push_back([self, wait_for, forge] {
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        self->check(tabs != nullptr, "the workspace tabs exist");
        if (tabs) tabs->setProperty("currentIndex", 2);
        wait_for([forge] { return forge()->catalog_ready(); }, 300000, [self](bool ok) {
          if (!ok) {
            self->skip("forge-behavior: the studio did not index a profile (open it with an MO2 instance and profile)");
            self->next_scenario();
            return;
          }
          self->step();
        });
      });
      steps.push_back([self, forge, search] {
        self->check(forge()->newProject(QStringLiteral("AG04Behavior")), "a Character Forge project was created");
        forge()->setCharacterName(QStringLiteral("AG04 Prueba"));
        forge()->setEditorId(QStringLiteral("AG04_Prueba"));
        search(QStringLiteral("race"), QStringLiteral("NordRace"), [self, forge, search](bool ok, QVariantList races) {
          self->check(ok && !races.isEmpty(), "the race picker finds NordRace in the real profile");
          if (!races.isEmpty()) forge()->setNpcValue(QStringLiteral("raceFormKey"), races.first().toMap().value("formKey"));
          search(QStringLiteral("voice"), QStringLiteral("FemaleEvenToned"), [self, forge](bool ok2, QVariantList voices) {
            self->check(ok2 && !voices.isEmpty(), "the voice picker finds FemaleEvenToned");
            if (!voices.isEmpty()) forge()->setNpcValue(QStringLiteral("voiceFormKey"), voices.first().toMap().value("formKey"));
            forge()->setNpcValue(QStringLiteral("female"), true);
            self->check(!forge()->npc().value("voiceFormKey").toString().isEmpty(), "the draft has a voice");
            self->step();
          });
        });
      });
      // The Datos tab, reached with a click; the catalog is requested when the controls appear.
      steps.push_back([self, wait_for, forge, item_named, click_item, shot] {
        // The left navigation of the Character Forge ("Crear NPC", not the button of the project page on the right), then the 'Datos' tab.
        QQuickItem* nav = nullptr;
        std::function<void(QQuickItem*)> scan = [&](QQuickItem* root) {
          for (auto* candidate : root->childItems()) {
            const auto text = candidate->property("text").toString();
            if (text.contains(QLatin1String("Crear NPC"))) self->log(QStringLiteral("  candidate '%1' %2 visible=%3 x=%4").arg(text, QString::fromLatin1(candidate->metaObject()->className())).arg(candidate->isVisible()).arg(candidate->mapToScene(QPointF(0, 0)).x()));
            if (candidate->isVisible() && text == QLatin1String("Crear NPC") && candidate->mapToScene(QPointF(0, 0)).x() < 300) nav = candidate;
            scan(candidate);
          }
        };
        scan(self->window->contentItem());
        self->check(nav != nullptr, "the Character Forge navigation offers 'Crear NPC'");
        if (nav) click_item(nav);
        self->after(500, [self, wait_for, forge, item_named, click_item, shot] {
        auto* tab = self->find_item(self->window->contentItem(), QStringLiteral("Datos"));
        self->check(tab != nullptr, "the 'Datos' tab is visible in the Character Forge");
        if (tab) click_item(tab);
        wait_for([forge, item_named] { return item_named("archetypeCombo") != nullptr && forge()->behavior_catalog().value("state").toString() == QLatin1String("ready"); }, 60000,
                 [self, forge, item_named, shot](bool ok) {
          self->check(ok, "the archetype catalog of the indexed profile loaded: " + forge()->behavior_catalog().value("reason").toString());
          if (!ok) { self->next_scenario(); return; }
          const auto catalog = forge()->behavior_catalog();
          const auto list = catalog.value("archetypes").toList();
          self->check(!list.isEmpty(), QStringLiteral("the profile offers %1 archetype(s)").arg(list.size()));
          for (const auto& a : list) {
            const auto m = a.toMap();
            self->log(QStringLiteral("archetype %1 allLinksResolved=%2 masters=%3 issues=%4").arg(m.value("id").toString(), m.value("allLinksResolved").toString(),
                                                                                                     m.value("requiredMasters").toStringList().join(","), QString::number(m.value("issues").toList().size())));
          }
          self->check(!catalog.value("loadOrderFingerprint").toString().isEmpty(), "the catalog carries the profile fingerprint " + catalog.value("loadOrderFingerprint").toString().left(16));
          auto* combo = item_named("archetypeCombo");
          self->check(combo && combo->property("count").toInt() == list.size(), "the archetype combo lists exactly the profile's archetypes");
          shot("forge-behavior-catalog.png");
          self->step();
        });
        });
      });
      steps.push_back([self, forge, item_named, click_item, state, shot, preview_settled, wait_for] {
        const auto list = forge()->behavior_catalog().value("archetypes").toList();
        int target = -1;
        for (int i = 0; i < list.size(); ++i)
          if (list[i].toMap().value("allLinksResolved").toBool()) { target = i; break; }
        auto* combo = item_named("archetypeCombo");
        auto* apply = item_named("archetypeApplyButton");
        auto* preview_button = item_named("archetypePreviewButton");
        self->check(combo && apply && preview_button, "the combo, Vista previa and Aplicar are on screen");
        if (!combo || !apply || !preview_button) { self->next_scenario(); return; }
        if (target < 0) {
          // No archetype resolves in this profile: the honest result is a disabled Aplicar that says why. Nothing is faked.
          self->check(!apply->isEnabled(), "no archetype resolves in this profile: Aplicar is disabled");
          auto* reason = item_named("archetypeBlockedReason");
          self->check(reason && !reason->property("text").toString().isEmpty(), "and the screen says why: " + (reason ? reason->property("text").toString() : QString()));
          self->skip("forge-behavior: apply / undo / redo not measured, no archetype of this profile has all its records");
          shot("forge-behavior-no-resolved-archetype.png");
          self->next_scenario();
          return;
        }
        combo->setProperty("currentIndex", target);
        (*state)["archetype"] = list[target].toMap().value("id").toString();
        (*state)["base"] = forge()->npc();
        (*state)["canUndo"] = forge()->can_undo();
        self->log("target archetype: " + state->value("archetype").toString());
        self->check(preview_button->isEnabled(), "Vista previa is enabled with a project, a ready profile and a chosen archetype");
        click_item(preview_button);
        wait_for(preview_settled, 60000, [self, forge, state, shot, item_named](bool ok) {
          const auto p = forge()->archetype_preview();
          self->log("preview: state=" + p.value("state").toString() + " " + p.value("message").toString());
          // The panel as drawn at this window size: nothing spills out sideways and no button is narrower than its label.
          if (auto* combo = item_named("archetypeCombo"); combo && combo->parentItem() && combo->parentItem()->parentItem()) {
            int inspected = 0;
            self->window->grabWindow();  // a window lays out (polishes) when it renders: measure after a frame, not before
            self->window->grabWindow();
            const auto problems = Stress::layout_problems(combo->parentItem()->parentItem(), false, &inspected);
            if (!problems.isEmpty()) {
              QString chain;
              int level = 0;
              for (QQuickItem* up = combo->parentItem(); up && level < 9; up = up->parentItem(), ++level)
                chain += QStringLiteral(" [%1 w=%2 implicitW=%3]").arg(QString::fromLatin1(up->metaObject()->className()).left(28)).arg(up->width()).arg(up->implicitWidth());
              self->log("ancestors of the combo:" + chain);
            }
            self->check(problems.isEmpty(), QStringLiteral("the behavior panel fits %1x%2 (%3 items): %4").arg(self->window->width()).arg(self->window->height()).arg(inspected).arg(problems.isEmpty() ? QStringLiteral("ok") : problems.join("; ")));
          }
          self->check(ok && p.value("state").toString() == QLatin1String("ready"), "Vista previa produced a ready preview");
          self->check(!p.value("changes").toList().isEmpty(), QStringLiteral("it lists %1 concrete change(s)").arg(p.value("changes").toList().size()));
          self->check(forge()->npc() == state->value("base").toMap(), "Vista previa did not touch the draft");
          self->check(forge()->can_undo() == state->value("canUndo").toBool(), "Vista previa added no undo step");
          shot("forge-behavior-preview.png");
          self->step();
        });
      });
      steps.push_back([self, forge, item_named, click_item, state, shot, preview_settled, wait_for] {
        auto* apply = item_named("archetypeApplyButton");
        self->check(apply && apply->isEnabled(), "Aplicar is enabled for a resolved archetype");
        if (!apply) { self->next_scenario(); return; }
        click_item(apply);
        wait_for([forge] { return forge()->archetype_preview().value("state").toString() == QLatin1String("applied") || forge()->archetype_preview().value("state").toString() == QLatin1String("error") ||
                                  forge()->archetype_preview().value("state").toString() == QLatin1String("stale") || forge()->archetype_preview().value("state").toString() == QLatin1String("conflict"); }, 60000,
                 [self, forge, state, shot](bool ok) {
          const auto p = forge()->archetype_preview();
          self->log("apply: state=" + p.value("state").toString() + " " + p.value("message").toString());
          self->check(ok && p.value("state").toString() == QLatin1String("applied"), "Aplicar applied the archetype");
          const auto base = state->value("base").toMap();
          const auto now = forge()->npc();
          self->check(now != base, "the draft changed");
          int differing = 0;
          bool protected_ok = true;
          for (auto it = now.begin(); it != now.end(); ++it) {
            if (it.value() != base.value(it.key())) {
              ++differing;
              self->log("  changed: " + it.key());
              if (protected_keys.contains(it.key())) protected_ok = false;
            }
          }
          self->check(protected_ok, QStringLiteral("only authorized fields changed (%1), identity, face, voice, sex and placement are untouched").arg(differing));
          self->check(forge()->can_undo(), "the application is one undoable step");
          self->check(forge()->dirty(), "the project is marked as modified");
          (*state)["applied"] = now;
          shot("forge-behavior-applied.png");
          self->step();
        });
      });
      steps.push_back([self, forge, item_named, click_item, state, wait_for, shot] {
        auto* undo = item_named("archetypeUndoButton");
        auto* redo = item_named("archetypeRedoButton");
        self->check(undo && undo->isEnabled(), "Deshacer is enabled after applying");
        self->check(redo && !redo->isEnabled(), "Rehacer is disabled until something was undone");
        if (!undo || !redo) { self->next_scenario(); return; }
        click_item(undo);
        wait_for([forge, state] { return forge()->npc() == state->value("base").toMap(); }, 5000, [self, forge, state, redo, click_item, wait_for, shot](bool ok) {
          self->check(ok, "Deshacer restored the exact previous draft in one click");
          self->check(redo->isEnabled(), "Rehacer is enabled after Deshacer");
          click_item(redo);
          wait_for([forge, state] { return forge()->npc() == state->value("applied").toMap(); }, 5000, [self, shot](bool ok2) {
            self->check(ok2, "Rehacer restored the exact applied draft");
            shot("forge-behavior-redo.png");
            self->step();
          });
        });
      });
      // A double click on Aplicar is one application: after it, a single Deshacer returns to the original draft.
      steps.push_back([self, forge, item_named, click_item, state, wait_for] {
        forge()->undo();
        self->check(forge()->npc() == state->value("base").toMap(), "back on the original draft before the double click");
        auto* apply = item_named("archetypeApplyButton");
        if (!apply) { self->next_scenario(); return; }
        click_item(apply);
        click_item(apply);
        wait_for([forge] { return forge()->archetype_preview().value("state").toString() == QLatin1String("applied"); }, 60000, [self, forge, state](bool ok) {
          self->check(ok, "the double click ended applied");
          forge()->undo();
          self->check(forge()->npc() == state->value("base").toMap(), "one Deshacer undoes a double-clicked Aplicar completely (no second application happened)");
          self->step();
        });
      });
      // Spells and perks through the real pickers: button -> dialog -> list item -> draft.
      auto pick = std::make_shared<std::function<void(const QString&, const QString&, std::function<void()>)>>();
      *pick = [self, forge, wait_for, item_named, click_item, shot](const QString& kind, const QString& field, std::function<void()> done) {
        QQuickItem* selector = nullptr;
        for (auto* item : self->window->findChildren<QQuickItem*>())
          if (item->isVisible() && item->property("kind").toString() == kind && item->property("title").isValid()) selector = item;
        self->check(selector != nullptr, "the " + kind + " selector is on screen");
        QQuickItem* button = nullptr;
        if (selector) {
          std::function<void(QQuickItem*)> visit = [&](QQuickItem* i) {
            for (auto* c : i->childItems()) {
              if (c->isVisible() && c->property("text").toString() == QString::fromUtf8("Seleccionar…") && c->metaObject()->indexOfProperty("checkable") >= 0) button = c;
              visit(c);
            }
          };
          visit(selector);
        }
        self->check(button && button->isEnabled(), "the " + kind + " selector button is enabled");
        if (!button) { self->next_scenario(); return; }
        const auto before = forge()->npc().value(field).toList();
        // The selector may be below the fold of the editor's scroll area: scroll it into view like a person would, then click it.
        for (QQuickItem* p = button->parentItem(); p; p = p->parentItem()) {
          if (!p->property("contentY").isValid() || !p->property("contentHeight").isValid()) continue;
          if (p->property("contentHeight").toReal() <= p->height() + 1) continue;  // a nested view that does not scroll
          auto* content = qvariant_cast<QQuickItem*>(p->property("contentItem"));
          if (!content) continue;
          const qreal y = button->mapToItem(content, QPointF(0, 0)).y();
          p->setProperty("contentY", std::max<qreal>(0, y - 200));
          break;
        }
        self->after(400, [self, forge, kind, field, before, item_named, shot, done, button, wait_for] {
        if (!button->isVisible() || button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).y() > self->window->height()) {
          QString chain;
          for (QQuickItem* p = button->parentItem(); p; p = p->parentItem())
            if (p->property("contentY").isValid()) chain += QStringLiteral(" %1(contentY=%2 contentHeight=%3 height=%4)").arg(QString::fromLatin1(p->metaObject()->className())).arg(p->property("contentY").toReal()).arg(p->property("contentHeight").toReal()).arg(p->height());
          self->log(QStringLiteral("selector at scene y=%1, window height %2, flickables:%3").arg(button->mapToScene(QPointF(0, 0)).y()).arg(self->window->height()).arg(chain));
          self->check(false, "the " + kind + " selector button is on screen after scrolling");
          self->next_scenario();
          return;
        }
        QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        wait_for([forge, kind, item_named] {
          const auto s = forge()->record_search();
          return s.value("kind").toString() == kind && s.value("state").toString() == QLatin1String("ready") && item_named("recordChoicesList") != nullptr;
        }, 60000, [self, forge, kind, field, before, item_named, shot, done](bool ok) {
          const auto count = forge()->record_search().value("count").toInt();
          self->check(ok && count > 0, QStringLiteral("the %1 picker lists %2 real record(s) of the profile").arg(kind).arg(count));
          if (!ok || count == 0) { self->next_scenario(); return; }
          const auto first = forge()->record_choices().first().toMap();
          // Let the dialog finish its opening transition before looking at it and clicking a row.
          self->after(800, [self, forge, kind, field, before, item_named, shot, done, first] {
          auto* list = item_named("recordChoicesList");
          QQuickItem* delegate = nullptr;
          if (list) QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(int, 0));
          self->check(delegate != nullptr, "the first result is drawn as a clickable row: " + first.value("editorId").toString());
          shot(kind == QLatin1String("spell") ? "forge-behavior-spell-picker.png" : "forge-behavior-perk-picker.png");
          if (!delegate) { self->next_scenario(); return; }
          const auto at = delegate->mapToScene(QPointF(delegate->width() / 2, delegate->height() / 2)).toPoint();
          self->log(QStringLiteral("clicking the row '%1' at %2,%3 (row %4x%5, enabled=%6)").arg(first.value("editorId").toString()).arg(at.x()).arg(at.y()).arg(delegate->width()).arg(delegate->height()).arg(delegate->isEnabled()));
          QTest::mouseClick(self->window, Qt::LeftButton, Qt::NoModifier, at);
          self->after(600, [self, forge, field, before, first, kind, done] {
            const auto after_list = forge()->npc().value(field).toList();
            const bool added = after_list.size() == before.size() + 1 && after_list.contains(first.value("formKey"));
            self->check(added, QStringLiteral("choosing %1 added it to the draft's %2 (error: '%3')").arg(first.value("editorId").toString(), field, forge()->last_error()));
            if (!added) {
              // Is the defect in the QML path or in the draft? The same edit straight from C++:
              forge()->setNpcValue(field, QVariantList{first.value("formKey")});
              self->log(QStringLiteral("  same edit from C++: size=%1 error='%2'").arg(forge()->npc().value(field).toList().size()).arg(forge()->last_error()));
              self->log(QStringLiteral("  workspace before: %1").arg(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(forge()->workspace())).toJson(QJsonDocument::Compact)).left(600)));
              forge()->setWorkspaceValue(QStringLiteral("recordLabels"), QVariantMap{{first.value("formKey").toString(), first.value("editorId")}});
              self->log(QStringLiteral("  setWorkspaceValue(recordLabels) from C++: error='%1'").arg(forge()->last_error()));
              self->log(QStringLiteral("  npc keys: %1").arg(QStringList(forge()->npc().keys()).join(",")));
              self->log(QStringLiteral("  %1 as stored: %2").arg(field, QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(forge()->npc().value(field).toList())).toJson(QJsonDocument::Compact))));
            }
            done();
          });
          });
        });
        });
      };
      steps.push_back([self, pick] { (*pick)(QStringLiteral("spell"), QStringLiteral("spellFormKeys"), [self] { self->step(); }); });
      steps.push_back([self, pick] { (*pick)(QStringLiteral("perk"), QStringLiteral("perkFormKeys"), [self] { self->step(); }); });
      steps.push_back([self, forge, search] {
        search(QStringLiteral("spell"), QStringLiteral("zzz-no-such-spell-ag04"), [self, forge](bool ok, QVariantList found) {
          self->check(ok && found.isEmpty() && forge()->record_search().value("count").toInt() == 0, "a search without matches is an empty 'ready' result, not an error");
          self->step();
        });
      });
      // The voice evidence follows the voice: a different voice never shows the previous voice's analysis.
      steps.push_back([self, forge, search, wait_for, shot] {
        wait_for([forge] { return forge()->voice_analysis().value("state").toString() == QLatin1String("ready"); }, 60000, [self, forge, search, wait_for, shot](bool ok) {
          const auto v = forge()->voice_analysis();
          const auto a = v.value("analysis").toMap();
          self->check(ok, "the voice analysis of the current voice is ready: " + v.value("reason").toString());
          self->log(QStringLiteral("voice %1 follower=%2 marriage=%3 listMarriage=%4 runtime=%5").arg(a.value("voiceEditorId").toString(), a.value("followerDialogueClassification").toString(),
                                                                                                           a.value("marriageDialogueClassification").toString(), a.value("hasStaticMarriageListEvidence").toString(), a.value("runtimeVerificationState").toString()));
          self->check(a.value("voiceFormKey").toString().compare(forge()->npc().value("voiceFormKey").toString(), Qt::CaseInsensitive) == 0, "the analysis is about the voice of the draft");
          self->check(a.value("runtimeVerificationState").toString().contains(QLatin1String("REQUIRES_IN_GAME_TEST")), "the evidence says it still needs an in-game test");
          self->check(!v.value("loadOrderFingerprint").toString().isEmpty(), "the evidence carries the profile fingerprint");
          search(QStringLiteral("voice"), QStringLiteral("Male"), [self, forge, wait_for, shot](bool ok2, QVariantList voices) {
            self->check(ok2 && !voices.isEmpty(), "another voice can be searched");
            const auto old_voice = forge()->npc().value("voiceFormKey").toString();
            QString other;
            for (const auto& r : voices) if (r.toMap().value("formKey").toString() != old_voice) { other = r.toMap().value("formKey").toString(); break; }
            if (other.isEmpty()) { self->skip("forge-behavior: no second voice found"); self->step(); return; }
            forge()->setNpcValue(QStringLiteral("voiceFormKey"), other);
            const auto during = forge()->voice_analysis();
            self->check(during.value("state").toString() != QLatin1String("ready") || during.value("analysis").toMap().value("voiceFormKey").toString().compare(other, Qt::CaseInsensitive) == 0,
                        "right after changing the voice, the panel does not show the old voice's analysis");
            wait_for([forge] { return forge()->voice_analysis().value("state").toString() == QLatin1String("ready"); }, 60000, [self, forge, other, shot](bool ok3) {
              const auto a2 = forge()->voice_analysis().value("analysis").toMap();
              self->check(ok3 && a2.value("voiceFormKey").toString().compare(other, Qt::CaseInsensitive) == 0, "the analysis followed the new voice (" + a2.value("voiceEditorId").toString() + ")");
              shot("forge-behavior-voice.png");
              self->step();
            });
          });
        });
      });
      // Validation keeps the playable export blocked and carries the behavior half.
      steps.push_back([self, forge, wait_for, item_named, shot] {
        forge()->validateNpc();
        wait_for([forge] { return !forge()->busy() && !forge()->validation().isEmpty(); }, 120000, [self, forge, item_named, shot](bool ok) {
          const auto v = forge()->validation();
          self->check(ok, "the draft was validated: " + forge()->last_error());
          self->check(!v.value("voiceAnalysis").toMap().isEmpty(), "the validation carries the voice analysis (voiceAnalysis propagated)");
          self->check(!v.value("exportable").toBool(), "the playable export stays blocked (exportable=false)");
          bool faceGenLock = false;
          int errors = 0;
          for (const auto& i : v.value("issues").toList()) {
            const auto m = i.toMap();
            faceGenLock |= m.value("code").toString() == QLatin1String("NativeFaceGenUnavailable");
            if (m.value("severity").toString() == QLatin1String("error")) {
              ++errors;
              self->log(QStringLiteral("  error issue %1: %2 [%3]").arg(m.value("code").toString(), m.value("message").toString().left(140), m.value("subject").toString()));
            }
            if (m.value("code").toString() == QLatin1String("SpellDuplicate") || m.value("code").toString() == QLatin1String("PerkDuplicate")) self->check(false, "unexpected duplicate: " + m.value("subject").toString());
          }
          self->check(faceGenLock, "NativeFaceGenUnavailable is still reported");
          self->log(QStringLiteral("validation: technicallyExportable=%1, %2 error issue(s), behaviorError='%3'").arg(v.value("technicallyExportable").toString()).arg(errors).arg(v.value("behaviorError").toString()));
          self->check(v.value("behaviorError").toString().isEmpty(), "the behavior half of the validation ran");
          shot("forge-behavior-validation.png");
          (void)item_named;
          self->step();
        });
      });
    } else if (name == "profile-switch") {
      // AG05: profile A -> B -> A inside one session, with a project open. Everything that belongs to the profile (catalog, voice analysis,
      // preview, record search, fingerprint) is dropped when the switch starts and rebuilt for the new one; a request in flight across the
      // switch never reaches the draft; going back to A gives A's fingerprint again. Needs an instance with two profiles (SNS_AG05_PROFILE_B).
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(25, *tick);
        };
        (*tick)();
      };
      auto forge = [self] { return self->studio->forge(); };
      const QString profile_b = qEnvironmentVariable("SNS_AG05_PROFILE_B");
      struct Mem {
        QString key_a, fingerprint_a, fingerprint_b, masters_a, masters_b, voice_fp_a;
        QVariantMap base;
        bool base_can_undo = false;
        qint64 a_to_b_ms = 0, b_to_a_ms = 0;
      };
      auto m = std::make_shared<Mem>();
      auto ready = [self] { return self->studio->phase() == StudioController::Phase::Ready; };
      auto masters_of = [](const QVariantMap& catalog) {
        for (const auto& a : catalog.value("archetypes").toList())
          if (a.toMap().value("id").toString() == QLatin1String("defensive-warrior")) return a.toMap().value("requiredMasters").toStringList().join(",");
        return QString();
      };
      auto search = [self, wait_for](const QString& kind, const QString& text, std::function<void(QVariantList)> next) {
        self->studio->forge()->searchRecords(kind, text);
        wait_for([self, kind] {
          const auto s = self->studio->forge()->record_search();
          return s.value("kind").toString() == kind && s.value("state").toString() != QLatin1String("loading");
        }, 60000, [self, next](bool) { next(self->studio->forge()->record_choices()); });
      };
      steps.push_back([self, wait_for, forge, search, m, ready, masters_of, profile_b] {
        if (profile_b.isEmpty() || self->studio->profiles().size() < 2) {
          self->skip("profile-switch: the instance has no second profile (set SNS_AG05_PROFILE_B)");
          self->next_scenario();
          return;
        }
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        if (tabs) tabs->setProperty("currentIndex", 2);
        wait_for([forge] { return forge()->catalog_ready(); }, 300000, [self, forge, search, m, masters_of](bool ok) {
          self->check(ok, "profile A is indexed and the Character Forge is ready");
          if (!ok) { self->next_scenario(); return; }
          self->check(forge()->newProject(QStringLiteral("AG05Switch")), "a project is open");
          m->key_a = forge()->profile_key();
          search(QStringLiteral("voice"), QStringLiteral("FemaleEvenToned"), [self, forge, m, masters_of](QVariantList voices) {
            if (!voices.isEmpty()) forge()->setNpcValue(QStringLiteral("voiceFormKey"), voices.first().toMap().value("formKey"));
            forge()->setNpcValue(QStringLiteral("female"), true);
            forge()->loadBehaviorCatalog();
            auto* wf = new std::function<void()>;  // leaked on purpose: lives as long as the scenario
            *wf = [self, forge, m, masters_of, wf] {
              if (forge()->behavior_catalog().value("state").toString() != QLatin1String("ready") || forge()->voice_analysis().value("state").toString() != QLatin1String("ready")) { self->after(50, *wf); return; }
              m->fingerprint_a = forge()->behavior_catalog().value("loadOrderFingerprint").toString();
              m->masters_a = masters_of(forge()->behavior_catalog());
              m->voice_fp_a = forge()->voice_analysis().value("loadOrderFingerprint").toString();
              m->base = forge()->npc();
              m->base_can_undo = forge()->can_undo();
              self->log(QStringLiteral("A: key=%1 fingerprint=%2 voiceFingerprint=%3 masters(defensive-warrior)=%4").arg(m->key_a, m->fingerprint_a.left(16), m->voice_fp_a.left(16), m->masters_a));
              self->check(!m->fingerprint_a.isEmpty() && m->fingerprint_a == m->voice_fp_a, "profile A: catalog and voice evidence carry the same fingerprint");
              forge()->previewArchetype(QStringLiteral("defensive-warrior"));
              self->step();
            };
            (*wf)();
          });
        });
      });
      // A -> B: what belonged to A is gone the moment the switch starts.
      steps.push_back([self, wait_for, forge, m, ready, profile_b] {
        wait_for([forge] { const auto s = forge()->archetype_preview().value("state").toString(); return s == QLatin1String("ready") || s == QLatin1String("error"); }, 60000, [self, forge, m, ready, profile_b, wait_for](bool) {
          self->check(forge()->archetype_preview().value("state").toString() == QLatin1String("ready"), "a preview for profile A is on screen before the switch");
          QElapsedTimer clock;
          clock.start();
          self->studio->loadProfile(profile_b);
          self->check(!forge()->catalog_ready(), "switching: the forge is not 'ready' while the new profile is scanned");
          self->check(forge()->behavior_catalog().value("state").toString() == QLatin1String("none"), "switching: the behavior catalog of A is dropped");
          self->check(forge()->archetype_preview().value("state").toString() == QLatin1String("idle"), "switching: A's preview is dropped");
          self->check(forge()->voice_analysis().value("state").toString() == QLatin1String("none"), "switching: A's voice analysis is dropped");
          self->check(forge()->record_search().value("state").toString() == QLatin1String("idle"), "switching: A's record search is dropped");
          self->check(forge()->npc() == m->base, "switching: the draft is untouched");
          wait_for(ready, 600000, [self, forge, m, clock, wait_for](bool ok) {
            m->a_to_b_ms = clock.elapsed();
            self->check(ok, QStringLiteral("profile B is indexed (%1 ms)").arg(m->a_to_b_ms));
            if (!ok) { self->next_scenario(); return; }
            self->check(forge()->catalog_ready() && forge()->profile_key() != m->key_a, "B has its own profile key: " + forge()->profile_key());
            forge()->loadBehaviorCatalog();
            wait_for([forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready") && forge()->voice_analysis().value("state").toString() == QLatin1String("ready"); }, 120000, [self, forge, m](bool ok2) {
              self->check(ok2, "B: catalog and voice evidence loaded");
              m->fingerprint_b = forge()->behavior_catalog().value("loadOrderFingerprint").toString();
              self->log(QStringLiteral("B: fingerprint=%1 voiceFingerprint=%2").arg(m->fingerprint_b.left(16), forge()->voice_analysis().value("loadOrderFingerprint").toString().left(16)));
              self->check(!m->fingerprint_b.isEmpty() && m->fingerprint_b != m->fingerprint_a, "B's fingerprint differs from A's");
              self->check(forge()->voice_analysis().value("loadOrderFingerprint").toString() == m->fingerprint_b, "B's voice evidence is B's, not A's");
              self->check(forge()->npc() == m->base && forge()->can_undo() == m->base_can_undo, "the draft survived the switch to B");
              self->step();
            });
          });
        });
      });
      // A request in flight across a switch: dropped, nothing reaches the draft.
      steps.push_back([self, wait_for, forge, m, ready] {
        forge()->applyArchetype(QStringLiteral("defensive-warrior"));
        self->studio->loadProfile(qEnvironmentVariable("SNS_AG05_PROFILE_A"));
        self->check(forge()->archetype_preview().value("state").toString() == QLatin1String("idle"), "a request in flight is dropped by the switch back to A");
        QElapsedTimer clock;
        clock.start();
        wait_for(ready, 600000, [self, forge, m, clock, wait_for](bool ok) {
          m->b_to_a_ms = clock.elapsed();
          self->check(ok, QStringLiteral("profile A is indexed again (%1 ms)").arg(m->b_to_a_ms));
          self->check(forge()->npc() == m->base && forge()->can_undo() == m->base_can_undo, "nothing of the request in flight reached the draft");
          self->check(forge()->profile_key() == m->key_a, "back on A's profile key");
          forge()->loadBehaviorCatalog();
          wait_for([forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready") && forge()->voice_analysis().value("state").toString() == QLatin1String("ready"); }, 120000, [self, forge, m](bool ok2) {
            self->check(ok2, "A again: catalog and voice evidence loaded");
            self->check(forge()->behavior_catalog().value("loadOrderFingerprint").toString() == m->fingerprint_a, "A's fingerprint is the same as the first time (deterministic)");
            self->check(forge()->voice_analysis().value("loadOrderFingerprint").toString() == m->fingerprint_a, "A's voice evidence is A's again");
            self->log(QStringLiteral("switch times: A->B %1 ms, B->A %2 ms").arg(m->a_to_b_ms).arg(m->b_to_a_ms));
            QJsonObject out{{"aToBMs", static_cast<double>(m->a_to_b_ms)}, {"bToAMs", static_cast<double>(m->b_to_a_ms)}, {"fingerprintA", m->fingerprint_a.left(16)}, {"fingerprintB", m->fingerprint_b.left(16)}};
            QFile f(QDir(self->work_dir).filePath("profile-switch.json"));
            if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(out).toJson());
            self->step();
          });
        });
      });
    } else if (name == "worker-recovery") {
      // AG05: the worker dies in the middle of a request (killed from outside, like a crash) and is started again: the request ends as a
      // visible error, the draft does not change, the studio comes back by itself after a reindex and the controls work again.
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(25, *tick);
        };
        (*tick)();
      };
      auto forge = [self] { return self->studio->forge(); };
      auto base = std::make_shared<QVariantMap>();
      auto base_can_undo = std::make_shared<bool>(false);
      auto search = [self, wait_for](const QString& kind, const QString& text, std::function<void(QVariantList)> next) {
        self->studio->forge()->searchRecords(kind, text);
        wait_for([self, kind] {
          const auto s = self->studio->forge()->record_search();
          return s.value("kind").toString() == kind && s.value("state").toString() != QLatin1String("loading");
        }, 60000, [self, next](bool) { next(self->studio->forge()->record_choices()); });
      };
      steps.push_back([self, wait_for, forge, search, base, base_can_undo] {
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        if (tabs) tabs->setProperty("currentIndex", 2);
        wait_for([forge] { return forge()->catalog_ready(); }, 300000, [self, forge, search, base, base_can_undo](bool ok) {
          if (!ok) { self->skip("worker-recovery: no indexed profile"); self->next_scenario(); return; }
          self->check(forge()->newProject(QStringLiteral("AG05Recovery")), "a project is open");
          search(QStringLiteral("voice"), QStringLiteral("FemaleEvenToned"), [self, forge, base, base_can_undo](QVariantList voices) {
            if (!voices.isEmpty()) forge()->setNpcValue(QStringLiteral("voiceFormKey"), voices.first().toMap().value("formKey"));
            forge()->setNpcValue(QStringLiteral("female"), true);
            *base = forge()->npc();
            *base_can_undo = forge()->can_undo();
            self->step();
          });
        });
      });
      // The kill, in the middle of a request.
      steps.push_back([self, wait_for, forge, base, base_can_undo] {
        forge()->loadBehaviorCatalog();  // the slow request: it is in flight when the worker dies
        self->after(150, [self, wait_for, forge, base, base_can_undo] {
          self->check(forge()->behavior_catalog().value("state").toString() == QLatin1String("loading"), "a catalog request is in flight");
          self->check(kill_child_worker(), "the studio's own worker process was killed");
          wait_for([forge] { const auto s = forge()->behavior_catalog().value("state").toString(); return s == QLatin1String("error") || s == QLatin1String("none"); }, 60000, [self, forge, base, base_can_undo](bool ok) {
            self->log("after the kill: catalog state=" + forge()->behavior_catalog().value("state").toString() + " reason=" + forge()->behavior_catalog().value("reason").toString() + ", worker: " + self->studio->worker()->statusText());
            self->check(ok, "the request that was in flight ended (it did not hang)");
            self->check(forge()->npc() == *base && forge()->can_undo() == *base_can_undo && !forge()->behavior_busy(), "the draft is untouched and the viewmodel is not stuck");
            self->check(!forge()->catalog_ready(), "the forge says the profile is not ready while the worker is gone");
            self->check(self->studio->worker()->state() != WorkerClient::State::Ready, "the studio knows the worker is gone: " + self->studio->worker()->statusText());
            self->step();
          });
        });
      });
      // The controls say why they cannot act, and do not crash, while the worker is gone.
      steps.push_back([self, forge, base] {
        forge()->previewArchetype(QStringLiteral("defensive-warrior"));
        const auto p = forge()->archetype_preview();
        self->check(p.value("state").toString() == QLatin1String("error") && !p.value("message").toString().isEmpty(), "Vista previa without a worker is a visible error: " + p.value("message").toString());
        self->check(p.value("message").toString().contains(QLatin1String("worker"), Qt::CaseInsensitive) && !forge()->worker_message().isEmpty(),
                    "and the error names the worker, not a generic 'index the profile': " + forge()->worker_message());
        self->check(forge()->npc() == *base, "and changes nothing");
        self->step();
      });
      // Restart: reindex brings the worker back and the profile with it.
      steps.push_back([self, wait_for, forge, base, base_can_undo] {
        QElapsedTimer clock;
        clock.start();
        self->studio->reindex();
        wait_for([self] { return self->studio->phase() == StudioController::Phase::Ready; }, 600000, [self, wait_for, forge, base, base_can_undo, clock](bool ok) {
          self->check(ok, QStringLiteral("the studio started a new worker and indexed the profile again (%1 ms)").arg(clock.elapsed()));
          self->check(self->studio->worker()->state() == WorkerClient::State::Ready, "the worker is Ready again");
          self->check(forge()->catalog_ready(), "the forge is ready again");
          self->check(forge()->npc() == *base, "the draft survived the crash and the restart");
          forge()->loadBehaviorCatalog();
          wait_for([forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready"); }, 120000, [self, forge, base, base_can_undo, wait_for](bool ok2) {
            self->check(ok2, "the catalog loads again");
            forge()->previewArchetype(QStringLiteral("defensive-warrior"));
            wait_for([forge] { const auto s = forge()->archetype_preview().value("state").toString(); return s == QLatin1String("ready") || s == QLatin1String("error"); }, 60000, [self, forge, base, base_can_undo, wait_for](bool ok3) {
              self->check(ok3 && forge()->archetype_preview().value("state").toString() == QLatin1String("ready"), "Vista previa works again");
              forge()->applyArchetype(QStringLiteral("defensive-warrior"));
              wait_for([forge] { return forge()->archetype_preview().value("state").toString() == QLatin1String("applied"); }, 60000, [self, forge, base](bool ok4) {
                self->check(ok4, "Aplicar works again");
                forge()->undo();
                self->check(forge()->npc() == *base, "and Deshacer restores the exact draft");
                self->step();
              });
            });
          });
        });
      });
      // A clean stop and start of the worker (what a reinicio of the worker does) and the application restart: the project survives on disk.
      steps.push_back([self, wait_for, forge, base] {
        const auto folder = QDir(self->work_dir).filePath(QStringLiteral("restart-project"));
        QDir(folder).removeRecursively();
        const bool saved = forge()->saveProject(folder);
        self->check(saved, "the project was saved before the clean restart: " + forge()->last_error());
        const auto path = forge()->project_path();
        QFile out(QDir(self->work_dir).filePath("restart-project.path"));
        if (out.open(QIODevice::WriteOnly)) out.write(path.toUtf8());
        QFile json(QDir(self->work_dir).filePath("restart-project.npc.json"));
        if (json.open(QIODevice::WriteOnly)) json.write(QJsonDocument(QJsonObject::fromVariantMap(*base)).toJson(QJsonDocument::Compact));
        self->studio->worker()->stop();
        self->check(self->studio->worker()->state() == WorkerClient::State::Stopped, "the worker was stopped cleanly");
        QElapsedTimer clock;
        clock.start();
        self->studio->reindex();
        wait_for([self] { return self->studio->phase() == StudioController::Phase::Ready; }, 600000, [self, forge, base, clock](bool ok) {
          self->check(ok && self->studio->worker()->state() == WorkerClient::State::Ready, QStringLiteral("a clean restart of the worker brings the studio back (%1 ms)").arg(clock.elapsed()));
          self->check(forge()->npc() == *base && !forge()->dirty(), "the saved project is as it was");
          self->step();
        });
      });
    } else if (name == "forge-reopen") {
      // AG05: after the application was closed and started again: the project saved by worker-recovery opens with the same draft.
      auto forge = [self] { return self->studio->forge(); };
      const auto from = qEnvironmentVariable("SNS_AG05_REOPEN_DIR");
      steps.push_back([self, forge, from] {
        QFile path(QDir(from).filePath("restart-project.path")), json(QDir(from).filePath("restart-project.npc.json"));
        if (!path.open(QIODevice::ReadOnly) || !json.open(QIODevice::ReadOnly)) { self->skip("forge-reopen: SNS_AG05_REOPEN_DIR has no project saved by worker-recovery"); self->next_scenario(); return; }
        const auto expected = QJsonDocument::fromJson(json.readAll()).object().toVariantMap();
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        if (tabs) tabs->setProperty("currentIndex", 2);
        const bool opened = forge()->openProject(QString::fromUtf8(path.readAll()));
        self->check(opened, "the project saved in the previous session opens: " + forge()->last_error());
        self->check(forge()->npc() == expected, "and its draft is exactly the one that was saved");
        self->step();
      });
    } else if (name == "forge-behavior-edge") {
      // AG04 T04 / T09 / T10 / T13 / T22 on the real window and profile: an unknown archetype, a profile that changes while a request is in
      // flight, a cancelled request, an oversized list, and the .sncf round trip of spells and perks. Nothing may mutate the draft by accident.
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(20, *tick);
        };
        (*tick)();
      };
      auto forge = [self] { return self->studio->forge(); };
      auto search = [self, wait_for](const QString& kind, const QString& text, std::function<void(QVariantList)> next) {
        self->studio->forge()->searchRecords(kind, text);
        wait_for([self, kind] {
          const auto s = self->studio->forge()->record_search();
          return s.value("kind").toString() == kind && s.value("state").toString() != QLatin1String("loading");
        }, 60000, [self, next](bool) { next(self->studio->forge()->record_choices()); });
      };
      auto settled = [forge] {
        const auto s = forge()->archetype_preview().value("state").toString();
        return s != QLatin1String("loading") && s != QLatin1String("idle");
      };
      auto base = std::make_shared<QVariantMap>();
      static bool edge_base_can_undo = false;
      steps.push_back([self, wait_for, forge, search, base] {
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        if (tabs) tabs->setProperty("currentIndex", 2);
        wait_for([forge] { return forge()->catalog_ready(); }, 300000, [self, forge, search, base](bool ok) {
          if (!ok) { self->skip("forge-behavior-edge: no indexed profile"); self->next_scenario(); return; }
          self->check(forge()->newProject(QStringLiteral("AG04Edge")), "a Character Forge project was created");
          search(QStringLiteral("race"), QStringLiteral("NordRace"), [self, forge, search, base](QVariantList races) {
            if (!races.isEmpty()) forge()->setNpcValue(QStringLiteral("raceFormKey"), races.first().toMap().value("formKey"));
            search(QStringLiteral("voice"), QStringLiteral("FemaleEvenToned"), [self, forge, base](QVariantList voices) {
              if (!voices.isEmpty()) forge()->setNpcValue(QStringLiteral("voiceFormKey"), voices.first().toMap().value("formKey"));
              forge()->setNpcValue(QStringLiteral("female"), true);
              *base = forge()->npc();
              edge_base_can_undo = forge()->can_undo();  // the setup edits above are undo steps already: later checks compare against this
              self->step();
            });
          });
        });
      });
      // T10: an archetype that does not exist is a visible error and changes nothing.
      steps.push_back([self, forge, wait_for, settled, base] {
        forge()->applyArchetype(QStringLiteral("no-such-archetype"));
        wait_for(settled, 30000, [self, forge, base](bool ok) {
          const auto p = forge()->archetype_preview();
          self->log("unknown archetype: state=" + p.value("state").toString() + " " + p.value("message").toString());
          self->check(ok && p.value("state").toString() == QLatin1String("error") && !p.value("message").toString().isEmpty(), "an unknown archetype is a visible error");
          self->check(forge()->npc() == *base && forge()->can_undo() == edge_base_can_undo, "and the draft was not touched");
          self->step();
        });
      });
      // T04: the profile changes while the answer is on its way: the answer is dropped, nothing is applied.
      steps.push_back([self, forge, wait_for, base] {
        const auto key = forge()->profile_key();
        self->check(!key.isEmpty(), "the studio knows the indexed profile's key: " + key);
        forge()->applyArchetype(QStringLiteral("defensive-warrior"));
        forge()->set_profile_key(QStringLiteral("another-profile"));  // what a re-index of a different profile does to the viewmodel
        self->check(forge()->archetype_preview().value("state").toString() == QLatin1String("idle"), "the preview was discarded together with the profile");
        self->after(4000, [self, forge, base, key] {
          self->check(forge()->npc() == *base && forge()->can_undo() == edge_base_can_undo, "the late answer was not applied to the draft");
          self->check(!forge()->behavior_busy(), "the viewmodel is not stuck waiting");
          forge()->set_profile_key(key);
          self->step();
        });
      });
      // T22: cancelling a request in flight leaves the draft alone and the controls usable.
      steps.push_back([self, forge, wait_for, settled, base] {
        forge()->loadBehaviorCatalog();
        wait_for([forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready"); }, 60000, [self, forge, wait_for, settled, base](bool ok) {
          self->check(ok, "the catalog loads again after the profile key came back");
          forge()->applyArchetype(QStringLiteral("defensive-warrior"));
          forge()->cancelOperation();
          self->after(3000, [self, forge, wait_for, settled, base] {
            const auto p = forge()->archetype_preview();
            self->log("after cancel: state=" + p.value("state").toString());
            self->check(p.value("state").toString() == QLatin1String("cancelled"), "a cancelled request says so");
            self->check(forge()->npc() == *base && forge()->can_undo() == edge_base_can_undo && !forge()->behavior_busy(), "and changed nothing");
            forge()->previewArchetype(QStringLiteral("defensive-warrior"));
            wait_for(settled, 60000, [self, forge](bool ok2) {
              self->check(ok2 && forge()->archetype_preview().value("state").toString() == QLatin1String("ready"), "a new request works right after a cancelled one");
              self->step();
            });
          });
        });
      });
      // T22: a request that does not answer in time ends as a visible error; the draft is untouched and the next request works.
      steps.push_back([self, forge, wait_for, settled, base] {
        (void)settled;
        // The catalog is the one request slow enough to time out: the client polls its deadline every 50 ms, so a faster answer always wins.
        forge()->set_behavior_timeout(std::chrono::milliseconds(1));
        forge()->set_catalog_ready(false);  // forgets the catalog loaded earlier
        forge()->set_catalog_ready(true);
        forge()->loadBehaviorCatalog();
        wait_for([forge] { return forge()->behavior_catalog().value("state").toString() != QLatin1String("loading"); }, 30000, [self, forge, wait_for, settled, base](bool ok) {
          const auto c = forge()->behavior_catalog();
          self->log("timeout: catalog state=" + c.value("state").toString() + " code=" + c.value("code").toString() + " " + c.value("reason").toString());
          self->check(ok && c.value("state").toString() == QLatin1String("error") && !c.value("reason").toString().isEmpty(), "a request that times out is a visible error");
          self->check(forge()->npc() == *base && forge()->can_undo() == edge_base_can_undo && !forge()->behavior_busy(), "the draft is untouched and the viewmodel is not stuck");
          forge()->set_behavior_timeout(std::chrono::seconds(60));
          forge()->loadBehaviorCatalog();  // an error state may be retried
          wait_for([forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready"); }, 60000, [self, forge](bool ok2) {
            self->check(ok2, "the catalog loads once the timeout is back to normal");
            self->step();
          });
        });
      });
      // Oversized list: refused by the draft, not stored, and said.
      steps.push_back([self, forge, base] {
        QVariantList many;
        for (int i = 0; i < 65; ++i) many << QStringLiteral("%1:Skyrim.esm").arg(0x100 + i, 6, 16, QLatin1Char('0')).toUpper();
        forge()->setNpcValue(QStringLiteral("spellFormKeys"), many);
        self->check(forge()->npc().value("spellFormKeys").toList().size() <= 64 && !forge()->last_error().isEmpty(), "a list over the 64 entry limit is refused with a reason: " + forge()->last_error());
        self->check(forge()->npc() == *base, "and the draft is as it was");
        self->step();
      });
      // T13: spells and perks survive a save and a reload of the .sncf project.
      steps.push_back([self, forge, search] {
        search(QStringLiteral("spell"), QStringLiteral("Firebolt"), [self, forge, search](QVariantList spells) {
          self->check(!spells.isEmpty(), "the real Firebolt is found");
          search(QStringLiteral("perk"), QStringLiteral("Overdraw"), [self, forge, spells](QVariantList perks) {
            self->check(!perks.isEmpty(), "the real Overdraw perk is found");
            if (spells.isEmpty() || perks.isEmpty()) { self->next_scenario(); return; }
            QVariantList spell_keys{spells.first().toMap().value("formKey")}, perk_keys{perks.first().toMap().value("formKey")};
            forge()->setNpcValue(QStringLiteral("spellFormKeys"), spell_keys);
            forge()->setNpcValue(QStringLiteral("perkFormKeys"), perk_keys);
            self->check(forge()->npc().value("spellFormKeys").toList() == spell_keys && forge()->npc().value("perkFormKeys").toList() == perk_keys, "the draft holds the chosen spell and perk");
            const auto saved = forge()->npc();
            const auto folder = QDir(self->work_dir).filePath(QStringLiteral("edge-project"));
            QDir(folder).removeRecursively();
            const bool saved_ok = forge()->saveProject(folder);
            self->check(saved_ok, "the project was saved: " + forge()->last_error());
            self->check(!forge()->dirty(), "the saved project is not dirty");
            const auto saved_path = forge()->project_path();  // the studio gives the folder its .sncf name
            self->log("saved as " + saved_path);
            self->check(forge()->newProject(QStringLiteral("AG04EdgeOther")), "another project replaces it");
            // A new project is unsaved work: the studio refuses to open another over it, so it is saved too (to its own folder).
            const auto other_folder = QDir(self->work_dir).filePath(QStringLiteral("edge-project-other"));
            QDir(other_folder).removeRecursively();
            const bool other_saved = forge()->saveProject(other_folder);
            self->check(other_saved, "the other project is saved too: " + forge()->last_error());
            self->check(forge()->npc().value("spellFormKeys").toList().isEmpty(), "the other project has no spells");
            const bool opened = forge()->openProject(saved_path);
            self->check(opened, "the saved project reopens: " + forge()->last_error());
            self->check(forge()->npc() == saved, "the reloaded draft is identical, spells and perks included");
            self->check(forge()->npc().value("spellFormKeys").toList() == spell_keys && forge()->npc().value("perkFormKeys").toList() == perk_keys, "the spell and the perk survived the .sncf round trip");
            // T22: a damaged project file is refused with a reason; the studio keeps the draft it had and stays usable.
            const auto damaged = QDir(self->work_dir).filePath(QStringLiteral("edge-project-damaged.sncf"));
            QDir(damaged).removeRecursively();
            QDir().mkpath(QFileInfo(damaged).absolutePath());
            bool copied = true;
            std::function<void(const QString&, const QString&)> copy_tree = [&](const QString& from, const QString& to) {
              QDir().mkpath(to);
              for (const auto& e : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (e.isDir()) copy_tree(e.absoluteFilePath(), QDir(to).filePath(e.fileName()));
                else copied &= QFile::copy(e.absoluteFilePath(), QDir(to).filePath(e.fileName()));
              }
            };
            copy_tree(saved_path, damaged);
            int broken = 0;
            for (const auto& e : QDir(damaged).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
              QFile f(e.absoluteFilePath());
              if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write("{ this is not json"); ++broken; }
            }
            self->check(copied && broken > 0, QStringLiteral("a damaged copy of the project was prepared (%1 file(s) broken)").arg(broken));
            const auto before_open = forge()->npc();
            const bool refused = !forge()->openProject(damaged);
            const auto why = forge()->last_error();
            self->check(refused && !why.isEmpty(), "a damaged project is refused with a reason: " + why);
            self->check(forge()->npc() == before_open, "and the studio keeps the draft it had");
            forge()->previewArchetype(QStringLiteral("defensive-warrior"));
            self->after(1500, [self, forge] {
              const auto st = forge()->archetype_preview().value("state").toString();
              self->check(st == QLatin1String("ready") || st == QLatin1String("loading"), "the controls still answer after the refusal (" + st + ")");
              self->step();
            });
          });
        });
      });
    } else if (name == "forge-behavior-perf") {
      // AG04 F5: timings of the behavior endpoints on the REAL indexed profile, measured from the call to the settled state the UI shows.
      // One warm-up round per metric (reported apart), then 10 measured rounds; median and p95 of the 10, plus the process memory. Not in "all".
      auto wait_for = [self](std::function<bool()> done, int ms, std::function<void(bool)> next) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, done, ms, next, timer, tick] {
          if (done()) { next(true); return; }
          if (timer->elapsed() > ms) { next(false); return; }
          self->after(10, *tick);
        };
        (*tick)();
      };
      auto forge = [self] { return self->studio->forge(); };
      struct Perf {
        QMap<QString, QList<double>> samples;
        QMap<QString, double> warmup;
        qint64 memory_before = 0;
      };
      auto perf = std::make_shared<Perf>();
      // One metric: `begin` starts the operation, `settled` says it is over, `reset` brings the app back (not timed).
      auto metric = std::make_shared<std::function<void(const QString&, std::function<void()>, std::function<bool()>, std::function<void()>, std::function<void()>)>>();
      *metric = [self, wait_for, perf](const QString& label, std::function<void()> begin, std::function<bool()> settled, std::function<void()> reset, std::function<void()> next) {
        auto round = std::make_shared<int>(0);
        auto loop = std::make_shared<std::function<void()>>();
        *loop = [self, wait_for, perf, label, begin, settled, reset, next, round, loop] {
          auto clock = std::make_shared<QElapsedTimer>();
          clock->start();
          begin();
          wait_for(settled, 60000, [self, perf, label, reset, next, round, loop, clock](bool ok) {
            const double ms = static_cast<double>(clock->nsecsElapsed()) / 1e6;
            self->check(ok, QStringLiteral("%1 round %2 settled").arg(label).arg(*round));
            if (*round == 0) perf->warmup[label] = ms; else perf->samples[label].append(ms);
            reset();
            if (++*round > 10) { next(); return; }
            self->after(30, *loop);
          });
        };
        (*loop)();
      };
      steps.push_back([self, wait_for, forge, perf] {
        auto* tabs = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
        if (tabs) tabs->setProperty("currentIndex", 2);
        wait_for([forge] { return forge()->catalog_ready(); }, 300000, [self, forge, perf](bool ok) {
          if (!ok) { self->skip("forge-behavior-perf: no indexed profile"); self->next_scenario(); return; }
          self->check(forge()->newProject(QStringLiteral("AG04Perf")), "a Character Forge project was created");
          perf->memory_before = process_memory_mb();
          self->step();
        });
      });
      steps.push_back([self, forge] {
        // A voice from the real profile, found the way the UI finds it.
        forge()->searchRecords(QStringLiteral("voice"), QStringLiteral("FemaleEvenToned"));
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [self, forge, tick] {
          if (forge()->record_search().value("state").toString() == QLatin1String("loading")) { self->after(20, *tick); return; }
          const auto voices = forge()->record_choices();
          self->check(!voices.isEmpty(), "the voice picker finds FemaleEvenToned");
          if (!voices.isEmpty()) forge()->setNpcValue(QStringLiteral("voiceFormKey"), voices.first().toMap().value("formKey"));
          forge()->setNpcValue(QStringLiteral("female"), true);
          self->step();
        };
        (*tick)();
      });
      steps.push_back([self, forge, metric] {
        (*metric)(QStringLiteral("catalog"),
                  [forge] { forge()->set_catalog_ready(false); forge()->set_catalog_ready(true); forge()->loadBehaviorCatalog(); },
                  [forge] { return forge()->behavior_catalog().value("state").toString() == QLatin1String("ready"); },
                  [] {}, [self] { self->step(); });
      });
      steps.push_back([self, forge, metric] {
        (*metric)(QStringLiteral("analyzeVoice"),
                  [forge] { forge()->refreshVoiceAnalysis(); },
                  [forge] { return forge()->voice_analysis().value("state").toString() == QLatin1String("ready"); },
                  [] {}, [self] { self->step(); });
      });
      steps.push_back([self, forge, metric] {
        (*metric)(QStringLiteral("preview"),
                  [forge] { forge()->previewArchetype(QStringLiteral("defensive-warrior")); },
                  [forge] { const auto s = forge()->archetype_preview().value("state").toString(); return s != QLatin1String("loading") && s != QLatin1String("idle"); },
                  [forge] { forge()->discardArchetypePreview(); }, [self] { self->step(); });
      });
      steps.push_back([self, forge, metric] {
        (*metric)(QStringLiteral("apply"),
                  [forge] { forge()->applyArchetype(QStringLiteral("defensive-warrior")); },
                  [forge] { const auto s = forge()->archetype_preview().value("state").toString(); return s == QLatin1String("applied") || s == QLatin1String("error") || s == QLatin1String("conflict"); },
                  [forge] { forge()->undo(); }, [self] { self->step(); });
      });
      steps.push_back([self, forge, perf] {
        QJsonObject report;
        const auto percentile = [](QList<double> v, double p) {
          if (v.isEmpty()) return 0.0;
          std::sort(v.begin(), v.end());
          return v[std::min<qsizetype>(v.size() - 1, static_cast<qsizetype>(std::ceil(p * static_cast<double>(v.size()))) - 1)];
        };
        for (auto it = perf->samples.begin(); it != perf->samples.end(); ++it) {
          QJsonArray raw;
          for (double ms : it.value()) raw.append(ms);
          const double median = percentile(it.value(), 0.5), p95 = percentile(it.value(), 0.95);
          report[it.key()] = QJsonObject{{"samplesMs", raw}, {"warmupMs", perf->warmup.value(it.key())}, {"medianMs", median}, {"p95Ms", p95}, {"n", it.value().size()}};
          self->log(QStringLiteral("PERF %1: warm-up %2 ms, median %3 ms, p95 %4 ms (n=%5)").arg(it.key()).arg(perf->warmup.value(it.key()), 0, 'f', 1).arg(median, 0, 'f', 1).arg(p95, 0, 'f', 1).arg(it.value().size()));
        }
        report["memoryBeforeMb"] = static_cast<double>(perf->memory_before);
        report["memoryAfterMb"] = static_cast<double>(process_memory_mb());
        self->log(QStringLiteral("PERF memory: %1 MB before, %2 MB after").arg(perf->memory_before).arg(process_memory_mb()));
        QFile out(QDir(self->work_dir).filePath("forge-behavior-perf.json"));
        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(report).toJson());
        self->check(perf->samples.size() == 4, "the four metrics were measured");
        (void)forge;
        self->step();
      });
    } else if (name == "quit-during-build") {
      steps.push_back([self, hair] {
        hair(QColor("#E6C280"));
        self->after(70, [self] {
          self->log(QStringLiteral("quitting while building=%1 sceneLoading=%2").arg(self->appearance()->building()).arg(self->scene()->loading()));
          self->finish();
        });
      });
    } else {
      check(false, "unknown scenario " + name);
    }
  }

  void finish() {
    if (finished) return;
    finished = true;
    log(QStringLiteral("summary: %1 (%2 scenario(s))").arg(passed ? "PASSED" : "FAILED").arg(scenarios.size()));
    for (const auto& f : failures) log("  failure: " + f);
    for (const auto& s : skipped) log("  skipped: " + s);
    completed(passed);
  }
};

}  // namespace

QStringList appearance_stress_scenarios() {
  return {"hair-single", "hair-sequence", "hair-rapid", "ui-hair-clicks", "color-dialog", "viewport-during-build",
          "early-edit", "sliders", "undo-redo", "reset", "original-toggle", "outfit-during-build", "material-preview-block", "workspace-during-build",
          "replacer-facegen", "hair-safe", "easy-mod", "easy-mod-layout", "easy-mod-outfit", "head-part", "head-part-eyes", "sliders-export", "skin-export", "preset-export", "donor-export", "body-export", "export-reopen", "switch-npc", "undo-redo-exact", "switch-npc-rapid", "project-cycle", "project-blocked-face", "creator-layout", "armor-mod-filter", "appearance-control", "appearance-scan", "face-resolution", "npc-gallery", "npc-portraits", "npc-gallery-paging", "quit-during-build"};
}

void run_appearance_stress(QQuickWindow* window, StudioController* studio, const QStringList& scenarios,
                           const QString& second_npc, const QString& work_dir, std::function<void(bool)> completed) {
  auto s = std::make_shared<Stress>();
  s->window = window;
  s->studio = studio;
  s->scenarios = scenarios;
  s->second_npc = second_npc;
  s->work_dir = work_dir;
  s->completed = std::move(completed);
  s->clock.start();
  s->primary_npc = studio->appearance()->npc_form_key();
  s->watch();
  s->log("start on " + s->primary_npc + " (" + studio->appearance()->npc_label() + "), scenarios: " + scenarios.join(", "));
  s->when_idle([s] { s->next_scenario(); });
}

}  // namespace sns::app
