// ForgeBlenderBridge against sns_fake_blender (tests/tools/fake_blender.cpp): how a Blender run ended, what it wrote
// and whether the result is delivered, offered for recovery or refused. No real Blender is needed.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <filesystem>
#include <functional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "forge_blender_bridge.hpp"
#include "stale_temp_folders.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using sns::app::BlenderRunFacts;
using sns::app::BlenderVerdict;
using sns::app::ForgeBlenderBridge;

namespace {
QString qs(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
QString fake() { return QStringLiteral(SNS_FAKE_BLENDER_EXE); }
QString triangle() { return qs(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj"); }

struct App {
  char name[20] = "sns_forge_process";
  char* argv[2] = {name, nullptr};
  int argc = 1;
  QCoreApplication app{argc, argv};
};

// Environment the fake reads; QProcess passes the test's environment to it.
struct Scenario {
  explicit Scenario(const char* name, bool session_temp = false) {
    qputenv("SNS_FAKE_BLENDER_SCENARIO", name);
    if (session_temp) qputenv("SNS_FAKE_BLENDER_TEMPDIR", "1");
  }
  ~Scenario() {
    qunsetenv("SNS_FAKE_BLENDER_SCENARIO");
    qunsetenv("SNS_FAKE_BLENDER_TEMPDIR");
    qunsetenv("SNS_FAKE_BLENDER_GLB");
  }
};

struct Outcome {
  QString failed;
  bool completed = false;
  QString operation;
  QVariantMap report;
  bool output_existed = false;  // the delivered GLB (or stage) existed while the receiver ran
  QVariantMap recoverable;
};

Outcome wait_for(ForgeBlenderBridge& bridge, const std::function<void()>& start, int watchdog_ms = 20000) {
  Outcome o;
  bool done = false;
  QEventLoop loop;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  const auto finish = [&] { done = true; loop.quit(); };
  QList<QMetaObject::Connection> connections{
      QObject::connect(&bridge, &ForgeBlenderBridge::failed, &loop, [&](const QString& m) { o.failed = m; finish(); }),
      QObject::connect(&bridge, &ForgeBlenderBridge::completed, &loop, [&](const QString& glb, const QVariantMap& r) {
        o.completed = true; o.report = r; o.output_existed = QFileInfo(glb).isFile(); finish();
      }),
      QObject::connect(&bridge, &ForgeBlenderBridge::skyrimCompleted, &loop,
                       [&](const QString& op, const QString& stage, const QVariantMap& r) {
        o.completed = true; o.operation = op; o.report = r; o.output_existed = QFileInfo(stage).isDir(); finish();
      }),
      QObject::connect(&bridge, &ForgeBlenderBridge::recoverable, &loop, [&](const QVariantMap& run) { o.recoverable = run; finish(); }),
      QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] { o.failed = QStringLiteral("test watchdog expired"); finish(); })};
  start();
  watchdog.start(watchdog_ms);
  if (!done) loop.exec();
  watchdog.stop();
  for (const auto& c : connections) QObject::disconnect(c);
  return o;
}

Outcome convert(ForgeBlenderBridge& bridge, const QString& source = triangle()) {
  return wait_for(bridge, [&] { bridge.convert(fake(), source); });
}

// Staging folders of this test process.
int stages() {
  return static_cast<int>(QDir::temp().entryList({QStringLiteral("sns-forge-blender-%1-*").arg(QCoreApplication::applicationPid())},
                                                 QDir::Dirs | QDir::NoDotAndDotDot).size());
}

// sns_fake_blender processes started by this test process that are still running.
int running_fakes() {
  int count = 0;
#ifdef _WIN32
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return -1;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
    if (entry.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"sns_fake_blender.exe") == 0) ++count;
  CloseHandle(snapshot);
#endif
  return count;
}

void prepare(ForgeBlenderBridge& bridge) { bridge.set_timeouts(10000, 20000, 1500); }
}  // namespace

TEST_CASE("Blender run verdict separates process, report, artifact and marker", "[forge][blender][process]") {
  BlenderRunFacts clean;
  clean.marker_ok = true;
  clean.report = "ok";
  clean.artifact = "verified";
  CHECK(sns::app::judge_blender_run(clean) == BlenderVerdict::accepted);

  for (const auto exit : {BlenderRunFacts::Exit::crashed, BlenderRunFacts::Exit::nonZero, BlenderRunFacts::Exit::hungAfterResult,
                          BlenderRunFacts::Exit::timedOut}) {
    auto facts = clean;
    facts.exit = exit;
    facts.exit_code = exit == BlenderRunFacts::Exit::crashed ? 0xC0000005LL : 3;
    CAPTURE(sns::app::exit_name(exit).toStdString());
    CHECK(sns::app::judge_blender_run(facts) == BlenderVerdict::recoverable);
  }
  for (const auto exit : {BlenderRunFacts::Exit::cancelled, BlenderRunFacts::Exit::failedToStart}) {
    auto facts = clean;
    facts.exit = exit;
    CHECK(sns::app::judge_blender_run(facts) == BlenderVerdict::failed);  // never recoverable
  }
  auto no_marker = clean;
  no_marker.marker_ok = false;
  CHECK(sns::app::judge_blender_run(no_marker) == BlenderVerdict::failed);
  auto truncated = clean;
  truncated.exit = BlenderRunFacts::Exit::crashed;
  truncated.artifact = "truncated";
  CHECK(sns::app::judge_blender_run(truncated) == BlenderVerdict::failed);
  auto error_report = clean;
  error_report.report = "error";
  CHECK(sns::app::judge_blender_run(error_report) == BlenderVerdict::failed);
  auto both_markers = clean;
  both_markers.marker_error = true;
  CHECK(sns::app::judge_blender_run(both_markers) == BlenderVerdict::failed);
  auto non_zero_clean_status = clean;
  non_zero_clean_status.exit_code = 1;  // normal exit status but code 1 is not a clean exit
  CHECK(sns::app::judge_blender_run(non_zero_clean_status) == BlenderVerdict::recoverable);
}

TEST_CASE("Blender bridge accepts a clean run with a verified GLB", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  QStringList steps;
  QObject::connect(&bridge, &ForgeBlenderBridge::progressed, [&](const QString& step, double) { steps << step; });
  const auto o = convert(bridge);
  INFO(o.failed.toStdString());
  REQUIRE(o.completed);
  CHECK(o.output_existed);
  const auto run = o.report.value("run").toMap();
  CHECK(run.value("verdict") == "accepted");
  CHECK(run.value("decision") == "accepted");
  CHECK(run.value("exit") == "normal");
  CHECK(run.value("artifact") == "verified");
  CHECK(run.value("report") == "ok");
  CHECK(run.value("markerOk").toBool());
  CHECK(steps.contains("open"));    // the GLB preview conversion reports its steps too
  CHECK(steps.contains("export"));
  CHECK_FALSE(bridge.busy());
  CHECK_FALSE(bridge.hasPendingRecovery());
  CHECK(stages() == 0);
}

TEST_CASE("Blender crash after a verified result is offered for recovery, never delivered silently", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok-then-crash", true);
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  INFO(o.failed.toStdString());
  REQUIRE_FALSE(o.completed);
  REQUIRE(o.failed.isEmpty());
  REQUIRE(bridge.hasPendingRecovery());
  CHECK_FALSE(bridge.busy());
  const auto run = o.recoverable;
  CHECK(run.value("verdict") == "recoverable");
  CHECK(run.value("exit") == "crashed");
  CHECK(run.value("exitHex") == "0xC0000005");
  CHECK(run.value("exitMeaning").toString().contains("EXCEPTION_ACCESS_VIOLATION"));
  CHECK(run.value("artifact") == "verified");
  CHECK(run.value("crashLog").toString().endsWith("fake.crash.txt"));
  CHECK(run.value("stderrTail").toString().contains("EXCEPTION_ACCESS_VIOLATION"));
  CHECK(run.value("message").toString().contains("0xC0000005"));
  CHECK(stages() == 1);  // kept until the decision
  // Blender's own session folder that the crash left behind is removed.
  CHECK(QDir::temp().entryList({QStringLiteral("blender_fake*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

  const auto accepted = wait_for(bridge, [&] { bridge.acceptRecovered(); });
  REQUIRE(accepted.completed);
  CHECK(accepted.output_existed);
  const auto delivered = accepted.report.value("run").toMap();
  CHECK(delivered.value("decision") == "recovered");
  CHECK(delivered.value("exit") == "crashed");  // the delivered result still says how Blender ended
  CHECK_FALSE(bridge.hasPendingRecovery());
  CHECK(stages() == 0);
}

TEST_CASE("Discarding a recoverable Blender result removes it", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok-then-crash");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  REQUIRE_FALSE(convert(bridge).recoverable.isEmpty());
  REQUIRE(stages() == 1);
  const auto o = wait_for(bridge, [&] { bridge.discardRecovered(); });
  CHECK_FALSE(o.completed);
  CHECK(o.failed.contains("descartado"));
  CHECK(bridge.lastRun().value("decision") == "discarded");
  CHECK_FALSE(bridge.hasPendingRecovery());
  CHECK(stages() == 0);
}

TEST_CASE("A new Blender run replaces an undecided recoverable result", "[forge][blender][process]") {
  App app;
  ForgeBlenderBridge bridge;
  prepare(bridge);
  {
    Scenario scenario("ok-then-crash");
    REQUIRE_FALSE(convert(bridge).recoverable.isEmpty());
  }
  Scenario scenario("ok");
  const auto o = convert(bridge);
  REQUIRE(o.completed);
  CHECK(o.report.value("run").toMap().value("decision") == "accepted");
  CHECK(stages() == 0);
}

TEST_CASE("Blender non-zero exit after a verified result is recoverable", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok-then-nonzero");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  REQUIRE(bridge.hasPendingRecovery());
  CHECK(o.recoverable.value("exit") == "nonZero");
  CHECK(o.recoverable.value("exitCode").toLongLong() == 3);
}

TEST_CASE("Blender hanging after its result is stopped after the grace period", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok-then-hang");
  ForgeBlenderBridge bridge;
  bridge.set_timeouts(10000, 60000, 1500);  // the run timeout is far away: the grace period must end it
  QElapsedTimer clock;
  clock.start();
  const auto o = convert(bridge);
  CHECK(clock.elapsed() < 30000);
  REQUIRE(bridge.hasPendingRecovery());
  CHECK(o.recoverable.value("exit") == "hungAfterResult");
  CHECK(running_fakes() == 0);  // the hung process was killed
}

TEST_CASE("Blender crash before writing a result fails without recovery", "[forge][blender][process]") {
  App app;
  Scenario scenario("crash-before");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  CHECK_FALSE(o.completed);
  CHECK(o.recoverable.isEmpty());
  CHECK(o.failed.contains("0xC0000005"));
  CHECK(o.failed.contains("informe"));
  CHECK(bridge.lastRun().value("report") == "missing");
  CHECK(bridge.lastRun().value("verdict") == "failed");
  CHECK_FALSE(bridge.hasPendingRecovery());
  CHECK(stages() == 0);
}

TEST_CASE("Blender bridge refuses an incomplete or altered GLB even after a clean exit", "[forge][blender][process]") {
  App app;
  for (const auto* name : {"truncated", "hash-mismatch"}) {
    CAPTURE(name);
    Scenario scenario(name);
    ForgeBlenderBridge bridge;
    prepare(bridge);
    const auto o = convert(bridge);
    CHECK_FALSE(o.completed);
    CHECK(o.recoverable.isEmpty());
    CHECK(bridge.lastRun().value("exit") == "normal");
    CHECK(bridge.lastRun().value("artifact") == (QString::fromLatin1(name) == "truncated" ? "truncated" : "hashMismatch"));
    CHECK(o.failed.contains("verificación"));
    CHECK(stages() == 0);
  }
}

TEST_CASE("Blender script error is reported with its own message", "[forge][blender][process]") {
  App app;
  Scenario scenario("error-report");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  CHECK_FALSE(o.completed);
  CHECK(o.failed.contains("Fake failure: the model has no mesh objects"));
  CHECK(bridge.lastRun().value("exit") == "nonZero");
  CHECK(bridge.lastRun().value("exitCode").toLongLong() == 17);
  CHECK(bridge.lastRun().value("markerError").toBool());
  CHECK(bridge.lastRun().value("stderrTail").toString().contains("ValueError"));
}

TEST_CASE("Blender run without the completion marker is not used", "[forge][blender][process]") {
  App app;
  Scenario scenario("no-marker");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  CHECK_FALSE(o.completed);
  CHECK(o.recoverable.isEmpty());
  CHECK(bridge.lastRun().value("artifact") == "verified");  // the file is fine; the missing marker alone refuses it
  CHECK(o.failed.contains("confirmar"));
}

TEST_CASE("Blender timeout without a result fails and kills the process", "[forge][blender][process]") {
  App app;
  Scenario scenario("hang");
  ForgeBlenderBridge bridge;
  bridge.set_timeouts(10000, 1500, 1500);
  const auto o = convert(bridge);
  CHECK_FALSE(o.completed);
  CHECK(o.recoverable.isEmpty());
  CHECK(bridge.lastRun().value("exit") == "timedOut");
  CHECK(o.failed.contains("tiempo máximo"));
  CHECK(running_fakes() == 0);
  CHECK(stages() == 0);
}

TEST_CASE("Blender cancellation kills the process and leaves nothing behind", "[forge][blender][process]") {
  App app;
  Scenario scenario("hang", true);  // the fake announces its session temp folder, then hangs
  ForgeBlenderBridge bridge;
  prepare(bridge);
  bool cancelled_once = false;
  QObject::connect(&bridge, &ForgeBlenderBridge::progressed, [&](const QString&, double) {
    if (!cancelled_once) { cancelled_once = true; bridge.cancel(); }  // cancel while the run is really under way
  });
  const auto o = convert(bridge);
  CHECK(cancelled_once);
  CHECK_FALSE(o.completed);
  CHECK(o.recoverable.isEmpty());
  CHECK(bridge.lastRun().value("exit") == "cancelled");
  CHECK(o.failed.contains("cancelada"));
  CHECK_FALSE(bridge.busy());
  CHECK(running_fakes() == 0);
  CHECK(stages() == 0);
  // Killed before any report: the folder announced on the first line is removed anyway.
  CHECK(QDir::temp().entryList({QStringLiteral("blender_fake*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

TEST_CASE("Real Blender cancelled mid-conversion leaves no session folder or stage", "[forge][blender][process][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  const auto folder = qEnvironmentVariable("SNS_LOCAL_BLENDER_FIXTURES");
  const fs::path source = fs::path(folder.toStdWString()) / "bridge_resources" / "heavy_resources.blend";
  if (executable.isEmpty() || folder.isEmpty() || !fs::is_regular_file(source))
    SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_BLENDER_FIXTURES (make_test_models.py bridge_resources)");
  App app;
  const auto sessions = [] { return QDir::temp().entryList({QStringLiteral("blender_*")}, QDir::Dirs | QDir::NoDotAndDotDot); };
  const auto before = sessions();
  ForgeBlenderBridge bridge;
  QString first_step;
  QObject::connect(&bridge, &ForgeBlenderBridge::progressed, [&](const QString& step, double) {
    if (first_step.isEmpty()) { first_step = step; bridge.cancel(); }  // the script is running: Blender made its folder
  });
  const auto o = wait_for(bridge, [&] { bridge.convert(executable, qs(source)); }, 120000);
  CHECK_FALSE(first_step.isEmpty());
  CHECK(bridge.lastRun().value("exit") == "cancelled");
  CHECK(stages() == 0);
  QStringList left;
  for (const auto& name : sessions()) if (!before.contains(name)) left << name;
  INFO(left.join(", ").toStdString());
  CHECK(left.isEmpty());
}

TEST_CASE("Blender bridge rejects invalid paths before starting anything", "[forge][blender][process]") {
  App app;
  sns::test::TempDir temp;
  ForgeBlenderBridge bridge;
  const auto folder = qs(temp.path());
  const auto empty = qs(temp.path() / "empty.blend");
  { QFile file(empty); REQUIRE(file.open(QIODevice::WriteOnly)); }
  CHECK_THROWS_AS(bridge.convert(qs(temp.path() / "no-blender.exe"), triangle()), std::invalid_argument);
  CHECK_THROWS_AS(bridge.convert(fake(), qs(temp.path() / "missing.obj")), std::invalid_argument);
  CHECK_THROWS_AS(bridge.convert(fake(), folder), std::invalid_argument);  // a folder is not a model
  CHECK_THROWS_AS(bridge.convert(fake(), empty), std::invalid_argument);   // 0 bytes
  CHECK_THROWS_AS(bridge.convert(fake(), qs(fs::path(SNS_TEST_MODELS_DIR) / "suzanne.glb")), std::invalid_argument);  // not a Blender input
  CHECK_THROWS_AS(bridge.runSkyrimJob(fake(), "inspect", triangle(), "..\\Evil", "a.nif"), std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob(fake(), "inspect", triangle(), "SNS\\Forge", "a.dds"), std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob(fake(), "delete", triangle(), "SNS\\Forge", "a.nif"), std::invalid_argument);
  CHECK_FALSE(bridge.busy());
  CHECK(stages() == 0);
}

TEST_CASE("Blender bridge handles a model path with spaces and accents", "[forge][blender][process]") {
  App app;
  Scenario scenario("ok");
  sns::test::TempDir temp;
  const auto folder = temp.path() / fs::path(L"Modelo de prueba ñ é");
  fs::create_directories(folder);
  const auto source = folder / fs::path(L"triángulo final.obj");
  fs::copy_file(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj", source);
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge, qs(source));
  INFO(o.failed.toStdString());
  REQUIRE(o.completed);
  CHECK(o.report.value("sourceFormat") == "obj");
}

TEST_CASE("Blender bridge refuses an executable that is not Blender 4 or 5", "[forge][blender][process]") {
  App app;
  Scenario scenario("badversion");
  ForgeBlenderBridge bridge;
  prepare(bridge);
  const auto o = convert(bridge);
  CHECK_FALSE(o.completed);
  CHECK(o.failed.contains("Blender 4 o 5"));
  CHECK(stages() == 0);
}

TEST_CASE("Blender bridge removes staging folders of studios that are no longer running", "[forge][blender][process]") {
  App app;
  qint64 dead = 0x7FFFFF00;
  while (sns::app::process_alive(dead)) --dead;
  const auto stale = QDir::temp().filePath(QStringLiteral("sns-forge-blender-%1-AbC123").arg(dead));
  REQUIRE(QDir().mkpath(stale + "/Data"));
  { QFile file(stale + "/model.glb"); REQUIRE(file.open(QIODevice::WriteOnly)); file.write("left by a crash"); }
  ForgeBlenderBridge bridge;  // the constructor sweeps them
  CHECK_FALSE(QFileInfo::exists(stale));
}

TEST_CASE("Skyrim job results follow the same verdict", "[forge][blender][process]") {
  App app;
  ForgeBlenderBridge bridge;
  prepare(bridge);
  {
    Scenario scenario("ok");
    const auto o = wait_for(bridge, [&] { bridge.runSkyrimJob(fake(), "exportNif", triangle(), "SNS\\Forge", "goblet.nif"); });
    INFO(o.failed.toStdString());
    REQUIRE(o.completed);
    CHECK(o.operation == "exportNif");
    CHECK(o.output_existed);
    CHECK(o.report.value("run").toMap().value("artifact") == "verified");
  }
  {
    Scenario scenario("ok-then-crash");
    const auto o = wait_for(bridge, [&] { bridge.runSkyrimJob(fake(), "inspect", triangle(), "SNS\\Forge", "goblet.nif"); });
    REQUIRE(bridge.hasPendingRecovery());
    CHECK(o.recoverable.value("artifact") == "notApplicable");
    const auto accepted = wait_for(bridge, [&] { bridge.acceptRecovered(); });
    REQUIRE(accepted.completed);
    CHECK(accepted.operation == "inspect");
    CHECK(accepted.report.value("run").toMap().value("decision") == "recovered");
  }
  {
    Scenario scenario("truncated");
    const auto o = wait_for(bridge, [&] { bridge.runSkyrimJob(fake(), "exportNif", triangle(), "SNS\\Forge", "goblet.nif"); });
    CHECK_FALSE(o.completed);
    CHECK(bridge.lastRun().value("artifact") == "truncated");
  }
  CHECK(stages() == 0);
}
