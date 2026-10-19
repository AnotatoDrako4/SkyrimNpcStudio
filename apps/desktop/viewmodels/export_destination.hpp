#pragma once

#include <QString>
#include <filesystem>
#include <vector>

namespace sns::app {

// A folder no export may write into, nor below (audit P0-01/F-13): the game, MO2's instance and its
// mods/overwrite/profiles/downloads folders.
struct ForbiddenRoot {
  QString label;
  std::filesystem::path path;
};

// The forbidden roots of one MO2 snapshot (a template so the test needs no MO2 headers).
template <class Snapshot>
[[nodiscard]] std::vector<ForbiddenRoot> forbidden_roots_of(const Snapshot& s) {
  return {{QStringLiteral("Skyrim"), s.game.root},
          {QStringLiteral("Skyrim (MO2 gamePath)"), s.config.game_path},
          {QStringLiteral("MO2"), s.config.info.root},
          {QStringLiteral("MO2 base"), s.config.base_dir},
          {QStringLiteral("MO2 mods"), s.config.mods_dir},
          {QStringLiteral("MO2 overwrite"), s.config.overwrite_dir},
          {QStringLiteral("MO2 profiles"), s.config.profiles_dir},
          {QStringLiteral("MO2 downloads"), s.config.downloads_dir}};
}

// Why an export folder chosen in the interface is refused, checked BEFORE anything is created there;
// empty when it may be used. The folder may not exist yet: its deepest existing ancestor is resolved
// through links and junctions (canonical path) and compared, as well as the path as written. The worker
// validates the destination again (ExportDestinationPolicy); this check keeps the app from creating a
// folder inside MO2 or the game before the worker has a say.
[[nodiscard]] QString refused_export_directory(const QString& output, const std::vector<ForbiddenRoot>& roots);

}  // namespace sns::app
