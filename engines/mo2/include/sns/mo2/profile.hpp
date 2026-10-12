#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/mo2/instance.hpp"

namespace sns::mo2 {

enum class ModKind {
  Regular,    // folder under mods/
  Separator,  // "<name>_separator": UI grouping only, contributes no files
  Foreign,    // "*DLC: ...", "*Creation Club: ...", "*Unmanaged: ...": files live in the game's Data
};

struct ModEntry {
  std::string name;
  bool enabled = false;
  ModKind kind = ModKind::Regular;
  // 0 = lowest priority. Higher priority wins file conflicts (MO2's left pane order).
  int priority = 0;
};

struct PluginListEntry {
  std::string name;
  bool active = false;
};

struct ProfileSettings {
  bool local_saves = false;
  bool local_settings = false;
  bool archive_invalidation = false;
};

struct Profile {
  std::string name;
  std::filesystem::path dir;
  std::vector<ModEntry> mods;              // ascending priority
  std::vector<PluginListEntry> plugins;    // plugins.txt, file order
  std::vector<std::string> load_order;     // loadorder.txt, file order
  ProfileSettings settings;
};

// modlist.txt lists the HIGHEST priority mod first; the result is in ascending priority.
std::vector<ModEntry> parse_modlist(std::string_view text);
std::vector<PluginListEntry> parse_plugins_txt(std::string_view text);
std::vector<std::string> parse_loadorder_txt(std::string_view text);

std::vector<std::string> list_profiles(const InstanceConfig& config);
Profile load_profile(const InstanceConfig& config, std::string_view name);

// The game's sLanguage ("SPANISH", "ENGLISH"...), which selects the localized .STRINGS files.
// Reads the profile's own Skyrim.ini/SkyrimCustom.ini when the profile uses local settings,
// otherwise Documents\My Games\<game folder>\, falling back to the profile copies when the global
// files do not exist. `my_games_override` replaces the Documents\My Games\<game> folder (tests).
std::string detect_game_language(const Profile& profile,
                                 const std::optional<std::filesystem::path>& my_games_override = std::nullopt);

// A value from the effective Skyrim.ini/SkyrimCustom.ini, resolved like detect_game_language.
std::optional<std::string> game_ini_value(const Profile& profile, std::string_view section, std::string_view key,
                                          const std::optional<std::filesystem::path>& my_games_override = std::nullopt);

}  // namespace sns::mo2
