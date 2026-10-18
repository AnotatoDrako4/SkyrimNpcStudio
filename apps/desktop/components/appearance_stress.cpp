#include "appearance_stress.hpp"

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
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
      for (auto* child : item->childItems()) {
        if (!child->isVisible() || child->width() <= 0) continue;
        const QByteArray cls = child->metaObject()->className();
        const QRectF r = child->mapRectToItem(container, QRectF(0, 0, child->width(), child->height()));
        const QString text = child->property("text").toString().left(40);
        if (elided_fails && child->property("truncated").toBool()) problems << QStringLiteral("elided %1 '%2'").arg(QString(cls), text);
        if (child->metaObject()->indexOfProperty("text") >= 0 && !text.isEmpty() && r.right() > container->width() + 1)
          problems << QStringLiteral("spills %1px '%2' (%3 at x %4, w %5)").arg(int(r.right() - container->width())).arg(text, QString(cls)).arg(int(r.x())).arg(int(r.width()));
        // Buttons with a label (custom-content buttons such as color swatches have none to elide).
        if (!text.isEmpty() && child->metaObject()->indexOfProperty("implicitContentWidth") >= 0 && child->metaObject()->indexOfProperty("checkable") >= 0) {
          const qreal need = child->property("implicitContentWidth").toReal() + child->property("leftPadding").toReal() +
                             child->property("rightPadding").toReal();
          if (need > child->width() + 0.5) problems << QStringLiteral("button '%1' needs %2px, has %3px").arg(text).arg(int(need)).arg(int(child->width()));
        }
        if (inspected) ++*inspected;
        visit(child);
      }
    };
    visit(container);
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
