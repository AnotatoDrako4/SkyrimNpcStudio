#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

#include "viewmodels/studio_controller.hpp"
#include "components/viewport_verifier.hpp"

namespace {

// Professional dark theme for the Fusion style (the QML side reads the same palette).
QPalette dark_palette() {
  QPalette p;
  const QColor window(0x1f, 0x22, 0x27), base(0x17, 0x19, 0x1d), text(0xdc, 0xdf, 0xe4), accent(0x4f, 0x8c, 0xc9);
  p.setColor(QPalette::Window, window);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, QColor(0x22, 0x25, 0x2b));
  p.setColor(QPalette::ToolTipBase, window);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, QColor(0x2a, 0x2e, 0x35));
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::BrightText, Qt::white);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::Link, accent);
  p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x86, 0x90));
  p.setColor(QPalette::Mid, QColor(0x33, 0x37, 0x3e));
  p.setColor(QPalette::Dark, QColor(0x12, 0x14, 0x17));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x70, 0x78));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x70, 0x78));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6b, 0x70, 0x78));
  return p;
}

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setOrganizationName("SkyrimNpcStudio");
  QGuiApplication::setApplicationName("SkyrimNpcStudio");
  QGuiApplication::setApplicationDisplayName("Skyrim NPC Studio");
  QGuiApplication::setApplicationVersion(SNS_VERSION);
  QQuickStyle::setStyle("Fusion");
  QGuiApplication::setPalette(dark_palette());

  QCommandLineParser cli;
  cli.setApplicationDescription("Skyrim NPC Studio");
  cli.addHelpOption();
  cli.addVersionOption();
  const QCommandLineOption instance_opt("instance", "Open this ModOrganizer.ini (or its folder).", "path");
  const QCommandLineOption profile_opt("profile", "Load this MO2 profile.", "name");
  const QCommandLineOption game_opt("game", "Use this Skyrim installation without changing MO2.", "path");
  const QCommandLineOption npc_opt("npc", "Select this NPC (FormKey or load-order FormID) once indexed.", "formKey");
  const QCommandLineOption shot_opt("screenshot", "Save a PNG of the window once loaded, then quit.", "file");
  const QCommandLineOption smoke_opt("smoke-test", "Load, print a summary to stderr and quit (exit code 0 on success).");
  const QCommandLineOption verify_opt("verify-viewport", "Verify real rendered pixels, picking, camera, visibility, UV and compact layout.");
  cli.addOptions({instance_opt, profile_opt, game_opt, npc_opt, shot_opt, smoke_opt, verify_opt});
  cli.process(app);

  // The worker is published next to the executable by the build (bin/worker/).
  const auto worker = QDir(QCoreApplication::applicationDirPath()).filePath("worker/SkyrimNpcStudio.BethesdaWorker.exe");
  sns::app::StudioController studio(worker);

  QQmlApplicationEngine engine;
  engine.setInitialProperties({{"studio", QVariant::fromValue(&studio)}});
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                   Qt::QueuedConnection);
  engine.loadFromModule("SkyrimNpcStudio", "Main");
  if (engine.rootObjects().isEmpty()) return 1;

  // ---- automation: used by scripts/smoke-test.ps1 and handy for shortcuts.
  const bool automated = cli.isSet(shot_opt) || cli.isSet(smoke_opt) || cli.isSet(verify_opt);
  const auto finish = [&](int code) {
    if (cli.isSet(shot_opt)) {
      if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        const bool saved = window->grabWindow().save(cli.value(shot_opt));
        std::fprintf(stderr, "screenshot %s: %s\n", saved ? "saved" : "FAILED", qPrintable(cli.value(shot_opt)));
      }
    }
    const auto s = studio.summary();
    const auto scene = studio.scene()->stats();
    for (const auto& issue : studio.scene()->issues()) {
      const auto row = issue.toMap();
      std::fprintf(stderr, "resource: %s %s %s\n", qPrintable(row.value("code").toString()),
        qPrintable(row.value("subject").toString()), qPrintable(row.value("message").toString()));
    }
    std::fprintf(stderr, "scene: shapes=%d triangles=%lld textures=%d bones=%lld missingBones=%d\n",
                 scene.value("shapes").toInt(), scene.value("triangles").toLongLong(), scene.value("textures").toInt(),
                 scene.value("bones").toLongLong(), scene.value("missingBones").toInt());
    std::fprintf(stderr, "smoke: phase=%d npcs=%d armors=%d plugins=%d files=%lld npc=%s\n", static_cast<int>(studio.phase()),
                 s.value("npcs").toInt(), s.value("armors").toInt(), s.value("activePlugins").toInt(),
                 s.value("files").toLongLong(), qPrintable(studio.npc().value("summary").toMap().value("editorId").toString()));
    QCoreApplication::exit(code);
  };
  if (cli.isSet(instance_opt) && !studio.openInstance(cli.value(instance_opt)) && automated) return 2;
  if (cli.isSet(game_opt) && !studio.setGamePath(cli.value(game_opt))) return 2;
  if (cli.isSet(profile_opt)) {
    QTimer::singleShot(0, &studio, [&] { studio.loadProfile(cli.value(profile_opt)); });
  } else if (automated) {
    QTimer::singleShot(3000, &app, [&] { finish(0); });  // nothing to load: capture the start screen
  }
  QObject::connect(&studio, &sns::app::StudioController::loaded, &app, [&] {
    if (cli.isSet(npc_opt)) {
      studio.selectNpc(cli.value(npc_opt));
    } else if (automated) {
      QTimer::singleShot(1500, &app, [&] { finish(0); });
    }
  });
  QObject::connect(studio.scene(), &sns::app::SceneViewModel::ready, &app, [&] {
    if (automated && cli.isSet(npc_opt)) {
      QTimer::singleShot(2500, &app, [&] {
        if (studio.scene()->parts().isEmpty()) { finish(5); return; }
        if (cli.isSet(verify_opt)) sns::app::verify_viewport(qobject_cast<QQuickWindow*>(engine.rootObjects().first()),
          studio.scene(), cli.value(shot_opt), [&](bool valid) { finish(valid ? 0 : 6); });
        else finish(0);
      });
    }
  });
  QObject::connect(&studio, &sns::app::StudioController::phaseChanged, &app, [&] {
    if (automated && studio.phase() == sns::app::StudioController::Phase::Error)
      QTimer::singleShot(100, &app, [&] { std::fprintf(stderr, "load error: %s\n", qPrintable(studio.progress_text())); finish(3); });
  });
  if (automated) QTimer::singleShot(10 * 60 * 1000, &app, [&] { finish(4); });

  return QGuiApplication::exec();
}
