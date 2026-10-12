#include "resource_context.hpp"

namespace sns::app {

std::shared_ptr<ResourceContext> ResourceContext::for_snapshot(const SnapshotPtr& snapshot) {
  static std::mutex mutex;
  static std::shared_ptr<ResourceContext> current;
  if (!snapshot) return nullptr;
  std::lock_guard lock(mutex);
  if (current && current->snapshot == snapshot) return current;

  auto context = std::make_shared<ResourceContext>();
  context->snapshot = snapshot;
  context->resolver = std::make_shared<const assets::AssetResolver>(
      snapshot->vfs, assets::archive_load_order(snapshot->profile, snapshot->load_order, snapshot->vfs));
  context->scene_cache = std::make_shared<rendering::SceneCache>();
  std::weak_ptr<const assets::AssetResolver> weak = context->resolver;
  context->face_assets = std::make_shared<face::FaceAssetCache>([weak](std::string_view path) -> std::optional<std::string> {
    const auto resolver = weak.lock();
    return resolver ? resolver->read(path) : std::nullopt;
  });
  current = context;
  return context;
}

}  // namespace sns::app
