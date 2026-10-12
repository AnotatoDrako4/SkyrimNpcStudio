#pragma once

#include <functional>
#include <QString>

class QQuickWindow;
namespace sns::app {
class SceneViewModel;
void verify_viewport(QQuickWindow* window, SceneViewModel* scene, const QString& screenshot,
                     std::function<void(bool)> completed);
}
