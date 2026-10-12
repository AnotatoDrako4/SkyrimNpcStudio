#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sns/mo2/game.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/profile.hpp"

namespace sns::mo2 {

enum class ProviderKind { GameData, Mod, Overwrite };

// A layer of the virtual Data folder.
struct Provider {
  std::string name;
  std::filesystem::path root;
  ProviderKind kind = ProviderKind::Mod;
  std::size_t file_count = 0;
};

struct ResolvedFile {
  std::filesystem::path absolute;
  std::uint32_t provider = 0;
};

struct VfsBuildOptions {
  std::function<bool()> cancelled;
  std::function<void(std::size_t done, std::size_t total, const std::string& provider)> progress;
  std::vector<std::string> skip_file_suffixes = {".mohidden"};
  std::vector<std::string> skip_directories;
};

struct ProviderConflicts {
  std::size_t files = 0;
  std::vector<std::string> overrides;     // paths where this provider wins over others
  std::vector<std::string> overridden;    // paths where a higher provider wins
  std::size_t overrides_total = 0;
  std::size_t overridden_total = 0;
};

// Read-only reconstruction of MO2's virtual Data folder for one profile:
// game Data (lowest) < enabled mods by priority < overwrite (highest).
// Loose files only: BSA contents are not indexed yet (see docs/MO2_INTEGRATION.md).
class VirtualFileSystem {
 public:
  // Providers must be given in ascending priority.
  static VirtualFileSystem build_from(std::vector<Provider> providers, const VfsBuildOptions& options = {});
  static VirtualFileSystem build(const InstanceConfig& config, const Profile& profile, const GameInstall& game,
                                 VfsBuildOptions options = {});

  [[nodiscard]] const std::vector<Provider>& providers() const noexcept { return providers_; }
  [[nodiscard]] std::size_t file_count() const noexcept { return files_.size(); }

  [[nodiscard]] std::optional<ResolvedFile> resolve(std::string_view data_relative) const;
  // Providers that contain the path, ascending priority (last one wins). Empty if absent.
  [[nodiscard]] std::span<const std::uint32_t> providers_of(std::string_view data_relative) const;
  [[nodiscard]] std::optional<std::uint32_t> find_provider(std::string_view name) const;
  [[nodiscard]] ProviderConflicts conflicts(std::uint32_t provider, std::size_t sample_limit = 200) const;
  // Plugin files (.esp/.esm/.esl) at the root of the virtual Data folder, original case.
  [[nodiscard]] std::vector<std::string> root_plugins() const;

 private:
  struct Entry {
    std::string display;                 // relative path as found on disk
    std::vector<std::uint32_t> providers;
  };
  std::vector<Provider> providers_;
  std::unordered_map<std::string, Entry> files_;
};

}  // namespace sns::mo2
