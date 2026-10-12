#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
#include <optional>

#include "console_model.hpp"
#include "mo2_snapshot.hpp"
#include "worker_client.hpp"
#include "sns/appearance/appearance_draft.hpp"
#include "sns/face/face_morphs.hpp"
#include "sns/face/head_parts.hpp"
#include "sns/presets/jslot_parser.hpp"
#include "sns/presets/preset_model.hpp"
#include "sns/projects/command_stack.hpp"

namespace sns::app {

class OutfitEditor;

class AppearanceEditor : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")

  Q_PROPERTY(QString npcFormKey READ npc_form_key NOTIFY targetChanged)
  Q_PROPERTY(QString npcLabel READ npc_label NOTIFY targetChanged)
  Q_PROPERTY(QString presetName READ preset_name NOTIFY appearanceChanged)
  Q_PROPERTY(QString donorNpcFormKey READ donor_npc_form_key NOTIFY appearanceChanged)
  Q_PROPERTY(QColor hairColor READ hair_color NOTIFY appearanceChanged)
  Q_PROPERTY(QColor skinTone READ skin_tone NOTIFY appearanceChanged)
  Q_PROPERTY(float weight READ weight NOTIFY appearanceChanged)
  Q_PROPERTY(float height READ height NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantMap morphs READ morphs NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList headParts READ head_parts NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList standardSliders READ standard_sliders CONSTANT)
  Q_PROPERTY(bool canUndo READ can_undo NOTIFY appearanceChanged)
  Q_PROPERTY(bool canRedo READ can_redo NOTIFY appearanceChanged)
  Q_PROPERTY(QString undoText READ undo_text NOTIFY appearanceChanged)
  Q_PROPERTY(QString redoText READ redo_text NOTIFY appearanceChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY appearanceChanged)
  Q_PROPERTY(bool hasFaceEdits READ has_face_edits NOTIFY appearanceChanged)
  Q_PROPERTY(QVariantList presetsList READ presets_list NOTIFY presetsChanged)
  Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

 public:
  using SnapshotProvider = std::function<SnapshotPtr()>;

  AppearanceEditor(WorkerClient* worker, ConsoleModel* console, OutfitEditor* outfit, SnapshotProvider snapshot, QObject* parent = nullptr);

  void set_target(const projects::NpcTarget& target, const std::optional<projects::FaceConfig>& initial_face = std::nullopt);
  void clear();

  [[nodiscard]] const projects::NpcTarget& target() const { return target_; }
  [[nodiscard]] QString npc_form_key() const;
  [[nodiscard]] QString npc_label() const;
  [[nodiscard]] QString preset_name() const;
  [[nodiscard]] QString donor_npc_form_key() const;
  [[nodiscard]] QColor hair_color() const;
  [[nodiscard]] QColor skin_tone() const;
  [[nodiscard]] float weight() const;
  [[nodiscard]] float height() const;
  [[nodiscard]] QVariantMap morphs() const;
  [[nodiscard]] QVariantList head_parts() const;
  [[nodiscard]] QVariantList standard_sliders() const;
  [[nodiscard]] bool can_undo() const { return stack_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return stack_.can_redo(); }
  [[nodiscard]] QString undo_text() const;
  [[nodiscard]] QString redo_text() const;
  [[nodiscard]] bool dirty() const { return !stack_.is_clean(); }
  [[nodiscard]] bool has_face_edits() const { return draft_.is_face_dirty(); }
  [[nodiscard]] QVariantList presets_list() const { return presets_list_; }
  [[nodiscard]] bool exporting() const { return exporting_; }

  [[nodiscard]] const appearance::AppearanceDraft& draft() const { return draft_; }
  [[nodiscard]] nlohmann::json build_donor_override_json() const;

  Q_INVOKABLE void applyDonor(const QString& donorFormKey, const QString& donorName = "");
  Q_INVOKABLE void applyJslotPreset(const QString& filePath);
  Q_INVOKABLE void setSlider(const QString& name, float value);
  Q_INVOKABLE void setHairColor(const QColor& color);
  Q_INVOKABLE void setSkinTone(const QColor& color);
  Q_INVOKABLE void setWeight(float weight);
  Q_INVOKABLE void setHeight(float height);
  Q_INVOKABLE void setHeadPart(const QString& formKey, const QString& type, const QString& editorId = "", const QString& meshPath = "");
  Q_INVOKABLE void removeHeadPart(const QString& type);
  Q_INVOKABLE void resetFace();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void loadPresetsFromVfs();
  Q_INVOKABLE QVariantMap evaluatePresetCompatibility(const QString& filePath);
  Q_INVOKABLE void exportFaceOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip = true);
  Q_INVOKABLE void exportCompleteOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip = true);

 signals:
  void targetChanged();
  void appearanceChanged();
  void presetsChanged();
  void exportingChanged();
  void exportFinished(bool success, const QString& message, const QString& modDirectory, const QString& zipPath);

 private:
  void execute(std::unique_ptr<projects::Command> cmd);
  void notify_changed();

  WorkerClient* worker_;
  ConsoleModel* console_;
  OutfitEditor* outfit_;
  SnapshotProvider snapshot_;
  projects::NpcTarget target_;
  appearance::AppearanceDraft draft_;
  projects::CommandStack stack_;
  QVariantList presets_list_;
  bool exporting_ = false;
};

}  // namespace sns::app
