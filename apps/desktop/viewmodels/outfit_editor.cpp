#include "outfit_editor.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::app {

using namespace std::chrono_literals;
namespace method = ipc::protocol::method;

namespace {

QString local_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

bool is_inside(const QString& child, const std::filesystem::path& parent) {
  if (parent.empty()) return false;
  const auto c = QDir::cleanPath(QFileInfo(child).absoluteFilePath()).toLower() + "/";
  const auto p = QDir::cleanPath(QString::fromStdWString(parent.native())).toLower() + "/";
  return c.startsWith(p);
}

outfit::Piece piece_from(const QVariantMap& armor) {
  outfit::Piece p;
  p.form_key = ss(armor.value("formKey").toString());
  p.name = ss(armor.value("name").toString().isEmpty() ? armor.value("editorId").toString() : armor.value("name").toString());
  p.plugin = ss(armor.value("winningPlugin").toString());
  p.slot_mask = armor.value("slotMask").toUInt();
  return p;
}

std::filesystem::path preset_directory() {
  return std::filesystem::path((QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/OutfitPresets").toStdWString());
}

bool custom_preset_id(const QString& id) {
  static const QRegularExpression safe_id("^[A-Za-z0-9_-]+$");
  if (!safe_id.match(id).hasMatch()) return false;
  const auto builtin = outfit::builtin_outfit_presets();
  return std::none_of(builtin.begin(), builtin.end(), [&](const auto& p) { return p.id == ss(id); });
}

std::filesystem::path preset_file(const QString& id) { return preset_directory() / (ss(id) + ".json"); }

}  // namespace

OutfitEditor::OutfitEditor(WorkerClient* worker, ConsoleModel* console, SnapshotProvider snapshot,
                           appearance::AppearanceDraft& draft, EditHistory* history, QObject* parent)
    : QObject(parent), worker_(worker), console_(console), snapshot_(std::move(snapshot)), draft_(draft), history_(history) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(250);
  connect(&debounce_, &QTimer::timeout, this, &OutfitEditor::validate);
  // Undo/redo of any panel: refresh, and re-validate if the outfit itself changed.
  connect(history_, &EditHistory::changed, this, [this] { after_change(); });
}

QString OutfitEditor::npc_label() const {
  if (target_.form_key.empty()) return {};
  return qs(target_.name.empty() ? target_.editor_id : target_.name) + " (" + qs(target_.form_key) + ")";
}

QString OutfitEditor::suggested_plugin_name() const {
  if (project_ && project_->npcs.size() > 1) {
    QString proj_name = qs(project_->name);
    if (!proj_name.isEmpty()) {
      proj_name.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
      return "SkyrimNpcStudio_" + proj_name + ".esp";
    }
  }
  QString base = qs(target_.editor_id.empty() ? target_.form_key : target_.editor_id);
  base.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
  return "SkyrimNpcStudio_" + base + ".esp";
}

QString OutfitEditor::default_export_directory() const {
  return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SkyrimNpcStudio/Exports";
}

QVariantList OutfitEditor::pieces() const {
  QVariantList out;
  for (const auto& p : draft_.outfit().pieces()) {
    QStringList slot_list;
    for (int s : outfit::slots_of(p.slot_mask)) slot_list << QString::number(s);
    QVariantMap map{
        {"formKey", qs(p.form_key)},
        {"name", qs(p.name)},
        {"plugin", qs(p.plugin)},
        {"slotMask", p.slot_mask},
        {"slots", slot_list.join(", ")},
        {"hasMaterial", p.material.has_value()},
        {"materialName", p.material ? qs(p.material->name) : QString()},
        {"diffuseOverride", p.material ? qs(p.material->diffuse_override) : QString()},
        {"normalOverride", p.material ? qs(p.material->normal_override) : QString()},
    };
    if (p.material && p.material->tint_color) {
      map["tintColor"] = QColor(p.material->tint_color->r, p.material->tint_color->g, p.material->tint_color->b);
    }
    out << map;
  }
  return out;
}

QVariantList OutfitEditor::local_conflicts() const {
  QVariantList out;
  for (const auto& c : draft_.outfit().conflicts()) {
    out << QVariantMap{{"slot", c.slot}, {"first", qs(c.first)}, {"second", qs(c.second)}};
  }
  return out;
}

QStringList OutfitEditor::history() const {
  QStringList out;
  out = history_->entries();
  return out;
}

bool OutfitEditor::exportable() const {
  const auto& pieces = draft_.outfit().pieces();
  return !validating_ && validation_.value("exportable").toBool() && !pieces.empty() &&
         std::none_of(pieces.begin(), pieces.end(), [](const auto& p) { return p.material.has_value(); });
}

QVariantList OutfitEditor::project_npcs() const {
  QVariantList out;
  if (!project_) return out;
  for (const auto& edit : project_->npcs) {
    out.push_back(QVariantMap{
        {"formKey", qs(edit.target.form_key)},
        {"name", qs(edit.target.name.empty() ? edit.target.editor_id : edit.target.name)},
        {"editorId", qs(edit.target.editor_id)},
        {"piecesCount", edit.outfit ? static_cast<int>(edit.outfit->pieces.size()) : 0},
        {"isCurrent", edit.target.form_key == target_.form_key}});
  }
  return out;
}

int OutfitEditor::project_npc_count() const {
  return project_ ? static_cast<int>(project_->npcs.size()) : 0;
}

void OutfitEditor::setTarget(const QVariantMap& npc) {
  ++validation_serial_;
  ++preset_serial_;
  validating_ = false;
  const auto summary = npc.value("summary").toMap();
  target_ = {ss(summary.value("formKey").toString()), ss(summary.value("editorId").toString()),
             ss(summary.value("name").toString()), ss(summary.value("winningPlugin").toString()),
             summary.value("overrideCount").toInt()};
  current_.clear();
  for (const auto& a : npc.value("defaultOutfit").toMap().value("armors").toList()) current_.push_back(piece_from(a.toMap()));

  // The studio has already reset the shared draft for this NPC (face, body, history).
  draft_.set_initial_outfit(current_);
  if (!pending_project_pieces_.empty()) {
    draft_.outfit().assign(std::move(pending_project_pieces_));
    pending_project_pieces_.clear();
  } else if (project_) {
    auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                           [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
    if (it != project_->npcs.end() && it->outfit) draft_.outfit().assign(it->outfit->pieces);
  }
  last_pieces_.clear();
  validation_.clear();
  emit validationChanged();
  emit targetChanged();
  after_change();
}

void OutfitEditor::push(std::unique_ptr<projects::Command> command) {
  try {
    history_->push(std::move(command));  // emits EditHistory::changed -> after_change()
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::after_change() {
  emit changed();
  if (target_.form_key.empty()) return;
  const bool pieces_changed = draft_.outfit().pieces() != last_pieces_;
  if (pieces_changed) {
    last_pieces_ = draft_.outfit().pieces();
    ++validation_serial_;  // discard a response for the previous set of pieces
    validation_.clear();
    validating_ = true;
    emit validationChanged();
  }
  if (pieces_changed || validation_.isEmpty()) {
    debounce_.start();
  }
}

void OutfitEditor::createNewOutfit(const QString& name) {
  const auto effective_name = name.trimmed().isEmpty() ? QStringLiteral("Nuevo Outfit") : name.trimmed();
  target_ = {
    .form_key = "000A2C8E:Skyrim.esm",
    .editor_id = "SNS_" + ss(effective_name).substr(0, 16),
    .name = ss(effective_name),
    .winning_plugin = "Skyrim.esm",
    .override_count = 1
  };
  current_.clear();
  draft_.set_initial_outfit({});
  draft_.outfit().clear();
  history_->clear();
  after_change();
  emit targetChanged();
  emit changed();
}

void OutfitEditor::addPiece(const QVariantMap& armor, bool replace_conflicting) {
  if (target_.form_key.empty()) {
    target_ = {
      .form_key = "000A2C8E:Skyrim.esm",
      .editor_id = "CustomStandaloneOutfit",
      .name = "Outfit personalizado",
      .winning_plugin = "Skyrim.esm",
      .override_count = 1
    };
    emit targetChanged();
  }
  try {
    push(outfit::make_add_piece(draft_.outfit(), piece_from(armor), replace_conflicting));
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::removePiece(const QString& form_key) {
  try {
    push(outfit::make_remove_piece(draft_.outfit(), ss(form_key)));
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::resetToCurrent() { push(outfit::make_set_pieces(draft_.outfit(), current_, "Restaurar atuendo actual")); }

void OutfitEditor::clearPieces() { push(outfit::make_set_pieces(draft_.outfit(), {}, "Quitar todas las piezas")); }

void OutfitEditor::undo() { history_->undo(); }

void OutfitEditor::redo() { history_->redo(); }

void OutfitEditor::exportStandaloneOutfit(const QString& name, const QString& plugin_name, const QString& output_dir_or_url,
                                          bool esl, bool zip, const QString& description) {
  exportPatch(plugin_name, output_dir_or_url, esl, zip, description, false, false, true, name);
}

QVariantList OutfitEditor::asset_issues(const Json& validation) const {
  QVariantList issues;
  const auto snap = snapshot_();
  if (!snap) return issues;
  for (const auto& piece : validation.value("pieces", Json::array())) {
    for (const auto& mesh : piece.value("meshes", Json::array())) {
      // ARMA model paths are relative to Data\meshes.
      const auto data_path = "meshes\\" + mesh.get<std::string>();
      if (snap->vfs.resolve(data_path)) continue;
      issues << QVariantMap{
          {"severity", "warning"},
          {"code", "MeshNotLoose"},
          {"message", tr("La malla '%1' no existe como archivo suelto en el perfil. Puede estar dentro de un BSA "
                         "(aún no indexado) o faltar.").arg(qs(data_path))},
          {"subject", qs(piece["armor"].value("formKey", ""))}};
    }
  }
  return issues;
}

void OutfitEditor::validate() {
  if (target_.form_key.empty()) return;
  Json armors = Json::array();
  for (const auto& p : draft_.outfit().pieces()) armors.push_back(p.form_key);
  validating_ = true;
  const int serial = ++validation_serial_;
  emit validationChanged();
  worker_->call(method::kValidateOutfit, Json{{"npcFormKey", target_.form_key}, {"armorFormKeys", armors}}, 30s, this,
                [this, serial](const Json& r) {
                  if (serial != validation_serial_) return;
                  auto result = to_map(r);
                  auto issues = result.value("issues").toList();
                  issues.append(asset_issues(r));
                  for (const auto& piece : draft_.outfit().pieces()) {
                    if (!piece.material) continue;
                    issues.append(QVariantMap{{"severity", "error"},
                                              {"code", "MaterialVariantPreviewOnly"},
                                              {"message", tr("La variante de material de %1 solo se puede previsualizar: "
                                                             "faltan DDS generados y un mapeo comprobado a las formas NIF. "
                                                             "Restáurala antes de exportar.").arg(qs(piece.form_key))},
                                              {"subject", qs(piece.form_key)}});
                    result["exportable"] = false;
                  }
                  result["issues"] = issues;
                  validation_ = result;
                  validating_ = false;
                  emit validationChanged();
                },
                [this, serial](int, const QString& message, const Json&) {
                  if (serial != validation_serial_) return;
                  validation_ = {{"exportable", false},
                                 {"issues", QVariantList{QVariantMap{{"severity", "error"}, {"code", "WorkerError"}, {"message", message}}}}};
                  validating_ = false;
                  emit validationChanged();
                });
}

void OutfitEditor::exportPatch(const QString& plugin_name, const QString& output_dir_or_url, bool esl, bool zip,
                               const QString& description, bool also_set_sleeping,
                               bool export_all_project_npcs, bool standalone, const QString& standalone_name) {
  if (exporting_ || target_.form_key.empty()) return;
  auto has_material = [](const auto& pieces) {
    return std::any_of(pieces.begin(), pieces.end(), [](const auto& p) { return p.material.has_value(); });
  };
  bool material_pending = has_material(draft_.outfit().pieces());
  if (!standalone && export_all_project_npcs && project_ && project_->npcs.size() > 1) {
    for (const auto& edit : project_->npcs) {
      if (edit.target.form_key != target_.form_key && edit.outfit && has_material(edit.outfit->pieces)) material_pending = true;
    }
  }
  if (material_pending) {
    emit exportFailed(tr("Hay variantes de material solo previsualizables. Restáuralas antes de exportar."),
                      QVariantList{QVariantMap{{"severity", "error"}, {"code", "MaterialVariantPreviewOnly"},
                                               {"message", tr("Faltan DDS generados y una asignación comprobada de formas NIF.")}}});
    return;
  }
  const auto output = QDir::toNativeSeparators(local_path(output_dir_or_url));
  const auto snap = snapshot_();
  if (snap && (is_inside(output, snap->config.overwrite_dir) || is_inside(output, snap->game.root))) {
    emit exportFailed(tr("No se exporta dentro de Overwrite ni de la carpeta del juego. Elige otra carpeta."), {});
    return;
  }
  QDir().mkpath(output);

  auto build_assignment = [&](const std::string& npc_form_key, const std::vector<projects::OutfitPieceRef>& pieces, bool sleeping) {
    Json armors = Json::array();
    Json materials = Json::array();
    for (const auto& p : pieces) {
      armors.push_back(p.form_key);
      if (p.material.has_value()) {
        Json mat{{"armorFormKey", p.form_key}};
        if (!p.material->diffuse_override.empty()) mat["diffusePath"] = p.material->diffuse_override;
        if (!p.material->normal_override.empty()) mat["normalPath"] = p.material->normal_override;
        if (!p.material->name.empty()) mat["materialName"] = p.material->name;
        if (p.material->tint_color.has_value()) {
          mat["customTintColor"] = {
              {"r", p.material->tint_color->r},
              {"g", p.material->tint_color->g},
              {"b", p.material->tint_color->b}};
        }
        materials.push_back(std::move(mat));
      }
    }
    Json asgn{{"npcFormKey", npc_form_key}, {"armorFormKeys", armors}, {"alsoSetSleepingOutfit", sleeping}};
    if (!materials.empty()) asgn["materialOverrides"] = materials;
    return asgn;
  };

  Json assignments = Json::array();
  if (!standalone && export_all_project_npcs && project_ && project_->npcs.size() > 1) {
    auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                           [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
    projects::NpcEdit current_edit;
    current_edit.target = target_;
    current_edit.outfit = projects::OutfitConfig{{}, draft_.outfit().pieces(), also_set_sleeping};
    if (it != project_->npcs.end()) {
      *it = std::move(current_edit);
    } else {
      project_->npcs.push_back(std::move(current_edit));
    }

    for (const auto& edit : project_->npcs) {
      if (!edit.outfit || edit.outfit->pieces.empty()) continue;
      assignments.push_back(build_assignment(
          edit.target.form_key, edit.outfit->pieces,
          (edit.target.form_key == target_.form_key) ? also_set_sleeping : edit.outfit->also_sleeping));
    }
  } else if (!standalone) {
    assignments.push_back(build_assignment(target_.form_key, draft_.outfit().pieces(), also_set_sleeping));
  }

  // plugin file name -> MO2 mod, documented in the generated README/manifest.
  Json providers = Json::object();
  if (snap) {
    for (const auto& p : snap->load_order.active) providers[p.name] = p.provider;
  }

  exporting_ = true;
  emit exportingChanged();
  console_->append("info", "export", tr("Exportando %1 (%2 asignaciones de NPC) en %3…")
                                         .arg(plugin_name).arg(assignments.size()).arg(output));
  Json standalone_outfits = Json::array();
  if (standalone) {
    Json armors = Json::array();
    Json materials = Json::array();
    for (const auto& p : draft_.outfit().pieces()) {
      armors.push_back(p.form_key);
      if (p.material) {
        Json mat{{"armorFormKey", p.form_key}};
        if (!p.material->diffuse_override.empty()) mat["diffusePath"] = p.material->diffuse_override;
        if (!p.material->normal_override.empty()) mat["normalPath"] = p.material->normal_override;
        if (!p.material->name.empty()) mat["materialName"] = p.material->name;
        if (p.material->tint_color) {
          mat["customTintColor"] = {{"r", p.material->tint_color->r}, {"g", p.material->tint_color->g}, {"b", p.material->tint_color->b}};
        }
        materials.push_back(std::move(mat));
      }
    }
    standalone_outfits.push_back(Json{{"name", ss(standalone_name.trimmed())}, {"armorFormKeys", armors}, {"materialOverrides", materials}});
  }
  worker_->call(
      method::kExportOutfitPatch,
      Json{{"pluginName", ss(plugin_name)},
           {"outputDirectory", ss(output)},
           {"eslFlag", esl},
           {"createZip", zip},
           {"description", ss(description)},
           {"pluginProviders", providers},
           {"assignments", assignments},
           {"standaloneOutfits", standalone_outfits}},
      5min, this,
      [this](const Json& r) {
        exporting_ = false;
        emit exportingChanged();
        const auto result = to_map(r);
        console_->append("info", "export", tr("Mod exportado: %1 (SHA-256 %2…). Masters: %3")
                                             .arg(result.value("pluginPath").toString(),
                                                  result.value("pluginSha256").toString().left(12),
                                                  result.value("masters").toStringList().join(", ")));
        if (project_) {
          project_->history.push_back({projects::utc_now_iso8601(), "Exported " + ss(result.value("pluginPath").toString())});
        }
        emit exported(result);
      },
      [this](int, const QString& message, const Json& data) {
        exporting_ = false;
        emit exportingChanged();
        console_->append("error", "export", tr("Exportación cancelada: %1").arg(message));
        emit exportFailed(message, to_list(data.is_object() ? data.value("issues", Json::array()) : Json::array()));
      });
}

void OutfitEditor::addCurrentToProject() {
  if (target_.form_key.empty()) return;
  if (!project_) {
    const auto snap = snapshot_();
    projects::EnvironmentIdentity env;
    if (snap) {
      env = {snap->config.info.name, text::path_to_utf8(snap->config.info.ini), snap->profile.name,
             mo2::to_string(snap->game.edition)};
    }
    project_ = projects::ProjectStore::create("Proyecto", env);
  }
  auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                         [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
  projects::NpcEdit edit;
  edit.target = target_;
  edit.outfit = projects::OutfitConfig{{}, draft_.outfit().pieces(), false};
  if (face_provider_) {
    edit.face = face_provider_(target_.form_key);
  }
  if (it != project_->npcs.end()) {
    *it = std::move(edit);
  } else {
    project_->npcs.push_back(std::move(edit));
  }
  emit projectChanged();
  console_->append("info", "project", tr("NPC añadido al proyecto: %1 (Total: %2)")
                                          .arg(npc_label()).arg(project_->npcs.size()));
}

void OutfitEditor::removeNpcFromProject(const QString& form_key) {
  if (!project_) return;
  const auto fk = ss(form_key);
  auto it = std::remove_if(project_->npcs.begin(), project_->npcs.end(),
                           [&fk](const projects::NpcEdit& e) { return e.target.form_key == fk; });
  if (it != project_->npcs.end()) {
    project_->npcs.erase(it, project_->npcs.end());
    emit projectChanged();
    console_->append("info", "project", tr("NPC eliminado del proyecto: %1").arg(form_key));
  }
}

void OutfitEditor::loadNpcFromProject(const QString& form_key) {
  if (!project_) return;
  const auto fk = ss(form_key);
  auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                         [&fk](const projects::NpcEdit& e) { return e.target.form_key == fk; });
  if (it != project_->npcs.end()) {
    if (it->outfit) {
      pending_project_pieces_ = it->outfit->pieces;
    }
    if (face_loaded_handler_) {
      face_loaded_handler_(it->target.form_key, it->face);
    }
    emit projectOpened(form_key);
  }
}

bool OutfitEditor::saveProject(const QString& folder_or_url, const QString& name) {
  if (target_.form_key.empty()) return false;
  auto folder = local_path(folder_or_url);
  if (!folder.endsWith(projects::kProjectExtension)) {
    folder = QDir(folder).filePath((name.isEmpty() ? QString("Proyecto") : name) + projects::kProjectExtension);
  }
  try {
    const auto snap = snapshot_();
    if (!project_) {
      projects::EnvironmentIdentity env;
      if (snap) {
        env = {snap->config.info.name, text::path_to_utf8(snap->config.info.ini), snap->profile.name,
               mo2::to_string(snap->game.edition)};
      }
      project_ = projects::ProjectStore::create(ss(name.isEmpty() ? QFileInfo(folder).completeBaseName() : name), env);
    }
    auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                           [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
    projects::NpcEdit edit;
    edit.target = target_;
    edit.outfit = projects::OutfitConfig{{}, draft_.outfit().pieces(), false};
    if (face_provider_) {
      edit.face = face_provider_(target_.form_key);
    }
    if (it != project_->npcs.end()) {
      *it = std::move(edit);
    } else {
      project_->npcs.push_back(std::move(edit));
    }
    project_->export_options.plugin_name = ss(suggested_plugin_name());
    project_->history.push_back({projects::utc_now_iso8601(), "Saved (" + std::to_string(project_->npcs.size()) + " NPCs)"});
    projects::ProjectStore::save(std::filesystem::path(folder.toStdWString()), *project_);
    history_->mark_clean();
    project_path_ = folder;
    emit projectChanged();
    emit changed();
    console_->append("info", "project", tr("Proyecto guardado en %1 (%2 NPCs)").arg(folder).arg(project_->npcs.size()));
    return true;
  } catch (const std::exception& ex) {
    console_->append("error", "project", tr("No se pudo guardar el proyecto: %1").arg(qs(ex.what())));
    return false;
  }
}

bool OutfitEditor::openProject(const QString& folder_or_url) {
  const auto folder = local_path(folder_or_url);
  try {
    auto project = projects::ProjectStore::load(std::filesystem::path(folder.toStdWString()));
    if (project.npcs.empty()) throw Error(ErrorKind::Parse, "the project has no NPC");
    const auto snap = snapshot_();
    if (snap && project.environment.profile != snap->profile.name) {
      console_->append("warning", "project",
                       tr("El proyecto se creó con el perfil '%1'; se resolverá contra el perfil actual '%2'.")
                           .arg(qs(project.environment.profile), qs(snap->profile.name)));
    }
    const auto& edit = project.npcs.front();
    pending_project_pieces_ = edit.outfit ? edit.outfit->pieces : std::vector<outfit::Piece>{};
    if (face_loaded_handler_) {
      face_loaded_handler_(edit.target.form_key, edit.face);
    }
    project_ = std::move(project);
    project_path_ = folder;
    emit projectChanged();
    console_->append("info", "project", tr("Proyecto abierto: %1").arg(folder));
    emit projectOpened(qs(edit.target.form_key));
    return true;
  } catch (const std::exception& ex) {
    console_->append("error", "project", tr("No se pudo abrir el proyecto: %1").arg(qs(ex.what())));
    return false;
  }
}

QVariantList OutfitEditor::outfit_presets() const {
  QVariantList list;
  const auto builtin = outfit::builtin_outfit_presets();
  for (const auto& p : outfit::load_outfit_presets_from_dir(preset_directory())) {
    const bool is_builtin = std::any_of(builtin.begin(), builtin.end(), [&](const auto& b) { return b.id == p.id; });
    list.push_back(QVariantMap{
        {"id", qs(p.id)},
        {"name", qs(p.name)},
        {"category", qs(p.category)},
        {"description", qs(p.description)},
        {"builtin", is_builtin},
        {"favorite", p.favorite},
        {"pieceCount", static_cast<int>(p.pieces.size())}});
  }
  return list;
}

void OutfitEditor::applyPreset(const QString& preset_id) {
  if (target_.form_key.empty()) return;
  const auto presets = outfit::load_outfit_presets_from_dir(preset_directory());
  auto it = std::find_if(presets.begin(), presets.end(),
                         [&](const outfit::OutfitPreset& p) { return p.id == ss(preset_id); });
  if (it == presets.end()) return;
  const auto preset = *it;
  Json keys = Json::array();
  for (const auto& p : preset.pieces) keys.push_back(p.form_key);
  const int serial = ++preset_serial_;
  const auto npc_key = target_.form_key;
  worker_->call(method::kValidateOutfit, Json{{"npcFormKey", npc_key}, {"armorFormKeys", keys}}, 30s, this,
    [this, serial, npc_key, preset](const Json& result) {
      if (serial != preset_serial_ || target_.form_key != npc_key) return;
      std::vector<outfit::Piece> resolved;
      for (const auto& row : result.value("pieces", Json::array())) {
        const auto armor = to_map(row.value("armor", Json::object()));
        auto piece = piece_from(armor);
        auto saved = std::find_if(preset.pieces.begin(), preset.pieces.end(), [&](const auto& p) { return p.form_key == piece.form_key; });
        if (saved != preset.pieces.end()) piece.material = saved->material;
        if (!piece.form_key.empty()) resolved.push_back(std::move(piece));
      }
      if (resolved.size() != preset.pieces.size()) {
        console_->append("error", "outfit", tr("El preset '%1' contiene FormKeys ausentes del load order activo.").arg(qs(preset.name)));
        return;
      }
      push(outfit::make_set_pieces(draft_.outfit(), std::move(resolved), "Aplicar preset: " + preset.name));
    },
    [this, serial](int, const QString& message, const Json&) {
      if (serial == preset_serial_) console_->append("error", "outfit", tr("No se pudo resolver el preset: %1").arg(message));
    });
}

bool OutfitEditor::saveCurrentPreset(const QString& name) {
  if (name.trimmed().isEmpty() || !exportable()) return false;
  outfit::OutfitPreset preset;
  preset.id = "custom_" + ss(QUuid::createUuid().toString(QUuid::WithoutBraces));
  preset.name = ss(name.trimmed());
  preset.category = "Custom";
  preset.pieces = draft_.outfit().pieces();
  if (!outfit::save_outfit_preset_to_file(preset_file(qs(preset.id)), preset)) return false;
  emit presetsChanged();
  return true;
}

bool OutfitEditor::duplicatePreset(const QString& preset_id, const QString& name) {
  if (name.trimmed().isEmpty()) return false;
  const auto presets = outfit::load_outfit_presets_from_dir(preset_directory());
  const auto it = std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.id == ss(preset_id); });
  if (it == presets.end()) return false;
  auto copy = *it;
  copy.id = "custom_" + ss(QUuid::createUuid().toString(QUuid::WithoutBraces));
  copy.name = ss(name.trimmed());
  copy.category = "Custom";
  copy.favorite = false;
  if (!outfit::save_outfit_preset_to_file(preset_file(qs(copy.id)), copy)) return false;
  emit presetsChanged();
  return true;
}

bool OutfitEditor::renamePreset(const QString& preset_id, const QString& name) {
  if (!custom_preset_id(preset_id) || name.trimmed().isEmpty()) return false;
  const auto path = preset_file(preset_id);
  if (!std::filesystem::exists(path)) return false;
  const auto presets = outfit::load_outfit_presets_from_dir(preset_directory());
  const auto it = std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.id == ss(preset_id); });
  if (it == presets.end()) return false;
  auto updated = *it;
  updated.name = ss(name.trimmed());
  if (!outfit::save_outfit_preset_to_file(path, updated)) return false;
  emit presetsChanged();
  return true;
}

bool OutfitEditor::deletePreset(const QString& preset_id) {
  if (!custom_preset_id(preset_id)) return false;
  std::error_code ec;
  if (!std::filesystem::remove(preset_file(preset_id), ec) || ec) return false;
  emit presetsChanged();
  return true;
}

bool OutfitEditor::setPresetFavorite(const QString& preset_id, bool favorite) {
  if (!custom_preset_id(preset_id)) return false;
  const auto path = preset_file(preset_id);
  if (!std::filesystem::exists(path)) return false;
  const auto presets = outfit::load_outfit_presets_from_dir(preset_directory());
  const auto it = std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.id == ss(preset_id); });
  if (it == presets.end()) return false;
  auto updated = *it;
  updated.favorite = favorite;
  if (!outfit::save_outfit_preset_to_file(path, updated)) return false;
  emit presetsChanged();
  return true;
}

QVariantMap OutfitEditor::compatibility_report() const {
  std::vector<std::string> active_plugins;
  std::vector<std::string> active_paths;
  const auto snap = snapshot_();
  if (snap) {
    for (const auto& p : snap->load_order.active) active_plugins.push_back(p.name);
  }

  const auto report = appearance::CompatibilityGuardian::evaluate(draft_, std::nullopt, active_plugins, active_paths);
  QVariantMap out;
  out["badge"] = (report.badge == appearance::HealthBadge::Verified) ? "verified"
                 : (report.badge == appearance::HealthBadge::ExportableWarning) ? "warning" : "error";
  // Guardian currently receives plugin names but no inventory of winning body meshes. Its body
  // classification is advisory; record and asset validation happens separately before export.
  out["badgeText"] = (report.badge == appearance::HealthBadge::Verified) ? tr("Preliminar: sin errores detectados")
                     : (report.badge == appearance::HealthBadge::ExportableWarning) ? tr("Preliminar: advertencias") : tr("Error Bloqueante");
  out["verificationLevel"] = "preliminary";
  out["summary"] = qs(report.summary);
  out["canExport"] = report.can_export;
  out["errorCount"] = report.error_count();
  out["warningCount"] = report.warning_count();

  QVariantMap body;
  body["name"] = qs(report.body_inspection.body_name);
  body["supportsWeightSliders"] = report.body_inspection.supports_weight_sliders;
  body["physicsEnabled"] = report.body_inspection.physics_enabled;
  body["details"] = qs(report.body_inspection.details);
  out["body"] = body;

  QVariantMap smp_runtime{{"file", "SKSE/Plugins/hdtSMP64.dll"}, {"present", false}, {"provider", QString()}};
  if (snap) {
    if (const auto runtime = snap->vfs.resolve("SKSE/Plugins/hdtSMP64.dll")) {
      smp_runtime["present"] = true;
      if (runtime->provider < snap->vfs.providers().size())
        smp_runtime["provider"] = qs(snap->vfs.providers()[runtime->provider].name);
    }
  }
  out["smpRuntime"] = smp_runtime;

  QVariantList issues;
  for (const auto& issue : report.issues) {
    issues.push_back(QVariantMap{
        {"code", qs(issue.code)},
        {"severity", qs(issue.severity_string())},
        {"component", qs(issue.component_string())},
        {"message", qs(issue.message)},
        {"target", qs(issue.target)},
        {"suggestedAction", qs(issue.suggested_action)}});
  }
  out["issues"] = issues;
  return out;
}

void OutfitEditor::setPieceMaterial(const QString& form_key, const QColor& tint_color, const QString& diffuse_path,
                                    const QString& normal_path, const QString& material_name) {
  auto pieces = draft_.outfit().pieces();
  auto it = std::find_if(pieces.begin(), pieces.end(), [&](const auto& p) { return p.form_key == ss(form_key); });
  if (it == pieces.end()) return;

  projects::PieceMaterialOverride mat;
  mat.name = ss(material_name.isEmpty() ? "CustomMaterial" : material_name);
  if (tint_color.isValid()) {
    mat.tint_color = projects::ColorRgb{
        static_cast<std::uint8_t>(tint_color.red()),
        static_cast<std::uint8_t>(tint_color.green()),
        static_cast<std::uint8_t>(tint_color.blue())};
  }
  mat.diffuse_override = ss(diffuse_path);
  mat.normal_override = ss(normal_path);
  it->material = std::move(mat);

  history_->push(outfit::make_set_pieces(draft_.outfit(), std::move(pieces), "Personalizar material: " + ss(form_key)));
}

void OutfitEditor::clearPieceMaterial(const QString& form_key) {
  auto pieces = draft_.outfit().pieces();
  auto it = std::find_if(pieces.begin(), pieces.end(), [&](const auto& p) { return p.form_key == ss(form_key); });
  if (it == pieces.end() || !it->material.has_value()) return;

  it->material.reset();
  history_->push(outfit::make_set_pieces(draft_.outfit(), std::move(pieces), "Restaurar material original: " + ss(form_key)));
}

}  // namespace sns::app
