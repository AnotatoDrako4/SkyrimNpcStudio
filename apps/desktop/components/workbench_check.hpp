#pragma once

#include <QString>
#include <functional>

class QQuickWindow;
namespace sns::app {
class WorkbenchState;

// Workbench layout on the real window, with real mouse and keyboard events (QTest on the QWindow):
// drags the Character Creator dividers, toggles panels with Ctrl+B / Ctrl+Alt+B / Ctrl+J, focuses the
// viewer with F11 and back, zooms with Ctrl++ / Ctrl+- / Ctrl+0, tries the extreme scales and switches
// workspaces. Every step is checked on the item geometry and on the stored layout; a capture per step
// goes to `dir`, and `dir/expect.json` keeps what a second run must find after a restart.
//
// With `expect_file` set it only checks that the layout stored by an earlier run came back (restart).
// Lines "workbench: ..." go to stderr.
void run_workbench_check(QQuickWindow* window, WorkbenchState* workbench, const QString& dir,
                         const QString& expect_file, std::function<void(bool)> completed);
}  // namespace sns::app
