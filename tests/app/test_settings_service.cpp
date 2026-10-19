#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <cmath>
#include <limits>

#include "settings_service.hpp"
#include "temp_dir.hpp"
#include "workbench_state.hpp"

using sns::app::SettingsService;
using sns::app::WorkbenchState;

namespace {

void ensure_app() {
  static int argc = 1;
  static char name[] = "sns_settings_tests";
  static char* argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
}

QString ini_in(const sns::test::TempDir& dir) { return QString::fromStdWString((dir.path() / "settings.ini").wstring()); }
QString file_in(const sns::test::TempDir& dir, const char* name) { return QString::fromStdWString((dir.path() / name).wstring()); }

QStringList ids_of(const QVariantList& rows) {
  QStringList out;
  for (const auto& r : rows) out << r.toMap().value("id").toString();
  return out;
}

QStringList stored_keys(const QString& ini) {
  QSettings s(ini, QSettings::IniFormat);
  s.beginGroup(SettingsService::kGroup);
  auto keys = s.childKeys();
  keys.sort();
  return keys;
}

void write_json(const QString& path, const QJsonObject& o) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(o).toJson());
}

}  // namespace

TEST_CASE("Settings start at their defaults and write nothing until something changes", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  {
    SettingsService s(ini_in(dir));
    CHECK(s.theme() == "graphite");
    CHECK(s.accent() == "steel");
    CHECK(s.start_workspace() == "last");
    CHECK(s.confirm_unsaved_close());
    CHECK(s.npc_browser_view() == "list");
    CHECK(s.gallery_card_width() == 92);   // the original card width
    CHECK(s.search_debounce_ms() == 200);  // the original debounce
    CHECK(s.portrait_instant_cache());
    CHECK(s.camera_fov() == 45);
    CHECK(s.anti_aliasing() == 2);  // MSAA high, as the viewer had it
    CHECK(s.viewport_background() == QColor("#16171a"));
    CHECK(std::abs(s.orbit_speed() - 0.1) < 1e-9);  // OrbitCameraController's own default
    CHECK(s.changed_count() == 0);
    CHECK(s.load_issues().isEmpty());
    s.flush();
  }
  CHECK(stored_keys(ini_in(dir)).isEmpty());
}

TEST_CASE("Settings validate types and ranges and persist only non-default values", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  {
    SettingsService s(ini_in(dir));
    CHECK(s.setValue("appearance.theme", "deep").isEmpty());
    CHECK(s.setValue("viewport.cameraFov", 60).isEmpty());
    CHECK(s.setValue("viewport.invertOrbitY", true).isEmpty());
    CHECK(s.setValue("npc.searchDebounceMs", 300).isEmpty());
    // Refused, value unchanged.
    CHECK_FALSE(s.setValue("appearance.theme", "neon").isEmpty());
    CHECK_FALSE(s.setValue("viewport.cameraFov", 500).isEmpty());
    CHECK_FALSE(s.setValue("viewport.cameraFov", std::numeric_limits<double>::quiet_NaN()).isEmpty());
    CHECK_FALSE(s.setValue("viewport.cameraFov", 50.5).isEmpty());
    CHECK_FALSE(s.setValue("viewport.invertOrbitY", "maybe").isEmpty());
    CHECK_FALSE(s.setValue("viewport.invertOrbitY", 1).isEmpty());
    CHECK_FALSE(s.setValue("nope.setting", 1).isEmpty());
    CHECK_FALSE(s.setValue("workspace.panels", 1).isEmpty());  // a page, not a value
    CHECK(s.theme() == "deep");
    CHECK(s.camera_fov() == 60);
    CHECK(s.changed_count() == 4);
    // Back to the default: its key is removed.
    CHECK(s.setValue("npc.searchDebounceMs", 200).isEmpty());
    s.flush();
  }
  const auto keys = stored_keys(ini_in(dir));
  CHECK(keys.contains("appearance.theme"));
  CHECK(keys.contains("viewport.cameraFov"));
  CHECK_FALSE(keys.contains("npc.searchDebounceMs"));
  SettingsService again(ini_in(dir));
  CHECK(again.theme() == "deep");
  CHECK(again.camera_fov() == 60);
  CHECK(again.invert_orbit_y());
  CHECK(again.search_debounce_ms() == 200);
}

TEST_CASE("Corrupt, unknown and future settings never break the start", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  {
    QSettings raw(ini_in(dir), QSettings::IniFormat);
    raw.beginGroup(SettingsService::kGroup);
    raw.setValue("schemaVersion", 1);
    raw.setValue("appearance.theme", "plaid");
    raw.setValue("viewport.cameraFov", "abc");
    raw.setValue("viewport.lightIntensity", 9999);
    raw.setValue("viewport.invertOrbitX", "true");
    raw.setValue("legacy.thing", 3);
    raw.endGroup();
  }
  {
    SettingsService s(ini_in(dir));
    CHECK(s.theme() == "graphite");
    CHECK(s.camera_fov() == 45);
    CHECK(s.light_intensity() == 1.0);
    CHECK(s.invert_orbit_x());  // a valid INI boolean is read
    CHECK(s.load_issues().size() == 4);
    CHECK_FALSE(s.read_only());
  }
  sns::test::TempDir future;
  {
    QSettings raw(ini_in(future), QSettings::IniFormat);
    raw.setValue(SettingsService::kGroup + "/schemaVersion", 7);
    raw.setValue(SettingsService::kGroup + "/appearance.theme", "deep");
  }
  {
    SettingsService s(ini_in(future));
    CHECK(s.read_only());
    CHECK(s.theme() == "graphite");
    CHECK(s.setValue("appearance.theme", "steel").isEmpty());  // works for this session
    s.flush();
  }
  QSettings raw(ini_in(future), QSettings::IniFormat);
  CHECK(raw.value(SettingsService::kGroup + "/schemaVersion").toInt() == 7);  // the newer file is untouched
  CHECK(raw.value(SettingsService::kGroup + "/appearance.theme").toString() == "deep");
}

TEST_CASE("Settings search finds real settings by words, accents ignored", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  SettingsService s(ini_in(dir));
  CHECK(ids_of(s.search("escala")).contains("appearance.uiScale"));
  REQUIRE_FALSE(s.search("tema").isEmpty());
  CHECK(ids_of(s.search("tema")).first() == "appearance.theme");
  CHECK(ids_of(s.search("retrato")).contains("npc.portraitInstantCache"));
  CHECK(ids_of(s.search("retrato")).contains("npc.portraitCache"));
  CHECK(ids_of(s.search("camara")).contains("viewport.defaultCamera"));  // "cámara" without the accent
  CHECK(ids_of(s.search("CÁMARA rostro")).contains("viewport.defaultCamera"));
  CHECK(ids_of(s.search("mo2")).contains("mo2.status"));
  CHECK(ids_of(s.search("galería donante")) == QStringList{"npc.donorView"});
  // Nothing is listed for features that do not exist (physics, SMP): no inert switch to find.
  CHECK(s.search("SMP").isEmpty());
  CHECK(s.search("fisica").isEmpty());
  CHECK(s.search("   ").isEmpty());
  for (const auto& r : s.search("visor")) CHECK_FALSE(r.toMap().value("categoryTitle").toString().isEmpty());
}

TEST_CASE("Every listed setting names its handler, category and apply policy", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  SettingsService s(ini_in(dir));
  QStringList categories;
  for (const auto& c : s.categories()) categories << c.toMap().value("key").toString();
  QSet<QString> ids;
  for (const auto& r : s.registry()) {
    const auto d = r.toMap();
    INFO(d.value("id").toString().toStdString());
    CHECK_FALSE(ids.contains(d.value("id").toString()));
    ids.insert(d.value("id").toString());
    CHECK(categories.contains(d.value("category").toString()));
    CHECK_FALSE(d.value("handler").toString().isEmpty());
    CHECK(QStringList{"live", "confirm", "restart"}.contains(d.value("apply").toString()));
    CHECK(QStringList{"global", "workspace", "mo2-profile"}.contains(d.value("scope").toString()));
  }
  // Forbidden switches never exist.
  for (const auto* bad : {"export.disableSafePatch", "export.ignoreMasters", "export.forceFaceGen", "export.autoApproveAmbiguous",
                          "export.skipReadback", "outfit.previewPhysics"})
    CHECK_FALSE(ids.contains(QString::fromLatin1(bad)));
}

TEST_CASE("Workbench preferences are reached through WorkbenchState, never duplicated", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState wb(file_in(dir, "workbench.ini"));
  SettingsService s(ini_in(dir));
  s.set_workbench(&wb);
  CHECK(s.value("appearance.uiScale").toInt() == 100);
  CHECK(s.setValue("appearance.uiScale", 125).isEmpty());
  CHECK(wb.ui_scale() == 125);
  CHECK(s.setValue("appearance.density", "compact").isEmpty());
  CHECK(wb.density() == WorkbenchState::Compact);
  CHECK(s.setValue("appearance.fontFamily", "Consolas").isEmpty());
  CHECK(wb.font_family() == "Consolas");
  CHECK_FALSE(s.setValue("appearance.uiScale", 400).isEmpty());  // validated before WorkbenchState clamps
  CHECK(wb.ui_scale() == 125);
  wb.layout("creator")->set_left_width(500);
  s.setValue("appearance.theme", "steel");
  CHECK(s.changed_count() == 4);
  s.flush();
  CHECK(stored_keys(ini_in(dir)) == QStringList{"appearance.theme", "schemaVersion"});  // the scale stays in "workbench/"
  // "Reset interface": appearance back, layouts kept.
  s.resetInterface();
  CHECK(wb.ui_scale() == 100);
  CHECK(wb.density() == WorkbenchState::Normal);
  CHECK(s.theme() == "graphite");
  CHECK(wb.layout("creator")->left_width() == 500);
  // "Reset all": layouts too.
  s.resetAll();
  CHECK(wb.layout("creator")->left_width() == wb.layout("creator")->defaults().left_width);
}

TEST_CASE("Theme preview reverts on demand and is stored only when kept", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState wb(file_in(dir, "workbench.ini"));
  SettingsService s(ini_in(dir));
  s.set_workbench(&wb);
  s.beginPreview();
  CHECK(s.preview_active());
  CHECK(s.preview_seconds_left() == SettingsService::kPreviewSeconds);
  s.setValue("appearance.theme", "contrast");
  s.setValue("appearance.uiScale", 150);
  s.flush();
  CHECK_FALSE(stored_keys(ini_in(dir)).contains("appearance.theme"));  // not confirmed yet
  s.revertPreview();
  CHECK_FALSE(s.preview_active());
  CHECK(s.theme() == "graphite");
  CHECK(wb.ui_scale() == 100);
  s.beginPreview();
  s.setValue("appearance.accent", "amber");
  s.keepPreview();
  CHECK(s.accent() == "amber");
  QSettings raw(ini_in(dir), QSettings::IniFormat);
  CHECK(raw.value(SettingsService::kGroup + "/appearance.accent").toString() == "amber");
}

TEST_CASE("Settings export and import round-trip and refuse bad documents untouched", "[settings]") {
  ensure_app();
  sns::test::TempDir dir;
  WorkbenchState wb(file_in(dir, "workbench.ini"));
  SettingsService s(ini_in(dir));
  s.set_workbench(&wb);
  s.setValue("appearance.theme", "steel");
  s.setValue("viewport.cameraFov", 35);
  wb.set_ui_scale(110);
  wb.layout("outfit")->set_right_width(333);
  const auto path = file_in(dir, "export.json");
  REQUIRE(s.exportTo(QUrl::fromLocalFile(path)).isEmpty());
  QFile f(path);
  REQUIRE(f.open(QIODevice::ReadOnly));
  const auto text = QString::fromUtf8(f.readAll());
  f.close();
  CHECK_FALSE(text.contains(":\\"));  // no absolute Windows paths
  CHECK_FALSE(text.contains(":/"));

  sns::test::TempDir other;
  WorkbenchState wb2(file_in(other, "workbench.ini"));
  SettingsService s2(ini_in(other));
  s2.set_workbench(&wb2);
  const auto preview = s2.previewImport(QUrl::fromLocalFile(path));
  CHECK(preview.value("ok").toBool());
  CHECK(preview.value("changes").toList().size() >= 3);
  CHECK(s2.theme() == "graphite");  // a preview applies nothing
  const auto applied = s2.applyImport(QUrl::fromLocalFile(path));
  CHECK(applied.value("ok").toBool());
  CHECK(s2.theme() == "steel");
  CHECK(s2.camera_fov() == 35);
  CHECK(wb2.ui_scale() == 110);
  CHECK(wb2.layout("outfit")->right_width() == 333);

  // Bad documents: refused, nothing changes.
  const auto doc = QJsonDocument::fromJson(text.toUtf8()).object();
  const auto try_doc = [&](const QJsonObject& o, const char* name) {
    INFO(name);
    const auto p = file_in(other, name);
    write_json(p, o);
    const auto r = s2.applyImport(QUrl::fromLocalFile(p));
    CHECK_FALSE(r.value("ok").toBool());
    CHECK_FALSE(r.value("error").toString().isEmpty());
    CHECK(s2.theme() == "steel");
    CHECK(s2.camera_fov() == 35);
  };
  auto future = doc;
  future["schemaVersion"] = 99;
  try_doc(future, "future.json");
  auto secret = doc;
  auto values = secret["settings"].toObject();
  values["appearance.theme"] = "deep";
  secret["settings"] = values;
  secret["apiToken"] = "abc";
  try_doc(secret, "secret.json");
  auto invalid = doc;
  values = invalid["settings"].toObject();
  values["appearance.theme"] = "deep";  // valid...
  values["viewport.cameraFov"] = 999;   // ...but this one is not: nothing at all is applied
  invalid["settings"] = values;
  try_doc(invalid, "invalid.json");
  auto unknown = doc;
  values = unknown["settings"].toObject();
  values["export.disableSafePatch"] = true;
  unknown["settings"] = values;
  try_doc(unknown, "unknown.json");
  try_doc(QJsonObject{{"format", "something-else"}, {"schemaVersion", 1}}, "format.json");
  const auto garbage = file_in(other, "garbage.json");
  QFile g(garbage);
  REQUIRE(g.open(QIODevice::WriteOnly));
  g.write("{not json");
  g.close();
  CHECK_FALSE(s2.applyImport(QUrl::fromLocalFile(garbage)).value("ok").toBool());
}

TEST_CASE("Every theme and accent keeps text readable", "[settings][contrast]") {
  for (const auto& theme : SettingsService::theme_ids()) {
    for (const auto& accent : SettingsService::accent_ids()) {
      INFO(theme.toStdString() << " / " << accent.toStdString());
      const auto p = SettingsService::palette_for(theme, accent);
      const auto c = [&](const char* k) { return QColor(p.value(QLatin1String(k)).toString()); };
      const double body = theme == "contrast" ? 7.0 : 4.5;
      for (const auto* surface : {"bg", "surface", "panel", "panelAlt", "control", "header", "panelHeader"}) {
        CHECK(SettingsService::contrastRatio(c("text"), c(surface)) >= body);
      }
      CHECK(SettingsService::contrastRatio(c("textDim"), c("panel")) >= body);
      CHECK(SettingsService::contrastRatio(c("textDim"), c("panelAlt")) >= 4.5);
      // The accent is a UI component against the panels (WCAG 1.4.11: 3:1).
      CHECK(SettingsService::contrastRatio(c("accent"), c("panel")) >= 3.0);
      // Text on the accent: 4.5:1, except the original graphite + steel pair (4.0:1, kept as the studio's identity).
      const double on = theme == "graphite" && accent == "steel" ? 4.0 : 4.5;
      CHECK(SettingsService::contrastRatio(c("onAccent"), c("accent")) >= on);
      // The Qt Quick Controls palette (menus, fields, buttons, placeholders).
      CHECK(SettingsService::contrastRatio(c("qtText"), c("qtBase")) >= body);
      CHECK(SettingsService::contrastRatio(c("qtText"), c("qtWindow")) >= body);
      CHECK(SettingsService::contrastRatio(c("qtText"), c("qtButton")) >= body);
      CHECK(SettingsService::contrastRatio(c("qtPlaceholder"), c("qtBase")) >= 4.5);
      CHECK(SettingsService::contrastRatio(c("qtHighlight"), c("qtBase")) >= 3.0);
      // Semantic colors are not replaced by the accent.
      CHECK(SettingsService::contrastRatio(c("error"), c("panel")) >= 3.0);
      CHECK(SettingsService::contrastRatio(c("warn"), c("panel")) >= 3.0);
    }
  }
  // The original palette stays value for value.
  const auto original = SettingsService::palette_for("graphite", "steel");
  CHECK(original.value("bg").toString() == "#16171a");
  CHECK(original.value("accent").toString() == "#4382c4");
  CHECK(original.value("selection").toString() == "#2f4f72");
  CHECK(original.value("qtWindow").toString() == "#1f2227");  // main.cpp dark_palette()
  CHECK(original.value("qtHighlight").toString() == "#4f8cc9");
}
