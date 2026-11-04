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
      skyrim_phase();
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

  void skyrim_phase() {
    const auto blender = qEnvironmentVariable("SNS_FORGE_SKYRIM_BLENDER");
    skyrim_model = qEnvironmentVariable("SNS_FORGE_SKYRIM_MODEL");
    if (blender.isEmpty() || skyrim_model.isEmpty()) { finish(); return; }
    auto* forge = studio->forge();
    log("=== Convertir a Skyrim: " + skyrim_model);
    skyrim_model_sha = file_sha(skyrim_model);
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
