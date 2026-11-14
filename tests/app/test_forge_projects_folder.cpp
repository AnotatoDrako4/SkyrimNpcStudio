#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include "forge_projects_folder.hpp"

using namespace sns::app;

namespace {

// QSettings of the test go to an INI file in a temporary folder, never to the user's registry.
struct IsolatedSettings {
  QTemporaryDir dir;
  IsolatedSettings() {
    REQUIRE(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("SnsForgeProjectsTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SnsForgeProjectsTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    qunsetenv("SNS_FORGE_PROJECTS_DIR");
  }
  ~IsolatedSettings() { qunsetenv("SNS_FORGE_PROJECTS_DIR"); }
};

}  // namespace

TEST_CASE("Forge projects folder defaults to Documents, never to the source tree", "[forge-projects]") {
  IsolatedSettings settings;
  const auto folder = forge_projects::projects_folder();
  CHECK(QDir::isAbsolutePath(folder));
  CHECK(folder.endsWith(QStringLiteral("SkyrimNpcStudio/Character Forge")));
  CHECK_FALSE(folder.contains(QStringLiteral(".Proyectos Games"), Qt::CaseInsensitive));
}

TEST_CASE("Forge projects folder: environment override, then the stored choice", "[forge-projects]") {
  IsolatedSettings settings;
  QTemporaryDir chosen;
  REQUIRE(chosen.isValid());
  CHECK(forge_projects::set_projects_folder(chosen.path(), {}).isEmpty());
  CHECK(forge_projects::projects_folder() == QDir::cleanPath(chosen.path()));

  QTemporaryDir run;
  qputenv("SNS_FORGE_PROJECTS_DIR", QDir::toNativeSeparators(run.path()).toUtf8());
  CHECK(forge_projects::projects_folder() == QDir::cleanPath(run.path()));
}

TEST_CASE("Forge projects folder refuses relative paths, files and the game or MO2 folders", "[forge-projects]") {
  IsolatedSettings settings;
  QTemporaryDir root;
  REQUIRE(root.isValid());
  const auto mods = root.filePath(QStringLiteral("MO2/mods"));
  REQUIRE(QDir().mkpath(mods));
  const std::vector<ForbiddenRoot> roots{{QStringLiteral("MO2 mods"), std::filesystem::path(mods.toStdWString())}};

  CHECK_FALSE(forge_projects::set_projects_folder(QStringLiteral("relative/folder"), roots).isEmpty());
  CHECK_FALSE(forge_projects::set_projects_folder(mods + QStringLiteral("/Mine"), roots).isEmpty());
  CHECK_FALSE(forge_projects::refused_projects_folder(mods, roots).isEmpty());
  QFile file(root.filePath(QStringLiteral("a-file")));
  REQUIRE(file.open(QIODevice::WriteOnly));
  file.close();
  CHECK_FALSE(forge_projects::set_projects_folder(file.fileName(), roots).isEmpty());
  // None of the refusals was stored
  CHECK(QSettings().value(QString::fromLatin1(forge_projects::kSettingsKey)).toString().isEmpty());
  CHECK(forge_projects::set_projects_folder(root.filePath(QStringLiteral("Projects")), roots).isEmpty());
}

TEST_CASE("Forge projects folder never reuses an existing project folder", "[forge-projects]") {
  QTemporaryDir root;
  REQUIRE(root.isValid());
  const auto first = forge_projects::unique_project_path(root.path(), QStringLiteral("Meshy Robot.blend"));
  CHECK(first == QDir::cleanPath(root.filePath(QStringLiteral("Meshy_Robot.sncf"))));
  REQUIRE(QDir().mkpath(first));
  const auto second = forge_projects::unique_project_path(root.path(), QStringLiteral("Meshy Robot.blend"));
  CHECK(second == QDir::cleanPath(root.filePath(QStringLiteral("Meshy_Robot_2.sncf"))));
  REQUIRE(QDir().mkpath(second));
  CHECK(forge_projects::unique_project_path(root.path(), QStringLiteral("Meshy Robot.blend")).endsWith(QStringLiteral("Meshy_Robot_3.sncf")));
  for (int n = 3; n <= 99; ++n) REQUIRE(QDir().mkpath(root.filePath(QStringLiteral("Meshy_Robot_%1.sncf").arg(n))));
  CHECK(forge_projects::unique_project_path(root.path(), QStringLiteral("Meshy Robot.blend")).isEmpty());
}
