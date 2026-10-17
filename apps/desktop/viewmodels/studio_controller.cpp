#include "studio_controller.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <QCryptographicHash>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include "diagnostics/crash_diagnostics.hpp"
#include "resource_context.hpp"
#include "sns/appearance/control_map.hpp"
#include "sns/core/error.hpp"
#include "sns/face/facegen.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::app {

using namespace std::chrono_literals;
namespace method = ipc::protocol::method;

namespace {

bool check_mo2_running() {
#ifdef _WIN32
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  bool running = false;
  if (Process32FirstW(snapshot, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"ModOrganizer.exe") == 0) {
        running = true;
        break;
      }
    } while (Process32NextW(snapshot, &pe));
  }
  CloseHandle(snapshot);
  return running;
#else
  return false;
#endif
}

QString local_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

std::filesystem::path to_path(const QString& s) { return std::filesystem::path(s.toStdWString()); }
QString from_path(const std::filesystem::path& p) { return QString::fromStdWString(p.native()); }

QString severity_name(mo2::Diagnostic::Severity s) {
  switch (s) {
    case mo2::Diagnostic::Severity::Error: return "error";
    case mo2::Diagnostic::Severity::Warning: return "warning";
    default: return "info";
  }
}

QVariantMap control_report_map(const appearance::ControlReport& report);

}  // namespace

StudioController::StudioController(QString worker_executable, QObject* parent)
    : QObject(parent),
      worker_(new WorkerClient(std::move(worker_executable), this)),
      console_(new ConsoleModel(this)),
      outfit_(nullptr),
      instances_(new VariantListModel({"name", "kind", "path"}, this)),
      mods_(new VariantListModel({"name", "isEnabled", "priority", "kind", "fileCount", "plugins"}, this)),
      diagnostics_(new VariantListModel({"severity", "code", "message", "subject", "source"}, this)),
      npc_results_(new VariantListModel({"formKey", "formId", "name", "editorId", "race", "sex", "originPlugin",
                                         "winningPlugin", "winningMod", "overrideCount", "isUnique", "usesTemplate"},
                                        this)),
      armor_results_(new VariantListModel({"formKey", "formId", "name", "editorId", "originPlugin", "winningPlugin",
                                           "winningMod", "slots", "slotMask", "armorType", "nonPlayable", "maleModel",
                                           "femaleModel", "addonCount", "nifFound", "hasSmp"},
                                          this)) {
  history_ = new EditHistory(this);
  outfit_ = new OutfitEditor(worker_, console_, [this] { return snapshot_; }, draft_, history_, this);
  appearance_ = new AppearanceEditor(worker_, console_, outfit_, [this] { return snapshot_; }, draft_, history_, this);
  // The control map uses the facial analysis (measured deviation) once it is available.
  connect(appearance_, &AppearanceEditor::faceGenChanged, this, &StudioController::refresh_appearance_control);
  // A face resolution export is checked against the control map the profile would have with the
  // written plugin loaded last (chain from the worker, FaceGen files as the profile has them).
  appearance_->set_control_projector([this](const QVariantMap& chain, const QString& plugin) -> QVariantMap {
    if (!snapshot_ || npc_.isEmpty()) return {};
    auto details = npc_;
    QVariantList overrides;
    for (const auto& o : chain.value("overrides").toList()) {
      auto entry = o.toMap();
      const auto name = entry.value("plugin").toString();
      // Not installed yet: its MO2 mod will be the exported folder, named after the plugin.
      entry["mod"] = name.compare(plugin, Qt::CaseInsensitive) == 0 ? QFileInfo(plugin).completeBaseName() : providerOf(name);
      overrides << entry;
    }
    details["overrides"] = overrides;
    details["conflicts"] = chain.value("conflicts");
    return control_report_map(appearance::analyze_appearance_control(control_inputs(details)));
  });
  scene_ = new SceneViewModel(worker_, outfit_, appearance_, [this] { return snapshot_; }, this);
  armor_preview_ = new SceneViewModel(worker_, outfit_, appearance_, [this] { return snapshot_; }, this, true);
  forge_ = new CharacterForgeViewModel([this] {
    creation::ForgeEnvironment env;
    env.instance_name = ss(instance_name_);
    env.instance_ini = ss(instance_path_);
    env.profile = ss(selected_profile_);
    if (snapshot_) env.game_edition = mo2::to_string(snapshot_->game.edition);
    return env;
  }, worker_, this);

  // Project save/open: the face travels with the outfit in the same .snps.
  outfit_->set_face_provider([this](const std::string& form_key) -> std::optional<projects::FaceConfig> {
    if (appearance_ && appearance_->npc_form_key().toStdString() == form_key) return appearance_->face_config_for_project();
    return std::nullopt;
  });
  outfit_->set_face_loaded_handler([this](const std::string& form_key, const std::optional<projects::FaceConfig>& face) {
    if (appearance_) appearance_->queue_saved_face(form_key, face);
  });

  connect(worker_, &WorkerClient::logLine, this,
          [this](const QString& level, const QString& text) { console_->append(level, "worker", text); });
  connect(worker_, &WorkerClient::stateChanged, this, [this] { console_->append("info", "studio", worker_->statusText()); });
  connect(worker_, &WorkerClient::notification, this, [this](const QString& m, const QVariantMap& p) {
    if (m != method::kIndexProgress || phase_ != Phase::Indexing) return;
    const auto total = p.value("total").toDouble();
    const auto current = p.value("current").toDouble();
    const auto stage = p.value("stage").toString();
    const auto plugin = p.value("plugin").toString();
    set_progress(stage == "plugins" ? tr("Indexando %1 (%2/%3)").arg(plugin).arg(current).arg(total)
                                    : tr("Indexando: %1").arg(stage),
                 total > 0 ? current / total : -1.0);
  });
  connect(worker_, &WorkerClient::ready, this, [this] {
    if (snapshot_ && phase_ == Phase::Indexing) start_index();
  });
  connect(outfit_, &OutfitEditor::projectOpened, this, [this](const QString& form_key) { selectNpc(form_key); });

  worker_->start();
  discoverInstances();

  QSettings settings;
  const auto last = settings.value("mo2/instanceIni").toString();
  if (!last.isEmpty() && openInstance(last)) {
    const auto profile = settings.value("mo2/profile").toString();
    if (profiles_.contains(profile)) selected_profile_ = profile;
    emit instanceChanged();
  }
}

StudioController::~StudioController() {
  if (scan_cancel_) *scan_cancel_ = true;
  QThreadPool::globalInstance()->waitForDone(10000);
}

void StudioController::log(const QString& level, const QString& text) { console_->append(level, "studio", text); }

void StudioController::set_phase(Phase phase) {
  phase_ = phase;
  if (forge_) forge_->set_catalog_ready(phase == Phase::Ready);
  emit phaseChanged();
}

void StudioController::set_progress(const QString& text, double value) {
  progress_text_ = text;
  progress_ = value;
  emit progressChanged();
}

// ------------------------------------------------------------------ MO2 instance and profile

void StudioController::discoverInstances() {
  QList<QVariantMap> rows;
  for (const auto& info : mo2::discover_global_instances()) {
    rows.push_back({{"name", qs(info.name)}, {"kind", "global"}, {"path", from_path(info.ini)}});
  }
  if (!instance_path_.isEmpty() &&
      std::none_of(rows.begin(), rows.end(), [&](const QVariantMap& r) { return r["path"] == instance_path_; })) {
    rows.push_back({{"name", instance_name_}, {"kind", "portable"}, {"path", instance_path_}});
  }
  instances_->reset(rows);
}

bool StudioController::openInstance(const QString& path_or_url) {
  const auto path = local_path(path_or_url);
  const auto info = mo2::instance_from_path(to_path(path));
  if (!info) {
    log("error", tr("'%1' no es una instancia de Mod Organizer 2 (falta ModOrganizer.ini).").arg(path));
    return false;
  }
  try {
    const auto config = mo2::load_instance_config(*info);
    if (!mo2::is_supported_game(config)) {
      log("error", tr("La instancia gestiona '%1'; solo Skyrim Special Edition/AE está soportado.").arg(qs(config.game_name)));
      return false;
    }
    forge_->set_catalog_ready(false);
    instance_path_ = from_path(info->ini);
    game_path_ = QSettings().value("games/" + instance_path_, from_path(config.game_path)).toString();
    instance_name_ = qs(info->name);
    profiles_.clear();
    for (const auto& p : mo2::list_profiles(config)) profiles_ << qs(p);
    selected_profile_ = qs(config.selected_profile);
    QSettings().setValue("mo2/instanceIni", instance_path_);
    log("info", tr("Instancia MO2 '%1' (%2 perfiles, juego en %3)")
                    .arg(instance_name_)
                    .arg(profiles_.size())
                    .arg(from_path(config.game_path)));
    discoverInstances();
    emit instanceChanged();
    return true;
  } catch (const std::exception& ex) {
    log("error", tr("No se pudo leer la instancia: %1").arg(qs(ex.what())));
    return false;
  }
}

void StudioController::loadProfile(const QString& profile) {
  if (instance_path_.isEmpty() || phase_ == Phase::Scanning || phase_ == Phase::Indexing) return;
  selected_profile_ = profile;
  QSettings().setValue("mo2/profile", profile);
  emit instanceChanged();

  set_phase(Phase::Scanning);
  scene_->clear();
  armor_preview_->clear();
  snapshot_.reset();
  // A profile scan belongs to the previous snapshot: its pending answers are dropped.
  if (scan_) {
    scan_.reset();
    emit appearanceScanChanged();
  }
  set_progress(tr("Leyendo perfil '%1'…").arg(profile), -1.0);
  scan_cancel_ = std::make_shared<std::atomic<bool>>(false);
  QPointer<StudioController> self(this);
  QThreadPool::globalInstance()->start([self, ini = instance_path_, game_path = game_path_, profile, cancel = scan_cancel_] {
    SnapshotPtr result;
    QString error;
    try {
      QElapsedTimer clock;
      clock.start();
      auto snap = std::make_shared<Mo2Snapshot>();
      snap->config = mo2::load_instance_config(*mo2::instance_from_path(to_path(ini)));
      if (!game_path.isEmpty()) snap->config.game_path = to_path(game_path);
      snap->profile = mo2::load_profile(snap->config, ss(profile));
      snap->game = mo2::inspect_game(snap->config.game_path);
      if (!snap->game.exists) throw Error(ErrorKind::NotFound, "Skyrim SE not found at " + text::path_to_utf8(snap->config.game_path));
      mo2::VfsBuildOptions options;
      options.cancelled = [cancel] { return cancel->load(); };
      options.progress = [self](std::size_t done, std::size_t total, const std::string& provider) {
        if (done % 25 != 0 && done != total) return;
        const auto text = tr("Escaneando archivos de mods: %1").arg(qs(provider));
        const double value = total ? static_cast<double>(done) / static_cast<double>(total) : -1.0;
        QMetaObject::invokeMethod(self, [self, text, value] { if (self) self->set_progress(text, value); }, Qt::QueuedConnection);
      };
      snap->vfs = mo2::VirtualFileSystem::build(snap->config, snap->profile, snap->game, options);
      snap->load_order = mo2::resolve_load_order(snap->profile, snap->vfs, snap->game);
      snap->language = mo2::detect_game_language(snap->profile);
      for (const auto* list : {&snap->load_order.active, &snap->load_order.inactive}) {
        for (const auto& p : *list) snap->plugin_provider[text::to_lower_ascii(p.name)] = p.provider;
      }
      snap->scan_ms = clock.elapsed();
      result = std::move(snap);
    } catch (const std::exception& ex) {
      error = qs(ex.what());
    }
    QMetaObject::invokeMethod(self, [self, result, error] { if (self) self->scan_finished(result, error); }, Qt::QueuedConnection);
  });
}

bool StudioController::setGamePath(const QString& path_or_url) {
  if (phase_ == Phase::Scanning || phase_ == Phase::Indexing || instance_path_.isEmpty()) return false;
  const auto path = QDir::cleanPath(local_path(path_or_url));
  if (!mo2::inspect_game(to_path(path)).exists) {
    log("error", tr("La carpeta debe contener SkyrimSE.exe y Data: %1").arg(path));
    return false;
  }
  game_path_ = path;
  QSettings().setValue("games/" + instance_path_, path);
  log("info", tr("Ruta de Skyrim del estudio: %1 (MO2 no se modifica)").arg(path));
  emit instanceChanged();
  return true;
}

void StudioController::scan_finished(SnapshotPtr snapshot, const QString& error) {
  if (!error.isEmpty() || !snapshot) {
    set_phase(Phase::Error);
    set_progress(tr("Error: %1").arg(error), 0.0);
    log("error", tr("Error al leer el perfil: %1").arg(error));
    return;
  }
  snapshot_ = std::move(snapshot);
  const auto& s = *snapshot_;
  log("info", tr("Perfil '%1': %2 mods, %3 archivos virtuales, %4 plugins activos (%5 ms). Juego %6 (%7), idioma %8.")
                  .arg(qs(s.profile.name))
                  .arg(s.profile.mods.size())
                  .arg(s.vfs.file_count())
                  .arg(s.load_order.active.size())
                  .arg(s.scan_ms)
                  .arg(qs(s.game.exe_version), qs(mo2::to_string(s.game.edition)), qs(s.language)));

  QList<QVariantMap> diag;
  for (const auto& d : s.load_order.diagnostics) {
    diag.push_back({{"severity", severity_name(d.severity)}, {"code", qs(d.code)}, {"message", qs(d.message)},
                    {"subject", qs(d.subject)}, {"source", "load order"}});
  }
  diagnostics_->reset(diag);
  plugin_counts_.clear();
  rebuild_mod_rows();
  filterMods({}, false);

  summary_ = {{"profile", qs(s.profile.name)},
              {"gameVersion", qs(s.game.exe_version)},
              {"edition", qs(mo2::to_string(s.game.edition))},
              {"language", qs(s.language)},
              {"mods", static_cast<int>(s.profile.mods.size())},
              {"files", static_cast<qlonglong>(s.vfs.file_count())},
              {"activePlugins", static_cast<int>(s.load_order.active.size())},
              {"inactivePlugins", static_cast<int>(s.load_order.inactive.size())}};
  emit summaryChanged();

  set_phase(Phase::Indexing);
  if (worker_->state() == WorkerClient::State::Ready) {
    start_index();
  } else {
    set_progress(tr("Esperando al worker…"), -1.0);
    if (worker_->state() != WorkerClient::State::Starting) worker_->start();
  }
}

void StudioController::start_index() {
  const auto& s = *snapshot_;
  Json plugins = Json::array();
  for (const auto& p : s.load_order.active) {
    plugins.push_back({{"fileName", p.name},
                       {"path", p.path ? Json(text::path_to_utf8(*p.path)) : Json()},
                       {"enabled", true}});
  }
  const auto key_source = instance_path_.toLower() + "|" + qs(s.profile.name);
  const auto hash = QCryptographicHash::hash(key_source.toUtf8(), QCryptographicHash::Sha1).toHex().left(10);
  const auto profile_key = instance_name_ + "-" + qs(s.profile.name) + "-" + QString::fromLatin1(hash);
  const auto cache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/cache";
  QDir().mkpath(cache);

  set_progress(tr("Indexando plugins…"), 0.0);
  forge_->invalidate_validation();
  index_cancel_ = worker_->call(
      method::kCatalogIndex,
      Json{{"profileKey", ss(profile_key)}, {"cacheDirectory", ss(QDir::toNativeSeparators(cache))}, {"plugins", plugins},
           {"language", s.language}},
      30min, this,
      [this](const Json& r) {
        for (const auto& issue : r.value("issues", Json::array())) {
          diagnostics_->append({{{"severity", qs(issue.value("severity", ""))}, {"code", qs(issue.value("code", ""))},
                                 {"message", qs(issue.value("message", ""))}, {"subject", qs(issue.value("subject", ""))},
                                 {"source", "index"}}});
        }
        summary_["npcs"] = r.value("npcCount", 0);
        summary_["armors"] = r.value("armorCount", 0);
        summary_["outfits"] = r.value("outfitCount", 0);
        summary_["indexMs"] = static_cast<qlonglong>(r.value("elapsedMs", 0LL));
        summary_["parsedPlugins"] = r.value("parsedPlugins", 0);
        summary_["cachedPlugins"] = r.value("cachedPlugins", 0);
        emit summaryChanged();
        log("info", tr("Índice listo: %1 NPC, %2 ARMO, %3 OTFT (%4 plugins analizados, %5 desde caché, %6 ms).")
                        .arg(r.value("npcCount", 0))
                        .arg(r.value("armorCount", 0))
                        .arg(r.value("outfitCount", 0))
                        .arg(r.value("parsedPlugins", 0))
                        .arg(r.value("cachedPlugins", 0))
                        .arg(r.value("elapsedMs", 0LL)));
        set_phase(Phase::Ready);
        set_progress(tr("Listo"), 1.0);
        refresh_plugin_counts();
        searchNpcs({}, {}, {}, {});
        searchArmors({}, 0, {});
        emit loaded();
      },
      [this](int code, const QString& message, const Json&) {
        set_phase(Phase::Error);
        set_progress(tr("Indexación fallida: %1").arg(message), 0.0);
        log(code == ipc::protocol::error::kRequestCancelled ? "warning" : "error", tr("Indexación: %1").arg(message));
      });
}

void StudioController::reindex() {
  if (!selected_profile_.isEmpty()) loadProfile(selected_profile_);
}

void StudioController::cancel() {
  if (scan_cancel_) *scan_cancel_ = true;
  if (index_cancel_) *index_cancel_ = true;
}

void StudioController::refresh_plugin_counts() {
  worker_->call(method::kListPlugins, nullptr, 30s, this, [this](const Json& r) {
    QList<QVariantMap> armor_plugins;
    for (const auto& p : r.value("plugins", Json::array())) {
      const auto file = qs(p.value("fileName", ""));
      plugin_counts_[file.toLower()] = to_map(p);
      const int armors = p.value("armorRecords", 0);
      if (armors > 0) armor_plugins << QVariantMap{{"plugin", file}, {"mod", providerOf(file)}, {"armors", armors}};
    }
    std::sort(armor_plugins.begin(), armor_plugins.end(), [](const QVariantMap& a, const QVariantMap& b) {
      const int byMod = QString::compare(a.value("mod").toString(), b.value("mod").toString(), Qt::CaseInsensitive);
      return byMod != 0 ? byMod < 0 : QString::compare(a.value("plugin").toString(), b.value("plugin").toString(), Qt::CaseInsensitive) < 0;
    });
    armor_plugins_.clear();
    for (const auto& m : armor_plugins) armor_plugins_ << m;
    emit armorPluginsChanged();
  }, nullptr);
}

// ------------------------------------------------------------------ mods

void StudioController::rebuild_mod_rows() {
  all_mod_rows_.clear();
  if (!snapshot_) return;
  const auto& s = *snapshot_;
  QHash<QString, QStringList> plugins_by_mod;
  for (const auto* list : {&s.load_order.active, &s.load_order.inactive}) {
    for (const auto& p : *list) plugins_by_mod[qs(p.provider)] << qs(p.name);
  }
  for (auto it = s.profile.mods.rbegin(); it != s.profile.mods.rend(); ++it) {  // highest priority first, like MO2
    const auto name = qs(it->name);
    qlonglong files = 0;
    if (const auto index = s.vfs.find_provider(it->name)) files = static_cast<qlonglong>(s.vfs.providers()[*index].file_count);
    const char* kind = it->kind == mo2::ModKind::Separator ? "separator" : it->kind == mo2::ModKind::Foreign ? "foreign" : "mod";
    all_mod_rows_.push_back({{"name", name}, {"isEnabled", it->enabled}, {"priority", it->priority}, {"kind", kind},
                             {"fileCount", files}, {"plugins", plugins_by_mod.value(name).join(", ")}});
  }
}

void StudioController::filterMods(const QString& text, bool enabled_only) {
  QList<QVariantMap> rows;
  for (const auto& row : all_mod_rows_) {
    if (enabled_only && !row["isEnabled"].toBool()) continue;
    if (!text.isEmpty() && !row["name"].toString().contains(text, Qt::CaseInsensitive) &&
        !row["plugins"].toString().contains(text, Qt::CaseInsensitive)) {
      continue;
    }
    rows.push_back(row);
  }
  mods_->reset(rows);
}

QVariantMap StudioController::modDetails(const QString& mod_name) const {
  QVariantMap out;
  if (!snapshot_) return out;
  const auto& s = *snapshot_;
  out["name"] = mod_name;
  const auto provider = s.vfs.find_provider(ss(mod_name));
  out["active"] = provider.has_value();
  if (!provider) {
    out["note"] = tr("Mod desactivado, separador o gestionado fuera de MO2: no aporta archivos al perfil.");
    return out;
  }
  const auto c = s.vfs.conflicts(*provider, 100);
  out["files"] = static_cast<qlonglong>(c.files);
  out["winsTotal"] = static_cast<qlonglong>(c.overrides_total);
  out["losesTotal"] = static_cast<qlonglong>(c.overridden_total);
  QStringList wins, loses;
  for (const auto& p : c.overrides) wins << qs(p);
  for (const auto& p : c.overridden) {
    const auto winner = s.vfs.resolve(p);
    loses << qs(p) + (winner ? "  →  " + qs(s.vfs.providers()[winner->provider].name) : QString());
  }
  out["wins"] = wins;
  out["loses"] = loses;

  QVariantList plugins;
  QStringList dependencies;
  for (const auto* list : {&s.load_order.active, &s.load_order.inactive}) {
    for (const auto& p : *list) {
      if (p.provider != ss(mod_name)) continue;
      auto counts = plugin_counts_.value(qs(p.name).toLower());
      plugins.push_back(QVariantMap{{"name", qs(p.name)}, {"active", p.active},
                                    {"light", p.header && p.header->is_light},
                                    {"npcs", counts.value("npcRecords", 0)},
                                    {"armors", counts.value("armorRecords", 0)},
                                    {"outfits", counts.value("outfitRecords", 0)}});
      if (!p.header) continue;
      for (const auto& master : p.header->masters) {
        const auto it = s.plugin_provider.find(text::to_lower_ascii(master));
        const auto entry = qs(master) + " — " + (it == s.plugin_provider.end() ? tr("NO ENCONTRADO") : qs(it->second));
        if (!dependencies.contains(entry)) dependencies << entry;
      }
    }
  }
  out["plugins"] = plugins;
  out["dependencies"] = dependencies;
  return out;
}

QString StudioController::providerOf(const QString& plugin) const {
  if (!snapshot_) return {};
  const auto it = snapshot_->plugin_provider.find(text::to_lower_ascii(ss(plugin)));
  return it == snapshot_->plugin_provider.end() ? QString() : qs(it->second);
}

QVariantMap StudioController::resolveAsset(const QString& data_path) const {
  if (!snapshot_) return {};
  const auto file = snapshot_->vfs.resolve(ss(data_path));
  if (!file) return {{"found", false}, {"path", data_path}};
  return {{"found", true}, {"path", data_path}, {"absolute", from_path(file->absolute)},
          {"provider", qs(snapshot_->vfs.providers()[file->provider].name)},
          {"overriddenCount", static_cast<int>(snapshot_->vfs.providers_of(ss(data_path)).size()) - 1}};
}

void StudioController::openFolder(const QString& path) const {
  QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath()));
}

QString StudioController::mo2_mods_directory() const {
  if (!snapshot_ || snapshot_->config.mods_dir.empty()) return {};
  return from_path(snapshot_->config.mods_dir);
}

bool StudioController::isMo2Running() const {
  return check_mo2_running();
}

QVariantMap StudioController::installModToMo2(const QString& mod_dir) {
  if (isMo2Running()) {
    return {{"success", false},
            {"error", tr("Mod Organizer 2 está abierto. Ciérralo antes de instalar en la carpeta mods/.")}};
  }
  if (!snapshot_ || snapshot_->config.mods_dir.empty()) {
    return {{"success", false}, {"error", tr("No hay instancia de MO2 cargada o la carpeta mods/ no está configurada.")}};
  }

  const auto src_path = to_path(local_path(mod_dir));
  if (!std::filesystem::exists(src_path) || !std::filesystem::is_directory(src_path)) {
    return {{"success", false}, {"error", tr("La carpeta del mod exportado no existe: %1").arg(mod_dir)}};
  }

  const auto mod_name = src_path.filename();
  const auto dest_path = snapshot_->config.mods_dir / mod_name;

  std::error_code ec;
  std::filesystem::create_directories(dest_path, ec);
  if (ec) {
    return {{"success", false}, {"error", tr("No se pudo crear la carpeta en MO2: %1").arg(QString::fromStdString(ec.message()))}};
  }

  std::filesystem::copy(src_path, dest_path, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return {{"success", false}, {"error", tr("Error copiando archivos a MO2: %1").arg(QString::fromStdString(ec.message()))}};
  }

  const auto dest_str = from_path(dest_path);
  console_->append("info", "export", tr("Mod instalado con éxito en MO2: %1").arg(dest_str));
  return {{"success", true}, {"destination", dest_str}};
}

// ------------------------------------------------------------------ NPCs and armors

QVariantMap StudioController::npc_row(const QVariantMap& item) const {
  QVariantMap row = item;
  row["race"] = item.value("raceEditorId");
  row["winningMod"] = providerOf(item.value("winningPlugin").toString());
  return row;
}

void StudioController::searchNpcs(const QString& text, const QString& sex, const QString& kind, const QString& plugin) {
  if (phase_ != Phase::Ready) return;
  if (npc_search_cancel_) *npc_search_cancel_ = true;
  Json params = {{"limit", 500}};
  if (!text.trimmed().isEmpty()) params["text"] = ss(text.trimmed());
  if (!sex.isEmpty()) params["sex"] = ss(sex);
  if (!kind.isEmpty()) params["kind"] = ss(kind);
  if (!plugin.trimmed().isEmpty()) params["plugin"] = ss(plugin.trimmed());
  const int serial = ++npc_search_serial_;
  npc_search_cancel_ = worker_->call(method::kSearchNpcs, params, 30s, this, [this, serial](const Json& r) {
    if (serial != npc_search_serial_) return;  // a newer search superseded this one
    QList<QVariantMap> rows;
    for (const auto& item : r.value("items", Json::array())) rows.push_back(npc_row(to_map(item)));
    npc_results_->reset(rows);
    npc_total_ = r.value("total", 0);
    emit npcResultsChanged();
  }, [this](int code, const QString& message, const Json&) {
    if (code != ipc::protocol::error::kRequestCancelled) log("error", tr("Búsqueda de NPC: %1").arg(message));
  });
}

void StudioController::selectNpc(const QString& form_key) {
  if (phase_ != Phase::Ready || form_key.isEmpty()) return;
  diag::record("studio", ("selectNpc " + form_key).toStdString());
  npc_loading_ = true;
  pending_npc_ = form_key;
  emit npcChanged();
  worker_->call(method::kGetNpc, Json{{"formKey", ss(form_key)}}, 30s, this, [this, form_key](const Json& r) {
    if (form_key != pending_npc_) return;
    auto details = to_map(r);
    // Studio-side facts the worker cannot know: which MO2 mod provides each plugin and asset.
    QVariantList overrides;
    for (const auto& o : details.value("overrides").toList()) {
      auto entry = o.toMap();
      entry["mod"] = providerOf(entry.value("plugin").toString());
      overrides << entry;
    }
    details["overrides"] = overrides;
    const auto facegen = details.value("faceGen").toMap();
    details["faceGenGeometry"] = resolveAsset(facegen.value("geometryPath").toString());
    details["faceGenTint"] = resolveAsset(facegen.value("tintPath").toString());
    npc_ = details;
    npc_loading_ = false;
    emit npcChanged();
    // Which mod controls each part (record versions + FaceGen files); the measured deviation joins
    // when the facial analysis of this NPC finishes (faceGenChanged).
    refresh_appearance_control();
    const auto sum = details.value("summary").toMap();
    projects::NpcTarget target{
        .form_key = ss(sum.value("formKey").toString()),
        .editor_id = ss(sum.value("editorId").toString()),
        .name = ss(sum.value("name").toString()),
        .winning_plugin = ss(sum.value("winningPlugin").toString()),
        .override_count = sum.value("overrideCount").toInt(),
    };
    // A new NPC starts a new draft and a new history (face, body and outfit together).
    draft_.set_target(target);
    history_->clear();
    outfit_->setTarget(details);
    appearance_->set_target(target);
  }, [this, form_key](int, const QString& message, const Json&) {
    if (form_key != pending_npc_) return;
    npc_loading_ = false;
    emit npcChanged();
    log("error", tr("No se pudo cargar el NPC %1: %2").arg(form_key, message));
    emit npcLoadFailed(message);
  });
}

namespace {

QStringList string_list(const std::vector<std::string>& items) {
  QStringList out;
  for (const auto& i : items) out << qs(i);
  return out;
}

// QML form of a control report: {facePlugin, faceMod, faceVia, worst, components, findings, resolution?}.
QVariantMap control_report_map(const appearance::ControlReport& report) {
  QVariantList components, findings;
  for (const auto& c : report.components) {
    components << QVariantMap{{"key", qs(c.key)},   {"label", qs(c.label)}, {"plugin", qs(c.plugin)},
                              {"mod", qs(c.mod)},   {"kind", qs(c.kind)},   {"overridden", string_list(c.overridden)},
                              {"via", qs(c.via)}};
  }
  QString worst = QStringLiteral("ok");
  for (const auto& f : report.findings) {
    findings << QVariantMap{{"severity", qs(f.severity)}, {"code", qs(f.code)}, {"message", qs(f.message)},
                            {"consequence", qs(f.consequence)}, {"suggestion", qs(f.suggestion)}};
    if (f.severity == "warning" || (f.severity == "info" && worst == "ok")) worst = qs(f.severity);
  }
  QVariantMap out{{"facePlugin", qs(report.face_plugin)}, {"faceMod", qs(report.face_mod)}, {"faceVia", qs(report.face_via)},
                  {"worst", worst}, {"components", components}, {"findings", findings}};
  if (const auto& r = report.resolution) {
    out["resolution"] = QVariantMap{{"sourcePlugin", qs(r->source_plugin)}, {"sourceMod", qs(r->source_mod)},
                                    {"winnerPlugin", qs(r->winner_plugin)}, {"winnerMod", qs(r->winner_mod)},
                                    {"fields", string_list(r->fields)},     {"loadAfter", string_list(r->load_after)},
                                    {"label", qs(r->label)},                {"detail", qs(r->detail)},
                                    {"explanation", qs(r->explanation)},    {"keeps", string_list(r->keeps)},
                                    {"replaced", string_list(r->replaced)}, {"pending", string_list(r->pending)},
                                    {"alternatives", string_list(r->alternatives)},
                                    {"discardsOwnEdits", string_list(r->discards_own_edits)}};
  }
  return out;
}

}  // namespace

appearance::ControlInputs StudioController::control_inputs(const QVariantMap& details) const {
  appearance::ControlInputs in;
  if (!snapshot_) return in;
  for (const auto& o : details.value("overrides").toList()) {
    const auto e = o.toMap();
    appearance::OverrideLink link;
    link.plugin = ss(e.value("plugin").toString());
    link.mod = ss(e.value("mod").toString());
    link.is_origin = e.value("isOrigin").toBool();
    link.is_winner = e.value("isWinner").toBool();
    for (const auto& f : e.value("changedFields").toList()) link.changed_fields.push_back(ss(f.toString()));
    const auto copied = e.value("copiedFrom").toMap();
    for (auto it = copied.begin(); it != copied.end(); ++it) link.copied_from[ss(it.key())] = ss(it.value().toString());
    for (const auto i : snapshot_->vfs.providers_of(link.plugin)) link.plugin_mods.push_back(snapshot_->vfs.providers()[i].name);
    in.chain.push_back(std::move(link));
  }
  for (const auto& c : details.value("conflicts").toList()) in.lost_fields.push_back(ss(c.toMap().value("field").toString()));

  // Effective copy (loose files over archives, like the game) and every loose copy in MO2.
  const auto ctx = ResourceContext::for_snapshot(snapshot_);
  const auto source = [&](const QString& path) {
    appearance::AssetSource a;
    a.path = ss(path);
    if (path.isEmpty()) return a;
    for (const auto i : snapshot_->vfs.providers_of(a.path)) a.loose_mods.push_back(snapshot_->vfs.providers()[i].name);
    const auto loc = ctx && ctx->resolver ? ctx->resolver->locate(a.path) : std::nullopt;
    if (loc) {
      a.found = true;
      a.kind = loc->kind == assets::AssetLocation::Kind::Loose ? "loose" : "bsa";
      a.winner_mod = a.kind == "bsa" && !loc->archive_provider.empty() ? loc->archive_provider : loc->provider;
    } else if (!a.loose_mods.empty()) {
      a.found = true;
      a.kind = "loose";
      a.winner_mod = a.loose_mods.back();
    }
    return a;
  };
  const auto facegen = details.value("faceGen").toMap();
  in.facegen = source(facegen.value("geometryPath").toString());
  in.facetint = source(facegen.value("tintPath").toString());
  in.facegen_expected = facegen_expected(details);

  // When another mod than the FaceGen's set the head parts: are all of them in the FaceGen? Its shapes
  // are named after the head part EditorIDs.
  const auto parts = details.value("headParts").toList();
  if (in.facegen.found && !parts.isEmpty() && !in.chain.empty() && ctx && ctx->resolver) {
    // The version whose head parts the record has (a forwarded copy counts as its original).
    const auto at = appearance::field_controller(in.chain, "HeadParts", in.facegen.winner_mod);
    if (in.chain[at].mod != in.facegen.winner_mod) {
      if (const auto bytes = ctx->resolver->read(in.facegen.path)) {
        const auto info = face::inspect_facegen(*bytes);
        bool all = info.readable;
        for (const auto& p : parts) {
          // A head part without a mesh (the "...00NoGash" / "NoBeard" placeholders) has no FaceGen shape.
          if (p.toMap().value("modelPath").toString().isEmpty()) continue;
          const auto editor_id = ss(p.toMap().value("record").toMap().value("editorId").toString());
          all = all && !editor_id.empty() &&
                std::any_of(info.shapes.begin(), info.shapes.end(), [&](const auto& s) { return text::iequals_ascii(s.first, editor_id); });
        }
        in.facegen_has_record_head_parts = all;
      }
    }
  }
  return in;
}

bool StudioController::facegen_expected(const QVariantMap& details) const {
  const auto summary = details.value("summary").toMap();
  if (details.contains("templateFlags")) {
    if (!details.value("template").toMap().isEmpty() && details.value("templateFlags").toStringList().contains("Traits")) return false;
  } else if (summary.value("usesTemplate").toBool()) {
    return false;  // scan entry without details: templated unique NPCs take their traits from the template
  }
  if (!details.value("headParts").toList().isEmpty()) return true;
  const auto race = summary.value("raceFormKey").toString().toLower();
  return races_with_facegen_.isEmpty() || race.isEmpty() || races_with_facegen_.contains(race);
}

void StudioController::refresh_appearance_control() {
  QElapsedTimer timer;
  timer.start();
  QVariantMap out;
  control_resolution_.reset();
  if (snapshot_ && !npc_.isEmpty()) {
    auto in = control_inputs(npc_);
    // The facial analysis counts only if it examined this NPC's FaceGen (it may still be the previous one).
    const auto geometry_path = npc_.value("faceGen").toMap().value("geometryPath").toString();
    const auto compatibility = appearance_->face_compatibility();
    const auto normalize = [](QString p) { return p.replace('/', '\\').toLower(); };
    const auto analysed = compatibility.value("provenance").toMap().value("faceGenPath").toString();
    bool numeric = false;
    const double deviation = compatibility.value("deviation").toDouble(&numeric);
    if (numeric && deviation >= 0.0 && !analysed.isEmpty() && normalize(analysed) == normalize(geometry_path)) {
      in.regeneration_deviation = static_cast<float>(deviation);
    }
    const auto report = appearance::analyze_appearance_control(in);
    control_resolution_ = report.resolution;
    out = control_report_map(report);
    out["formKey"] = npc_.value("summary").toMap().value("formKey");
    out["deviation"] = in.regeneration_deviation ? QVariant(*in.regeneration_deviation) : QVariant();
  }
  // faceGenChanged fires on every rebuild: notify only when the map itself changed.
  auto previous = appearance_control_;
  previous.remove("analysisUs");
  if (previous == out) return;
  out["analysisUs"] = static_cast<qint64>(timer.nsecsElapsed() / 1000);
  appearance_control_ = out;
  emit appearanceControlChanged();
}

// ------------------------------------------------------------------ profile-wide control map

struct StudioController::AppearanceScan {
  bool running = true;
  QString stage = QStringLiteral("listing");  // listing, analysing, done, cancelled, failed
  QElapsedTimer timer;
  QStringList pending;  // NPCs with several record versions: their chain needs getNpc
  QVariantList singles;  // NPCs with one version, analysed once every race is known
  int next_single = 0;   // the singles are analysed in slices so the interface keeps repainting
  int in_flight = 0;     // getNpc requests sent and not answered yet
  QSet<QString> races;   // races with a FaceGen file for some listed NPC
  int listed = 0;
  int analysed = 0;
  int total = 0;
  int rpc_calls = 0;
  int errors = 0;
  int resolutions = 0;  // NPCs for which the map offers «Usar el rostro de X»
  int since_emit = 0;
  qint64 listing_ms = 0;
  qint64 elapsed_ms = 0;
  // Time on the UI thread: the longest single slice (the interface cannot repaint meanwhile) and the
  // total spent analysing in the studio (the rest of the elapsed time is waiting for the worker).
  qint64 max_block_us = 0;
  QString max_block_where;
  qint64 analysis_us = 0;
  void note_block(const QElapsedTimer& t, const char* where) {
    if (const auto us = t.nsecsElapsed() / 1000; us > max_block_us) {
      max_block_us = us;
      max_block_where = QString::fromLatin1(where);
    }
  }
  QMap<QString, int> counts;
  QMap<QString, int> codes;
  QVariantList rows;
};

QVariantMap StudioController::appearance_scan() const {
  if (!scan_) return {};
  const auto& s = *scan_;
  QVariantMap counts, codes;
  for (auto it = s.counts.begin(); it != s.counts.end(); ++it) counts.insert(it.key(), it.value());
  for (auto it = s.codes.begin(); it != s.codes.end(); ++it) codes.insert(it.key(), it.value());
  return {{"running", s.running},     {"stage", s.stage},       {"listed", s.listed},     {"analysed", s.analysed},
          {"total", s.total},         {"pending", static_cast<int>(s.pending.size())},       {"rpcCalls", s.rpc_calls},
          {"errors", s.errors},       {"listingMs", s.listing_ms},
          {"maxBlockMs", s.max_block_us / 1000.0}, {"maxBlockWhere", s.max_block_where}, {"analysisMs", s.analysis_us / 1000},  {"resolutions", s.resolutions},
          {"elapsedMs", s.running ? s.timer.elapsed() : s.elapsed_ms},
          {"counts", counts},         {"codes", codes},         {"rows", s.rows}};
}

bool StudioController::prepareFaceResolution() {
  const auto form_key = appearance_control_.value("formKey").toString();
  if (!control_resolution_ || form_key.isEmpty() || form_key != appearance_->npc_form_key()) {
    log("warning", tr("El mapa de control no ofrece ninguna resolución para este NPC."));
    return false;
  }
  diag::record("studio", "face resolution " + ss(form_key) + ": " + control_resolution_->label);
  appearance_->set_face_resolution(*control_resolution_);
  return true;
}

void StudioController::scanAppearanceControl() {
  if (phase_ != Phase::Ready || !snapshot_ || (scan_ && scan_->running)) return;
  diag::record("studio", "appearance scan");
  scan_ = std::make_shared<AppearanceScan>();
  scan_->timer.start();
  emit appearanceScanChanged();
  scan_page(0);
}

void StudioController::cancelAppearanceScan() {
  if (!scan_ || !scan_->running) return;
  scan_->stage = QStringLiteral("cancelled");
  scan_finish();
}

void StudioController::scan_page(int offset) {
  auto scan = scan_;
  ++scan->rpc_calls;
  worker_->call(method::kSearchNpcs, Json{{"limit", 250}, {"offset", offset}, {"kind", "unique"}}, 60s, this,
                [this, scan, offset](const Json& r) {
    if (scan != scan_ || !scan->running) return;
    QElapsedTimer block;
    block.start();
    scan->total = r.value("total", 0);
    const auto items = r.value("items", Json::array());
    const auto ctx = ResourceContext::for_snapshot(snapshot_);
    for (const auto& item : items) {
      const auto summary = to_map(item);
      const auto form_key = summary.value("formKey").toString();
      ++scan->listed;
      // FaceGen files are named after the origin plugin and the plugin-local FormID (as the worker's
      // FaceGenFor); a race with any such file uses FaceGen.
      const auto colon = form_key.indexOf(':');
      const auto plugin = form_key.mid(colon + 1);
      const auto id = form_key.left(colon).toUpper().rightJustified(8, '0');
      const auto geometry = QStringLiteral("meshes\\actors\\character\\facegendata\\facegeom\\%1\\%2.nif").arg(plugin, id);
      if (ctx && ctx->resolver && ctx->resolver->locate(ss(geometry))) scan->races.insert(summary.value("raceFormKey").toString().toLower());
      if (summary.value("overrideCount").toInt() > 1) {
        scan->pending << form_key;
        continue;
      }
      // One record version: the chain is the origin alone.
      const QVariantMap origin{{"plugin", plugin}, {"mod", providerOf(plugin)}, {"isOrigin", true}, {"isWinner", true}, {"changedFields", QVariantList{}}};
      scan->singles << QVariantMap{{"summary", summary},
                                   {"overrides", QVariantList{origin}},
                                   {"faceGen", QVariantMap{{"geometryPath", geometry},
                                                           {"tintPath", QStringLiteral("textures\\actors\\character\\facegendata\\facetint\\%1\\%2.dds").arg(plugin, id)}}}};
    }
    const int next = offset + static_cast<int>(items.size());
    if (!items.empty() && next < scan->total) {
      scan->note_block(block, "listing page");
      scan_page(next);
      return;
    }
    races_with_facegen_ = scan->races;
    scan->note_block(block, "listing");
    scan->listing_ms = scan->timer.elapsed();
    scan->stage = QStringLiteral("analysing");
    emit appearanceScanChanged();
    // The worker starts on the multi-version NPCs while the studio analyses the single-version ones.
    scan_next();
    scan_singles();
  }, [this, scan](int, const QString& message, const Json&) {
    if (scan != scan_ || !scan->running) return;
    log("error", tr("Análisis del perfil: %1").arg(message));
    ++scan->errors;
    scan->stage = QStringLiteral("failed");
    scan_finish();
  });
}

// Single-version NPCs: analysed 40 at a time, one slice per pass of the event loop (one slice of all of
// them blocked the interface for 0.8 s on the test profile).
void StudioController::scan_singles() {
  auto scan = scan_;
  if (!scan || !scan->running) return;
  QElapsedTimer block;
  block.start();
  const int end = std::min<int>(scan->next_single + 40, static_cast<int>(scan->singles.size()));
  for (; scan->next_single < end; ++scan->next_single) scan_record(scan->singles[scan->next_single].toMap());
  scan->note_block(block, "single-version NPCs (slice)");
  if (scan->next_single < scan->singles.size()) {
    QTimer::singleShot(0, this, [this] { scan_singles(); });
    return;
  }
  scan_next();  // finishes the scan when the worker is done too
}

// Multi-version NPCs: up to four getNpc requests in flight, so the worker never waits for the studio
// to send the next one (it answered one at a time, ~30 ms each, on the test profile).
void StudioController::scan_next() {
  auto scan = scan_;
  if (!scan || !scan->running) return;
  while (scan->in_flight < 4 && !scan->pending.isEmpty()) {
    const auto form_key = scan->pending.takeFirst();
    ++scan->rpc_calls;
    ++scan->in_flight;
    request_scan_npc(scan, form_key);
  }
  const bool singles_left = scan->next_single < scan->singles.size();
  if (scan->pending.isEmpty() && scan->in_flight == 0 && !singles_left) {
    scan->stage = QStringLiteral("done");
    scan_finish();
  }
}

void StudioController::request_scan_npc(const std::shared_ptr<AppearanceScan>& scan, const QString& form_key) {
  worker_->call(method::kGetNpc, Json{{"formKey", ss(form_key)}}, 30s, this, [this, scan](const Json& r) {
    if (scan != scan_ || !scan->running) return;
    --scan->in_flight;
    QElapsedTimer block;
    block.start();
    auto details = to_map(r);
    QVariantList overrides;
    for (const auto& o : details.value("overrides").toList()) {
      auto entry = o.toMap();
      entry["mod"] = providerOf(entry.value("plugin").toString());
      overrides << entry;
    }
    details["overrides"] = overrides;
    scan_record(details);
    scan->note_block(block, "getNpc answer");
    scan_next();
  }, [this, scan, form_key](int, const QString& message, const Json&) {
    if (scan != scan_ || !scan->running) return;
    --scan->in_flight;
    ++scan->errors;
    log("warning", tr("Análisis del perfil: %1 no se pudo leer (%2)").arg(form_key, message));
    scan_next();
  });
}

void StudioController::scan_record(const QVariantMap& details) {
  auto& scan = *scan_;
  QElapsedTimer analysis;
  analysis.start();
  const struct Account {
    AppearanceScan& scan;
    QElapsedTimer& t;
    ~Account() { scan.analysis_us += t.nsecsElapsed() / 1000; }
  } account{scan, analysis};
  const auto report = appearance::analyze_appearance_control(control_inputs(details));
  QString worst = QStringLiteral("ok");
  QString message;
  QStringList codes;
  for (const auto& f : report.findings) {
    ++scan.codes[qs(f.code)];
    if (f.severity == "ok") continue;
    codes << qs(f.code);
    if (f.severity == "warning") {
      worst = QStringLiteral("warning");
      if (message.isEmpty()) message = qs(f.message);
    } else if (worst == "ok") {
      worst = QStringLiteral("info");
    }
  }
  ++scan.analysed;
  if (report.resolution) ++scan.resolutions;
  ++scan.counts[worst];
  if (worst != "ok") {
    const auto summary = details.value("summary").toMap();
    QString facegen_mod;
    for (const auto& c : report.components) {
      if (c.key == "facegen") facegen_mod = qs(c.mod);
    }
    const auto name = summary.value("name").toString();
    scan.rows << QVariantMap{{"formKey", summary.value("formKey")},
                             {"name", name.isEmpty() ? summary.value("editorId").toString() : name},
                             {"worst", worst},
                             {"codes", codes},
                             {"facePlugin", qs(report.face_plugin)},
                             {"faceMod", qs(report.face_mod)},
                             {"faceGenMod", facegen_mod},
                             {"message", message},
                             {"resolution", report.resolution ? qs(report.resolution->label) : QString()},
                             {"resolutionWinner", report.resolution ? qs(report.resolution->winner_plugin) : QString()},
                             {"resolutionFields", report.resolution ? string_list(report.resolution->fields) : QStringList()},
                             {"resolutionKeeps", report.resolution ? string_list(report.resolution->keeps) : QStringList()},
                             {"resolutionDiscardsOwn", report.resolution ? string_list(report.resolution->discards_own_edits) : QStringList()}};
  }
  if (++scan.since_emit >= 250) {
    scan.since_emit = 0;
    emit appearanceScanChanged();
  }
}

void StudioController::scan_finish() {
  auto& scan = *scan_;
  scan.running = false;
  scan.elapsed_ms = scan.timer.elapsed();
  // Warnings first, then notes; by name inside each group.
  std::stable_sort(scan.rows.begin(), scan.rows.end(), [](const QVariant& a, const QVariant& b) {
    const auto ma = a.toMap(), mb = b.toMap();
    const bool wa = ma.value("worst").toString() == "warning", wb = mb.value("worst").toString() == "warning";
    if (wa != wb) return wa;
    return ma.value("name").toString().compare(mb.value("name").toString(), Qt::CaseInsensitive) < 0;
  });
  diag::record("studio", ("appearance scan " + scan.stage).toStdString());
  log("info", tr("Análisis del perfil (%1): %2 NPC únicos, %3 con avisos, %4 con notas, %5 errores, %6 s")
                  .arg(scan.stage).arg(scan.analysed).arg(scan.counts.value("warning")).arg(scan.counts.value("info"))
                  .arg(scan.errors).arg(scan.elapsed_ms / 1000.0, 0, 'f', 1));
  emit appearanceScanChanged();
}

void StudioController::searchArmors(const QString& text, int slot, const QString& plugin, const QString& armor_type, bool smp_only) {
  if (phase_ != Phase::Ready) return;
  if (armor_search_cancel_) *armor_search_cancel_ = true;
  Json params = {{"limit", 500}};
  if (!text.trimmed().isEmpty()) params["text"] = ss(text.trimmed());
  if (slot >= 30 && slot <= 61) params["slotMask"] = 1u << (slot - 30);
  if (!plugin.trimmed().isEmpty()) params["plugin"] = ss(plugin.trimmed());
  if (!armor_type.trimmed().isEmpty() && armor_type != "all") params["armorType"] = ss(armor_type.trimmed());
  if (smp_only) params["smpOnly"] = true;
  const int serial = ++armor_search_serial_;
  armor_search_cancel_ = worker_->call(method::kSearchArmors, params, 30s, this, [this, serial](const Json& r) {
    if (serial != armor_search_serial_) return;
    std::shared_ptr<const assets::AssetResolver> resolver;
    if (snapshot_) {
      const auto ctx = ResourceContext::for_snapshot(snapshot_);
      if (ctx) resolver = ctx->resolver;
    }
    QList<QVariantMap> rows;
    for (const auto& item : r.value("items", Json::array())) {
      auto row = to_map(item);
      row["winningMod"] = providerOf(row.value("winningPlugin").toString());
      QStringList slot_list;
      for (const auto& s : row.value("slots").toList()) slot_list << s.toString();
      row["slots"] = slot_list.join(", ");

      const auto male_model = row.value("maleModel").toString();
      const auto female_model = row.value("femaleModel").toString();
      const auto effective_model = !female_model.isEmpty() ? female_model : male_model;
      bool nif_found = false;
      if (!effective_model.isEmpty()) {
        if (resolver) {
          const auto loc = resolver->locate(ss(effective_model));
          nif_found = loc.has_value();
          if (loc.has_value() && row.value("winningMod").toString().isEmpty()) {
            row["winningMod"] = qs(loc->provider);
          }
        } else if (snapshot_) {
          nif_found = snapshot_->vfs.resolve(ss(effective_model)).has_value();
        }
      }
      row["nifFound"] = nif_found;

      if (!row.contains("hasSmp")) {
        const auto combined = (row.value("name").toString() + " " + row.value("editorId").toString() + " " + effective_model).toLower();
        row["hasSmp"] = combined.contains("smp") || combined.contains("hdt") || combined.contains("physics");
      }

      rows.push_back(row);
    }
    armor_results_->reset(rows);
    armor_total_ = r.value("total", 0);
    emit armorResultsChanged();
  }, [this](int code, const QString& message, const Json&) {
    if (code != ipc::protocol::error::kRequestCancelled) log("error", tr("Catálogo de ropa: %1").arg(message));
  });
}

void StudioController::copyToClipboard(const QString& text) {
  if (auto* cb = QGuiApplication::clipboard()) {
    cb->setText(text);
  }
}

}  // namespace sns::app
