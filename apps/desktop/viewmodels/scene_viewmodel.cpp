#include "scene_viewmodel.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QtQuick3D/QQuick3DTextureData>
#include <algorithm>
#include <cmath>
#include "appearance_editor.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/ipc/protocol.hpp"

namespace sns::app {
namespace {
QVector3D vec(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

QQuick3DGeometry* geometry(const rendering::RenderPart& p, QObject* owner, bool uv) {
  auto* g = new QQuick3DGeometry;
  g->setParent(owner);
  auto vertices = p.vertices;
  if (uv) {
    for (std::size_t i = 0; i < vertices.size(); i += rendering::kFloatsPerVertex) {
      vertices[i] = (vertices[i + 6] - 0.5f) * 150;
      vertices[i + 1] = (0.5f - vertices[i + 7]) * 150;
      vertices[i + 2] = 0;
      vertices[i + 3] = 0; vertices[i + 4] = 0; vertices[i + 5] = 1;
    }
  }
  g->setVertexData(QByteArray(reinterpret_cast<const char*>(vertices.data()), static_cast<qsizetype>(vertices.size() * sizeof(float))));
  g->setIndexData(QByteArray(reinterpret_cast<const char*>(p.indices.data()), static_cast<qsizetype>(p.indices.size() * sizeof(std::uint32_t))));
  g->setStride(static_cast<int>(rendering::kFloatsPerVertex * sizeof(float)));
  g->setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
  using A = QQuick3DGeometry::Attribute;
  g->addAttribute(A::PositionSemantic, 0, A::F32Type);
  g->addAttribute(A::NormalSemantic, 12, A::F32Type);
  g->addAttribute(A::TexCoord0Semantic, 24, A::F32Type);
  g->addAttribute(A::TangentSemantic, 32, A::F32Type);
  g->addAttribute(A::BinormalSemantic, 44, A::F32Type);
  g->addAttribute(A::ColorSemantic, static_cast<int>(rendering::kColorOffset * sizeof(float)), A::F32Type);
  g->addAttribute(A::IndexSemantic, 0, A::U32Type);
  g->setBounds(uv ? QVector3D(-75,-75,0) : vec(p.min), uv ? QVector3D(75,75,0) : vec(p.max));
  g->update();
  return g;
}
QQuick3DTextureData* texture(const textures::DdsImage& img, QObject* owner) {
  auto* t = new QQuick3DTextureData;
  t->setParent(owner);
  t->setFormat(QQuick3DTextureData::RGBA8);
  t->setSize(QSize(static_cast<int>(img.width), static_cast<int>(img.height)));
  t->setHasTransparency(img.has_alpha);
  t->setTextureData(QByteArray(img.data.data(), static_cast<qsizetype>(img.data.size())));
  return t;
}

QString shader_name(std::uint32_t type) {
  static const char* names[] = {"Default", "EnvironmentMap", "Glow", "Parallax", "FaceTint", "SkinTint", "HairTint",
                                "ParallaxOcclusion", "MultitextureLandscape", "LODLandscape", "Snow", "MultiLayerParallax",
                                "TreeAnim", "LODObjects", "SparkleSnow", "LODObjectsHD", "EyeEnvmap", "Cloud"};
  return type < std::size(names) ? QString::fromLatin1(names[type]) : QString::number(type);
}
QString hex(std::uint32_t v, int width = 8) { return QStringLiteral("0x") + QString::number(v, 16).rightJustified(width, QLatin1Char('0')).toUpper(); }
QString vec3(const std::array<float, 3>& a) { return QStringLiteral("(%1, %2, %3)").arg(a[0], 0, 'f', 2).arg(a[1], 0, 'f', 2).arg(a[2], 0, 'f', 2); }
QString slots_of(std::uint32_t mask) {
  QStringList out;
  for (int bit = 0; bit < 32; ++bit) if (mask & (1u << bit)) out << QString::number(bit + 30);
  return out.join(", ");
}

// Everything the inspector shows for one part, as plain strings (QML only lays it out).
QVariantMap inspector(const rendering::RenderPart& p, const rendering::CharacterScene& scene) {
  static const char* slot_names[] = {"Diffuse", "Normal", "Glow / Skin", "Detail / Parallax", "Environment",
                                     "Env. mask", "Tint / Multilayer", "Specular / Backlight", "Slot 8"};
  QVariantList textures;
  auto add_texture = [&](const QString& role, const std::string& key) {
    if (key.empty()) return;
    QVariantMap t{{"role", role}, {"path", qs(key)}};
    if (const auto it = scene.texture_info.find(key); it != scene.texture_info.end()) {
      const auto& i = it->second;
      t["location"] = qs(i.location);
      if (!i.error.empty()) t["error"] = qs(i.error);
      else {
        t["size"] = QStringLiteral("%1x%2 (file %3x%4, mip %5)").arg(i.width).arg(i.height).arg(i.source_width).arg(i.source_height).arg(i.mip_level);
        t["format"] = qs(i.format);
        t["alpha"] = i.alpha.uses_alpha
            ? QStringLiteral("yes: min %1, mean %2, %3% translucent").arg(i.alpha.min).arg(i.alpha.mean, 0, 'f', 0).arg(100.0 * i.alpha.translucent_fraction, 0, 'f', 1)
            : QStringLiteral("no (opaque texels)");
      }
      if (!i.derived_from.empty()) t["derivedFrom"] = qs(i.derived_from);
    } else {
      t["location"] = QStringLiteral("not loaded by the preview");
    }
    textures.push_back(t);
  };
  for (std::size_t slot = 0; slot < p.texture_slots.size(); ++slot) add_texture(QString::fromLatin1(slot_names[slot]), p.texture_slots[slot]);
  if (p.diffuse != p.original_diffuse) add_texture(QStringLiteral("Effective diffuse"), p.diffuse);
  if (!p.face_tint.empty() && p.face_tint != p.texture_slots[nif::Tint]) add_texture(QStringLiteral("FaceGen tint"), p.face_tint);

  const auto& r = p.to_root.rotation;
  QStringList bones_mesh;
  for (std::size_t i = 0; i < std::min<std::size_t>(p.bones_from_mesh.size(), 6); ++i) bones_mesh << qs(p.bones_from_mesh[i]);
  QStringList warnings;
  for (const auto& w : p.warnings) warnings << qs(w);
  const auto tris = static_cast<qlonglong>(p.indices.size() / 3);
  return {
      {"shape", qs(p.shape_name)}, {"piece", qs(p.label)}, {"group", qs(p.group)},
      {"nif", qs(p.mesh_path)}, {"nifProvider", qs(p.mesh_location)}, {"node", p.parent_node.empty() ? QStringLiteral("(root)") : qs(p.parent_node)},
      {"armor", qs(p.armor_form_key)}, {"addon", qs(p.addon_form_key)}, {"slots", slots_of(p.slot_mask)},
      {"shader", p.shader_block.empty() ? QStringLiteral("none") : qs(p.shader_block) + " / " + shader_name(p.shader_type)},
      {"shaderFlags", p.shader_block.empty() ? QString() : hex(p.shader_flags1) + " / " + hex(p.shader_flags2)},
      {"materialAlpha", p.material_alpha},
      {"uvTransform", QStringLiteral("offset (%1, %2) scale (%3, %4)").arg(p.uv_offset.u).arg(p.uv_offset.v).arg(p.uv_scale.u).arg(p.uv_scale.v)},
      {"textures", textures},
      {"alphaProperty", p.alpha_flags == 0 ? QStringLiteral("none")
                        : QStringLiteral("%1 (blend %2, test %3, threshold %4)").arg(hex(p.alpha_flags, 4)).arg(p.alpha_blend ? "on" : "off")
                              .arg(p.alpha_test ? "on" : "off").arg(std::lround(p.alpha_cutoff * 255))},
      {"alphaMode", qs(std::string(rendering::to_string(p.alpha_mode)))}, {"alphaReason", qs(p.alpha_reason)},
      {"vertices", static_cast<qlonglong>(p.vertex_count)}, {"triangles", tris},
      {"bounds", vec3(p.min) + " - " + vec3(p.max)},
      {"transform", QStringLiteral("T %1  S %2  R [%3 %4 %5 | %6 %7 %8 | %9 %10 %11]")
                        .arg(vec3({p.to_root.translation.x, p.to_root.translation.y, p.to_root.translation.z})).arg(p.to_root.scale)
                        .arg(r[0][0], 0, 'f', 3).arg(r[0][1], 0, 'f', 3).arg(r[0][2], 0, 'f', 3).arg(r[1][0], 0, 'f', 3).arg(r[1][1], 0, 'f', 3)
                        .arg(r[1][2], 0, 'f', 3).arg(r[2][0], 0, 'f', 3).arg(r[2][1], 0, 'f', 3).arg(r[2][2], 0, 'f', 3)},
      {"skinning", !p.skinned ? QStringLiteral("not skinned")
                   : QStringLiteral("%1 bones, %2 (reference pose)").arg(p.bone_count).arg(p.skinning_applied ? "applied" : "incomplete")},
      {"bonesFromMesh", p.bones_from_mesh.empty() ? QString()
                        : QStringLiteral("%1 from the NIF itself: %2%3").arg(p.bones_from_mesh.size()).arg(bones_mesh.join(", ")).arg(p.bones_from_mesh.size() > 6 ? ", ..." : "")},
      {"bonesMissing", static_cast<qlonglong>(p.bones_missing.size())},
      {"vertexColors", p.vertex_colors}, {"tangentsSwapped", p.tangents_swapped}, {"modelSpaceNormals", p.model_space_normals},
      {"weightBlended", p.weight_blended}, {"physicsXml", qs(p.physics_xml)},
      {"hiddenReason", qs(p.hidden_reason)}, {"warnings", warnings}};
}
}

SceneViewModel::SceneViewModel(WorkerClient* worker, OutfitEditor* outfit, AppearanceEditor* appearance, std::function<SnapshotPtr()> snapshot, QObject* parent)
    : QObject(parent), worker_(worker), outfit_(outfit), appearance_(appearance), snapshot_(std::move(snapshot)) {
  pool_.setMaxThreadCount(1);
  debounce_.setSingleShot(true);
  debounce_.setInterval(200);
  connect(&debounce_, &QTimer::timeout, this, &SceneViewModel::reload);
  connect(outfit_, &OutfitEditor::changed, this, [this] { cancel(); debounce_.start(); });
  if (appearance_) {
    connect(appearance_, &AppearanceEditor::appearanceChanged, this, [this] { cancel(); debounce_.start(); });
  }
}
SceneViewModel::~SceneViewModel() { cancel(); pool_.waitForDone(); }
void SceneViewModel::cancel() {
  ++serial_;
  if (cancel_) *cancel_ = true;
  if (request_cancel_) *request_cancel_ = true;
  debounce_.stop();
  loading_ = false;
  if (!status_.isEmpty()) status_ = tr("Carga cancelada");
  emit loadingChanged();
}
void SceneViewModel::clear() {
  cancel(); parts_.clear(); issues_.clear(); stats_.clear(); visibility_.clear(); skeleton_ = nullptr; selected_.clear();
  status_.clear();
  emit changed(); emit visibilityChanged(); emit selectionChanged();
  if (resources_) { resources_->deleteLater(); resources_ = nullptr; }
}
void SceneViewModel::setOriginal(bool value) {
  if (original_ == value) return;
  original_ = value; emit originalChanged(); reload();
}
void SceneViewModel::reload() {
  cancel();
  const auto snapshot = snapshot_();
  if (!snapshot || outfit_->npc_form_key().isEmpty()) return;
  const int serial = serial_;
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  loading_ = true; status_ = tr("Cargando recursos 3D..."); emit loadingChanged();
  Json params{{"npcFormKey", ss(outfit_->npc_form_key())}};
  if (preview_armor_) {
    params["armorFormKeys"] = Json::array();
    for (const auto& key : *preview_armor_) params["armorFormKeys"].push_back(ss(key));
  } else if (!original_) {
    params["armorFormKeys"] = Json::array();
    for (const auto& p : outfit_->pieces()) params["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
  }
  if (!original_ && appearance_ && appearance_->has_face_edits()) {
    auto donor_json = appearance_->build_donor_override_json();
    if (!donor_json.is_null()) {
      params["donorOverride"] = std::move(donor_json);
    }
  }
  request_cancel_ = worker_->call(ipc::protocol::method::kGetAppearance, params, std::chrono::seconds(30), this,
    [this, snapshot, serial, cancel = cancel_](const Json& data) {
      if (serial != serial_) return;
      QPointer<SceneViewModel> self(this);
      pool_.start([self, snapshot, data, serial, cancel] {
        rendering::CharacterScene scene;
        QString error;
        QElapsedTimer timer; timer.start();
        try {
          const auto appearance = rendering::appearance_from_json(data);
          const auto cancelled = [cancel] { return cancel->load(); };
          assets::AssetResolver resolver(snapshot->vfs, assets::archive_load_order(snapshot->profile, snapshot->load_order, snapshot->vfs), cancelled);
          rendering::SceneOptions options; options.apply_skinning = true; options.cancelled = cancelled;
          scene = rendering::build_character_scene(appearance, resolver, options);
        } catch (const std::exception& e) { error = qs(e.what()); }
        if (cancel->load()) return;
        const auto elapsed = timer.elapsed();
        QMetaObject::invokeMethod(self, [self, scene = std::move(scene), error, serial, elapsed] {
          if (!self || serial != self->serial_) return;
          self->loading_ = false;
          if (error.isEmpty()) {
            self->install(scene);
            self->stats_["elapsedMs"] = elapsed;
            self->status_ = tr("%1 mallas, %2 geometrías, %3 texturas (%4 ms)").arg(scene.stats.meshes_loaded).arg(scene.stats.shapes).arg(scene.stats.textures_loaded).arg(elapsed);
          } else {
            self->clear();
            self->issues_ = {QVariantMap{{"severity", "error"}, {"code", "SceneFailed"}, {"message", error}}};
            self->status_ = error;
            emit self->changed();
          }
          emit self->loadingChanged(); emit self->ready();
        }, Qt::QueuedConnection);
      });
    }, [this, serial](int, const QString& error, const Json&) {
      if (serial != serial_) return;
      clear(); status_ = error;
      issues_ = {QVariantMap{{"severity", "error"}, {"code", "AppearanceUnavailable"}, {"message", error}}};
      emit changed(); emit loadingChanged(); emit ready();
    });
}

void SceneViewModel::install(const rendering::CharacterScene& scene) {
  auto* old = resources_;
  resources_ = new QObject(this);
  parts_.clear(); issues_.clear(); selected_.clear(); visibility_.clear();
  QHash<QString, QQuick3DTextureData*> textures;
  for (const auto& [path, image] : scene.textures) textures[qs(path)] = texture(*image, resources_);
  for (const auto& part : scene.parts) {
    QVariantMap row{{"id", qs(part.id)}, {"label", qs(part.label + " / " + part.shape_name)}, {"shape", qs(part.shape_name)},
      {"group", qs(part.group)}, {"path", qs(part.mesh_path)}, {"armorFormKey", qs(part.armor_form_key)},
      {"shownByDefault", part.shown_by_default}, {"hiddenReason", qs(part.hidden_reason)},
      {"geometry", QVariant::fromValue(geometry(part, resources_, false))},
      {"uvGeometry", QVariant::fromValue(geometry(part, resources_, true))},
      {"diffuse", QVariant::fromValue(textures.value(qs(part.diffuse)))},
      {"normal", QVariant::fromValue(textures.value(qs(part.normal)))},
      {"alphaMode", qs(std::string(rendering::to_string(part.alpha_mode)))}, {"alphaCutoff", part.alpha_cutoff},
      {"materialAlpha", part.material_alpha}, {"vertexColors", part.vertex_colors}, {"hair", part.shader_type == nif::kShaderHairTint},
      {"doubleSided", part.double_sided}, {"center", (vec(part.min) + vec(part.max)) * 0.5f * scene.scale},
      {"radius", std::max(1.0f, (vec(part.max) - vec(part.min)).length() * 0.5f * scene.scale)},
      {"scale", scene.scale}, {"tint", QColor(Qt::white)}, {"warningCount", static_cast<int>(part.warnings.size())},
      {"inspector", inspector(part, scene)}};
    if (part.tint) row["tint"] = QColor::fromRgbF(part.tint->r, part.tint->g, part.tint->b);
    visibility_[qs(part.id)] = part.shown_by_default;
    parts_.push_back(row);
  }
  skeleton_ = new QQuick3DGeometry; skeleton_->setParent(resources_);
  skeleton_->setVertexData(QByteArray(reinterpret_cast<const char*>(scene.skeleton_lines.data()), static_cast<qsizetype>(scene.skeleton_lines.size() * sizeof(float))));
  skeleton_->setStride(12); skeleton_->setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
  skeleton_->addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
  skeleton_->setBounds(vec(scene.min), vec(scene.max)); skeleton_->update();
  for (const auto& issue : scene.issues) issues_.push_back(QVariantMap{{"severity", qs(issue.severity)}, {"code", qs(issue.code)}, {"message", qs(issue.message)}, {"subject", qs(issue.subject)}});
  center_ = (vec(scene.min) + vec(scene.max)) * 0.5f * scene.scale;
  radius_ = std::max(1.0f, (vec(scene.max) - vec(scene.min)).length() * 0.5f * scene.scale);
  stats_ = {{"shapes", scene.stats.shapes}, {"triangles", static_cast<qlonglong>(scene.stats.triangles)},
    {"textures", scene.stats.textures_loaded}, {"missingTextures", scene.stats.textures_missing},
    {"skinnedShapes", scene.stats.skinned_shapes}, {"bones", static_cast<qlonglong>(scene.skeleton_bones)}, {"missingBones", scene.stats.bones_missing},
    {"hiddenShapes", scene.stats.hidden_shapes}, {"texturesReduced", scene.stats.textures_reduced}, {"piecesUnequipped", scene.stats.pieces_unequipped}};
  emit changed(); emit visibilityChanged(); emit selectionChanged();
  if (old) old->deleteLater();
}
void SceneViewModel::select(const QString& id) { selected_ = id; emit selectionChanged(); }
void SceneViewModel::setVisible(const QString& id, bool visible) {
  if (!visibility_.contains(id) || visibility_.value(id).toBool() == visible) return;
  visibility_[id] = visible;
  emit visibilityChanged();
}
void SceneViewModel::restoreVisibility() {
  for (const auto& p : parts_) { const auto row = p.toMap(); visibility_[row.value("id").toString()] = row.value("shownByDefault", true); }
  emit visibilityChanged();
}
void SceneViewModel::setPreviewArmor(const QVariant& formKeys) {
  if (formKeys.isNull() || !formKeys.isValid()) preview_armor_.reset();
  else preview_armor_ = formKeys.toStringList();
  reload();
}
QVariantMap SceneViewModel::selectedPart() const {
  for (const auto& p : parts_) { auto row = p.toMap(); if (row.value("id").toString() == selected_) return row.value("inspector").toMap(); }
  return {};
}
QVariantMap SceneViewModel::selectedBounds() const {
  for (const auto& p : parts_) { auto row = p.toMap(); if (row.value("id").toString() == selected_) return row; }
  return {};
}
} // namespace sns::app
