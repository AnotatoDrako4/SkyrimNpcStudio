#include "outfit_fit_doctor_viewmodel.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include "outfit_editor.hpp"
#include "scene_viewmodel.hpp"
#include "studio_controller.hpp"
#include "worker_client.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/outfit/outfit_draft.hpp"

namespace sns::app {

namespace {

QString local_path_from_url_or_string(const QString& path_or_url) {
  if (path_or_url.startsWith("file://", Qt::CaseInsensitive)) {
    return QUrl(path_or_url).toLocalFile();
  }
  return path_or_url;
}

}  // namespace

OutfitFitDoctorViewModel::OutfitFitDoctorViewModel(QObject* parent)
    : QObject(parent) {
  debounce_timer_.setSingleShot(true);
  connect(&debounce_timer_, &QTimer::timeout, this, &OutfitFitDoctorViewModel::executeDiagnosis);
}

OutfitFitDoctorViewModel::OutfitFitDoctorViewModel(
    SnapshotProvider snapshot, OutfitEditor* outfit, SceneViewModel* scene,
    WorkerClient* worker, QObject* parent)
    : QObject(parent),
      snapshot_(std::move(snapshot)),
      outfit_(outfit),
      scene_(scene),
      worker_(worker) {
  debounce_timer_.setSingleShot(true);
  connect(&debounce_timer_, &QTimer::timeout, this, &OutfitFitDoctorViewModel::executeDiagnosis);
  if (outfit_) {
    connect(outfit_, &OutfitEditor::targetChanged, this, &OutfitFitDoctorViewModel::scheduleDiagnosis);
    connect(outfit_, &OutfitEditor::changed, this, &OutfitFitDoctorViewModel::scheduleDiagnosis);
  }
}

void OutfitFitDoctorViewModel::set_dependencies(
    SnapshotProvider snapshot, OutfitEditor* outfit, SceneViewModel* scene, WorkerClient* worker) {
  snapshot_ = std::move(snapshot);
  if (outfit_ != outfit) {
    if (outfit_) disconnect(outfit_, nullptr, this, nullptr);
    outfit_ = outfit;
    if (outfit_) {
      connect(outfit_, &OutfitEditor::targetChanged, this, &OutfitFitDoctorViewModel::scheduleDiagnosis);
      connect(outfit_, &OutfitEditor::changed, this, &OutfitFitDoctorViewModel::scheduleDiagnosis);
    }
  }
  scene_ = scene;
  worker_ = worker;
  scheduleDiagnosis();
}

void OutfitFitDoctorViewModel::scheduleDiagnosis() {
  debounce_timer_.start(150);
}

void OutfitFitDoctorViewModel::init(QObject* studio_obj) {
  if (auto* sc = qobject_cast<StudioController*>(studio_obj)) {
    set_dependencies([sc] { return sc->snapshot(); }, sc->outfit(), sc->scene(), sc->worker());
    sc->set_fit_doctor(this);  // the Character Forge Modo Fácil reads its last diagnosis
  }
}

bool OutfitFitDoctorViewModel::has_target() const {
  return !report_.body.form_key.empty();
}

QString OutfitFitDoctorViewModel::npc_form_key() const {
  return QString::fromStdString(report_.body.form_key);
}

QString OutfitFitDoctorViewModel::npc_name() const {
  return QString::fromStdString(report_.body.name);
}

QString OutfitFitDoctorViewModel::body_family() const {
  return QString::fromStdString(outfit::OutfitFitDoctor::family_to_string(report_.body.body_family));
}

QString OutfitFitDoctorViewModel::body_confidence() const {
  return QString::fromStdString(outfit::OutfitFitDoctor::confidence_to_string(report_.body.confidence));
}

QString OutfitFitDoctorViewModel::body_provider() const {
  return QString::fromStdString(report_.body.body_provider);
}

QString OutfitFitDoctorViewModel::body_mesh_path() const {
  return QString::fromStdString(report_.body.body_mesh_path);
}

bool OutfitFitDoctorViewModel::body_has_tri() const {
  return report_.body.body_has_tri;
}

QString OutfitFitDoctorViewModel::outfit_name() const {
  return QString::fromStdString(report_.outfit_name);
}

QString OutfitFitDoctorViewModel::outfit_form_key() const {
  return QString::fromStdString(report_.outfit_form_key);
}

int OutfitFitDoctorViewModel::total_issues_count() const {
  return static_cast<int>(report_.total_issues_count);
}

int OutfitFitDoctorViewModel::incompatible_pieces_count() const {
  return static_cast<int>(report_.incompatible_pieces_count);
}

int OutfitFitDoctorViewModel::fixable_alternatives_count() const {
  return static_cast<int>(report_.fixable_alternatives_count);
}

bool OutfitFitDoctorViewModel::needs_bodyslide_refit() const {
  return report_.needs_bodyslide_refit;
}

bool OutfitFitDoctorViewModel::is_draft_mode() const {
  return !repair_history_.empty();
}

bool OutfitFitDoctorViewModel::can_undo_repair() const {
  return !repair_history_.empty();
}

QVariantList OutfitFitDoctorViewModel::proposed_repairs() const {
  QVariantList list;
  for (const auto& r : repair_history_) {
    QVariantMap m;
    m["pieceIndex"] = r.piece_index;
    m["originalMeshPath"] = r.original_mesh_path;
    m["repairedMeshPath"] = r.repaired_mesh_path;
    m["modName"] = r.mod_name;
    m["description"] = r.description;
    list.append(m);
  }
  return list;
}

QVariantList OutfitFitDoctorViewModel::pieces() const {
  QVariantList list;
  for (const auto& p : report_.pieces) {
    QVariantMap m;
    m["armorFormKey"] = QString::fromStdString(p.armor_form_key);
    m["armorName"] = QString::fromStdString(p.armor_name);
    m["addonFormKey"] = QString::fromStdString(p.addon_form_key);
    m["slotDescription"] = QString::fromStdString(p.slot_description);
    m["slotMask"] = static_cast<qint64>(p.slot_mask);

    QVariantList slotList;
    for (int s : p.biped_slots) slotList.append(s);
    m["slots"] = slotList;

    m["effectiveNifPath"] = QString::fromStdString(p.effective_nif_path);
    m["winningProvider"] = QString::fromStdString(p.winning_provider);
    m["isOverwrite"] = p.is_overwrite;
    m["isGameData"] = p.is_game_data;
    m["detectedFamily"] = QString::fromStdString(outfit::OutfitFitDoctor::family_to_string(p.detected_family));
    m["confidence"] = QString::fromStdString(outfit::OutfitFitDoctor::confidence_to_string(p.confidence));
    m["familyReason"] = QString::fromStdString(p.family_reason);
    m["topologyCompatible"] = p.topology_compatible;
    m["morphsAvailable"] = p.morphs_available;
    m["weightVariantsValid"] = p.weight_variants_valid;
    m["bipedPartitionsCoherent"] = p.biped_partitions_coherent;
    m["skeletonCompatible"] = p.skeleton_compatible;

    // Geometry details
    QVariantMap geom;
    geom["vertexCount"] = static_cast<qint64>(p.geometry.vertex_count);
    geom["triangleCount"] = static_cast<qint64>(p.geometry.triangle_count);
    geom["hasBodyTri"] = p.geometry.has_body_tri;
    geom["siblingTriExists"] = p.geometry.sibling_tri_exists;
    geom["hasWeight0"] = p.geometry.has_weight_0;
    geom["hasWeight1"] = p.geometry.has_weight_1;

    QStringList shapes;
    for (const auto& s : p.geometry.shape_names) shapes.append(QString::fromStdString(s));
    geom["shapeNames"] = shapes;

    QStringList physics;
    for (const auto& b : p.geometry.physics_bones) physics.append(QString::fromStdString(b));
    geom["physicsBones"] = physics;

    QStringList hidden;
    for (const auto& h : p.geometry.hidden_or_collision_shapes) hidden.append(QString::fromStdString(h));
    geom["hiddenOrCollisionShapes"] = hidden;

    m["geometry"] = geom;

    // Issues
    QVariantList issuesList;
    for (const auto& is : p.issues) {
      QVariantMap im;
      im["code"] = QString::fromStdString(is.code_string());
      im["severity"] = QString::fromStdString(is.severity_string());
      im["filePath"] = QString::fromStdString(is.file_path);
      im["provider"] = QString::fromStdString(is.provider);
      im["evidence"] = QString::fromStdString(is.evidence);
      im["possibleFix"] = QString::fromStdString(is.possible_fix);
      issuesList.append(im);
    }
    m["issues"] = issuesList;

    // Compatible Alternatives
    QVariantList altsList;
    for (const auto& alt : p.compatible_alternatives) {
      QVariantMap am;
      am["resourceId"] = QString::fromStdString(alt.resource_id);
      am["modName"] = QString::fromStdString(alt.mod_name);
      am["dataPath"] = QString::fromStdString(alt.data_path);
      am["family"] = QString::fromStdString(outfit::OutfitFitDoctor::family_to_string(alt.family));
      am["confidence"] = QString::fromStdString(outfit::OutfitFitDoctor::confidence_to_string(alt.confidence));
      am["hasTri"] = alt.has_tri;
      am["hasWeightVariants"] = alt.has_weight_variants;
      am["bodyslideProject"] = QString::fromStdString(alt.bodyslide_project);
      am["description"] = QString::fromStdString(alt.description);
      altsList.append(am);
    }
    m["compatibleAlternatives"] = altsList;

    list.append(m);
  }
  return list;
}

void OutfitFitDoctorViewModel::runDiagnosis() {
  debounce_timer_.stop();
  executeDiagnosis();
}

void OutfitFitDoctorViewModel::executeDiagnosis() {
  if (!snapshot_) return;
  const auto snap = snapshot_();
  if (!snap) return;
  const int serial = ++diagnosis_serial_;
  if (!outfit_ || outfit_->npc_form_key().isEmpty() || !worker_ || worker_->state() != WorkerClient::State::Ready) {
    // Nothing real to diagnose: no report, never a stand-in NPC.
    report_ = {};
    analyzing_ = false;
    emit analyzingChanged();
    emit diagnosisChanged();
    return;
  }
  analyzing_ = true;
  emit analyzingChanged();

  // The body and the garments the game equips: the worker's appearance of this NPC with the pieces being edited (the same
  // query the 3D viewer makes), so the diagnosis reads the effective skin ARMA and each piece's ARMA mesh.
  nlohmann::json params{{"npcFormKey", outfit_->npc_form_key().toStdString()}, {"armorFormKeys", nlohmann::json::array()}};
  std::map<std::string, std::string> piece_names;  // the names the outfit editor shows (the worker answers with EditorIDs)
  for (const auto& p : outfit_->current_pieces()) {
    params["armorFormKeys"].push_back(p.form_key);
    if (!p.name.empty()) piece_names[p.form_key] = p.name;
  }
  const auto name = outfit_->npc_label().toStdString();
  worker_->call(ipc::protocol::method::kGetAppearance, params, std::chrono::seconds(30), this,
    [self = QPointer<OutfitFitDoctorViewModel>(this), snap, serial, name, piece_names](const nlohmann::json& appearance) {
      if (!self || self->diagnosis_serial_ != serial) return;
      auto npc_details = outfit::OutfitFitDoctor::npc_details_from_appearance(appearance, name);
      for (auto& armor : npc_details["defaultOutfit"]["armors"])
        if (const auto it = piece_names.find(armor.value("formKey", std::string{})); it != piece_names.end()) armor["name"] = it->second;
      QThreadPool::globalInstance()->start([self, snap, npc_details, serial] {
        const auto report = outfit::OutfitFitDoctor::analyze(snap->vfs, npc_details, nullptr);
        QMetaObject::invokeMethod(self, [self, report, serial] {
          if (!self || self->diagnosis_serial_ != serial) return;
          self->report_ = report;
          self->analyzing_ = false;
          emit self->analyzingChanged();
          emit self->diagnosisChanged();
        }, Qt::QueuedConnection);
      });
    },
    [self = QPointer<OutfitFitDoctorViewModel>(this), serial](int, const QString&, const nlohmann::json&) {
      if (!self || self->diagnosis_serial_ != serial) return;
      self->report_ = {};
      self->analyzing_ = false;
      emit self->analyzingChanged();
      emit self->diagnosisChanged();
    });
}

void OutfitFitDoctorViewModel::setComparisonMode(const QString& mode) {
  if (comparison_mode_ == mode) return;
  comparison_mode_ = mode;
  emit comparisonModeChanged();
  update_scene_preview();
}

void OutfitFitDoctorViewModel::applyAlternativeRepair(int piece_index, int alternative_index) {
  if (piece_index < 0 || piece_index >= static_cast<int>(report_.pieces.size())) return;
  auto& piece = report_.pieces[piece_index];
  if (alternative_index < 0 || alternative_index >= static_cast<int>(piece.compatible_alternatives.size())) return;

  const auto& alt = piece.compatible_alternatives[alternative_index];

  RepairAction action;
  action.piece_index = piece_index;
  action.original_mesh_path = QString::fromStdString(piece.effective_nif_path);
  action.repaired_mesh_path = QString::fromStdString(alt.data_path);
  action.mod_name = QString::fromStdString(alt.mod_name);
  action.description = QString::fromStdString(alt.description);
  repair_history_.push_back(action);

  // Apply replacement
  piece.effective_nif_path = alt.data_path;
  piece.winning_provider = alt.mod_name;
  piece.detected_family = alt.family;
  piece.confidence = alt.confidence;
  piece.topology_compatible = true;
  piece.morphs_available = alt.has_tri;
  piece.family_reason = std::format("{} (Reparada con {})",
                                    outfit::OutfitFitDoctor::family_to_string(alt.family), alt.mod_name);

  // Remove family mismatch issues
  std::erase_if(piece.issues, [](const outfit::FitIssue& is) {
    return is.code == outfit::DiagnosticCode::BodyOutfitFamilyMismatch ||
           is.code == outfit::DiagnosticCode::OutfitTopologyMismatch;
  });

  comparison_mode_ = "B";
  emit comparisonModeChanged();
  emit draftChanged();
  emit diagnosisChanged();
  update_scene_preview();
}

void OutfitFitDoctorViewModel::undoLastRepair() {
  if (repair_history_.empty()) return;
  repair_history_.pop_back();

  // Re-run diagnosis to restore base state and re-apply remaining history
  runDiagnosis();
  for (const auto& action : repair_history_) {
    if (action.piece_index >= 0 && action.piece_index < static_cast<int>(report_.pieces.size())) {
      auto& piece = report_.pieces[action.piece_index];
      piece.effective_nif_path = action.repaired_mesh_path.toStdString();
      piece.winning_provider = action.mod_name.toStdString();
      piece.topology_compatible = true;
    }
  }

  if (repair_history_.empty()) {
    comparison_mode_ = "A";
    emit comparisonModeChanged();
  }

  emit draftChanged();
  emit diagnosisChanged();
  update_scene_preview();
}

void OutfitFitDoctorViewModel::revertAllRepairs() {
  repair_history_.clear();
  comparison_mode_ = "A";
  emit comparisonModeChanged();
  emit draftChanged();
  runDiagnosis();
  update_scene_preview();
}

void OutfitFitDoctorViewModel::update_scene_preview() {
  if (!scene_) return;

  if (comparison_mode_ == "A") {
    // Mode A: Baseline load-order pieces
    scene_->setPreviewArmor(QVariantList{});
  } else {
    // Mode B: Repaired preview armors
    QVariantList previewList;
    for (const auto& p : report_.pieces) {
      previewList.append(QString::fromStdString(p.armor_form_key));
    }
    scene_->setPreviewArmor(previewList);
  }
}

void OutfitFitDoctorViewModel::isolatePieceInViewport(int piece_index, bool isolate) {
  if (!scene_) return;
  if (!isolate || piece_index < 0 || piece_index >= static_cast<int>(report_.pieces.size())) {
    isolated_piece_index_ = -1;
    update_scene_preview();
    return;
  }
  isolated_piece_index_ = piece_index;
  const auto& piece = report_.pieces[piece_index];
  scene_->setPreviewArmor(QVariantList{QString::fromStdString(piece.armor_form_key)});
}

void OutfitFitDoctorViewModel::setViewportCameraPreset(const QString& view_preset) {
  emit cameraPresetRequested(view_preset);
}

void OutfitFitDoctorViewModel::toggleWireframe(bool enabled) {
  if (wireframe_enabled_ == enabled) return;
  wireframe_enabled_ = enabled;
  emit wireframeChanged();
}

bool OutfitFitDoctorViewModel::exportDoctorMod(const QString& target_dir_or_url, const QString& mod_name) {
  const QString target_dir = local_path_from_url_or_string(target_dir_or_url);
  if (target_dir.isEmpty()) {
    emit exportError(tr("Directorio de exportación no especificado."));
    return false;
  }

  QDir dir(target_dir);
  const QString mod_folder_name = mod_name.isEmpty() ? "OutfitFitDoctor_Fix" : mod_name;
  const QString export_path = dir.filePath(mod_folder_name);
  QDir().mkpath(export_path);

  // Write manifest.json
  QJsonObject manifest;
  manifest["modName"] = mod_folder_name;
  manifest["targetNpc"] = QString::fromStdString(report_.body.form_key);
  manifest["npcName"] = QString::fromStdString(report_.body.name);
  manifest["bodyFamily"] = QString::fromStdString(outfit::OutfitFitDoctor::family_to_string(report_.body.body_family));
  manifest["generatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  manifest["safePatch"] = true;
  manifest["inGameVerified"] = false;
  manifest["repairsCount"] = static_cast<int>(repair_history_.size());

  QJsonArray repairsArr;
  for (const auto& r : repair_history_) {
    QJsonObject ro;
    ro["pieceIndex"] = r.piece_index;
    ro["original"] = r.original_mesh_path;
    ro["repaired"] = r.repaired_mesh_path;
    ro["modProvider"] = r.mod_name;
    ro["description"] = r.description;
    repairsArr.append(ro);
  }
  manifest["repairs"] = repairsArr;

  QFile manifestFile(QDir(export_path).filePath("manifest.json"));
  if (manifestFile.open(QIODevice::WriteOnly)) {
    manifestFile.write(QJsonDocument(manifest).toJson());
    manifestFile.close();
  }

  // Write README.md
  QFile readmeFile(QDir(export_path).filePath("README.md"));
  if (readmeFile.open(QIODevice::WriteOnly)) {
    QString readme = QStringLiteral(
        "# %1 — Parche Seguro de Ajuste de Vestuario\n\n"
        "Generado automáticamente por **Outfit Fit Doctor** de Skyrim NPC Studio.\n\n"
        "## Detalles del NPC\n"
        "- **NPC Objetivo:** %2 (%3)\n"
        "- **Cuerpo Base:** %4 (Proveedor: %5)\n"
        "- **Fecha de Generación:** %6\n\n"
        "## Correcciones Aplicadas\n")
        .arg(mod_folder_name, QString::fromStdString(report_.body.name), QString::fromStdString(report_.body.form_key),
             QString::fromStdString(outfit::OutfitFitDoctor::family_to_string(report_.body.body_family)),
             QString::fromStdString(report_.body.body_provider),
             QDateTime::currentDateTime().toString(Qt::ISODate));

    for (const auto& r : repair_history_) {
      readme += QStringLiteral("- **Pieza %1:** Reemplazada `%2` por `%3` (de `%4`)\n")
                    .arg(r.piece_index)
                    .arg(r.original_mesh_path, r.repaired_mesh_path, r.mod_name);
    }

    readme += QStringLiteral(
        "\n## Instrucciones de Instalación en MO2\n"
        "1. Instala esta carpeta como un nuevo mod en Mod Organizer 2.\n"
        "2. Colócala con prioridad superior a los mods del cuerpo y de la prenda de este NPC.\n"
        "3. Comprueba el ajuste del vestuario directamente dentro de Skyrim.\n");

    readmeFile.write(readme.toUtf8());
    readmeFile.close();
  }

  // Write fit-report.json
  const auto json_report = outfit::OutfitFitDoctor::to_json(report_);
  QFile reportFile(QDir(export_path).filePath("fit-report.json"));
  if (reportFile.open(QIODevice::WriteOnly)) {
    reportFile.write(QByteArray::fromStdString(json_report.dump(2)));
    reportFile.close();
  }

  emit exportCompleted(export_path, "");
  return true;
}

}  // namespace sns::app
