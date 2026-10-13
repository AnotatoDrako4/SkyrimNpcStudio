#include "character_forge_viewmodel.hpp"

#include <QRegularExpression>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QtEndian>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include "sns/ipc/protocol.hpp"
#include <chrono>
#include <algorithm>

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

template <class F> void CharacterForgeViewModel::edit(F&& change, bool affects_validation) {
  if (!has_project_ || busy_) return;
  try {
    change();
    if (affects_validation) invalidate_validation();
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
QVariantMap CharacterForgeViewModel::workspace() const {
  if (!has_project_) return {};
  auto result = to_map(draft_.current().workspace);
  auto model = result.value("model").toMap();
  if (!model.isEmpty()) { model["available"] = QFileInfo(model.value("path").toString()).isFile(); result["model"] = model; }
  return result;
}
QVariantMap CharacterForgeViewModel::capabilities() const {
  return {{"nativeRecords", true}, {"nativeFaceGen", false}, {"modelPreview", false},
          {"blenderBridge", false}, {"blenderMcp", false}, {"aiGeneration", false}, {"materialExport", false}};
}
void CharacterForgeViewModel::setWorkspaceValue(const QString& field, const QVariant& value) {
  edit([&] { auto w = draft_.current().workspace; w[ss(field)] = to_json(value); draft_.set_workspace(std::move(w)); }, false);
}
void CharacterForgeViewModel::selectModelFile(const QString& url) {
  edit([&] {
    const QFileInfo info(local_path(url));
    const auto extension = info.suffix().toLower();
    if (!info.isFile() || !info.isReadable() || info.size() <= 0 || info.size() > 2LL * 1024 * 1024 * 1024)
      throw std::invalid_argument("Select a readable, nonempty model file smaller than 2 GiB");
    if (!QStringList{"glb", "gltf", "fbx", "obj", "blend"}.contains(extension))
      throw std::invalid_argument("Supported selections: GLB, glTF, FBX, OBJ, BLEND");
    Json model{{"name", ss(info.fileName())}, {"path", ss(info.canonicalFilePath())}, {"format", ss(extension)},
               {"sizeBytes", info.size()}, {"status", "selected-only"}, {"convertedToNif", false}};
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) throw std::invalid_argument("Cannot read selected file");
    QByteArray jsonBytes;
    if (extension == "gltf") {
      if (info.size() > 16 * 1024 * 1024) throw std::invalid_argument("glTF metadata exceeds 16 MiB");
      jsonBytes = file.readAll();
    } else if (extension == "glb") {
      const auto header = file.read(20);
      if (header.size() != 20 || header.left(4) != "glTF") throw std::invalid_argument("Invalid GLB header");
      const auto* data = reinterpret_cast<const uchar*>(header.constData());
      const auto length = qFromLittleEndian<quint32>(data + 12);
      if (qFromLittleEndian<quint32>(data + 4) != 2 || qFromLittleEndian<quint32>(data + 8) != info.size()
          || qFromLittleEndian<quint32>(data + 16) != 0x4E4F534A || length > 16 * 1024 * 1024 || length > info.size() - 20)
        throw std::invalid_argument("Unsupported or truncated GLB metadata");
      jsonBytes = file.read(length);
    } else if (extension == "blend" && file.read(7) != "BLENDER") {
      throw std::invalid_argument("Invalid Blender file header");
    }
    if (!jsonBytes.isEmpty()) {
      const auto metadata = Json::parse(jsonBytes.toStdString());
      if (!metadata.is_object() || !metadata.contains("asset") || metadata.at("asset").value("version", "") != "2.0")
        throw std::invalid_argument("Only glTF 2.0 metadata is supported");
      for (const auto* field : {"meshes", "materials", "textures", "skins"}) {
        const auto values = metadata.value(field, Json::array());
        if (!values.is_array()) throw std::invalid_argument("Malformed glTF array");
        model[field] = values.size();
      }
      model["metadataInspected"] = true;
    } else model["metadataInspected"] = false;
    auto w = draft_.current().workspace;
    w["model"] = model;
    auto library = w.value("library", Json::array());
    if (!library.is_array()) library = Json::array();
    const auto same = std::find_if(library.begin(), library.end(), [&](const Json& entry) { return entry.value("path", "") == model.at("path"); });
    if (same == library.end() && library.size() < 100) library.push_back(model);
    w["library"] = library;
    draft_.set_workspace(std::move(w));
  }, false);
}
void CharacterForgeViewModel::selectBlenderExecutable(const QString& url) {
  edit([&] {
    const QFileInfo info(local_path(url));
    if (!info.isFile() || !info.isReadable() || (!info.isExecutable() && info.suffix().compare("exe", Qt::CaseInsensitive) != 0))
      throw std::invalid_argument("Select an existing Blender executable");
    auto w = draft_.current().workspace;
    w["blender"] = {{"executable", ss(info.canonicalFilePath())}, {"status", "configured-unverified"}};
    draft_.set_workspace(std::move(w));
  }, false);
}
void CharacterForgeViewModel::detectBlender() {
  const auto path = QStandardPaths::findExecutable("blender");
  if (path.isEmpty()) { last_error_ = tr("Blender no encontrado en PATH. Puedes seleccionar el ejecutable; el bridge aún no está implementado."); emit changed(); return; }
  selectBlenderExecutable(path);
}
void CharacterForgeViewModel::selectReferenceImage(const QString& role, const QString& url) {
  edit([&] {
    if (!QStringList{"front", "side", "back"}.contains(role)) throw std::invalid_argument("Unknown reference image role");
    const QFileInfo info(local_path(url));
    QImageReader reader(info.absoluteFilePath());
    if (!info.isFile() || info.size() > 20 * 1024 * 1024 || !reader.canRead() || !reader.size().isValid())
      throw std::invalid_argument("Select a readable reference image up to 20 MiB");
    auto w = draft_.current().workspace;
    auto ai = w.value("ai", Json::object());
    ai[ss(role)] = {{"path", ss(info.canonicalFilePath())}, {"width", reader.size().width()}, {"height", reader.size().height()}};
    w["ai"] = ai;
    draft_.set_workspace(std::move(w));
  }, false);
}
}  // namespace sns::app
