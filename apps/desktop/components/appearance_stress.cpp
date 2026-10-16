#include "appearance_stress.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

#include "viewmodels/studio_controller.hpp"
#include "sns/core/text.hpp"
#include "sns/rendering/scene.hpp"
#include "sns/face/facegen.hpp"

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
        const QStringList names = {"presets", "rostro", "pelo-ojos", "cuerpo", "vestuario"};
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
    completed(passed);
  }
};

}  // namespace

QStringList appearance_stress_scenarios() {
  return {"hair-single", "hair-sequence", "hair-rapid", "ui-hair-clicks", "color-dialog", "viewport-during-build",
          "early-edit", "sliders", "undo-redo", "reset", "original-toggle", "outfit-during-build", "material-preview-block", "workspace-during-build",
          "replacer-facegen", "hair-safe", "easy-mod", "easy-mod-layout", "easy-mod-outfit", "head-part", "head-part-eyes", "sliders-export", "skin-export", "preset-export", "donor-export", "body-export", "export-reopen", "switch-npc", "undo-redo-exact", "switch-npc-rapid", "project-cycle", "project-blocked-face", "creator-layout", "armor-mod-filter", "quit-during-build"};
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
