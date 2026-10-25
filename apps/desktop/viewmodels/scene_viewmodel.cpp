#include "scene_viewmodel.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QtQuick3D/QQuick3DTextureData>
#include <algorithm>
#include <cmath>
#include <QSettings>
#include "appearance_editor.hpp"
#include "body_director.hpp"
#include "diagnostics/crash_diagnostics.hpp"
#include "resource_context.hpp"
#include "sns/appearance/skin_match.hpp"
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
// A pooled object is detached from the scene while Repeater3D swaps delegates; Qt then drops its
// backend node and builds a new, empty one when it is attached again, because the "data changed"
// flags were consumed by the first upload. Re-setting the (implicitly shared) buffers flags them
// again. Without this, reused heads were invisible and reused skin textures white after a rebuild.
void refresh(QQuick3DGeometry* g) {
  g->setVertexData(g->vertexData());
  g->setIndexData(g->indexData());
  g->update();
}
void refresh(QQuick3DTextureData* t) { t->setTextureData(t->textureData()); }

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

// The skin match of what is on screen: the head against the body, hands and feet that the viewer draws (an estimate; see skin_match.hpp).
// A body covered by the outfit is not drawn, so it is not measured: the result says so (BodyNotMeasured) instead of guessing.
QVariantMap skin_match_of(const rendering::CharacterScene& scene) {
  std::vector<appearance::SkinSample> samples;
  for (const auto& part : scene.parts) {
    if (!part.has_skin_median) continue;
    samples.push_back({part.group == "head" ? std::string("head") : appearance::skin_role_of_slots(part.slot_mask),
                       part.label + " / " + part.shape_name, part.skin_median});
  }
  const auto match = appearance::evaluate_skin_match(samples);
  const auto hex = [](const std::array<float, 3>& c) { return QColor::fromRgbF(c[0], c[1], c[2]).name(); };
  QStringList codes, caveats;
  for (const auto& c : match.codes) codes << qs(c);
  for (const auto& c : match.caveats) caveats << qs(c);
  QVariantMap head;
  QVariantList pairs;
  std::size_t next_pair = 0;
  for (const auto& s : samples) {
    if (s.role == "head") {
      if (head.isEmpty()) head = {{"label", qs(s.label)}, {"color", hex(s.rgb)}};
    } else if (s.role == "body" || s.role == "hands" || s.role == "feet") {
      if (next_pair < match.pairs.size()) {
        const auto& d = match.pairs[next_pair++];
        pairs.push_back(QVariantMap{{"role", qs(d.role)}, {"label", qs(d.label)}, {"color", hex(s.rgb)}, {"deltaE", d.delta_e}});
      }
    }
  }
  return {{"state", "ready"}, {"verdict", qs(std::string(appearance::to_string(match.verdict)))}, {"message", qs(match.message)},
          {"worst", match.worst}, {"worstRole", qs(match.worst_role)}, {"codes", codes}, {"caveats", caveats},
          {"head", head}, {"pairs", pairs}, {"consistentMax", appearance::kConsistentMax}, {"noticeableMax", appearance::kNoticeableMax}};
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
  QStringList collisions;
  for (const auto& name : p.collision_shapes) collisions << qs(name);
  QStringList physics_bones;
  for (const auto& name : p.physics_bones) physics_bones << qs(name);
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
      {"physicsXmlProvider", qs(p.physics_xml_location)},
      {"physicsDetected", !p.physics_xml.empty()},
      {"physicsXmlFound", !p.physics_xml.empty() && p.physics_xml_location != "missing"},
      {"physicsXmlWellFormed", p.physics_xml_well_formed},
      {"physicsXmlError", qs(p.physics_xml_error)},
      {"physicsBones", physics_bones},
      {"collisionShapes", collisions},
      {"staticCompatible", !p.mesh_path.empty() && p.bones_missing.empty() &&
           (p.physics_xml.empty() || p.physics_xml_well_formed)},
      {"hiddenReason", qs(p.hidden_reason)}, {"warnings", warnings}};
}
// "0A2C8E:Skyrim.esm" and "000A2C8E:Skyrim.esm" name the same record.
bool same_form_key(const QString& a, const QString& b) {
  const auto ia = a.indexOf(':'), ib = b.indexOf(':');
  if (ia < 0 || ib < 0) return a.compare(b, Qt::CaseInsensitive) == 0;
  bool oka = false, okb = false;
  const auto ida = a.left(ia).toUInt(&oka, 16), idb = b.left(ib).toUInt(&okb, 16);
  return oka && okb && ida == idb && a.mid(ia + 1).compare(b.mid(ib + 1), Qt::CaseInsensitive) == 0;
}

QColor rgb_color(const face::Rgb8& c) { return QColor(c.r, c.g, c.b); }

// Scene inputs other than the hair color: equal inputs give an identical scene.
bool same_scene_inputs(const FacePreview& a, const FacePreview& b) {
  return a.npc_form_key == b.npc_form_key && a.geometry_revision == b.geometry_revision &&
         (a.overlay == nullptr) == (b.overlay == nullptr) && a.weight == b.weight && a.height == b.height &&
         a.skin_tone == b.skin_tone && a.assigned_body_preset == b.assigned_body_preset;
}

QString content_key(const rendering::RenderPart& part) {
  return qs(part.mesh_path + "|" + part.shape_name + "|") +
         QString::number(qHashBits(part.vertices.data(), part.vertices.size() * sizeof(float)), 16) + "|" +
         QString::number(qHashBits(part.indices.data(), part.indices.size() * sizeof(std::uint32_t)), 16);
}

int setting_ms(const char* key, int fallback) {
  bool ok = false;
  const int value = QSettings().value(QString::fromLatin1(key), fallback).toInt(&ok);
  return ok ? std::clamp(value, 0, 5000) : fallback;
}
}  // namespace

// A built scene plus what install() needs from it, all computed on the build thread so the GUI
// thread only creates or reuses Quick 3D objects.
struct SceneViewModel::Prepared {
  std::uint64_t task = 0;
  rendering::CharacterScene scene;
  std::vector<QString> geometry_keys;  // per part, content hash of vertices + indices
  std::vector<std::pair<QString, std::shared_ptr<const textures::DdsImage>>> textures;  // pool key, image
  std::vector<QString> texture_paths;
  QString error;
  qint64 elapsed = 0;
  FacePreview preview;
  QString npc;
  QString assigned_body_name;
  QColor record_hair;
};

SceneViewModel::SceneViewModel(WorkerClient* worker, OutfitEditor* outfit, AppearanceEditor* appearance, std::function<SnapshotPtr()> snapshot,
                               QObject* parent, bool armor_preview)
    : QObject(parent), worker_(worker), outfit_(outfit), appearance_(appearance), snapshot_(std::move(snapshot)),
      armor_preview_(armor_preview) {
  pool_.setMaxThreadCount(1);
  debounce_.setSingleShot(true);
  // Pause after the last outfit change before the scene is rebuilt (preview/sceneReloadDebounceMs).
  reload_delay_ms_ = setting_ms("preview/sceneReloadDebounceMs", 200);
  debounce_.setInterval(reload_delay_ms_);
  connect(&debounce_, &QTimer::timeout, this, &SceneViewModel::reload);
  // Outfit edits need the worker (ARMA/ARMO resolution); face edits arrive as regenerated FaceGen.
  connect(outfit_, &OutfitEditor::changed, this, [this] {
    if (npc_override_.isEmpty()) schedule(reload_delay_ms_);
  });
  if (appearance_) {
    connect(appearance_, &AppearanceEditor::faceGenChanged, this, &SceneViewModel::on_face_changed);
    // The hair color shows at once; the FaceGen patch that matches it follows in the background.
    connect(appearance_, &AppearanceEditor::appearanceChanged, this, &SceneViewModel::sync_hair_tint);
  }
}

SceneViewModel::~SceneViewModel() {
  cancel();
  pool_.waitForDone();
}

void SceneViewModel::schedule(int delay_ms) {
  if (!active_) {
    stale_ = true;
    return;
  }
  cancel();
  debounce_.setInterval(delay_ms);
  debounce_.start();
}

void SceneViewModel::cancel() {
  ++serial_;
  if (cancel_) *cancel_ = true;
  if (request_cancel_) *request_cancel_ = true;
  debounce_.stop();
  if (!loading_) return;
  loading_ = false;
  status_ = tr("Carga cancelada");
  emit loadingChanged();
}

void SceneViewModel::clear() {
  cancel(); parts_.clear(); issues_.clear(); stats_.clear(); skin_match_.clear(); visibility_.clear(); skeleton_ = nullptr; selected_.clear(); hair_vertex_tint_.clear();
  outfit_diagnostics_.clear(); covered_body_diagnostics_.clear();
  status_.clear();
  last_appearance_ = nullptr;
  last_request_key_.clear();
  installed_ = false;
  installed_npc_.clear();
  installed_preview_ = {};
  emit changed(); emit visibilityChanged(); emit selectionChanged();
  // Deferred: QML drops its references to these objects when it handles changed() above.
  if (resources_) { resources_->deleteLater(); resources_ = nullptr; }
  if (pool_owner_) { pool_owner_->deleteLater(); pool_owner_ = nullptr; }
  geometry_pool_.clear(); uv_pool_.clear(); texture_pool_.clear();
}

void SceneViewModel::setOriginal(bool value) {
  if (original_ == value) return;
  original_ = value; emit originalChanged();
  sync_hair_tint();
  reload();
}

void SceneViewModel::setPose(const QString& pose) {
  if (pose_ == pose) return;
  pose_ = pose;
  emit poseChanged();
  reload();
}

void SceneViewModel::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  emit activeChanged();
  if (active_ && stale_) {
    stale_ = false;
    reload();
  }
}

QString SceneViewModel::effective_npc() const {
  if (!npc_override_.isEmpty()) return npc_override_;
  QString npc = outfit_->npc_form_key();
  if (npc.isEmpty() && armor_preview_) npc = QStringLiteral("000A2C8E:Skyrim.esm");
  return npc;
}

void SceneViewModel::set_body_director(BodyDirector* body) {
  if (body_ == body) return;
  body_ = body;
  if (body_) {
    connect(body_, &BodyDirector::bodyAssigned, this, [this](const QString& form_key, const QString&) {
      if (same_form_key(form_key, effective_npc())) {
        schedule(30);
      }
    });
    connect(body_, &BodyDirector::draftChanged, this, [this] {
      schedule(30);
    });
  }
}

FacePreview SceneViewModel::current_preview() const {
  if (original_ || !appearance_) return {};
  auto p = appearance_->preview();
  if (p.npc_form_key.empty() || !same_form_key(qs(p.npc_form_key), effective_npc())) return {};
  if (body_) {
    const auto assigned = body_->assigned_preset_for(effective_npc());
    if (assigned) p.assigned_body_preset = assigned->uid.toStdString();
  }
  return p;
}

// Face draft changed (FaceGen rebuilt, patched or reset): rebuild the scene only when something it
// draws changed; a hair color patch just recolors the materials.
void SceneViewModel::on_face_changed() {
  if (!appearance_ || appearance_->building()) return;
  const auto p = current_preview();
  if (installed_ && same_form_key(installed_npc_, effective_npc()) && same_scene_inputs(p, installed_preview_)) {
    sync_hair_tint();
    return;
  }
  schedule(30);
}

void SceneViewModel::sync_hair_tint() {
  QColor color = record_hair_.isValid() ? record_hair_ : QColor(Qt::white);
  if (installed_ && same_form_key(installed_npc_, effective_npc())) {
    const auto p = current_preview();
    if (!p.npc_form_key.empty()) color = p.hair_color ? rgb_color(*p.hair_color) : QColor(Qt::white);
  }
  if (color == hair_tint_) return;
  diag::record("scene", QStringLiteral("hair tint %1 -> %2 (no rebuild)").arg(hair_tint_.name(), color.name()).toStdString());
  hair_tint_ = color;
  apply_hair_vertex_tint();
  emit hairTintChanged();
}

void SceneViewModel::apply_hair_vertex_tint() {
  // Qt treats material colors as sRGB but uses vertex colors as they are: the tint is linearized so a
  // fully tinted vertex (green = 1) looks exactly as the white-vertex parts tinted by baseColor.
  auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
  const float tint[3] = {linear(hair_tint_.redF()), linear(hair_tint_.greenF()), linear(hair_tint_.blueF())};
  for (auto& entry : hair_vertex_tint_) {
    if (!entry.geometry) continue;
    auto vertices = entry.vertices;
    for (std::size_t v = 0; v + rendering::kFloatsPerVertex <= vertices.size(); v += rendering::kFloatsPerVertex) {
      const float g = vertices[v + rendering::kColorOffset + 1];
      for (int c = 0; c < 3; ++c) vertices[v + rendering::kColorOffset + c] = 1.0f + g * (tint[c] - 1.0f);
    }
    entry.geometry->setVertexData(QByteArray(reinterpret_cast<const char*>(vertices.data()), static_cast<qsizetype>(vertices.size() * sizeof(float))));
    entry.geometry->update();
  }
}

void SceneViewModel::reload() {
  cancel();
  if (!active_) {
    stale_ = true;
    return;
  }
  stale_ = false;
  const auto snapshot = snapshot_();
  const QString effective = effective_npc();
  const bool has_armor_selection = !selected_armor_.isEmpty() || !selected_mesh_.isEmpty();
  if (!snapshot || effective.isEmpty() || (armor_preview_ && !has_armor_selection)) {
    if (armor_preview_) {
      status_ = !has_armor_selection ? tr("Selecciona una prenda o malla NIF del catálogo para inspeccionarla")
                                     : tr("Selecciona un NPC como referencia de cuerpo, raza y esqueleto");
      emit loadingChanged();
    }
    return;
  }
  const int serial = serial_;
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  loading_ = true; status_ = tr("Cargando recursos 3D..."); emit loadingChanged();
  Json params{{"npcFormKey", ss(effective)}};
  const QString selected_mesh = armor_preview_ ? selected_mesh_ : QString();
  const bool is_mesh = !selected_mesh.isEmpty();

  if (armor_preview_ && is_mesh) {
    params["armorFormKeys"] = Json::array();
    if (preview_mode_ == "npc" || preview_mode_ == "outfit") {
      for (const auto& p : outfit_->pieces()) params["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
    }
  } else if (armor_preview_ && preview_mode_ == "outfit") {
    params["armorFormKeys"] = Json::array();
    for (const auto& p : outfit_->pieces()) params["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
  } else if (armor_preview_ && preview_mode_ == "npc") {
    params["armorFormKeys"] = Json::array();
    params["armorFormKeys"].push_back(ss(selected_armor_));
    for (const auto& p : outfit_->pieces()) {
      const auto fk = ss(p.toMap().value("formKey").toString());
      if (fk != ss(selected_armor_)) params["armorFormKeys"].push_back(fk);
    }
  } else if (armor_preview_) {
    params["armorFormKeys"] = Json::array({ss(selected_armor_)});
  } else if (preview_armor_) {
    params["armorFormKeys"] = Json::array();
    for (const auto& key : *preview_armor_) params["armorFormKeys"].push_back(ss(key));
  } else if (!original_) {
    params["armorFormKeys"] = Json::array();
    for (const auto& p : outfit_->pieces()) params["armorFormKeys"].push_back(ss(p.toMap().value("formKey").toString()));
  }
  // "Original" shows the profile as it is: no face overlay, record weight/height/colors. The face
  // draft only applies to the NPC it belongs to.
  const FacePreview preview = current_preview();
  const QString display_mode = armor_preview_ ? preview_mode_ : QString();
  const QString current_pose = pose_;
  const auto task = diag::next_task_id();
  std::optional<BodyDirector::AssignedPresetInfo> assigned_preset;
  if (body_ && !original_) {
    assigned_preset = body_->assigned_preset_for(effective);
  }
  diag::record("scene", QStringLiteral("reload #%1 npc=%2 %3 face=%4 geometryRevision=%5 body=%6")
                            .arg(task).arg(effective, armor_preview_ ? "armor-preview" : "main",
                                           preview.npc_form_key.empty() ? "record" : "draft")
                            .arg(preview.geometry_revision)
                            .arg(assigned_preset ? assigned_preset->name : QStringLiteral("none")).toStdString());
  auto build = [this, snapshot, serial, preview, display_mode, current_pose, task, effective, selected_mesh, assigned_preset, cancel = cancel_, own_cache = own_cache_, portrait = portrait_mode_, portrait_overlay = portrait_overlay_](const Json& data) {
    QPointer<SceneViewModel> self(this);
    pool_.start([self, snapshot, data, serial, cancel, preview, display_mode, current_pose, task, effective, selected_mesh, assigned_preset, own_cache, portrait, portrait_overlay] {
      auto prepared = std::make_shared<Prepared>();
      prepared->task = task;
      prepared->preview = preview;
      prepared->npc = effective;
      auto& scene = prepared->scene;
      QElapsedTimer timer; timer.start();
      diag::Operation op("scene.build", task, ss(effective));
      try {
        op.stage("appearance");
        auto appearance = rendering::appearance_from_json(data);
        const auto cancelled = [cancel] { return cancel->load(); };
        const auto context = ResourceContext::for_snapshot(snapshot);
        if (!selected_mesh.isEmpty()) {
          if (appearance.skeleton.empty() &&
              context->resolver->locate("meshes\\actors\\character\\character assets female\\skeleton_female.nif").has_value()) {
            appearance.skeleton = "meshes\\actors\\character\\character assets female\\skeleton_female.nif";
          }
          rendering::AppearancePiece piece;
          piece.armor_form_key = "";
          piece.name = selected_mesh.toStdString();
          piece.source = "outfit";
          piece.slot_mask = 0;

          rendering::AddonModel addon;
          addon.form_key = "";
          QString norm = selected_mesh;
          norm.replace('/', '\\');
          addon.mesh = norm.toStdString();
          addon.weight_slider = norm.endsWith("_1.nif", Qt::CaseInsensitive) || norm.endsWith("_0.nif", Qt::CaseInsensitive);
          addon.slot_mask = 0;
          piece.addons.push_back(std::move(addon));

          appearance.pieces.push_back(std::move(piece));
        }
        if (appearance.hair_color) {
          prepared->record_hair = QColor::fromRgbF(appearance.hair_color->r, appearance.hair_color->g, appearance.hair_color->b);
        }
        if (!preview.npc_form_key.empty()) {
          appearance.weight = preview.weight;
          appearance.height = preview.height;
          if (preview.hair_color) appearance.hair_color = rendering::Rgb{preview.hair_color->r / 255.0f, preview.hair_color->g / 255.0f, preview.hair_color->b / 255.0f};
          else appearance.hair_color.reset();
          appearance.skin_tone = rendering::Rgb{preview.skin_tone.r / 255.0f, preview.skin_tone.g / 255.0f, preview.skin_tone.b / 255.0f};
        }
        rendering::SceneOptions options;
        options.apply_skinning = true;
        options.pose_name = current_pose.toStdString();
        options.cancelled = cancelled;
        options.cache = own_cache ? own_cache : context->scene_cache;
        if (portrait) {
          options.head_only = true;
          options.diffuse_max_dimension = 512;
          options.normal_max_dimension = 512;
        }
        options.overlay = preview.overlay ? preview.overlay : portrait_overlay;
        if (assigned_preset) {
          rendering::BodyPresetOption bpo;
          bpo.name = assigned_preset->name.toStdString();
          bpo.family = assigned_preset->family.toStdString();
          for (const auto& s : assigned_preset->sliders) {
            bpo.sliders.push_back({s.name, s.small, s.big});
          }
          options.body_preset = std::move(bpo);
          prepared->assigned_body_name = assigned_preset->name;
        }
        op.stage("build_character_scene");
        scene = rendering::build_character_scene(appearance, *context->resolver, options);
        if (display_mode == "piece" || display_mode == "mannequin") {
          std::erase_if(scene.parts, [&](const rendering::RenderPart& part) {
            return display_mode == "piece" ? part.group != "outfit" : part.group == "head";
          });
          if (display_mode == "piece") scene.skeleton_lines.clear();
          bool first = true;
          for (const auto& part : scene.parts) {
            if (!part.shown_by_default) continue;
            for (int i = 0; i < 3; ++i) {
              if (first) { scene.min[i] = part.min[i]; scene.max[i] = part.max[i]; }
              else { scene.min[i] = std::min(scene.min[i], part.min[i]); scene.max[i] = std::max(scene.max[i], part.max[i]); }
            }
            first = false;
          }
        }
        // Content keys for GPU object reuse, here rather than on the GUI thread.
        op.stage("content keys");
        prepared->geometry_keys.reserve(scene.parts.size());
        for (const auto& part : scene.parts) prepared->geometry_keys.push_back(content_key(part));
        for (const auto& [path, image] : scene.textures) {
          prepared->texture_paths.push_back(qs(path));
          prepared->textures.emplace_back(qs(path) + QStringLiteral("|%1x%2|").arg(image->width).arg(image->height) +
                                              QString::number(reinterpret_cast<quintptr>(image.get()), 16),
                                          image);
        }
        op.succeed();
      } catch (const std::exception& e) {
        prepared->error = qs(e.what());
        if (!cancel->load()) op.fail(e.what());
      }
      if (cancel->load()) return;
      prepared->elapsed = timer.elapsed();
      QMetaObject::invokeMethod(self, [self, prepared, serial, selected_mesh] {
        if (!self || serial != self->serial_) return;
        self->loading_ = false;
        if (prepared->error.isEmpty()) {
          self->install(*prepared);
          self->stats_["elapsedMs"] = prepared->elapsed;
          self->stats_["generatedFaceGen"] = prepared->preview.overlay != nullptr;
          const auto& s = prepared->scene.stats;
          if (!selected_mesh.isEmpty() && s.meshes_loaded == 0) {
            QString diag_msg = tr("No se pudo cargar la malla NIF: %1").arg(selected_mesh);
            for (const auto& iss : prepared->scene.issues) {
              if (iss.code == "MeshUnreadable" || iss.code == "MeshNotFound") {
                diag_msg = qs(iss.message);
                break;
              }
            }
            self->status_ = diag_msg;
          } else {
            self->status_ = tr("%1 mallas, %2 geometrías, %3 texturas (%4 ms)%5%6").arg(s.meshes_loaded).arg(s.shapes)
                                .arg(s.textures_loaded).arg(prepared->elapsed)
                                .arg(prepared->preview.overlay ? tr(" · cabeza regenerada del borrador") : QString())
                                .arg(!prepared->assigned_body_name.isEmpty() ? tr(" · cuerpo: %1").arg(prepared->assigned_body_name) : QString());
          }
        } else {
          self->clear();
          self->issues_ = {QVariantMap{{"severity", "error"}, {"code", "SceneFailed"}, {"message", prepared->error}}};
          self->status_ = prepared->error;
          emit self->changed();
        }
        emit self->loadingChanged(); emit self->ready();
      }, Qt::QueuedConnection);
    });
  };
  if (armor_preview_ && is_mesh && preview_mode_ == "piece") {
    build(Json::object({{"npcFormKey", ss(effective)}, {"sex", "female"}, {"weight", 50.0f}, {"height", 1.0f}, {"raceHeight", 1.0f}}));
    return;
  }
  const auto key = qs(params.dump()) + (assigned_preset ? QStringLiteral("|body:") + assigned_preset->uid : QString());
  if (key == last_request_key_ && !last_appearance_.is_null()) {
    build(last_appearance_);
    return;
  }
  request_cancel_ = worker_->call(ipc::protocol::method::kGetAppearance, params, std::chrono::seconds(30), this,
    [this, serial, key, build](const Json& data) {
      if (serial != serial_) return;
      last_request_key_ = key;
      last_appearance_ = data;
      build(data);
    }, [this, serial, is_mesh, build, effective](int, const QString& error, const Json&) {
      if (serial != serial_) return;
      if (is_mesh) {
        build(Json::object({{"npcFormKey", ss(effective)}, {"sex", "female"}, {"weight", 50.0f}, {"height", 1.0f}, {"raceHeight", 1.0f}}));
        return;
      }
      clear(); status_ = error;
      issues_ = {QVariantMap{{"severity", "error"}, {"code", "AppearanceUnavailable"}, {"message", error}}};
      emit changed(); emit loadingChanged(); emit ready();
    });
}

void SceneViewModel::install(Prepared& prepared) {
  const auto& scene = prepared.scene;
  diag::Operation op("scene.install", prepared.task, ss(prepared.npc));
  auto* old = resources_;
  resources_ = new QObject(this);
  if (!pool_owner_) pool_owner_ = new QObject(this);
  parts_.clear(); issues_.clear(); selected_.clear(); visibility_.clear(); hair_vertex_tint_.clear();
  // Reuse GPU objects whose content did not change (a face edit leaves the body and outfit alone).
  op.stage("textures");
  QHash<QString, QQuick3DTextureData*> textures;
  QHash<QString, PooledTexture> texture_pool;
  for (std::size_t i = 0; i < prepared.textures.size(); ++i) {
    const auto& [key, image] = prepared.textures[i];
    auto pooled = texture_pool.value(key);
    if (!pooled.gpu) {
      pooled = texture_pool_.take(key);
      if (pooled.gpu) refresh(pooled.gpu);
    }
    if (!pooled.gpu) pooled = PooledTexture{texture(*image, pool_owner_), image};
    texture_pool.insert(key, pooled);
    textures[prepared.texture_paths[i]] = pooled.gpu;
  }
  op.stage("geometries");
  QHash<QString, QQuick3DGeometry*> geometry_pool, uv_pool;
  auto take = [&](const rendering::RenderPart& part, const QString& key, bool uv) {
    auto& fresh = uv ? uv_pool : geometry_pool;
    auto& previous = uv ? uv_pool_ : geometry_pool_;
    if (auto* g = fresh.value(key)) return g;
    auto* g = previous.take(key);
    if (g) refresh(g);
    else g = geometry(part, pool_owner_, uv);
    fresh.insert(key, g);
    return g;
  };
  for (std::size_t index = 0; index < scene.parts.size(); ++index) {
    const auto& part = scene.parts[index];
    const auto& key = prepared.geometry_keys[index];
    QVariantMap row{{"id", qs(part.id)}, {"label", qs(part.label + " / " + part.shape_name)}, {"shape", qs(part.shape_name)},
      {"group", qs(part.group)}, {"path", qs(part.mesh_path)}, {"armorFormKey", qs(part.armor_form_key)},
      {"shownByDefault", part.shown_by_default}, {"hiddenReason", qs(part.hidden_reason)},
      {"geometry", QVariant::fromValue(take(part, key, false))},
      {"uvGeometry", QVariant::fromValue(take(part, key, true))},
      {"diffuse", QVariant::fromValue(textures.value(qs(part.diffuse)))},
      {"normal", QVariant::fromValue(textures.value(qs(part.normal)))},
      {"alphaMode", qs(std::string(rendering::to_string(part.alpha_mode)))}, {"alphaCutoff", part.alpha_cutoff},
      {"materialAlpha", part.material_alpha}, {"vertexColors", part.vertex_colors}, {"hair", part.shader_type == nif::kShaderHairTint},
      {"doubleSided", part.double_sided}, {"center", (vec(part.min) + vec(part.max)) * 0.5f * scene.scale},
      {"radius", std::max(1.0f, (vec(part.max) - vec(part.min)).length() * 0.5f * scene.scale)},
      {"scale", scene.scale}, {"tint", QColor(Qt::white)}, {"warningCount", static_cast<int>(part.warnings.size())},
      {"inspector", inspector(part, scene)}};
    if (part.tint) row["tint"] = QColor::fromRgbF(part.tint->r, part.tint->g, part.tint->b);
    if (part.shader_type == nif::kShaderHairTint && part.vertex_colors) {
      row["hairVertexTint"] = true;
      hair_vertex_tint_.push_back({row.value("geometry").value<QQuick3DGeometry*>(), part.vertices});
    }
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

  outfit_diagnostics_.clear();
  for (const auto& diag : scene.outfit_diagnostics) {
    QVariantList slot_list, conf_list, paths, providers;
    for (int s : diag.slot_indices) slot_list.push_back(s);
    for (int s : diag.conflicting_slots) conf_list.push_back(s);
    for (const auto& p : diag.mesh_paths) paths.push_back(qs(p));
    for (const auto& pr : diag.mesh_providers) providers.push_back(qs(pr));
    outfit_diagnostics_.push_back(QVariantMap{
        {"armorFormKey", qs(diag.armor_form_key)},
        {"name", qs(diag.name)},
        {"slotMask", static_cast<qulonglong>(diag.slot_mask)},
        {"slots", slot_list},
        {"equipped", diag.equipped},
        {"unequippedBy", qs(diag.unequipped_by)},
        {"unequippedByFormKey", qs(diag.unequipped_by_form_key)},
        {"conflictingSlots", conf_list},
        {"meshPaths", paths},
        {"meshProviders", providers},
        {"meshesExist", diag.meshes_exist},
        {"reason", qs(diag.reason)},
    });
  }

  covered_body_diagnostics_.clear();
  for (const auto& diag : scene.covered_body_diagnostics) {
    QVariantList slot_list, cov_slots;
    for (int s : diag.slot_indices) slot_list.push_back(s);
    for (int s : diag.covering_slots) cov_slots.push_back(s);
    covered_body_diagnostics_.push_back(QVariantMap{
        {"pieceName", qs(diag.piece_name)},
        {"addonFormKey", qs(diag.addon_form_key)},
        {"slotMask", static_cast<qulonglong>(diag.slot_mask)},
        {"slots", slot_list},
        {"coveredByPiece", qs(diag.covered_by_piece)},
        {"coveredByFormKey", qs(diag.covered_by_form_key)},
        {"coveringSlots", cov_slots},
        {"meshPath", qs(diag.mesh_path)},
        {"meshProvider", qs(diag.mesh_provider)},
        {"meshExists", diag.mesh_exists},
    });
  }

  skin_match_ = skin_match_of(scene);
  installed_ = true;
  installed_npc_ = prepared.npc;
  installed_preview_ = prepared.preview;
  record_hair_ = prepared.record_hair;
  sync_hair_tint();
  apply_hair_vertex_tint();  // new geometries carry the NIF colors until the tint is written
  op.stage("notify QML");
  emit changed(); emit visibilityChanged(); emit selectionChanged();
  // QML has switched to the new objects synchronously in changed(); what is left over goes away
  // with deleteLater, after the scene graph has synchronized (never while a frame uses it).
  op.stage("release unused GPU objects");
  if (old) old->deleteLater();
  for (auto* g : std::as_const(geometry_pool_)) g->deleteLater();
  for (auto* g : std::as_const(uv_pool_)) g->deleteLater();
  for (const auto& t : std::as_const(texture_pool_)) t.gpu->deleteLater();
  geometry_pool_ = std::move(geometry_pool);
  uv_pool_ = std::move(uv_pool);
  texture_pool_ = std::move(texture_pool);
  op.succeed();
}
void SceneViewModel::set_npc_override(const QString& form_key) {
  npc_override_ = form_key;
  reload();
}
void SceneViewModel::select(const QString& id) { selected_ = id; emit selectionChanged(); }
void SceneViewModel::setVisible(const QString& id, bool visible) {
  if (!visibility_.contains(id) || visibility_.value(id).toBool() == visible) return;
  visibility_[id] = visible;
  emit visibilityChanged();
}
void SceneViewModel::setPieceVisible(const QString& keyOrPath, bool visible) {
  if (keyOrPath.isEmpty()) return;
  QString norm = keyOrPath;
  norm.replace('/', '\\');
  bool any_changed = false;
  for (const auto& p : parts_) {
    const auto row = p.toMap();
    const QString id = row.value("id").toString();
    const QString fk = row.value("armorFormKey").toString();
    QString path = row.value("path").toString();
    path.replace('/', '\\');
    if (id.compare(keyOrPath, Qt::CaseInsensitive) == 0 ||
        (!fk.isEmpty() && fk.compare(keyOrPath, Qt::CaseInsensitive) == 0) ||
        (!path.isEmpty() && path.compare(norm, Qt::CaseInsensitive) == 0)) {
      if (visibility_.value(id).toBool() != visible) {
        visibility_[id] = visible;
        any_changed = true;
      }
    }
  }
  if (any_changed) emit visibilityChanged();
}
bool SceneViewModel::isPieceVisible(const QString& keyOrPath) const {
  if (keyOrPath.isEmpty()) return false;
  QString norm = keyOrPath;
  norm.replace('/', '\\');
  for (const auto& p : parts_) {
    const auto row = p.toMap();
    const QString id = row.value("id").toString();
    const QString fk = row.value("armorFormKey").toString();
    QString path = row.value("path").toString();
    path.replace('/', '\\');
    if (id.compare(keyOrPath, Qt::CaseInsensitive) == 0 ||
        (!fk.isEmpty() && fk.compare(keyOrPath, Qt::CaseInsensitive) == 0) ||
        (!path.isEmpty() && path.compare(norm, Qt::CaseInsensitive) == 0)) {
      if (visibility_.value(id, row.value("shownByDefault", true)).toBool()) return true;
    }
  }
  return false;
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
void SceneViewModel::selectMesh(const QString& dataRelativePath) {
  if (!armor_preview_) return;
  QString norm = dataRelativePath.trimmed();
  norm.replace('/', '\\');
  while (norm.startsWith('\\')) norm.remove(0, 1);
  if (norm.startsWith(QStringLiteral("data\\"), Qt::CaseInsensitive)) norm.remove(0, 5);
  while (norm.startsWith('\\')) norm.remove(0, 1);
  if (norm.isEmpty()) {
    selected_mesh_.clear();
    emit previewModeChanged();
    return;
  }
  if (!norm.endsWith(QStringLiteral(".nif"), Qt::CaseInsensitive)) {
    cancel();
    selected_armor_.clear();
    selected_mesh_.clear();
    clear();
    loading_ = false;
    status_ = tr("El archivo '%1' no es una malla NIF compatible con el visor 3D.").arg(norm);
    issues_ = {QVariantMap{{"severity", "warning"}, {"code", "UnsupportedMeshFormat"}, {"message", status_}, {"subject", norm}}};
    emit previewModeChanged();
    emit changed();
    emit loadingChanged();
    emit ready();
    return;
  }
  selected_armor_.clear();
  selected_mesh_ = norm;
  emit previewModeChanged();
  reload();
}
void SceneViewModel::selectArmor(const QString& formKey) {
  if (!armor_preview_ || formKey.isEmpty()) return;
  const QString key = formKey.trimmed();
  const bool looks_like_path = key.contains('/') || key.contains('\\') ||
                               key.endsWith(QStringLiteral(".nif"), Qt::CaseInsensitive) ||
                               key.endsWith(QStringLiteral(".dds"), Qt::CaseInsensitive) ||
                               key.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive) ||
                               key.endsWith(QStringLiteral(".bsa"), Qt::CaseInsensitive);
  if (looks_like_path) {
    cancel();
    selected_armor_.clear();
    selected_mesh_.clear();
    clear();
    loading_ = false;
    status_ = tr("Ruta de archivo inválida para selectArmor ('%1'). Usa selectMesh(dataRelativePath) para mallas NIF sueltas.").arg(key);
    issues_ = {QVariantMap{{"severity", "error"}, {"code", "InvalidArmorFormKey"}, {"message", status_}, {"subject", key}}};
    emit previewModeChanged();
    emit changed();
    emit loadingChanged();
    emit ready();
    return;
  }
  selected_mesh_.clear();
  selected_armor_ = key;
  emit previewModeChanged();
  reload();
}
void SceneViewModel::setPreviewMode(const QString& mode) {
  if (!armor_preview_ || (mode != "piece" && mode != "mannequin" && mode != "npc" && mode != "outfit") || mode == preview_mode_) return;
  preview_mode_ = mode;
  emit previewModeChanged();
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

QVariantMap SceneViewModel::explainPieceVisibility(const QString& armorFormKey) const {
  for (const auto& v : outfit_diagnostics_) {
    const auto map = v.toMap();
    if (map.value("armorFormKey").toString().compare(armorFormKey, Qt::CaseInsensitive) == 0) {
      return map;
    }
  }
  return {};
}

QVariantMap SceneViewModel::explainShapeVisibility(const QString& shapeId) const {
  for (const auto& p : parts_) {
    const auto row = p.toMap();
    if (row.value("id").toString() == shapeId) {
      QVariantMap res;
      res["id"] = shapeId;
      res["shape"] = row.value("shape");
      res["label"] = row.value("label");
      res["shownByDefault"] = row.value("shownByDefault");
      res["hiddenReason"] = row.value("hiddenReason");
      res["currentlyVisible"] = visibility_.value(shapeId, row.value("shownByDefault"));
      res["armorFormKey"] = row.value("armorFormKey");
      res["group"] = row.value("group");
      return res;
    }
  }
  return {};
}

} // namespace sns::app
