#include "resource_context.hpp"

#include <QString>
#include <QtGlobal>

#include "diagnostics/crash_diagnostics.hpp"

namespace sns::app {

namespace {

// Decoded-texture budget of the scene cache: SNS_SCENE_CACHE_MB (64..16384) or SceneCache's default (768 MB).
std::shared_ptr<rendering::SceneCache> make_scene_cache() {
  if (!qEnvironmentVariableIsSet("SNS_SCENE_CACHE_MB")) return std::make_shared<rendering::SceneCache>();
  bool ok = false;
  const int mb = qEnvironmentVariableIntValue("SNS_SCENE_CACHE_MB", &ok);
  if (!ok || mb < 64 || mb > 16384) {
    diag::record("scene", "SNS_SCENE_CACHE_MB ignored (expected 64..16384): " + qEnvironmentVariable("SNS_SCENE_CACHE_MB").toStdString());
    return std::make_shared<rendering::SceneCache>();
  }
  diag::record("scene", "scene cache texture budget " + std::to_string(mb) + " MB (SNS_SCENE_CACHE_MB)");
  return std::make_shared<rendering::SceneCache>(static_cast<std::uint64_t>(mb) * 1024 * 1024);
}

}  // namespace

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
  context->scene_cache = make_scene_cache();
  std::weak_ptr<const assets::AssetResolver> weak = context->resolver;
  context->face_assets = std::make_shared<face::FaceAssetCache>([weak](std::string_view path) -> std::optional<std::string> {
    const auto resolver = weak.lock();
    return resolver ? resolver->read(path) : std::nullopt;
  });
  current = context;
  return context;
}

}  // namespace sns::app
