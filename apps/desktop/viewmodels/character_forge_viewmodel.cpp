#include "character_forge_viewmodel.hpp"

#include <QRegularExpression>
#include <QUrl>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include "sns/ipc/protocol.hpp"
#include <chrono>

namespace sns::app {
namespace {

QString local_path(const QString& path) {
  const QUrl url(path);
  return url.isLocalFile() ? url.toLocalFile() : path;
}

QString safe_stem(QString name) {
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), "_");
  return name.isEmpty() ? QStringLiteral("CharacterForge") : name.left(80);
}

}  // namespace

CharacterForgeViewModel::CharacterForgeViewModel(EnvironmentProvider environment, WorkerClient* worker, QObject* parent)
    : QObject(parent), environment_(std::move(environment)), worker_(worker) {
  connect(worker_, &WorkerClient::stateChanged, this, [this] { set_catalog_ready(false); });
}

QString CharacterForgeViewModel::project_name() const {
  return has_project_ ? QString::fromStdString(draft_.current().name) : QString();
}
QString CharacterForgeViewModel::character_name() const {
  return has_project_ ? QString::fromStdString(draft_.current().npc.name) : QString();
}
QString CharacterForgeViewModel::editor_id() const {
  return has_project_ ? QString::fromStdString(draft_.current().npc.editor_id) : QString();
}
QString CharacterForgeViewModel::description() const {
  return has_project_ ? QString::fromStdString(draft_.current().description) : QString();
}

bool CharacterForgeViewModel::newProject(const QString& name) {
  if (busy_) return false;
  if (dirty()) {
    last_error_ = tr("Guarda el proyecto actual antes de crear otro.");
    emit changed();
    return false;
  }
  try {
    draft_.replace(creation::ForgeProjectStore::create(name.toStdString(), environment_()));
    invalidate_validation();
    has_project_ = true;
    project_path_.clear();
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

bool CharacterForgeViewModel::openProject(const QString& folder_or_url) {
  if (busy_) return false;
  if (dirty()) {
    last_error_ = tr("Guarda el proyecto actual antes de abrir otro.");
    emit changed();
    return false;
  }
  try {
    const auto folder = std::filesystem::path(local_path(folder_or_url).toStdWString());
    auto project = creation::ForgeProjectStore::load(folder);
    draft_.replace(std::move(project));
    invalidate_validation();
    has_project_ = true;
    project_path_ = QString::fromStdWString(folder.wstring());
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

bool CharacterForgeViewModel::saveProject(const QString& folder_or_url) {
  if (!has_project_ || busy_) return false;
  try {
    auto folder = std::filesystem::path(local_path(folder_or_url).toStdWString());
    if (folder.extension() == L".snps")
      throw std::invalid_argument("Cannot create Character Forge inside an existing .snps project");
    if (folder.extension() != L".sncf") {
      folder /= (safe_stem(project_name()) + QStringLiteral(".sncf")).toStdWString();
    }
    creation::ForgeProjectStore::save(folder, draft_.current());
    draft_.mark_saved();
    project_path_ = QString::fromStdWString(folder.wstring());
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

template <class F> void CharacterForgeViewModel::edit(F&& change) {
  if (!has_project_ || busy_) return;
  try {
    change();
    invalidate_validation();
    last_error_.clear();
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
  }
  emit changed();
}

void CharacterForgeViewModel::setProjectName(const QString& value) {
  edit([&] { draft_.set_project_name(value.toStdString()); });
}
void CharacterForgeViewModel::setCharacterName(const QString& value) {
  edit([&] { draft_.set_character_name(value.toStdString()); });
}
void CharacterForgeViewModel::setEditorId(const QString& value) {
  edit([&] { draft_.set_editor_id(value.toStdString()); });
}
void CharacterForgeViewModel::setDescription(const QString& value) {
  edit([&] { draft_.set_description(value.toStdString()); });
}
void CharacterForgeViewModel::undo() {
  if (has_project_ && !busy_ && draft_.undo()) { invalidate_validation(); emit changed(); }
}
void CharacterForgeViewModel::redo() {
  if (has_project_ && !busy_ && draft_.redo()) { invalidate_validation(); emit changed(); }
}

QVariantMap CharacterForgeViewModel::npc() const {
  return has_project_ ? to_map(creation::native_npc_to_json(draft_.current().npc)) : QVariantMap{};
}
QVariantMap CharacterForgeViewModel::export_options() const {
  return has_project_ ? to_map(draft_.current().export_options) : QVariantMap{};
}
void CharacterForgeViewModel::invalidate_validation() {
  ++operation_serial_;
  if (operation_cancel_) operation_cancel_->store(true);
  busy_ = false;
  validation_.clear();
  emit changed();
}
void CharacterForgeViewModel::set_catalog_ready(bool ready) {
  catalog_ready_ = ready;
  if (!ready) {
    ++search_serial_;
    record_choices_.clear();
    invalidate_validation();
    emit recordsChanged();
  }
  emit changed();
}
void CharacterForgeViewModel::setNpcValue(const QString& field, const QVariant& value) {
  edit([&] {
    auto json = creation::native_npc_to_json(draft_.current().npc);
    if (!json.contains(ss(field))) throw std::invalid_argument("Unknown Native NPC field");
    json[ss(field)] = to_json(value);
    draft_.set_native_npc(creation::native_npc_from_json(json));
  });
}
void CharacterForgeViewModel::setExportOption(const QString& field, const QVariant& value) {
  edit([&] { auto json = draft_.current().export_options; json[ss(field)] = to_json(value); draft_.set_export_options(std::move(json)); });
}
void CharacterForgeViewModel::searchRecords(const QString& kind, const QString& text) {
  if (!catalog_ready_) { last_error_ = tr("Carga e indexa el perfil actual antes de buscar registros."); emit changed(); return; }
  const auto serial = ++search_serial_;
  worker_->call(ipc::protocol::method::kCreationSearchRecords, Json{{"kind", ss(kind)}, {"text", ss(text)}, {"limit", 100}}, std::chrono::seconds(30), this,
    [this, serial](const Json& r) { if (serial != search_serial_) return; record_choices_ = to_list(r); emit recordsChanged(); },
    [this, serial](int, const QString& error, const Json&) { if (serial != search_serial_) return; record_choices_.clear(); last_error_ = error; emit recordsChanged(); emit changed(); });
}
void CharacterForgeViewModel::validateNpc() {
  if (!has_project_ || busy_ || !catalog_ready_) return;
  busy_ = true; last_error_.clear();
  const auto serial = ++operation_serial_;
  emit changed();
  operation_cancel_ = worker_->call(ipc::protocol::method::kCreationValidateNpc,
    Json{{"npc", creation::native_npc_to_json(draft_.current().npc)}, {"pluginName", draft_.current().export_options.value("pluginName", "CharacterForge.esp")}}, std::chrono::seconds(60), this,
    [this, serial](const Json& r) { if (serial != operation_serial_) return; validation_ = to_map(r); busy_ = false; emit changed(); },
    [this, serial](int, const QString& error, const Json&) { if (serial != operation_serial_) return; busy_ = false; last_error_ = error; emit changed(); });
}
void CharacterForgeViewModel::exportTechnical(const QString& folder_or_url) {
  if (!has_project_ || !can_export_technical()) return;
  busy_ = true; last_error_.clear();
  const auto serial = ++operation_serial_;
  auto params = draft_.current().export_options;
  params["npc"] = creation::native_npc_to_json(draft_.current().npc);
  params["technicalOnly"] = true;
  params["outputDirectory"] = ss(local_path(folder_or_url));
  emit changed();
  operation_cancel_ = worker_->call(ipc::protocol::method::kCreationExportNpc, params, std::chrono::minutes(2), this,
    [this, serial](const Json& r) { if (serial != operation_serial_) return; busy_ = false; last_error_ = tr("Plugin técnico verificado: %1. NO instalar; FaceGen pendiente.").arg(qs(r.at("package").at("pluginPath").get<std::string>())); emit changed(); },
    [this, serial](int, const QString& error, const Json&) { if (serial != operation_serial_) return; busy_ = false; last_error_ = error; emit changed(); });
}
void CharacterForgeViewModel::cancelOperation() {
  if (operation_cancel_) operation_cancel_->store(true);
  // Remain busy until cancellation is acknowledged, preventing a racing duplicate export.
}
}  // namespace sns::app
