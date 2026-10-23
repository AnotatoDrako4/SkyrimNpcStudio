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
#include "forge_asset_library.hpp"
#include "forge_blender_bridge.hpp"
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
  scene_ = new ForgeSceneViewModel(this);
  blender_bridge_ = new ForgeBlenderBridge(this);
  connect(blender_bridge_, &ForgeBlenderBridge::completed, this, [this](const QString& glb, const QVariantMap& report) {
    try {
      const auto source = draft_.current().workspace.at("model");
      const auto imported = forge_assets::import_model(project_path_, glb);
      Json model{{"name", source.at("name").get<std::string>() + ".glb"},
                 {"path", imported.relative_path.toStdString()}, {"format", "glb"},
                 {"storage", "managed"}, {"sha256", imported.sha256.toStdString()},
                 {"sizeBytes", imported.size_bytes}, {"status", "validated-managed"},
                 {"convertedToNif", false}, {"metadataInspected", true},
                 {"sourceFormat", source.at("format")}, {"sourcePath", source.at("path")},
                 {"blenderVersion", report.value("blenderVersion").toString().toStdString()}};
      auto next = draft_.current().workspace;
      next["model"] = model;
      auto library = next.value("library", Json::array());
      bool found = false;
      for (auto& entry : library) if (entry.is_object() && entry.value("storage", std::string{}) == "managed" &&
                                       entry.value("sha256", std::string{}) == imported.sha256.toStdString()) {
        entry = model; found = true; break;
      }
      if (!found) library.push_back(model);
      next["library"] = library;
      auto blender = next.value("blender", Json::object());
      blender["status"] = "verified";
      blender["version"] = report.value("blenderVersion").toString().toStdString();
      next["blender"] = blender;
      draft_.set_workspace(std::move(next));
      last_error_.clear();
      preview_selected_model();
    } catch (const std::exception& ex) { last_error_ = QString::fromUtf8(ex.what()); }
    busy_ = false;
    emit changed();
  });
  connect(blender_bridge_, &ForgeBlenderBridge::failed, this, [this](const QString& message) {
    busy_ = false; last_error_ = message; emit changed();
  });
  connect(worker_, &WorkerClient::stateChanged, this, [this] { set_catalog_ready(false); });
}

bool CharacterForgeViewModel::blender_busy() const { return blender_bridge_->busy(); }

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
    scene_->reset();
    set_preview({});  // a preview belonged to the previous project's draft
    emit changed();
    refreshVoiceAnalysis();
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
    preview_selected_model();
    set_preview({});
    emit changed();
    refreshVoiceAnalysis();
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
    if (!project_path_.isEmpty() && std::filesystem::weakly_canonical(folder) !=
        std::filesystem::weakly_canonical(std::filesystem::path(project_path_.toStdWString()))) {
      const auto& w = draft_.current().workspace;
      const auto managed = [](const Json& item) { return item.is_object() && item.value("storage", std::string{}) == "managed"; };
      if ((w.contains("model") && managed(w.at("model"))) ||
          (w.contains("library") && std::any_of(w.at("library").begin(), w.at("library").end(), managed)))
        throw std::invalid_argument("Move or copy the complete .sncf folder to keep its managed model assets together");
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
    if (affects_validation) {
      invalidate_validation();
      mark_preview_stale(tr("El borrador cambió después de calcular la vista previa: vuelve a pedirla."));
    }
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
  if (has_project_ && !busy_ && draft_.undo()) {
    invalidate_validation();
    mark_preview_stale(tr("El borrador cambió después de calcular la vista previa: vuelve a pedirla."));
    preview_selected_model();
    emit changed();
    refreshVoiceAnalysis();  // the undone draft may have another voice, sex or factions
  }
}
void CharacterForgeViewModel::redo() {
  if (has_project_ && !busy_ && draft_.redo()) {
    invalidate_validation();
    mark_preview_stale(tr("El borrador cambió después de calcular la vista previa: vuelve a pedirla."));
    preview_selected_model();
    emit changed();
    refreshVoiceAnalysis();
  }
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
    record_search_.clear();
    invalidate_validation();
    reset_behavior();  // evidence, previews and the catalog belong to the profile that was indexed: none of it survives a re-index
    emit recordsChanged();
  }
  emit changed();
  if (ready && has_project_) refreshVoiceAnalysis();
}
void CharacterForgeViewModel::set_profile_key(const QString& key) {
  if (profile_key_ == key) return;
  profile_key_ = key;
  reset_behavior();
}
void CharacterForgeViewModel::setNpcValue(const QString& field, const QVariant& value) {
  const auto before = has_project_ ? creation::native_npc_to_json(draft_.current().npc) : Json();
  edit([&] {
    auto json = creation::native_npc_to_json(draft_.current().npc);
    if (!json.contains(ss(field))) throw std::invalid_argument("Unknown Native NPC field");
    json[ss(field)] = to_json(value);
    draft_.set_native_npc(creation::native_npc_from_json(json));
  });
  // The voice evidence follows the voice, the sex and the factions (VoicesFollower / VoicesMarriage depend on them).
  if (has_project_ && (field == QLatin1String("voiceFormKey") || field == QLatin1String("female") || field == QLatin1String("factions")) &&
      creation::native_npc_to_json(draft_.current().npc) != before) {
    refreshVoiceAnalysis();
  }
}
void CharacterForgeViewModel::setExportOption(const QString& field, const QVariant& value) {
  edit([&] { auto json = draft_.current().export_options; json[ss(field)] = to_json(value); draft_.set_export_options(std::move(json)); });
}
void CharacterForgeViewModel::searchRecords(const QString& kind, const QString& text) {
  if (!catalog_ready_) {
    last_error_ = tr("Carga e indexa el perfil actual antes de buscar registros.");
    record_search_ = {{"state", "error"}, {"kind", kind}, {"count", 0}, {"code", ipc::protocol::error::kNotIndexed},
                      {"error", tr("El perfil aún no está indexado: espera a que termine la indexación.")}};
    emit recordsChanged();
    emit changed();
    return;
  }
  const auto serial = ++search_serial_;
  record_search_ = {{"state", "loading"}, {"kind", kind}, {"count", 0}};
  emit recordsChanged();
  worker_->call(ipc::protocol::method::kCreationSearchRecords, Json{{"kind", ss(kind)}, {"text", ss(text)}, {"limit", 100}}, std::chrono::seconds(30), this,
    [this, serial, kind](const Json& r) {
      if (serial != search_serial_) return;
      record_choices_ = to_list(r);
      record_search_ = {{"state", "ready"}, {"kind", kind}, {"count", static_cast<int>(record_choices_.size())}};
      emit recordsChanged();
    },
    [this, serial, kind](int code, const QString& error, const Json&) {
      if (serial != search_serial_) return;
      record_choices_.clear();
      last_error_ = error;
      // 0 results is "ready" with count 0; these are failures, and the picker says which.
      const auto why = code == ipc::protocol::error::kNotIndexed ? tr("El perfil aún no está indexado.")
                     : code == ipc::protocol::error::kInvalidArgument ? tr("El tipo de registro «%1» no se puede buscar: %2").arg(kind, error)
                     : code == ipc::protocol::error::kBusy ? tr("El perfil está ocupado (indexando o exportando); vuelve a intentarlo en unos segundos.")
                     : error;
      record_search_ = {{"state", "error"}, {"kind", kind}, {"count", 0}, {"code", code}, {"error", why}};
      emit recordsChanged();
      emit changed();
    });
}
void CharacterForgeViewModel::validateNpc() {
  if (!has_project_ || busy_ || !catalog_ready_) return;
  busy_ = true; last_error_.clear();
  const auto serial = ++operation_serial_;
  emit changed();
  const auto npc_json = creation::native_npc_to_json(draft_.current().npc);
  const auto plugin_name = draft_.current().export_options.value("pluginName", "CharacterForge.esp");
  // Structural validation first (creation.validateNpc: records, links, masters, outfit, FaceGen lock). Then, when the worker knows them,
  // the behavior half (creation.validateBehavior: voice evidence, class / combat style resolution). The second never replaces the
  // first: its issues are added, and the export gate stays the structural one.
  operation_cancel_ = worker_->call(ipc::protocol::method::kCreationValidateNpc, Json{{"npc", npc_json}, {"pluginName", plugin_name}}, std::chrono::seconds(60), this,
    [this, serial, npc_json, plugin_name](const Json& r) {
      if (serial != operation_serial_) return;
      validation_ = to_map(r);
      if (!behavior_supported_) { busy_ = false; emit changed(); return; }
      operation_cancel_ = worker_->call(ipc::protocol::method::kCreationValidateBehavior,
        Json{{"npc", npc_json}, {"pluginName", plugin_name}, {"profileName", ss(profile_key_)}}, std::chrono::seconds(60), this,
        [this, serial](const Json& b) {
          if (serial != operation_serial_) return;
          merge_behavior_validation(b);
          busy_ = false;
          emit changed();
          emit behaviorChanged();
        },
        [this, serial](int code, const QString& error, const Json&) {
          if (serial != operation_serial_) return;
          if (code == ipc::protocol::error::kMethodNotFound) { behavior_supported_ = false; emit behaviorChanged(); }
          validation_["behaviorError"] = error;  // the structural result stays, and says the behavior half could not run
          busy_ = false;
          emit changed();
        });
    },
    [this, serial](int, const QString& error, const Json&) { if (serial != operation_serial_) return; busy_ = false; last_error_ = error; emit changed(); });
}
void CharacterForgeViewModel::merge_behavior_validation(const Json& b) {
  validation_["voiceAnalysis"] = to_map(b.value("voiceAnalysis", Json::object()));
  validation_["behaviorIssues"] = to_list(b.value("behaviorIssues", Json::array()));
  validation_["resolvedLinks"] = to_list(b.value("resolvedLinks", Json::array()));
  validation_["limitations"] = to_list(b.value("limitations", Json::array()));
  validation_["behaviorFingerprint"] = qs(b.value("loadOrderFingerprint", std::string()));
  validation_["behaviorTechnicallyExportable"] = b.value("technicallyExportable", false);
  if (b.contains("resolvedClass") && !b.at("resolvedClass").is_null()) validation_["resolvedClass"] = to_map(b.at("resolvedClass"));
  if (b.contains("resolvedCombatStyle") && !b.at("resolvedCombatStyle").is_null()) validation_["resolvedCombatStyle"] = to_map(b.at("resolvedCombatStyle"));
  // Merge the behavior issues into the list the panel shows, without duplicating one the structural half already reported
  // (code + subject). The behavior half names the target "target"; the structural one "subject".
  auto issues = validation_.value("issues").toList();
  QSet<QString> seen;
  for (const auto& i : issues) { const auto m = i.toMap(); seen.insert(m.value("code").toString() + "|" + m.value("subject").toString()); }
  for (const auto& i : b.value("issues", Json::array())) {
    const auto code = qs(i.value("code", std::string()));
    const auto subject = qs(i.value("target", std::string()));
    if (seen.contains(code + "|" + subject)) continue;
    seen.insert(code + "|" + subject);
    issues.push_back(QVariantMap{{"severity", qs(i.value("severity", std::string("info")))}, {"code", code},
                                 {"message", qs(i.value("message", std::string()))}, {"subject", subject}, {"source", "behavior"}});
  }
  validation_["issues"] = issues;
  // The voice evidence of this validation is also the voice panel's: it was computed for this very draft.
  voice_analysis_ = {{"state", "ready"}, {"analysis", validation_.value("voiceAnalysis")}, {"loadOrderFingerprint", validation_.value("behaviorFingerprint")}};
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
  if (blender_bridge_->busy()) { blender_bridge_->cancel(); return; }
  if (behavior_busy_) {  // a preview / apply in flight: its answer is dropped, nothing is applied
    ++behavior_serial_;
    if (behavior_cancel_) behavior_cancel_->store(true);
    behavior_busy_ = false;
    set_preview({{"state", "cancelled"}, {"kind", archetype_preview_.value("kind")}, {"archetypeId", archetype_preview_.value("archetypeId")},
                 {"message", tr("Operación cancelada: no se cambió nada.")}});
    return;
  }
  if (operation_cancel_) operation_cancel_->store(true);
  // Remain busy until cancellation is acknowledged, preventing a racing duplicate export.
}
// ------------------------------------------------------------------ follower behavior (AG04)

namespace {
// What an archetype may never change (BulkArchetypeEngine.ProtectedCategories). The worker already leaves these alone; this is the
// studio's own check of the answer before it touches the draft.
const std::vector<const char*>& protected_npc_keys() {
  static const std::vector<const char*> keys = {"name", "editorId", "description", "raceFormKey", "female", "voiceFormKey", "height", "weight",
                                                "face", "headPartFormKeys", "hairColorFormKey", "headTextureFormKey", "level", "levelMultiplier",
                                                "minLevel", "maxLevel", "placement"};
  return keys;
}
}  // namespace

void CharacterForgeViewModel::set_preview(QVariantMap preview) {
  archetype_preview_ = std::move(preview);
  emit behaviorChanged();
}

void CharacterForgeViewModel::mark_preview_stale(const QString& why) {
  const auto state = archetype_preview_.value("state").toString();
  if (state == QLatin1String("ready") || state == QLatin1String("conflict") || state == QLatin1String("notApplicable")) {
    archetype_preview_["state"] = QStringLiteral("stale");
    archetype_preview_["message"] = why;
    emit behaviorChanged();
  } else if (state == QLatin1String("applied")) {
    archetype_preview_.clear();  // what was applied has been edited or undone since: the summary no longer describes the draft
    emit behaviorChanged();
  }
}

void CharacterForgeViewModel::reset_behavior() {
  ++behavior_serial_;
  ++catalog_serial_;
  ++voice_serial_;
  if (behavior_cancel_) behavior_cancel_->store(true);
  behavior_busy_ = false;
  behavior_catalog_.clear();
  archetype_preview_.clear();
  voice_analysis_.clear();
  behavior_supported_ = true;
  emit behaviorChanged();
}

void CharacterForgeViewModel::discardArchetypePreview() { set_preview({}); }

void CharacterForgeViewModel::loadBehaviorCatalog() {
  if (!catalog_ready_ || !behavior_supported_) {
    behavior_catalog_ = {{"state", "none"}, {"reason", !behavior_supported_ ? tr("Este worker no incluye el catálogo de comportamiento: actualiza la aplicación.")
                                                                           : tr("Carga e indexa el perfil actual antes de elegir un arquetipo.")}};
    emit behaviorChanged();
    return;
  }
  const auto state = behavior_catalog_.value("state").toString();
  if (state == QLatin1String("loading") || state == QLatin1String("ready")) return;  // once per profile (reset_behavior clears it)
  const int serial = ++catalog_serial_;
  behavior_catalog_ = {{"state", "loading"}};
  emit behaviorChanged();
  worker_->call(ipc::protocol::method::kCreationGetBehaviorCatalog, Json{{"profileName", ss(profile_key_)}, {"limit", 200}}, behavior_timeout_, this,
    [this, serial](const Json& r) {
      if (serial != catalog_serial_) return;
      behavior_catalog_ = to_map(r);
      behavior_catalog_["state"] = QStringLiteral("ready");
      emit behaviorChanged();
    },
    [this, serial](int code, const QString& error, const Json&) {
      if (serial != catalog_serial_) return;
      if (code == ipc::protocol::error::kMethodNotFound) behavior_supported_ = false;
      behavior_catalog_ = {{"state", "error"}, {"code", code},
                           {"reason", code == ipc::protocol::error::kNotIndexed ? tr("El perfil aún no está indexado.")
                                    : code == ipc::protocol::error::kInvalidLoadOrder ? tr("El perfil activo cambió: vuelve a abrir el panel.")
                                    : code == ipc::protocol::error::kClientTimeout ? tr("El worker no respondió a tiempo. Vuelve a intentarlo.")
                                    : error}};
      emit behaviorChanged();
    });
}

void CharacterForgeViewModel::previewArchetype(const QString& archetype_id, const QStringList& categories) { run_archetype(archetype_id, categories, false); }
void CharacterForgeViewModel::applyArchetype(const QString& archetype_id, const QStringList& categories) { run_archetype(archetype_id, categories, true); }

void CharacterForgeViewModel::run_archetype(const QString& archetype_id, const QStringList& categories, bool commit) {
  const auto kind = commit ? QStringLiteral("apply") : QStringLiteral("preview");
  const auto refuse = [&](const QString& state, const QString& message) {
    set_preview({{"state", state}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", message}});
  };
  if (!has_project_) return refuse(QStringLiteral("error"), tr("Abre o crea un proyecto de Character Forge primero."));
  if (!catalog_ready_) return refuse(QStringLiteral("error"), worker_message().isEmpty() ? tr("Carga e indexa el perfil actual antes de usar un arquetipo.") : tr("El worker no está disponible: %1").arg(worker_message()));
  if (!behavior_supported_) return refuse(QStringLiteral("error"), tr("Este worker no incluye los arquetipos: actualiza la aplicación."));
  if (behavior_busy_) return;  // a second click while one is running does not start a second one
  if (commit && busy_) return refuse(QStringLiteral("error"), tr("Hay otra operación en curso (validación o exportación): espera a que termine."));

  behavior_busy_ = true;
  const int serial = ++behavior_serial_;
  const auto base = creation::native_npc_to_json(draft_.current().npc);
  const auto base_dump = base.dump();
  const auto profile_at_request = profile_key_;
  set_preview({{"state", "loading"}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", commit ? tr("Calculando y aplicando el arquetipo…") : tr("Calculando la vista previa…")}});
  Json params{{"npc", base}, {"archetypeId", ss(archetype_id)}, {"dryRun", true}, {"profileName", ss(profile_key_)}};
  if (!categories.isEmpty()) {
    Json list = Json::array();
    for (const auto& c : categories) list.push_back(ss(c));
    params["categories"] = list;
  }
  behavior_cancel_ = worker_->call(ipc::protocol::method::kCreationApplyArchetype, params, behavior_timeout_, this,
    [this, serial, commit, kind, archetype_id, base, base_dump, profile_at_request](const Json& r) {
      if (serial != behavior_serial_) return;  // superseded, cancelled or the profile was re-indexed meanwhile
      behavior_busy_ = false;
      const auto fail = [&](const QString& state, const QString& message) {
        set_preview({{"state", state}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", message}});
      };
      if (!has_project_ || !catalog_ready_ || profile_key_ != profile_at_request)
        return fail(QStringLiteral("stale"), tr("El perfil activo cambió mientras se calculaba: no se aplicó nada."));
      if (creation::native_npc_to_json(draft_.current().npc).dump() != base_dump)
        return fail(QStringLiteral("stale"), tr("El borrador cambió mientras se calculaba: no se aplicó nada. Vuelve a pedirlo."));

      QVariantList changes;
      for (const auto& c : r.value("changes", Json::array())) {
        changes.push_back(QVariantMap{{"category", qs(c.value("category", std::string()))}, {"field", qs(c.value("fieldName", std::string()))},
                                      {"before", qs(c.value("beforeValueJson", std::string()))}, {"after", qs(c.value("afterValueJson", std::string()))}});
      }
      const auto validation = r.value("archetypeValidation", Json::object());
      const bool all_resolved = validation.value("allLinksResolved", false);
      QVariantList unresolved;
      for (const auto& l : validation.value("links", Json::array())) {
        if (l.value("resolved", true)) continue;
        unresolved.push_back(QVariantMap{{"formKey", qs(l.value("formKey", std::string()))}, {"category", qs(l.value("category", std::string()))},
                                         {"type", qs(l.value("expectedRecordType", std::string()))}, {"note", qs(l.value("note", std::string()))}});
      }
      QVariantMap preview{{"kind", kind}, {"archetypeId", archetype_id}, {"changes", changes}, {"appliedCategories", to_list(r.value("appliedCategories", Json::array()))},
                          {"allLinksResolved", all_resolved}, {"unresolved", unresolved}, {"issues", to_list(validation.value("issues", Json::array()))},
                          {"requiredMasters", to_list(validation.value("requiredMasters", Json::array()))},
                          {"loadOrderFingerprint", qs(r.value("loadOrderFingerprint", std::string()))},
                          {"proposedSha256", qs(r.value("proposedSha256", std::string()))}};
      // Defense in depth: an answer that touches identity, face, sex, voice or placement is refused whatever the worker meant.
      const auto& proposed = r.at("proposedNpc");
      for (const auto* key : protected_npc_keys()) {
        if (proposed.value(key, Json(nullptr)) != base.value(key, Json(nullptr))) {
          preview["state"] = QStringLiteral("error");
          preview["message"] = tr("El arquetipo intentó cambiar un campo protegido (%1): no se aplicó nada.").arg(QString::fromLatin1(key));
          return set_preview(std::move(preview));
        }
      }
      if (!all_resolved) {
        preview["state"] = QStringLiteral("conflict");
        preview["message"] = tr("Faltan registros de este arquetipo en el perfil activo (%1): no se puede aplicar.").arg(unresolved.size());
        return set_preview(std::move(preview));
      }
      if (changes.isEmpty()) {
        preview["state"] = QStringLiteral("notApplicable");
        preview["message"] = tr("El personaje ya cumple este arquetipo: no hay nada que cambiar.");
        return set_preview(std::move(preview));
      }
      preview["state"] = QStringLiteral("ready");
      preview["message"] = tr("%1 cambios propuestos. Nada se ha modificado todavía.").arg(changes.size());
      if (!commit) return set_preview(std::move(preview));

      // Apply: the answer becomes the draft in ONE undoable edit (the standard undo / redo of the project restores the exact previous draft).
      if (busy_) return set_preview({{"state", "error"}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", tr("Hay otra operación en curso: no se aplicó nada.")}});
      try {
        auto next = creation::native_npc_from_json(proposed);
        edit([&] { draft_.set_native_npc(std::move(next)); });
        if (creation::native_npc_to_json(draft_.current().npc).dump() == base_dump)  // edit() swallowed an error: nothing changed
          return set_preview({{"state", "error"}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", last_error_.isEmpty() ? tr("No se pudo aplicar.") : last_error_}});
      } catch (const std::exception& ex) {
        return set_preview({{"state", "error"}, {"kind", kind}, {"archetypeId", archetype_id}, {"message", tr("La respuesta del arquetipo no es válida: %1").arg(QString::fromUtf8(ex.what()))}});
      }
      preview["state"] = QStringLiteral("applied");
      preview["message"] = tr("Arquetipo aplicado (%1 cambios). Deshacer lo revierte de una vez.").arg(changes.size());
      set_preview(std::move(preview));
      refreshVoiceAnalysis();  // factions or packages may have changed what the voice evidence depends on
    },
    [this, serial, kind, archetype_id](int code, const QString& error, const Json&) {
      if (serial != behavior_serial_) return;
      behavior_busy_ = false;
      if (code == ipc::protocol::error::kMethodNotFound) behavior_supported_ = false;
      const auto message = code == ipc::protocol::error::kNotIndexed ? tr("El perfil aún no está indexado.")
                         : code == ipc::protocol::error::kInvalidLoadOrder ? tr("El perfil activo cambió: no se aplicó nada.")
                         : code == ipc::protocol::error::kRecordNotFound ? tr("El arquetipo «%1» no existe.").arg(archetype_id)
                         : code == ipc::protocol::error::kRequestCancelled ? tr("Operación cancelada: no se cambió nada.")
                         : code == ipc::protocol::error::kClientTimeout ? tr("El worker no respondió a tiempo: no se cambió nada. Vuelve a intentarlo.")
                         : error;
      set_preview({{"state", code == ipc::protocol::error::kInvalidLoadOrder ? "stale" : code == ipc::protocol::error::kRequestCancelled ? "cancelled" : "error"},
                   {"kind", kind}, {"archetypeId", archetype_id}, {"message", message}, {"code", code}});
    });
}

void CharacterForgeViewModel::refreshVoiceAnalysis() {
  const int serial = ++voice_serial_;
  const auto none = [&](const QString& reason) {
    voice_analysis_ = {{"state", "none"}, {"reason", reason}};
    emit behaviorChanged();
  };
  if (!has_project_) return none(tr("Sin proyecto abierto."));
  if (!catalog_ready_) return none(tr("Carga e indexa el perfil actual para ver la evidencia de la voz."));
  if (!behavior_supported_) return none(tr("Este worker no incluye el análisis de voz: actualiza la aplicación."));
  const auto& npc = draft_.current().npc;
  if (npc.voice_form_key.empty()) return none(tr("El personaje aún no tiene voz elegida."));
  voice_analysis_ = {{"state", "loading"}, {"voiceFormKey", qs(npc.voice_form_key)}};
  emit behaviorChanged();
  const auto json = creation::native_npc_to_json(npc);
  const auto voice = npc.voice_form_key;
  worker_->call(ipc::protocol::method::kCreationAnalyzeVoice,
                Json{{"voiceFormKey", voice}, {"female", npc.female}, {"factions", json.at("factions")}, {"profileName", ss(profile_key_)}}, std::min<std::chrono::milliseconds>(behavior_timeout_, std::chrono::seconds(30)), this,
    [this, serial, voice](const Json& r) {
      if (serial != voice_serial_) return;  // the voice, the sex or the profile changed since: this answer is for something else
      voice_analysis_ = {{"state", "ready"}, {"voiceFormKey", qs(voice)}, {"analysis", to_map(r.value("voiceAnalysis", Json::object()))},
                         {"loadOrderFingerprint", qs(r.value("loadOrderFingerprint", std::string()))}, {"issues", to_list(r.value("workerIssues", Json::array()))}};
      emit behaviorChanged();
    },
    [this, serial](int code, const QString& error, const Json&) {
      if (serial != voice_serial_) return;
      if (code == ipc::protocol::error::kMethodNotFound) behavior_supported_ = false;
      voice_analysis_ = {{"state", "error"}, {"code", code}, {"reason", code == ipc::protocol::error::kNotIndexed ? tr("El perfil aún no está indexado.") : error}};
      emit behaviorChanged();
    });
}

QVariantMap CharacterForgeViewModel::workspace() const {
  if (!has_project_) return {};
  auto result = to_map(draft_.current().workspace);
  const auto available = [this](QVariantMap& model) {
    if (model.isEmpty()) return;
    const auto path = model.value("path").toString();
    if (model.value("storage").toString() == QStringLiteral("managed"))
      model["available"] = !forge_assets::locate_model(project_path_, path).isEmpty();
    else model["available"] = QFileInfo(path).isFile();
  };
  auto model = result.value("model").toMap();
  available(model);
  if (!model.isEmpty()) result["model"] = model;
  auto library = result.value("library").toList();
  for (auto& item : library) { auto entry = item.toMap(); available(entry); item = entry; }
  result["library"] = library;
  return result;
}
QVariantMap CharacterForgeViewModel::capabilities() const {
  return {{"nativeRecords", true}, {"nativeFaceGen", false}, {"modelPreview", true},
          {"blenderBridge", true}, {"blenderMcp", false}, {"aiGeneration", false}, {"materialExport", false}};
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
               {"storage", "external"},
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
    const auto modelPath = model.at("path").get<std::string>();
    const auto same = std::find_if(library.begin(), library.end(), [&](const Json& entry) {
      return entry.is_object() && entry.value("path", std::string{}) == modelPath;
    });
    if (same == library.end() && library.size() < 100) library.push_back(model);
    w["library"] = library;
    draft_.set_workspace(std::move(w));
  }, false);
  if (last_error_.isEmpty()) preview_selected_model();
}

// The preview follows the workspace's selected model (selection, undo/redo, opened project). The
// scene validates it and renders only GLB/glTF; other formats report that they need conversion.
void CharacterForgeViewModel::preview_selected_model() {
  const auto model = to_map(draft_.current().workspace.value("model", Json::object()));
  auto path = model.value("path").toString();
  if (path.isEmpty()) {
    if (!scene_->file_path().isEmpty()) scene_->reset();
    return;
  }
  if (model.value("storage").toString() == QStringLiteral("managed")) {
    path = forge_assets::resolve_model(project_path_, path, model.value("sha256").toString());
    if (path.isEmpty()) {
      scene_->reset();
      last_error_ = tr("Modelo administrado ausente, modificado o fuera del proyecto .sncf.");
      return;
    }
  }
  if (QFileInfo(path).absoluteFilePath() == scene_->file_path() && scene_->state() != QStringLiteral("empty")) return;
  scene_->load(path);
}

void CharacterForgeViewModel::copySelectedModelToProject() {
  if (!has_project_ || busy_) return;
  try {
    const auto& w = draft_.current().workspace;
    if (!w.contains("model") || !w.at("model").is_object()) throw std::invalid_argument("Select a model first");
    auto model = w.at("model");
    if (model.value("storage", std::string{}) == "managed") return;
    const auto path = QString::fromStdString(model.at("path").get<std::string>());
    const auto imported = forge_assets::import_model(project_path_, path);
    model["path"] = imported.relative_path.toStdString();
    model["storage"] = "managed";
    model["sha256"] = imported.sha256.toStdString();
    model["sizeBytes"] = imported.size_bytes;
    model["status"] = "validated-managed";
    auto next = w;
    next["model"] = model;
    auto library = next.value("library", Json::array());
    bool found = false;
    for (auto& entry : library) if (entry.is_object() && entry.value("storage", std::string{}) == "managed" &&
                                     entry.value("sha256", std::string{}) == imported.sha256.toStdString()) {
      entry = model; found = true; break;
    }
    if (!found) {
      if (library.size() >= 100) throw std::invalid_argument("The model library already has 100 entries");
      library.push_back(model);
    }
    next["library"] = library;
    draft_.set_workspace(std::move(next));
    last_error_.clear();
    preview_selected_model();
  } catch (const std::exception& ex) { last_error_ = QString::fromUtf8(ex.what()); }
  emit changed();
}

void CharacterForgeViewModel::selectLibraryModel(int index) {
  if (!has_project_ || busy_) return;
  try {
    auto next = draft_.current().workspace;
    const auto library = next.value("library", Json::array());
    if (!library.is_array() || index < 0 || static_cast<std::size_t>(index) >= library.size())
      throw std::invalid_argument("Invalid model library selection");
    next["model"] = library.at(static_cast<std::size_t>(index));
    draft_.set_workspace(std::move(next));
    last_error_.clear();
    preview_selected_model();
  } catch (const std::exception& ex) { last_error_ = QString::fromUtf8(ex.what()); }
  emit changed();
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
  auto path = QStandardPaths::findExecutable("blender");
  if (path.isEmpty()) {
#if defined(Q_OS_WIN)
    for (const auto* candidate : {"D:/Program Files/blender.exe",
                                  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe",
                                  "C:/Program Files/Blender Foundation/Blender 4.2/blender.exe",
                                  "C:/Program Files/Blender Foundation/Blender/blender.exe"}) {
      if (QFileInfo::exists(candidate)) {
        path = QString::fromLatin1(candidate);
        break;
      }
    }
#endif
  }
  if (path.isEmpty()) { last_error_ = tr("Blender no encontrado en PATH. Puedes seleccionar su ejecutable."); emit changed(); return; }
  selectBlenderExecutable(path);
}
void CharacterForgeViewModel::convertSelectedModelWithBlender() {
  if (!has_project_ || busy_) return;
  try {
    if (project_path_.isEmpty()) throw std::invalid_argument("Save the .sncf project before converting a model");
    const auto& w = draft_.current().workspace;
    const auto& model = w.at("model");
    const auto format = model.at("format").get<std::string>();
    if (model.value("storage", std::string{}) == "managed" ||
        (format != "fbx" && format != "obj" && format != "blend"))
      throw std::invalid_argument("Select an external FBX, OBJ or BLEND model");
    if (w.value("library", Json::array()).size() >= 100)
      throw std::invalid_argument("The model library already has 100 entries");
    const auto executable = QString::fromStdString(w.at("blender").at("executable").get<std::string>());
    const auto source = QString::fromStdString(model.at("path").get<std::string>());
    blender_bridge_->convert(executable, source);
    busy_ = true; last_error_.clear();
  } catch (const std::exception& ex) { last_error_ = QString::fromUtf8(ex.what()); }
  emit changed();
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
