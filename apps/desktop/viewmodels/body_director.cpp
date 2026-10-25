#include "body_director.hpp"

#include <QDir>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include <format>
#include <optional>

#include "diagnostics/crash_diagnostics.hpp"
#include "export_destination.hpp"
#include "json_bridge.hpp"
#include "resource_context.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/body/body_analysis.hpp"
#include "sns/body/bodyslide_presets.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::app {
namespace method = ipc::protocol::method;
using namespace std::chrono_literals;

namespace {

constexpr int kMaxListedPresets = 600;

QString local_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

QVariantList issue_list(const std::vector<body::Issue>& issues) {
  QVariantList out;
  for (const auto& i : issues) out << QVariantMap{{"severity", qs(i.severity)}, {"code", qs(i.code)}, {"message", qs(i.message)}};
  return out;
}

QStringList string_list(const std::vector<std::string>& items) {
  QStringList out;
  for (const auto& s : items) out << qs(s);
  return out;
}

QString layout_id(body::TextureLayout l) {
  switch (l) {
    case body::TextureLayout::Standard: return QStringLiteral("standard");
    case body::TextureLayout::Ube: return QStringLiteral("ube");
    case body::TextureLayout::Unknown: break;
  }
  return QStringLiteral("unknown");
}

// Stable rule id per NPC: BCNG seeds its per-actor pick with it, so the same NPC keeps the same id across exports.
QString rule_id_for(const QString& form_key) {
  auto id = QStringLiteral("sns-npc-") + form_key.toLower();
  id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_.-]")), QStringLiteral("-"));
  return id;
}

}  // namespace

struct BodyDirector::Library {
  SnapshotPtr snapshot;
  std::shared_ptr<ResourceContext> context;  // BSA-aware reads of the body meshes (vanilla bodies live in archives)
  body::BodyEnvironment env;
  body::PresetLibrary presets;
  std::optional<body::bcng::Distribution> base;  // the profile's current distribution, when it parses
  std::string base_error;
  std::unique_ptr<body::BodyInspector> inspector;
};

struct BodyDirector::Analysis {
  body::NpcBodyInfo info;
  body::NpcBodyAnalysis body;
};

BodyDirector::BodyDirector(WorkerClient* worker, QObject* parent) : QObject(parent), worker_(worker) {
  environment_ = {{"state", "none"}};
  npc_ = {{"state", "none"}};
  export_result_ = {{"state", "none"}};
  rebuild_draft();
}

BodyDirector::~BodyDirector() = default;

QString BodyDirector::default_export_directory() const {
  return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SkyrimNpcStudio/Exports";
}

void BodyDirector::set_profile(SnapshotPtr snapshot) {
  ++profile_serial_;
  ++npc_serial_;
  snapshot_ = std::move(snapshot);
  library_.reset();
  analysis_.reset();
  rules_.clear();
  merged_text_.clear();
  npc_ = {{"state", "none"}};
  export_result_ = {{"state", "none"}};
  environment_ = {{"state", snapshot_ ? "loading" : "none"}};
  presets_.clear();
  preset_total_ = 0;
  emit environmentChanged();
  emit npcChanged();
  emit presetsChanged();
  emit exportResultChanged();
  rebuild_draft();
  if (snapshot_) start_load();
}

void BodyDirector::reload() {
  if (!snapshot_) return;
  const auto pending = npc_.value("formKey").toString();
  ++profile_serial_;
  library_.reset();
  environment_ = {{"state", "loading"}};
  emit environmentChanged();
  start_load();
  if (!pending.isEmpty()) npc_["pending"] = pending;
}

void BodyDirector::start_load() {
  const int serial = profile_serial_;
  QThreadPool::globalInstance()->start([self = QPointer<BodyDirector>(this), snapshot = snapshot_, serial] {
    diag::record("body", "load presets and environment");
    auto lib = std::make_shared<Library>();
    lib->snapshot = snapshot;
    QString error;
    try {
      lib->env = body::inspect_environment(snapshot->vfs, &snapshot->load_order);
      lib->presets = body::load_preset_library(snapshot->vfs);
      if (!lib->env.distribution_text.empty()) {
        try {
          lib->base = body::bcng::parse_distribution(lib->env.distribution_text);
        } catch (const std::exception& e) {
          lib->base_error = e.what();
        }
      }
      lib->context = ResourceContext::for_snapshot(snapshot);
      const auto resolver = lib->context ? lib->context->resolver : nullptr;
      body::DataLookup lookup;
      if (resolver) {
        lookup = [resolver](std::string_view path, bool want_bytes) -> std::optional<body::DataFile> {
          const auto where = resolver->locate(path);
          if (!where) return std::nullopt;
          body::DataFile f;
          f.archived = where->kind == assets::AssetLocation::Kind::Archive;
          f.provider = where->provider;
          f.game_data = f.archived ? where->archive_provider == "<Game Data>" : where->provider == "<Game Data>";
          if (want_bytes) f.bytes = resolver->read(path);
          return f;
        };
      } else {
        lookup = body::loose_lookup(snapshot->vfs);
      }
      lib->inspector = std::make_unique<body::BodyInspector>(lib->env, std::move(lookup));
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, serial, lib, error] {
          if (!self || serial != self->profile_serial_) return;
          if (!error.isEmpty()) {
            self->environment_ = {{"state", "error"}, {"error", error}};
            emit self->environmentChanged();
            return;
          }
          self->library_ = lib;
          const auto& env = lib->env;
          QVariantList families;
          for (const auto f : {body::BodyFamily::Cbbe3ba, body::BodyFamily::Cbbe, body::BodyFamily::Ube, body::BodyFamily::Other,
                               body::BodyFamily::Unknown}) {
            const auto it = lib->presets.family_counts.find(f);
            families << QVariantMap{{"id", qs(std::string(body::family_id(f)))},
                                    {"label", qs(std::string(body::family_label(f)))},
                                    {"count", it == lib->presets.family_counts.end() ? 0 : static_cast<int>(it->second)}};
          }
          int unreadable = 0;
          for (const auto& f : lib->presets.files) unreadable += f.error.empty() ? 0 : 1;
          self->environment_ = {
              {"state", "ready"},
              {"bcng", qs(env.bcng_dll_provider)},
              {"obody", qs(env.obody_dll_provider)},
              {"racemenu", qs(env.racemenu_dll_provider)},
              {"skypatcher", qs(env.skypatcher_dll_provider)},
              {"globalFamily", qs(env.settings.female_label())},
              {"settingsFromFile", env.settings.from_file},
              {"settingsError", qs(env.settings_error)},
              {"distributionProvider", qs(env.distribution_provider)},
              {"distributionRules", lib->base ? static_cast<int>(lib->base->rules.size()) : 0},
              {"distributionError", qs(lib->base_error)},
              {"installedFamilies", qs(body::mask_label(env.installed_female_mask))},
              {"ubeRacePlugin", env.ube_race_plugin_active},
              {"presets", static_cast<int>(lib->presets.presets.size())},
              {"files", static_cast<int>(lib->presets.files.size())},
              {"unreadableFiles", unreadable},
              {"duplicates", static_cast<int>(lib->presets.duplicates.size())},
              {"families", families},
          };
          emit self->environmentChanged();
          self->rebuild_presets();
          self->rebuild_draft();
          const auto pending = self->npc_.value("pending").toString();
          if (!pending.isEmpty()) self->inspect(pending);
        },
        Qt::QueuedConnection);
  });
}

void BodyDirector::inspect(const QString& form_key) {
  if (form_key.isEmpty() || !snapshot_) return;
  const int serial = ++npc_serial_;
  if (!library_) {  // the library is still loading: inspect as soon as it is ready
    npc_ = {{"state", "inspecting"}, {"formKey", form_key}, {"pending", form_key}};
    emit npcChanged();
    return;
  }
  analysis_.reset();
  npc_ = {{"state", "inspecting"}, {"formKey", form_key}};
  emit npcChanged();
  rebuild_presets();
  worker_->call(
      method::kBodyInspectNpcs, Json{{"formKeys", Json::array({ss(form_key)})}}, 60s, this,
      [this, serial, form_key](const Json& r) {
        if (serial != npc_serial_ || !library_) return;
        const auto items = r.value("items", Json::array());
        if (items.empty()) {
          npc_ = {{"state", "error"}, {"formKey", form_key},
                  {"error", tr("El NPC %1 no es un NPC del orden de carga activo.").arg(form_key)}};
          emit npcChanged();
          return;
        }
        body::NpcBodyInfo info;
        try {
          info = body::npc_body_from_json(items.front());
        } catch (const std::exception& e) {
          npc_ = {{"state", "error"}, {"formKey", form_key}, {"error", QString::fromUtf8(e.what())}};
          emit npcChanged();
          return;
        }
        const int profile = profile_serial_;
        QThreadPool::globalInstance()->start([self = QPointer<BodyDirector>(this), lib = library_, info, serial, profile] {
          auto analysis = std::make_shared<Analysis>();
          analysis->info = info;
          QString error;
          try {
            analysis->body = lib->inspector->analyze(info);
          } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
          }
          QVariantList changes;
          if (error.isEmpty()) {
            for (const auto target : {body::BodyFamily::Cbbe3ba, body::BodyFamily::Ube}) {
              const auto a = body::assess_family_change(analysis->body, target, lib->env);
              changes << QVariantMap{{"target", qs(std::string(body::family_id(target)))},
                                     {"label", qs(std::string(body::family_label(target)))},
                                     {"verdict", qs(std::string(body::verdict_id(a.verdict)))},
                                     {"verdictLabel", qs(std::string(body::verdict_label(a.verdict)))},
                                     {"issues", issue_list(a.issues)}};
            }
          }
          QMetaObject::invokeMethod(
              self.data(),
              [self, serial, profile, analysis, error, changes] {
                if (!self || serial != self->npc_serial_ || profile != self->profile_serial_) return;
                const auto& info = analysis->info;
                if (!error.isEmpty()) {
                  self->npc_ = {{"state", "error"}, {"formKey", qs(info.form_key)}, {"error", error}};
                  emit self->npcChanged();
                  return;
                }
                self->analysis_ = analysis;
                const auto& a = analysis->body;
                QVariantMap body{{"path", qs(a.body.data_path)},
                                 {"provider", qs(a.body.provider)},
                                 {"archived", a.body.archived},
                                 {"shapes", string_list(a.body.shape_names)},
                                 {"tri", qs(a.body.body_tri)},
                                 {"triProvider", qs(a.body.body_tri_provider)},
                                 {"physicsBones", string_list(a.body.physics_bones)}};
                self->npc_ = {{"state", "ready"},
                              {"formKey", qs(info.form_key)},
                              {"name", qs(info.name)},
                              {"editorId", qs(info.editor_id)},
                              {"sex", qs(info.sex_for_rules())},
                              {"race", qs(info.race_for_rules().editor_id)},
                              {"family", qs(std::string(body::family_id(a.family)))},
                              {"familyLabel", qs(std::string(body::family_label(a.family)))},
                              {"familyMask", qs(body::mask_label(a.family_mask))},
                              {"layout", layout_id(a.layout)},
                              {"child", a.child},
                              {"beast", a.beast},
                              {"follower", a.follower_candidate},
                              {"customFace", a.custom_appearance},
                              {"faceProvider", qs(a.facegen_provider)},
                              {"body", body},
                              {"evidence", string_list(a.evidence)},
                              {"issues", issue_list(a.issues)},
                              {"changes", changes}};
                emit self->npcChanged();
                self->rebuild_presets();
              },
              Qt::QueuedConnection);
        });
      },
      [this, serial, form_key](int, const QString& message, const Json&) {
        if (serial != npc_serial_) return;
        npc_ = {{"state", "error"}, {"formKey", form_key}, {"error", tr("El worker no pudo leer el cuerpo del NPC: %1").arg(message)}};
        emit npcChanged();
      });
}

void BodyDirector::setFilter(const QString& text, const QString& family, bool compatible_only) {
  filter_text_ = text.trimmed();
  filter_family_ = family;
  filter_compatible_ = compatible_only;
  rebuild_presets();
}

void BodyDirector::rebuild_presets() {
  presets_.clear();
  preset_total_ = 0;
  if (library_) {
    const auto needle = filter_text_.toLower();
    QString assigned_uid;
    if (analysis_) {
      for (const auto& r : rules_) {
        if (r.form_key == qs(analysis_->info.form_key)) assigned_uid = r.uid;
      }
    }
    for (const auto& p : library_->presets.presets) {
      const auto family = qs(std::string(body::family_id(p.family.family)));
      if (!filter_family_.isEmpty() && family != filter_family_) continue;
      if (!needle.isEmpty() && !qs(p.name).toLower().contains(needle) && !qs(p.provider).toLower().contains(needle) &&
          !qs(p.source).toLower().contains(needle)) {
        continue;
      }
      QVariantMap row{{"uid", qs(p.uid)},
                      {"name", qs(p.name)},
                      {"source", qs(p.source)},
                      {"file", qs(p.file)},
                      {"provider", qs(p.provider)},
                      {"family", family},
                      {"familyLabel", p.family.family == body::BodyFamily::Other ? qs(p.family.other_name)
                                                                                 : qs(std::string(body::family_label(p.family.family)))},
                      {"reason", qs(p.family.reason)},
                      {"groups", string_list(p.groups)},
                      {"sliders", static_cast<int>(p.sliders.size())},
                      {"refit", p.refit},
                      {"duplicate", library_->presets.duplicate_of(p.name) != nullptr},
                      {"assigned", qs(p.uid) == assigned_uid}};
      if (analysis_) {
        const auto a = body::assess_preset(analysis_->body, p, library_->presets, library_->env, /*automatic=*/false);
        if (filter_compatible_ && (a.verdict == body::Verdict::Blocked || a.verdict == body::Verdict::NeedsResources)) continue;
        row["verdict"] = qs(std::string(body::verdict_id(a.verdict)));
        row["verdictLabel"] = qs(std::string(body::verdict_label(a.verdict)));
        row["issues"] = issue_list(a.issues);
      } else {
        row["verdict"] = QString();
        row["verdictLabel"] = tr("Elige un NPC");
        row["issues"] = QVariantList{};
      }
      ++preset_total_;
      if (presets_.size() < kMaxListedPresets) presets_ << row;
    }
  }
  emit presetsChanged();
}

QString BodyDirector::assign(const QString& uid, bool accept_experimental) {
  if (!library_ || !analysis_) return tr("Primero elige un NPC y espera a que se analice su cuerpo.");
  const auto* p = library_->presets.find(ss(uid));
  if (!p) return tr("El preset ya no está en el perfil.");
  const auto a = body::assess_preset(analysis_->body, *p, library_->presets, library_->env, /*automatic=*/false);
  if (a.verdict == body::Verdict::Blocked || a.verdict == body::Verdict::NeedsResources) {
    QStringList why;
    for (const auto& i : a.issues) {
      if (i.severity == "error") why << qs(i.message);
    }
    return tr("No se puede asignar (%1): %2").arg(qs(std::string(body::verdict_label(a.verdict))), why.join(QStringLiteral(" ")));
  }
  if (a.verdict == body::Verdict::Experimental && !accept_experimental) {
    return tr("La asignación es experimental: confirma que aceptas probarla en el juego.");
  }
  const auto& info = analysis_->info;
  DraftRule d;
  d.form_key = qs(info.form_key);
  d.id = rule_id_for(d.form_key);
  d.npc_name = qs(info.name.empty() ? info.editor_id : info.name);
  d.uid = uid;
  d.preset = qs(p->name);
  d.provider = qs(p->provider);
  d.family = qs(std::string(body::family_label(p->family.family)));
  d.verdict = qs(std::string(body::verdict_id(a.verdict)));
  d.verdict_label = qs(std::string(body::verdict_label(a.verdict)));
  d.experimental = a.verdict == body::Verdict::Experimental;
  auto& rule = d.rule;
  rule.id = ss(d.id);
  rule.name = std::format("Skyrim NPC Studio: {} -> {}", info.name.empty() ? info.editor_id : info.name, p->name);
  rule.sex = analysis_->body.female ? "female" : "male";
  rule.scope = body::bcng::Scope::Npc;
  const auto plugin = text::to_lower_ascii(info.defining_plugin());
  const auto light_it = library_->env.light_plugins.find(plugin);
  const bool light = light_it != library_->env.light_plugins.end() && light_it->second;
  try {
    rule.target = body::bcng::form_target(info.form_key, info.name.empty() ? info.editor_id : info.name, light);
  } catch (const std::exception& e) {
    return tr("FormKey no válido para Body Change NG: %1").arg(QString::fromUtf8(e.what()));
  }
  body::bcng::PresetRef ref{p->name, {}};
  // A name shared by several winning files is ambiguous for BCNG: name the file (BOD-04).
  if (library_->presets.duplicate_of(p->name)) ref.file = p->source;
  d.file = qs(ref.file);
  rule.presets = {ref};
  rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [&](const auto& r) { return r.id == d.id; }), rules_.end());
  rules_.push_back(std::move(d));
  diag::record("body", ("assign " + ss(uid)).c_str());
  rebuild_draft();
  rebuild_presets();
  return {};
}

void BodyDirector::removeRule(const QString& id) {
  rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [&](const auto& r) { return r.id == id; }), rules_.end());
  rebuild_draft();
  rebuild_presets();
}

void BodyDirector::clearDraft() {
  rules_.clear();
  rebuild_draft();
  rebuild_presets();
}

void BodyDirector::rebuild_draft() {
  QVariantList rules;
  for (const auto& r : rules_) {
    rules << QVariantMap{{"id", r.id},           {"formKey", r.form_key}, {"npcName", r.npc_name}, {"preset", r.preset},
                         {"file", r.file},       {"provider", r.provider}, {"family", r.family},   {"verdict", r.verdict},
                         {"verdictLabel", r.verdict_label}, {"experimental", r.experimental}};
  }
  QStringList errors;
  merged_text_.clear();
  int base_rules = 0;
  int merged_rules = 0;
  const QString base_provider = library_ ? qs(library_->env.distribution_provider) : QString();
  const QString base_error = library_ ? qs(library_->base_error) : QString();
  if (library_ && !rules_.empty()) {
    if (!library_->base_error.empty()) {
      // Exporting would replace a file BCNG itself may still read: never drop its rules silently.
      errors << tr("La distribución actual de «%1» no se puede leer (%2): no se exporta para no sustituir sus reglas.")
                    .arg(base_provider, base_error);
    } else {
      // NPC rules first: BCNG takes the first matching rule, and a named NPC must beat race/faction/all rules.
      body::bcng::Distribution merged;
      for (const auto& r : rules_) merged.rules.push_back(r.rule);
      if (library_->base) {
        base_rules = static_cast<int>(library_->base->rules.size());
        for (const auto& b : library_->base->rules) {
          const bool replaced = std::any_of(rules_.begin(), rules_.end(), [&](const auto& r) { return ss(r.id) == b.id; });
          if (!replaced) merged.rules.push_back(b);
        }
      }
      merged_rules = static_cast<int>(merged.rules.size());
      for (const auto& e : body::bcng::structural_errors(merged)) errors << qs(e);
      if (errors.isEmpty()) {
        try {
          merged_text_ = body::bcng::write_distribution(merged);
          body::bcng::parse_distribution(merged_text_);  // what BCNG would read back
        } catch (const std::exception& e) {
          errors << QString::fromUtf8(e.what());
          merged_text_.clear();
        }
      }
    }
  }
  draft_ = {{"rules", rules},
            {"baseRules", base_rules},
            {"baseProvider", base_provider},
            {"baseError", base_error},
            {"mergedRules", merged_rules},
            {"errors", errors},
            {"text", qs(merged_text_)}};
  emit draftChanged();
}

void BodyDirector::exportDistribution(const QString& mod_name, const QString& output_dir_or_url, bool create_zip) {
  auto fail = [this](const QString& message) {
    export_result_ = {{"state", "error"}, {"error", message}, {"inGame", "PENDIENTE_SKYRIM"}};
    emit exportResultChanged();
  };
  if (!snapshot_ || !library_) return fail(tr("No hay un perfil MO2 cargado."));
  if (rules_.empty()) return fail(tr("No hay reglas en el borrador."));
  if (merged_text_.empty()) {
    return fail(tr("La distribución no es válida: %1").arg(draft_.value("errors").toStringList().join(QStringLiteral(" "))));
  }
  const auto name = mod_name.trimmed();
  if (name.isEmpty()) return fail(tr("Escribe un nombre para el mod."));
  const auto output = QDir::toNativeSeparators(local_path(output_dir_or_url));
  if (const auto refused = refused_export_directory(output, forbidden_roots_of(*snapshot_)); !refused.isEmpty()) {
    return fail(refused);  // nothing created yet
  }
  QDir().mkpath(output);

  QStringList lines;
  lines << QStringLiteral("Skyrim NPC Studio - Body Change NG distribution (schema 8)") << QString();
  lines << QStringLiteral("Reglas del estudio (van primero; Body Change NG usa la primera regla que coincide):");
  for (const auto& r : rules_) {
    lines << QStringLiteral("- %1 (%2): preset \"%3\"%4 de %5 [%6]")
                 .arg(r.npc_name, r.form_key, r.preset, r.file.isEmpty() ? QString() : QStringLiteral(" (archivo %1)").arg(r.file),
                      r.provider, r.verdict_label);
  }
  lines << QString();
  if (library_->base) {
    lines << QStringLiteral("Incluye las %1 reglas que tenía el archivo de \"%2\", sin cambios y detrás de las del estudio.")
                 .arg(library_->base->rules.size())
                 .arg(qs(library_->env.distribution_provider));
    lines << QStringLiteral("Los comentarios del archivo original no se copian; el original no se modifica.");
    lines << QStringLiteral("Para que este archivo gane, coloque este mod DEBAJO de \"%1\" en el panel izquierdo de MO2 (prioridad mayor).")
                 .arg(qs(library_->env.distribution_provider));
  } else {
    lines << QStringLiteral("El perfil no tenía BodyChangeNGdistribution.json.");
  }
  lines << QString() << QStringLiteral("Estado: PAQUETE_MO2 generado y verificado por lectura; PENDIENTE_SKYRIM (no probado en el juego).");
  lines << QStringLiteral("Prueba recomendada: perfil MO2 aislado, Skyrim cerrado al instalar, F7 (menú de BCNG) o nueva partida.");

  Json files = Json::array({
      Json{{"relativePath", "SKSE/Plugins/BodyChangeNGdistribution.json"}, {"content", merged_text_}},
      Json{{"relativePath", "SNS_BodyDistribution_README.txt"}, {"content", ss(lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n"))}},
  });
  Json metadata{{"profile", snapshot_->profile.name},
                {"baseDistributionProvider", library_->env.distribution_provider},
                {"studioRules", std::to_string(rules_.size())},
                {"inGameVerification", "PENDIENTE_SKYRIM"}};
  export_result_ = {{"state", "exporting"}};
  emit exportResultChanged();
  diag::record("body", ("export " + ss(name)).c_str());
  worker_->call(
      method::kExportBodyDistribution,
      Json{{"modName", ss(name)}, {"outputDirectory", ss(output)}, {"createZip", create_zip}, {"files", files},
           {"description", "Body Change NG distribution generated by Skyrim NPC Studio"}, {"metadata", metadata}},
      120s, this,
      [this](const Json& r) {
        QVariantList files;
        for (const auto& f : r.value("files", Json::array())) {
          files << QVariantMap{{"path", qs(f.value("path", ""))}, {"bytes", static_cast<qlonglong>(f.value("bytes", 0ll))},
                               {"sha256", qs(f.value("sha256", ""))}};
        }
        QStringList verification;
        for (const auto& v : r.value("verification", Json::array())) {
          if (v.is_string()) verification << qs(v.get<std::string>());
        }
        export_result_ = {{"state", "done"},
                          {"modDirectory", qs(r.value("modDirectory", ""))},
                          {"zipPath", r.contains("zipPath") && r["zipPath"].is_string() ? qs(r["zipPath"].get<std::string>()) : QString()},
                          {"manifestPath", qs(r.value("manifestPath", ""))},
                          {"files", files},
                          {"verification", verification},
                          {"status", "PAQUETE_MO2_VERIFICADO"},
                          {"inGame", "PENDIENTE_SKYRIM"}};
        emit exportResultChanged();
      },
      [this](int, const QString& message, const Json&) {
        export_result_ = {{"state", "error"}, {"error", message}, {"inGame", "PENDIENTE_SKYRIM"}};
        emit exportResultChanged();
      });
}

}  // namespace sns::app
