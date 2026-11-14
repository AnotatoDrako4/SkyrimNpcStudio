#include "forge_preview_check.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <algorithm>
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
  // A left click through the platform input path, like a real mouse. A QMouseEvent sent directly
  // does not record the press position (Qt 6.11 QSinglePointEvent constructor), so pointer
  // handlers such as TapHandler see a drag past the threshold and cancel the tap.
  void click_at(QPointF pos) { QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos.toPoint()); }
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

  // Event-loop heartbeat: the longest gap between two 16 ms ticks is how long the GUI thread was held (input frozen).
  QTimer heartbeat;
  QElapsedTimer beat;
  qint64 max_stall_ms = 0;

  void start() {
    beat.start();
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, studio, [this] {
      max_stall_ms = std::max(max_stall_ms, beat.restart() - 16);
    });
    heartbeat.start();
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
      recovery_phase();
      return;
    }
    const auto file = QFileInfo(files[index]).absoluteFilePath();
    const bool expected = gltf::load_and_inspect(std::filesystem::path(file.toStdWString())).inspection.loadable();
    log(QStringLiteral("=== %1/%2 %3 (validator: %4)").arg(index + 1).arg(files.size()).arg(file, expected ? "loadable" : "rejected"));
    max_stall_ms = 0;  // the harness's own validation above ran on the GUI thread: measure only the studio's load
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
      log(QStringLiteral("timings %1 parts=%2 maxUiStallMs=%3")
              .arg(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(scene()->timings())).toJson(QJsonDocument::Compact)))
              .arg(scene()->parts().size()).arg(max_stall_ms));
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

  // ---- Convertir a Skyrim (optional phase) -------------------------------------------------------------
  QString skyrim_model;
  QByteArray skyrim_model_sha;

  static QByteArray file_sha(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toUpper()
                                          : QByteArray();
  }
  // Scrolls the enclosing Flickable so a control below the fold can be clicked like a user would after scrolling.
  void ensure_visible(QQuickItem* item) {
    for (auto* p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
      if (!p->property("contentY").isValid() || !p->property("contentHeight").isValid()) continue;
      auto* content = p->property("contentItem").value<QQuickItem*>();
      if (!content) continue;
      const auto y = item->mapToItem(content, QPointF{}).y();
      const auto span = p->property("contentHeight").toReal() - p->height();
      p->setProperty("contentY", std::clamp(y - p->height() / 3, 0.0, std::max(0.0, span)));
      return;
    }
  }
  // Repeater delegates and Loader content are children in the visual tree only (findChildren misses them).
  QQuickItem* find_visual(const QString& text, QQuickItem* from = nullptr) const {
    if (!from) from = window->contentItem();
    if (!from->isVisible()) return nullptr;
    if (from->property("text").toString() == text) return from;
    for (auto* child : from->childItems())
      if (auto* hit = find_visual(text, child)) return hit;
    return nullptr;
  }
  bool click_control(const QString& text) {
    auto* item = find_visual(text);
    if (!item) return false;
    ensure_visible(item);
    click_at(item->mapToScene(QPointF(item->width() / 2, item->height() / 2)));
    return true;
  }
  void capture(const QString& name) {
    if (!dir.isEmpty()) window->grabWindow().save(QDir(dir).filePath(name));
  }

  // ---- Blender run outcome and recovery (optional phase) ----------------------------------------------------------
  // SNS_FORGE_RECOVERY_BLENDER = sns_fake_blender.exe (tests/tools/fake_blender.cpp), SNS_FORGE_RECOVERY_MODEL = an OBJ or
  // BLEND, SNS_FORGE_RECOVERY_GLB = the GLB the fake writes. The BlenderRunPanel is driven with real clicks: a crash after
  // a verified result (discarded, then recovered), a cancel and a clean run; the recovered model survives a reopen.
  QString recovery_project, recovery_model;
  int recovery_round = 0;

  void recovery_phase() {
    const auto blender = qEnvironmentVariable("SNS_FORGE_RECOVERY_BLENDER");
    recovery_model = qEnvironmentVariable("SNS_FORGE_RECOVERY_MODEL");
    const auto glb = qEnvironmentVariable("SNS_FORGE_RECOVERY_GLB");
    if (blender.isEmpty() || recovery_model.isEmpty() || glb.isEmpty()) { skyrim_phase(); return; }
    auto* forge = studio->forge();
    log("=== Blender run recovery with " + blender);
    qputenv("SNS_FAKE_BLENDER_GLB", glb.toLocal8Bit());
    recovery_project = QDir(dir.isEmpty() ? QDir::tempPath() : dir).filePath(QStringLiteral("RecoveryCheck.sncf"));
    QDir(recovery_project).removeRecursively();
    check(forge->saveProject(recovery_project), "recovery project saved " + forge->last_error());
    forge->selectBlenderExecutable(blender);
    check(forge->last_error().isEmpty(), "fake Blender selected " + forge->last_error());
    recovery_convert("ok-then-crash");
  }

  QString model_value(const char* key) const {
    return studio->forge()->workspace().value("model").toMap().value(QString::fromLatin1(key)).toString();
  }

  // Selects the source model again. The workspace converts a newly selected BLEND/FBX/OBJ by itself the first time
  // (CharacterForge.qml syncPreviewAndAutoConvert); later attempts need the viewport's own button, clicked like a user.
  // The last, clean run converts again although the project already holds a GLB for this source (direct call).
  void recovery_convert(const char* scenario) {
    qputenv("SNS_FAKE_BLENDER_SCENARIO", scenario);
    auto* forge = studio->forge();
    const auto before = forge->blender_run();
    forge->selectModelFile(recovery_model);
    const QString name = QString::fromLatin1(scenario);
    if (name == "ok") forge->convertSelectedModelWithBlender();
    after(1200, [self = shared_from_this(), name, before] {
      if (name != "ok") {
        if (self->studio->forge()->blender_run() != before) {
          self->log("the workspace started the conversion by itself for scenario " + name);
        } else {
          const bool clicked = self->click_control(QString::fromUtf8("Convertir a GLB y mostrar en el visor")) ||
                               self->click_control(QString::fromUtf8("Reintentar conversión a GLB"));
          self->check(clicked, "convert button clicked for scenario " + name);
          if (!clicked) { self->capture(QStringLiteral("recovery-no-button.png")); self->finish(); return; }
        }
      }
      self->wait_run(name == "hang" ? QStringLiteral("running") : name == "ok" ? QStringLiteral("done") : QStringLiteral("recoverable"),
                     0, [self, name] { self->recovery_check(name); });
    });
  }

  void wait_run(const QString& state, int waited, std::function<void()> then) {
    const auto run = studio->forge()->blender_run();
    const bool settled = run.value("state").toString() == state && (state != "done" || scene()->state() == "loaded");
    if (!settled && waited < 60000) {
      after(100, [self = shared_from_this(), state, waited, then] { self->wait_run(state, waited + 100, then); });
      return;
    }
    check(settled, QStringLiteral("blenderRun %1 (now %2: %3)").arg(state, run.value("state").toString(), run.value("message").toString()));
    after(600, then);  // let the panel repaint
  }

  void recovery_check(const QString& scenario) {
    auto* forge = studio->forge();
    const auto run = forge->blender_run();
    const auto facts = run.value("run").toMap();
    if (scenario == "ok-then-crash" && recovery_round == 0) {
      capture(QStringLiteral("recovery-1-offered.png"));
      check(facts.value("exitHex").toString() == "0xC0000005" && facts.value("artifact").toString() == "verified",
            "facts: exit " + facts.value("exitHex").toString() + ", artifact " + facts.value("artifact").toString());
      check(find_visual(QString::fromUtf8("Blender terminó mal después de generar un resultado verificado")) != nullptr,
            "the panel says Blender ended badly after a verified result");
      check(model_value("storage") != "managed", "nothing added to the project before a decision");
      ++recovery_round;
      check(click_control(QString::fromUtf8("Descartar")), "Descartar clicked");
      wait_run(QStringLiteral("failed"), 0, [self = shared_from_this()] {
        self->check(self->studio->forge()->last_error().contains(QString::fromUtf8("descartado")), "discard is reported: " + self->studio->forge()->last_error());
        self->check(self->model_value("storage") != "managed", "the discarded GLB was not added");
        self->recovery_convert("hang");
      });
      return;
    }
    if (scenario == "hang") {
      check(forge->blender_busy(), "Blender running (fake hang)");
      check(click_control(QString::fromUtf8("Cancelar")), "Cancelar clicked in the run panel");
      wait_run(QStringLiteral("failed"), 0, [self = shared_from_this()] {
        auto* forge = self->studio->forge();
        self->check(forge->blender_run().value("run").toMap().value("exit").toString() == "cancelled", "the run ended as cancelled");
        self->check(!forge->blender_busy() && !forge->busy(), "Blender stopped, controls usable");
        self->capture(QStringLiteral("recovery-2-cancelled.png"));
        self->recovery_convert("ok-then-crash");
      });
      return;
    }
    if (scenario == "ok-then-crash") {  // second time: keep it
      check(click_control(QString::fromUtf8("Usar el resultado verificado")), "Usar el resultado verificado clicked");
      wait_run(QStringLiteral("done"), 0, [self = shared_from_this()] { self->recovery_recovered(); });
      return;
    }
    // scenario "ok": a clean run carries no warning
    check(facts.value("decision").toString() == "accepted", "clean run accepted");
    check(model_value("storage") == "managed" && !forge->workspace().value("model").toMap().contains("blenderRun"),
          "clean run: managed GLB without a recovery mark");
    check(find_visual(QString::fromUtf8("Resultado recuperado de una ejecución de Blender que terminó mal")) == nullptr,
          "no recovery warning after a clean run");
    capture(QStringLiteral("recovery-4-clean.png"));
    skyrim_phase();
  }

  void recovery_recovered() {
    auto* forge = studio->forge();
    capture(QStringLiteral("recovery-3-recovered.png"));
    const auto mark = forge->workspace().value("model").toMap().value("blenderRun").toMap();
    check(model_value("storage") == "managed" && scene()->has_model(), "recovered GLB is a managed model shown in the viewer");
    check(mark.value("decision").toString() == "recovered" && mark.value("exitHex").toString() == "0xC0000005",
          "the model keeps the recovery mark " + mark.value("exitHex").toString());
    check(find_visual(QString::fromUtf8("Resultado recuperado de una ejecución de Blender que terminó mal")) != nullptr,
          "the viewer shows the recovery warning");
    // Close and reopen the project: the warning is stored in the .sncf.
    check(forge->saveProject(recovery_project), "project saved after the recovery");
    check(forge->openProject(recovery_project), "project reopened " + forge->last_error());
    const auto reopened = forge->workspace().value("model").toMap().value("blenderRun").toMap();
    check(reopened.value("decision").toString() == "recovered", "the reopened .sncf keeps the recovery mark");
    after(300, [self = shared_from_this()] { self->recovery_convert("ok"); });
  }

  void skyrim_phase() {
    const auto blender = qEnvironmentVariable("SNS_FORGE_SKYRIM_BLENDER");
    skyrim_model = qEnvironmentVariable("SNS_FORGE_SKYRIM_MODEL");
    if (blender.isEmpty() || skyrim_model.isEmpty()) { finish(); return; }
    auto* forge = studio->forge();
    log("=== Convertir a Skyrim: " + skyrim_model);
    skyrim_model_sha = file_sha(skyrim_model);
    if (qEnvironmentVariable("SNS_FORGE_EASY_GUIDED") == "1") { guided_start(blender); return; }
    const auto project = QDir(dir.isEmpty() ? QDir::tempPath() : dir).filePath(QStringLiteral("SkyrimCheck.sncf"));
    QDir(project).removeRecursively();
    check(forge->saveProject(project), "project saved as " + project + " " + forge->last_error());
    forge->selectModelFile(skyrim_model);
    check(forge->last_error().isEmpty(), "model selected " + forge->last_error());
    forge->selectBlenderExecutable(blender);
    check(forge->last_error().isEmpty(), "Blender selected " + forge->last_error());
    const bool opened = click_control(QString::fromUtf8("Convertir a Skyrim"));
    check(opened, "navigation opens Convertir a Skyrim");
    if (!passed) { finish(); return; }
    if (qEnvironmentVariable("SNS_FORGE_SKYRIM_EASY") == "1") {
      after(700, [self = shared_from_this()] { self->easy_phase(0); });
      return;
    }
    after(700, [self = shared_from_this()] {
      self->capture(QStringLiteral("skyrim-1-open.png"));
      self->check(self->studio->forge()->skyrim_conversion().value("state").toString() == "none", "no result before the analysis");
      const bool clicked = self->click_control(QString::fromUtf8("ANALIZAR COMPATIBILIDAD CON SKYRIM"));
      self->check(clicked, "ANALIZAR clicked");
      if (!clicked) { self->finish(); return; }
      self->wait_skyrim(QStringLiteral("analyzed"), 0, [self] { self->after_analysis(); });
    });
  }

  // Modo fácil: index the profile first (the mod step needs it), click CREAR MOD CON EL MODELO ACTUAL once, then wait for the chain.
  void easy_phase(int waited) {
    auto* forge = studio->forge();
    const auto profile = qEnvironmentVariable("SNS_FORGE_SKYRIM_PROFILE");
    if (waited == 0 && !profile.isEmpty() && studio->phase() != StudioController::Phase::Ready) {
      log("loading profile " + profile + " for the worker index");
      studio->loadProfile(profile);
      after(1000, [self = shared_from_this()] { self->easy_phase(-1); });
      return;
    }
    if (waited < 0) {
      if (studio->phase() == StudioController::Phase::Error || waited < -600000) { check(false, "profile indexed"); finish(); return; }
      if (studio->phase() != StudioController::Phase::Ready) {
        after(1000, [self = shared_from_this(), waited] { self->easy_phase(waited - 1000); });
        return;
      }
      log(QStringLiteral("profile indexed after %1 s").arg(-waited / 1000));
      if (!find_visual(QString::fromUtf8("CREAR MOD CON EL MODELO ACTUAL"))) click_control(QString::fromUtf8("Convertir a Skyrim"));
      after(1500, [self = shared_from_this()] { self->easy_phase(1); });
      return;
    }
    if (waited == 1 || (waited == 0 && profile.isEmpty())) {
      capture(QStringLiteral("skyrim-1-open.png"));
      const bool clicked = click_control(QString::fromUtf8("CREAR MOD CON EL MODELO ACTUAL"));
      check(clicked, "CREAR MOD CON EL MODELO ACTUAL clicked");
      if (!clicked) { finish(); return; }
      after(1000, [self = shared_from_this()] { self->easy_phase(2); });
      return;
    }
    const auto conversion = forge->skyrim_conversion();
    if (conversion.value("easy").toBool() && waited < 900000) {
      if (waited % 30000 < 1000)
        log("easy: state " + conversion.value("state").toString() + " step " + conversion.value("step").toString() + " mod " +
            conversion.value("modState").toString());
      after(1000, [self = shared_from_this(), waited] { self->easy_phase(waited + 1000); });
      return;
    }
    const auto classification = conversion.value("classification").toMap();
    for (const auto& issue : classification.value("issues").toList()) {
      const auto i = issue.toMap();
      log(QStringLiteral("  [%1] %2 %3").arg(i.value("severity").toString(), i.value("code").toString(), i.value("problem").toString()));
    }
    log(QStringLiteral("prepare: %1").arg(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(conversion.value("prepare").toMap())).toJson(QJsonDocument::Compact).left(1500))));
    for (const auto& c : conversion.value("verification").toMap().value("checks").toList()) {
      const auto m = c.toMap();
      log(QStringLiteral("  %1 %2: %3").arg(m.value("passed").toBool() ? "ok  " : "FAIL", m.value("code").toString(), m.value("detail").toString()));
    }
    capture(QStringLiteral("skyrim-easy-result.png"));
    const auto mod = conversion.value("mod").toMap();
    log("easy result: state " + conversion.value("state").toString() + " mod " + conversion.value("modState").toString() + " " +
        conversion.value("modMessage").toString() + " " + conversion.value("message").toString());
    check(conversion.value("modState").toString() == "done", "modo fácil produced the MO2 mod");
    if (conversion.value("modState").toString() == "done") {
      log("mod zip " + mod.value("zipPath").toString() + " plugin sha256 " + mod.value("pluginSha256").toString());
      check(file_sha(mod.value("pluginPath").toString()) == mod.value("pluginSha256").toString().toLatin1(), "plugin hash matches the worker's");
      check(find_visual(QString::fromUtf8("Mod creado: ") + mod.value("zipPath").toString()) != nullptr, "the easy panel reports the mod");
    }
    check(file_sha(skyrim_model) == skyrim_model_sha, "the original model is unchanged");
    finish();
  }

  // ---- Character Forge Modo Fácil, driven through its own panel (EasyModePanel.qml) -----------------------------
  // SNS_FORGE_EASY_GUIDED=1: the project gets its folder from forge.projectsFolder (SNS_FORGE_PROJECTS_DIR), the
  // panel's buttons are clicked by objectName, and forge.easyMode is checked at every step. SNS_FORGE_EASY_EXPECT=skinned
  // expects a rigged model to stop at the diagnosis with the STAT mod refused.
  QVariantMap easy() const { return studio->forge()->easy_mode(); }
  QVariantMap easy_action(const QString& id) const {
    for (const auto& a : easy().value("actions").toList())
      if (a.toMap().value("id").toString() == id) return a.toMap();
    return {};
  }
  QVariantMap easy_step(const QString& id) const {
    for (const auto& s : easy().value("flow").toMap().value("steps").toList())
      if (s.toMap().value("id").toString() == id) return s.toMap();
    return {};
  }
  // By objectName through the visual tree: Repeater delegates are not QObject children of the window, so
  // findChildren misses them (and a "button disabled" check would pass on a button it never found).
  QQuickItem* object(const QString& name, QQuickItem* from = nullptr) const {
    if (!from) from = window->contentItem();
    if (!from->isVisible()) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
      if (auto* hit = object(name, child)) return hit;
    return nullptr;
  }
  bool enabled(const QString& name) const {
    auto* item = object(name);
    return item && item->property("enabled").toBool();
  }
  bool click_object(const QString& name) {
    auto* item = object(name);
    if (!item || !item->property("enabled").toBool()) return false;
    ensure_visible(item);
    click_at(item->mapToScene(QPointF(item->width() / 2, item->height() / 2)));
    return true;
  }
  void log_easy(const QString& label) {
    QStringList steps;
    for (const auto& s : easy().value("flow").toMap().value("steps").toList())
      steps << s.toMap().value("id").toString() + "=" + s.toMap().value("state").toString();
    QStringList actions;
    for (const auto& a : easy().value("actions").toList())
      actions << a.toMap().value("id").toString() + "=" + a.toMap().value("state").toString();
    log("easy " + label + ": flow [" + steps.join(' ') + "] current=" + easy().value("flow").toMap().value("current").toString() +
        " actions [" + actions.join(' ') + "] diagnosis=" + easy().value("diagnosis").toMap().value("state").toString());
  }
  // Polls until `ready` holds (or the time runs out), then continues.
  void wait_for(const QString& what, std::function<bool()> ready, int waited, int limit, std::function<void()> then) {
    if (ready()) { then(); return; }
    if (waited >= limit) { check(false, what + " (timed out)"); log_easy("timeout"); finish(); return; }
    if (waited % 30000 == 0 && waited > 0) log_easy("waiting for " + what);
    after(500, [self = shared_from_this(), what, ready, waited, limit, then] { self->wait_for(what, ready, waited + 500, limit, then); });
  }

  void guided_start(const QString& blender) {
    auto* forge = studio->forge();
    const auto folder = forge->projects_folder();
    log("projects folder " + folder);
    check(!folder.contains(QStringLiteral("SkyrimNpcStudio-ui2/out/forge-projects"), Qt::CaseInsensitive),
          "the projects folder is not the old fixed source-tree path");
    const bool saved = forge->saveProjectInProjectsFolder(QFileInfo(skyrim_model).fileName());
    check(saved, "project created in the projects folder " + forge->project_path() + " " + forge->last_error());
    check(QDir::cleanPath(QFileInfo(forge->project_path()).absolutePath()).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) == 0,
          "the .sncf lives in forge.projectsFolder");
    forge->selectModelFile(skyrim_model);
    check(forge->last_error().isEmpty(), "model selected " + forge->last_error());
    forge->selectBlenderExecutable(blender);
    check(forge->last_error().isEmpty(), "Blender selected " + forge->last_error());
    if (!passed) { finish(); return; }
    const auto profile = qEnvironmentVariable("SNS_FORGE_SKYRIM_PROFILE");
    if (!profile.isEmpty() && studio->phase() != StudioController::Phase::Ready) {
      log("loading profile " + profile + " for the worker index");
      studio->loadProfile(profile);
    }
    wait_for("profile indexed", [this, profile] { return profile.isEmpty() || studio->phase() == StudioController::Phase::Ready; }, 0, 600000,
             [self = shared_from_this()] {
               if (!qEnvironmentVariable("SNS_FORGE_EASY_NPC").isEmpty()) self->guided_npc();
               else self->guided_open();
             });
  }
  void guided_open() {
    check(click_control(QString::fromUtf8("Modo Fácil")), "navigation opens Modo Fácil");
    after(800, [self = shared_from_this()] {
      self->check(self->object("forgeEasyMode") != nullptr, "the Modo Fácil panel is shown");
      self->check(self->easy().value("actions").toList().size() == 7, "seven actions, each from the backend facts");
      for (const auto* id : {"createCharacter", "editNpc", "bodyAppearance", "outfits", "importBlender", "prepareResource", "exportMod"})
        self->check(self->object(QStringLiteral("easyAction_") + id) != nullptr, QStringLiteral("action card %1 visible").arg(id));
      const auto mod = self->easy_action("exportMod");
      self->check(mod.value("state").toString() == "blocked" && !mod.value("missing").toList().isEmpty(),
                  "export is blocked before the NIF, with what is missing: " + mod.value("reason").toString());
      self->check(self->object("easyActionButton_exportMod") != nullptr && !self->enabled("easyActionButton_exportMod"),
                  "CREAR MOD button present and disabled");
      self->log_easy("opened");
      self->wait_for("preview of this model loaded", [self] { return self->easy_step("preview").value("state").toString() == "done"; }, 0, 900000,
                     [self] {
        self->after(1500, [self] {
          self->capture(QStringLiteral("easy-1-preview.png"));
          self->check(self->scene()->has_model(), "the preview is visible");
          self->log_easy("preview");
          self->check(self->easy().value("flow").toMap().value("current").toString() == "diagnose", "next step is DIAGNOSTICAR");
          self->check(self->click_object("easyRunStep"), "DIAGNOSTICAR clicked");
          self->wait_for("diagnosis finished", [self] {
            const auto s = self->easy_step("diagnose").value("state").toString();
            return s == "done" || s == "blocked";
          }, 0, 900000, [self] { self->after(800, [self] { self->guided_diagnosed(); }); });
        });
      });
    });
  }
  // SNS_FORGE_EASY_NPC=<FormKey>: the NPC section of the Modo Fácil for an NPC of the indexed profile (read-only).
  QVariantMap easy_npc() const { return easy().value("npc").toMap(); }
  void guided_npc() {
    const auto form_key = qEnvironmentVariable("SNS_FORGE_EASY_NPC");
    log("selecting NPC " + form_key);
    studio->selectNpc(form_key);
    // The section must wait for the real analyses: the BodyDirector (body, presets) and the Fit Doctor (garments) of THIS NPC.
    wait_for("NPC analysed by BodyDirector and Outfit Fit Doctor", [this] {
      const auto npc = easy_npc();
      const auto fit_same = npc.value("outfits").toMap().value("verified").toBool();
      return npc.value("state").toString() == "error" || (npc.value("state").toString() == "ready" && fit_same);
    }, 0, 300000, [self = shared_from_this()] { self->after(1500, [self] { self->guided_npc_check(); }); });
  }
  void guided_npc_check() {
    if (auto* tabs = window->findChild<QObject*>(QStringLiteral("workspaceTabs"))) tabs->setProperty("currentIndex", 2);
    after(800, [self = shared_from_this()] {
      self->check(self->click_control(QString::fromUtf8("Modo Fácil")) || self->object("forgeEasyMode") != nullptr, "Modo Fácil shown");
      self->after(1200, [self] {
        const auto npc = self->easy_npc();
        self->log("npc overview: " + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(npc)).toJson(QJsonDocument::Compact).left(3000)));
        self->check(npc.value("state").toString() == "ready", "NPC overview ready: " + npc.value("message").toString());
        const auto body = npc.value("body").toMap();
        self->check(!body.value("familyLabel").toString().isEmpty() && !body.value("path").toString().isEmpty(),
                    "resolved body and family shown: " + body.value("familyLabel").toString() + " " + body.value("path").toString());
        const auto bd = self->studio->body()->npc();
        self->check(body.value("familyLabel").toString() == bd.value("familyLabel").toString() &&
                    body.value("provider").toString() == bd.value("body").toMap().value("provider").toString(),
                    "same body and family as the BodyDirector");
        const auto presets = npc.value("presets").toMap();
        const auto all = self->studio->body()->preset_verdicts();
        self->check(presets.value("total").toInt() == all.value("total").toInt() && presets.value("total").toInt() > 0 &&
                        presets.value("compatible").toInt() == all.value("compatible").toInt(),
                    QStringLiteral("every preset of the profile counted (%1 total, %2 compatible)")
                        .arg(presets.value("total").toInt()).arg(presets.value("compatible").toInt()));
        const auto garments = npc.value("outfits").toMap();
        const auto facts = self->studio->easy_npc_facts();
        {  // evidence of both body analysers (investigation of the CBBE / CBBE 3BA disagreement)
          const auto director = self->studio->body()->npc();
          self->log("BodyDirector body " + director.value("body").toMap().value("path").toString() + " shapes [" +
                    director.value("body").toMap().value("shapes").toStringList().join(", ") + "] physics [" +
                    director.value("body").toMap().value("physicsBones").toStringList().join(", ") + "]");
          for (const auto& e : director.value("evidence").toList()) self->log("  BodyDirector evidence: " + e.toString());
          self->log("Fit Doctor body mesh " + QString::fromStdString(facts.value("fit", nlohmann::json::object()).value("bodyMeshPath", std::string{})) +
                    " confidence " + QString::fromStdString(facts.value("fit", nlohmann::json::object()).value("bodyConfidence", std::string{})));
        }
        self->log(QStringLiteral("fit doctor body family '%1'").arg(QString::fromStdString(facts.value("fit", nlohmann::json::object()).value("bodyFamily", std::string{}))));
        for (const auto& piece : facts.value("fit", nlohmann::json::object()).value("pieces", nlohmann::json::array()))
          self->log(QStringLiteral("  fit piece %1 | family %2 | %3 | topology %4 skeleton %5 | %6")
                        .arg(QString::fromStdString(piece.value("armorName", std::string{})), QString::fromStdString(piece.value("detectedFamily", std::string{})),
                             QString::fromStdString(piece.value("confidence", std::string{})))
                        .arg(piece.value("topologyCompatible", false)).arg(piece.value("skeletonCompatible", false))
                        .arg(QString::fromStdString(piece.value("familyReason", std::string{}))));
        self->check(garments.value("verified").toBool(), "garments come from the Outfit Fit Doctor of this NPC");
        const int listed = garments.value("compatible").toList().size() + garments.value("needsConversion").toList().size() +
                           garments.value("unverified").toList().size();
        self->check(listed > 0, QStringLiteral("%1 garment(s) classified").arg(listed));
        QStringList codes;
        for (const auto& f : npc.value("findings").toList()) codes << f.toMap().value("code").toString();
        if (!garments.value("unverified").toList().isEmpty())
          self->check(codes.contains("garmentsIndeterminate"), "unverified garments are explained (garmentsIndeterminate)");
        const auto doctor_family = QString::fromStdString(facts.value("fit", nlohmann::json::object()).value("bodyFamily", std::string{}));
        if (doctor_family.compare(body.value("familyLabel").toString(), Qt::CaseInsensitive) != 0)
          self->check(codes.contains("bodyFamilyDisagreement"),
                      "the disagreement between BodyDirector (" + body.value("familyLabel").toString() + ") and Fit Doctor (" + doctor_family + ") is reported");
        QStringList ids;
        for (const auto& e : npc.value("exports").toList()) ids << e.toMap().value("id").toString() + "=" + e.toMap().value("state").toString();
        self->log("exports: " + ids.join(' '));
        self->check(ids.size() == 5, "five export routes listed");
        self->check(ids.contains("familyChange=blocked"), "a body family change is never offered for export");
        self->check(self->object("easyNpc") != nullptr && self->object("easyNpcBody") != nullptr && self->object("easyNpcGarments") != nullptr,
                    "the NPC section is visible");
        if (auto* g = self->object("easyNpcGarments")) self->ensure_visible(g);
        self->after(600, [self] {
          self->capture(QStringLiteral("easy-npc.png"));
          // The outfit route opens the Estudio de atuendos (it never exports from here)
          auto* row = self->object("easyNpcExport_outfitPatch");
          QQuickItem* go = nullptr;
          if (row) for (auto* child : row->childItems()) if (child->property("text").toString() == "IR") go = child;
          const bool can = go && go->property("enabled").toBool();
          if (can) {
            self->ensure_visible(go);
            self->click_at(go->mapToScene(QPointF(go->width() / 2, go->height() / 2)));
          }
          self->after(800, [self, can] {
            auto* t = self->window->findChild<QObject*>(QStringLiteral("workspaceTabs"));
            if (can) self->check(t && t->property("currentIndex").toInt() == 1, "IR on the outfit route opens the Estudio de atuendos");
            else self->log("outfit route not enabled (its own checks did not pass): no navigation check");
            self->check(file_sha(self->skyrim_model) == self->skyrim_model_sha, "the model file is unchanged");
            self->finish();
          });
        });
      });
    });
  }

  void guided_diagnosed() {
    log_easy("diagnosed");
    capture(QStringLiteral("easy-2-diagnosis.png"));
    const auto diagnosis = easy().value("diagnosis").toMap();
    for (const auto& f : diagnosis.value("findings").toList()) {
      const auto m = f.toMap();
      log(QStringLiteral("  [%1] %2 %3 | %4").arg(m.value("uiState").toString(), m.value("code").toString(), m.value("resource").toString(),
                                                   m.value("problem").toString()));
    }
    check(object("easyDiagnosisState") != nullptr, "the diagnosis is shown with its state");
    if (qEnvironmentVariable("SNS_FORGE_EASY_EXPECT") == "skinned") {
      check(easy_step("diagnose").value("state").toString() == "blocked", "a rigged model is not exportable as a static resource");
      check(diagnosis.value("state").toString() == "BLOQUEANTE", "diagnosis BLOQUEANTE");
      const auto prepare = easy_action("prepareResource");
      check(prepare.value("state").toString() == "partial" && prepare.value("reason").toString().contains("skinned"),
            "prepare says it needs the skinned profile: " + prepare.value("reason").toString());
      const auto mod = easy_action("exportMod");
      check(mod.value("state").toString() == "blocked" && mod.value("reason").toString().contains("ARMO"),
            "the STAT mod is refused: " + mod.value("reason").toString());
      check(object("easyActionButton_exportMod") != nullptr && !enabled("easyActionButton_exportMod"), "CREAR MOD present and disabled");
      check(studio->forge()->skyrim_conversion().value("state").toString() == "analyzed", "nothing was exported");
      check(file_sha(skyrim_model) == skyrim_model_sha, "the original model is unchanged");
      finish();
      return;
    }
    check(easy_step("diagnose").value("state").toString() == "done", "diagnosis done: " + diagnosis.value("summary").toString());
    check(easy().value("flow").toMap().value("current").toString() == "prepare", "next step is PREPARAR");
    check(click_object("easyRunStep"), "PREPARAR Y EXPORTAR NIF clicked");
    wait_for("NIF exported and validated", [this] {
      const auto s = easy_step("validate").value("state").toString();
      return s == "done" || s == "blocked" || easy_step("prepare").value("state").toString() == "blocked";
    }, 0, 1800000, [self = shared_from_this()] { self->after(800, [self] { self->guided_validated(); }); });
  }
  void guided_validated() {
    log_easy("validated");
    capture(QStringLiteral("easy-3-validated.png"));
    const auto conversion = studio->forge()->skyrim_conversion();
    for (const auto& c : conversion.value("verification").toMap().value("checks").toList()) {
      const auto m = c.toMap();
      log(QStringLiteral("  %1 %2: %3").arg(m.value("passed").toBool() ? "ok  " : "FAIL", m.value("code").toString(), m.value("detail").toString()));
    }
    check(easy_step("validate").value("state").toString() == "done", "VALIDAR done: nifly re-read every check");
    check(conversion.value("skyrimStatus").toString() == "PENDIENTE_SKYRIM", "the NIF stays PENDIENTE_SKYRIM");
    const auto mod = easy_action("exportMod");
    check(mod.value("state").toString() == "available", "export available only now: " + mod.value("reason").toString());
    check(easy().value("flow").toMap().value("canExport").toBool(), "flow allows the export");
    if (!passed) { finish(); return; }
    check(click_object("easyActionButton_exportMod"), "CREAR MOD clicked on its action card");
    wait_for("mod packaged", [this] {
      const auto s = studio->forge()->skyrim_conversion().value("modState").toString();
      return s == "done" || s == "error";
    }, 0, 600000, [self = shared_from_this()] { self->guided_exported(); });
  }
  void guided_exported() {
    after(1200, [self = shared_from_this()] {
      self->log_easy("exported");
      self->capture(QStringLiteral("easy-4-mod.png"));
      const auto conversion = self->studio->forge()->skyrim_conversion();
      const auto mod = conversion.value("mod").toMap();
      self->check(conversion.value("modState").toString() == "done", "mod created " + conversion.value("modMessage").toString());
      for (const auto& s : self->easy().value("flow").toMap().value("steps").toList())
        self->check(s.toMap().value("state").toString() == "done", "step " + s.toMap().value("id").toString() + " done");
      self->check(self->object("easyModResult") != nullptr, "the panel shows the mod and PENDIENTE_SKYRIM");
      self->check(QFileInfo(mod.value("zipPath").toString()).isFile(), "ZIP exists " + mod.value("zipPath").toString());
      self->check(file_sha(mod.value("pluginPath").toString()) == mod.value("pluginSha256").toString().toLatin1(), "plugin hash matches the worker's");
      self->check(mod.value("eslFlagged").toBool() && mod.value("masters").toList().isEmpty(), "ESL plugin without masters");
      self->check(QDir::cleanPath(mod.value("zipPath").toString()).startsWith(QDir::cleanPath(self->studio->forge()->project_path()), Qt::CaseInsensitive),
                  "the mod is inside the project, not in MO2 or Skyrim");
      self->check(file_sha(self->skyrim_model) == self->skyrim_model_sha, "the original model is unchanged");
      self->finish();
    });
  }

  void wait_skyrim(const QString& expected, int waited, std::function<void()> then) {
    const auto state = studio->forge()->skyrim_conversion().value("state").toString();
    if ((state == "running" || state == "none") && waited < 240000) {
      after(250, [self = shared_from_this(), expected, waited, then] { self->wait_skyrim(expected, waited + 250, then); });
      return;
    }
    const auto conversion = studio->forge()->skyrim_conversion();
    check(state == expected, QStringLiteral("conversion state %1 (expected %2) %3").arg(state, expected, conversion.value("message").toString()));
    after(600, then);
  }

  void after_analysis() {
    const auto conversion = studio->forge()->skyrim_conversion();
    const auto classification = conversion.value("classification").toMap();
    const auto counts = classification.value("counts").toMap();
    log(QStringLiteral("analysis: %1 issue(s); blocking %2, autoFixable %3, warning %4, manualReview %5, compatible %6; exportable %7")
            .arg(classification.value("issues").toList().size()).arg(counts.value("blocking").toInt()).arg(counts.value("autoFixable").toInt())
            .arg(counts.value("warning").toInt()).arg(counts.value("manualReview").toInt()).arg(counts.value("compatible").toInt())
            .arg(classification.value("exportable").toBool()));
    for (const auto& issue : classification.value("issues").toList()) {
      const auto i = issue.toMap();
      log(QStringLiteral("  [%1] %2 %3").arg(i.value("severity").toString(), i.value("code").toString(), i.value("problem").toString()));
    }
    capture(QStringLiteral("skyrim-2-analyzed.png"));
    check(find_visual(QString::fromUtf8("Bloqueante: %1").arg(counts.value("blocking").toInt())) != nullptr,
          "the panel shows the blocking count");
    if (!classification.value("exportable").toBool()) { finish(); return; }
    const bool clicked = click_control(QString::fromUtf8("EXPORTAR NIF VALIDADO"));
    check(clicked, "EXPORTAR NIF VALIDADO clicked");
    if (!clicked) { finish(); return; }
    wait_skyrim(QStringLiteral("exported"), 0, [self = shared_from_this()] { self->after_export(); });
  }

  void after_export() {
    const auto conversion = studio->forge()->skyrim_conversion();
    capture(QStringLiteral("skyrim-3-exported.png"));
    const auto folder = conversion.value("folder").toString();
    log("exported folder " + folder);
    for (const auto& c : conversion.value("verification").toMap().value("checks").toList()) {
      const auto m = c.toMap();
      log(QStringLiteral("  %1 %2: %3").arg(m.value("passed").toBool() ? "ok  " : "FAIL", m.value("code").toString(), m.value("detail").toString()));
    }
    check(conversion.value("verification").toMap().value("passed").toBool(), "nifly re-read every check of the exported NIF");
    check(conversion.value("skyrimStatus").toString() == "PENDIENTE_SKYRIM", "the result is labelled PENDIENTE_SKYRIM");
    QFile manifest_file(QDir(folder).filePath("manifest.json"));
    const auto manifest = manifest_file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(manifest_file.readAll()).object() : QJsonObject();
    check(manifest.value("schema").toString() == "sns.forge.skyrim.package/1", "manifest.json written");
    const auto entries = manifest.value("files").toArray();
    bool hashes = !entries.isEmpty(), nif = false;
    for (const auto& f : entries) {
      const auto entry = f.toObject();
      const auto path = QDir(folder).filePath(entry.value("path").toString());
      hashes &= file_sha(path) == entry.value("sha256").toString().toLatin1();
      nif |= path.endsWith(".nif");
      log(QStringLiteral("  %1 %2 bytes sha256 %3").arg(entry.value("path").toString()).arg(entry.value("bytes").toInteger())
              .arg(entry.value("sha256").toString()));
    }
    check(hashes && nif, "every file of the package matches its manifest hash, NIF included");
    check(find_visual(QString::fromUtf8("NIF verificado y empaquetado · Skyrim: PENDIENTE_SKYRIM")) != nullptr, "the panel reports the verified NIF");
    check(file_sha(skyrim_model) == skyrim_model_sha, "the original model is unchanged");
    package_mod(0);
  }

  // EMPAQUETAR MOD MO2 through the real worker; needs an indexed profile (--instance/--profile), else SKIP.
  void package_mod(int waited) {
    if (qEnvironmentVariable("SNS_FORGE_SKYRIM_MOD") != "1") { log("SKIP mod: SNS_FORGE_SKYRIM_MOD not set"); finish(); return; }
    auto* forge = studio->forge();
    // The worker needs an indexed load order: SNS_FORGE_SKYRIM_PROFILE (with --instance) loads it now, read-only, and waits.
    const auto profile = qEnvironmentVariable("SNS_FORGE_SKYRIM_PROFILE");
    if (waited == 0 && !profile.isEmpty() && studio->phase() != StudioController::Phase::Ready) {
      log("loading profile " + profile + " for the worker index");
      studio->loadProfile(profile);
      after(1000, [self = shared_from_this()] { self->package_mod(-1); });
      return;
    }
    if (waited < 0) {
      if (studio->phase() == StudioController::Phase::Error || waited < -600000) {
        check(false, "the profile was indexed for the mod step");
        finish();
        return;
      }
      if (studio->phase() != StudioController::Phase::Ready) {
        after(1000, [self = shared_from_this(), waited] { self->package_mod(waited - 1000); });
        return;
      }
      log(QStringLiteral("profile indexed after %1 s").arg(-waited / 1000));
      // Loading a profile may switch the workspace: bring Character Forge's Skyrim panel back like a user would.
      if (!find_visual(QString::fromUtf8("EMPAQUETAR MOD MO2"))) click_control(QString::fromUtf8("Convertir a Skyrim"));
      after(1500, [self = shared_from_this()] { self->package_mod(0); });
      return;
    }
    const auto state = forge->skyrim_conversion().value("modState").toString();
    if (state == "running" || (waited > 0 && state.isEmpty())) {
      after(500, [self = shared_from_this(), waited] { self->package_mod(waited + 500); });
      return;
    }
    if (state == "error" && forge->skyrim_conversion().value("modMessage").toString().contains("index") && waited < 300000) {
      after(3000, [self = shared_from_this(), waited] { self->click_control(QString::fromUtf8("EMPAQUETAR MOD MO2")); self->package_mod(waited + 3000); });
      return;
    }
    if (state.isEmpty() || (state == "error" && waited == 0)) {
      const bool clicked = click_control(QString::fromUtf8("EMPAQUETAR MOD MO2"));
      check(clicked, "EMPAQUETAR MOD MO2 clicked");
      if (!clicked) { finish(); return; }
      after(500, [self = shared_from_this(), waited] { self->package_mod(waited + 500); });
      return;
    }
    const auto conversion = forge->skyrim_conversion();
    check(state == "done", "mod state " + state + " " + conversion.value("modMessage").toString());
    const auto mod = conversion.value("mod").toMap();
    log("mod zip " + mod.value("zipPath").toString() + " plugin sha256 " + mod.value("pluginSha256").toString());
    for (const auto& v : mod.value("verification").toList()) log("  ok   " + v.toString());
    for (const auto& v : mod.value("pendingVerification").toList()) log("  PEND " + v.toString());
    for (const auto& r : mod.value("records").toList())
      log("  record " + r.toMap().value("recordType").toString() + " " + r.toMap().value("editorId").toString() + " " + r.toMap().value("formKey").toString());
    check(QFileInfo(mod.value("zipPath").toString()).isFile() && QFileInfo(mod.value("pluginPath").toString()).isFile(), "ZIP and plugin exist");
    check(file_sha(mod.value("pluginPath").toString()) == mod.value("pluginSha256").toString().toLatin1(), "plugin hash matches the worker's");
    check(mod.value("eslFlagged").toBool() && mod.value("masters").toList().isEmpty(), "ESL plugin without masters");
    check(find_visual(QString::fromUtf8("Mod MO2 verificado (PAQUETE_MO2_VERIFICADO) · Skyrim: PENDIENTE_SKYRIM")) != nullptr, "the panel reports the mod");
    capture(QStringLiteral("skyrim-4-mod.png"));
    finish();
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
      auto* viewport = self->window->findChild<QQuickItem*>("forgeViewport");
      self->check(viewport && viewport->property("selectionShown").toBool(), "selection box shown around the selected part");
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
    // Cost of one use of each part control: the GUI thread updates every parts list inside these calls.
    if (parts.size() > 1) {
      const int last = static_cast<int>(parts.size()) - 1;
      QElapsedTimer cost;
      cost.start();
      scene()->setPartVisible(0, false);
      const auto toggle_ms = cost.restart();
      scene()->setPartVisible(0, true);
      cost.restart();
      scene()->selectPart(last);
      const auto select_ms = cost.restart();
      scene()->isolatePart(last);
      const auto isolate_ms = cost.restart();
      check(scene()->isolated_part() == last && scene()->any_part_hidden(), "isolatePart shows that part alone");
      scene()->isolatePart(last);
      const auto restore_ms = cost.elapsed();
      check(scene()->isolated_part() == -1 && !scene()->any_part_hidden(), "isolatePart again shows every part");
      log(QStringLiteral("part controls with %1 parts: toggle %2 ms, select %3 ms, isolate %4 ms, show all %5 ms")
              .arg(parts.size()).arg(toggle_ms).arg(select_ms).arg(isolate_ms).arg(restore_ms));
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
