#include "scene_viewmodel.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QtQuick3D/QQuick3DTextureData>
#include <algorithm>
#include <cmath>
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
}

SceneViewModel::SceneViewModel(WorkerClient* worker, OutfitEditor* outfit, std::function<SnapshotPtr()> snapshot, QObject* parent)
    : QObject(parent), worker_(worker), outfit_(outfit), snapshot_(std::move(snapshot)) {
  pool_.setMaxThreadCount(1);
  debounce_.setSingleShot(true);
  debounce_.setInterval(200);
  connect(&debounce_, &QTimer::timeout, this, &SceneViewModel::reload);
  connect(outfit_, &OutfitEditor::changed, this, [this] { cancel(); debounce_.start(); });
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
  cancel(); parts_.clear(); issues_.clear(); stats_.clear(); skeleton_ = nullptr; selected_.clear();
  status_.clear();
  emit changed(); emit selectionChanged();
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
  if (!original_) {
    params["armorFormKeys"] = Json::array();
    for (const auto& p : outfit_->pieces()) params["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
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
  parts_.clear(); issues_.clear(); selected_.clear();
  QHash<QString, QQuick3DTextureData*> textures;
  for (const auto& [path, image] : scene.textures) textures[qs(path)] = texture(*image, resources_);
  for (const auto& part : scene.parts) {
    QVariantMap row{{"id", qs(part.id)}, {"label", qs(part.label + " / " + part.shape_name)},
      {"group", qs(part.group)}, {"path", qs(part.mesh_path)}, {"armorFormKey", qs(part.armor_form_key)}, {"visible", true},
      {"geometry", QVariant::fromValue(geometry(part, resources_, false))},
      {"uvGeometry", QVariant::fromValue(geometry(part, resources_, true))},
      {"diffuse", QVariant::fromValue(textures.value(qs(part.diffuse)))},
      {"normal", QVariant::fromValue(textures.value(qs(part.normal)))},
      {"alphaBlend", part.alpha_blend}, {"alphaTest", part.alpha_test}, {"alphaCutoff", part.alpha_cutoff},
      {"doubleSided", part.double_sided}, {"center", (vec(part.min) + vec(part.max)) * 0.5f * scene.scale},
      {"radius", std::max(1.0f, (vec(part.max) - vec(part.min)).length() * 0.5f * scene.scale)},
      {"scale", scene.scale}, {"tint", QColor(Qt::white)}};
    if (part.tint) row["tint"] = QColor::fromRgbF(part.tint->r, part.tint->g, part.tint->b);
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
    {"skinnedShapes", scene.stats.skinned_shapes}, {"bones", static_cast<qlonglong>(scene.skeleton_bones)}, {"missingBones", scene.stats.bones_missing}};
  emit changed(); emit selectionChanged();
  if (old) old->deleteLater();
}
void SceneViewModel::select(const QString& id) { selected_ = id; emit selectionChanged(); }
void SceneViewModel::setVisible(const QString& id, bool visible) {
  for (auto& p : parts_) { auto row = p.toMap(); if (row.value("id").toString() == id) { row["visible"] = visible; p = row; } }
  emit changed();
}
QVariantMap SceneViewModel::selectedBounds() const {
  for (const auto& p : parts_) { auto row = p.toMap(); if (row.value("id").toString() == selected_) return row; }
  return {};
}
} // namespace sns::app
