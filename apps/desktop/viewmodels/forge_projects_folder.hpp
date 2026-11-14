#pragma once

#include <QString>
#include <vector>

#include "export_destination.hpp"

namespace sns::app::forge_projects {

// Where Character Forge creates a project for a model picked before any .sncf exists (Modo Fácil, automatic BLEND
// preview). Order: SNS_FORGE_PROJECTS_DIR (test runs, isolated), the folder chosen by the user (QSettings key
// characterForge/projectsFolder), then <Documents>/SkyrimNpcStudio/Character Forge. Never a path of the source tree.
inline constexpr const char* kSettingsKey = "characterForge/projectsFolder";
[[nodiscard]] QString projects_folder();

// Stores the user's choice (absolute folder). Returns why it is refused, or empty once stored.
[[nodiscard]] QString set_projects_folder(const QString& folder, const std::vector<ForbiddenRoot>& roots);

// Why a projects folder is refused (empty when usable): relative paths, and anything inside the game, MO2's
// instance or its mods/overwrite/profiles/downloads folders (refused_export_directory).
[[nodiscard]] QString refused_projects_folder(const QString& folder, const std::vector<ForbiddenRoot>& roots);

// A .sncf folder for the model name inside `folder` that does not exist yet: <name>.sncf, then <name>_2.sncf ...
// <name>_99.sncf (name from creation::easy::project_folder_name). Empty when all are taken.
[[nodiscard]] QString unique_project_path(const QString& folder, const QString& model_name);

}  // namespace sns::app::forge_projects
