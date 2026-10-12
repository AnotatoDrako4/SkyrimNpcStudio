#include "appearance_editor.hpp"

#include <QDir>
#include <QFileInfo>
#include <filesystem>

#include "json_bridge.hpp"
#include "outfit_editor.hpp"
#include "sns/core/file_io.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::app {

using Json = nlohmann::json;

AppearanceEditor::AppearanceEditor(
    WorkerClient* worker,
    ConsoleModel* console,
    OutfitEditor* outfit,
    SnapshotProvider snapshot,
    QObject* parent)
    : QObject(parent),
      worker_(worker),
      console_(console),
      outfit_(outfit),
      snapshot_(std::move(snapshot)) {}

void AppearanceEditor::set_target(
    const projects::NpcTarget& target,
    const std::optional<projects::FaceConfig>& initial_face) {
  target_ = target;
  draft_ = appearance::AppearanceDraft();
  draft_.set_target(target);
  if (initial_face) {
    draft_.set_initial_face(*initial_face);
  }
  stack_.clear();
  emit targetChanged();
  notify_changed();
}

void AppearanceEditor::clear() {
  target_ = {};
  draft_ = appearance::AppearanceDraft();
  stack_.clear();
  emit targetChanged();
  notify_changed();
}

QString AppearanceEditor::npc_form_key() const {
  return qs(target_.form_key);
}

QString AppearanceEditor::npc_label() const {
  if (!target_.name.empty()) {
    return qs(target_.name);
  }
  return qs(target_.form_key);
}

QString AppearanceEditor::preset_name() const {
  return qs(draft_.face().config.preset_name);
}

QString AppearanceEditor::donor_npc_form_key() const {
  return qs(draft_.face().config.donor_npc_form_key);
}

QColor AppearanceEditor::hair_color() const {
  if (const auto& c = draft_.face().config.hair_color) {
    return QColor(c->r, c->g, c->b);
  }
  return QColor(Qt::white);
}

QColor AppearanceEditor::skin_tone() const {
  if (const auto& c = draft_.face().config.skin_tone) {
    return QColor(c->r, c->g, c->b);
  }
  return QColor(255, 220, 200);
}

float AppearanceEditor::weight() const {
  return draft_.face().config.weight;
}

float AppearanceEditor::height() const {
  return draft_.face().config.height;
}

QVariantMap AppearanceEditor::morphs() const {
  QVariantMap map;
  for (const auto& [k, v] : draft_.face().config.morph_sliders) {
    map[qs(k)] = v;
  }
  return map;
}

QVariantList AppearanceEditor::head_parts() const {
  QVariantList list;
  for (const auto& hp : draft_.face().config.head_parts) {
    QVariantMap item{
        {"formKey", qs(hp.form_key)},
        {"editorId", qs(hp.editor_id)},
        {"type", qs(hp.type)},
        {"meshPath", qs(hp.mesh_path)},
    };
    list.push_back(item);
  }
  return list;
}

QVariantList AppearanceEditor::standard_sliders() const {
  QVariantList list;
  for (const auto& def : face::get_standard_face_morphs()) {
    QVariantMap m{
        {"key", QString::fromStdString(def.key)},
        {"category", QString::fromUtf8(face::morph_category_name(def.category).data(), static_cast<qsizetype>(face::morph_category_name(def.category).size()))},
        {"displayNameEs", QString::fromStdString(def.display_name_es)},
        {"displayNameEn", QString::fromStdString(def.display_name_en)},
        {"min", def.min_value},
        {"max", def.max_value},
        {"defaultValue", def.default_value},
    };
    list.push_back(m);
  }
  return list;
}

QString AppearanceEditor::undo_text() const {
  return qs(stack_.undo_text());
}

QString AppearanceEditor::redo_text() const {
  return qs(stack_.redo_text());
}

void AppearanceEditor::execute(std::unique_ptr<projects::Command> cmd) {
  stack_.push(std::move(cmd));
  notify_changed();
}

void AppearanceEditor::notify_changed() {
  emit appearanceChanged();
}

void AppearanceEditor::undo() {
  stack_.undo();
  notify_changed();
}

void AppearanceEditor::redo() {
  stack_.redo();
  notify_changed();
}

void AppearanceEditor::setSlider(const QString& name, float value) {
  auto clamped = face::clamp_morph_value(ss(name), value);
  execute(appearance::make_set_face_slider(draft_, ss(name), clamped));
}

void AppearanceEditor::setHairColor(const QColor& color) {
  projects::ColorRgb rgb{
      .r = static_cast<std::uint8_t>(color.red()),
      .g = static_cast<std::uint8_t>(color.green()),
      .b = static_cast<std::uint8_t>(color.blue()),
  };
  execute(appearance::make_set_hair_color(draft_, rgb));
}

void AppearanceEditor::setSkinTone(const QColor& color) {
  projects::ColorRgb rgb{
      .r = static_cast<std::uint8_t>(color.red()),
      .g = static_cast<std::uint8_t>(color.green()),
      .b = static_cast<std::uint8_t>(color.blue()),
  };
  execute(appearance::make_set_skin_tone(draft_, rgb));
}

void AppearanceEditor::setWeight(float weight) {
  execute(appearance::make_set_body_dimensions(draft_, weight, draft_.face().config.height));
}

void AppearanceEditor::setHeight(float height) {
  execute(appearance::make_set_body_dimensions(draft_, draft_.face().config.weight, height));
}

void AppearanceEditor::setHeadPart(const QString& formKey, const QString& type, const QString& editorId, const QString& meshPath) {
  projects::HeadPartRef ref{
      .form_key = ss(formKey),
      .editor_id = ss(editorId),
      .type = ss(type),
      .mesh_path = ss(meshPath),
  };
  execute(appearance::make_set_head_part(draft_, std::move(ref)));
}

void AppearanceEditor::removeHeadPart(const QString& type) {
  execute(appearance::make_remove_head_part(draft_, ss(type)));
}

void AppearanceEditor::resetFace() {
  execute(appearance::make_reset_face(draft_));
}

void AppearanceEditor::applyDonor(const QString& donorFormKey, const QString& donorName) {
  if (donorFormKey.isEmpty() || !worker_) return;

  Json req{{"npcFormKey", ss(donorFormKey)}};
  worker_->call(ipc::protocol::method::kGetDonorAppearance, req, std::chrono::seconds(10), this,
    [this, donorFormKey, donorName](const Json& data) {
      projects::FaceConfig cfg;
      cfg.preset_id = ss(donorFormKey);
      cfg.preset_name = donorName.isEmpty() ? ss(donorFormKey) : ss(donorName);
      cfg.preset_source = "donor";
      cfg.donor_npc_form_key = ss(donorFormKey);
      cfg.donor_plugin = data.value("winningPlugin", "");
      cfg.donor_facegen_mesh = data.value("faceGeometryPath", "");
      cfg.donor_facegen_tint = data.value("faceTintPath", "");
      cfg.weight = data.value("weight", 50.0f);
      cfg.height = data.value("height", 1.0f);

      if (data.contains("hairColor") && data["hairColor"].is_object()) {
        cfg.hair_color = projects::ColorRgb{
            .r = data["hairColor"].value("r", static_cast<std::uint8_t>(255)),
            .g = data["hairColor"].value("g", static_cast<std::uint8_t>(255)),
            .b = data["hairColor"].value("b", static_cast<std::uint8_t>(255)),
        };
      }

      if (data.contains("skinTone") && data["skinTone"].is_object()) {
        cfg.skin_tone = projects::ColorRgb{
            .r = data["skinTone"].value("r", static_cast<std::uint8_t>(255)),
            .g = data["skinTone"].value("g", static_cast<std::uint8_t>(255)),
            .b = data["skinTone"].value("b", static_cast<std::uint8_t>(255)),
        };
      }

      if (data.contains("headParts") && data["headParts"].is_array()) {
        for (const auto& hp : data["headParts"]) {
          projects::HeadPartRef ref{
              .form_key = hp.value("formKey", ""),
              .editor_id = hp.value("editorId", ""),
              .type = hp.value("type", ""),
              .mesh_path = hp.value("meshPath", ""),
          };
          cfg.head_parts.push_back(std::move(ref));
        }
      }

      execute(appearance::make_apply_donor_face(draft_, std::move(cfg), "Apply Donor Appearance"));
      if (console_) {
        console_->append("info", "appearance", tr("Apariencia de donor '%1' aplicada correctamente.").arg(donorName.isEmpty() ? donorFormKey : donorName));
      }
    },
    [this](int, const QString& err, const Json&) {
      if (console_) {
        console_->append("error", "appearance", tr("Error al obtener apariencia de donor: %1").arg(err));
      }
    });
}

void AppearanceEditor::applyJslotPreset(const QString& filePath) {
  std::string content;
  try {
    content = file_io::read_text_file(std::filesystem::path(reinterpret_cast<const char8_t*>(filePath.toUtf8().constData())));
  } catch (const std::exception&) {
    if (console_) console_->append("error", "appearance", tr("No se pudo leer el archivo .jslot: %1").arg(filePath));
    return;
  }

  auto preset_item = presets::parse_jslot(content, QFileInfo(filePath).baseName().toStdString());
  auto face_cfg = presets::to_face_config(preset_item);

  execute(appearance::make_apply_donor_face(draft_, std::move(face_cfg), "Apply RaceMenu Preset"));
  if (console_) {
    console_->append("info", "appearance", tr("Preset RaceMenu '%1' aplicado.").arg(QFileInfo(filePath).fileName()));
  }
}

QVariantMap AppearanceEditor::evaluatePresetCompatibility(const QString& filePath) {
  QVariantMap result;
  std::string content;
  try {
    content = file_io::read_text_file(std::filesystem::path(reinterpret_cast<const char8_t*>(filePath.toUtf8().constData())));
  } catch (const std::exception& e) {
    result["compatible"] = false;
    result["exportable"] = false;
    result["issues"] = QStringList{tr("No se puede abrir el archivo de preset: %1").arg(qs(e.what()))};
    return result;
  }

  auto preset_item = presets::parse_jslot(content, QFileInfo(filePath).baseName().toStdString());

  std::vector<std::string> active_plugins;
  if (auto snap = snapshot_()) {
    for (const auto& entry : snap->load_order.active) {
      active_plugins.push_back(entry.name);
    }
  }

  auto report = presets::evaluate_compatibility(preset_item, target_, "", "", active_plugins);
  result["compatible"] = report.compatible;
  result["exportable"] = report.exportable;
  QStringList issues;
  for (const auto& iss : report.issues) issues << qs(iss);
  result["issues"] = issues;
  return result;
}

void AppearanceEditor::loadPresetsFromVfs() {
  presets_list_.clear();
  auto snap = snapshot_();
  if (!snap) return;

  for (const auto& provider : snap->vfs.providers()) {
    std::filesystem::path preset_dir = provider.root / "SKSE" / "Plugins" / "CharGen" / "Presets";
    std::error_code ec;
    if (std::filesystem::exists(preset_dir, ec) && std::filesystem::is_directory(preset_dir, ec)) {
      for (const auto& entry : std::filesystem::directory_iterator(preset_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jslot") {
          QVariantMap item{
              {"name", QString::fromStdString(entry.path().stem().string())},
              {"fileName", QString::fromStdString(entry.path().filename().string())},
              {"vfsPath", QString::fromStdString("SKSE/Plugins/CharGen/Presets/" + entry.path().filename().string())},
              {"diskPath", QString::fromStdString(entry.path().string())},
              {"sourceMod", QString::fromStdString(provider.name)},
          };
          presets_list_.push_back(item);
        }
      }
    }
  }
  emit presetsChanged();
}

Json AppearanceEditor::build_donor_override_json() const {
  if (!draft_.is_face_dirty()) {
    return nullptr;
  }

  const auto& f = draft_.face().config;
  Json obj;
  if (!f.donor_npc_form_key.empty()) {
    obj["donorNpcFormKey"] = f.donor_npc_form_key;
  }
  if (!f.donor_facegen_mesh.empty()) {
    obj["donorFaceGeometryPath"] = f.donor_facegen_mesh;
  }
  if (!f.donor_facegen_tint.empty()) {
    obj["donorFaceTintPath"] = f.donor_facegen_tint;
  }

  if (f.hair_color) {
    obj["hairColor"] = {{"r", f.hair_color->r}, {"g", f.hair_color->g}, {"b", f.hair_color->b}};
  }
  if (f.skin_tone) {
    obj["skinTone"] = {{"r", f.skin_tone->r}, {"g", f.skin_tone->g}, {"b", f.skin_tone->b}};
  }
  obj["weight"] = f.weight;

  if (!f.head_parts.empty()) {
    Json hps = Json::array();
    for (const auto& hp : f.head_parts) {
      hps.push_back({
          {"formKey", hp.form_key},
          {"editorId", hp.editor_id},
          {"type", hp.type},
          {"meshPath", hp.mesh_path},
      });
    }
    obj["headParts"] = std::move(hps);
  }

  return obj;
}

void AppearanceEditor::exportFaceOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip) {
  if (!worker_ || target_.form_key.empty()) return;

  exporting_ = true;
  emit exportingChanged();

  const auto& f = draft_.face().config;
  Json assignment{
      {"npcFormKey", target_.form_key},
  };

  if (!f.donor_npc_form_key.empty()) {
    assignment["donorNpcFormKey"] = f.donor_npc_form_key;
    assignment["donorFaceGeometryPath"] = f.donor_facegen_mesh;
    assignment["donorFaceTintPath"] = f.donor_facegen_tint;
  }

  if (f.hair_color) {
    assignment["hairColor"] = {{"r", f.hair_color->r}, {"g", f.hair_color->g}, {"b", f.hair_color->b}};
  }
  if (f.skin_tone) {
    assignment["skinTone"] = {{"r", f.skin_tone->r}, {"g", f.skin_tone->g}, {"b", f.skin_tone->b}};
  }
  assignment["weight"] = f.weight;
  assignment["height"] = f.height;

  if (!f.head_parts.empty()) {
    Json hps = Json::array();
    for (const auto& hp : f.head_parts) {
      if (!hp.form_key.empty()) hps.push_back(hp.form_key);
    }
    if (!hps.empty()) assignment["headPartFormKeys"] = std::move(hps);
  }

  if (!f.morph_sliders.empty()) {
    Json morphs = Json::object();
    for (const auto& [k, v] : f.morph_sliders) {
      morphs[k] = v;
    }
    assignment["faceMorphs"] = std::move(morphs);
  }

  Json req{
      {"pluginName", ss(pluginName)},
      {"outputDirectory", ss(outputDir)},
      {"eslFlag", eslFlag},
      {"createZip", createZip},
      {"assignments", Json::array({assignment})},
  };

  worker_->call(ipc::protocol::method::kExportFacePatch, req, std::chrono::seconds(60), this,
    [this](const Json& data) {
      exporting_ = false;
      emit exportingChanged();
      auto modDir = qs(data.value("modDirectory", ""));
      auto zip = qs(data.value("zipPath", ""));
      emit exportFinished(true, tr("Mod de Face Overhaul exportado exitosamente."), modDir, zip);
      if (console_) console_->append("info", "export", tr("Exportación completada: %1").arg(modDir));
    },
    [this](int, const QString& err, const Json&) {
      exporting_ = false;
      emit exportingChanged();
      emit exportFinished(false, err, "", "");
      if (console_) console_->append("error", "export", tr("Error al exportar Face Overhaul: %1").arg(err));
    });
}

void AppearanceEditor::exportCompleteOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip) {
  if (!worker_ || target_.form_key.empty()) return;

  exporting_ = true;
  emit exportingChanged();

  const auto& f = draft_.face().config;
  Json completeAssignment{
      {"npcFormKey", target_.form_key},
  };

  // Outfit part
  if (outfit_ && !outfit_->pieces().isEmpty()) {
    Json outfitPart{
        {"npcFormKey", target_.form_key},
        {"armorFormKeys", Json::array()},
    };
    for (const auto& p : outfit_->pieces()) {
      outfitPart["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
    }
    completeAssignment["outfit"] = std::move(outfitPart);
  }

  // Face part
  Json facePart{{"npcFormKey", target_.form_key}};
  if (!f.donor_npc_form_key.empty()) {
    facePart["donorNpcFormKey"] = f.donor_npc_form_key;
    facePart["donorFaceGeometryPath"] = f.donor_facegen_mesh;
    facePart["donorFaceTintPath"] = f.donor_facegen_tint;
  }
  if (f.hair_color) facePart["hairColor"] = {{"r", f.hair_color->r}, {"g", f.hair_color->g}, {"b", f.hair_color->b}};
  if (f.skin_tone) facePart["skinTone"] = {{"r", f.skin_tone->r}, {"g", f.skin_tone->g}, {"b", f.skin_tone->b}};
  facePart["weight"] = f.weight;
  facePart["height"] = f.height;

  if (!f.head_parts.empty()) {
    Json hps = Json::array();
    for (const auto& hp : f.head_parts) {
      if (!hp.form_key.empty()) hps.push_back(hp.form_key);
    }
    if (!hps.empty()) facePart["headPartFormKeys"] = std::move(hps);
  }
  if (!f.morph_sliders.empty()) {
    Json morphs = Json::object();
    for (const auto& [k, v] : f.morph_sliders) morphs[k] = v;
    facePart["faceMorphs"] = std::move(morphs);
  }

  completeAssignment["face"] = std::move(facePart);

  Json req{
      {"pluginName", ss(pluginName)},
      {"outputDirectory", ss(outputDir)},
      {"eslFlag", eslFlag},
      {"createZip", createZip},
      {"assignments", Json::array({completeAssignment})},
  };

  worker_->call(ipc::protocol::method::kExportCompleteNpc, req, std::chrono::seconds(60), this,
    [this](const Json& data) {
      exporting_ = false;
      emit exportingChanged();
      auto modDir = qs(data.value("modDirectory", ""));
      auto zip = qs(data.value("zipPath", ""));
      emit exportFinished(true, tr("Mod de Complete Overhaul exportado exitosamente."), modDir, zip);
      if (console_) console_->append("info", "export", tr("Exportación completada: %1").arg(modDir));
    },
    [this](int, const QString& err, const Json&) {
      exporting_ = false;
      emit exportingChanged();
      emit exportFinished(false, err, "", "");
      if (console_) console_->append("error", "export", tr("Error al exportar Complete Overhaul: %1").arg(err));
    });
}

}  // namespace sns::app
