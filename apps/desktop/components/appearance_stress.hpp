#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QQuickWindow;
namespace sns::app {
class StudioController;

// Scripted Character Creator session on the real window (QML, Quick 3D viewport, worker, FaceGen
// regeneration): the regression suite for edits that used to close the studio. Each scenario runs
// the same calls the QML controls make and waits until every asynchronous stage is idle before
// checking the result. Progress goes to stderr ("stress:" lines); a summary to `report` if set.
//
// Scenarios: hair-single, hair-sequence, hair-rapid, sliders, switch-npc, undo-redo, reset,
// original-toggle, outfit-during-build, export-reopen, quit-during-build (must be last).
QStringList appearance_stress_scenarios();
void run_appearance_stress(QQuickWindow* window, StudioController* studio, const QStringList& scenarios,
                           const QString& second_npc, const QString& work_dir, std::function<void(bool)> completed);
}  // namespace sns::app
