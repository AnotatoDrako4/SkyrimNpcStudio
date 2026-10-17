#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <QThread>
#include <filesystem>

#include "temp_dir.hpp"
#include "workbench_state.hpp"

using sns::app::LayoutValues;
using sns::app::WorkbenchState;
using sns::app::WorkspaceLayout;

namespace {

// QTimer (the debounced save) needs an application object; one is enough for the whole run.
void ensure_app() {
  static int argc = 1;
  static char name[] = "sns_workbench_tests";
  static char* argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
}

QString ini_in(const sns::test::TempDir& dir) { return QString::fromStdWString((dir.path() / "workbench.ini").wstring()); }

}  // namespace

TEST_CASE("Workbench starts at the documented defaults", "[workbench]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState state(ini_in(dir));
  CHECK(state.ui_scale() == 100);
  CHECK(state.font_scale() == 100);
  CHECK(state.density() == WorkbenchState::Normal);
  CHECK(state.font_family().isEmpty());
  CHECK(state.appearance_at_defaults());
  for (const auto& zone : WorkbenchState::kZones) CHECK(state.zone_fonts().value(zone).toInt() == 100);
  // Every workspace has its own object, with different panel sizes and the diagnostics closed.
  auto* creator = state.layout("creator");
  auto* outfit = state.layout("outfit");
  auto* forge = state.layout("forge");
  REQUIRE((creator && outfit && forge));
  CHECK(state.layout("nope") == nullptr);
  CHECK(creator != outfit);
  CHECK(creator->right_width() != forge->right_width());
  for (auto* layout : {creator, outfit, forge}) {
    CHECK(layout->left_visible());
    CHECK(layout->right_visible());
    CHECK_FALSE(layout->bottom_visible());
    CHECK_FALSE(layout->maximized());
  }
}

TEST_CASE("Workbench clamps scales to the documented ranges", "[workbench]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState state(ini_in(dir));
  state.set_ui_scale(500);
  CHECK(state.ui_scale() == 150);
  state.set_ui_scale(10);
  CHECK(state.ui_scale() == 85);
  state.set_font_scale(1000);
  CHECK(state.font_scale() == 160);
  state.set_font_scale(1);
  CHECK(state.font_scale() == 80);
  state.set_density(9);
  CHECK(state.density() == WorkbenchState::Comfortable);
  state.set_density(-4);
  CHECK(state.density() == WorkbenchState::Compact);
  state.setZoneFont("inspector", 900);
  CHECK(state.zone_fonts().value("inspector").toInt() == WorkbenchState::kMaxZoneFont);
  state.setZoneFont("not-a-zone", 150);
  CHECK_FALSE(state.zone_fonts().contains("not-a-zone"));
  CHECK_FALSE(state.appearance_at_defaults());

  auto* creator = state.layout("creator");
  creator->set_left_width(5);
  CHECK(creator->left_width() == WorkspaceLayout::kMinLeftWidth);
  creator->set_right_width(99999);
  CHECK(creator->right_width() == WorkspaceLayout::kMaxRightWidth);
  creator->set_bottom_height(-1);
  CHECK(creator->bottom_height() == WorkspaceLayout::kMinBottomHeight);
}

TEST_CASE("Zoom keys move along fixed steps and stay inside the range", "[workbench][shortcuts]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState state(ini_in(dir));
  state.zoomIn();
  CHECK(state.ui_scale() == 110);
  state.zoomIn();
  CHECK(state.ui_scale() == 125);
  for (int i = 0; i < 10; ++i) state.zoomIn();
  CHECK(state.ui_scale() == WorkbenchState::kMaxUiScale);
  state.zoomOut();
  CHECK(state.ui_scale() == 135);
  state.zoomReset();
  CHECK(state.ui_scale() == 100);
  for (int i = 0; i < 10; ++i) state.zoomOut();
  CHECK(state.ui_scale() == WorkbenchState::kMinUiScale);
  // A value set by a slider between two steps goes to the next step in the direction pressed.
  state.set_ui_scale(95);
  state.zoomIn();
  CHECK(state.ui_scale() == 100);
  state.set_ui_scale(95);
  state.zoomOut();
  CHECK(state.ui_scale() == 90);
}

TEST_CASE("Workbench restores appearance and per-workspace layouts after a restart", "[workbench][persistence]") {
  ensure_app();
  sns::test::TempDir dir;
  const auto ini = ini_in(dir);
  {
    WorkbenchState state(ini);
    state.set_ui_scale(125);
    state.set_font_scale(140);
    state.set_density(WorkbenchState::Compact);
    state.set_font_family("Consolas");
    state.setZoneFont("explorer", 130);
    state.setZoneFont("console", 90);
    state.set_workspace(2);
    auto* creator = state.layout("creator");
    creator->set_left_width(310);
    creator->set_right_width(520);
    creator->set_left_visible(false);
    creator->set_bottom_visible(true);
    creator->set_bottom_height(260);
    auto* forge = state.layout("forge");
    forge->set_maximized(true);
    forge->set_right_visible(false);
    state.flush();
  }
  WorkbenchState again(ini);
  CHECK(again.ui_scale() == 125);
  CHECK(again.font_scale() == 140);
  CHECK(again.density() == WorkbenchState::Compact);
  CHECK(again.font_family() == "Consolas");
  CHECK(again.zone_fonts().value("explorer").toInt() == 130);
  CHECK(again.zone_fonts().value("console").toInt() == 90);
  CHECK(again.zone_fonts().value("viewer").toInt() == 100);
  CHECK(again.workspace() == 2);
  auto* creator = again.layout("creator");
  CHECK(creator->left_width() == 310);
  CHECK(creator->right_width() == 520);
  CHECK_FALSE(creator->left_visible());
  CHECK(creator->right_visible());
  CHECK(creator->bottom_visible());
  CHECK(creator->bottom_height() == 260);
  CHECK_FALSE(creator->maximized());
  // Another workspace is untouched by what was done to the Creator, and keeps its own state.
  auto* outfit = again.layout("outfit");
  CHECK(outfit->left_visible());
  CHECK(outfit->left_width() == outfit->defaults().left_width);
  auto* forge = again.layout("forge");
  CHECK(forge->maximized());
  CHECK_FALSE(forge->right_visible());
}

TEST_CASE("Workbench saves by itself shortly after a change", "[workbench][persistence]") {
  ensure_app();
  sns::test::TempDir dir;
  const auto ini = ini_in(dir);
  WorkbenchState state(ini);
  state.layout("outfit")->set_right_width(333);
  // No flush: the debounce timer writes it.
  QThread::msleep(600);
  QCoreApplication::processEvents();
  WorkbenchState again(ini);
  CHECK(again.layout("outfit")->right_width() == 333);
}

TEST_CASE("Workbench ignores corrupt or out-of-range stored values", "[workbench][persistence]") {
  ensure_app();
  sns::test::TempDir dir;
  const auto ini = ini_in(dir);
  {
    QSettings raw(ini, QSettings::IniFormat);
    raw.setValue("workbench/uiScale", 9999);
    raw.setValue("workbench/fontScale", "not a number");
    raw.setValue("workbench/density", 77);
    raw.setValue("workbench/workspace", 40);
    raw.setValue("workbench/layout/creator/leftWidth", -50);
    raw.setValue("workbench/layout/creator/rightWidth", 12345678);
    raw.setValue("workbench/layout/creator/bottomHeight", 0);
    raw.setValue("workbench/userLayouts", "{ this is not json");
    raw.sync();
  }
  WorkbenchState state(ini);
  CHECK(state.ui_scale() == WorkbenchState::kMaxUiScale);
  CHECK(state.font_scale() == WorkbenchState::kMinFontScale);  // toInt() of garbage is 0, clamped up
  CHECK(state.density() == WorkbenchState::Comfortable);
  CHECK(state.workspace() == 2);
  auto* creator = state.layout("creator");
  CHECK(creator->left_width() == WorkspaceLayout::kMinLeftWidth);
  CHECK(creator->right_width() == WorkspaceLayout::kMaxRightWidth);
  CHECK(creator->bottom_height() == WorkspaceLayout::kMinBottomHeight);
  CHECK(state.saved_layouts().isEmpty());
}

TEST_CASE("Resetting the appearance leaves layouts alone and vice versa", "[workbench][reset]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState state(ini_in(dir));
  state.set_ui_scale(140);
  state.set_font_scale(150);
  state.set_density(WorkbenchState::Comfortable);
  state.set_font_family("Verdana");
  state.setZoneFont("viewer", 160);
  state.layout("creator")->set_left_width(500);
  state.layout("creator")->set_right_visible(false);

  state.resetAppearance();
  CHECK(state.appearance_at_defaults());
  CHECK(state.layout("creator")->left_width() == 500);
  CHECK_FALSE(state.layout("creator")->right_visible());

  state.set_ui_scale(120);
  state.resetLayouts();
  CHECK(state.ui_scale() == 120);
  CHECK(state.layout("creator")->left_width() == state.layout("creator")->defaults().left_width);
  CHECK(state.layout("creator")->right_visible());

  state.set_font_scale(90);
  state.layout("forge")->set_maximized(true);
  state.resetAll();
  CHECK(state.appearance_at_defaults());
  CHECK_FALSE(state.layout("forge")->maximized());
}

TEST_CASE("Built-in layouts apply to one workspace only", "[workbench][layouts]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState state(ini_in(dir));
  CHECK(state.builtin_layouts().size() == 6);
  CHECK(state.applyBuiltinLayout("face", "creator"));
  auto* creator = state.layout("creator");
  CHECK_FALSE(creator->left_visible());
  CHECK(creator->right_visible());
  CHECK(creator->right_width() == 480);
  // The other workspaces did not change.
  CHECK(state.layout("outfit")->left_visible());
  CHECK(state.layout("forge")->left_visible());

  CHECK(state.applyBuiltinLayout("viewer", "creator"));
  CHECK(creator->maximized());
  CHECK(state.applyBuiltinLayout("creator", "creator"));  // back to the workspace defaults
  CHECK(creator->values() == creator->defaults());
  CHECK_FALSE(state.applyBuiltinLayout("nope", "creator"));
  CHECK_FALSE(state.applyBuiltinLayout("face", "nope"));

  CHECK(state.applyBuiltinLayout("compact", "forge"));
  CHECK(state.layout("forge")->left_width() == 240);
  CHECK(state.layout("forge")->right_width() == 300);
}

TEST_CASE("User layouts are saved, applied, replaced and deleted", "[workbench][layouts]") {
  ensure_app();
  sns::test::TempDir dir;
  const auto ini = ini_in(dir);
  {
    WorkbenchState state(ini);
    state.layout("creator")->set_left_width(280);
    state.layout("outfit")->set_right_visible(false);
    CHECK(state.saveLayout("  Mi   diseño  "));  // whitespace is collapsed
    CHECK(state.saved_layouts() == QStringList{"Mi diseño"});
    CHECK_FALSE(state.saveLayout("   "));
    CHECK_FALSE(state.saveLayout("vestuario"));  // a built-in name is reserved, case-insensitive
    CHECK(state.saveLayout(QString(200, 'x')));
    CHECK(state.saved_layouts().size() == 2);

    state.resetLayouts();
    CHECK(state.layout("creator")->left_width() != 280);
    CHECK(state.applyLayout("Mi diseño"));
    CHECK(state.layout("creator")->left_width() == 280);
    CHECK_FALSE(state.layout("outfit")->right_visible());
    CHECK_FALSE(state.applyLayout("no existe"));

    // Same name in another case replaces the entry instead of adding one.
    state.layout("creator")->set_left_width(300);
    CHECK(state.saveLayout("MI DISEÑO"));
    CHECK(state.saved_layouts().size() == 2);
    state.flush();
  }
  WorkbenchState again(ini);
  CHECK(again.saved_layouts().contains("MI DISEÑO"));
  again.resetLayouts();
  CHECK(again.applyLayout("MI DISEÑO"));
  CHECK(again.layout("creator")->left_width() == 300);
  CHECK(again.deleteLayout("MI DISEÑO"));
  CHECK_FALSE(again.deleteLayout("MI DISEÑO"));
  CHECK(again.saved_layouts().size() == 1);
}

TEST_CASE("Workbench writes nothing outside its own settings group", "[workbench][isolation]") {
  ensure_app();
  sns::test::TempDir dir;
  const auto ini = ini_in(dir);
  {
    QSettings raw(ini, QSettings::IniFormat);
    raw.setValue("mo2/profile", "Default");  // what the studio keeps next to it
    raw.sync();
  }
  {
    WorkbenchState state(ini);
    state.set_ui_scale(130);
    state.layout("creator")->set_maximized(true);
    state.saveLayout("Prueba");
    state.flush();
  }
  QSettings raw(ini, QSettings::IniFormat);
  CHECK(raw.value("mo2/profile").toString() == "Default");
  for (const auto& key : raw.allKeys()) {
    INFO(key.toStdString());
    CHECK((key.startsWith("workbench/") || key == "mo2/profile"));
  }
}
