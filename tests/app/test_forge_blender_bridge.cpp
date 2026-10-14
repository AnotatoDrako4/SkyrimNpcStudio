#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <filesystem>

#include "forge_asset_library.hpp"
#include "forge_blender_bridge.hpp"
#include "sns/creation/forge_project.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
namespace {
QString qs(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
}

TEST_CASE("Blender bridge rejects unsupported inputs", "[forge][blender]") {
  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  sns::app::ForgeBlenderBridge bridge;
  CHECK_THROWS_AS(bridge.convert("missing.exe", "missing.obj"), std::invalid_argument);
  CHECK_FALSE(bridge.busy());
}

TEST_CASE("Blender bridge rejects a non-Blender executable", "[forge][blender]") {
  const auto executable = QStandardPaths::findExecutable("pwsh");
  if (executable.isEmpty()) SKIP("PowerShell is unavailable for the executable rejection test");
  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  sns::app::ForgeBlenderBridge bridge;
  QEventLoop loop;
  QString failure;
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop,
                   [&](const QString& message) { failure = message; loop.quit(); });
  bridge.convert(executable, qs(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj"));
  loop.exec();
  CHECK_FALSE(failure.isEmpty());
  CHECK_FALSE(bridge.busy());
}

TEST_CASE("Blender bridge converts an OBJ into a validated managed GLB", "[forge][blender][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) SKIP("Set SNS_LOCAL_BLENDER to run the real Blender bridge test");
  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  sns::test::TempDir temp;
  const auto project = temp.path() / "Bridge.sncf";
  sns::creation::ForgeProjectStore::save(project, sns::creation::ForgeProjectStore::create("Bridge"));
  sns::app::ForgeBlenderBridge bridge;
  QEventLoop loop;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QString failure, sha;
  QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] { failure = "Test watchdog expired"; bridge.cancel(); loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop, [&](const QString& message) {
    failure = message; loop.quit();
  });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::completed, &loop,
                   [&](const QString& output, const QVariantMap& report) {
    try {
      if (report.value("meshes").toInt() != 1 || report.value("sourceFormat").toString() != "obj")
        throw std::runtime_error("Blender report does not describe the OBJ fixture");
      const auto imported = sns::app::forge_assets::import_model(qs(project), output);
      sha = imported.sha256;
      if (sns::app::forge_assets::resolve_model(qs(project), imported.relative_path, sha).isEmpty())
        throw std::runtime_error("The managed GLB did not validate");
    } catch (const std::exception& error) { failure = QString::fromUtf8(error.what()); }
    loop.quit();
  });
  bridge.convert(executable, qs(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj"));
  watchdog.start(45000);
  loop.exec();
  INFO(failure.toStdString());
  REQUIRE(failure.isEmpty());
  CHECK(sha.size() == 64);
  CHECK_FALSE(bridge.busy());
}

TEST_CASE("Blender bridge cancellation reports failure and returns idle", "[forge][blender][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) SKIP("Set SNS_LOCAL_BLENDER to run cancellation test");
  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  sns::app::ForgeBlenderBridge bridge;
  QEventLoop loop;
  QString failure;
  bool completed = false;
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop,
                   [&](const QString& message) { failure = message; loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::completed, &loop,
                   [&](const QString&, const QVariantMap&) { completed = true; loop.quit(); });
  bridge.convert(executable, qs(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj"));
  QTimer::singleShot(0, &bridge, &sns::app::ForgeBlenderBridge::cancel);
  loop.exec();
  CHECK_FALSE(completed);
  CHECK_FALSE(failure.isEmpty());
  CHECK_FALSE(bridge.busy());
}
