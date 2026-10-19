#pragma once
#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <vector>

#include "workbench_state.hpp"  // complete type: a Q_PROPERTY of the singleton (moc)

namespace sns::app {

// Metadata of one setting of the Settings Center. Only settings whose effect is implemented are listed: the
// catalogue drives the search, the "changed" count, reset, export and import, and every entry names the code
// that applies it (`handler`).
struct SettingDescriptor {
  QString id;           // stable, e.g. "appearance.theme"
  QString category;     // key of SettingsService::categories()
  QString title;
  QString description;
  QString keywords;     // extra search words (synonyms, English names)
  QString type;         // "bool" | "int" | "enum" | "font" | "panel" (a page widget, no single value)
  QString scope;        // "global" | "workspace" | "mo2-profile"
  QString apply;        // "live" (applied at once) | "confirm" (kept or reverted) | "restart" (next start)
  QString backend;      // "app" (this service) | "workbench" (WorkbenchState) | "panel" (a page widget)
  QString handler;      // where the effect is implemented
  QString unit;
  QVariant def;
  int min = 0, max = 0, step = 1;
  QVariantList options;  // enum: [{value, label}]
};

// Settings of the Settings Center that WorkbenchState does not own: theme and accent, startup workspace, NPC
// browser and portrait options, 3D viewer options. Interface scale, text size, density, font family, the font
// of each zone and the panel layouts stay in WorkbenchState (one source of truth); this service only describes
// them for the search and exports/imports them through it.
//
// Storage: QSettings group "appSettings/v1" (schemaVersion 1). Only values that differ from their default
// are written; a default value removes its key. A stored value that is not valid (wrong type, out of range,
// unknown option) is ignored at load with a reason in `loadIssues`. A file of a newer schema is never
// overwritten (read-only session).
class SettingsService : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(AppSettings)
  QML_SINGLETON
  // Appearance
  Q_PROPERTY(QString theme READ theme WRITE set_theme NOTIFY themeChanged)
  Q_PROPERTY(QString accent READ accent WRITE set_accent NOTIFY themeChanged)
  Q_PROPERTY(QVariantMap palette READ palette NOTIFY themeChanged)
  // General
  Q_PROPERTY(QString startWorkspace READ start_workspace NOTIFY valuesChanged)
  Q_PROPERTY(bool confirmUnsavedClose READ confirm_unsaved_close NOTIFY valuesChanged)
  // NPC browser and portraits
  Q_PROPERTY(QString npcBrowserView READ npc_browser_view WRITE set_npc_browser_view NOTIFY valuesChanged)
  Q_PROPERTY(int galleryCardWidth READ gallery_card_width NOTIFY valuesChanged)
  Q_PROPERTY(int searchDebounceMs READ search_debounce_ms NOTIFY valuesChanged)
  Q_PROPERTY(bool portraitInstantCache READ portrait_instant_cache NOTIFY valuesChanged)
  Q_PROPERTY(int portraitPrefetchRows READ portrait_prefetch_rows NOTIFY valuesChanged)
  Q_PROPERTY(QString donorView READ donor_view WRITE set_donor_view NOTIFY valuesChanged)
  // 3D viewer
  Q_PROPERTY(QColor viewportBackground READ viewport_background NOTIFY valuesChanged)
  Q_PROPERTY(int antiAliasing READ anti_aliasing NOTIFY valuesChanged)  // 0 off, 1 medium, 2 high, 3 very high
  Q_PROPERTY(int cameraFov READ camera_fov NOTIFY valuesChanged)
  Q_PROPERTY(double orbitSpeed READ orbit_speed NOTIFY valuesChanged)  // OrbitCameraController xSpeed/ySpeed
  Q_PROPERTY(bool invertOrbitX READ invert_orbit_x NOTIFY valuesChanged)
  Q_PROPERTY(bool invertOrbitY READ invert_orbit_y NOTIFY valuesChanged)
  Q_PROPERTY(bool autoFitOnNpc READ auto_fit_on_npc NOTIFY valuesChanged)
  Q_PROPERTY(int defaultCameraPreset READ default_camera_preset NOTIFY valuesChanged)  // CharacterViewport preset index
  Q_PROPERTY(double lightIntensity READ light_intensity NOTIFY valuesChanged)          // factor on the viewer lights
  // Catalogue and state
  Q_PROPERTY(QVariantList categories READ categories CONSTANT)
  Q_PROPERTY(QVariantList registry READ registry CONSTANT)
  Q_PROPERTY(sns::app::WorkbenchState* workbench READ workbench WRITE set_workbench NOTIFY workbenchChanged)
  Q_PROPERTY(int revision READ revision NOTIFY valuesChanged)
  Q_PROPERTY(int changedCount READ changed_count NOTIFY valuesChanged)
  Q_PROPERTY(QStringList loadIssues READ load_issues CONSTANT)
  Q_PROPERTY(bool readOnly READ read_only CONSTANT)
  Q_PROPERTY(bool previewActive READ preview_active NOTIFY previewChanged)
  Q_PROPERTY(int previewSecondsLeft READ preview_seconds_left NOTIFY previewChanged)

 public:
  static constexpr int kSchemaVersion = 1;
  static constexpr int kPreviewSeconds = 15;
  static inline const QString kGroup = QStringLiteral("appSettings/v1");
  static inline const QString kExportFormat = QStringLiteral("skyrim-npc-studio-settings");

  // QML creates the singleton: the user's settings (or the INI folder of SNS_SETTINGS_DIR).
  explicit SettingsService(QObject* parent = nullptr);
  // Tests and tools: settings in this INI file.
  explicit SettingsService(const QString& ini_path, QObject* parent = nullptr);
  ~SettingsService() override;

  // Values (typed views over the validated store).
  QString theme() const { return string_of("appearance.theme"); }
  QString accent() const { return string_of("appearance.accent"); }
  void set_theme(const QString& v) { setValue("appearance.theme", v); }
  void set_accent(const QString& v) { setValue("appearance.accent", v); }
  QVariantMap palette() const { return palette_for(theme(), accent()); }
  QString start_workspace() const { return string_of("general.startWorkspace"); }
  bool confirm_unsaved_close() const { return bool_of("general.confirmUnsavedClose"); }
  QString npc_browser_view() const { return string_of("npc.browserDefaultView"); }
  void set_npc_browser_view(const QString& v) { setValue("npc.browserDefaultView", v); }
  int gallery_card_width() const;
  int search_debounce_ms() const { return int_of("npc.searchDebounceMs"); }
  bool portrait_instant_cache() const { return bool_of("npc.portraitInstantCache"); }
  int portrait_prefetch_rows() const { return int_of("performance.portraitPrefetchRows"); }
  QString donor_view() const { return string_of("npc.donorView"); }
  void set_donor_view(const QString& v) { setValue("npc.donorView", v); }
  QColor viewport_background() const;
  int anti_aliasing() const;
  int camera_fov() const { return int_of("viewport.cameraFov"); }
  double orbit_speed() const { return 0.1 * int_of("viewport.orbitSensitivity") / 100.0; }
  bool invert_orbit_x() const { return bool_of("viewport.invertOrbitX"); }
  bool invert_orbit_y() const { return bool_of("viewport.invertOrbitY"); }
  bool auto_fit_on_npc() const { return bool_of("viewport.autoFitOnNpc"); }
  int default_camera_preset() const;
  double light_intensity() const { return int_of("viewport.lightIntensity") / 100.0; }

  QVariantList categories() const;
  QVariantList registry() const;
  WorkbenchState* workbench() const { return workbench_; }
  void set_workbench(WorkbenchState* workbench);
  int revision() const { return revision_; }
  int changed_count() const;
  QStringList load_issues() const { return load_issues_; }
  bool read_only() const { return read_only_; }
  bool preview_active() const { return preview_.has_value(); }
  int preview_seconds_left() const { return preview_seconds_left_; }

  // Current value of any listed setting (app or workbench); invalid QVariant for an unknown id or a panel.
  Q_INVOKABLE QVariant value(const QString& id) const;
  // Validates and applies. Returns "" or why the value was refused (the current value stays).
  Q_INVOKABLE QString setValue(const QString& id, const QVariant& value);
  Q_INVOKABLE bool isDefault(const QString& id) const;
  Q_INVOKABLE QVariant defaultValue(const QString& id) const;
  Q_INVOKABLE QVariantMap descriptor(const QString& id) const;
  Q_INVOKABLE QString valueText(const QString& id) const;  // the value as the user reads it (option label, unit)
  Q_INVOKABLE void resetSetting(const QString& id);
  // Every setting of the category back to its default (layouts are reset by the Workspace page itself).
  Q_INVOKABLE void resetCategory(const QString& category);
  // Theme, accent and the WorkbenchState appearance (scale, text, density, fonts). Layouts and projects stay.
  Q_INVOKABLE void resetInterface();
  // Every setting of this catalogue and the workbench appearance and layouts. Projects, mods, caches and
  // evidence are not touched.
  Q_INVOKABLE void resetAll();

  // Settings matching all the words of `text` (accents and case ignored) in their title, description, keywords
  // or category: [{id, category, categoryTitle, title, description, match}] best first. Empty text: none.
  Q_INVOKABLE QVariantList search(const QString& text) const;
  Q_INVOKABLE QVariantList settingsOf(const QString& category) const;
  // Settings whose value is not the default: [{id, title, categoryTitle, current, default}].
  Q_INVOKABLE QVariantList diffFromDefaults() const;

  // Theme / scale preview: snapshot now; changes apply live; keep or revert (automatic revert after
  // kPreviewSeconds so an unreadable choice never sticks).
  Q_INVOKABLE void beginPreview();
  Q_INVOKABLE void keepPreview();
  Q_INVOKABLE void revertPreview();

  // JSON document with every exportable setting (no paths, no secrets). Returns "" or the error.
  Q_INVOKABLE QString exportTo(const QUrl& file) const;
  QJsonObject export_document() const;
  // Reads and validates a whole document without applying it: {ok, error, changes: [{id, title, from, to}]}.
  Q_INVOKABLE QVariantMap previewImport(const QUrl& file) const;
  // Applies a document after validating all of it; on any failure nothing changes: {ok, error, applied}.
  Q_INVOKABLE QVariantMap applyImport(const QUrl& file);
  QVariantMap preview_document(const QJsonObject& doc) const;
  QVariantMap apply_document(const QJsonObject& doc);

  // Detected facts for the System page (Qt, build, OS, CPU architecture). Nothing is inferred.
  Q_INVOKABLE QVariantMap systemInfo() const;
  Q_INVOKABLE void flush();

  // Palettes (also used by the tests: contrast of every theme and accent).
  static QStringList theme_ids();
  static QStringList accent_ids();
  static QVariantMap palette_for(const QString& theme, const QString& accent);
  // WCAG 2.x contrast ratio of two opaque colors (1..21).
  Q_INVOKABLE static double contrastRatio(const QColor& a, const QColor& b);
  // Lower case, without accents: what the search compares.
  static QString fold(const QString& text);

 signals:
  void themeChanged();
  void valuesChanged();
  void workbenchChanged();
  void previewChanged();
  void settingChanged(const QString& id);

 private:
  void init();
  void load();
  void save();
  void schedule_save();
  const SettingDescriptor* find(const QString& id) const;
  // The value converted to the descriptor's type when it is valid; nullopt and `error` otherwise.
  std::optional<QVariant> validate(const SettingDescriptor& d, const QVariant& value, QString* error) const;
  QString string_of(const QString& id) const;
  int int_of(const QString& id) const;
  bool bool_of(const QString& id) const;
  void set_internal(const SettingDescriptor& d, const QVariant& value);
  QVariant workbench_value(const QString& id) const;
  QString set_workbench_value(const QString& id, const QVariant& value);
  QString category_title(const QString& key) const;
  static std::vector<SettingDescriptor> build_registry();

  std::unique_ptr<QSettings> settings_;
  std::vector<SettingDescriptor> registry_;
  QVariantMap values_;  // id -> validated value (app backend only)
  QPointer<WorkbenchState> workbench_;
  QTimer save_timer_;
  bool loading_ = false;
  bool read_only_ = false;
  QStringList load_issues_;
  int revision_ = 0;
  struct Preview {
    QString theme, accent;
    int ui_scale = 100, font_scale = 100, density = 1;
    QString font_family;
  };
  std::optional<Preview> preview_;
  QTimer preview_timer_;
  int preview_seconds_left_ = 0;
};

}  // namespace sns::app
