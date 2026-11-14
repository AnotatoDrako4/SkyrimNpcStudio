#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>
#include <vector>

namespace sns::app {

// ForgeSceneViewModel.parts as a list model with one role, "modelData" (the part's map). List delegates keep their
// `required property var modelData` and are updated in place when a part changes, instead of the whole list being
// converted to JavaScript and every delegate rebuilt (seconds per click with 172 parts).
class ForgePartsModel final : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by ForgeSceneViewModel")
 public:
  ForgePartsModel(const QVariantList* parts, QObject* parent) : QAbstractListModel(parent), parts_(parts) {}
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(parts_->size()); }
  QVariant data(const QModelIndex& index, int role) const override {
    return index.isValid() && role == Qt::UserRole && index.row() < parts_->size() ? parts_->at(index.row()) : QVariant();
  }
  QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "modelData"}}; }
  void begin_reset() { beginResetModel(); }
  void end_reset() { endResetModel(); }
  void rows_changed(int first, int last) { emit dataChanged(index(first), index(last), {Qt::UserRole}); }

 private:
  const QVariantList* parts_;
};

// The 3D preview of Character Forge: one external model (.glb / .gltf) shown with Qt Quick 3D's
// RuntimeLoader. Independent from the NPC scene of Character Creator and Outfit Studio.
//
// A load goes: validate (sns::gltf, worker thread) -> stage the exact validated bytes in a private
// temporary folder -> hand that folder's file to RuntimeLoader -> the viewport reports the loader
// status and the rendered bounds back. RuntimeLoader "does not sandbox or validate asset contents"
// (Qt documentation), so nothing reaches it without passing the validation first. Every load has
// an operation number; results and viewport reports of an older operation are ignored.
class ForgeSceneViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by CharacterForgeViewModel")
  // empty, validating, loading, loaded, error, cancelled, unsupported
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)  // what RuntimeLoader must load ("" = nothing)
  Q_PROPERTY(QString fileName READ file_name NOTIFY changed)
  Q_PROPERTY(QString filePath READ file_path NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)   // short, for the viewport
  Q_PROPERTY(QString details READ details NOTIFY changed)   // full technical report (advanced inspector)
  Q_PROPERTY(QVariantMap inspection READ inspection NOTIFY changed)
  Q_PROPERTY(QVariantMap rendered READ rendered NOTIFY changed)  // bounds reported by the viewport
  Q_PROPERTY(bool hasModel READ has_model NOTIFY changed)        // loaded and drawn with non-empty bounds
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(int operation READ operation NOTIFY changed)
  // Drawable parts: one per Model that RuntimeLoader created, in its scene-tree order. Each entry:
  // index, label, visible, rendered data (materials, skinned, morphTargets, size) and, when the
  // part is matched to a glTF node, node, nodeName, meshName, primitives, vertices, triangles.
  Q_PROPERTY(QVariantList parts READ parts NOTIFY partsChanged)
  // -1 = none. Its own signal: selecting a part must not rebuild every list that shows the parts.
  Q_PROPERTY(int selectedPart READ selected_part NOTIFY selectionChanged)
  // Facts every parts list needs, computed once per change here instead of by each list delegate (a delegate that walked
  // `parts` made one change cost O(parts²) conversions: minutes with 172 parts).
  Q_PROPERTY(int partCount READ part_count NOTIFY partsChanged)
  Q_PROPERTY(bool anyPartHidden READ any_part_hidden NOTIFY partsChanged)
  // The part shown alone (only it and the ancestors it needs are visible), or -1. isolatePart() toggles it.
  Q_PROPERTY(int isolatedPart READ isolated_part NOTIFY partsChanged)
  Q_PROPERTY(bool anyPartShown READ any_part_shown NOTIFY partsChanged)
  // `parts` for list views (see ForgePartsModel), and the selected part's map ({} = none).
  Q_PROPERTY(ForgePartsModel* partsModel READ parts_model CONSTANT)
  Q_PROPERTY(QVariantMap selectedPartInfo READ selected_part_info NOTIFY selectedPartInfoChanged)
  // How parts relate to glTF nodes. RuntimeLoader (Qt 6.11) gives its Models no names and no
  // node index, so the match is by depth-first order and only when the counts agree:
  // "order" (counts equal, every part's materials/skin/morph targets agree with its node),
  // "order-unconfirmed" (counts equal, some part disagrees), "none" (counts differ), "" (no parts).
  Q_PROPERTY(QString partMatch READ part_match NOTIFY partsChanged)
  // Measured cost of the current load: {validateMs, stageMs (worker thread), importMs (where the 3D import ran),
  // uiBlockMs (longest time the GUI thread was held by it), totalMs (load() -> loaded), fileBytes, workingSetMb}.
  Q_PROPERTY(QVariantMap timings READ timings NOTIFY changed)

 public:
  explicit ForgeSceneViewModel(QObject* parent = nullptr);
  ~ForgeSceneViewModel() override;

  [[nodiscard]] QString state() const { return state_; }
  [[nodiscard]] QUrl source() const { return source_; }
  [[nodiscard]] QString file_name() const;
  [[nodiscard]] QString file_path() const { return path_; }
  [[nodiscard]] QString message() const { return message_; }
  [[nodiscard]] QString details() const { return details_; }
  [[nodiscard]] QVariantMap inspection() const { return inspection_; }
  [[nodiscard]] QVariantMap rendered() const { return rendered_; }
  [[nodiscard]] bool has_model() const { return state_ == QStringLiteral("loaded") && !rendered_.isEmpty(); }
  [[nodiscard]] bool busy() const { return state_ == QStringLiteral("validating") || state_ == QStringLiteral("loading"); }
  [[nodiscard]] int operation() const { return operation_; }
  [[nodiscard]] QVariantList parts() const { return parts_; }
  [[nodiscard]] int selected_part() const { return selected_; }
  [[nodiscard]] int part_count() const { return static_cast<int>(parts_.size()); }
  [[nodiscard]] bool any_part_hidden() const { return any_hidden_; }
  [[nodiscard]] int isolated_part() const { return isolated_; }
  [[nodiscard]] bool any_part_shown() const { return any_shown_; }
  [[nodiscard]] ForgePartsModel* parts_model() const { return parts_model_; }
  [[nodiscard]] QVariantMap selected_part_info() const { return partAt(selected_); }
  // One part's map ({} out of range): what a viewport needs for one part, without converting the whole list.
  Q_INVOKABLE QVariantMap partAt(int index) const;
  // Each part's own visibility choice, in order (the viewport applies it to its Models).
  Q_INVOKABLE QVariantList partVisibility() const;
  // The first part whose label, node or mesh name equals or contains `name` (either way, case-insensitive), or -1.
  Q_INVOKABLE int findPart(const QString& name) const;
  [[nodiscard]] QString part_match() const { return part_match_; }
  [[nodiscard]] QVariantMap timings() const { return timings_; }
  // Called by the viewport's loader with the time its import took and how long it held the GUI thread.
  Q_INVOKABLE void reportImportCost(const QUrl& loaded_source, double import_ms, double ui_block_ms);

  // Formats the preview can validate and render (RuntimeLoader also reads .obj, but OBJ is not
  // validated here: it goes through the Blender bridge like FBX and BLEND).
  [[nodiscard]] static bool previewable(const QString& path);

  Q_INVOKABLE void load(const QString& path_or_url);
  Q_INVOKABLE void reload();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void reset();
  // Called by the viewport for the RuntimeLoader showing `loaded_source`.
  // status: 0 Empty, 1 Success, 2 Error (RuntimeLoader.Status).
  Q_INVOKABLE void reportLoaderStatus(const QUrl& loaded_source, int status, const QString& error_string);
  Q_INVOKABLE void reportBounds(const QUrl& loaded_source, const QVector3D& minimum, const QVector3D& maximum);
  // The Models found under the RuntimeLoader, each {materials, skinned, morphTargets, minimum,
  // maximum} (local bounds). Visibility and selection survive a repeated report of the same count.
  Q_INVOKABLE void reportParts(const QUrl& loaded_source, const QVariantList& rendered_parts);
  Q_INVOKABLE void selectPart(int index);  // out of range clears the selection
  Q_INVOKABLE void setPartVisible(int index, bool visible);
  Q_INVOKABLE void showAllParts();
  // Shows only `index` and its ancestors (one change, one signal) and selects it; on the isolated part, shows all again.
  Q_INVOKABLE void isolatePart(int index);

 signals:
  void changed();
  void sourceChanged();
  void frameRequested();  // new geometry: the viewport frames the camera on it
  void partsChanged();
  void selectionChanged();
  void selectedPartInfoChanged();

 private:
  struct Result;
  void finish(const std::shared_ptr<Result>& result);
  void set_state(const QString& state, const QString& message, const QString& details = {});
  void clear_source();
  void clear_parts();
  void refresh_visibility();
  void remove_staging(const QString& folder);

  QThreadPool pool_;
  int operation_ = 0;
  std::shared_ptr<std::atomic<bool>> cancel_;
  QString state_ = QStringLiteral("empty");
  QString path_;
  QUrl source_;
  QString staging_;  // folder of the staged copy currently shown
  QString message_;
  QString details_;
  QVariantMap inspection_;
  QVariantMap rendered_;
  QVariantList parts_;
  int selected_ = -1;
  bool any_hidden_ = false;
  bool any_shown_ = false;
  int isolated_ = -1;
  ForgePartsModel* parts_model_ = nullptr;
  // Snapshot of every part's visible/shown/hiddenBy, taken before a visibility change; publish_visibility() then
  // tells the model only the rows that changed.
  struct PartView {
    bool visible = false;
    bool shown = false;
    QString hidden_by;
    bool operator==(const PartView&) const = default;
  };
  std::vector<PartView> visibility_snapshot() const;
  void publish_visibility(const std::vector<PartView>& before);
  QString part_match_;
  QString root_;  // per-process staging root
  QVariantMap timings_;
  QElapsedTimer load_clock_;
};

}  // namespace sns::app
