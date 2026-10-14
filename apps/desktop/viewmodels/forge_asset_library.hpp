#pragma once

#include <QString>

namespace sns::app::forge_assets {

struct ImportedModel {
  QString relative_path;  // assets/models/<content hash>/model.glb or model.gltf
  QString sha256;          // main file and every validated external resource
  qint64 size_bytes = 0;
  bool duplicate = false;
};

// The project must already be saved as a .sncf folder. Only validated glTF/GLB is copied.
ImportedModel import_model(const QString& project_folder, const QString& source_file);
// Path and confinement check only; used for the library's lightweight availability badges.
QString locate_model(const QString& project_folder, const QString& relative_path);
// Empty result means a missing, escaped, modified or invalid managed asset.
QString resolve_model(const QString& project_folder, const QString& relative_path, const QString& sha256);

}  // namespace sns::app::forge_assets
