#pragma once

#include <QString>
#include <functional>

class QQuickWindow;
namespace sns::app {
class SettingsService;
class WorkbenchState;

// Settings Center on the real window: Ctrl+, opens it with the focus on the search, the search lists real settings
// (and nothing for features that do not exist), the theme, text size, viewer background and a panel width are
// changed through their controls, the preview banner is answered with real clicks (keep and revert), Escape closes.
// Every step is checked on the window (colors, font, View3D environment, layout) and on the stored values; a
// capture per step goes to `dir`, and `dir/expect.json` keeps what a second run must find after a restart.
// With `expect_file` set it only checks that those values came back. Lines "settings: ..." go to stderr.
void run_settings_check(QQuickWindow* window, SettingsService* settings, WorkbenchState* workbench, const QString& dir,
                        const QString& expect_file, std::function<void(bool)> completed);
}  // namespace sns::app
