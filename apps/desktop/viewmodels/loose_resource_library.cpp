#include "loose_resource_library.hpp"

#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <algorithm>

#include "diagnostics/crash_diagnostics.hpp"
#include "json_bridge.hpp"
#include "resource_context.hpp"

namespace sns::app {

namespace {

QString kind_label(const std::string& kind) {
  if (kind == "bodyslide-project") return QStringLiteral("Proyecto BodySlide");
  if (kind == "replacer-mesh") return QStringLiteral("Replacer");
  return QStringLiteral("Malla suelta (sin ESP)");
}

QString category_label(const std::string& cat) {
  if (cat == "cuirass") return QStringLiteral("Pechera / Ropa");
  if (cat == "boots") return QStringLiteral("Calzado / Botas");
  if (cat == "gauntlets") return QStringLiteral("Guantes / Manos");
  if (cat == "helmet") return QStringLiteral("Cabeza / Casco");
  if (cat == "accessory") return QStringLiteral("Accesorio");
  return QStringLiteral("Otro");
}

QVariantMap to_variant_map(const outfit::LooseResource& r) {
  QVariantMap m;
  m["resourceId"] = qs(r.resource_id);
  m["displayName"] = qs(r.display_name);
  m["kind"] = qs(r.kind);
  m["kindLabel"] = kind_label(r.kind);
  m["dataRelativePath"] = qs(r.data_relative_path);
  m["winningProvider"] = qs(r.winning_provider);

  QStringList overridden;
  for (const auto& o : r.overridden_providers) overridden << qs(o);
  m["overriddenProviders"] = overridden;

  m["sizeBytes"] = static_cast<qulonglong>(r.size_bytes);
  m["bodyFamily"] = qs(r.body_family);
  m["category"] = qs(r.category);
  m["categoryLabel"] = category_label(r.category);

  QVariantMap assoc;
  assoc["armorFormKey"] = qs(r.association.armor_form_key);
  assoc["armorAddonFormKey"] = qs(r.association.armor_addon_form_key);
  assoc["confidence"] = qs(r.association.confidence);
  QStringList ev;
  for (const auto& e : r.association.evidence) ev << qs(e);
  assoc["evidence"] = ev;
  m["association"] = assoc;

  QVariantMap cap;
  cap["discovered"] = r.capabilities.discovered;
  cap["previewable"] = r.capabilities.previewable;
  cap["equippable"] = r.capabilities.equippable;  // Always false in RC-A
  cap["convertible"] = qs(r.capabilities.convertible);
  cap["exportable"] = r.capabilities.exportable;
  m["capabilities"] = cap;

  QStringList files;
  for (const auto& f : r.associated_files) files << qs(f);
  m["associatedFiles"] = files;

  QStringList issues;
  for (const auto& i : r.issues) issues << qs(i);
  m["issues"] = issues;

  return m;
}

}  // namespace

LooseResourceLibrary::LooseResourceLibrary(WorkerClient* worker, QObject* parent)
    : QObject(parent), worker_(worker) {
}

LooseResourceLibrary::~LooseResourceLibrary() = default;

void LooseResourceLibrary::set_profile(
    SnapshotPtr snapshot,
    const std::unordered_map<std::string, std::string>& registered_meshes) {
  ++profile_serial_;
  snapshot_ = std::move(snapshot);
  registered_meshes_ = registered_meshes;
  raw_resources_.clear();
  all_resources_.clear();
  filtered_resources_.clear();
  selected_resource_.clear();
  available_mods_.clear();
  available_families_.clear();
  loose_mesh_count_ = 0;
  bodyslide_project_count_ = 0;
  replacer_mesh_count_ = 0;

  if (!snapshot_) {
    state_ = "none";
    emit stateChanged();
    emit resourcesChanged();
    emit countsChanged();
    emit selectedResourceChanged();
    emit filtersChanged();
    return;
  }

  state_ = "indexing";
  emit stateChanged();
  start_indexing();
}

void LooseResourceLibrary::reload() {
  if (!snapshot_) return;
  ++profile_serial_;
  state_ = "indexing";
  emit stateChanged();
  start_indexing();
}

void LooseResourceLibrary::start_indexing() {
  const int serial = profile_serial_;
  QThreadPool::globalInstance()->start([self = QPointer<LooseResourceLibrary>(this), snapshot = snapshot_,
                                        reg_meshes = registered_meshes_, serial] {
    diag::record("loose_outfit", "scan loose outfit packs and bodyslide projects");
    if (!self) return;

    std::shared_ptr<const assets::AssetResolver> resolver;
    const auto ctx = ResourceContext::for_snapshot(snapshot);
    if (ctx) resolver = ctx->resolver;

    outfit::LooseOutfitIndexOptions options;
    options.registered_mesh_to_armo = reg_meshes;
    options.cancelled = [self, serial] {
      return !self || self->profile_serial_ != serial;
    };

    auto result = outfit::LooseOutfitIndexer::index(snapshot->vfs, resolver.get(), options);

    // Build the variant list and filter maps in the background worker thread so the GUI thread stays instant!
    QVariantList all_res;
    all_res.reserve(static_cast<qsizetype>(result.resources.size()));
    QSet<QString> mods_set;
    QSet<QString> families_set;

    for (const auto& r : result.resources) {
      auto m = to_variant_map(r);
      all_res << m;

      const auto prov = m.value("winningProvider").toString();
      if (!prov.isEmpty()) mods_set.insert(prov);

      const auto fam = m.value("bodyFamily").toString();
      if (!fam.isEmpty() && fam != "unknown") families_set.insert(fam);
    }

    QVariantList avail_mods;
    avail_mods << QVariantMap{{"text", tr("Todos los mods")}, {"value", ""}};
    QStringList sorted_mods = mods_set.values();
    std::sort(sorted_mods.begin(), sorted_mods.end(), [](const QString& a, const QString& b) {
      return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const auto& mod_name : sorted_mods) {
      avail_mods << QVariantMap{{"text", mod_name}, {"value", mod_name}};
    }

    QVariantList avail_fams;
    avail_fams << QVariantMap{{"text", tr("Todas las familias")}, {"value", ""}};
    QStringList sorted_fams = families_set.values();
    std::sort(sorted_fams.begin(), sorted_fams.end());
    for (const auto& fam_name : sorted_fams) {
      avail_fams << QVariantMap{{"text", fam_name}, {"value", fam_name}};
    }

    QMetaObject::invokeMethod(self, [self, serial,
                                     res = std::move(result),
                                     all_res = std::move(all_res),
                                     avail_mods = std::move(avail_mods),
                                     avail_fams = std::move(avail_fams)]() mutable {
      if (!self || self->profile_serial_ != serial) return;

      self->raw_resources_ = std::move(res.resources);
      self->loose_mesh_count_ = static_cast<int>(res.loose_mesh_count);
      self->bodyslide_project_count_ = static_cast<int>(res.bodyslide_project_count);
      self->replacer_mesh_count_ = static_cast<int>(res.replacer_mesh_count);

      self->all_resources_ = std::move(all_res);
      self->available_mods_ = std::move(avail_mods);
      self->available_families_ = std::move(avail_fams);

      self->state_ = "ready";
      self->apply_filters();

      emit self->stateChanged();
      emit self->countsChanged();
      emit self->filtersChanged();
    });
  });
}

void LooseResourceLibrary::search(const QString& text, const QString& family, const QString& mod, const QString& kind) {
  filter_text_ = text.trimmed();
  filter_family_ = family.trimmed();
  filter_mod_ = mod.trimmed();
  filter_kind_ = kind.trimmed();
  apply_filters();
}

void LooseResourceLibrary::apply_filters() {
  filtered_resources_.clear();
  for (const auto& v : all_resources_) {
    const auto m = v.toMap();
    if (!filter_text_.isEmpty()) {
      const auto name = m.value("displayName").toString();
      const auto path = m.value("dataRelativePath").toString();
      const auto prov = m.value("winningProvider").toString();
      if (!name.contains(filter_text_, Qt::CaseInsensitive) &&
          !path.contains(filter_text_, Qt::CaseInsensitive) &&
          !prov.contains(filter_text_, Qt::CaseInsensitive)) {
        continue;
      }
    }
    if (!filter_family_.isEmpty()) {
      if (m.value("bodyFamily").toString().compare(filter_family_, Qt::CaseInsensitive) != 0) {
        continue;
      }
    }
    if (!filter_mod_.isEmpty()) {
      if (m.value("winningProvider").toString().compare(filter_mod_, Qt::CaseInsensitive) != 0) {
        continue;
      }
    }
    if (!filter_kind_.isEmpty() && filter_kind_ != "all") {
      if (m.value("kind").toString().compare(filter_kind_, Qt::CaseInsensitive) != 0) {
        continue;
      }
    }
    filtered_resources_ << m;
  }
  emit resourcesChanged();
}

void LooseResourceLibrary::selectResource(const QString& resourceId) {
  if (resourceId.isEmpty()) {
    clearSelection();
    return;
  }
  for (const auto& v : all_resources_) {
    const auto m = v.toMap();
    if (m.value("resourceId").toString() == resourceId) {
      selected_resource_ = m;
      emit selectedResourceChanged();
      return;
    }
  }
}

void LooseResourceLibrary::clearSelection() {
  if (!selected_resource_.isEmpty()) {
    selected_resource_.clear();
    emit selectedResourceChanged();
  }
}

}  // namespace sns::app
