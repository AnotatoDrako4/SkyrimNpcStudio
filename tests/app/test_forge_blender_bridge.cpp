#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
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

TEST_CASE("Blender bridge imports generated OBJ FBX and BLEND into managed GLB", "[forge][blender][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  const auto folder = qEnvironmentVariable("SNS_LOCAL_BLENDER_FIXTURES");
  if (executable.isEmpty() || !QFileInfo(executable).isExecutable() || folder.isEmpty())
    SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_BLENDER_FIXTURES for the complete bridge regression");

  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  const fs::path fixture_dir(folder.toStdWString());

  sns::test::TempDir temp;
  const auto project = temp.path() / "BridgeFormats.sncf";
  sns::creation::ForgeProjectStore::save(project, sns::creation::ForgeProjectStore::create("Bridge formats"));
  for (const auto* extension : {"obj", "fbx", "blend"}) {
    CAPTURE(extension);
    const auto source = fixture_dir / (std::string("bridge_sample.") + extension);
    REQUIRE(fs::is_regular_file(source));
    sns::app::ForgeBlenderBridge bridge;
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QString failure, sha;
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
      failure = "Bridge fixture conversion timed out"; bridge.cancel(); loop.quit();
    });
    QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop,
                     [&](const QString& message) { failure = message; loop.quit(); });
    QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::completed, &loop,
                     [&](const QString& glb, const QVariantMap& report) {
      try {
        if (report.value("sourceFormat").toString() != QString::fromLatin1(extension) ||
            report.value("meshes").toInt() != 1)
          throw std::runtime_error("The bridge report does not match the input fixture");
        const auto imported = sns::app::forge_assets::import_model(qs(project), glb);
        sha = imported.sha256;
        if (sns::app::forge_assets::resolve_model(qs(project), imported.relative_path, sha).isEmpty())
          throw std::runtime_error("Converted GLB was not stored and validated in .sncf");
      } catch (const std::exception& error) { failure = QString::fromUtf8(error.what()); }
      loop.quit();
    });
    bridge.convert(executable, qs(source));
    watchdog.start(90000);
    loop.exec();
    watchdog.stop();
    INFO(failure.toStdString());
    REQUIRE(failure.isEmpty());
    CHECK(sha.size() == 64);
    CHECK_FALSE(bridge.busy());
  }
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

TEST_CASE("Blender bridge preview relinks moved textures and lightens a heavy scene", "[forge][blender][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  const auto folder = qEnvironmentVariable("SNS_LOCAL_BLENDER_FIXTURES");
  const fs::path source = fs::path(folder.toStdWString()) / "bridge_resources" / "heavy_resources.blend";
  if (executable.isEmpty() || !QFileInfo(executable).isExecutable() || folder.isEmpty() || !fs::is_regular_file(source))
    SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_BLENDER_FIXTURES (make_test_models.py bridge_resources)");

  char name[] = "sns_forge_asset_tests";
  char* args[] = {name, nullptr};
  int count = 1;
  QCoreApplication app(count, args);
  const auto before = fs::last_write_time(source);
  const auto bytes = fs::file_size(source);

  sns::app::ForgeBlenderBridge bridge;
  QEventLoop loop;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QString failure;
  QVariantMap report;
  QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] { failure = "timed out"; bridge.cancel(); loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop, [&](const QString& m) { failure = m; loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::completed, &loop, [&](const QString&, const QVariantMap& r) {
    report = r;
    loop.quit();
  });
  bridge.convert(executable, qs(source));
  watchdog.start(240000);
  loop.exec();
  watchdog.stop();
  INFO(failure.toStdString());
  REQUIRE(failure.isEmpty());

  // The texture that exists once below the model is relinked; the name found twice in non-matching folders is not guessed.
  const auto relink = report.value("images").toMap().value("relink").toMap();
  CHECK(relink.value("missingBefore").toInt() == 2);
  const auto relinked = relink.value("relinked").toList();
  REQUIRE(relinked.size() == 1);
  CHECK(QDir::fromNativeSeparators(relinked.front().toMap().value("to").toString()).endsWith("moved/textures/heavy_diffuse.png"));
  const auto ambiguous = relink.value("ambiguous").toList();
  REQUIRE(ambiguous.size() == 1);
  CHECK(ambiguous.front().toMap().value("candidates").toList().size() == 2);

  // Over the budget: the preview is reduced (and says so), never above the budget
  const auto reduction = report.value("previewReduction").toMap();
  CHECK(reduction.value("applied").toBool());
  CHECK(reduction.value("trianglesBefore").toLongLong() > 300000);
  CHECK(reduction.value("trianglesAfter").toLongLong() <= 300000);
  CHECK(reduction.value("trianglesAfter").toLongLong() > 100000);
  CHECK(report.value("timings").toMap().contains("reduce"));

  // The user's file is never written
  CHECK(fs::last_write_time(source) == before);
  CHECK(fs::file_size(source) == bytes);
}
