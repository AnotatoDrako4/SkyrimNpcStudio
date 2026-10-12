#include "outfit_editor.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

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

}  // namespace

OutfitEditor::OutfitEditor(WorkerClient* worker, ConsoleModel* console, SnapshotProvider snapshot, QObject* parent)
    : QObject(parent), worker_(worker), console_(console), snapshot_(std::move(snapshot)) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(250);
  connect(&debounce_, &QTimer::timeout, this, &OutfitEditor::validate);
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
  for (const auto& p : draft_.pieces()) {
    QStringList slot_list;
    for (int s : outfit::slots_of(p.slot_mask)) slot_list << QString::number(s);
    out << QVariantMap{{"formKey", qs(p.form_key)}, {"name", qs(p.name)}, {"plugin", qs(p.plugin)},
                       {"slotMask", p.slot_mask}, {"slots", slot_list.join(", ")}};
  }
  return out;
}

QVariantList OutfitEditor::local_conflicts() const {
  QVariantList out;
  for (const auto& c : draft_.conflicts()) {
    out << QVariantMap{{"slot", c.slot}, {"first", qs(c.first)}, {"second", qs(c.second)}};
  }
  return out;
}

QStringList OutfitEditor::history() const {
  QStringList out;
  for (const auto& h : stack_.history()) out << qs(h);
  return out;
}

bool OutfitEditor::exportable() const {
  return !validating_ && validation_.value("exportable").toBool() && !draft_.pieces().empty();
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
  const auto summary = npc.value("summary").toMap();
  target_ = {ss(summary.value("formKey").toString()), ss(summary.value("editorId").toString()),
             ss(summary.value("name").toString()), ss(summary.value("winningPlugin").toString()),
             summary.value("overrideCount").toInt()};
  current_.clear();
  for (const auto& a : npc.value("defaultOutfit").toMap().value("armors").toList()) current_.push_back(piece_from(a.toMap()));

  stack_.clear();
  if (!pending_project_pieces_.empty()) {
    draft_.assign(std::move(pending_project_pieces_));
    pending_project_pieces_.clear();
  } else if (project_) {
    auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                           [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
    if (it != project_->npcs.end() && it->outfit) {
      draft_.assign(it->outfit->pieces);
    } else {
      draft_.assign(current_);
    }
  } else {
    draft_.assign(current_);
  }
  stack_.mark_clean();
  validation_.clear();
  emit targetChanged();
  after_change();
}

void OutfitEditor::push(std::unique_ptr<projects::Command> command) {
  try {
    stack_.push(std::move(command));
    after_change();
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::after_change() {
  emit changed();
  if (!target_.form_key.empty()) debounce_.start();
}

void OutfitEditor::addPiece(const QVariantMap& armor, bool replace_conflicting) {
  if (target_.form_key.empty()) return;
  try {
    push(outfit::make_add_piece(draft_, piece_from(armor), replace_conflicting));
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::removePiece(const QString& form_key) {
  try {
    push(outfit::make_remove_piece(draft_, ss(form_key)));
  } catch (const std::exception& ex) {
    console_->append("warning", "outfit", qs(ex.what()));
  }
}

void OutfitEditor::resetToCurrent() { push(outfit::make_set_pieces(draft_, current_, "Restore current outfit")); }

void OutfitEditor::clearPieces() { push(outfit::make_set_pieces(draft_, {}, "Remove all pieces")); }

void OutfitEditor::undo() {
  if (stack_.undo()) after_change();
}

void OutfitEditor::redo() {
  if (stack_.redo()) after_change();
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
  for (const auto& p : draft_.pieces()) armors.push_back(p.form_key);
  validating_ = true;
  const int serial = ++validation_serial_;
  emit validationChanged();
  worker_->call(method::kValidateOutfit, Json{{"npcFormKey", target_.form_key}, {"armorFormKeys", armors}}, 30s, this,
                [this, serial](const Json& r) {
                  if (serial != validation_serial_) return;
                  auto result = to_map(r);
                  auto issues = result.value("issues").toList();
                  issues.append(asset_issues(r));
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
                               bool export_all_project_npcs) {
  if (exporting_ || target_.form_key.empty()) return;
  const auto output = QDir::toNativeSeparators(local_path(output_dir_or_url));
  const auto snap = snapshot_();
  if (snap && (is_inside(output, snap->config.overwrite_dir) || is_inside(output, snap->game.root))) {
    emit exportFailed(tr("No se exporta dentro de Overwrite ni de la carpeta del juego. Elige otra carpeta."), {});
    return;
  }
  QDir().mkpath(output);

  Json assignments = Json::array();
  if (export_all_project_npcs && project_ && project_->npcs.size() > 1) {
    auto it = std::find_if(project_->npcs.begin(), project_->npcs.end(),
                           [this](const projects::NpcEdit& e) { return e.target.form_key == target_.form_key; });
    projects::NpcEdit current_edit;
    current_edit.target = target_;
    current_edit.outfit = projects::OutfitConfig{{}, draft_.pieces(), also_set_sleeping};
    if (it != project_->npcs.end()) {
      *it = std::move(current_edit);
    } else {
      project_->npcs.push_back(std::move(current_edit));
    }

    for (const auto& edit : project_->npcs) {
      if (!edit.outfit || edit.outfit->pieces.empty()) continue;
      Json armors = Json::array();
      for (const auto& p : edit.outfit->pieces) armors.push_back(p.form_key);
      assignments.push_back(
          {{"npcFormKey", edit.target.form_key},
           {"armorFormKeys", armors},
           {"alsoSetSleepingOutfit", (edit.target.form_key == target_.form_key) ? also_set_sleeping : edit.outfit->also_sleeping}});
    }
  } else {
    Json armors = Json::array();
    for (const auto& p : draft_.pieces()) armors.push_back(p.form_key);
    assignments.push_back(
        {{"npcFormKey", target_.form_key},
         {"armorFormKeys", armors},
         {"alsoSetSleepingOutfit", also_set_sleeping}});
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
  worker_->call(
      method::kExportOutfitPatch,
      Json{{"pluginName", ss(plugin_name)},
           {"outputDirectory", ss(output)},
           {"eslFlag", esl},
           {"createZip", zip},
           {"description", ss(description)},
           {"pluginProviders", providers},
           {"assignments", assignments}},
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
  edit.outfit = projects::OutfitConfig{{}, draft_.pieces(), false};
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
    edit.outfit = projects::OutfitConfig{{}, draft_.pieces(), false};
    if (it != project_->npcs.end()) {
      *it = std::move(edit);
    } else {
      project_->npcs.push_back(std::move(edit));
    }
    project_->export_options.plugin_name = ss(suggested_plugin_name());
    project_->history.push_back({projects::utc_now_iso8601(), "Saved (" + std::to_string(project_->npcs.size()) + " NPCs)"});
    projects::ProjectStore::save(std::filesystem::path(folder.toStdWString()), *project_);
    stack_.mark_clean();
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

}  // namespace sns::app
