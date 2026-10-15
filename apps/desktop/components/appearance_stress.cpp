#include "appearance_stress.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

#include "viewmodels/studio_controller.hpp"
#include "sns/core/text.hpp"
#include "sns/face/facegen.hpp"

namespace sns::app {
namespace {

const char* const kPalette[] = {"#111111", "#3B2219", "#5A3825", "#8B5A2B", "#C2B280", "#E6C280",
                                "#A53A1E", "#C0392B", "#808080", "#ECECEC", "#2980B9", "#8E44AD"};

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
        self->appearance()->exportFaceOverhaul(QStringLiteral("SNS_HairSafe.esp"), dir, true, false);
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
          QStringList changed;
          if (manifest.open(QIODevice::ReadOnly)) {
            const auto faces = QJsonDocument::fromJson(manifest.readAll()).object().value("faces").toArray();
            for (const auto& f : faces) {
              for (const auto& field : f.toObject().value("changedFields").toArray()) changed << field.toString();
            }
          }
          self->check(changed == QStringList{"HCLF"}, "record fields changed by the plugin: " + changed.join(", ") + " (expected HCLF)");
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
        self->appearance()->exportCompleteOverhaul(QStringLiteral("SNS_EasyComplete.esp"), dir_c, true, false, QStringLiteral("SNS Tester"), {});
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
          self->log("donor: face edits blocked for this head; donor not applied");
          self->head_part_skip = true;
          self->step();
          return;
        }
        self->head_part_skip = false;
        self->log("applyDonor " + donor);
        self->appearance()->applyDonor(donor, QStringLiteral("donante"));
        self->after(500, [self] { self->then_idle(); });
      });
      const auto dir = QDir(work_dir).filePath(QStringLiteral("donor-%1").arg(QDateTime::currentMSecsSinceEpoch()));
      steps.push_back([self, dir] {
        if (self->head_part_skip) { self->step(); return; }
        self->export_face_and_check(dir, QStringLiteral("SNS_Donor.esp"), {});
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
          "replacer-facegen", "hair-safe", "easy-mod", "easy-mod-outfit", "head-part", "head-part-eyes", "sliders-export", "skin-export", "preset-export", "donor-export", "body-export", "export-reopen", "switch-npc", "quit-during-build"};
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
