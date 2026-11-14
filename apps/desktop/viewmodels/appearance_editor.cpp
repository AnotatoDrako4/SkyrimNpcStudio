#include "appearance_editor.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <filesystem>

#include "diagnostics/crash_diagnostics.hpp"
#include "export_destination.hpp"
#include "json_bridge.hpp"
#include "outfit_editor.hpp"
#include "resource_context.hpp"
#include "sns/appearance/easy_mod.hpp"
#include "sns/appearance/review_case.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/face/face_tint.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/presets/jslot_parser.hpp"
#include "sns/presets/preset_model.hpp"
#include "sns/textures/dds.hpp"

namespace sns::app {

using namespace std::chrono_literals;
namespace method = ipc::protocol::method;

namespace {

QColor qcolor(const face::Rgb8& c) { return QColor(c.r, c.g, c.b); }
face::Rgb8 rgb8(const QColor& c) {
  return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue())};
}
Json rgb_json(const face::Rgb8& c) { return {{"r", c.r}, {"g", c.g}, {"b", c.b}}; }

QVariantMap issue_map(const face::FaceIssue& i) {
  return {{"severity", qs(i.severity)}, {"code", qs(i.code)}, {"message", qs(i.message)}, {"subject", qs(i.subject)}};
}

QString local_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

std::filesystem::path fs_path(const QString& p) { return std::filesystem::path(p.toStdWString()); }

QString sha256_hex(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).toUpper(); }

QString color_text(const std::optional<face::Rgb8>& c) {
  return c ? QColor(c->r, c->g, c->b).name() : QStringLiteral("(sin color)");
}

// ------------------------------------------------------------------------- face compatibility (UI)

QString operation_label(const std::string& op) {
  if (op == "hair-color") return QObject::tr("Color de cabello (Hair Color Safe Mode)");
  if (op == "outfit") return QObject::tr("Vestuario");
  if (op == "face-morphs") return QObject::tr("Sliders y presets del rostro");
  if (op == "head-parts") return QObject::tr("Cambiar piezas de cabeza (Head Parts)");
  if (op == "regenerate") return QObject::tr("Regenerar la cabeza desde el registro");
  if (op == "skin-tone") return QObject::tr("Tono de piel y tintes del rostro");
  return qs(op);
}

QString compatibility_label(const std::string& code) {
  if (code == "vanilla-compatible") return QObject::tr("Geometría compatible con el registro");
  if (code == "custom-limited") return QObject::tr("Geometría personalizada: edición limitada");
  if (code == "custom-incompatible-morphs") return QObject::tr("Geometría personalizada con morphs incompatibles");
  if (code == "facegen-missing") return QObject::tr("FaceGen ausente");
  if (code == "facegen-inconsistent") return QObject::tr("FaceGen inconsistente");
  if (code == "missing-dependencies") return QObject::tr("Dependencias faltantes");
  return QObject::tr("Estado indeterminado");
}

QString blocked_reason(const face::FaceCompatibilityReport& r, const std::string& op) {
  using face::FaceCompatibility;
  if (op == "hair-color") {
    if (r.kind == FaceCompatibility::FaceGenMissing) return QObject::tr("No hay FaceGen del perfil que recolorear.");
    if (!r.hair_tint && r.kind != FaceCompatibility::FaceGenInconsistent) {
      return QObject::tr("Ninguna pieza del FaceGen usa el shader HairTint: un color CLFM no cambia este cabello. Hace falta "
                         "editar su textura o material por separado (no se modifica el shader de terceros).");
    }
    return QObject::tr("El FaceGen ganador no se puede leer.");
  }
  if (op == "skin-tone" && r.kind != FaceCompatibility::FaceGenInconsistent) {
    return QObject::tr("La cabeza usa su propia textura de cara (ranura 6) en lugar del FaceTint: cambiar el tono de piel o los "
                       "tintes la sustituiría por un FaceTint generado y cambiaría su aspecto.");
  }
  if (op == "regenerate" && (r.kind == FaceCompatibility::CustomLimited || r.kind == FaceCompatibility::CustomIncompatibleMorphs)) {
    return QObject::tr("Regenerar desde el registro perdería la geometría personalizada y el esculpido.");
  }
  switch (r.kind) {
    case FaceCompatibility::CustomIncompatibleMorphs:
      return op == "face-morphs" ? QObject::tr("Los TRI de la cabeza no coinciden con su malla: los sliders y presets no se pueden aplicar.")
                                 : QObject::tr("Sin verificar en una cabeza cuyos morphs no coinciden con la malla.");
    case FaceCompatibility::MissingDependencies:
      return QObject::tr("Faltan mallas o TRI de los Head Parts en el perfil.");
    case FaceCompatibility::FaceGenInconsistent:
      return QObject::tr("El FaceGen ganador no se hizo con los Head Parts del registro (o no se puede leer).");
    case FaceCompatibility::FaceGenMissing:
      return QObject::tr("La cabeza no se puede construir con los Head Parts del registro.");
    default:
      return QObject::tr("No se pudo regenerar la cabeza para comprobarlo.");
  }
}

QVariantMap compatibility_map(const face::FaceCompatibilityReport& r, bool regenerate_mode = false) {
  QVariantList allowed, blocked, mismatches;
  const bool unlocked = regenerate_mode && r.regenerable_from_record && !r.blocked.empty();
  if (unlocked) {
    for (const char* op : {"hair-color", "face-morphs", "head-parts", "skin-tone", "outfit", "regenerate"}) {
      allowed << QVariantMap{{"op", qs(op)}, {"label", operation_label(op)}};
    }
  } else {
    for (const auto& op : r.allowed) allowed << QVariantMap{{"op", qs(op)}, {"label", operation_label(op)}};
    for (const auto& [op, english] : r.blocked) {
      blocked << QVariantMap{{"op", qs(op)}, {"label", operation_label(op)}, {"reason", blocked_reason(r, op)}, {"detail", qs(english)}};
    }
  }
  for (const auto& m : r.tri_mismatches) {
    mismatches << QVariantMap{{"part", qs(m.part)}, {"tri", qs(m.tri)}, {"triVertices", static_cast<qulonglong>(m.tri_vertices)},
                              {"meshVertices", static_cast<qulonglong>(m.mesh_vertices)}};
  }
  QStringList evidence, missing;
  for (const auto& e : r.evidence) evidence << qs(e);
  for (const auto& m : r.missing) missing << qs(m);
  const bool keep_missing_code = r.kind == face::FaceCompatibility::FaceGenMissing;
  const QString effective_code = (unlocked && !keep_missing_code) ? QStringLiteral("unlocked-regenerate") : qs(r.code);
  const QString effective_label = (unlocked && !keep_missing_code)
                                      ? QObject::tr("Desbloqueado para modear (regeneración completa desde el registro)")
                                      : compatibility_label(r.code);
  return {{"code", effective_code},
          {"baseCode", qs(r.code)},
          {"label", effective_label},
          {"allowed", allowed},
          {"blocked", blocked},
          {"triMismatches", mismatches},
          {"missing", missing},
          {"evidence", evidence},
          {"hairTint", r.hair_tint},
          {"deviation", r.regeneration_deviation},
          {"regenerableFromRecord", r.regenerable_from_record},
          {"unlockedForModding", unlocked},
          {"canUnlockForModding", r.regenerable_from_record && !unlocked && !blocked.isEmpty()}};
}

}  // namespace

// One regeneration of the face: the state it was made from and the exact files it produced.
struct AppearanceEditor::Build {
  std::uint64_t task = 0;
  std::string npc;
  std::string kind;  // "full" or "hair-safe" (Hair Color Safe Mode on the winning FaceGen)
  appearance::FaceState state;
  face::FaceGenBuild facegen;
  std::shared_ptr<const std::string> nif;
  std::shared_ptr<const textures::DdsImage> tint_rgba;     // null: the tint was not regenerated
  std::shared_ptr<const std::string> tint_preview_dds;     // uncompressed copy for the preview
  std::vector<std::string> tint_layers;
  std::vector<face::FaceIssue> tint_issues;
  std::vector<std::string> recolored;  // hair-safe: shapes whose HairTint color was rewritten
  qint64 elapsed_ms = 0;
};

namespace {

int setting_ms(const char* key, int fallback) {
  bool ok = false;
  const int value = QSettings().value(QString::fromLatin1(key), fallback).toInt(&ok);
  return ok ? std::clamp(value, 0, 5000) : fallback;
}

// MO2 mods (and overwrite) with loose FaceGen/FaceTint files of this NPC, ascending priority: an
// exported FaceGen must be installed above all of them, or the game shows their head with the new
// record. Archives never compete (a loose file always wins over a BSA), nor does the game's Data.
std::vector<std::string> facegen_competitors(const mo2::VirtualFileSystem& vfs, const face::FaceRecipe& recipe) {
  std::vector<std::string> mods;
  for (const auto* path : {&recipe.facegen_mesh_path, &recipe.facegen_tint_path}) {
    for (const auto i : vfs.providers_of(*path)) {
      const auto& p = vfs.providers()[i];
      if (p.kind != mo2::ProviderKind::GameData && std::find(mods.begin(), mods.end(), p.name) == mods.end()) mods.push_back(p.name);
    }
  }
  return mods;
}

constexpr const char* kVirtualBaldFormKey = "000000:BALD";

bool has_virtual_bald(const std::vector<std::string>& head_parts) {
  return std::find(head_parts.begin(), head_parts.end(), std::string(kVirtualBaldFormKey)) != head_parts.end();
}

QString classify_head_part_package(const QString& plugin, const QString& model_path, const QString& editor_id) {
  const QString combined = (plugin + " " + model_path + " " + editor_id).toLower();
  if (combined.contains("kshair") || combined.contains("ks hair")) return QStringLiteral("KS Hairdos");
  if (combined.contains("apachii")) return QStringLiteral("ApachiiSkyHair");
  if (combined.contains("hghair") || combined.contains("hg hair")) return QStringLiteral("HG Hairdos");
  if (combined.contains("yundao") || combined.contains("dint999")) return QStringLiteral("HDT-SMP Hair Pack");
  if (combined.contains("highpolyhead") || combined.contains("kl_highpoly")) return QStringLiteral("High Poly Head");
  if (combined.contains("eyesofbeauty") || combined.contains("theeyesofbeauty")) return QStringLiteral("The Eyes of Beauty");
  if (combined.contains("brows.esp") || combined.contains("hvergelmir")) return QStringLiteral("Hvergelmir Brows / Beards");
  if (combined.contains("beards.esp")) return QStringLiteral("Beards");
  static const QStringList vanilla_masters = {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"};
  if (vanilla_masters.contains(plugin.toLower()) || plugin.toLower().startsWith("cc")) return QStringLiteral("Vanilla / Creation Club");
  return plugin.isEmpty() ? QStringLiteral("Otro") : plugin;
}

QString tint_mask_category_label(int mask_type, const QString& path) {
  const QString lower = path.toLower();
  if (mask_type == face::kTintMaskSkinTone || lower.contains("skintone")) return QStringLiteral("Piel");
  if (mask_type == 1 || mask_type == 2 || mask_type == 3 || mask_type == 4 || mask_type == 5 || mask_type == 9 ||
      lower.contains("lips") || lower.contains("eyeliner") || lower.contains("eyesocket") || lower.contains("cheek")) {
    return QStringLiteral("Maquillaje");
  }
  if (mask_type == 7 || lower.contains("warpaint") || lower.contains("paint") || lower.contains("scar")) {
    return QStringLiteral("Pintura y cicatrices");
  }
  return QStringLiteral("Rasgos y sombras");
}

QString tint_mask_display_name(int index, int mask_type, const QString& path) {
  const QString base = QFileInfo(QString(path).replace('\\', '/')).completeBaseName();
  if (mask_type == face::kTintMaskSkinTone) return QStringLiteral("Tono de piel base (#%1)").arg(index);
  if (!base.isEmpty()) return QStringLiteral("%1 (#%2)").arg(base).arg(index);
  return QStringLiteral("Capa de tinte #%1").arg(index);
}

}  // namespace

AppearanceEditor::AppearanceEditor(WorkerClient* worker, ConsoleModel* console, OutfitEditor* outfit, SnapshotProvider snapshot,
                                   appearance::AppearanceDraft& draft, EditHistory* history, QObject* parent)
    : QObject(parent), worker_(worker), console_(console), outfit_(outfit), snapshot_(std::move(snapshot)), draft_(draft), history_(history) {
  pool_.setMaxThreadCount(1);
  build_timer_.setSingleShot(true);
  // Pause after the last edit before the FaceGen is rebuilt (configurable: preview/faceBuildDebounceMs).
  build_timer_.setInterval(setting_ms("preview/faceBuildDebounceMs", 60));
  connect(&build_timer_, &QTimer::timeout, this, &AppearanceEditor::start_build);
  // Any undo/redo (face or outfit): refresh, and follow head part changes with a new recipe.
  connect(history_, &EditHistory::changed, this, [this] {
    refresh_compatibility_map(true);
    emit appearanceChanged();
    if (!recipe_ || !draft_.face_loaded()) return;
    if (draft_.face().head_parts != recipe_->npc_head_parts || draft_.face().head_texture != recipe_->npc_head_texture) {
      request_recipe(false);
    } else {
      schedule_build();
    }
  });
  // Easy Mod Creator: the plan follows the face, the FaceGen report and the outfit validation.
  for (auto signal : {&AppearanceEditor::faceGenChanged, &AppearanceEditor::appearanceChanged, &AppearanceEditor::recipeChanged,
                      &AppearanceEditor::targetChanged, &AppearanceEditor::exportingChanged}) {
    connect(this, signal, this, &AppearanceEditor::easyModChanged);
  }
  if (outfit_) {
    connect(outfit_, &OutfitEditor::changed, this, &AppearanceEditor::easyModChanged);
    connect(outfit_, &OutfitEditor::validationChanged, this, &AppearanceEditor::easyModChanged);
  }
}

AppearanceEditor::~AppearanceEditor() {
  // Closing with a build or analysis in flight: ask them to stop, then wait. Their results are
  // posted to this object and dropped with it; nothing runs on a destroyed editor.
  build_timer_.stop();
  cancel_jobs();
  pool_.waitForDone();
}

void AppearanceEditor::cancel_jobs() {
  if (running_) {
    running_->cancel->store(true);
    diag::record("facegen", QStringLiteral("cancel #%1 (npc %2)").arg(running_->task).arg(qs(running_->npc)).toStdString());
  }
  running_.reset();
  rebuild_pending_ = false;
  if (analysis_cancel_) analysis_cancel_->store(true);
}

std::shared_ptr<ResourceContext> AppearanceEditor::context() const { return ResourceContext::for_snapshot(snapshot_()); }

// ------------------------------------------------------------------------------------- target

void AppearanceEditor::set_target(const projects::NpcTarget& target) {
  // Work started for the previous NPC is cancelled; anything it still reports is discarded
  // because it carries that NPC's FormKey (finish_build).
  build_timer_.stop();
  cancel_jobs();
  target_ = target;
  resolution_.reset();  // a resolution belongs to the NPC it was offered for
  resolution_npc_.clear();
  ++head_part_serial_;  // options of the previous NPC are not valid for this one
  head_part_search_ = {{"state", "none"}};
  head_part_options_.clear();
  emit headPartOptionsChanged();
  diag::set_context("npc", target.form_key + " " + target.editor_id + " (" + target.name + ")");
  diag::set_context("winningPlugin", target.winning_plugin);
  base_recipe_.reset();
  recipe_.reset();
  existing_facegen_.reset();
  has_existing_tint_ = false;
  base_compatibility_report_.reset();
  base_provenance_.clear();
  compatibility_.clear();
  geometry_reason_.clear();
  existing_issues_.clear();
  recipe_issues_.clear();
  build_.reset();
  overlay_.reset();
  ++geometry_revision_;
  facegen_report_.clear();
  load_error_.clear();
  building_ = false;
  emit targetChanged();
  emit appearanceChanged();
  emit faceGenChanged();
  request_recipe(true);
}

void AppearanceEditor::clear() {
  build_timer_.stop();
  cancel_jobs();
  target_ = {};
  base_recipe_.reset();
  recipe_.reset();
  existing_facegen_.reset();
  has_existing_tint_ = false;
  base_compatibility_report_.reset();
  base_provenance_.clear();
  compatibility_.clear();
  build_.reset();
  overlay_.reset();
  ++geometry_revision_;
  ++recipe_serial_;
  loading_ = building_ = false;
  emit targetChanged();
  emit recipeChanged();
  emit appearanceChanged();
  emit faceGenChanged();
}

void AppearanceEditor::queue_saved_face(const std::string& form_key, const std::optional<projects::FaceConfig>& face) {
  if (face) saved_faces_[form_key] = *face;
  else saved_faces_.erase(form_key);
}

std::optional<projects::FaceConfig> AppearanceEditor::face_config_for_project() const {
  if (!draft_.face_loaded() || !(draft_.is_face_dirty() || draft_.is_height_dirty()) || !recipe_) return std::nullopt;
  auto config = appearance::to_face_config(draft_.face(), *recipe_);
  config.height = draft_.height();
  return config;
}

FacePreview AppearanceEditor::preview() const {
  FacePreview p;
  p.geometry_revision = geometry_revision_;
  if (!draft_.face_loaded()) return p;
  p.npc_form_key = target_.form_key;
  const auto& v = draft_.face().values;
  p.weight = v.weight;
  p.height = draft_.height();
  p.hair_color = v.hair_color;
  p.skin_tone = v.skin_tone;
  p.active = draft_.is_face_dirty() || draft_.is_height_dirty();
  p.overlay = overlay_;
  return p;
}

void AppearanceEditor::request_recipe(bool initial) {
  if (target_.form_key.empty() || !worker_) return;
  Json req{{"npcFormKey", target_.form_key}};
  if (!initial) {
    std::vector<std::string> requested_parts = draft_.face().head_parts;
    std::erase(requested_parts, std::string(kVirtualBaldFormKey));
    req["headPartFormKeys"] = requested_parts;
    req["headTextureFormKey"] = draft_.face().head_texture;
  }
  const int serial = ++recipe_serial_;
  loading_ = true;
  emit recipeChanged();
  worker_->call(method::kGetFaceRecipe, req, 30s, this,
                [this, initial, serial](const Json& json) { on_recipe(json, initial, serial); },
                [this, serial](int, const QString& error, const Json&) {
                  if (serial != recipe_serial_) return;
                  loading_ = false;
                  load_error_ = error;
                  emit recipeChanged();
                  console_->append("warning", "appearance", tr("No se pudo leer el rostro del NPC: %1").arg(error));
                });
}

void AppearanceEditor::on_recipe(const Json& json, bool initial, int serial) {
  if (serial != recipe_serial_) return;
  loading_ = false;
  face::FaceRecipe recipe;
  try {
    recipe = face::recipe_from_json(json);
  } catch (const std::exception& e) {
    load_error_ = qs(e.what());
    emit recipeChanged();
    return;
  }
  load_error_.clear();
  recipe_issues_ = to_list(json.value("issues", Json::array()));
  if (!initial && draft_.face_loaded() && has_virtual_bald(draft_.face().head_parts)) {
    recipe.npc_head_parts = draft_.face().head_parts;
    for (auto& p : recipe.parts) {
      if (p.type == face::HeadPartType::Hair) {
        p.form_key = kVirtualBaldFormKey;
        p.editor_id = "IntentionallyBald";
        p.model.clear();
        p.tris.clear();
      }
    }
  }
  if (initial) {
    base_recipe_ = recipe;
    recipe_ = recipe;
    // The geometry mode stays "pending" until analyse_existing_facegen() has compared the profile's
    // FaceGen with the record: no edit and no build may start with a provisional mode (a custom
    // head built with "regenerate" loses its sculpt).
    auto baseline = appearance::face_state_from_recipe(recipe);
    baseline.geometry_mode = appearance::kGeometryPending;
    draft_.set_face_baseline(baseline);
    draft_.set_height_baseline(recipe.height);
    if (const auto it = saved_faces_.find(target_.form_key); it != saved_faces_.end()) {
      std::vector<std::string> notes;
      auto saved = appearance::from_face_config(it->second, draft_.face_baseline(), recipe, &notes);
      // A saved mode may be a provisional one written before the analysis had finished (older
      // versions): the analysis of the current profile decides again. "preserve" and
      // "regenerate" only differ when the FaceGen does not come from the record, which is exactly
      // when regenerating would lose a sculpt, so an explicit "regenerate" must be chosen again.
      if (saved.geometry_mode == "regenerate") {
        notes.push_back(tr("El proyecto pedía regenerar la cabeza de %1; el modo se decide de nuevo al analizar su "
                           "FaceGen para no perder un esculpido. Elige «Regenerar» otra vez si era intencionado.")
                            .arg(npc_label())
                            .toStdString());
      }
      saved.geometry_mode = appearance::kGeometryPending;
      draft_.set_face(std::move(saved));
      if (it->second.height > 0.0f && it->second.height < 10.0f) draft_.set_height(it->second.height);
      for (const auto& n : notes) console_->append("warning", "project", qs(n));
      console_->append("info", "project", tr("Rostro del proyecto aplicado a %1.").arg(npc_label()));
      saved_faces_.erase(it);
    }
    emit recipeChanged();
    emit appearanceChanged();
    analyse_existing_facegen();
    if (draft_.face().head_parts != recipe.npc_head_parts || draft_.face().head_texture != recipe.npc_head_texture) {
      request_recipe(false);
    } else {
      schedule_build();
    }
    return;
  }
  recipe_ = recipe;
  emit recipeChanged();
  emit appearanceChanged();
  schedule_build();
}

// Decides how edits reach the game: if the profile's FaceGen is exactly what the record produces,
// edits regenerate the head like the Creation Kit; otherwise (RaceMenu export, sculpt, custom
// morphs) they are applied as a difference on top of it so the existing look is kept.
void AppearanceEditor::analyse_existing_facegen() {
  auto ctx = context();
  if (!ctx || !base_recipe_) return;
  auto recipe = *base_recipe_;
  recipe.morph_extensions = morph_extensions(*ctx);
  QPointer<AppearanceEditor> self(this);
  const int recipe_serial = recipe_serial_;
  if (analysis_cancel_) analysis_cancel_->store(true);
  analysis_cancel_ = std::make_shared<std::atomic<bool>>(false);
  const auto task = diag::next_task_id();
  pool_.start([self, ctx, recipe, recipe_serial, task, winning_plugin = target_.winning_plugin, cancel = analysis_cancel_] {
    struct Result {
      std::shared_ptr<const std::string> existing;
      bool tint = false;
      QString mode = QStringLiteral("regenerate");
      QString reason;
      QVariantList issues;
      face::FaceCompatibilityReport report;
      QVariantMap provenance;
    };
    auto result = std::make_shared<Result>();
    diag::Operation op("facegen.analyse", task, "npc " + recipe.npc_form_key);
    if (cancel->load()) return;
    try {
      op.stage("read existing FaceGen");
      if (auto bytes = ctx->resolver->read(recipe.facegen_mesh_path)) result->existing = std::make_shared<const std::string>(std::move(*bytes));
      result->tint = ctx->resolver->locate(recipe.facegen_tint_path).has_value();
      op.stage("regenerate for comparison");
      face::FaceGenBuildOptions options;
      options.cancelled = [cancel] { return cancel->load(); };
      const auto regen = face::build_facegen(recipe, *ctx->face_assets, options);
      if (regen.cancelled) return;
      op.stage("compatibility");
      result->report = face::diagnose_face_compatibility(recipe, *ctx->face_assets, result->existing.get(), regen);
      // Where each piece comes from (MO2), and whether the record and its FaceGen come from the same mod.
      QVariantMap provenance{{"winningPlugin", qs(winning_plugin)}};
      for (const auto& p : ctx->snapshot->load_order.active) {
        if (text::iequals_ascii(p.name, winning_plugin)) provenance["pluginProvider"] = qs(p.provider);
      }
      const auto where = [](const std::optional<assets::AssetLocation>& loc) {
        if (!loc) return QVariantMap{};
        return QVariantMap{{"provider", qs(loc->provider)},
                           {"kind", loc->kind == assets::AssetLocation::Kind::Loose ? QStringLiteral("suelto") : QStringLiteral("BSA")},
                           {"archiveProvider", qs(loc->archive_provider)}};
      };
      const auto mesh_at = where(ctx->resolver->locate(recipe.facegen_mesh_path));
      provenance["faceGen"] = mesh_at;
      provenance["faceTint"] = where(ctx->resolver->locate(recipe.facegen_tint_path));
      provenance["faceGenPath"] = qs(recipe.facegen_mesh_path);
      QStringList competitors;
      for (const auto& m : facegen_competitors(ctx->snapshot->vfs, recipe)) competitors << qs(m);
      provenance["faceGenCompetitors"] = competitors;
      const auto facegen_mod = mesh_at.value("kind").toString() == "suelto" ? mesh_at.value("provider").toString()
                                                                             : mesh_at.value("archiveProvider").toString();
      const auto plugin_mod = provenance.value("pluginProvider").toString();
      if (!facegen_mod.isEmpty() && !plugin_mod.isEmpty() && facegen_mod.compare(plugin_mod, Qt::CaseInsensitive) != 0) {
        provenance["conflict"] = QObject::tr("El registro ganador (%1) viene de «%2» y su FaceGen de «%3»: posible incompatibilidad "
                                             "entre replacers. No se cambia el orden de carga automáticamente.")
                                     .arg(qs(winning_plugin), plugin_mod, facegen_mod);
      }
      result->provenance = provenance;
      if (!result->existing) {
        result->reason = QObject::tr("El NPC no tiene FaceGen en el perfil: el juego construye la cabeza en tiempo real y suele "
                                     "mostrar la cara oscura. Exportar genera un FaceGen coherente con el registro.");
        result->issues << QVariantMap{{"severity", "warning"}, {"code", "NoFaceGen"}, {"message", result->reason},
                                      {"subject", qs(recipe.facegen_mesh_path)}};
      } else {
        op.stage("audit");
        for (const auto& i : face::audit_facegen(face::inspect_facegen(*result->existing), recipe)) result->issues << issue_map(i);
        const auto cmp = face::compare_facegen(regen.nif, *result->existing);
        if (regen.exportable() && cmp.same_topology && cmp.max_distance < 0.05f) {
          result->reason = QObject::tr("El FaceGen actual coincide con el que el registro produce (desviación máxima %1 unidades): "
                                       "las ediciones regeneran la cabeza como el Creation Kit.").arg(cmp.max_distance, 0, 'f', 3);
        } else {
          result->mode = QStringLiteral("preserve");
          result->reason = cmp.same_shapes
              ? QObject::tr("El FaceGen actual no sale del registro (diferencia de hasta %1 unidades): contiene esculpido o morphs "
                            "de RaceMenu. Las ediciones se aplican como diferencia sobre él para conservar ese aspecto.")
                    .arg(cmp.max_distance, 0, 'f', 2)
              : QObject::tr("El FaceGen actual tiene otras formas que las piezas del registro (cabeza exportada o modificada). "
                            "Se conserva lo que coincide y las piezas nuevas se regeneran.");
        }
      }
    } catch (const std::exception& e) {
      op.fail(e.what());
      result->issues << QVariantMap{{"severity", "warning"}, {"code", "FaceGenAnalysisFailed"}, {"message", qs(e.what())}};
      // Not known to come from the record: never regenerate over it.
      if (result->existing) result->mode = QStringLiteral("preserve");
    }
    op.succeed();
    QMetaObject::invokeMethod(self, [self, result, recipe_serial] {
      if (!self || recipe_serial != self->recipe_serial_) return;
      self->existing_facegen_ = result->existing;
      self->has_existing_tint_ = result->tint;
      self->geometry_reason_ = result->reason;
      self->existing_issues_ = result->issues;
      self->base_compatibility_report_ = result->report;
      self->base_provenance_ = result->provenance;
      self->draft_.set_baseline_geometry_mode(ss(result->mode));
      self->refresh_compatibility_map(false);
      diag::record("facegen", ("compatibility " + ss(self->compatibility_.value("code").toString())).c_str());
      self->enforce_face_blocks();
      emit self->appearanceChanged();
      emit self->faceGenChanged();
      self->schedule_build();
    }, Qt::QueuedConnection);
  });
}

void AppearanceEditor::refresh_compatibility_map(bool emit_signal) {
  if (!base_compatibility_report_) return;
  const bool regenerate_active = draft_.face_loaded() && draft_.face().geometry_mode == "regenerate";
  QVariantMap next = compatibility_map(*base_compatibility_report_, regenerate_active);
  if (!base_provenance_.isEmpty()) next["provenance"] = base_provenance_;
  if (next == compatibility_) return;
  compatibility_ = std::move(next);
  if (!head_part_options_.isEmpty()) {
    const bool blocked = operation_blocked(QStringLiteral("head-parts"));
    for (int i = 0; i < head_part_options_.size(); ++i) {
      auto item = head_part_options_[i].toMap();
      const bool mesh_found = item.value("meshFound").toBool();
      item["headPartBlocked"] = blocked;
      item["compatibilityState"] = blocked ? QStringLiteral("blocked-custom-head")
                                           : (!mesh_found ? QStringLiteral("missing-assets") : QStringLiteral("compatible"));
      head_part_options_[i] = item;
    }
    emit headPartOptionsChanged();
  }
  if (emit_signal) emit faceGenChanged();
}

std::vector<face::MorphExtension> AppearanceEditor::morph_extensions(const ResourceContext& ctx) const {
  if (extensions_cache_ && extensions_snapshot_ == ctx.snapshot) return *extensions_cache_;
  std::vector<face::MorphExtension> out;
  for (const auto& plugin : ctx.snapshot->load_order.active) {
    const auto ini = std::string(face::kFaceGenMorphsRoot) + plugin.name + "\\morphs.ini";
    if (auto text = ctx.resolver->read(ini)) {
      auto parsed = face::parse_morphs_ini(*text);
      out.insert(out.end(), parsed.begin(), parsed.end());
    }
  }
  extensions_cache_ = out;
  extensions_snapshot_ = ctx.snapshot;
  return out;
}

// ------------------------------------------------------------------------------------- build

void AppearanceEditor::schedule_build() { build_timer_.start(); }

void AppearanceEditor::start_build() {
  if (!recipe_ || !draft_.face_loaded()) return;
  // The recipe must describe the draft's head parts before a build makes sense.
  if (draft_.face().head_parts != recipe_->npc_head_parts) return;
  // One job at a time, latest wins: the running build is asked to stop and the newest draft is
  // built as soon as it has returned (finish_build). The queue never grows with each edit.
  if (running_) {
    running_->cancel->store(true);
    rebuild_pending_ = true;
    return;
  }
  rebuild_pending_ = false;
  const auto state = draft_.face();
  const auto& baseline = draft_.face_baseline();
  const auto decision = appearance::face_build_kind(baseline, state);
  if (decision == appearance::FaceBuildKind::Unchanged || decision == appearance::FaceBuildKind::WaitForAnalysis) {
    build_.reset();
    if (overlay_) ++geometry_revision_;
    overlay_.reset();
    building_ = false;
    facegen_report_ = {{"status", decision == appearance::FaceBuildKind::Unchanged ? "unchanged" : "analysing"}, {"mode", geometry_mode()}};
    emit faceGenChanged();
    return;
  }
  auto ctx = context();
  if (!ctx) return;
  auto recipe = *recipe_;
  recipe.values = state.values;
  recipe.morph_extensions = morph_extensions(*ctx);

  // Hair Color Safe Mode: only HCLF differs from the record and the profile has a winning FaceGen,
  // so it is recolored in place without rebuilding the head from its parts.
  const bool hair_safe = decision == appearance::FaceBuildKind::HairColorSafe && existing_facegen_ != nullptr;
  const auto hair_safe_base = hair_safe ? existing_facegen_ : nullptr;

  face::FaceGenBuildOptions options;
  if (!hair_safe && state.geometry_mode == "preserve" && existing_facegen_) {
    options.mode = face::GeometryMode::PreserveExisting;
    options.existing_facegen = existing_facegen_;
    options.baseline = baseline.values;
  }
  const bool mode_regenerated = state.geometry_mode == "regenerate" && baseline.geometry_mode != "regenerate";
  const bool tint_changed = mode_regenerated || state.values.tints != baseline.values.tints ||
                            state.values.skin_tone != baseline.values.skin_tone || !has_existing_tint_;
  Job job{diag::next_task_id(), target_.form_key, std::make_shared<std::atomic<bool>>(false)};
  options.cancelled = [cancel = job.cancel] { return cancel->load(); };
  running_ = job;
  building_ = true;
  const auto kind = hair_safe ? std::string("hair-safe") : std::string("full");
  diag::record("facegen", QStringLiteral("start #%1 %2 npc=%3 mode=%4 hair=%5")
                              .arg(job.task).arg(qs(kind), qs(job.npc), qs(state.geometry_mode), color_text(state.values.hair_color))
                              .toStdString());
  emit faceGenChanged();
  QPointer<AppearanceEditor> self(this);
  pool_.start([self, ctx, recipe, options, state, tint_changed, job, hair_safe, hair_safe_base, kind] {
    auto b = std::make_shared<Build>();
    b->task = job.task;
    b->npc = job.npc;
    b->kind = kind;
    b->state = state;
    QElapsedTimer timer;
    timer.start();
    diag::Operation op("facegen." + kind, job.task, "npc " + job.npc);
    try {
      if (job.cancel->load()) {
        b->facegen.cancelled = true;
      } else if (hair_safe) {
        if (!hair_safe_base) {
          // No silent fallback to build_facegen(): rebuilding a head the profile does not have
          // would replace it with geometry derived from the head parts.
          b->facegen.issues.push_back(
              {"error", "FACEGEN_ORIGINAL_REQUIRED",
               QObject::tr("Hair Color Safe Mode necesita el FaceGen que el perfil carga para este NPC y no existe en "
                           "el perfil: no se cambia el color ni se exporta. Comprueba que el mod del NPC incluya su FaceGen.")
                   .toStdString(),
               recipe.facegen_mesh_path});
        } else {
          op.stage("nif: Hair Color Safe Mode");
          auto patch = face::patch_hair_tint(*hair_safe_base, recipe);
          b->recolored = patch.patched;
          b->facegen.issues = std::move(patch.issues);
          b->facegen.nif = std::move(patch.nif);
          if (!b->facegen.nif.empty()) b->nif = std::make_shared<const std::string>(b->facegen.nif);
        }
      } else {
        op.stage("nif: build_facegen");
        b->facegen = face::build_facegen(recipe, *ctx->face_assets, options);
        if (!b->facegen.nif.empty()) b->nif = std::make_shared<const std::string>(b->facegen.nif);
        if (tint_changed && !b->facegen.cancelled && !job.cancel->load()) {
          op.stage("dds: compose FaceTint");
          auto tint = face::compose_face_tint(recipe, *ctx->face_assets);
          b->tint_layers = tint.layers;
          b->tint_issues = tint.issues;
          b->tint_rgba = std::make_shared<const textures::DdsImage>(std::move(tint.rgba));
          op.stage("dds: encode preview");
          b->tint_preview_dds = std::make_shared<const std::string>(textures::encode_dds(*b->tint_rgba, textures::TextureFormat::RGBA8, false));
        }
      }
      if (job.cancel->load()) b->facegen.cancelled = true;
      op.succeed();
    } catch (const std::exception& e) {
      // A build that throws is reported as an issue; nothing half-built is installed.
      op.fail(e.what());
      b->nif.reset();
      b->facegen.nif.clear();
      b->facegen.issues.push_back({"error", "FaceGenBuildFailed", e.what(), recipe.npc_form_key});
    }
    b->elapsed_ms = timer.elapsed();
    QMetaObject::invokeMethod(self, [self, b] {
      if (self) self->finish_build(b);
    }, Qt::QueuedConnection);
  });
}

void AppearanceEditor::finish_build(const std::shared_ptr<Build>& b) {
  if (running_ && running_->task == b->task) running_.reset();
  const bool same_npc = b->npc == target_.form_key;
  const bool current = same_npc && recipe_ && !b->facegen.cancelled && draft_.face_loaded() && b->state == draft_.face();
  if (current) {
    const bool geometry_changed = !build_ || b->kind != "hair-safe";
    build_ = b;
    auto overlay = std::make_shared<rendering::AssetOverlay>();
    if (b->nif) (*overlay)[text::data_path_key(recipe_->facegen_mesh_path)] = b->nif;
    if (b->tint_preview_dds) (*overlay)[text::data_path_key(recipe_->facegen_tint_path)] = b->tint_preview_dds;
    overlay_ = overlay->empty() ? nullptr : std::shared_ptr<const rendering::AssetOverlay>(overlay);
    if (geometry_changed) ++geometry_revision_;
    QVariantList parts;
    for (const auto& p : b->facegen.parts) {
      QStringList applied, missing;
      for (const auto& a : p.applied) applied << qs(a);
      for (const auto& m : p.missing) missing << qs(m);
      parts << QVariantMap{{"editorId", qs(p.editor_id)}, {"model", qs(p.model)}, {"status", qs(p.status)},
                           {"source", qs(p.geometry_source)}, {"vertices", static_cast<qlonglong>(p.vertex_count)},
                           {"applied", applied}, {"missing", missing}};
    }
    QStringList layers;
    for (const auto& l : b->tint_layers) layers << qs(l);
    QStringList recolored;
    for (const auto& r : b->recolored) recolored << qs(r);
    facegen_report_ = {{"status", b->facegen.exportable() ? "built" : "failed"},
                       {"kind", qs(b->kind)},
                       {"mode", qs(b->state.geometry_mode)},
                       {"parts", parts},
                       {"recoloredShapes", recolored},
                       // Hair Color Safe Mode output passed the guard: same bytes as the winning
                       // FaceGen except the HairTint colors.
                       {"integrity", b->kind == "hair-safe" && b->facegen.exportable() ? "hair-tint-only" : ""},
                       {"elapsedMs", b->elapsed_ms},
                       {"nifBytes", b->nif ? static_cast<qlonglong>(b->nif->size()) : 0},
                       {"tintRegenerated", b->tint_rgba != nullptr},
                       {"tintLayers", layers}};
    diag::record("facegen", QStringLiteral("installed #%1 %2 (%3 ms, %4)")
                                .arg(b->task).arg(qs(b->kind)).arg(b->elapsed_ms)
                                .arg(b->facegen.exportable() ? "exportable" : "not exportable")
                                .toStdString());
  } else {
    diag::record("facegen", QStringLiteral("discarded #%1 %2: %3")
                                .arg(b->task).arg(qs(b->kind))
                                .arg(!same_npc ? "another NPC is selected" : b->facegen.cancelled ? "cancelled" : "the draft changed")
                                .toStdString());
  }
  building_ = false;
  // The draft moved on while this job ran (or it was superseded): build the newest state.
  const bool stale = same_npc && draft_.face_loaded() && draft_.is_face_dirty() && (!build_ || !(build_->state == draft_.face()));
  if (!running_ && (rebuild_pending_ || stale)) {
    rebuild_pending_ = false;
    start_build();
    return;
  }
  emit faceGenChanged();
}

// ------------------------------------------------------------------------------------- getters

QString AppearanceEditor::npc_form_key() const { return qs(target_.form_key); }

QString AppearanceEditor::npc_label() const {
  if (!target_.name.empty()) return qs(target_.name);
  return qs(target_.form_key);
}

QString AppearanceEditor::preset_name() const { return qs(draft_.face().source); }

QVariantMap AppearanceEditor::morphs() const {
  QVariantMap map;
  for (const auto& def : face::get_standard_face_morphs()) map[qs(def.key)] = draft_.face().values.sliders[def.index];
  return map;
}

QVariantList AppearanceEditor::standard_sliders() const {
  QVariantList list;
  for (const auto& def : face::get_standard_face_morphs()) {
    const auto category = face::morph_category_name(def.category);
    list.push_back(QVariantMap{{"key", qs(def.key)},
                               {"index", static_cast<int>(def.index)},
                               {"category", QString::fromUtf8(category.data(), static_cast<qsizetype>(category.size()))},
                               {"displayNameEs", qs(def.display_name_es)},
                               {"displayNameEn", qs(def.display_name_en)},
                               {"positiveMorph", qs(def.positive_morph)},
                               {"negativeMorph", qs(def.negative_morph)},
                               {"min", def.min_value},
                               {"max", def.max_value},
                               {"defaultValue", def.default_value}});
  }
  return list;
}

QVariantList AppearanceEditor::preset_slots() const {
  QVariantList list;
  std::shared_ptr<const face::TriFile> chargen_tri;
  if (recipe_) {
    if (const auto ctx = context(); ctx && ctx->face_assets) {
      for (const auto& part : recipe_->parts) {
        if (part.type != face::HeadPartType::Face) continue;
        for (const auto& ref : part.tris) {
          if (ref.kind == face::TriKind::Chargen) {
            try { chargen_tri = ctx->face_assets->tri(ref.path); } catch (...) {}
            break;
          }
        }
        break;
      }
    }
  }
  for (const auto& slot : face::face_preset_slots()) {
    if (slot.prefix.empty()) continue;
    int max_index = 35;
    if (chargen_tri) {
      int detected = 0;
      for (int idx = 1; idx <= 99; ++idx) {
        if (chargen_tri->find(slot.prefix + std::to_string(idx))) detected = idx;
      }
      if (detected > 0) max_index = detected;
    }
    const int cur_val = draft_.face_loaded() ? draft_.face().values.presets[slot.index] : 0;
    const bool editable = !operation_blocked(QStringLiteral("face-morphs"));
    list.push_back(QVariantMap{{"slot", static_cast<int>(slot.index)},
                               {"index", static_cast<int>(slot.index)},
                               {"key", qs(slot.key)},
                               {"prefix", qs(slot.prefix)},
                               {"displayNameEs", qs(slot.display_name_es)},
                               {"label", qs(slot.display_name_es)},
                               {"max", max_index},
                               {"maxPreset", max_index},
                               {"value", cur_val},
                               {"editable", editable},
                               {"triVerified", chargen_tri != nullptr}});
  }
  return list;
}

QVariantList AppearanceEditor::face_presets() const {
  QVariantList list;
  for (const auto p : draft_.face().values.presets) list << p;
  return list;
}

QColor AppearanceEditor::hair_color() const {
  const auto& c = draft_.face().values.hair_color;
  return c ? qcolor(*c) : QColor();
}
bool AppearanceEditor::has_hair_color() const { return draft_.face().values.hair_color.has_value(); }
QColor AppearanceEditor::skin_tone() const { return qcolor(draft_.face().values.skin_tone); }
float AppearanceEditor::weight() const { return draft_.face().values.weight; }
float AppearanceEditor::height() const { return draft_.height(); }

QVariantList AppearanceEditor::head_parts() const {
  QVariantList list;
  if (!recipe_) return list;
  const auto& pnam = draft_.face().head_parts;
  const bool female = recipe_->female;
  const auto snap = snapshot_();
  const auto ctx = context();
  for (const auto& p : recipe_->parts) {
    const bool own = std::find(pnam.begin(), pnam.end(), p.form_key) != pnam.end();
    const bool sex_ok = p.valid_sex == "any" || (p.valid_sex == "female") == female;
    const QString type_str = qs(std::string(face::head_part_type_to_string(p.type)));
    const QString plugin_str = qs(p.plugin);
    const QString model_str = qs(p.model);
    const QString editor_str = qs(p.editor_id);
    QString plugin_mod;
    if (snap && !p.plugin.empty()) {
      if (const auto it = snap->plugin_provider.find(text::to_lower_ascii(p.plugin)); it != snap->plugin_provider.end()) {
        plugin_mod = qs(it->second);
      }
    }
    bool mesh_found = p.model.empty();
    QString asset_kind = p.model.empty() ? QStringLiteral("none") : QStringLiteral("missing");
    QString effective_provider = plugin_mod;
    QStringList overridden_by;
    if (!p.model.empty()) {
      if (ctx && ctx->resolver) {
        if (const auto loc = ctx->resolver->locate(p.model)) {
          mesh_found = true;
          asset_kind = loc->kind == assets::AssetLocation::Kind::Loose ? QStringLiteral("loose") : QStringLiteral("bsa");
          effective_provider = qs(loc->kind == assets::AssetLocation::Kind::Archive && !loc->archive_provider.empty()
                                      ? loc->archive_provider
                                      : loc->provider);
        }
      }
      if (snap) {
        for (const auto idx : snap->vfs.providers_of(p.model)) {
          overridden_by << qs(snap->vfs.providers()[idx].name);
        }
      }
    }
    list.push_back(QVariantMap{{"formKey", qs(p.form_key)},
                               {"editorId", editor_str},
                               {"type", type_str},
                               {"model", model_str},
                               {"extra", p.extra},
                               {"plugin", plugin_str},
                               {"pluginMod", plugin_mod},
                               {"packageFamily", classify_head_part_package(plugin_str, model_str, editor_str)},
                               {"official", p.official},
                               {"fromRecord", own},
                               {"validSex", sex_ok},
                               {"validRace", p.valid_for_race},
                               {"meshFound", mesh_found},
                               {"assetKind", asset_kind},
                               {"effectiveProvider", effective_provider},
                               {"overrideCount", std::max(0, static_cast<int>(overridden_by.size()) - 1)},
                               {"overriddenBy", overridden_by},
                               {"isBaldPart", p.type == face::HeadPartType::Hair && p.model.empty()},
                               {"requiresFaceGenRebuild", true}});
  }
  return list;
}

QVariantList AppearanceEditor::tints() const {
  QVariantList list;
  if (!recipe_) return list;
  for (const auto& t : draft_.face().values.tints) {
    const auto* mask = recipe_->tint_mask(t.index);
    const QString mask_path = mask ? qs(mask->mask_path) : QString();
    const int mask_type = mask ? mask->mask_type : -1;
    list.push_back(QVariantMap{{"index", t.index},
                               {"color", qcolor(t.color)},
                               {"value", t.value},
                               {"preset", t.preset},
                               {"mask", mask_path},
                               {"maskType", mask_type},
                               {"category", tint_mask_category_label(mask_type, mask_path)},
                               {"label", tint_mask_display_name(t.index, mask_type, mask_path)}});
  }
  return list;
}

QVariantList AppearanceEditor::tint_catalog() const {
  QVariantList list;
  if (!recipe_) return list;
  const auto ctx = context();
  const auto& active_tints = draft_.face().values.tints;
  for (const auto& mask : recipe_->tint_masks) {
    const auto it = std::find_if(active_tints.begin(), active_tints.end(), [&](const auto& t) { return t.index == mask.index; });
    const QColor color = it != active_tints.end()
                             ? qcolor(it->color)
                             : (mask.mask_type == face::kTintMaskSkinTone ? qcolor(draft_.face().values.skin_tone) : QColor(255, 255, 255));
    const float value = it != active_tints.end() ? it->value : (mask.mask_type == face::kTintMaskSkinTone ? 1.0f : 0.0f);
    const QString path_str = qs(mask.mask_path);
    bool texture_found = mask.mask_path.empty();
    QString provider;
    if (!mask.mask_path.empty() && ctx && ctx->resolver) {
      if (const auto loc = ctx->resolver->locate(mask.mask_path)) {
        texture_found = true;
        provider = qs(loc->kind == assets::AssetLocation::Kind::Archive && !loc->archive_provider.empty()
                          ? loc->archive_provider
                          : loc->provider);
      }
    }
    list.push_back(QVariantMap{{"index", static_cast<int>(mask.index)},
                               {"color", color},
                               {"value", value},
                               {"active", it != active_tints.end() && value > 0.001f},
                               {"mask", path_str},
                               {"maskType", mask.mask_type},
                               {"category", tint_mask_category_label(mask.mask_type, path_str)},
                               {"label", tint_mask_display_name(static_cast<int>(mask.index), mask.mask_type, path_str)},
                               {"textureFound", texture_found},
                               {"provider", provider}});
  }
  return list;
}

QVariantMap AppearanceEditor::custom_morphs() const {
  QVariantMap map;
  for (const auto& [k, v] : draft_.face().values.custom_morphs) map[qs(k)] = v;
  return map;
}

QVariantList AppearanceEditor::available_custom_morphs() const {
  QVariantList list;
  if (!recipe_) return list;
  QStringList names;
  for (const auto& [k, _] : draft_.face().values.custom_morphs) {
    const QString name = qs(k);
    if (!names.contains(name)) names << name;
  }
  if (const auto ctx = context()) {
    for (const auto& ext : morph_extensions(*ctx)) {
      if (auto tri = ctx->face_assets->tri(ext.tri)) {
        for (const auto& m : tri->morphs) {
          const QString name = qs(m.name);
          if (!names.contains(name)) names << name;
        }
      }
    }
  }
  const auto& active = draft_.face().values.custom_morphs;
  for (const auto& name : names) {
    const auto it = active.find(ss(name));
    list.push_back(QVariantMap{{"name", name},
                               {"value", it != active.end() ? it->second : 0.0f},
                               {"min", -1.5f},
                               {"max", 1.5f}});
  }
  return list;
}

QString AppearanceEditor::head_texture() const {
  return qs(draft_.face().head_texture);
}

bool AppearanceEditor::intentionally_bald() const {
  if (!draft_.face_loaded()) return false;
  if (has_virtual_bald(draft_.face().head_parts)) return true;
  if (!recipe_) return false;
  for (const auto& p : recipe_->parts) {
    if (p.type == face::HeadPartType::Hair) return p.model.empty();
  }
  return false;
}

int AppearanceEditor::sculpt_targets() const { return static_cast<int>(draft_.face().values.sculpt.size()); }
QString AppearanceEditor::geometry_mode() const { return qs(draft_.face().geometry_mode); }

QVariantList AppearanceEditor::issues() const {
  QVariantList out = recipe_issues_;
  out.append(existing_issues_);
  if (recipe_) {
    for (const auto& p : recipe_->parts) {
      if (p.valid_sex != "any" && (p.valid_sex == "female") != recipe_->female) {
        out << QVariantMap{{"severity", "warning"}, {"code", "HeadPartWrongSex"},
                           {"message", tr("%1 es una pieza para el otro sexo.").arg(qs(p.editor_id))}, {"subject", qs(p.form_key)}};
      }
      if (!p.valid_for_race) {
        out << QVariantMap{{"severity", "warning"}, {"code", "HeadPartWrongRace"},
                           {"message", tr("%1 no está en la lista de razas válidas para %2.").arg(qs(p.editor_id), qs(recipe_->race_editor_id))},
                           {"subject", qs(p.form_key)}};
      }
    }
  }
  if (build_) {
    for (const auto& i : build_->facegen.issues) out << issue_map(i);
    for (const auto& i : build_->tint_issues) out << issue_map(i);
  }
  return out;
}

bool AppearanceEditor::operation_blocked(const QString& op) const {
  for (const auto& b : compatibility_.value("blocked").toList()) {
    if (b.toMap().value("op").toString() == op) return true;
  }
  return false;
}

QVariantMap AppearanceEditor::easy_mod_plan() const {
  appearance::EasyModInputs in;
  in.npc_loaded = !target_.form_key.empty();
  in.npc_label = ss(npc_label());
  if (in.npc_loaded && target_.form_key != ss(npc_label())) in.npc_label += " (" + target_.form_key + ")";
  in.face_kind = draft_.face_loaded() ? appearance::face_build_kind(draft_.face_baseline(), draft_.face())
                                      : appearance::FaceBuildKind::Unchanged;
  if (in.face_kind == appearance::FaceBuildKind::HairColorSafe && !existing_facegen_) {
    in.face_kind = appearance::FaceBuildKind::Full;
  }
  in.face_exportable = exportable();
  in.face_building = building_;
  in.face_integrity = ss(facegen_report_.value("integrity").toString());
  in.height_changed = draft_.is_height_dirty();
  const auto& pieces = draft_.outfit().pieces();
  in.outfit_changed = draft_.is_outfit_dirty();
  in.outfit_pieces = pieces.size();
  in.outfit_preview_material = std::any_of(pieces.begin(), pieces.end(), [](const auto& p) { return p.material.has_value(); });
  if (outfit_) {
    in.outfit_validating = outfit_->validating();
    in.outfit_exportable = outfit_->exportable();
  }
  for (const auto& b : compatibility_.value("blocked").toList()) in.blocked_operations.push_back(ss(b.toMap().value("op").toString()));
  in.provenance_conflict = ss(compatibility_.value("provenance").toMap().value("conflict").toString());
  if (resolution_ && resolution_npc_ == target_.form_key) in.face_resolution = resolution_;
  if (in.face_resolution && appearance::requires_review(*in.face_resolution)) {
    in.review_required = true;
    in.review_block = review_gate_ ? ss(review_gate_(target_.form_key, *in.face_resolution, false))
                                   : ss(tr("La revisión individual no está disponible en esta ventana."));
  }

  const auto plan = appearance::plan_easy_mod(in);
  auto items = [](const std::vector<appearance::EasyModItem>& list) {
    QVariantList out;
    for (const auto& i : list) {
      const auto state = appearance::to_string(i.state);
      out << QVariantMap{{"key", qs(i.key)},
                         {"state", QString::fromUtf8(state.data(), static_cast<qsizetype>(state.size()))},
                         {"label", qs(i.label)},
                         {"detail", qs(i.detail)}};
    }
    return out;
  };
  const auto mode = appearance::to_string(plan.mode);
  QVariantMap out{{"mode", QString::fromUtf8(mode.data(), static_cast<qsizetype>(mode.size()))},
                  {"summary", qs(plan.summary)},
                  {"canExport", plan.can_export && !exporting_},
                  {"changes", items(plan.changes)},
                  {"checks", items(plan.checks)}};
  if (in.face_resolution) {
    QStringList after;
    for (const auto& p : in.face_resolution->load_after) after << qs(p);
    out["loadAfter"] = after;
    // One patch per NPC: "SNS_FaceFix_<EditorID>.esp" (the FormID when there is no EditorID).
    auto id = qs(target_.editor_id.empty() ? target_.form_key.substr(0, target_.form_key.find(':')) : target_.editor_id);
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    out["suggestedPluginName"] = QStringLiteral("SNS_FaceFix_%1.esp").arg(id.left(60));
  }
  return out;
}

void AppearanceEditor::set_face_resolution(const appearance::FaceResolution& resolution) {
  resolution_ = resolution;
  resolution_npc_ = target_.form_key;
  emit easyModChanged();
}

void AppearanceEditor::clearFaceResolution() {
  if (!resolution_) return;
  resolution_.reset();
  resolution_npc_.clear();
  emit easyModChanged();
}

bool AppearanceEditor::exportable() const {
  if (!draft_.face_loaded() || building_) return false;
  if (has_virtual_bald(draft_.face().head_parts)) return false;
  switch (appearance::face_build_kind(draft_.face_baseline(), draft_.face())) {
    case appearance::FaceBuildKind::Unchanged:
      return true;
    case appearance::FaceBuildKind::WaitForAnalysis:
      return false;
    default:
      return !running_ && build_ && build_->npc == target_.form_key && build_->state == draft_.face() && build_->facegen.exportable();
  }
}

// ------------------------------------------------------------------------------------- edits

void AppearanceEditor::edit(appearance::FaceState next, const QString& description, const QString& merge_key) {
  if (!draft_.face_loaded()) {
    console_->append("warning", "appearance", tr("El rostro del NPC aún no se ha leído."));
    return;
  }
  if (analysing()) {
    // Until the profile's FaceGen is analysed the geometry mode is unknown; an edit made now would
    // be recorded (and undone/redone) with a provisional mode.
    console_->append("warning", "appearance", tr("Analizando el FaceGen original de %1; espera a que termine para editar.").arg(npc_label()));
    return;
  }
  if (next == draft_.face()) return;
  diag::record("appearance", QStringLiteral("edit '%1' npc=%2").arg(description, qs(target_.form_key)).toStdString());
  history_->push(appearance::make_set_face(draft_, std::move(next), ss(description), ss(merge_key)));
}

void AppearanceEditor::enforce_face_blocks() {
  if (!draft_.face_loaded()) return;
  const auto& base = draft_.face_baseline();
  auto face = draft_.face();
  QStringList reverted;
  const auto revert = [&](const char* op, bool differs, auto&& restore) {
    if (!differs || !operation_blocked(QString::fromLatin1(op))) return;
    restore();
    reverted << operation_label(op);
  };
  auto& v = face.values;
  const auto& b = base.values;
  revert("face-morphs", v.sliders != b.sliders || v.presets != b.presets || v.weight != b.weight || v.custom_morphs != b.custom_morphs || v.sculpt != b.sculpt,
         [&] { v.sliders = b.sliders; v.presets = b.presets; v.weight = b.weight; v.custom_morphs = b.custom_morphs; v.sculpt = b.sculpt; });
  revert("head-parts", face.head_parts != base.head_parts || face.head_texture != base.head_texture,
         [&] { face.head_parts = base.head_parts; face.head_texture = base.head_texture; });
  revert("skin-tone", v.skin_tone != b.skin_tone || v.tints != b.tints, [&] { v.skin_tone = b.skin_tone; v.tints = b.tints; });
  if (reverted.isEmpty()) return;
  draft_.set_face(std::move(face));
  console_->append("warning", "appearance",
                   tr("El rostro cargado para %1 tenía cambios bloqueados para esta cabeza (%2): se restauran los del registro; "
                      "el resto (p. ej. el color de cabello) se conserva.").arg(npc_label(), reverted.join(", ")));
}

bool AppearanceEditor::refuse_blocked(const QString& op) {
  for (const auto& b : compatibility_.value("blocked").toList()) {
    const auto m = b.toMap();
    if (m.value("op").toString() != op) continue;
    console_->append("warning", "appearance",
                     tr("%1 bloqueado para %2: %3").arg(m.value("label").toString(), npc_label(), m.value("reason").toString()));
    return true;
  }
  return false;
}

void AppearanceEditor::setSlider(const QString& key, float value) {
  const auto* def = face::find_morph_def(ss(key));
  if (!def || refuse_blocked(QStringLiteral("face-morphs"))) return;
  auto next = draft_.face();
  next.values.sliders[def->index] = def->clamp(value);
  edit(std::move(next), tr("Ajustar %1").arg(qs(def->display_name_es)), "slider:" + key);
}

void AppearanceEditor::setFacePreset(int slot, int value) {
  if (slot < 0 || slot >= static_cast<int>(face::kPresetSlotCount) || refuse_blocked(QStringLiteral("face-morphs"))) return;
  auto next = draft_.face();
  next.values.presets[static_cast<std::size_t>(slot)] = std::clamp(value, -1, 99);
  edit(std::move(next), tr("Cambiar %1").arg(qs(face::face_preset_slots()[static_cast<std::size_t>(slot)].display_name_es)),
       QStringLiteral("preset:%1").arg(slot));
}

void AppearanceEditor::setHairColor(const QColor& color) {
  if (refuse_blocked(QStringLiteral("hair-color"))) return;
  auto next = draft_.face();
  if (color.isValid()) next.values.hair_color = rgb8(color);
  else next.values.hair_color.reset();
  diag::record("appearance", QStringLiteral("setHairColor npc=%1 (%2, winner %3) %4 -> %5")
                                 .arg(qs(target_.form_key), qs(target_.name), qs(target_.winning_plugin),
                                      color_text(draft_.face().values.hair_color), color_text(next.values.hair_color))
                                 .toStdString());
  edit(std::move(next), tr("Cambiar color de cabello"), "hairColor");
}

void AppearanceEditor::setSkinTone(const QColor& color) {
  if (!color.isValid() || refuse_blocked(QStringLiteral("skin-tone"))) return;
  auto next = draft_.face();
  next.values.skin_tone = rgb8(color);
  // The skin tone tint layer follows QNAM, as RaceMenu and the Creation Kit keep them equal.
  if (recipe_) {
    for (auto& t : next.values.tints) {
      if (const auto* mask = recipe_->tint_mask(t.index); mask && mask->mask_type == face::kTintMaskSkinTone) t.color = next.values.skin_tone;
    }
  }
  edit(std::move(next), tr("Cambiar tono de piel"), "skinTone");
}

void AppearanceEditor::setWeight(float weight) {
  // Weight morphs the head (SkinnyMorph): the same TRI fit as the sliders is needed.
  if (refuse_blocked(QStringLiteral("face-morphs"))) return;
  auto next = draft_.face();
  next.values.weight = std::clamp(weight, 0.0f, 100.0f);
  edit(std::move(next), tr("Cambiar peso"), "weight");
}

void AppearanceEditor::setHeight(float height) {
  if (!draft_.face_loaded()) return;
  history_->push(appearance::make_set_height(draft_, std::clamp(height, 0.5f, 2.0f)));
}

void AppearanceEditor::setHeadPart(const QString& formKey, const QString& type) {
  if (formKey.isEmpty() || !recipe_ || refuse_blocked(QStringLiteral("head-parts"))) return;
  const auto wanted = face::string_to_head_part_type(ss(type));
  auto next = draft_.face();
  if (wanted == face::HeadPartType::Hair) {
    std::erase(next.head_parts, std::string(kVirtualBaldFormKey));
  }
  std::erase_if(next.head_parts, [&](const std::string& fk) {
    const auto it = std::find_if(recipe_->parts.begin(), recipe_->parts.end(), [&](const auto& p) { return p.form_key == fk; });
    return it != recipe_->parts.end() && it->type == wanted;
  });
  next.head_parts.push_back(ss(formKey));
  edit(std::move(next), tr("Cambiar %1").arg(type));
}

void AppearanceEditor::searchHeadParts(const QString& type, const QString& text) {
  if (target_.form_key.empty() || !worker_) return;
  const int serial = ++head_part_serial_;
  // A new search replaces the list: never show (or pick from) the results of another type.
  head_part_options_.clear();
  head_part_search_ = {{"state", "searching"}, {"type", type}, {"text", text.trimmed()}};
  emit headPartOptionsChanged();
  Json params{{"npcFormKey", target_.form_key}, {"limit", 1000}};
  if (!type.isEmpty()) params["type"] = ss(type);
  if (!text.trimmed().isEmpty()) params["text"] = ss(text.trimmed());
  worker_->call(method::kSearchHeadParts, params, 30s, this,
                [this, serial](const Json& r) {
                  if (serial != head_part_serial_) return;
                  const auto raw_items = to_variant(r.value("items", Json::array())).toList();
                  const auto snap = snapshot_();
                  const auto ctx = context();
                  const bool blocked = operation_blocked(QStringLiteral("head-parts"));
                  QVariantList enriched;
                  QStringList packages;
                  for (const auto& v : raw_items) {
                    auto item = v.toMap();
                    const QString plugin = item.value("plugin").toString();
                    const QString model_raw = item.value("modelPath").toString();
                    const QString editor_id = item.value("editorId").toString();
                    const QString part_type = item.value("type").toString();
                    QString plugin_mod;
                    if (snap && !plugin.isEmpty()) {
                      if (const auto it = snap->plugin_provider.find(text::to_lower_ascii(ss(plugin))); it != snap->plugin_provider.end()) {
                        plugin_mod = qs(it->second);
                      }
                    }
                    const QString pkg = classify_head_part_package(plugin, model_raw, editor_id);
                    if (!packages.contains(pkg)) packages << pkg;
                    const bool is_empty_model = model_raw.trimmed().isEmpty();
                    bool mesh_found = is_empty_model;
                    QString asset_kind = is_empty_model ? QStringLiteral("none") : QStringLiteral("missing");
                    QString effective_provider = plugin_mod;
                    QStringList overridden_by;
                    bool has_smp = false;
                    if (!is_empty_model) {
                      QString norm = model_raw.trimmed();
                      norm.replace('/', '\\');
                      while (norm.startsWith('\\')) norm.remove(0, 1);
                      if (!norm.startsWith(QStringLiteral("meshes\\"), Qt::CaseInsensitive)) norm = QStringLiteral("meshes\\") + norm;
                      const std::string mesh_key = ss(norm);
                      if (ctx && ctx->resolver) {
                        if (const auto loc = ctx->resolver->locate(mesh_key)) {
                          mesh_found = true;
                          asset_kind = loc->kind == assets::AssetLocation::Kind::Loose ? QStringLiteral("loose") : QStringLiteral("bsa");
                          effective_provider = qs(loc->kind == assets::AssetLocation::Kind::Archive && !loc->archive_provider.empty()
                                                      ? loc->archive_provider
                                                      : loc->provider);
                        }
                        if (norm.endsWith(QStringLiteral(".nif"), Qt::CaseInsensitive)) {
                          const QString xml_cand = norm.left(norm.size() - 4) + QStringLiteral(".xml");
                          if (ctx->resolver->locate(ss(xml_cand)).has_value()) has_smp = true;
                        }
                      }
                      if (snap) {
                        for (const auto idx : snap->vfs.providers_of(mesh_key)) {
                          overridden_by << qs(snap->vfs.providers()[idx].name);
                        }
                      }
                      const QString comb = (item.value("name").toString() + " " + editor_id + " " + model_raw).toLower();
                      if (comb.contains("smp") || comb.contains("hdt") || comb.contains("physics")) has_smp = true;
                    }
                    item["pluginMod"] = plugin_mod;
                    item["packageFamily"] = pkg;
                    item["isBaldPart"] = part_type == QLatin1String("Hair") && is_empty_model;
                    item["isNonePart"] = is_empty_model;
                    item["meshFound"] = mesh_found;
                    item["assetKind"] = asset_kind;
                    item["effectiveProvider"] = effective_provider;
                    item["overrideCount"] = std::max(0, static_cast<int>(overridden_by.size()) - 1);
                    item["overriddenBy"] = overridden_by;
                    item["hasSmp"] = has_smp;
                    item["requiresFaceGenRebuild"] = true;
                    item["headPartBlocked"] = blocked;
                    item["compatibilityState"] = blocked ? QStringLiteral("blocked-custom-head")
                                                         : (!mesh_found ? QStringLiteral("missing-assets") : QStringLiteral("compatible"));
                    enriched.push_back(item);
                  }
                  head_part_options_ = std::move(enriched);
                  head_part_search_["state"] = QStringLiteral("ready");
                  head_part_search_["count"] = head_part_options_.size();
                  head_part_search_["packages"] = packages;
                  emit headPartOptionsChanged();
                },
                [this, serial](int, const QString& message, const Json&) {
                  if (serial != head_part_serial_) return;
                  head_part_options_.clear();
                  head_part_search_["state"] = QStringLiteral("error");
                  head_part_search_["error"] = message;
                  emit headPartOptionsChanged();
                  console_->append("warning", "appearance", tr("Búsqueda de piezas de cabeza: %1").arg(message));
                });
}

void AppearanceEditor::cancelHeadPartSearch() {
  ++head_part_serial_;
  if (head_part_search_.value("state").toString() == QLatin1String("searching")) {
    head_part_search_["state"] = QStringLiteral("cancelled");
    emit headPartOptionsChanged();
  }
}

void AppearanceEditor::removeHeadPart(const QString& type) {
  if (!recipe_ || refuse_blocked(QStringLiteral("head-parts"))) return;
  const auto wanted = face::string_to_head_part_type(ss(type));
  auto next = draft_.face();
  if (wanted == face::HeadPartType::Hair) {
    std::erase(next.head_parts, std::string(kVirtualBaldFormKey));
  }
  std::erase_if(next.head_parts, [&](const std::string& fk) {
    const auto it = std::find_if(recipe_->parts.begin(), recipe_->parts.end(), [&](const auto& p) { return p.form_key == fk; });
    return it != recipe_->parts.end() && it->type == wanted;
  });
  edit(std::move(next), tr("Quitar %1 (vuelve la pieza por defecto de la raza)").arg(type));
}

void AppearanceEditor::setIntentionallyBald(bool bald) {
  if (!recipe_ || refuse_blocked(QStringLiteral("head-parts"))) return;
  auto next = draft_.face();
  std::erase(next.head_parts, std::string(kVirtualBaldFormKey));
  std::erase_if(next.head_parts, [&](const std::string& fk) {
    const auto it = std::find_if(recipe_->parts.begin(), recipe_->parts.end(), [&](const auto& p) { return p.form_key == fk; });
    return it != recipe_->parts.end() && it->type == face::HeadPartType::Hair;
  });
  if (bald) {
    QString bald_form_key;
    for (const auto& v : head_part_options_) {
      const auto m = v.toMap();
      if (m.value("type").toString() == QLatin1String("Hair") && m.value("modelPath").toString().trimmed().isEmpty()) {
        bald_form_key = m.value("formKey").toString();
        break;
      }
    }
    if (!bald_form_key.isEmpty()) {
      next.head_parts.push_back(ss(bald_form_key));
    } else {
      next.head_parts.push_back(kVirtualBaldFormKey);
    }
    edit(std::move(next), tr("Establecer NPC calvo intencionalmente"));
  } else {
    edit(std::move(next), tr("Restaurar cabello por defecto de la raza"));
  }
}

void AppearanceEditor::setCustomMorph(const QString& name, float value) {
  if (name.trimmed().isEmpty() || refuse_blocked(QStringLiteral("face-morphs"))) return;
  auto next = draft_.face();
  const float clamped = std::clamp(value, -1.5f, 1.5f);
  if (std::abs(clamped) < 1e-4f) {
    next.values.custom_morphs.erase(ss(name.trimmed()));
  } else {
    next.values.custom_morphs[ss(name.trimmed())] = clamped;
  }
  edit(std::move(next), tr("Ajustar morph personalizado %1").arg(name.trimmed()), "customMorph:" + name.trimmed());
}

void AppearanceEditor::setHeadTexture(const QString& formKey) {
  if (!recipe_ || refuse_blocked(QStringLiteral("head-parts"))) return;
  auto next = draft_.face();
  next.head_texture = ss(formKey.trimmed());
  edit(std::move(next), formKey.trimmed().isEmpty() ? tr("Restaurar textura facial por defecto")
                                                    : tr("Cambiar textura de cabeza (%1)").arg(formKey.trimmed()));
}

void AppearanceEditor::setTint(int index, const QColor& color, float value) {
  if (refuse_blocked(QStringLiteral("skin-tone"))) return;
  auto next = draft_.face();
  auto it = std::find_if(next.values.tints.begin(), next.values.tints.end(), [&](const auto& t) { return t.index == index; });
  if (it == next.values.tints.end()) {
    if (!recipe_ || !recipe_->tint_mask(static_cast<std::uint16_t>(index))) return;
    next.values.tints.push_back({static_cast<std::uint16_t>(index), {}, 0.0f, -1});
    it = std::prev(next.values.tints.end());
  }
  if (color.isValid()) it->color = rgb8(color);
  it->value = std::clamp(value, 0.0f, 1.0f);
  edit(std::move(next), tr("Ajustar capa de tinte %1").arg(index), QStringLiteral("tint:%1").arg(index));
}

void AppearanceEditor::setGeometryMode(const QString& mode) {
  if (mode != "regenerate" && mode != "preserve") return;
  if (analysing()) {
    console_->append("warning", "appearance", tr("Analizando el FaceGen original de %1; espera a que termine.").arg(npc_label()));
    return;
  }
  if (mode == "regenerate" && base_compatibility_report_ && !base_compatibility_report_->regenerable_from_record) {
    console_->append("warning", "appearance",
                     tr("No se puede desbloquear la regeneración para %1: faltan mallas o TRI de Head Parts en MO2.").arg(npc_label()));
    return;
  }
  if (mode == "preserve" && !existing_facegen_) {
    console_->append("warning", "appearance", tr("No hay FaceGen existente que conservar: se regenera desde el registro."));
    return;
  }
  auto next = draft_.face();
  next.geometry_mode = ss(mode);
  edit(std::move(next), mode == "preserve" ? tr("Conservar el FaceGen existente") : tr("Regenerar FaceGen desde el registro"));
}

void AppearanceEditor::unlockForModding() {
  if (!draft_.face_loaded() || analysing()) return;
  if (base_compatibility_report_ && !base_compatibility_report_->regenerable_from_record) {
    console_->append("warning", "appearance",
                     tr("No se puede desbloquear %1: faltan mallas de Head Parts en el perfil MO2.").arg(npc_label()));
    return;
  }
  if (draft_.face().geometry_mode != "regenerate") {
    setGeometryMode(QStringLiteral("regenerate"));
    console_->append("info", "appearance",
                     tr("Rostro de %1 desbloqueado para modear: la exportación regenerará un FaceGen completo desde el registro.")
                         .arg(npc_label()));
  } else {
    refresh_compatibility_map(true);
  }
}

void AppearanceEditor::resetFace() {
  if (!draft_.face_loaded()) return;
  edit(draft_.face_baseline(), tr("Restablecer el rostro del registro"));
  // "Restablecer" returns the NPC to its record: the body height too (an undoable step of its own).
  if (draft_.is_height_dirty()) history_->push(appearance::make_set_height(draft_, draft_.height_baseline()));
}

void AppearanceEditor::resetMorphs() {
  if (!draft_.face_loaded()) return;
  const auto& base = draft_.face_baseline();
  auto next = draft_.face();
  next.values.sliders = base.values.sliders;
  next.values.presets = base.values.presets;
  next.values.custom_morphs = base.values.custom_morphs;
  next.values.sculpt = base.values.sculpt;
  next.values.weight = base.values.weight;
  edit(std::move(next), tr("Restablecer geometría y morphs faciales"));
}

void AppearanceEditor::resetTints() {
  if (!draft_.face_loaded()) return;
  const auto& base = draft_.face_baseline();
  auto next = draft_.face();
  next.values.skin_tone = base.values.skin_tone;
  next.values.tints = base.values.tints;
  edit(std::move(next), tr("Restablecer tono de piel y capas de maquillaje/tinte"));
}

void AppearanceEditor::resetHeadParts() {
  if (!draft_.face_loaded()) return;
  const auto& base = draft_.face_baseline();
  auto next = draft_.face();
  next.head_parts = base.head_parts;
  next.head_texture = base.head_texture;
  next.values.hair_color = base.values.hair_color;
  edit(std::move(next), tr("Restablecer cabello, ojos, cejas y piezas de cabeza"));
}

void AppearanceEditor::apply_face_state(appearance::FaceState next, const QString& description) {
  edit(std::move(next), description);
}

void AppearanceEditor::undo() {
  diag::record("appearance", ("undo '" + history_->undo_text() + "'").toStdString());
  history_->undo();
}
void AppearanceEditor::redo() {
  diag::record("appearance", ("redo '" + history_->redo_text() + "'").toStdString());
  history_->redo();
}

// ------------------------------------------------------------------------------------- donors and presets

// A donor replaces morphs, presets, Head Parts, skin tone and tints at once, so it is refused wherever any
// of those operations is blocked (e.g. a High Poly head whose TRI do not match its mesh).
QString AppearanceEditor::donor_blocked_reason() const {
  for (const auto& b : compatibility_.value("blocked").toList()) {
    const auto m = b.toMap();
    const auto op = m.value("op").toString();
    if (op == QLatin1String("face-morphs") || op == QLatin1String("head-parts") || op == QLatin1String("skin-tone"))
      return m.value("label").toString() + QStringLiteral(": ") + m.value("reason").toString();
  }
  return {};
}

void AppearanceEditor::applyDonor(const QString& donorFormKey, const QString& donorName) {
  if (donorFormKey.isEmpty() || !worker_ || !recipe_) return;
  if (const auto reason = donor_blocked_reason(); !reason.isEmpty()) {
    console_->append("warning", "appearance", tr("Aplicar un donante está bloqueado para %1 (%2).").arg(npc_label(), reason));
    return;
  }
  worker_->call(method::kGetFaceRecipe, Json{{"npcFormKey", ss(donorFormKey)}}, 30s, this,
                [this, donorFormKey, donorName](const Json& json) {
                  face::FaceRecipe donor;
                  try {
                    donor = face::recipe_from_json(json);
                  } catch (const std::exception& e) {
                    console_->append("error", "appearance", qs(e.what()));
                    return;
                  }
                  if (!recipe_ || !donor_blocked_reason().isEmpty()) return;  // the NPC may have changed meanwhile
                  if (donor.female != recipe_->female) {
                    console_->append("error", "appearance", tr("El donante es de otro sexo: sus piezas y morphs no son compatibles."));
                    return;
                  }
                  if (donor.race_editor_id != recipe_->race_editor_id) {
                    console_->append("warning", "appearance",
                                     tr("El donante es %1 y el NPC %2: las piezas fuera de la lista de razas válidas se marcarán.")
                                         .arg(qs(donor.race_editor_id), qs(recipe_->race_editor_id)));
                  }
                  auto next = draft_.face();
                  next.values = donor.values;
                  next.head_parts = donor.npc_head_parts;
                  next.head_texture = donor.npc_head_texture;
                  // Tint indices are race specific: keep the target's layers when the races differ.
                  if (donor.race_editor_id != recipe_->race_editor_id) next.values.tints = draft_.face().values.tints;
                  next.source = ss(tr("Donante: %1").arg(donorName.isEmpty() ? donorFormKey : donorName));
                  // The donor's FaceGen is not copied: the face is rebuilt from its record, and a
                  // donor whose visible face is a custom export will look different (reported).
                  next.geometry_mode = "regenerate";
                  edit(std::move(next), tr("Aplicar apariencia de %1").arg(donorName.isEmpty() ? donorFormKey : donorName));
                  console_->append("info", "appearance",
                                   tr("Rostro de %1 aplicado desde su registro; el FaceGen se regenera (no se copia el del donante).")
                                       .arg(donorName.isEmpty() ? donorFormKey : donorName));
                },
                [this](int, const QString& error, const Json&) {
                  console_->append("error", "appearance", tr("No se pudo leer el donante: %1").arg(error));
                });
}

QVariantMap AppearanceEditor::jslot_compatibility(const QString& file_path, std::optional<appearance::FaceState>* mapped) {
  QVariantMap result{{"file", file_path}};
  QVariantList findings;
  auto finding = [&](const QString& verdict, const QString& code, const QString& message) {
    findings << QVariantMap{{"verdict", verdict}, {"code", code}, {"message", message}};
  };
  presets::RaceMenuPreset preset;
  try {
    preset = presets::parse_jslot(file_io::read_text_file(fs_path(local_path(file_path))), ss(QFileInfo(file_path).completeBaseName()));
  } catch (const std::exception& e) {
    finding("blocking", "PresetUnreadable", qs(e.what()));
    result["findings"] = findings;
    result["verdict"] = "blocking";
    return result;
  }
  if (!recipe_ || !draft_.face_loaded()) {
    finding("blocking", "NpcNotLoaded", tr("Selecciona un NPC y espera a que se lea su rostro."));
    result["findings"] = findings;
    result["verdict"] = "blocking";
    return result;
  }
  // A preset rewrites morphs, Head Parts and tints like a donor: never on a protected head (a High Poly
  // head whose TRI do not match its mesh would be replaced by a regenerated one).
  if (const auto reason = donor_blocked_reason(); !reason.isEmpty()) {
    finding("blocking", "FaceProtected", tr("Los presets no se pueden aplicar a este NPC (%1).").arg(reason));
    result["findings"] = findings;
    result["verdict"] = "blocking";
    result["compatible"] = false;
    result["exportable"] = false;
    return result;
  }
  // Plugins the preset needs that are not active in the profile.
  QStringList missing;
  if (const auto snap = snapshot_()) {
    for (const auto& plugin : preset.plugins) {
      const bool active = std::any_of(snap->load_order.active.begin(), snap->load_order.active.end(),
                                      [&](const auto& e) { return text::iequals_ascii(e.name, plugin); });
      if (!active) missing << qs(plugin);
    }
  }
  if (!missing.isEmpty()) {
    finding("missing-dependency", "PresetPluginMissing", tr("Plugins que el preset usa y no están activos: %1").arg(missing.join(", ")));
  }
  const auto m = presets::map_preset(preset, draft_.face(), *recipe_);
  // The head parts the preset ends up using must come from active plugins too: the plugin list a
  // .jslot declares can omit them (Carrot names KSHairdosSMP.esp parts without listing the plugin),
  // and the export would then fail with HeadPartUnresolved.
  if (const auto snap = snapshot_()) {
    QStringList unresolved;
    for (const auto& fk : m.state.head_parts) {
      const auto colon = fk.find(':');
      if (colon == std::string::npos) continue;
      const auto plugin = fk.substr(colon + 1);
      const bool active = std::any_of(snap->load_order.active.begin(), snap->load_order.active.end(),
                                      [&](const auto& e) { return text::iequals_ascii(e.name, plugin); });
      if (!active && !unresolved.contains(qs(fk))) unresolved << qs(fk);
    }
    if (!unresolved.isEmpty()) {
      finding("missing-dependency", "PresetHeadPartMissing",
              tr("Piezas de cabeza del preset cuyos plugins no están activos: %1").arg(unresolved.join(", ")));
    }
  }
  for (const auto& n : m.notes) finding("warning", "PresetNote", qs(n));
  for (const auto& r : m.runtime_only) {
    finding("warning", "RuntimeOnly", tr("No se exporta al NPC (solo lo aplica RaceMenu en el juego): %1").arg(qs(r)));
  }
  if (!preset.custom_morphs.empty()) {
    finding("pending", "CustomMorphs",
            tr("%1 morphs personalizados de RaceMenu: se hornean si sus TRI (morphs.ini) están en el perfil; se comprueba al regenerar.")
                .arg(preset.custom_morphs.size()));
  }
  if (!preset.sculpt.empty()) {
    finding("pending", "Sculpt", tr("Esculpido en %1 piezas: se hornea si las piezas y su número de vértices coinciden; se comprueba al regenerar.")
                                     .arg(preset.sculpt.size()));
  }
  // An exported head next to the preset (RaceMenu "Export Head") is reported, never copied.
  const auto dir = QFileInfo(file_path).dir();
  QDir chargen = dir;
  if (chargen.dirName().compare("presets", Qt::CaseInsensitive) == 0) chargen.cdUp();
  if (QFileInfo::exists(chargen.filePath(QFileInfo(file_path).completeBaseName() + ".nif"))) {
    finding("info", "ExportedHead", tr("El preset trae una cabeza exportada de RaceMenu (%1.nif). El estudio la regenera desde los "
                                       "datos del preset en lugar de copiar el archivo del autor.").arg(QFileInfo(file_path).completeBaseName()));
  }
  QString verdict = QStringLiteral("exportable");
  for (const auto& f : findings) {
    const auto v = f.toMap().value("verdict").toString();
    if (v == "blocking") verdict = v;
    else if (v == "missing-dependency" && verdict != "blocking") verdict = v;
    else if ((v == "warning" || v == "pending") && verdict == "exportable") verdict = "exportable-with-warnings";
  }
  result["findings"] = findings;
  result["verdict"] = verdict;
  result["name"] = qs(preset.name);
  result["headParts"] = static_cast<int>(preset.head_parts.size());
  result["tintsMatched"] = static_cast<int>(m.tints_matched);
  result["tintsSkipped"] = static_cast<int>(m.tints_skipped);
  // Legacy keys used by the preset gallery.
  QStringList issues;
  for (const auto& f : findings) issues << f.toMap().value("message").toString();
  result["issues"] = issues;
  result["compatible"] = verdict != "blocking";
  result["exportable"] = verdict == "exportable" || verdict == "exportable-with-warnings";
  if (mapped && verdict != "blocking") *mapped = m.state;
  return result;
}

QVariantMap AppearanceEditor::evaluatePresetCompatibility(const QString& filePath) { return jslot_compatibility(filePath, nullptr); }

void AppearanceEditor::applyJslotPreset(const QString& filePath) {
  std::optional<appearance::FaceState> mapped;
  const auto report = jslot_compatibility(filePath, &mapped);
  for (const auto& f : report.value("findings").toList()) {
    const auto m = f.toMap();
    console_->append(m.value("verdict").toString() == "blocking" ? "error" : "info", "preset", m.value("message").toString());
  }
  if (!mapped) return;
  edit(std::move(*mapped), tr("Aplicar preset RaceMenu %1").arg(QFileInfo(filePath).completeBaseName()));
}

void AppearanceEditor::loadPresetsFromVfs() {
  presets_list_.clear();
  const auto snap = snapshot_();
  if (!snap) return;
  // Highest-priority provider wins for a given preset file name, like the game's Data folder.
  std::map<std::string, QVariantMap> by_name;
  for (const auto& provider : snap->vfs.providers()) {
    for (const auto* sub : {"SKSE/Plugins/CharGen/Presets", "SKSE/Plugins/CharGen/Exported"}) {
      std::error_code ec;
      const auto dir = provider.root / std::filesystem::path(sub);
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || !text::iequals_ascii(text::path_to_utf8(entry.path().extension()), ".jslot")) continue;
        const auto file_name = text::path_to_utf8(entry.path().filename());
        by_name[text::to_lower_ascii(file_name)] = QVariantMap{
            {"name", qs(text::path_to_utf8(entry.path().stem()))},
            {"fileName", qs(file_name)},
            {"vfsPath", qs(std::string(sub) + "/" + file_name)},
            {"diskPath", QString::fromStdWString(entry.path().native())},
            {"sourceMod", qs(provider.name)},
        };
      }
    }
  }
  for (auto& [key, item] : by_name) presets_list_.push_back(item);
  emit presetsChanged();
}

// ------------------------------------------------------------------------------------- export

void AppearanceEditor::exportFaceOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip,
                                          const QString& author, const QString& description) {
  export_overhaul(false, pluginName, outputDir, eslFlag, createZip, author, description);
}

void AppearanceEditor::exportCompleteOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip,
                                              const QString& author, const QString& description) {
  export_overhaul(true, pluginName, outputDir, eslFlag, createZip, author, description);
}

void AppearanceEditor::finish_export(bool ok, const QString& message, const QString& mod_dir, const QString& zip, const QVariantMap& extra) {
  diag::record("export", QStringLiteral("%1: %2").arg(ok ? "done" : "FAILED", message).toStdString());
  exporting_ = false;
  last_export_ = extra;
  last_export_["success"] = ok;
  last_export_["message"] = message;
  last_export_["modDirectory"] = mod_dir;
  last_export_["zipPath"] = zip;
  emit exportingChanged();
  emit exportFinished(ok, message, mod_dir, zip);
  console_->append(ok ? "info" : "error", "export", message);
}

void AppearanceEditor::export_overhaul(bool complete, const QString& plugin_name, const QString& output_dir, bool esl, bool zip,
                                       const QString& author, const QString& description) {
  if (exporting_ || target_.form_key.empty() || !worker_) return;
  const auto face_kind = draft_.face_loaded() ? appearance::face_build_kind(draft_.face_baseline(), draft_.face())
                                              : appearance::FaceBuildKind::Unchanged;
  const bool face_edit = face_kind != appearance::FaceBuildKind::Unchanged;
  // Minimal change: a hair color edit on an existing FaceGen exports HCLF and the recolored winning FaceGen.
  const bool hair_only = face_kind == appearance::FaceBuildKind::HairColorSafe && existing_facegen_ != nullptr;
  const bool body_edit = draft_.is_height_dirty();
  // The draft starts with the NPC's current outfit: only a changed outfit is an edit (an unchanged
  // one would add a new OTFT identical to the current one).
  const bool outfit_edit = complete && outfit_ && draft_.is_outfit_dirty() && !outfit_->current_pieces().empty();
  if (outfit_edit && std::any_of(outfit_->current_pieces().begin(), outfit_->current_pieces().end(),
                                 [](const auto& p) { return p.material.has_value(); })) {
    finish_export(false, tr("El atuendo contiene una variante de material solo previsualizable. "
                                  "Restáurala antes de exportar el mod completo."), {}, {}, {});
    return;
  }
  if (!face_edit && !body_edit && !outfit_edit) {
    finish_export(false, tr("No hay cambios que exportar."), {}, {}, {});
    return;
  }
  if (face_edit && !exportable()) {
    finish_export(false, building_ ? tr("La cabeza se está regenerando; vuelve a exportar cuando termine.")
                        : face_kind == appearance::FaceBuildKind::WaitForAnalysis ? tr("El FaceGen original aún se está analizando.")
                                                                                  : tr("El rostro no es exportable: revisa el panel de problemas."),
                        {}, {}, {});
    return;
  }
  // FaceGen Geometry Integrity Guard, again on the exact bytes about to be staged.
  if (hair_only) {
    const auto integrity = build_ && build_->nif && existing_facegen_
                               ? face::verify_hair_tint_integrity(*existing_facegen_, *build_->nif)
                               : face::FaceGenIntegrity{};
    if (!integrity.unchanged_except_hair_tint || !integrity.byte_identical_outside_hair_tint) {
      diag::record("export", ("FACEGEN_GEOMETRY_CHANGED " + integrity.summary()).c_str());
      finish_export(false, tr("FACEGEN_GEOMETRY_CHANGED: el FaceGen a exportar no es el original con otro color de cabello (%1). "
                                    "Exportación bloqueada.").arg(qs(integrity.summary())), {}, {}, {});
      return;
    }
  }
  // The worker only accepts absolute output paths.
  const auto output = QDir::toNativeSeparators(QFileInfo(local_path(output_dir)).absoluteFilePath());
  {
    const auto snap = snapshot_();
    if (const auto refused = refused_export_directory(output, snap ? forbidden_roots_of(*snap) : std::vector<ForbiddenRoot>{});
        !refused.isEmpty()) {
      finish_export(false, refused, {}, {}, {});  // nothing created yet
      return;
    }
  }
  QDir().mkpath(output);
  exporting_ = true;
  emit exportingChanged();
  diag::record("export", QStringLiteral("%1 npc=%2 plugin=%3 face=%4 height=%5 outfit=%6 -> %7")
                             .arg(complete ? "complete overhaul" : "face overhaul", qs(target_.form_key), plugin_name)
                             .arg(face_edit).arg(body_edit).arg(outfit_edit).arg(output).toStdString());

  // Stage the regenerated files (the exact preview bytes; the tint re-encoded to BC3 like vanilla).
  const auto stage = QDir(QDir::tempPath()).filePath("sns-generated-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  Json assets = Json::array();
  QVariantMap verification;
  try {
    if (face_edit && build_ && build_->nif && recipe_) {
      auto write = [&](const std::string& data_path, const std::string& bytes, const char* kind) {
        const auto file = QDir(stage).filePath(qs(text::data_path_key(data_path)).replace('\\', '/'));
        QDir().mkpath(QFileInfo(file).absolutePath());
        file_io::write_file_atomic(fs_path(file), bytes);
        const auto hash = sha256_hex(QByteArray::fromStdString(bytes));
        assets.push_back({{"sourcePath", ss(QDir::toNativeSeparators(file))}, {"dataPath", data_path}, {"sha256", ss(hash)}, {"kind", kind}});
        verification[qs(data_path)] = hash;
      };
      write(recipe_->facegen_mesh_path, *build_->nif, "facegen-geometry");
      if (build_->tint_rgba) write(recipe_->facegen_tint_path, face::encode_face_tint(*build_->tint_rgba), "facegen-tint");
    }
  } catch (const std::exception& e) {
    QDir(stage).removeRecursively();
    finish_export(false, tr("No se pudieron preparar los archivos FaceGen: %1").arg(qs(e.what())), {}, {});
    return;
  }

  Json face_part = nullptr;
  if ((face_edit || body_edit) && recipe_) {
    const auto& state = draft_.face();
    const auto& v = state.values;
    Json tints = Json::array();
    for (const auto& t : v.tints) tints.push_back({{"index", t.index}, {"color", rgb_json(t.color)}, {"value", t.value}, {"preset", t.preset}});
    std::vector<std::string> derived;
    if (face_edit && build_) {
      for (const auto& p : recipe_->parts) {
        if (!p.official && !p.model.empty()) derived.push_back(p.plugin);
      }
      if ((hair_only || state.geometry_mode == "preserve") && existing_facegen_) {
        if (const auto ctx = context()) {
          if (const auto loc = ctx->resolver->locate(recipe_->facegen_mesh_path)) derived.push_back("FaceGen: " + loc->provider);
        }
      }
      std::sort(derived.begin(), derived.end());
      derived.erase(std::unique(derived.begin(), derived.end()), derived.end());
    }
    Json parts = Json::array();
    if (build_) {
      for (const auto& p : build_->facegen.parts) {
        if (p.status == "built") parts.push_back(p.editor_id + " (" + p.geometry_source + ")");
      }
    }
    face_part = {{"npcFormKey", target_.form_key},
                 {"values",
                  {{"sliders", v.sliders},
                   {"presets", v.presets},
                   {"weight", v.weight},
                   {"skinTone", rgb_json(v.skin_tone)},
                   {"hairColor", v.hair_color ? rgb_json(*v.hair_color) : Json(nullptr)},
                   {"tints", tints}}},
                 {"headParts", state.head_parts},
                 {"headTexture", state.head_texture},
                 {"height", draft_.height()},
                 {"generatedAssets", assets},
                 {"faceGen",
                  {{"mode", hair_only ? std::string("hair-color-safe") : state.geometry_mode},
                   {"parts", parts},
                   {"derivedFrom", derived},
                   {"customMorphs", v.custom_morphs.size()},
                   {"sculptTargets", v.sculpt.size()}}}};
    // A generated FaceGen only shows in game if this mod wins over every mod with the NPC's loose FaceGen.
    if (!assets.empty()) {
      if (const auto ctx = context()) face_part["faceGen"]["installAbove"] = facegen_competitors(ctx->snapshot->vfs, *recipe_);
    }
    // Minimal change: for a hair color and/or height edit the worker writes only these record fields
    // on the winning record and rejects the export if any other face field differs from it.
    if (hair_only || !face_edit) {
      Json only = Json::array();
      if (hair_only) only.push_back("HCLF");
      if (body_edit) only.push_back("NAM6");
      face_part["onlyFields"] = only;
    } else {
      // A complete face: the worker writes and allows only the NPC_ fields the user changed (draft
      // against the record), so values that merely round-trip can never alter other fields.
      const auto& base = draft_.face_baseline();
      Json requested = Json::array();
      if (v.sliders != base.values.sliders) requested.push_back("FaceMorph");
      if (v.presets != base.values.presets) requested.push_back("FaceParts");
      if (v.weight != base.values.weight) requested.push_back("Weight");
      if (v.skin_tone != base.values.skin_tone) requested.push_back("TextureLighting");
      if (v.tints != base.values.tints) requested.push_back("TintLayers");
      if (v.hair_color != base.values.hair_color) requested.push_back("HairColor");
      if (state.head_parts != base.head_parts) requested.push_back("HeadParts");
      if (state.head_texture != base.head_texture) requested.push_back("HeadTexture");
      if (body_edit) requested.push_back("Height");
      face_part["requestedFields"] = requested;
    }
  }

  Json providers = Json::object();
  if (const auto snap = snapshot_()) {
    for (const auto& p : snap->load_order.active) providers[p.name] = p.provider;
  }
  Json request{{"pluginName", ss(plugin_name)},
               {"outputDirectory", ss(output)},
               {"eslFlag", esl},
               {"createZip", zip},
               {"pluginProviders", providers}};
  if (!author.trimmed().isEmpty()) request["author"] = ss(author.trimmed());
  if (!description.trimmed().isEmpty()) request["description"] = ss(description.trimmed());
  const char* rpc = method::kExportFacePatch;
  if (complete) {
    Json assignment{{"npcFormKey", target_.form_key}};
    if (!face_part.is_null()) assignment["face"] = face_part;
    if (outfit_edit) {
      Json armors = Json::array();
      for (const auto& p : outfit_->current_pieces()) armors.push_back(p.form_key);
      assignment["outfit"] = {{"npcFormKey", target_.form_key}, {"armorFormKeys", armors}};
    }
    request["assignments"] = Json::array({assignment});
    rpc = method::kExportCompleteNpc;
  } else {
    request["assignments"] = Json::array({face_part});
  }

  const auto recipe = recipe_;
  worker_->call(rpc, request, 5min, this,
                [this, stage, verification, recipe](const Json& r) {
                  const auto mod_dir = qs(r.value("modDirectory", ""));
                  const auto zip_path = qs(r.value("zipPath", ""));
                  // Post-export check: the files in the mod are byte-identical to the previewed ones,
                  // and the exported FaceGen matches the head parts the exported record names.
                  QStringList problems;
                  for (auto it = verification.begin(); it != verification.end(); ++it) {
                    QFile f(QDir(mod_dir).filePath(QString(it.key()).replace('\\', '/')));
                    if (!f.open(QIODevice::ReadOnly)) {
                      problems << tr("Falta en el mod: %1").arg(it.key());
                      continue;
                    }
                    const auto bytes = f.readAll();
                    if (sha256_hex(bytes) != it.value().toString()) problems << tr("Contenido distinto al previsualizado: %1").arg(it.key());
                    if (recipe && it.key().endsWith(".nif", Qt::CaseInsensitive)) {
                      auto check = *recipe;
                      for (const auto& i : face::audit_facegen(face::inspect_facegen(bytes.toStdString()), check)) {
                        if (i.severity != "info") problems << qs(i.message);
                      }
                    }
                  }
                  QDir(stage).removeRecursively();
                  QVariantMap extra{{"pluginPath", qs(r.value("pluginPath", ""))},
                                    {"masters", to_variant(r.value("masters", Json::array()))},
                                    {"issues", to_variant(r.value("issues", Json::array()))},
                                    {"verifiedFiles", verification.keys()},
                                    {"verificationProblems", problems},
                                    {"records", to_variant(r.value("records", Json::array()))},
                                    {"semanticDiff", to_variant(r.value("semanticDiff", Json::array()))},
                                    {"files", to_variant(r.value("files", Json::array()))},
                                    {"verification", to_variant(r.value("verification", Json::array()))},
                                    {"pendingVerification", to_variant(r.value("pendingVerification", Json::array()))},
                                    {"inGameVerified", false}};
                  if (!problems.isEmpty()) {
                    finish_export(false, tr("Mod escrito pero la verificación posterior falló: %1").arg(problems.join("; ")), mod_dir, zip_path, extra);
                    return;
                  }
                  finish_export(true,
                                tr("Mod exportado y verificado (%1 archivo(s) FaceGen idénticos a la vista previa). Pendiente: "
                                   "comprobarlo dentro del juego.").arg(verification.size()),
                                mod_dir, zip_path, extra);
                },
                [this, stage](int, const QString& error, const Json& data) {
                  QDir(stage).removeRecursively();
                  QVariantMap extra{{"issues", to_variant(data.is_object() ? data.value("issues", Json::array()) : Json::array())}};
                  // The reasons (e.g. which NPC field the safe patch refused) are part of the message:
                  // the console, the assistant and the session log show them.
                  QStringList reasons;
                  for (const auto& i : extra.value("issues").toList()) {
                    const auto m = i.toMap();
                    if (m.value("severity").toString() == "error") reasons << m.value("code").toString() + ": " + m.value("message").toString();
                  }
                  finish_export(false, tr("Exportación cancelada: %1").arg(error) + (reasons.isEmpty() ? QString() : "\n" + reasons.join("\n")),
                                {}, {}, extra);
                });
}

namespace {

// NPC_ property of the control map -> the subrecord the worker's OnlyFields names.
const char* subrecord_of(const std::string& field) {
  static const std::map<std::string, const char*> map = {{"HeadParts", "PNAM"},   {"FaceMorph", "NAM9"}, {"FaceParts", "NAMA"},
                                                         {"TintLayers", "TINT"},  {"HairColor", "HCLF"}, {"HeadTexture", "FTST"},
                                                         {"TextureLighting", "QNAM"}, {"Weight", "NAM7"}};
  const auto it = map.find(field);
  return it == map.end() ? nullptr : it->second;
}

}  // namespace

void AppearanceEditor::exportFaceResolution(const QString& plugin_name, const QString& output_dir, bool esl, bool zip,
                                            const QString& author, const QString& description) {
  if (exporting_ || target_.form_key.empty() || !worker_) return;
  if (!resolution_ || resolution_npc_ != target_.form_key) {
    finish_export(false, tr("No hay ninguna resolución preparada para este NPC (pestaña Origen)."), {}, {}, {});
    return;
  }
  // Ambiguous case (D1): re-read the profile (chain, effective FaceGen) and check the individual approval
  // against those inputs right before writing anything.
  if (appearance::requires_review(*resolution_)) {
    const auto reason = review_gate_ ? review_gate_(target_.form_key, *resolution_, true)
                                     : tr("La revisión individual no está disponible en esta ventana.");
    if (!reason.isEmpty()) {
      diag::record("export", "face resolution blocked by review: " + target_.form_key);
      finish_export(false, tr("El parche de resolución no se puede crear: %1").arg(reason), {}, {}, {});
      return;
    }
  }
  // The same checks the assistant shows: no pending editor changes, analysis finished.
  const auto plan = easy_mod_plan();
  if (plan.value("mode").toString() != QLatin1String("face-resolution") || !plan.value("canExport").toBool()) {
    QStringList reasons;
    for (const auto& c : plan.value("checks").toList()) {
      const auto m = c.toMap();
      if (m.value("state").toString() == "blocked" || m.value("state").toString() == "waiting") reasons << m.value("label").toString();
    }
    finish_export(false, tr("El parche de resolución no se puede crear ahora: %1").arg(reasons.join("; ")), {}, {}, {});
    return;
  }
  const auto r = *resolution_;
  Json only = Json::array();
  for (const auto& f : r.fields) {
    if (const auto* code = subrecord_of(f)) only.push_back(code);
  }
  // The worker only accepts absolute output paths.
  const auto output = QDir::toNativeSeparators(QFileInfo(local_path(output_dir)).absoluteFilePath());
  {
    const auto snap = snapshot_();
    if (const auto refused = refused_export_directory(output, snap ? forbidden_roots_of(*snap) : std::vector<ForbiddenRoot>{});
        !refused.isEmpty()) {
      finish_export(false, refused, {}, {}, {});  // nothing created yet
      return;
    }
  }
  QDir().mkpath(output);
  exporting_ = true;
  emit exportingChanged();
  diag::record("export", QStringLiteral("face resolution npc=%1 from=%2 onto=%3 fields=%4 plugin=%5 -> %6")
                             .arg(qs(target_.form_key), qs(r.source_plugin), qs(r.winner_plugin), qs(only.dump()), plugin_name, output)
                             .toStdString());

  // The FaceGen the game uses now, located again in MO2 (loose over BSA): the worker refuses the copy unless
  // it belongs to the source plugin's mod, and refuses it when it cannot be located (empty).
  std::string facegen_mod;
  const auto& recipe = recipe_ ? recipe_ : base_recipe_;
  if (const auto ctx = context(); ctx && ctx->resolver && recipe) {
    if (const auto loc = ctx->resolver->locate(recipe->facegen_mesh_path)) {
      facegen_mod = loc->kind == assets::AssetLocation::Kind::Archive && !loc->archive_provider.empty() ? loc->archive_provider : loc->provider;
    }
  }

  // No generated assets: the source mod's FaceGen is the one the game already loads.
  Json face_part{{"npcFormKey", target_.form_key},
                 {"copyFaceFrom", r.source_plugin},
                 {"onlyFields", only},
                 {"faceGen",
                  {{"mode", "source-facegen"},
                   {"effectiveFaceGenMod", facegen_mod},
                   {"parts", Json::array()},
                   {"derivedFrom", Json::array()},
                   {"installAbove", Json::array()}}}};
  Json providers = Json::object();
  if (const auto snap = snapshot_()) {
    for (const auto& p : snap->load_order.active) providers[p.name] = p.provider;
  }
  Json request{{"pluginName", ss(plugin_name)}, {"outputDirectory", ss(output)}, {"eslFlag", esl},
               {"createZip", zip},              {"pluginProviders", providers},  {"assignments", Json::array({face_part})}};
  if (!author.trimmed().isEmpty()) request["author"] = ss(author.trimmed());
  if (!description.trimmed().isEmpty()) request["description"] = ss(description.trimmed());

  worker_->call(method::kExportFacePatch, request, 5min, this,
                [this, r, plugin_name](const Json& j) {
                  const auto mod_dir = qs(j.value("modDirectory", ""));
                  const auto zip_path = qs(j.value("zipPath", ""));
                  QStringList problems;
                  // FaceGen Integrity Guard for this mode: the mod carries no FaceGen at all.
                  for (const auto& f : j.value("files", Json::array())) {
                    const auto path = qs(f.value("path", ""));
                    if (path.contains(QLatin1String("facegendata"), Qt::CaseInsensitive)) problems << tr("El mod trae FaceGen (%1)").arg(path);
                  }
                  QStringList masters;
                  for (const auto& m : j.value("masters", Json::array())) masters << qs(m.get<std::string>());
                  for (const auto& p : r.load_after) {
                    if (!masters.contains(qs(p), Qt::CaseInsensitive)) problems << tr("No se carga después de %1 (no es master)").arg(qs(p));
                  }
                  // The control map with the plugin loaded last must say record and FaceGen now agree.
                  QVariantMap projected, chain;
                  for (const auto& c : j.value("projectedChains", Json::array())) {
                    if (c.value("npc", "") == target_.form_key) chain = to_variant(c).toMap();
                  }
                  if (chain.isEmpty()) {
                    problems << tr("El worker no devolvió la cadena del NPC con el parche");
                  } else if (const auto remaining = chain.value("remainingFaceDifferences").toStringList(); !remaining.isEmpty()) {
                    // Compared on the re-read records, not on fingerprints: the copy is incomplete.
                    problems << tr("El parche no lleva todo el rostro de %1: siguen distintos %2").arg(qs(r.source_plugin), remaining.join(", "));
                  } else if (control_projector_) {
                    projected = control_projector_(chain, plugin_name);
                    QStringList codes;
                    for (const auto& f : projected.value("findings").toList()) codes << f.toMap().value("code").toString();
                    if (codes.contains(QLatin1String("FaceGenRecordMismatch")) || projected.value("faceMod").toString() != qs(r.source_mod)) {
                      problems << tr("Con el parche, el mapa de control sigue viendo un desajuste (datos faciales de «%1»): %2")
                                      .arg(projected.value("faceMod").toString(), codes.join(", "));
                    }
                  }
                  QVariantMap extra{{"pluginPath", qs(j.value("pluginPath", ""))},
                                    {"masters", masters},
                                    {"issues", to_variant(j.value("issues", Json::array()))},
                                    {"records", to_variant(j.value("records", Json::array()))},
                                    {"semanticDiff", to_variant(j.value("semanticDiff", Json::array()))},
                                    {"files", to_variant(j.value("files", Json::array()))},
                                    {"verification", to_variant(j.value("verification", Json::array()))},
                                    {"pendingVerification", to_variant(j.value("pendingVerification", Json::array()))},
                                    {"projectedChain", chain},
                                    {"projectedControl", projected},
                                    {"verificationProblems", problems},
                                    {"faceResolution", qs(r.label)},
                                    {"readyForGame", false},
                                    {"inGameVerified", false}};
                  if (!problems.isEmpty()) {
                    finish_export(false, tr("Mod escrito pero la verificación posterior falló: %1").arg(problems.join("; ")), mod_dir, zip_path, extra);
                    return;
                  }
                  finish_export(true,
                                tr("Parche de resolución exportado y verificado: el rostro es el de «%1» y el mapa de control, con el parche al "
                                   "final, ve registro y FaceGen del mismo mod. Pendiente: comprobarlo dentro del juego.")
                                    .arg(qs(r.source_plugin)),
                                mod_dir, zip_path, extra);
                },
                [this](int, const QString& error, const Json& data) {
                  QVariantMap extra{{"issues", to_variant(data.is_object() ? data.value("issues", Json::array()) : Json::array())}};
                  QStringList reasons;
                  for (const auto& i : extra.value("issues").toList()) {
                    const auto m = i.toMap();
                    if (m.value("severity").toString() == "error") reasons << m.value("code").toString() + ": " + m.value("message").toString();
                  }
                  finish_export(false, tr("Exportación cancelada: %1").arg(error) + (reasons.isEmpty() ? QString() : "\n" + reasons.join("\n")),
                                {}, {}, extra);
                });
}

}  // namespace sns::app
