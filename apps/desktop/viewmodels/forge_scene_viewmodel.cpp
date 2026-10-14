#include "forge_scene_viewmodel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "diagnostics/crash_diagnostics.hpp"
#include "sns/core/text.hpp"
#include "sns/gltf/gltf_inspect.hpp"

namespace sns::app {
namespace {

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

QString local_path(const QString& path_or_url) {
  const QUrl url(path_or_url);
  return url.isLocalFile() ? url.toLocalFile() : path_or_url;
}

// Preview limits: RuntimeLoader imports on the GUI thread, so the preview refuses what would
// freeze the studio for long. Skyrim characters stay far below these.
gltf::Limits preview_limits() {
  gltf::Limits limits;
  limits.max_file_bytes = 256ull * 1024 * 1024;
  limits.max_resource_bytes = 512ull * 1024 * 1024;
  limits.max_vertices = 5'000'000;
  limits.max_triangles = 5'000'000;
  return limits;
}

QString short_message(const std::string& code) {
  static const std::pair<const char*, const char*> messages[] = {
      {"NotGltf", QT_TRANSLATE_NOOP("ForgeScene", "Archivo inválido: no es un glTF 2.0 legible.")},
      {"FileUnreadable", QT_TRANSLATE_NOOP("ForgeScene", "No se puede leer el archivo.")},
      {"GlbTruncated", QT_TRANSLATE_NOOP("ForgeScene", "GLB truncado o incompleto.")},
      {"JsonInvalid", QT_TRANSLATE_NOOP("ForgeScene", "El glTF está mal formado.")},
      {"UnsupportedVersion", QT_TRANSLATE_NOOP("ForgeScene", "Versión no compatible: se requiere glTF 2.0.")},
      {"BufferMissing", QT_TRANSLATE_NOOP("ForgeScene", "Falta un buffer de datos del modelo.")},
      {"ImageMissing", QT_TRANSLATE_NOOP("ForgeScene", "Falta una textura del modelo.")},
      {"UriRejected", QT_TRANSLATE_NOOP("ForgeScene", "El modelo pide un archivo remoto o fuera de su carpeta: bloqueado.")},
      {"AccessorOutOfBounds", QT_TRANSLATE_NOOP("ForgeScene", "Datos de geometría fuera de rango.")},
      {"IndexOutOfRange", QT_TRANSLATE_NOOP("ForgeScene", "Índices de triángulos inválidos.")},
      {"InvalidReference", QT_TRANSLATE_NOOP("ForgeScene", "Referencias internas rotas en el modelo.")},
      {"NodeHierarchyInvalid", QT_TRANSLATE_NOOP("ForgeScene", "Jerarquía de nodos inválida (ciclo o doble padre).")},
      {"EmptyGeometry", QT_TRANSLATE_NOOP("ForgeScene", "El modelo no contiene geometría que dibujar.")},
      {"ModelTooLarge", QT_TRANSLATE_NOOP("ForgeScene", "Modelo demasiado grande para la vista previa.")},
      {"CompressedDataUnsupported", QT_TRANSLATE_NOOP("ForgeScene", "Usa compresión (Draco/meshopt/Basis) no compatible.")},
      {"Cancelled", QT_TRANSLATE_NOOP("ForgeScene", "Carga cancelada.")},
  };
  for (const auto& [c, m] : messages) {
    if (code == c) return QCoreApplication::translate("ForgeScene", m);
  }
  return QCoreApplication::translate("ForgeScene", "El modelo no supera la validación.");
}

QVariantMap to_variant(const gltf::Inspection& i) {
  QVariantList meshes, skins, materials, images, animations, resources, issues;
  for (const auto& m : i.mesh_list) {
    QVariantList primitives;
    for (const auto& p : m.primitives) {
      primitives << QVariantMap{{"mode", p.mode}, {"material", p.material}, {"vertices", static_cast<qulonglong>(p.vertices)},
                                {"indices", static_cast<qulonglong>(p.indices)}, {"triangles", static_cast<qulonglong>(p.triangles)},
                                {"normals", p.normals}, {"uv0", p.uv0}, {"skinned", p.skinned},
                                {"morphTargets", static_cast<qulonglong>(p.morph_targets)}};
    }
    meshes << QVariantMap{{"name", qs(m.name)}, {"primitives", primitives}};
  }
  for (const auto& s : i.skin_list) {
    QStringList joints;
    for (const auto& j : s.joints) joints << qs(j);
    skins << QVariantMap{{"name", qs(s.name)}, {"joints", joints}};
  }
  for (const auto& m : i.material_list) {
    materials << QVariantMap{{"name", qs(m.name)}, {"alphaMode", qs(m.alpha_mode)}, {"doubleSided", m.double_sided},
                             {"baseColorTexture", m.base_color_texture}, {"normalTexture", m.normal_texture}};
  }
  for (const auto& im : i.image_list) {
    images << QVariantMap{{"name", qs(im.name)}, {"mime", qs(im.mime)}, {"source", qs(im.source)}, {"bytes", static_cast<qulonglong>(im.bytes)}};
  }
  for (const auto& a : i.animation_list) animations << QVariantMap{{"name", qs(a.name)}, {"channels", static_cast<qulonglong>(a.channels)}};
  for (const auto& r : i.resources) {
    resources << QVariantMap{{"uri", qs(r.uri)}, {"path", qs(r.relative_path)}, {"kind", qs(r.kind)}, {"bytes", static_cast<qulonglong>(r.bytes)}};
  }
  for (const auto& x : i.issues) {
    issues << QVariantMap{{"severity", qs(x.severity)}, {"code", qs(x.code)}, {"message", qs(x.message)}, {"where", qs(x.where)}};
  }
  QStringList used, required;
  for (const auto& e : i.extensions_used) used << qs(e);
  for (const auto& e : i.extensions_required) required << qs(e);
  QVariantList bounds;
  if (i.position_bounds) for (const auto v : *i.position_bounds) bounds << v;
  // Nodes that draw a mesh in the order RuntimeLoader's scene tree is walked (depth first).
  QVariantList mesh_nodes;
  std::vector<int> order_of(i.node_list.size(), -1);
  for (std::size_t k = 0; k < i.mesh_nodes_in_scene_order.size(); ++k) order_of[i.mesh_nodes_in_scene_order[k]] = static_cast<int>(k);
  for (const auto n : i.mesh_nodes_in_scene_order) {
    const auto& node = i.node_list[n];
    // Nearest ancestor that draws a mesh, as a position in this list (-1 = none).
    int mesh_parent = -1;
    for (int a = node.parent; a >= 0 && mesh_parent < 0; a = i.node_list[static_cast<std::size_t>(a)].parent) mesh_parent = order_of[static_cast<std::size_t>(a)];
    if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= i.mesh_list.size()) continue;
    const auto& mesh = i.mesh_list[static_cast<std::size_t>(node.mesh)];
    qulonglong vertices = 0, triangles = 0, morph_targets = 0;
    for (const auto& p : mesh.primitives) {
      vertices += p.vertices;
      triangles += p.triangles;
      morph_targets = std::max<qulonglong>(morph_targets, p.morph_targets);
    }
    mesh_nodes << QVariantMap{{"node", static_cast<qulonglong>(n)}, {"name", qs(node.name)}, {"mesh", node.mesh},
                              {"meshName", qs(mesh.name)}, {"primitives", static_cast<qulonglong>(mesh.primitives.size())},
                              {"vertices", vertices}, {"triangles", triangles}, {"skinned", node.skin >= 0},
                              {"morphTargets", morph_targets}, {"meshParent", mesh_parent}};
  }
  return {{"container", qs(i.container)},
          {"generator", qs(i.generator)},
          {"version", qs(i.version)},
          {"fileBytes", static_cast<qulonglong>(i.file_bytes)},
          {"loadable", i.loadable()},
          {"declared", QVariantMap{{"scenes", static_cast<qulonglong>(i.scenes)}, {"nodes", static_cast<qulonglong>(i.nodes)},
                                   {"meshes", static_cast<qulonglong>(i.meshes)}, {"primitives", static_cast<qulonglong>(i.primitives)},
                                   {"accessors", static_cast<qulonglong>(i.accessors)}, {"materials", static_cast<qulonglong>(i.materials)},
                                   {"textures", static_cast<qulonglong>(i.textures)}, {"images", static_cast<qulonglong>(i.images)},
                                   {"skins", static_cast<qulonglong>(i.skins)}, {"joints", static_cast<qulonglong>(i.joints)},
                                   {"animations", static_cast<qulonglong>(i.animations)}, {"morphTargets", static_cast<qulonglong>(i.morph_targets)},
                                   {"cameras", static_cast<qulonglong>(i.cameras)}}},
          {"validated", QVariantMap{{"vertices", static_cast<qulonglong>(i.vertices)}, {"indices", static_cast<qulonglong>(i.indices)},
                                    {"triangles", static_cast<qulonglong>(i.triangles)}, {"indicesChecked", i.indices_checked}}},
          {"positionBounds", bounds},
          {"meshNodes", mesh_nodes},
          {"meshes", meshes},
          {"skins", skins},
          {"materials", materials},
          {"images", images},
          {"animations", animations},
          {"resources", resources},
          {"extensionsUsed", used},
          {"extensionsRequired", required},
          {"issues", issues}};
}

QString details_of(const gltf::Inspection& i) {
  QStringList lines;
  for (const auto& x : i.issues) {
    lines << QStringLiteral("%1 %2%3: %4").arg(qs(x.severity), qs(x.code), x.where.empty() ? QString() : " " + qs(x.where), qs(x.message));
  }
  return lines.join('\n');
}

// Removes older only when it is a staging folder below oot (never an empty or outside path).
void remove_below(const QString& root, const QString& folder) {
  if (!root.isEmpty() && !folder.isEmpty() && folder.startsWith(root + '/')) QDir(folder).removeRecursively();
}

#ifdef _WIN32
bool process_alive(qint64 pid) {
  const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (!h) return GetLastError() == ERROR_ACCESS_DENIED;  // exists but belongs to someone else
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
}
#endif

}  // namespace

struct ForgeSceneViewModel::Result {
  int operation = 0;
  gltf::Inspection inspection;
  QString staged_file;    // the file RuntimeLoader gets
  QString staged_folder;
  QString failure;        // exception or staging failure
  bool cancelled = false;
};

ForgeSceneViewModel::ForgeSceneViewModel(QObject* parent) : QObject(parent) {
  pool_.setMaxThreadCount(1);
  const QDir temp = QDir::temp();
  root_ = temp.filePath(QStringLiteral("sns-forge-preview-%1").arg(QCoreApplication::applicationPid()));
#ifdef _WIN32
  // Staging folders of studios that ended without cleaning up (crash, kill).
  for (const auto& name : temp.entryList({QStringLiteral("sns-forge-preview-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok = false;
    const auto pid = name.section('-', -1).toLongLong(&ok);
    if (ok && pid != QCoreApplication::applicationPid() && !process_alive(pid)) QDir(temp.filePath(name)).removeRecursively();
  }
#endif
}

ForgeSceneViewModel::~ForgeSceneViewModel() {
  if (cancel_) cancel_->store(true);
  pool_.waitForDone();
  QDir(root_).removeRecursively();
}

QString ForgeSceneViewModel::file_name() const { return path_.isEmpty() ? QString() : QFileInfo(path_).fileName(); }

bool ForgeSceneViewModel::previewable(const QString& path) {
  const auto suffix = QFileInfo(local_path(path)).suffix().toLower();
  return suffix == QStringLiteral("glb") || suffix == QStringLiteral("gltf");
}

void ForgeSceneViewModel::set_state(const QString& state, const QString& message, const QString& details) {
  state_ = state;
  message_ = message;
  details_ = details;
  emit changed();
}

void ForgeSceneViewModel::clear_parts() {
  if (parts_.isEmpty() && selected_ == -1 && part_match_.isEmpty()) return;
  parts_.clear();
  selected_ = -1;
  part_match_.clear();
  emit partsChanged();
}

void ForgeSceneViewModel::clear_source() {
  clear_parts();  // parts belong to the asset being shown
  if (source_.isEmpty()) return;
  source_.clear();
  emit sourceChanged();  // the viewport unloads the current asset now
}

void ForgeSceneViewModel::remove_staging(const QString& folder) { remove_below(root_, folder); }

void ForgeSceneViewModel::load(const QString& path_or_url) {
  const auto local = local_path(path_or_url);
  ++operation_;
  if (cancel_) cancel_->store(true);
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  path_ = local.isEmpty() ? QString() : QFileInfo(local).absoluteFilePath();
  inspection_.clear();
  rendered_.clear();
  clear_source();
  remove_staging(staging_);
  staging_.clear();
  if (path_.isEmpty()) {
    set_state(QStringLiteral("empty"), {});
    return;
  }
  if (!previewable(path_)) {
    set_state(QStringLiteral("unsupported"),
              tr("Vista previa directa solo para GLB/glTF. Convierte %1 a GLB con el puente de Blender.").arg(file_name()));
    return;
  }
  set_state(QStringLiteral("validating"), tr("Validando %1…").arg(file_name()));
  const int op = operation_;
  const auto folder = QDir(root_).filePath(QString::number(op));
  diag::record("forge", ("preview #" + std::to_string(op) + " " + path_.toStdString()).c_str());
  QPointer<ForgeSceneViewModel> self(this);
  pool_.start([self, op, path = path_, folder, root = root_, cancel = cancel_] {
    auto result = std::make_shared<Result>();
    result->operation = op;
    diag::Operation diagnostics("forge.validate", diag::next_task_id(), path.toStdString());
    try {
      auto asset = gltf::load_and_inspect(std::filesystem::path(path.toStdWString()), preview_limits(),
                                          [cancel] { return cancel->load(); });
      result->inspection = std::move(asset.inspection);
      result->cancelled = cancel->load();
      if (result->inspection.loadable() && !result->cancelled) {
        // Stage exactly the validated bytes: the renderer never reads the original files, which
        // could change between validation and loading.
        diagnostics.stage("stage validated bytes");
        const std::filesystem::path dir(folder.toStdWString());
        std::filesystem::create_directories(dir);
        const auto main_name = std::string("model.") + result->inspection.container;
        auto write = [&](const std::filesystem::path& file, const std::string& bytes) {
          std::filesystem::create_directories(file.parent_path());
          std::ofstream out(file, std::ios::binary | std::ios::trunc);
          out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
          out.close();
          if (!out || std::filesystem::file_size(file) != bytes.size()) throw std::runtime_error("cannot write " + text::path_to_utf8(file));
        };
        write(dir / main_name, asset.main_bytes);
        for (const auto& [relative, bytes] : asset.resources) write(dir / text::path_from_utf8(relative), bytes);
        result->staged_folder = folder;
        result->staged_file = QString::fromStdWString((dir / main_name).wstring());
      }
      diagnostics.succeed();
    } catch (const std::exception& e) {
      diagnostics.fail(e.what());
      result->failure = QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(self, [self, result, root] {
      if (self) {
        self->finish(result);
      } else {
        remove_below(root, result->staged_folder);
      }
    }, Qt::QueuedConnection);
  });
}

void ForgeSceneViewModel::finish(const std::shared_ptr<Result>& r) {
  if (r->operation != operation_) {  // superseded by a newer load, a cancel or a reset
    remove_staging(r->staged_folder);
    return;
  }
  inspection_ = to_variant(r->inspection);
  const auto& issues = r->inspection.issues;
  if (!r->failure.isEmpty()) {
    remove_staging(r->staged_folder);
    set_state(QStringLiteral("error"), tr("No se pudo preparar el modelo para la vista previa."), r->failure);
    return;
  }
  if (r->cancelled) {
    remove_staging(r->staged_folder);
    set_state(QStringLiteral("cancelled"), short_message("Cancelled"));
    return;
  }
  if (!r->inspection.loadable()) {
    const auto first = std::find_if(issues.begin(), issues.end(), [](const gltf::Issue& i) { return i.severity == "error"; });
    diag::record("forge", ("preview #" + std::to_string(r->operation) + " rejected: " + first->code + " " + first->message).c_str());
    set_state(QStringLiteral("error"), short_message(first->code), details_of(r->inspection));
    return;
  }
  staging_ = r->staged_folder;
  set_state(QStringLiteral("loading"), tr("Cargando %1 en el visor…").arg(file_name()), details_of(r->inspection));
  // A short delay lets the viewport paint the loading state: RuntimeLoader then imports the asset
  // synchronously on the GUI thread.
  const auto url = QUrl::fromLocalFile(r->staged_file);
  QTimer::singleShot(50, this, [this, op = r->operation, url] {
    if (op != operation_) return;
    source_ = url;
    emit sourceChanged();
  });
}

void ForgeSceneViewModel::reportLoaderStatus(const QUrl& loaded_source, int status, const QString& error_string) {
  if (source_.isEmpty() || loaded_source != source_) return;  // a report about an older asset
  if (status == 1 && state_ != QStringLiteral("loaded")) {
    diag::record("forge", ("preview #" + std::to_string(operation_) + " loaded by RuntimeLoader").c_str());
    set_state(QStringLiteral("loaded"), {}, details_);
    emit frameRequested();  // on the validated bounds until the rendered ones arrive
  } else if (status == 2) {
    diag::record("forge", ("preview #" + std::to_string(operation_) + " RuntimeLoader error: " + error_string.toStdString()).c_str());
    rendered_.clear();
    set_state(QStringLiteral("error"), tr("Fallo de renderizado: el visor 3D no pudo cargar el modelo."),
              error_string + (details_.isEmpty() ? QString() : "\n" + details_));
    clear_source();
  }
}

void ForgeSceneViewModel::reportBounds(const QUrl& loaded_source, const QVector3D& minimum, const QVector3D& maximum) {
  if (source_.isEmpty() || loaded_source != source_) {
    diag::record("forge", ("bounds for another source: " + loaded_source.toString().toStdString()).c_str());
    return;
  }
  const auto size = maximum - minimum;
  const bool valid = std::isfinite(size.x()) && std::isfinite(size.y()) && std::isfinite(size.z()) && size.x() >= 0 &&
                     size.y() >= 0 && size.z() >= 0 && size.length() > 0;
  if (!valid) {
    diag::record("forge", QStringLiteral("bounds ignored (%1, %2, %3)").arg(size.x()).arg(size.y()).arg(size.z()).toStdString().c_str());
    return;
  }
  const bool first = rendered_.isEmpty();
  rendered_ = {{"minimum", QVariant::fromValue(minimum)},
               {"maximum", QVariant::fromValue(maximum)},
               {"size", QVariant::fromValue(size)},
               {"center", QVariant::fromValue((minimum + maximum) * 0.5f)},
               {"radius", size.length() * 0.5f}};
  emit changed();
  if (first) emit frameRequested();
}

void ForgeSceneViewModel::reportParts(const QUrl& loaded_source, const QVariantList& rendered_parts) {
  if (source_.isEmpty() || loaded_source != source_ || state_ != QStringLiteral("loaded")) return;
  const auto nodes = inspection_.value(QStringLiteral("meshNodes")).toList();
  const bool by_order = !rendered_parts.isEmpty() && rendered_parts.size() == nodes.size();
  const bool same_parts = parts_.size() == rendered_parts.size();  // a repeated report keeps the user's choices
  bool consistent = by_order;
  QVariantList parts;
  for (qsizetype i = 0; i < rendered_parts.size(); ++i) {
    const auto r = rendered_parts[i].toMap();
    const auto minimum = r.value(QStringLiteral("minimum")).value<QVector3D>();
    const auto maximum = r.value(QStringLiteral("maximum")).value<QVector3D>();
    const int materials = r.value(QStringLiteral("materials")).toInt();
    const bool skinned = r.value(QStringLiteral("skinned")).toBool();
    const int morph_targets = r.value(QStringLiteral("morphTargets")).toInt();
    // Nearest Model above this one in the render tree; listed depth first, so it comes earlier.
    const int parent = r.value(QStringLiteral("parent"), -1).toInt();
    QVariantMap part{{"index", static_cast<int>(i)},
                     {"parent", parent >= 0 && parent < i ? parent : -1},
                     {"visible", same_parts ? parts_[i].toMap().value(QStringLiteral("visible"), true).toBool() : true},
                     {"materials", materials},
                     {"skinned", skinned},
                     {"morphTargets", morph_targets},
                     {"size", QVariant::fromValue(maximum - minimum)}};
    if (by_order) {
      const auto n = nodes[i].toMap();
      // Qt's importer gives a Model one material per glTF primitive, keeps skins and morph targets
      // on it and mirrors the node hierarchy; a disagreement means the order assumption does not
      // hold for this asset.
      const bool agrees = n.value(QStringLiteral("primitives")).toInt() == materials &&
                          n.value(QStringLiteral("skinned")).toBool() == skinned &&
                          n.value(QStringLiteral("morphTargets")).toInt() == morph_targets &&
                          n.value(QStringLiteral("meshParent")).toInt() == part.value(QStringLiteral("parent")).toInt();
      consistent &= agrees;
      const auto name = n.value(QStringLiteral("name")).toString();
      const auto mesh_name = n.value(QStringLiteral("meshName")).toString();
      part.insert(QVariantMap{
          {"label", !name.isEmpty() ? name : !mesh_name.isEmpty() ? mesh_name : tr("Nodo %1").arg(n.value("node").toInt())},
          {"node", n.value("node")},
          {"nodeName", name},
          {"meshName", mesh_name},
          {"primitives", n.value("primitives")},
          {"vertices", n.value("vertices")},
          {"triangles", n.value("triangles")},
          {"agrees", agrees}});
    } else {
      part.insert(QStringLiteral("label"), tr("Parte %1").arg(i + 1));
    }
    parts << part;
  }
  parts_ = parts;
  refresh_visibility();
  part_match_ = parts_.isEmpty() ? QString() : !by_order ? QStringLiteral("none") : consistent ? QStringLiteral("order") : QStringLiteral("order-unconfirmed");
  if (!same_parts || selected_ >= parts_.size()) selected_ = -1;
  if (!same_parts) {
    diag::record("forge", QStringLiteral("preview #%1: %2 part(s), %3 glTF mesh node(s), match %4")
                              .arg(operation_).arg(parts_.size()).arg(nodes.size()).arg(part_match_).toStdString().c_str());
  }
  emit partsChanged();
}

void ForgeSceneViewModel::selectPart(int index) {
  const int selected = index >= 0 && index < parts_.size() ? index : -1;
  if (selected == selected_) return;
  selected_ = selected;
  emit partsChanged();
}

void ForgeSceneViewModel::setPartVisible(int index, bool visible) {
  if (index < 0 || index >= parts_.size()) return;
  auto part = parts_[index].toMap();
  if (part.value(QStringLiteral("visible")).toBool() == visible) return;
  part.insert(QStringLiteral("visible"), visible);
  parts_[index] = part;
  refresh_visibility();
  emit partsChanged();
}

// Qt Quick 3D does not render the children of a hidden node: "shown" is the part's own choice and
// every ancestor's, "hiddenBy" names the nearest hidden ancestor.
void ForgeSceneViewModel::refresh_visibility() {
  for (qsizetype i = 0; i < parts_.size(); ++i) {
    auto part = parts_[i].toMap();
    QString hidden_by;
    for (int a = part.value(QStringLiteral("parent")).toInt(); a >= 0 && a < i; a = parts_[a].toMap().value(QStringLiteral("parent")).toInt()) {
      const auto ancestor = parts_[a].toMap();
      if (!ancestor.value(QStringLiteral("visible")).toBool()) {
        hidden_by = ancestor.value(QStringLiteral("label")).toString();
        break;
      }
    }
    part.insert(QStringLiteral("hiddenBy"), hidden_by);
    part.insert(QStringLiteral("shown"), part.value(QStringLiteral("visible")).toBool() && hidden_by.isEmpty());
    parts_[i] = part;
  }
}

void ForgeSceneViewModel::showAllParts() {
  bool any = false;
  for (auto& p : parts_) {
    auto part = p.toMap();
    if (part.value(QStringLiteral("visible")).toBool()) continue;
    part.insert(QStringLiteral("visible"), true);
    p = part;
    any = true;
  }
  if (!any) return;
  refresh_visibility();
  emit partsChanged();
}

void ForgeSceneViewModel::reload() {
  if (!path_.isEmpty()) load(path_);
}

void ForgeSceneViewModel::cancel() {
  if (!busy()) return;
  ++operation_;
  if (cancel_) cancel_->store(true);
  clear_source();
  set_state(QStringLiteral("cancelled"), short_message("Cancelled"));
}

void ForgeSceneViewModel::reset() {
  ++operation_;
  if (cancel_) cancel_->store(true);
  path_.clear();
  inspection_.clear();
  rendered_.clear();
  clear_source();
  remove_staging(staging_);
  staging_.clear();
  set_state(QStringLiteral("empty"), {});
}

}  // namespace sns::app
