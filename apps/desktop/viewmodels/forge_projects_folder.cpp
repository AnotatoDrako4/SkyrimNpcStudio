#include "forge_projects_folder.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "sns/creation/forge_easy_mode.hpp"

namespace sns::app::forge_projects {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("ForgeProjects", text); }
}  // namespace

QString projects_folder() {
  if (const auto env = qEnvironmentVariable("SNS_FORGE_PROJECTS_DIR"); !env.trimmed().isEmpty())
    return QDir::cleanPath(QDir::fromNativeSeparators(env.trimmed()));
  if (const auto chosen = QSettings().value(QString::fromLatin1(kSettingsKey)).toString(); !chosen.isEmpty() && QDir::isAbsolutePath(chosen))
    return QDir::cleanPath(chosen);
  auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (documents.isEmpty()) documents = QDir::homePath();
  return QDir::cleanPath(documents + QStringLiteral("/SkyrimNpcStudio/Character Forge"));
}

QString refused_projects_folder(const QString& folder, const std::vector<ForbiddenRoot>& roots) {
  return refused_export_directory(folder, roots);
}

QString set_projects_folder(const QString& folder, const std::vector<ForbiddenRoot>& roots) {
  if (auto refused = refused_projects_folder(folder, roots); !refused.isEmpty()) return refused;
  const QFileInfo info(folder.trimmed());
  if (info.exists() && !info.isDir()) return tr("La ruta elegida es un archivo, no una carpeta.");
  QSettings().setValue(QString::fromLatin1(kSettingsKey), QDir::cleanPath(QDir::fromNativeSeparators(folder.trimmed())));
  return {};
}

QString unique_project_path(const QString& folder, const QString& model_name) {
  const auto file = QString::fromStdString(creation::easy::project_folder_name(model_name.toStdString()));
  const auto stem = file.chopped(5);  // ".sncf"
  const QDir dir(folder);
  for (int n = 1; n <= 99; ++n) {
    const auto name = n == 1 ? file : QStringLiteral("%1_%2.sncf").arg(stem).arg(n);
    const auto path = dir.filePath(name);
    if (!QFileInfo::exists(path)) return QDir::cleanPath(path);
  }
  return {};
}

}  // namespace sns::app::forge_projects
