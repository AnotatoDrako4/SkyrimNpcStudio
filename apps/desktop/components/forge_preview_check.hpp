#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QQuickWindow;
namespace sns::app {
class StudioController;

// Character Forge preview on the real window, without an MO2 profile: clicks "Continuar en
// Character Forge sin perfil", creates a project and loads each file in turn. Files the glTF
// validator accepts go through the importer path (CharacterForgeViewModel::selectModelFile) and
// must end "loaded" with rendered bounds and model pixels in the forge View3D; rejected files go
// to the preview directly and must end "error" with a message and nothing loaded. A capture per
// step is saved in `capture_dir`. Lines "forge: ..." go to stderr.
void run_forge_preview_check(QQuickWindow* window, StudioController* studio, const QStringList& files,
                             const QString& capture_dir, std::function<void(bool)> completed);
}  // namespace sns::app
