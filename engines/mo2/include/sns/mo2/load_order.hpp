#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sns/mo2/game.hpp"
#include "sns/mo2/plugin_header.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::mo2 {

struct Diagnostic {
  enum class Severity { Info, Warning, Error };
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  std::string subject;
};

struct ResolvedPlugin {
  std::string name;
  bool active = false;
  bool implicit = false;  // official master or Creation Club entry from Skyrim.ccc
  std::optional<std::filesystem::path> path;
  std::string provider;   // MO2 mod (or "<Game Data>"/"<Overwrite>") that wins the file
  std::optional<PluginHeader> header;
};

// The plugin load order the game will actually use for a profile. This is a different axis from
// mod priority: mod priority decides which FILE wins, the load order decides which RECORD wins.
struct EffectiveLoadOrder {
  std::vector<ResolvedPlugin> active;    // in load order
  std::vector<ResolvedPlugin> inactive;  // present but not enabled
  std::vector<Diagnostic> diagnostics;
};

EffectiveLoadOrder resolve_load_order(const Profile& profile, const VirtualFileSystem& vfs, const GameInstall& game);

}  // namespace sns::mo2
