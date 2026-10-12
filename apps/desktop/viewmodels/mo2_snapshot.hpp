#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "sns/mo2/game.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::app {

// Immutable result of scanning one MO2 profile. Shared read-only between the GUI and pool threads.
struct Mo2Snapshot {
  mo2::InstanceConfig config;
  mo2::Profile profile;
  mo2::GameInstall game;
  mo2::VirtualFileSystem vfs;
  mo2::EffectiveLoadOrder load_order;
  std::string language;
  // lower-case plugin file name -> MO2 mod (or "<Game Data>") that provides the winning file
  std::unordered_map<std::string, std::string> plugin_provider;
  long long scan_ms = 0;
};

using SnapshotPtr = std::shared_ptr<const Mo2Snapshot>;

}  // namespace sns::app
