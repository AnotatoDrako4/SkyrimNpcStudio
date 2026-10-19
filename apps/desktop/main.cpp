#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "diagnostics/crash_diagnostics.hpp"
#include "viewmodels/studio_controller.hpp"
#include "components/appearance_stress.hpp"
#include "components/forge_preview_check.hpp"
#include "components/settings_check.hpp"
#include "components/workbench_check.hpp"
#include "viewmodels/settings_service.hpp"
#include "viewmodels/workbench_state.hpp"
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

// Qt warnings and errors (QML binding errors included) go to the session log too, so a crash
// report shows what the UI was complaining about. Repeated lines are folded into a count.
QtMessageHandler g_previous_handler = nullptr;
void message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (type != QtDebugMsg && type != QtInfoMsg) {
    static std::mutex mutex;
    static QString last;
    static int repeats = 0;
    std::lock_guard lock(mutex);
    if (message == last) {
      ++repeats;
    } else {
      if (repeats > 0) sns::app::diag::record("qt", QStringLiteral("(previous message repeated %1 more times)").arg(repeats).toStdString());
      last = message;
      repeats = 0;
      const char* level = type == QtWarningMsg ? "warning" : type == QtCriticalMsg ? "critical" : "fatal";
      sns::app::diag::record("qt", (QString::fromLatin1(level) + ": " + message).toStdString());
    }
  }
  if (g_previous_handler) g_previous_handler(type, context, message);
}

// Deliberate failures with an operation in progress, as a FaceGen build would have, so the test can
// check every part of the report. No Windows error dialog: the test runs unattended.
int crash_selftest(const QString& kind) {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);  // no "abort() has been called" box in Debug builds
#endif
  sns::app::diag::set_context("npc", "0A2C8E:Skyrim.esm HousecarlWhiterun (Lydia) [selftest]");
  sns::app::diag::record("appearance", "setHairColor npc=0A2C8E:Skyrim.esm #302321 -> #a53a1e [selftest]");
  sns::app::diag::Operation op("facegen.full", sns::app::diag::next_task_id(), "selftest");
  op.stage("nif: build_facegen");
  if (kind == "access-violation") {
#ifdef _WIN32
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
  } else if (kind == "terminate") {
    // std::terminate while an exception is in flight, as for an exception escaping a worker task.
    try {
      throw std::runtime_error("selftest: exception nobody catches");
    } catch (...) {
      std::terminate();
    }
  } else if (kind == "abort") {
    std::abort();
  }
  std::fprintf(stderr, "unknown crash-selftest kind\n");
  return 2;
}

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setOrganizationName("SkyrimNpcStudio");
  QGuiApplication::setApplicationName("SkyrimNpcStudio");
  QGuiApplication::setApplicationDisplayName("Skyrim NPC Studio");
  QGuiApplication::setApplicationVersion(SNS_VERSION);
  // Automation and tests keep every preference (window, workbench layout, MO2 profile) in an INI file
  // of their own instead of the user's registry hive.
  if (qEnvironmentVariableIsSet("SNS_SETTINGS_DIR")) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qEnvironmentVariable("SNS_SETTINGS_DIR"));
  }

  // Crash diagnostics first: everything after this point is covered by the session log and the
  // crash handler (minidump + report in %LOCALAPPDATA%\SkyrimNpcStudio\SkyrimNpcStudio\diagnostics,
  // or SNS_DIAGNOSTICS_DIR).
  const auto diagnostics_dir = qEnvironmentVariableIsSet("SNS_DIAGNOSTICS_DIR")
                                   ? qEnvironmentVariable("SNS_DIAGNOSTICS_DIR")
                                   : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/diagnostics";
  const auto previous_crash = sns::app::diag::install(diagnostics_dir.toStdWString());
  sns::app::diag::record("studio", "Skyrim NPC Studio " SNS_VERSION ", Qt " QT_VERSION_STR);
  g_previous_handler = qInstallMessageHandler(&message_handler);
  // Destroyed last (after the QML engine and the studio): only a complete, normal exit marks the
  // session log as cleanly closed.
  struct CleanExit {
    ~CleanExit() { sns::app::diag::shutdown(); }
  } clean_exit;
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
  const QCommandLineOption workspace_opt("smoke-workspace", "Select workspace 0 (character), 1 (outfit) or 2 (forge) for a smoke capture.", "index");
  const QCommandLineOption size_opt("smoke-size", "Resize the smoke capture window (WIDTHxHEIGHT).", "size");
  const QCommandLineOption smoke_opt("smoke-test", "Load, print a summary to stderr and quit (exit code 0 on success).");
  const QCommandLineOption verify_opt("verify-viewport", "Verify real rendered pixels, picking, inspector, camera, visibility, UV, diagnostics and compact layout.");
  const QCommandLineOption capture_opt("capture-set", "Save five fixed-camera captures (face, hair, body, armor, outfit) with pixel metrics.", "directory");
  const QCommandLineOption stress_opt("stress-appearance",
                                      "Run Character Creator regression scenarios on the selected NPC (comma separated, or 'all').",
                                      "scenarios");
  const QCommandLineOption second_npc_opt("second-npc", "Second NPC for the switch-npc stress scenario.", "formKey",
                                          "013BB8:Skyrim.esm");
  const QCommandLineOption stress_work_opt("stress-work", "Folder for the files the stress scenarios export (default out/stress-work).",
                                           "directory", QDir::current().filePath("out/stress-work"));
  const QCommandLineOption crash_opt("crash-selftest",
                                     "Crash on purpose (access-violation, terminate, abort) to check that the crash report and "
                                     "minidump are written. For tests only.",
                                     "kind");
  const QCommandLineOption forge_preview_opt("forge-preview",
                                             "Check the Character Forge 3D preview with these models (';'-separated, loaded in order).",
                                             "files");
  const QCommandLineOption forge_capture_opt("forge-capture", "Folder for the Character Forge preview captures.", "directory");
  const QCommandLineOption workbench_opt("workbench-check",
                                         "Drive the panel layout, focus and scale with real input; captures and expect.json here.",
                                         "directory");
  const QCommandLineOption workbench_expect_opt("workbench-expect",
                                                "Check that the layout an earlier --workbench-check stored came back.", "file");
  cli.addOptions({workbench_opt, workbench_expect_opt});
  const QCommandLineOption settings_opt("settings-check",
                                        "Drive the Settings Center with real input (Ctrl+, search, theme, text, viewer, panels); "
                                        "captures and expect.json here.",
                                        "directory");
  const QCommandLineOption settings_expect_opt("settings-expect", "Check that the settings an earlier --settings-check stored came back.",
                                               "file");
  cli.addOptions({settings_opt, settings_expect_opt});
  cli.addOptions({instance_opt, profile_opt, game_opt, npc_opt, shot_opt, workspace_opt, size_opt, smoke_opt, verify_opt, capture_opt, stress_opt,
                  second_npc_opt, stress_work_opt, crash_opt, forge_preview_opt, forge_capture_opt});
  cli.process(app);
  if (cli.isSet(crash_opt)) return crash_selftest(cli.value(crash_opt));

  // The worker is published next to the executable by the build (bin/worker/).
  const auto worker = QDir(QCoreApplication::applicationDirPath()).filePath("worker/SkyrimNpcStudio.BethesdaWorker.exe");
  sns::app::StudioController studio(worker);
  if (!previous_crash.empty()) {
    studio.log("warning", QObject::tr("La sesión anterior se cerró de forma inesperada. Registro: %1 (informes en %2/crashes)")
                              .arg(QString::fromStdWString(previous_crash.native()), diagnostics_dir));
  }

  QQmlApplicationEngine engine;
  engine.setInitialProperties({{"studio", QVariant::fromValue(&studio)}});
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                   Qt::QueuedConnection);
  engine.loadFromModule("SkyrimNpcStudio", "Main");
  if (engine.rootObjects().isEmpty()) return 1;
  auto* main_window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (cli.isSet(workspace_opt)) {
    bool valid = false;
    const int index = cli.value(workspace_opt).toInt(&valid);
    auto* tabs = main_window ? main_window->findChild<QObject*>("workspaceTabs") : nullptr;
    if (!valid || index < 0 || index > 2 || !tabs || !tabs->setProperty("currentIndex", index)) {
      std::fprintf(stderr, "Invalid smoke workspace or missing workspace tabs\n");
      return 2;
    }
  }
  if (cli.isSet(size_opt)) {
    const auto dimensions = cli.value(size_opt).split('x');
    bool width_ok = false, height_ok = false;
    const int width = dimensions.value(0).toInt(&width_ok), height = dimensions.value(1).toInt(&height_ok);
    if (!main_window || dimensions.size() != 2 || !width_ok || !height_ok || width < 800 || height < 600) {
      std::fprintf(stderr, "Invalid smoke size; expected WIDTHxHEIGHT (at least 800x600)\n");
      return 2;
    }
    main_window->resize(width, height);
  }

  // ---- automation: used by scripts/smoke-test.ps1 and handy for shortcuts.
  const bool automated = cli.isSet(shot_opt) || cli.isSet(workspace_opt) || cli.isSet(size_opt) || cli.isSet(smoke_opt) || cli.isSet(verify_opt) || cli.isSet(capture_opt) ||
                         cli.isSet(stress_opt) || cli.isSet(forge_preview_opt) ||
                         cli.isSet(workbench_opt) || cli.isSet(workbench_expect_opt) || cli.isSet(settings_opt) ||
                         cli.isSet(settings_expect_opt);
  // Automation starts in Character Creator unless told otherwise: the workspace the user left last
  // (restored from the preferences) must not change what a smoke run or a check loads.
  if (automated && !cli.isSet(workspace_opt) && main_window) {
    if (auto* tabs = main_window->findChild<QObject*>("workspaceTabs")) tabs->setProperty("currentIndex", 0);
  }
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
  if (cli.isSet(forge_preview_opt)) {
    // Character Forge needs no MO2 profile: the check starts once the window is up.
    QTimer::singleShot(2500, &app, [&] {
      sns::app::run_forge_preview_check(main_window, &studio, cli.value(forge_preview_opt).split(';', Qt::SkipEmptyParts),
                                        cli.value(forge_capture_opt), [&](bool ok) { finish(ok ? 0 : 10); });
    });
  } else if (cli.isSet(profile_opt)) {
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
  // Only the first scene load starts the automation: verification and captures reload the scene.
  bool automation_started = false;
  QObject::connect(studio.scene(), &sns::app::SceneViewModel::ready, &app, [&] {
    if (automated && cli.isSet(npc_opt) && !automation_started) {
      automation_started = true;
      QTimer::singleShot(2500, &app, [&] {
        if (studio.scene()->parts().isEmpty()) { finish(5); return; }
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        const auto then_stress = [&, window] {
          if (cli.isSet(settings_opt) || cli.isSet(settings_expect_opt)) {
            auto* workbench = engine.singletonInstance<sns::app::WorkbenchState*>("SkyrimNpcStudio", "Workbench");
            auto* settings = engine.singletonInstance<sns::app::SettingsService*>("SkyrimNpcStudio", "AppSettings");
            sns::app::run_settings_check(window, settings, workbench, cli.value(settings_opt), cli.value(settings_expect_opt),
                                         [&](bool ok) { finish(ok ? 0 : 12); });
            return;
          }
          if (cli.isSet(workbench_opt) || cli.isSet(workbench_expect_opt)) {
            auto* workbench = engine.singletonInstance<sns::app::WorkbenchState*>("SkyrimNpcStudio", "Workbench");
            sns::app::run_workbench_check(window, workbench, cli.value(workbench_opt), cli.value(workbench_expect_opt),
                                          [&](bool ok) { finish(ok ? 0 : 11); });
            return;
          }
          if (!cli.isSet(stress_opt)) { finish(0); return; }
          auto scenarios = cli.value(stress_opt).split(',', Qt::SkipEmptyParts);
          if (scenarios == QStringList{"all"}) scenarios = sns::app::appearance_stress_scenarios();
          // One folder per run: exports and the saved project never mix with an earlier run.
          const auto work = QDir(cli.value(stress_work_opt)).filePath(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
          QDir().mkpath(work);
          sns::app::run_appearance_stress(window, &studio, scenarios, cli.value(second_npc_opt), work,
                                          [&](bool ok) { finish(ok ? 0 : 8); });
        };
        const auto then_capture = [&, window, then_stress](bool valid) {
          if (!valid) { finish(6); return; }
          if (!cli.isSet(capture_opt)) { then_stress(); return; }
          sns::app::capture_set(window, studio.scene(), cli.value(capture_opt), [&, then_stress](bool ok) {
            if (!ok) { finish(7); return; }
            then_stress();
          });
        };
        if (cli.isSet(verify_opt)) sns::app::verify_viewport(window, studio.scene(), cli.value(shot_opt), then_capture);
        else then_capture(true);
      });
    }
  });
  // An --npc that cannot be loaded ends the run now instead of at the 10-minute timeout.
  QObject::connect(&studio, &sns::app::StudioController::npcLoadFailed, &app, [&](const QString& message) {
    if (!automated) return;
    std::fprintf(stderr, "npc load error: %s\n", qPrintable(message));
    finish(9);
  });
  QObject::connect(&studio, &sns::app::StudioController::phaseChanged, &app, [&] {
    if (automated && studio.phase() == sns::app::StudioController::Phase::Error)
      QTimer::singleShot(100, &app, [&] { std::fprintf(stderr, "load error: %s\n", qPrintable(studio.progress_text())); finish(3); });
  });
  if (automated) QTimer::singleShot(10 * 60 * 1000, &app, [&] { finish(4); });

  return QGuiApplication::exec();
}
