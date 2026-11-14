#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <nlohmann/json.hpp>
#include <vector>

#include "export_destination.hpp"
#include "sns/mo2/ingame_test_kit.hpp"
#include "sns/mo2/load_order.hpp"

namespace sns::app {

// M2 "Preparar prueba en Skyrim": from an export the user already installed in a (test) MO2 profile to the
// export.bodyDistribution request (kind "ingame-test-kit") that packages its batch file and acceptance report as a
// new mod. Pure: reads the export's manifest.json and the plugin the profile loads, writes nothing; the worker does
// the writing (staging, read-back, manifest, ZIP). StudioController::prepareInGameTest sends `params`.
struct InGameTestPlan {
  QStringList errors;         // non-empty: nothing may be packaged; shown to the user as they are
  QString manifest_path;      // the manifest.json that was read
  QString export_folder;      // its folder
  QString output_directory;   // where the kit's mod folder and ZIP are created: next to the export
  QString mod_name;           // <kit_name>_<yyyyMMdd-HHmmss> (UTC), never an existing folder name by accident
  QString installed_sha256;   // SHA-256 (upper case) of the plugin the profile loads; empty when unreadable
  mo2::InGameTestKit kit;
  nlohmann::json params;      // export.bodyDistribution request; null unless ok()
  [[nodiscard]] bool ok() const { return errors.isEmpty(); }
};

// manifest.json is small (a few KiB per thousand records); a bigger file is not an export manifest.
inline constexpr qint64 kMaxExportManifestBytes = 8 * 1024 * 1024;

// `export_dir_or_manifest`: the export folder or its manifest.json (a local path or file:// URL).
[[nodiscard]] InGameTestPlan plan_ingame_test(const QString& export_dir_or_manifest, const QString& profile,
                                              const mo2::EffectiveLoadOrder& load_order,
                                              const std::vector<ForbiddenRoot>& forbidden_roots, const QDateTime& now_utc);

}  // namespace sns::app
