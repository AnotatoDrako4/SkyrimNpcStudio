#pragma once

#include <memory>
#include <mutex>

#include "mo2_snapshot.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/face/facegen.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::app {

// Read-side resources of one MO2 snapshot, shared by the 3D view and the face regenerator:
// the asset resolver (every BSA directory opened once), parsed meshes / decoded textures, and the
// raw head part meshes and TRI files used by FaceGen. Built lazily on a pool thread.
struct ResourceContext {
  SnapshotPtr snapshot;
  std::shared_ptr<const assets::AssetResolver> resolver;
  std::shared_ptr<rendering::SceneCache> scene_cache;
  std::shared_ptr<face::FaceAssetCache> face_assets;

  // The context of `snapshot`, building it on first use (thread-safe). A new snapshot replaces
  // the cached one; contexts still referenced elsewhere stay valid.
  static std::shared_ptr<ResourceContext> for_snapshot(const SnapshotPtr& snapshot);
};

}  // namespace sns::app
