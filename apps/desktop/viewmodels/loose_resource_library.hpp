#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mo2_snapshot.hpp"
#include "sns/outfit/loose_outfit_indexer.hpp"
#include "worker_client.hpp"

namespace sns::app {

// Manages discovery and visual browsing of loose clothing packs, replacers, and BodySlide projects
// without ESP (EPIC C / Point 4 of RC-A).
// Strictly read-only over the MO2 VFS: exposes items to QML without inventing fake ARMO FormKeys,
// identifies replacers vs unregistered loose meshes, and keeps equippable=false until ARMO conversion
// is completed in a later release.
class LooseResourceLibrary : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")

  // State: "none" | "indexing" | "ready" | "error"
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(bool indexing READ is_indexing NOTIFY stateChanged)
  Q_PROPERTY(QVariantList resources READ resources NOTIFY resourcesChanged)
  Q_PROPERTY(int totalResources READ total_resources NOTIFY resourcesChanged)
  Q_PROPERTY(int looseMeshCount READ loose_mesh_count NOTIFY countsChanged)
  Q_PROPERTY(int bodyslideProjectCount READ bodyslide_project_count NOTIFY countsChanged)
  Q_PROPERTY(int replacerMeshCount READ replacer_mesh_count NOTIFY countsChanged)
  Q_PROPERTY(int weaponMeshCount READ weapon_mesh_count NOTIFY countsChanged)
  Q_PROPERTY(int facegenCount READ facegen_count NOTIFY countsChanged)
  Q_PROPERTY(int textureDdsCount READ texture_dds_count NOTIFY countsChanged)
  Q_PROPERTY(int bsaArchiveCount READ bsa_archive_count NOTIFY countsChanged)
  Q_PROPERTY(int unsupportedFileCount READ unsupported_file_count NOTIFY countsChanged)
  Q_PROPERTY(QVariantMap selectedResource READ selected_resource NOTIFY selectedResourceChanged)
  Q_PROPERTY(QVariantList availableMods READ available_mods NOTIFY filtersChanged)
  Q_PROPERTY(QVariantList availableFamilies READ available_families NOTIFY filtersChanged)

 public:
  explicit LooseResourceLibrary(WorkerClient* worker, QObject* parent = nullptr);
  ~LooseResourceLibrary() override;

  [[nodiscard]] QString state() const { return state_; }
  [[nodiscard]] bool is_indexing() const { return state_ == "indexing"; }
  [[nodiscard]] QVariantList resources() const { return filtered_resources_; }
  [[nodiscard]] int total_resources() const { return static_cast<int>(all_resources_.size()); }
  [[nodiscard]] int loose_mesh_count() const { return loose_mesh_count_; }
  [[nodiscard]] int bodyslide_project_count() const { return bodyslide_project_count_; }
  [[nodiscard]] int replacer_mesh_count() const { return replacer_mesh_count_; }
  [[nodiscard]] int weapon_mesh_count() const { return weapon_mesh_count_; }
  [[nodiscard]] int facegen_count() const { return facegen_count_; }
  [[nodiscard]] int texture_dds_count() const { return texture_dds_count_; }
  [[nodiscard]] int bsa_archive_count() const { return bsa_archive_count_; }
  [[nodiscard]] int unsupported_file_count() const { return unsupported_file_count_; }
  [[nodiscard]] QVariantMap selected_resource() const { return selected_resource_; }
  [[nodiscard]] QVariantList available_mods() const { return available_mods_; }
  [[nodiscard]] QVariantList available_families() const { return available_families_; }

  void set_profile(SnapshotPtr snapshot, const std::unordered_map<std::string, std::string>& registered_meshes = {});
  void set_registered_meshes(const std::unordered_map<std::string, std::string>& registered_meshes);

  Q_INVOKABLE void search(const QString& text, const QString& family, const QString& mod, const QString& kind);
  Q_INVOKABLE void selectResource(const QString& resourceId);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void reload();
  Q_INVOKABLE QVariantMap inspectTexture(const QString& dataRelativePath) const;
  Q_INVOKABLE QVariantMap inspectBsaArchive(const QString& dataRelativePath) const;

 signals:
  void stateChanged();
  void resourcesChanged();
  void countsChanged();
  void selectedResourceChanged();
  void filtersChanged();
  void previewRequested(const QString& dataRelativePath);

 private:
  void start_indexing();
  void apply_filters();

  WorkerClient* worker_ = nullptr;
  SnapshotPtr snapshot_;
  int profile_serial_ = 0;
  QString state_ = "none";

  std::unordered_map<std::string, std::string> registered_meshes_;
  std::vector<outfit::LooseResource> raw_resources_;
  QVariantList all_resources_;
  QVariantList filtered_resources_;
  QVariantMap selected_resource_;
  QVariantList available_mods_;
  QVariantList available_families_;

  int loose_mesh_count_ = 0;
  int bodyslide_project_count_ = 0;
  int replacer_mesh_count_ = 0;
  int weapon_mesh_count_ = 0;
  int facegen_count_ = 0;
  int texture_dds_count_ = 0;
  int bsa_archive_count_ = 0;
  int unsupported_file_count_ = 0;

  QString filter_text_;
  QString filter_family_;
  QString filter_mod_;
  QString filter_kind_;
};

}  // namespace sns::app
