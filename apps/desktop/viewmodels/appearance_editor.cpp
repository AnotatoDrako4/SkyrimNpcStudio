#include "appearance_editor.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <filesystem>

#include "diagnostics/crash_diagnostics.hpp"
#include "json_bridge.hpp"
#include "outfit_editor.hpp"
#include "resource_context.hpp"
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

// The two states differ in nothing but the hair color (HCLF): a FaceGen for one becomes the
// FaceGen for the other by rewriting the HairTint shader color.
bool only_hair_color_differs(const appearance::FaceState& a, const appearance::FaceState& b) {
  if (a.values.hair_color == b.values.hair_color) return false;
  auto x = a;
  x.values.hair_color = b.values.hair_color;
  return x == b;
}

QString color_text(const std::optional<face::Rgb8>& c) {
  return c ? QColor(c->r, c->g, c->b).name() : QStringLiteral("(sin color)");
}

}  // namespace

// One regeneration of the face: the state it was made from and the exact files it produced.
struct AppearanceEditor::Build {
  std::uint64_t task = 0;
  std::string npc;
  std::string kind;  // "full" or "hair-patch"
  appearance::FaceState state;
  face::FaceGenBuild facegen;
  std::shared_ptr<const std::string> nif;
  std::shared_ptr<const textures::DdsImage> tint_rgba;     // null: the tint was not regenerated
  std::shared_ptr<const std::string> tint_preview_dds;     // uncompressed copy for the preview
  std::vector<std::string> tint_layers;
  std::vector<face::FaceIssue> tint_issues;
  qint64 elapsed_ms = 0;
};

namespace {

int setting_ms(const char* key, int fallback) {
  bool ok = false;
  const int value = QSettings().value(QString::fromLatin1(key), fallback).toInt(&ok);
  return ok ? std::clamp(value, 0, 5000) : fallback;
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
    emit appearanceChanged();
    if (!recipe_ || !draft_.face_loaded()) return;
    if (draft_.face().head_parts != recipe_->npc_head_parts || draft_.face().head_texture != recipe_->npc_head_texture) {
      request_recipe(false);
    } else {
      schedule_build();
    }
  });
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
  diag::set_context("npc", target.form_key + " " + target.editor_id + " (" + target.name + ")");
  diag::set_context("winningPlugin", target.winning_plugin);
  base_recipe_.reset();
  recipe_.reset();
  existing_facegen_.reset();
  has_existing_tint_ = false;
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
    req["headPartFormKeys"] = draft_.face().head_parts;
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
  if (initial) {
    base_recipe_ = recipe;
    recipe_ = recipe;
    draft_.set_face_baseline(appearance::face_state_from_recipe(recipe));
    draft_.set_height_baseline(recipe.height);
    if (const auto it = saved_faces_.find(target_.form_key); it != saved_faces_.end()) {
      std::vector<std::string> notes;
      draft_.set_face(appearance::from_face_config(it->second, draft_.face_baseline(), recipe, &notes));
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
  pool_.start([self, ctx, recipe, recipe_serial, task, cancel = analysis_cancel_] {
    struct Result {
      std::shared_ptr<const std::string> existing;
      bool tint = false;
      QString mode = QStringLiteral("regenerate");
      QString reason;
      QVariantList issues;
    };
    auto result = std::make_shared<Result>();
    diag::Operation op("facegen.analyse", task, "npc " + recipe.npc_form_key);
    if (cancel->load()) return;
    try {
      op.stage("read existing FaceGen");
      if (auto bytes = ctx->resolver->read(recipe.facegen_mesh_path)) result->existing = std::make_shared<const std::string>(std::move(*bytes));
      result->tint = ctx->resolver->locate(recipe.facegen_tint_path).has_value();
      if (!result->existing) {
        result->reason = QObject::tr("El NPC no tiene FaceGen en el perfil: el juego construye la cabeza en tiempo real y suele "
                                     "mostrar la cara oscura. Exportar genera un FaceGen coherente con el registro.");
        result->issues << QVariantMap{{"severity", "warning"}, {"code", "NoFaceGen"}, {"message", result->reason},
                                      {"subject", qs(recipe.facegen_mesh_path)}};
      } else {
        op.stage("audit + regenerate for comparison");
        for (const auto& i : face::audit_facegen(face::inspect_facegen(*result->existing), recipe)) result->issues << issue_map(i);
        face::FaceGenBuildOptions options;
        options.cancelled = [cancel] { return cancel->load(); };
        const auto regen = face::build_facegen(recipe, *ctx->face_assets, options);
        if (regen.cancelled) return;
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
    }
    op.succeed();
    QMetaObject::invokeMethod(self, [self, result, recipe_serial] {
      if (!self || recipe_serial != self->recipe_serial_) return;
      self->existing_facegen_ = result->existing;
      self->has_existing_tint_ = result->tint;
      self->geometry_reason_ = result->reason;
      self->existing_issues_ = result->issues;
      self->draft_.set_baseline_geometry_mode(ss(result->mode));
      emit self->appearanceChanged();
      emit self->faceGenChanged();
      self->schedule_build();
    }, Qt::QueuedConnection);
  });
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
  if (!draft_.is_face_dirty()) {
    build_.reset();
    if (overlay_) ++geometry_revision_;
    overlay_.reset();
    building_ = false;
    facegen_report_ = {{"status", "unchanged"}, {"mode", geometry_mode()}};
    emit faceGenChanged();
    return;
  }
  auto ctx = context();
  if (!ctx) return;
  const auto state = draft_.face();
  const auto& baseline = draft_.face_baseline();
  auto recipe = *recipe_;
  recipe.values = state.values;
  recipe.morph_extensions = morph_extensions(*ctx);

  // Hair color only, compared with the FaceGen already built: rewrite its HairTint color instead
  // of rebuilding every head part (tens of milliseconds instead of a full regeneration).
  std::shared_ptr<const Build> base;
  if (build_ && build_->nif && build_->npc == target_.form_key && only_hair_color_differs(build_->state, state)) base = build_;

  face::FaceGenBuildOptions options;
  if (!base && state.geometry_mode == "preserve" && existing_facegen_) {
    options.mode = face::GeometryMode::PreserveExisting;
    options.existing_facegen = existing_facegen_;
    options.baseline = baseline.values;
  }
  const bool tint_changed = state.values.tints != baseline.values.tints || state.values.skin_tone != baseline.values.skin_tone ||
                            !has_existing_tint_;
  Job job{diag::next_task_id(), target_.form_key, std::make_shared<std::atomic<bool>>(false)};
  options.cancelled = [cancel = job.cancel] { return cancel->load(); };
  running_ = job;
  building_ = true;
  const auto kind = base ? std::string("hair-patch") : std::string("full");
  diag::record("facegen", QStringLiteral("start #%1 %2 npc=%3 mode=%4 hair=%5")
                              .arg(job.task).arg(qs(kind), qs(job.npc), qs(state.geometry_mode), color_text(state.values.hair_color))
                              .toStdString());
  emit faceGenChanged();
  QPointer<AppearanceEditor> self(this);
  pool_.start([self, ctx, recipe, options, state, tint_changed, job, base, kind] {
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
      } else if (base) {
        op.stage("nif: patch HairTint color");
        auto patch = face::patch_hair_tint(*base->nif, recipe);
        b->facegen.parts = base->facegen.parts;
        for (const auto& i : base->facegen.issues) {
          if (i.code != "HairTintUnsupported") b->facegen.issues.push_back(i);  // re-checked by the patch
        }
        b->facegen.issues.insert(b->facegen.issues.end(), patch.issues.begin(), patch.issues.end());
        b->facegen.nif = std::move(patch.nif);
        if (!b->facegen.nif.empty()) b->nif = std::make_shared<const std::string>(b->facegen.nif);
        // The FaceTint does not depend on the hair color: the base build's files are reused as is.
        b->tint_rgba = base->tint_rgba;
        b->tint_preview_dds = base->tint_preview_dds;
        b->tint_layers = base->tint_layers;
        b->tint_issues = base->tint_issues;
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
    const bool geometry_changed = !build_ || b->kind != "hair-patch";
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
    facegen_report_ = {{"status", b->facegen.exportable() ? "built" : "failed"},
                       {"kind", qs(b->kind)},
                       {"mode", qs(b->state.geometry_mode)},
                       {"parts", parts},
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
  for (const auto& slot : face::face_preset_slots()) {
    if (slot.prefix.empty()) continue;
    list.push_back(QVariantMap{{"slot", static_cast<int>(slot.index)}, {"key", qs(slot.key)}, {"prefix", qs(slot.prefix)},
                               {"displayNameEs", qs(slot.display_name_es)}, {"max", 40}});
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
  for (const auto& p : recipe_->parts) {
    const bool own = std::find(pnam.begin(), pnam.end(), p.form_key) != pnam.end();
    const bool sex_ok = p.valid_sex == "any" || (p.valid_sex == "female") == female;
    list.push_back(QVariantMap{{"formKey", qs(p.form_key)},
                               {"editorId", qs(p.editor_id)},
                               {"type", qs(std::string(face::head_part_type_to_string(p.type)))},
                               {"model", qs(p.model)},
                               {"extra", p.extra},
                               {"plugin", qs(p.plugin)},
                               {"official", p.official},
                               {"fromRecord", own},
                               {"validSex", sex_ok},
                               {"validRace", p.valid_for_race}});
  }
  return list;
}

QVariantList AppearanceEditor::tints() const {
  QVariantList list;
  if (!recipe_) return list;
  for (const auto& t : draft_.face().values.tints) {
    const auto* mask = recipe_->tint_mask(t.index);
    list.push_back(QVariantMap{{"index", t.index},
                               {"color", qcolor(t.color)},
                               {"value", t.value},
                               {"preset", t.preset},
                               {"mask", mask ? qs(mask->mask_path) : QString()},
                               {"maskType", mask ? mask->mask_type : -1}});
  }
  return list;
}

QVariantMap AppearanceEditor::custom_morphs() const {
  QVariantMap map;
  for (const auto& [k, v] : draft_.face().values.custom_morphs) map[qs(k)] = v;
  return map;
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

bool AppearanceEditor::exportable() const {
  if (!draft_.face_loaded() || building_) return false;
  if (!draft_.is_face_dirty()) return true;
  return !running_ && build_ && build_->npc == target_.form_key && build_->state == draft_.face() && build_->facegen.exportable();
}

// ------------------------------------------------------------------------------------- edits

void AppearanceEditor::edit(appearance::FaceState next, const QString& description, const QString& merge_key) {
  if (!draft_.face_loaded()) {
    console_->append("warning", "appearance", tr("El rostro del NPC aún no se ha leído."));
    return;
  }
  if (next == draft_.face()) return;
  diag::record("appearance", QStringLiteral("edit '%1' npc=%2").arg(description, qs(target_.form_key)).toStdString());
  history_->push(appearance::make_set_face(draft_, std::move(next), ss(description), ss(merge_key)));
}

void AppearanceEditor::setSlider(const QString& key, float value) {
  const auto* def = face::find_morph_def(ss(key));
  if (!def) return;
  auto next = draft_.face();
  next.values.sliders[def->index] = def->clamp(value);
  edit(std::move(next), tr("Ajustar %1").arg(qs(def->display_name_es)), "slider:" + key);
}

void AppearanceEditor::setFacePreset(int slot, int value) {
  if (slot < 0 || slot >= static_cast<int>(face::kPresetSlotCount)) return;
  auto next = draft_.face();
  next.values.presets[static_cast<std::size_t>(slot)] = std::clamp(value, -1, 99);
  edit(std::move(next), tr("Cambiar %1").arg(qs(face::face_preset_slots()[static_cast<std::size_t>(slot)].display_name_es)),
       QStringLiteral("preset:%1").arg(slot));
}

void AppearanceEditor::setHairColor(const QColor& color) {
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
  if (!color.isValid()) return;
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
  auto next = draft_.face();
  next.values.weight = std::clamp(weight, 0.0f, 100.0f);
  edit(std::move(next), tr("Cambiar peso"), "weight");
}

void AppearanceEditor::setHeight(float height) {
  if (!draft_.face_loaded()) return;
  history_->push(appearance::make_set_height(draft_, std::clamp(height, 0.5f, 2.0f)));
}

void AppearanceEditor::setHeadPart(const QString& formKey, const QString& type) {
  if (formKey.isEmpty() || !recipe_) return;
  const auto wanted = face::string_to_head_part_type(ss(type));
  auto next = draft_.face();
  std::erase_if(next.head_parts, [&](const std::string& fk) {
    const auto it = std::find_if(recipe_->parts.begin(), recipe_->parts.end(), [&](const auto& p) { return p.form_key == fk; });
    return it != recipe_->parts.end() && it->type == wanted;
  });
  next.head_parts.push_back(ss(formKey));
  edit(std::move(next), tr("Cambiar %1").arg(type));
}

void AppearanceEditor::removeHeadPart(const QString& type) {
  if (!recipe_) return;
  const auto wanted = face::string_to_head_part_type(ss(type));
  auto next = draft_.face();
  std::erase_if(next.head_parts, [&](const std::string& fk) {
    const auto it = std::find_if(recipe_->parts.begin(), recipe_->parts.end(), [&](const auto& p) { return p.form_key == fk; });
    return it != recipe_->parts.end() && it->type == wanted;
  });
  edit(std::move(next), tr("Quitar %1 (vuelve la pieza por defecto de la raza)").arg(type));
}

void AppearanceEditor::setTint(int index, const QColor& color, float value) {
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
  if (mode == "preserve" && !existing_facegen_) {
    console_->append("warning", "appearance", tr("No hay FaceGen existente que conservar: se regenera desde el registro."));
    return;
  }
  auto next = draft_.face();
  next.geometry_mode = ss(mode);
  edit(std::move(next), mode == "preserve" ? tr("Conservar el FaceGen existente") : tr("Regenerar FaceGen desde el registro"));
}

void AppearanceEditor::resetFace() {
  if (!draft_.face_loaded()) return;
  edit(draft_.face_baseline(), tr("Restablecer el rostro del registro"));
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

void AppearanceEditor::applyDonor(const QString& donorFormKey, const QString& donorName) {
  if (donorFormKey.isEmpty() || !worker_ || !recipe_) return;
  worker_->call(method::kGetFaceRecipe, Json{{"npcFormKey", ss(donorFormKey)}}, 30s, this,
                [this, donorFormKey, donorName](const Json& json) {
                  face::FaceRecipe donor;
                  try {
                    donor = face::recipe_from_json(json);
                  } catch (const std::exception& e) {
                    console_->append("error", "appearance", qs(e.what()));
                    return;
                  }
                  if (!recipe_) return;
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

void AppearanceEditor::exportFaceOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip) {
  export_overhaul(false, pluginName, outputDir, eslFlag, createZip);
}

void AppearanceEditor::exportCompleteOverhaul(const QString& pluginName, const QString& outputDir, bool eslFlag, bool createZip) {
  export_overhaul(true, pluginName, outputDir, eslFlag, createZip);
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

void AppearanceEditor::export_overhaul(bool complete, const QString& plugin_name, const QString& output_dir, bool esl, bool zip) {
  if (exporting_ || target_.form_key.empty() || !worker_) return;
  const bool face_edit = draft_.is_face_dirty();
  const bool body_edit = draft_.is_height_dirty();
  const bool outfit_edit = complete && outfit_ && !outfit_->current_pieces().empty();
  if (outfit_edit && std::any_of(outfit_->current_pieces().begin(), outfit_->current_pieces().end(),
                                 [](const auto& p) { return p.material.has_value(); })) {
    emit exportFinished(false, tr("El atuendo contiene una variante de material solo previsualizable. "
                                  "Restáurala antes de exportar el mod completo."), {}, {});
    return;
  }
  if (!face_edit && !body_edit && !outfit_edit) {
    emit exportFinished(false, tr("No hay cambios que exportar."), {}, {});
    return;
  }
  if (face_edit && !exportable()) {
    emit exportFinished(false, building_ ? tr("La cabeza se está regenerando; vuelve a exportar cuando termine.")
                                         : tr("El rostro no es exportable: revisa el panel de problemas."), {}, {});
    return;
  }
  const auto output = QDir::toNativeSeparators(local_path(output_dir));
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
      if (state.geometry_mode == "preserve" && existing_facegen_) {
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
                  {{"mode", state.geometry_mode},
                   {"parts", parts},
                   {"derivedFrom", derived},
                   {"customMorphs", v.custom_morphs.size()},
                   {"sculptTargets", v.sculpt.size()}}}};
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
                  finish_export(false, tr("Exportación cancelada: %1").arg(error), {}, {}, extra);
                });
}

}  // namespace sns::app
