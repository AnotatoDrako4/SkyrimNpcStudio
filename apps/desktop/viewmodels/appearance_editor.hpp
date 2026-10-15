#pragma once

#include <QColor>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "console_model.hpp"
#include "edit_history.hpp"
#include "mo2_snapshot.hpp"
#include "worker_client.hpp"
#include "sns/appearance/appearance_draft.hpp"
#include "sns/face/face_recipe.hpp"
#include "sns/face/facegen.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::app {

class OutfitEditor;
struct ResourceContext;

// What the 3D view needs from the face draft: the regenerated FaceGen files (as an in-memory
// overlay of their Data paths) and the record values that change the body (weight, height, colors).
struct FacePreview {
  std::string npc_form_key;  // the NPC this preview belongs to ("" when none is loaded)
  std::shared_ptr<const rendering::AssetOverlay> overlay;  // null: show the profile's own FaceGen
  // Changes whenever the overlay changes what the viewer draws (geometry, textures). A hair color
  // patch only rewrites the HairTint value inside the NIF, which the viewer takes from hair_color,
  // so it keeps the revision: the view recolors the hair without rebuilding the scene.
  std::uint64_t geometry_revision = 0;
  float weight = 50.0f;
  float height = 1.0f;
  std::optional<face::Rgb8> hair_color;
  face::Rgb8 skin_tone{255, 255, 255};
  bool active = false;  // the draft has face/body edits
};

// Face editing for the selected NPC on top of the FaceGen regenerator (engines/face):
//  - the face is the NPC_ record (NAM9/NAMA/PNAM/tints/colors/weight) read with face.getRecipe;
//  - every edit is a command of the shared EditHistory on the shared AppearanceDraft;
//  - each change rebuilds the FaceGen NIF/tint off the UI thread; the 3D view shows those exact bytes;
//  - export writes the record values AND the very same regenerated files into the mod.
class AppearanceEditor : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")

  Q_PROPERTY(QString npcFormKey READ npc_form_key NOTIFY targetChanged)
  Q_PROPERTY(QString npcLabel READ npc_label NOTIFY targetChanged)
  Q_PROPERTY(bool loaded READ loaded NOTIFY recipeChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY recipeChanged)
  Q_PROPERTY(QString loadError READ load_error NOTIFY recipeChanged)
  Q_PROPERTY(QString presetName READ preset_name NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantMap morphs READ morphs NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList standardSliders READ standard_sliders CONSTANT)
  Q_PROPERTY(QVariantList presetSlots READ preset_slots CONSTANT)
  Q_PROPERTY(QVariantList facePresets READ face_presets NOTIFY appearanceChanged)
  Q_PROPERTY(QColor hairColor READ hair_color NOTIFY appearanceChanged)
  Q_PROPERTY(bool hasHairColor READ has_hair_color NOTIFY appearanceChanged)
  Q_PROPERTY(QColor skinTone READ skin_tone NOTIFY appearanceChanged)
  Q_PROPERTY(float weight READ weight NOTIFY appearanceChanged)
  Q_PROPERTY(float height READ height NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList headParts READ head_parts NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList tints READ tints NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantMap customMorphs READ custom_morphs NOTIFY appearanceChanged)
  Q_PROPERTY(int sculptTargets READ sculpt_targets NOTIFY appearanceChanged)
  Q_PROPERTY(QString geometryMode READ geometry_mode NOTIFY appearanceChanged)
  Q_PROPERTY(QString geometryModeReason READ geometry_mode_reason NOTIFY faceGenChanged)
  Q_PROPERTY(QVariantMap faceGen READ facegen_report NOTIFY faceGenChanged)
  Q_PROPERTY(QVariantList issues READ issues NOTIFY faceGenChanged)
  Q_PROPERTY(bool building READ building NOTIFY faceGenChanged)
  // The profile's FaceGen is still being analysed: no edit is accepted and nothing is built yet.
  Q_PROPERTY(bool analysing READ analysing NOTIFY faceGenChanged)
  // Facial compatibility diagnosis (sns::face::diagnose_face_compatibility) with MO2 provenance:
  // code, label, allowed [{op, label}], blocked [{op, label, reason}], triMismatches, evidence, provenance.
  Q_PROPERTY(QVariantMap faceCompatibility READ face_compatibility NOTIFY faceGenChanged)
  Q_PROPERTY(bool exportable READ exportable NOTIFY faceGenChanged)
  Q_PROPERTY(bool canUndo READ can_undo NOTIFY appearanceChanged)
  Q_PROPERTY(bool canRedo READ can_redo NOTIFY appearanceChanged)
  Q_PROPERTY(QString undoText READ undo_text NOTIFY appearanceChanged)
  Q_PROPERTY(QString redoText READ redo_text NOTIFY appearanceChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY appearanceChanged)
  Q_PROPERTY(bool hasFaceEdits READ has_face_edits NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList presetsList READ presets_list NOTIFY presetsChanged)
  Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
  Q_PROPERTY(QVariantMap lastExport READ last_export NOTIFY exportingChanged)
  // Easy Mod Creator: what a guided export would write now, its mode and every check
  // (sns::appearance::plan_easy_mod over the face and outfit state of the shared draft).
  Q_PROPERTY(QVariantMap easyModPlan READ easy_mod_plan NOTIFY easyModChanged)

 public:
  using SnapshotProvider = std::function<SnapshotPtr()>;

  AppearanceEditor(WorkerClient* worker, ConsoleModel* console, OutfitEditor* outfit, SnapshotProvider snapshot,
                   appearance::AppearanceDraft& draft, EditHistory* history, QObject* parent = nullptr);
  ~AppearanceEditor() override;

  // Called by the studio after it reset the shared draft for a newly selected NPC.
  void set_target(const projects::NpcTarget& target);
  // A face saved in a project for `form_key`: applied once that NPC's record has been read.
  void queue_saved_face(const std::string& form_key, const std::optional<projects::FaceConfig>& face);
  // The face to save in the project (nullopt when the face equals the record).
  [[nodiscard]] std::optional<projects::FaceConfig> face_config_for_project() const;
  [[nodiscard]] FacePreview preview() const;
  void clear();

  [[nodiscard]] QString npc_form_key() const;
  [[nodiscard]] QString npc_label() const;
  [[nodiscard]] bool loaded() const { return draft_.face_loaded(); }
  [[nodiscard]] bool loading() const { return loading_; }
  [[nodiscard]] QString load_error() const { return load_error_; }
  [[nodiscard]] QString preset_name() const;
  [[nodiscard]] QVariantMap morphs() const;
  [[nodiscard]] QVariantList standard_sliders() const;
  [[nodiscard]] QVariantList preset_slots() const;
  [[nodiscard]] QVariantList face_presets() const;
  [[nodiscard]] QColor hair_color() const;
  [[nodiscard]] bool has_hair_color() const;
  [[nodiscard]] QColor skin_tone() const;
  [[nodiscard]] float weight() const;
  [[nodiscard]] float height() const;
  [[nodiscard]] QVariantList head_parts() const;
  [[nodiscard]] QVariantList tints() const;
  [[nodiscard]] QVariantMap custom_morphs() const;
  [[nodiscard]] int sculpt_targets() const;
  [[nodiscard]] QString geometry_mode() const;
  [[nodiscard]] QString geometry_mode_reason() const { return geometry_reason_; }
  [[nodiscard]] QVariantMap facegen_report() const { return facegen_report_; }
  [[nodiscard]] QVariantList issues() const;
  [[nodiscard]] bool building() const { return building_; }
  [[nodiscard]] bool analysing() const {
    return draft_.face_loaded() && draft_.face_baseline().geometry_mode == appearance::kGeometryPending;
  }
  [[nodiscard]] bool exportable() const;
  [[nodiscard]] QVariantMap face_compatibility() const { return compatibility_; }
  // An operation ("hair-color", "face-morphs", ...) the diagnosis blocks; false while unknown.
  [[nodiscard]] bool operation_blocked(const QString& op) const;
  [[nodiscard]] bool can_undo() const { return history_->can_undo(); }
  [[nodiscard]] bool can_redo() const { return history_->can_redo(); }
  [[nodiscard]] QString undo_text() const { return history_->undo_text(); }
  [[nodiscard]] QString redo_text() const { return history_->redo_text(); }
  [[nodiscard]] bool dirty() const { return draft_.is_face_dirty() || draft_.is_height_dirty(); }
  [[nodiscard]] bool has_face_edits() const { return draft_.is_face_dirty(); }
  [[nodiscard]] QVariantList presets_list() const { return presets_list_; }
  [[nodiscard]] bool exporting() const { return exporting_; }
  [[nodiscard]] QVariantMap last_export() const { return last_export_; }
  [[nodiscard]] QVariantMap easy_mod_plan() const;
  [[nodiscard]] const face::FaceRecipe* recipe() const { return recipe_ ? &*recipe_ : nullptr; }
  // The FaceGen the profile loads for the NPC (MO2 winner), as read for the analysis; null if none.
  [[nodiscard]] std::shared_ptr<const std::string> winning_facegen() const { return existing_facegen_; }

  Q_INVOKABLE void setSlider(const QString& key, float value);
  Q_INVOKABLE void setFacePreset(int slot, int value);
  Q_INVOKABLE void setHairColor(const QColor& color);
  Q_INVOKABLE void setSkinTone(const QColor& color);
  Q_INVOKABLE void setWeight(float weight);
  Q_INVOKABLE void setHeight(float height);
  Q_INVOKABLE void setHeadPart(const QString& formKey, const QString& type);
  Q_INVOKABLE void removeHeadPart(const QString& type);
  Q_INVOKABLE void setTint(int index, const QColor& color, float value);
  Q_INVOKABLE void setGeometryMode(const QString& mode);
  Q_INVOKABLE void resetFace();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void applyDonor(const QString& donorFormKey, const QString& donorName = QString());
  Q_INVOKABLE void applyJslotPreset(const QString& filePath);
  Q_INVOKABLE QVariantMap evaluatePresetCompatibility(const QString& filePath);
  Q_INVOKABLE void loadPresetsFromVfs();
  Q_INVOKABLE void exportFaceOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip = true,
                                      const QString& author = {}, const QString& description = {});
  Q_INVOKABLE void exportCompleteOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip = true,
                                          const QString& author = {}, const QString& description = {});

 signals:
  void targetChanged();
  void recipeChanged();
  void appearanceChanged();
  void faceGenChanged();
  void presetsChanged();
  void exportingChanged();
  void exportFinished(bool success, const QString& message, const QString& modDirectory, const QString& zipPath);
  void easyModChanged();

 private:
  struct Build;
  // One FaceGen job in flight. Builds run one at a time; an edit made meanwhile cancels it
  // (cooperatively, between head parts) and a new build of the latest draft starts when it returns.
  struct Job {
    std::uint64_t task = 0;
    std::string npc;
    std::shared_ptr<std::atomic<bool>> cancel;
  };
  void edit(appearance::FaceState next, const QString& description, const QString& merge_key = {});
  // True (and the reason written to the console) when the compatibility diagnosis blocks `op`.
  bool refuse_blocked(const QString& op);
  void request_recipe(bool initial);
  void on_recipe(const Json& json, bool initial, int serial);
  void analyse_existing_facegen();
  void schedule_build();
  void start_build();
  void finish_build(const std::shared_ptr<Build>& build);
  void cancel_jobs();
  void export_overhaul(bool complete, const QString& plugin_name, const QString& output_dir, bool esl, bool zip,
                       const QString& author, const QString& description);
  void finish_export(bool ok, const QString& message, const QString& mod_dir, const QString& zip, const QVariantMap& extra = {});
  std::shared_ptr<ResourceContext> context() const;
  std::vector<face::MorphExtension> morph_extensions(const ResourceContext& context) const;
  QVariantMap jslot_compatibility(const QString& file_path, std::optional<appearance::FaceState>* mapped);

  WorkerClient* worker_;
  ConsoleModel* console_;
  OutfitEditor* outfit_;
  SnapshotProvider snapshot_;
  appearance::AppearanceDraft& draft_;
  EditHistory* history_;
  projects::NpcTarget target_;

  // The record as read (base) and the structure for the draft's current head parts.
  std::optional<face::FaceRecipe> base_recipe_;
  std::optional<face::FaceRecipe> recipe_;
  bool loading_ = false;
  QString load_error_;
  int recipe_serial_ = 0;
  std::map<std::string, projects::FaceConfig> saved_faces_;  // per NPC, from an opened project
  QVariantList recipe_issues_;

  // Existing FaceGen of the profile, and how edits are applied to it.
  std::shared_ptr<const std::string> existing_facegen_;
  QString geometry_reason_;
  QVariantList existing_issues_;
  QVariantMap compatibility_;
  bool has_existing_tint_ = false;

  // Latest regenerated FaceGen (preview == export bytes).
  QThreadPool pool_;
  QTimer build_timer_;
  bool building_ = false;
  std::optional<Job> running_;
  bool rebuild_pending_ = false;
  std::shared_ptr<std::atomic<bool>> analysis_cancel_;
  std::shared_ptr<Build> build_;
  QVariantMap facegen_report_;
  std::shared_ptr<const rendering::AssetOverlay> overlay_;
  std::uint64_t geometry_revision_ = 0;

  QVariantList presets_list_;
  bool exporting_ = false;
  QVariantMap last_export_;
  mutable std::optional<std::vector<face::MorphExtension>> extensions_cache_;
  mutable SnapshotPtr extensions_snapshot_;
};

}  // namespace sns::app
