#include "ingame_test_request.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <exception>

namespace sns::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

QString local_path(const QString& path_or_url) {
  const auto trimmed = path_or_url.trimmed();
  return trimmed.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive) ? QUrl(trimmed).toLocalFile() : trimmed;
}

// Streams the file: plugins can be hundreds of MiB.
QString sha256_of_file(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&f)) return {};
  return QString::fromLatin1(hash.result().toHex()).toUpper();
}

}  // namespace

InGameTestPlan plan_ingame_test(const QString& export_dir_or_manifest, const QString& profile,
                                const mo2::EffectiveLoadOrder& load_order,
                                const std::vector<ForbiddenRoot>& forbidden_roots, const QDateTime& now_utc) {
  InGameTestPlan plan;
  auto fail = [&plan](const QString& message) {
    plan.errors << message;
    plan.params = nullptr;
    return plan;
  };

  const auto input = local_path(export_dir_or_manifest);
  if (input.isEmpty()) return fail(QStringLiteral("Elige la carpeta de una exportación del estudio o su manifest.json."));
  const QFileInfo info(input);
  if (info.isDir()) {
    plan.manifest_path = QDir(info.absoluteFilePath()).filePath(QStringLiteral("manifest.json"));
  } else if (info.fileName().compare(QStringLiteral("manifest.json"), Qt::CaseInsensitive) == 0) {
    plan.manifest_path = info.absoluteFilePath();
  } else {
    return fail(QStringLiteral("«%1» no es una carpeta de exportación ni un manifest.json.").arg(QDir::toNativeSeparators(input)));
  }
  plan.manifest_path = QDir::toNativeSeparators(plan.manifest_path);
  plan.export_folder = QDir::toNativeSeparators(QFileInfo(plan.manifest_path).absolutePath());

  QFile manifest_file(plan.manifest_path);
  if (!manifest_file.exists())
    return fail(QStringLiteral("No hay manifest.json en «%1»: no es una exportación del estudio.").arg(plan.export_folder));
  if (manifest_file.size() > kMaxExportManifestBytes)
    return fail(QStringLiteral("«%1» es demasiado grande para ser un manifiesto de exportación.").arg(plan.manifest_path));
  if (!manifest_file.open(QIODevice::ReadOnly))
    return fail(QStringLiteral("No se pudo leer «%1»: %2").arg(plan.manifest_path, manifest_file.errorString()));
  const auto bytes = manifest_file.readAll();

  mo2::ExportManifestSummary manifest;
  try {
    manifest = mo2::parse_export_manifest(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  } catch (const std::exception& e) {
    return fail(QStringLiteral("«%1» no es un manifiesto de exportación válido: %2").arg(plan.manifest_path, QString::fromUtf8(e.what())));
  }

  // The plugin the profile actually loads; the engine compares its bytes with the export and explains a mismatch.
  const auto active = std::find_if(load_order.active.begin(), load_order.active.end(), [&](const mo2::ResolvedPlugin& p) {
    return QString::fromStdString(p.name).compare(qs(manifest.plugin_file), Qt::CaseInsensitive) == 0;
  });
  if (active != load_order.active.end() && active->path)
    plan.installed_sha256 = sha256_of_file(QString::fromStdWString(active->path->wstring()));

  const mo2::InGameTestKitContext context{ss(profile), ss(plan.installed_sha256),
                                          ss(now_utc.toUTC().toString(Qt::ISODate)), ss(plan.export_folder)};
  plan.kit = mo2::build_ingame_test_kit(manifest, load_order, context);
  if (!plan.kit.ok) {
    for (const auto& e : plan.kit.errors) plan.errors << qs(e);
    if (plan.errors.isEmpty()) plan.errors << QStringLiteral("No se pudo preparar el kit de prueba.");
    plan.params = nullptr;
    return plan;
  }

  // Next to the export, never inside it (the export folder may be the mod MO2 installed from) nor inside MO2/the game.
  plan.output_directory = QDir::toNativeSeparators(QDir::cleanPath(plan.export_folder + QStringLiteral("/..")));
  if (const auto refused = refused_export_directory(plan.output_directory, forbidden_roots); !refused.isEmpty())
    return fail(refused);

  plan.mod_name = qs(plan.kit.kit_name) + QLatin1Char('_') + now_utc.toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  plan.params = nlohmann::json{
      {"modName", ss(plan.mod_name)},
      {"outputDirectory", ss(plan.output_directory)},
      {"createZip", true},
      {"kind", "ingame-test-kit"},
      {"files", nlohmann::json::array({
                    {{"relativePath", plan.kit.batch_file}, {"content", plan.kit.batch_text}},
                    {{"relativePath", plan.kit.report_file}, {"content", plan.kit.report_text}},
                })},
      {"description", "In-game test kit for " + manifest.plugin_file + " generated by Skyrim NPC Studio"},
      {"metadata",
       {{"profile", ss(profile)},
        {"sourceExport", ss(plan.export_folder)},
        {"pluginFile", manifest.plugin_file},
        {"pluginSha256", ss(plan.installed_sha256)},
        {"inGameVerification", "PENDIENTE_SKYRIM"}}}};
  return plan;
}

}  // namespace sns::app
