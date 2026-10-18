#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QtQuickControls2/qquickattachedpropertypropagator.h>

namespace sns::app {

// Font sizes of one section of the window. `Zone.scale` set on an item (a panel, the window) is
// inherited by every item, delegate and popup below it, the way Material.theme is; text then asks for
// `font.pixelSize: Zone.small` and gets the base size of that step times the scale of its section.
//
//   ApplicationWindow { Zone.scale: Theme.fontFactor; Zone.density: Theme.densityFactor }
//   Pane { Zone.scale: Theme.fontFactor * Workbench.zoneFonts.inspector / 100 }   // one section
//   Label { font.pixelSize: Zone.small }                                        // 11 px at 100 %
//   Button { implicitHeight: Math.round(28 * Zone.unit) }                       // grows with the text
//
// The sizes are logical pixels: Qt already applies the Windows display scale, so it is never applied
// here a second time.
class ZoneAttached : public QQuickAttachedPropertyPropagator {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(qreal scale READ scale WRITE set_scale RESET reset_scale NOTIFY scaleChanged FINAL)
  Q_PROPERTY(qreal density READ density WRITE set_density RESET reset_density NOTIFY densityChanged FINAL)
  Q_PROPERTY(qreal unit READ unit NOTIFY unitChanged FINAL)
  Q_PROPERTY(int tiny READ tiny NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int micro READ micro NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int small READ small NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int normal READ normal NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int medium READ medium NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int large READ large NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int title READ title NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int heading READ heading NOTIFY scaleChanged FINAL)
  Q_PROPERTY(int display READ display NOTIFY scaleChanged FINAL)

 public:
  // Base sizes at 100 % (the steps the QML used as literals before). Theme.qml uses the same values.
  static constexpr int kTiny = 9, kMicro = 10, kSmall = 11, kNormal = 12, kMedium = 13, kLarge = 14, kTitle = 16,
                       kHeading = 18, kDisplay = 22;
  static constexpr int kMinPixelSize = 7;  // below this text stops being readable at any DPI

  explicit ZoneAttached(QObject* parent = nullptr);

  qreal scale() const { return scale_; }
  void set_scale(qreal scale);
  void reset_scale();
  qreal density() const { return density_; }
  void set_density(qreal density);
  void reset_density();
  qreal unit() const { return scale_ * density_; }

  // round(base * scale), never under kMinPixelSize.
  static int px(int base, qreal scale);
  int tiny() const { return px(kTiny, scale_); }
  int micro() const { return px(kMicro, scale_); }
  int small() const { return px(kSmall, scale_); }
  int normal() const { return px(kNormal, scale_); }
  int medium() const { return px(kMedium, scale_); }
  int large() const { return px(kLarge, scale_); }
  int title() const { return px(kTitle, scale_); }
  int heading() const { return px(kHeading, scale_); }
  int display() const { return px(kDisplay, scale_); }

 signals:
  void scaleChanged();
  void densityChanged();
  void unitChanged();

 protected:
  void attachedParentChange(QQuickAttachedPropertyPropagator* new_parent, QQuickAttachedPropertyPropagator* old_parent) override;

 private:
  void inherit_scale(qreal scale);
  void inherit_density(qreal density);
  void apply_scale(qreal scale);
  void apply_density(qreal density);

  qreal scale_ = 1.0;
  qreal density_ = 1.0;
  bool explicit_scale_ = false;
  bool explicit_density_ = false;
};

// QML name of the attached type: `Zone.scale`, `Zone.small`, ...
class Zone : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Zone is only used as an attached property (Zone.scale, Zone.small, ...)")
  QML_ATTACHED(ZoneAttached)

 public:
  static ZoneAttached* qmlAttachedProperties(QObject* object) { return new ZoneAttached(object); }
};

}  // namespace sns::app
