#include "export_destination.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <system_error>

namespace sns::app {
namespace {

// Lower-case, '/'-separated, ending in '/': "a/b/" is inside "a/" but "a/bc/" is not inside "a/b/".
QString key_of(const QString& absolute) { return QDir::cleanPath(QDir::fromNativeSeparators(absolute)).toLower() + u'/'; }

// The path with its deepest existing ancestor resolved through symbolic links AND junctions (MSVC's
// weakly_canonical uses GetFinalPathNameByHandleW; QFileInfo::canonicalFilePath does not follow junctions in
// Qt 6); the part that does not exist yet is kept as written.
QString canonical_of(const QString& absolute) {
  std::error_code ec;
  const auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(absolute.toStdWString()), ec);
  if (ec || resolved.empty()) return absolute;
  auto text = QString::fromStdWString(resolved.native());
  // "\\?\UNC\server\share" -> "\\server\share"; "\\?\C:\x" -> "C:\x".
  if (text.startsWith(QStringLiteral(R"(\\?\UNC\)"))) text = QStringLiteral(R"(\\)") + text.mid(8);
  else if (text.startsWith(QStringLiteral(R"(\\?\)"))) text = text.mid(4);
  return text;
}

QString tr(const char* text) { return QCoreApplication::translate("ExportDestination", text); }

}  // namespace

QString refused_export_directory(const QString& output, const std::vector<ForbiddenRoot>& roots) {
  const auto trimmed = output.trimmed();
  if (trimmed.isEmpty() || !QDir::isAbsolutePath(QDir::fromNativeSeparators(trimmed)))
    return tr("Elige una carpeta de salida con ruta absoluta.");
  const auto written = QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
  const QString forms[] = {key_of(written), key_of(canonical_of(written))};
  // The deepest forbidden root that contains the folder names it (MO2 mods, not the whole instance).
  const ForbiddenRoot* hit = nullptr;
  qsizetype hit_length = -1;
  for (const auto& root : roots) {
    if (root.path.empty()) continue;
    const auto raw = QDir::cleanPath(QString::fromStdWString(root.path.native()));
    for (const auto& r : {key_of(raw), key_of(canonical_of(raw))}) {
      for (const auto& f : forms) {
        if (f.startsWith(r) && r.size() > hit_length) {
          hit = &root;
          hit_length = r.size();
        }
      }
    }
  }
  if (hit)
    return tr("No se exporta dentro de %1 (%2) ni de sus subcarpetas o enlaces: elige una carpeta propia fuera del juego y de MO2.")
        .arg(hit->label, QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(hit->path.native()))));
  return {};
}

}  // namespace sns::app
