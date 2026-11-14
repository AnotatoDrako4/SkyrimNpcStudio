#include "beauty_lab_viewmodel.hpp"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "json_bridge.hpp"
#include "resource_context.hpp"

namespace sns::app {

namespace {

QString local_file_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

QImage grab_object_image(QObject* viewport_or_window) {
  if (!viewport_or_window) return {};
  if (auto* win = qobject_cast<QQuickWindow*>(viewport_or_window)) {
    return win->grabWindow();
  }
  if (auto* item = qobject_cast<QQuickItem*>(viewport_or_window)) {
    if (auto* win = item->window()) {
      const QImage full = win->grabWindow();
      if (full.isNull()) return {};
      const qreal dpr = win->effectiveDevicePixelRatio();
      const QRectF scene_rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
      const QRect pixel_rect(
          std::max(0, static_cast<int>(std::lround(scene_rect.x() * dpr))),
          std::max(0, static_cast<int>(std::lround(scene_rect.y() * dpr))),
          std::max(1, static_cast<int>(std::lround(scene_rect.width() * dpr))),
          std::max(1, static_cast<int>(std::lround(scene_rect.height() * dpr))));
      const QRect bounded = pixel_rect.intersected(full.rect());
      return bounded.isEmpty() ? full : full.copy(bounded);
    }
  }
  return {};
}

}  // namespace

BeautyLabViewModel::BeautyLabViewModel(AppearanceEditor* appearance,
                                       OutfitEditor* outfit,
                                       SceneViewModel* scene,
                                       EditHistory* history,
                                       ConsoleModel* console,
                                       SnapshotGetter snapshot,
                                       QObject* parent)
    : QObject(parent),
      appearance_(appearance),
      outfit_(outfit),
      scene_(scene),
      history_(history),
      console_(console),
      snapshot_(std::move(snapshot)),
      style_presets_(next_gen_style_presets()) {
  if (appearance_) {
    connect(appearance_, &AppearanceEditor::targetChanged, this, [this] {
      comparator_mode_ = QStringLiteral("live");
      selected_head_part_inspection_.clear();
      emit comparatorModeChanged();
      emit headPartInspectionChanged();
      rebuild_comparison_and_capabilities();
      refreshHeadPartGallery();
      rebuild_quality_check();
      emit libraryChanged();
    });
    connect(appearance_, &AppearanceEditor::recipeChanged, this, [this] {
      rebuild_comparison_and_capabilities();
      rebuild_filtered_head_parts();
      rebuild_quality_check();
    });
    connect(appearance_, &AppearanceEditor::appearanceChanged, this, [this] {
      rebuild_comparison_and_capabilities();
      rebuild_filtered_head_parts();
      rebuild_quality_check();
    });
    connect(appearance_, &AppearanceEditor::faceGenChanged, this, [this] {
      rebuild_comparison_and_capabilities();
      rebuild_quality_check();
    });
    connect(appearance_, &AppearanceEditor::headPartOptionsChanged, this, [this] {
      rebuild_filtered_head_parts();
    });
  }
  if (scene_) {
    connect(scene_, &SceneViewModel::changed, this, [this] {
      rebuild_quality_check();
    });
  }

  rebuild_comparison_and_capabilities();
  rebuild_quality_check();
}

void BeautyLabViewModel::rebuild_comparison_and_capabilities() {
  if (!appearance_) return;
  const auto snap = snapshot_ ? snapshot_() : nullptr;
  const auto ctx = snap ? ResourceContext::for_snapshot(snap) : nullptr;
  const auto* base_rec = appearance_->base_recipe();
  const auto* active_rec = appearance_->recipe();

  workshop_capabilities_ =
      evaluate_workshop_capabilities(base_rec, appearance_->face_compatibility(), appearance_->geometry_mode());
  emit workshopCapabilitiesChanged();

  if (appearance_->loaded() && base_rec && active_rec) {
    comparison_ = compare_original_vs_proposed(
        base_rec,
        active_rec,
        appearance_->draft_state().face_baseline(),
        appearance_->draft_state().face(),
        appearance_->draft_state().height_baseline(),
        appearance_->height(),
        appearance_->face_compatibility(),
        appearance_->facegen_report(),
        appearance_->exportable(),
        snap.get(),
        ctx ? ctx->resolver.get() : nullptr);
  } else {
    comparison_ = {
        {"loaded", false},
        {"hasChanges", false},
        {"changeCount", 0},
        {"proposalStatus", QStringLiteral("SIN_CAMBIOS")},
        {"proposalStatusLabel", tr("Selecciona un NPC para iniciar Beauty Lab")},
        {"proposalStatusDetail", QString()}};
  }
  emit comparisonChanged();
}

void BeautyLabViewModel::rebuild_filtered_head_parts() {
  if (!appearance_) return;
  const QVariantList raw = appearance_->head_part_options();
  const QStringList favs = library_.favorite_hair_keys();

  // Compute package counts across the current category's options
  QMap<QString, int> pkg_counts;
  int total_in_cat = 0;
  for (const auto& v : raw) {
    const auto m = v.toMap();
    if (!hair_category_.isEmpty() && hair_category_ != QLatin1String("all") &&
        m.value("type").toString() != hair_category_) {
      continue;
    }
    ++total_in_cat;
    const QString fam = m.value("packageFamily").toString();
    if (!fam.isEmpty()) pkg_counts[fam] += 1;
  }

  QVariantList pkgs;
  pkgs.push_back(QVariantMap{
      {"id", QStringLiteral("all")},
      {"label", tr("Todos los paquetes (%1)").arg(total_in_cat)},
      {"count", total_in_cat}});
  for (auto it = pkg_counts.begin(); it != pkg_counts.end(); ++it) {
    pkgs.push_back(QVariantMap{
        {"id", it.key()},
        {"label", QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value())},
        {"count", it.value()}});
  }
  available_packages_ = pkgs;

  QVariantList filtered = filter_head_part_options(
      raw, hair_category_, hair_search_text_, hair_package_filter_, hair_compatibility_filter_, favs);

  // Mark currently equipped HeadPart for quick UI highlighting
  QStringList active_fks;
  if (const auto* active_rec = appearance_->recipe()) {
    for (const auto& p : active_rec->parts) {
      if (!p.extra) active_fks << qs(p.form_key);
    }
  }
  for (auto& v : filtered) {
    auto m = v.toMap();
    const QString fk = m.value("formKey").toString();
    m["isCurrent"] = active_fks.contains(fk, Qt::CaseInsensitive) ||
                     (appearance_->intentionally_bald() && m.value("isBaldPart").toBool() &&
                      m.value("type").toString() == QLatin1String("Hair"));
    v = m;
  }

  filtered_head_parts_ = filtered;
  emit hairStudioChanged();
}

void BeautyLabViewModel::rebuild_quality_check() {
  if (!appearance_ || !appearance_->loaded()) {
    quality_check_ = {
        {"overallStatus", QStringLiteral("idle")},
        {"warningCount", 0},
        {"errorCount", 0},
        {"checks", QVariantList{}},
        {"findings", QVariantList{}},
        {"inGameVerified", false},
        {"verificationStatus", QStringLiteral("PENDIENTE_SKYRIM")}};
    emit qualityCheckChanged();
    return;
  }

  const auto snap = snapshot_ ? snapshot_() : nullptr;
  const auto ctx = snap ? ResourceContext::for_snapshot(snap) : nullptr;
  quality_check_ = run_visual_quality_check(
      appearance_->recipe(),
      appearance_->draft_state().face_baseline(),
      appearance_->draft_state().face(),
      appearance_->intentionally_bald(),
      appearance_->face_compatibility(),
      appearance_->facegen_report(),
      scene_ ? scene_->parts() : QVariantList{},
      scene_ ? scene_->stats() : QVariantMap{},
      scene_ ? scene_->issues() : QVariantList{},
      scene_ ? scene_->skin_match() : QVariantMap{},
      snap.get(),
      ctx ? ctx->resolver.get() : nullptr);
  emit qualityCheckChanged();
}

void BeautyLabViewModel::setComparatorMode(const QString& mode) {
  const QString clean = mode.trimmed().isEmpty() ? QStringLiteral("live") : mode.trimmed();
  if (comparator_mode_ == clean) return;
  comparator_mode_ = clean;
  if (scene_) {
    scene_->setOriginal(comparator_mode_ == QLatin1String("original"));
  }
  emit comparatorModeChanged();
}

void BeautyLabViewModel::revertSingleChange(const QString& category, const QString& key) {
  if (!appearance_ || !appearance_->loaded()) return;
  const auto& base = appearance_->draft_state().face_baseline();
  auto next = appearance_->draft_state().face();

  if (category == QLatin1String("morph")) {
    if (const auto* def = face::find_morph_def(ss(key))) {
      next.values.sliders[def->index] = base.values.sliders[def->index];
      next.source = "Revertir morph (" + def->key + ")";
      appearance_->apply_face_state(next, tr("Revertir %1").arg(qs(def->display_name_es)));
    }
  } else if (category == QLatin1String("customMorph")) {
    const std::string k = ss(key);
    auto it = base.values.custom_morphs.find(k);
    if (it == base.values.custom_morphs.end()) {
      next.values.custom_morphs.erase(k);
    } else {
      next.values.custom_morphs[k] = it->second;
    }
    next.source = "Revertir morph personalizado (" + k + ")";
    appearance_->apply_face_state(next, tr("Revertir morph %1").arg(key));
  } else if (category == QLatin1String("preset")) {
    bool ok = false;
    const int slot = key.toInt(&ok);
    if (ok && slot >= 0 && slot < static_cast<int>(face::kPresetSlotCount)) {
      next.values.presets[static_cast<std::size_t>(slot)] = base.values.presets[static_cast<std::size_t>(slot)];
      next.source = "Revertir preset racial";
      appearance_->apply_face_state(next, tr("Revertir preset racial #%1").arg(slot + 1));
    }
  } else if (category == QLatin1String("tint")) {
    bool ok = false;
    const int idx = key.toInt(&ok);
    if (ok && idx >= 0) {
      std::erase_if(next.values.tints, [&](const auto& t) { return static_cast<int>(t.index) == idx; });
      for (const auto& bt : base.values.tints) {
        if (static_cast<int>(bt.index) == idx) {
          next.values.tints.push_back(bt);
          break;
        }
      }
      next.source = "Revertir capa de tinte #" + std::to_string(idx);
      appearance_->apply_face_state(next, tr("Revertir capa de tinte #%1").arg(idx));
    }
  } else if (category == QLatin1String("headPart")) {
    next.head_parts = base.head_parts;
    next.source = "Revertir piezas de cabeza";
    appearance_->apply_face_state(next, tr("Revertir piezas de cabeza al original"));
  } else if (category == QLatin1String("scalar")) {
    if (key == QLatin1String("hairColor")) {
      next.values.hair_color = base.values.hair_color;
      next.source = "Revertir color de cabello";
      appearance_->apply_face_state(next, tr("Revertir color de cabello"));
    } else if (key == QLatin1String("skinTone")) {
      next.values.skin_tone = base.values.skin_tone;
      next.source = "Revertir tono de piel";
      appearance_->apply_face_state(next, tr("Revertir tono de piel"));
    } else if (key == QLatin1String("weight")) {
      appearance_->setWeight(base.values.weight);
    } else if (key == QLatin1String("height")) {
      appearance_->setHeight(appearance_->draft_state().height_baseline());
    } else if (key == QLatin1String("headTexture")) {
      appearance_->setHeadTexture(qs(base.head_texture));
    }
  }
}

void BeautyLabViewModel::refreshAll() {
  rebuild_comparison_and_capabilities();
  refreshHeadPartGallery();
  rebuild_quality_check();
  emit libraryChanged();
}

void BeautyLabViewModel::setHairCategory(const QString& category) {
  const QString c = category.trimmed().isEmpty() ? QStringLiteral("Hair") : category.trimmed();
  if (hair_category_ == c) return;
  hair_category_ = c;
  hair_package_filter_ = QStringLiteral("all");
  selected_head_part_inspection_.clear();
  emit headPartInspectionChanged();
  refreshHeadPartGallery();
}

void BeautyLabViewModel::setHairSearchText(const QString& text) {
  if (hair_search_text_ == text) return;
  hair_search_text_ = text;
  rebuild_filtered_head_parts();
}

void BeautyLabViewModel::setHairPackageFilter(const QString& packageFamily) {
  const QString p = packageFamily.trimmed().isEmpty() ? QStringLiteral("all") : packageFamily.trimmed();
  if (hair_package_filter_ == p) return;
  hair_package_filter_ = p;
  rebuild_filtered_head_parts();
}

void BeautyLabViewModel::setHairCompatibilityFilter(const QString& filter) {
  const QString f = filter.trimmed().isEmpty() ? QStringLiteral("all") : filter.trimmed();
  if (hair_compatibility_filter_ == f) return;
  hair_compatibility_filter_ = f;
  rebuild_filtered_head_parts();
}

void BeautyLabViewModel::setEyeColorGroupFilter(const QString& filter) {
  const QString f = filter.trimmed().isEmpty() ? QStringLiteral("all") : filter.trimmed();
  if (eye_color_group_filter_ == f) return;
  eye_color_group_filter_ = f;
  rebuild_filtered_head_parts();
}

void BeautyLabViewModel::refreshHeadPartGallery() {
  if (!appearance_ || !appearance_->loaded()) {
    filtered_head_parts_.clear();
    available_packages_.clear();
    emit hairStudioChanged();
    return;
  }
  appearance_->searchHeadParts(hair_category_, QString());
}

void BeautyLabViewModel::inspectHeadPart(const QVariantMap& headPartRow) {
  if (headPartRow.isEmpty()) {
    selected_head_part_inspection_.clear();
    emit headPartInspectionChanged();
    return;
  }
  const auto snap = snapshot_ ? snapshot_() : nullptr;
  const auto ctx = snap ? ResourceContext::for_snapshot(snap) : nullptr;
  selected_head_part_inspection_ = inspect_head_part_asset(
      headPartRow,
      appearance_ ? appearance_->recipe() : nullptr,
      snap.get(),
      ctx ? ctx->resolver.get() : nullptr,
      ctx ? ctx->face_assets.get() : nullptr);
  selected_head_part_inspection_["isFavorite"] = isHairFavorite(headPartRow.value("formKey").toString());
  emit headPartInspectionChanged();
}

void BeautyLabViewModel::selectAndApplyHeadPart(const QString& formKey, const QString& type) {
  if (!appearance_ || !appearance_->loaded() || formKey.trimmed().isEmpty()) return;
  // Find row in filtered or raw results to inspect it as well
  for (const auto& v : appearance_->head_part_options()) {
    const auto m = v.toMap();
    if (m.value("formKey").toString().compare(formKey, Qt::CaseInsensitive) == 0) {
      inspectHeadPart(m);
      break;
    }
  }
  appearance_->setHeadPart(formKey, type.isEmpty() ? hair_category_ : type);
}

void BeautyLabViewModel::applyInspectedHeadPart() {
  if (selected_head_part_inspection_.isEmpty()) return;
  const QString fk = selected_head_part_inspection_.value("formKey").toString();
  const QString type = selected_head_part_inspection_.value("type").toString();
  if (fk.isEmpty()) return;
  if (selected_head_part_inspection_.value("isBaldPart").toBool() && type == QLatin1String("Hair")) {
    if (appearance_) appearance_->setIntentionallyBald(true);
    return;
  }
  if (appearance_) appearance_->setHeadPart(fk, type);
}

QVariantMap BeautyLabViewModel::applyStylePreset(const QString& styleId, const QVariantMap& options) {
  if (!appearance_ || !appearance_->loaded() || !appearance_->recipe()) {
    last_style_result_ = {
        {"styleId", styleId},
        {"changed", false},
        {"summary", tr("Selecciona primero un NPC válido para aplicar un estilo Next-Gen.")}};
    emit styleResultChanged();
    return last_style_result_;
  }

  const auto res = apply_next_gen_style(
      styleId,
      options,
      *appearance_->recipe(),
      appearance_->draft_state().face_baseline(),
      appearance_->draft_state().face(),
      appearance_->head_part_options(),
      appearance_->face_compatibility());

  if (res.changed) {
    appearance_->apply_face_state(res.next_state, tr("Aplicar estilo Next-Gen: %1").arg(res.style_name));
  }

  last_style_result_ = {
      {"styleId", res.style_id},
      {"styleName", res.style_name},
      {"changed", res.changed},
      {"appliedRules", res.applied_rules},
      {"skippedRules", res.skipped_rules},
      {"summary", res.summary}};
  emit styleResultChanged();
  return last_style_result_;
}

QVariantMap BeautyLabViewModel::runQualityCheck() {
  rebuild_quality_check();
  return quality_check_;
}

QVariantMap BeautyLabViewModel::captureViewportWithMetadata(QObject* viewportOrWindow, const QString& notes) {
  rebuild_quality_check();

  const QString base_dir = QDir(library_.directory()).filePath(QStringLiteral("captures"));
  QDir().mkpath(base_dir);
  const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  QString npc_tag = appearance_ ? appearance_->npc_form_key() : QStringLiteral("no-npc");
  npc_tag.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
  const QString stem = QStringLiteral("capture_%1_%2").arg(npc_tag, stamp);
  const QString png_path = QDir(base_dir).filePath(stem + QStringLiteral(".png"));
  const QString meta_path = QDir(base_dir).filePath(stem + QStringLiteral(".quality.json"));

  QImage img = grab_object_image(viewportOrWindow);
  bool image_saved = false;
  if (!img.isNull()) {
    image_saved = img.save(png_path, "PNG");
  }

  const auto* active_rec = appearance_ ? appearance_->recipe() : nullptr;
  QJsonObject meta{
      {"schemaVersion", 1},
      {"capturedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {"npcFormKey", appearance_ ? appearance_->npc_form_key() : QString()},
      {"npcLabel", appearance_ ? appearance_->npc_label() : QString()},
      {"raceEditorId", active_rec ? qs(active_rec->race_editor_id) : QString()},
      {"female", active_rec ? active_rec->female : false},
      {"imageSaved", image_saved},
      {"imagePath", image_saved ? png_path : QString()},
      {"imageWidth", img.width()},
      {"imageHeight", img.height()},
      {"notes", notes},
      {"inGameVerified", false},
      {"verificationStatus", QStringLiteral("PENDIENTE_SKYRIM")},
      {"rendererDisclaimer", tr("El motor de renderizado Qt Quick 3D es una aproximación técnica y difiere del motor Creation Engine / Community Shaders de Skyrim SE/AE.")},
      {"comparison", QJsonObject::fromVariantMap(comparison_)},
      {"qualityCheck", QJsonObject::fromVariantMap(quality_check_)}};

  QSaveFile f(meta_path);
  bool meta_saved = false;
  if (f.open(QIODevice::WriteOnly)) {
    const QByteArray bytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
    meta_saved = (f.write(bytes) == bytes.size() && f.commit());
  }

  last_capture_report_ = {
      {"ok", meta_saved},
      {"imageSaved", image_saved},
      {"imagePath", image_saved ? png_path : QString()},
      {"metadataPath", meta_saved ? meta_path : QString()},
      {"capturedAt", meta.value("capturedAt").toString()},
      {"summary", meta_saved
                      ? tr("Captura y metadata reproducible guardadas en %1").arg(meta_path)
                      : tr("No se pudo escribir el archivo de metadata en %1").arg(meta_path)}};
  emit captureReportChanged();
  return last_capture_report_;
}

QVariantList BeautyLabViewModel::annotate_entries_compatibility(const QVariantList& entries) const {
  const auto snap = snapshot_ ? snapshot_() : nullptr;
  const auto* rec = appearance_ ? appearance_->recipe() : nullptr;
  const QVariantMap compat = appearance_ ? appearance_->face_compatibility() : QVariantMap{};

  QVariantList out;
  out.reserve(entries.size());
  for (const auto& v : entries) {
    auto m = v.toMap();
    m["liveCompatibility"] = library_.evaluate_entry_compatibility(m, rec, compat, snap.get());
    out.push_back(m);
  }
  return out;
}

QVariantList BeautyLabViewModel::library_presets() const {
  return annotate_entries_compatibility(library_.presets());
}

QVariantList BeautyLabViewModel::favorite_hairs() const {
  return annotate_entries_compatibility(library_.favorite_hairs());
}

QVariantList BeautyLabViewModel::head_eye_combos() const {
  return annotate_entries_compatibility(library_.head_eye_combos());
}

QVariantList BeautyLabViewModel::facial_configs() const {
  return annotate_entries_compatibility(library_.facial_configs());
}

void BeautyLabViewModel::set_library_status(bool ok, const QString& message, const QStringList& warnings) {
  last_library_status_ = {
      {"ok", ok},
      {"message", message},
      {"warnings", warnings},
      {"timestamp", QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))}};
  emit libraryStatusChanged();
}

QVariantMap BeautyLabViewModel::saveCurrentAsPreset(const QString& name,
                                                    const QString& category,
                                                    const QString& notes,
                                                    QObject* viewportOrWindow) {
  if (!appearance_ || !appearance_->loaded() || !appearance_->recipe()) {
    set_library_status(false, tr("No hay ningún NPC cargado para guardar como preset."));
    return last_library_status_;
  }

  QString preview_path;
  if (viewportOrWindow) {
    const QImage img = grab_object_image(viewportOrWindow);
    if (!img.isNull()) {
      const QString preview_id = QStringLiteral("preview-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(10);
      preview_path = library_.save_preview_image(preview_id, img);
    }
  }

  QString err;
  const QString id = library_.save_preset(
      name,
      category,
      notes,
      *appearance_->recipe(),
      appearance_->draft_state().face(),
      appearance_->height(),
      appearance_->intentionally_bald(),
      proposal_status(),
      proposal_status_detail(),
      provenance(),
      preview_path,
      &err);

  if (id.isEmpty()) {
    set_library_status(false, tr("Error al guardar preset: %1").arg(err));
    return last_library_status_;
  }

  emit libraryChanged();
  set_library_status(
      true,
      tr("Preset «%1» guardado en la biblioteca (%2).").arg(name.trimmed().isEmpty() ? tr("Sin título") : name.trimmed(), proposal_status()));
  return last_library_status_;
}

bool BeautyLabViewModel::deletePreset(const QString& id) {
  QString err;
  if (!library_.delete_preset(id, &err)) {
    set_library_status(false, tr("No se pudo eliminar el preset: %1").arg(err));
    return false;
  }
  emit libraryChanged();
  set_library_status(true, tr("Preset eliminado de la biblioteca."));
  return true;
}

QVariantMap BeautyLabViewModel::applyLibraryEntry(const QVariantMap& entry) {
  if (!appearance_ || !appearance_->loaded() || !appearance_->recipe()) {
    set_library_status(false, tr("Selecciona un NPC antes de aplicar una entrada de la biblioteca."));
    return last_library_status_;
  }

  const auto snap = snapshot_ ? snapshot_() : nullptr;
  const auto live_compat = library_.evaluate_entry_compatibility(
      entry, appearance_->recipe(), appearance_->face_compatibility(), snap.get());
  if (!live_compat.value("compatible").toBool()) {
    const QStringList reasons = live_compat.value("reasons").toStringList();
    set_library_status(false, tr("Entrada incompatible con el NPC actual: %1").arg(reasons.join(QStringLiteral(" · "))), reasons);
    return last_library_status_;
  }

  const QString kind = entry.value("kind").toString();
  if (kind == QLatin1String("favorite-hair")) {
    const QString fk = entry.value("formKey").toString();
    const QString type = entry.value("type", QStringLiteral("Hair")).toString();
    appearance_->setHeadPart(fk, type);
    set_library_status(true, tr("Cabello favorito «%1» aplicado.").arg(entry.value("name").toString()));
    return last_library_status_;
  }

  QStringList warnings;
  const auto next_state = library_.apply_entry_to_state(
      entry,
      *appearance_->recipe(),
      appearance_->draft_state().face(),
      appearance_->face_compatibility(),
      &warnings);

  if (!next_state) {
    set_library_status(false, tr("No se pudo aplicar «%1»: %2").arg(entry.value("name").toString(), warnings.join(QStringLiteral(" · "))));
    return last_library_status_;
  }

  appearance_->apply_face_state(*next_state, tr("Aplicar «%1» de Beauty Library").arg(entry.value("name").toString()));
  if (entry.value("faceState").toMap().contains("height")) {
    const float h = entry.value("faceState").toMap().value("height").toFloat();
    if (h >= 0.5f && h <= 1.5f) appearance_->setHeight(h);
  }
  set_library_status(true, tr("«%1» aplicado sobre el NPC actual.").arg(entry.value("name").toString()), warnings);
  return last_library_status_;
}

bool BeautyLabViewModel::toggleFavoriteHair(const QVariantMap& hairRow) {
  QString err;
  const bool now_fav = library_.toggle_favorite_hair(hairRow, &err);
  if (!err.isEmpty()) {
    set_library_status(false, tr("Error al actualizar favoritos: %1").arg(err));
    return false;
  }
  if (!selected_head_part_inspection_.isEmpty() &&
      selected_head_part_inspection_.value("formKey").toString().compare(
          hairRow.value("formKey").toString(), Qt::CaseInsensitive) == 0) {
    selected_head_part_inspection_["isFavorite"] = now_fav;
    emit headPartInspectionChanged();
  }
  rebuild_filtered_head_parts();
  emit libraryChanged();
  set_library_status(
      true,
      now_fav ? tr("Peinado «%1» añadido a favoritos.").arg(hairRow.value("name").toString())
              : tr("Peinado «%1» eliminado de favoritos.").arg(hairRow.value("name").toString()));
  return now_fav;
}

bool BeautyLabViewModel::isHairFavorite(const QString& formKey) const {
  return library_.favorite_hair_keys().contains(formKey, Qt::CaseInsensitive);
}

QVariantMap BeautyLabViewModel::saveCurrentHeadEyeCombo(const QString& name, const QString& notes) {
  Q_UNUSED(notes);
  if (!appearance_ || !appearance_->loaded() || !appearance_->recipe()) {
    set_library_status(false, tr("Selecciona un NPC antes de guardar una combinación de cabeza y ojos."));
    return last_library_status_;
  }
  QString err;
  const QString id = library_.save_head_eye_combo(
      name, *appearance_->recipe(), appearance_->draft_state().face(), provenance(), QString(), &err);
  if (id.isEmpty()) {
    set_library_status(false, tr("Error al guardar combinación: %1").arg(err));
    return last_library_status_;
  }
  emit libraryChanged();
  set_library_status(true, tr("Combinación de cabeza y ojos «%1» guardada.").arg(name));
  return last_library_status_;
}

bool BeautyLabViewModel::deleteHeadEyeCombo(const QString& id) {
  QString err;
  if (!library_.delete_head_eye_combo(id, &err)) {
    set_library_status(false, tr("Error al eliminar combinación: %1").arg(err));
    return false;
  }
  emit libraryChanged();
  set_library_status(true, tr("Combinación de cabeza y ojos eliminada."));
  return true;
}

QVariantMap BeautyLabViewModel::saveCurrentFacialConfig(const QString& name, const QString& notes) {
  Q_UNUSED(notes);
  if (!appearance_ || !appearance_->loaded() || !appearance_->recipe()) {
    set_library_status(false, tr("Selecciona un NPC antes de guardar una configuración facial."));
    return last_library_status_;
  }
  QString err;
  const QString id = library_.save_facial_config(
      name, *appearance_->recipe(), appearance_->draft_state().face(), QString(), &err);
  if (id.isEmpty()) {
    set_library_status(false, tr("Error al guardar configuración facial: %1").arg(err));
    return last_library_status_;
  }
  emit libraryChanged();
  set_library_status(true, tr("Configuración facial «%1» guardada.").arg(name));
  return last_library_status_;
}

bool BeautyLabViewModel::deleteFacialConfig(const QString& id) {
  QString err;
  if (!library_.delete_facial_config(id, &err)) {
    set_library_status(false, tr("Error al eliminar configuración facial: %1").arg(err));
    return false;
  }
  emit libraryChanged();
  set_library_status(true, tr("Configuración facial eliminada."));
  return true;
}

QVariantMap BeautyLabViewModel::importCompatibleResource(const QString& pathOrUrl, const QString& categoryTag) {
  const QString path = local_file_path(pathOrUrl);
  QString err;
  const QVariantMap res = library_.import_compatible_resource(path, categoryTag, &err);
  if (res.isEmpty()) {
    set_library_status(false, tr("No se pudo importar el recurso: %1").arg(err));
    return last_library_status_;
  }
  emit libraryChanged();
  set_library_status(
      true,
      tr("Recurso «%1» importado (%2) sin redistribuir contenido protegido.")
          .arg(res.value("name").toString(), res.value("format").toString()));
  return last_library_status_;
}

bool BeautyLabViewModel::deleteImportedResource(const QString& id) {
  QString err;
  if (!library_.delete_imported_resource(id, &err)) {
    set_library_status(false, tr("No se pudo eliminar el recurso importado: %1").arg(err));
    return false;
  }
  emit libraryChanged();
  set_library_status(true, tr("Recurso importado eliminado del catálogo."));
  return true;
}

bool BeautyLabViewModel::reloadLibrary() {
  const bool ok = library_.load();
  emit libraryChanged();
  set_library_status(ok, ok ? tr("Biblioteca recargada desde disco (%1).").arg(library_.file_path())
                            : tr("Error al recargar la biblioteca desde disco."));
  return ok;
}

void BeautyLabViewModel::invalidateCaches() {
  const auto snap = snapshot_ ? snapshot_() : nullptr;
  if (snap) {
    if (const auto ctx = ResourceContext::for_snapshot(snap)) {
      if (ctx->face_assets) ctx->face_assets->clear();
      if (ctx->scene_cache) ctx->scene_cache->clear();
    }
  }
  if (scene_) scene_->reload();
  if (appearance_ && appearance_->loaded()) {
    appearance_->apply_face_state(appearance_->draft_state().face(), QString());
  }
  if (console_) {
    console_->append(QStringLiteral("info"), QStringLiteral("beauty-lab"),
                     tr("Cachés de FaceGen (NIF/TRI) y escena 3D (texturas/mallas) invalidadas y recargadas."));
  }
  set_library_status(true, tr("Cachés de recursos 3D y FaceGen invalidadas y reconstruidas."));
}

}  // namespace sns::app
