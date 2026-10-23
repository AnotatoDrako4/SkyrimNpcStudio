#pragma once
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace sns::app {

// Which panels of one workspace are shown and how big they are. Sizes are in "design pixels", i.e. at a
// 100 % interface scale: the QML side multiplies them by the scale, so changing the scale keeps the
// proportions of the layout the user arranged.
struct LayoutValues {
  bool left_visible = true;
  bool right_visible = true;
  bool bottom_visible = false;
  bool maximized = false;  // the viewer alone: side panels and diagnostics are put away
  int left_width = 340;
  int right_width = 400;
  int bottom_height = 200;
  bool operator==(const LayoutValues&) const = default;
};

// The panels of one workspace (Character Creator, Outfit Studio, Character Forge). One object per
// workspace, so each workspace keeps its own sizes and visibility. It never touches NPC or project (.snps)
// data: it only describes the window.
class WorkspaceLayout : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by WorkbenchState")
  Q_PROPERTY(QString id READ id CONSTANT)
  Q_PROPERTY(bool leftVisible READ left_visible WRITE set_left_visible NOTIFY leftVisibleChanged)
  Q_PROPERTY(bool rightVisible READ right_visible WRITE set_right_visible NOTIFY rightVisibleChanged)
  Q_PROPERTY(bool bottomVisible READ bottom_visible WRITE set_bottom_visible NOTIFY bottomVisibleChanged)
  Q_PROPERTY(bool maximized READ maximized WRITE set_maximized NOTIFY maximizedChanged)
  Q_PROPERTY(int leftWidth READ left_width WRITE set_left_width NOTIFY leftWidthChanged)
  Q_PROPERTY(int rightWidth READ right_width WRITE set_right_width NOTIFY rightWidthChanged)
  Q_PROPERTY(int bottomHeight READ bottom_height WRITE set_bottom_height NOTIFY bottomHeightChanged)
  // The sizes this workspace starts with: a double click on a divider puts one side back to them.
  Q_PROPERTY(int defaultLeftWidth READ default_left_width CONSTANT)
  Q_PROPERTY(int defaultRightWidth READ default_right_width CONSTANT)
  Q_PROPERTY(int defaultBottomHeight READ default_bottom_height CONSTANT)
  // A side panel the user wants shown but that the window has no room for: the workspace hides it on screen
  // only. Never stored and never part of a saved layout; the QML side sets it, the View menu and header read it.
  Q_PROPERTY(bool leftAutoHidden READ left_auto_hidden WRITE set_left_auto_hidden NOTIFY autoHiddenChanged)
  Q_PROPERTY(bool rightAutoHidden READ right_auto_hidden WRITE set_right_auto_hidden NOTIFY autoHiddenChanged)

 public:
  static constexpr int kMinLeftWidth = 160, kMaxLeftWidth = 900;
  static constexpr int kMinRightWidth = 200, kMaxRightWidth = 900;
  static constexpr int kMinBottomHeight = 80, kMaxBottomHeight = 700;

  WorkspaceLayout(QString id, LayoutValues defaults, QObject* parent = nullptr);

  QString id() const { return id_; }
  const LayoutValues& values() const { return v_; }
  const LayoutValues& defaults() const { return defaults_; }
  bool left_visible() const { return v_.left_visible; }
  bool right_visible() const { return v_.right_visible; }
  bool bottom_visible() const { return v_.bottom_visible; }
  bool maximized() const { return v_.maximized; }
  int left_width() const { return v_.left_width; }
  int right_width() const { return v_.right_width; }
  int bottom_height() const { return v_.bottom_height; }
  int default_left_width() const { return defaults_.left_width; }
  int default_right_width() const { return defaults_.right_width; }
  int default_bottom_height() const { return defaults_.bottom_height; }
  bool left_auto_hidden() const { return left_auto_hidden_; }
  bool right_auto_hidden() const { return right_auto_hidden_; }
  void set_left_auto_hidden(bool v) {
    if (left_auto_hidden_ == v) return;
    left_auto_hidden_ = v;
    emit autoHiddenChanged();
  }
  void set_right_auto_hidden(bool v) {
    if (right_auto_hidden_ == v) return;
    right_auto_hidden_ = v;
    emit autoHiddenChanged();
  }
  void set_left_visible(bool v);
  void set_right_visible(bool v);
  void set_bottom_visible(bool v);
  void set_maximized(bool v);
  void set_left_width(int v);
  void set_right_width(int v);
  void set_bottom_height(int v);

  // Replaces everything at once (restoring, applying a saved layout); sizes are clamped.
  void set_values(const LayoutValues& values);
  static LayoutValues sanitized(LayoutValues values);

  Q_INVOKABLE void reset() { set_values(defaults_); }
  // Sizes back to the defaults; which panels show stays as the user chose ("Restablecer paneles").
  Q_INVOKABLE void resetSizes() {
    set_left_width(defaults_.left_width);
    set_right_width(defaults_.right_width);
    set_bottom_height(defaults_.bottom_height);
  }

 signals:
  void leftVisibleChanged();
  void rightVisibleChanged();
  void bottomVisibleChanged();
  void maximizedChanged();
  void leftWidthChanged();
  void rightWidthChanged();
  void bottomHeightChanged();
  void changed();  // any of the above: what the persistence listens to
  void autoHiddenChanged();  // transient: not persisted, so it does not emit changed()

 private:
  QString id_;
  LayoutValues defaults_;
  LayoutValues v_;
  bool left_auto_hidden_ = false;
  bool right_auto_hidden_ = false;
};

// Window preferences of the workbench: interface scale, font scale and family, density, the font size of
// each section and the layout of every workspace. Kept apart from the studio controller on purpose:
// nothing here depends on the NPC being edited or on any .snps / .sncf project.
class WorkbenchState : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Workbench)
  QML_SINGLETON
  Q_PROPERTY(int uiScale READ ui_scale WRITE set_ui_scale NOTIFY uiScaleChanged)
  Q_PROPERTY(int fontScale READ font_scale WRITE set_font_scale NOTIFY fontScaleChanged)
  Q_PROPERTY(int density READ density WRITE set_density NOTIFY densityChanged)
  Q_PROPERTY(QString fontFamily READ font_family WRITE set_font_family NOTIFY fontFamilyChanged)
  Q_PROPERTY(QVariantMap zoneFonts READ zone_fonts NOTIFY zoneFontsChanged)
  Q_PROPERTY(int workspace READ workspace WRITE set_workspace NOTIFY workspaceChanged)
  Q_PROPERTY(QStringList savedLayouts READ saved_layouts NOTIFY savedLayoutsChanged)
  Q_PROPERTY(QVariantList builtinLayouts READ builtin_layouts CONSTANT)
  Q_PROPERTY(bool appearanceAtDefaults READ appearance_at_defaults NOTIFY appearanceChanged)
  // True while the scale preferences would push body text below its legible minimum: the text is held at
  // that minimum instead (the preference itself is kept), and the window says so.
  Q_PROPERTY(bool legibilityFloorActive READ legibility_floor_active NOTIFY appearanceChanged)
  // "comfortable", "balanced", "compact" when the preferences match one of the readability presets, else "custom".
  Q_PROPERTY(QString readabilityPreset READ readability_preset NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList readabilityPresets READ readability_presets CONSTANT)

 public:
  enum Density { Compact = 0, Normal = 1, Comfortable = 2 };
  Q_ENUM(Density)

  // Pixel size of a text whose base size is `base` px at 100 %, under an interface and a text scale. It
  // never goes below the legible minimum of the role (body text 11 px, secondary 10 px, captions 9 px,
  // titles 12 px): 85 % x 80 % no longer turns the catalog into 8 px text. Theme.fs() and the
  // workbench check both use this, so there is one formula.
  static int legibleFontPixels(int base, int ui_scale, int font_scale);
  static int legibleMinimum(int base);
  Q_INVOKABLE int fontPixels(int base, int ui_scale, int font_scale) const { return legibleFontPixels(base, ui_scale, font_scale); }

  static constexpr int kMinUiScale = 85, kMaxUiScale = 150;
  static constexpr int kMinFontScale = 80, kMaxFontScale = 160;
  static constexpr int kMinZoneFont = 80, kMaxZoneFont = 200;
  static inline const QStringList kZones = {"explorer", "viewer", "inspector", "console"};
  static inline const QStringList kWorkspaces = {"creator", "outfit", "forge"};

  // QML creates the singleton: the user's own settings (or the INI folder of SNS_SETTINGS_DIR).
  explicit WorkbenchState(QObject* parent = nullptr);
  // Tests and tools: preferences in this INI file.
  explicit WorkbenchState(const QString& ini_path, QObject* parent = nullptr);
  ~WorkbenchState() override;

  int ui_scale() const { return ui_scale_; }
  int font_scale() const { return font_scale_; }
  int density() const { return density_; }
  QString font_family() const { return font_family_; }
  QVariantMap zone_fonts() const;
  int workspace() const { return workspace_; }
  QStringList saved_layouts() const;
  QVariantList builtin_layouts() const;
  bool appearance_at_defaults() const;
  bool legibility_floor_active() const;
  QString readability_preset() const;
  QVariantList readability_presets() const;
  // Scale, text and density of one readability preset, applied together (the user's own action only).
  Q_INVOKABLE bool applyReadabilityPreset(const QString& key);
  void set_ui_scale(int percent);
  void set_font_scale(int percent);
  void set_density(int density);
  void set_font_family(const QString& family);
  void set_workspace(int index);
  int zone_font(const QString& zone) const;

  // Layout object of a workspace ("creator", "outfit", "forge"); null for an unknown id.
  Q_INVOKABLE sns::app::WorkspaceLayout* layout(const QString& workspace) const;
  Q_INVOKABLE void setZoneFont(const QString& zone, int percent);

  // Ctrl++ / Ctrl+- / Ctrl+0: the interface scale moves along fixed steps.
  Q_INVOKABLE void zoomIn();
  Q_INVOKABLE void zoomOut();
  Q_INVOKABLE void zoomReset();
  // Back to 100 % scale, normal density, default font. The layouts stay as they are.
  Q_INVOKABLE void resetAppearance();
  // Every workspace back to its default panels. The appearance stays as it is.
  Q_INVOKABLE void resetLayouts();
  Q_INVOKABLE void resetAll();

  // Named layouts: a built-in one applies to one workspace, a user one to all three.
  Q_INVOKABLE bool applyBuiltinLayout(const QString& key, const QString& workspace);
  Q_INVOKABLE bool saveLayout(const QString& name);
  Q_INVOKABLE bool applyLayout(const QString& name);
  Q_INVOKABLE bool deleteLayout(const QString& name);

  // Writes pending changes now (otherwise a short timer does).
  Q_INVOKABLE void flush();

 signals:
  void uiScaleChanged();
  void fontScaleChanged();
  void densityChanged();
  void fontFamilyChanged();
  void zoneFontsChanged();
  void workspaceChanged();
  void savedLayoutsChanged();
  void appearanceChanged();  // scale, font, density or a zone font: anything "reset" would undo

 private:
  void init();
  void load();
  void save();
  void schedule_save();
  QVariantMap read_user_layouts() const;
  void write_user_layouts(const QVariantMap& layouts);

  std::unique_ptr<QSettings> settings_;
  QTimer save_timer_;
  bool loading_ = false;
  int ui_scale_ = 100;
  int font_scale_ = 100;
  int density_ = Normal;
  QString font_family_;
  QMap<QString, int> zone_fonts_;
  int workspace_ = 0;
  QMap<QString, WorkspaceLayout*> layouts_;
};

}  // namespace sns::app
