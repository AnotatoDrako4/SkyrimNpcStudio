#pragma once

#include <functional>
#include <QString>

class QQuickWindow;
namespace sns::app {
class SceneViewModel;
void verify_viewport(QQuickWindow* window, SceneViewModel* scene, const QString& screenshot,
                     std::function<void(bool)> completed);

// Five fixed-camera captures for before/after comparisons (face only, with hair, full body,
// vanilla steel armor, the NPC's own outfit). The camera is placed at absolute view-space points,
// independent of the scene bounds, so two builds frame the character identically. Each capture
// reports covered and near-white pixels. PNGs and captures.txt go to `directory`.
void capture_set(QQuickWindow* window, SceneViewModel* scene, const QString& directory,
                 std::function<void(bool)> completed);
}
