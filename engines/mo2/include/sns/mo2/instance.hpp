#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sns::mo2 {

enum class InstanceKind { Global, Portable };

struct InstanceInfo {
  std::string name;
  InstanceKind kind = InstanceKind::Global;
  std::filesystem::path root;  // folder that contains ModOrganizer.ini
  std::filesystem::path ini;   // ModOrganizer.ini
};

// Paths and settings of an instance, with %BASE_DIR% resolved. Read-only: nothing is written.
struct InstanceConfig {
  InstanceInfo info;
  std::string game_name;  // "Skyrim Special Edition"
  std::string game_edition;
  std::filesystem::path game_path;
  std::string selected_profile;
  std::string mo2_version;
  std::filesystem::path base_dir;
  std::filesystem::path mods_dir;
  std::filesystem::path profiles_dir;
  std::filesystem::path overwrite_dir;
  std::filesystem::path downloads_dir;
  std::vector<std::string> skip_file_suffixes;  // e.g. ".mohidden"
  std::vector<std::string> skip_directories;    // e.g. ".git"
};

// Global instances live in %LOCALAPPDATA%\ModOrganizer\<name>\ModOrganizer.ini.
std::vector<InstanceInfo> discover_global_instances();

// Comprehensive discovery: active running MO2 process, registry, standard/portable locations,
// and global AppData instances. Deduplicated and verified.
std::vector<InstanceInfo> discover_all_instances();

// Same, from an explicit root (testable without touching the real profile).
std::vector<InstanceInfo> discover_instances_in(const std::filesystem::path& mod_organizer_appdata);

// Accepts a ModOrganizer.ini, its folder, or a portable MO2 installation folder.
std::optional<InstanceInfo> instance_from_path(const std::filesystem::path& path);

InstanceConfig load_instance_config(const InstanceInfo& instance);

bool is_supported_game(const InstanceConfig& config);

}  // namespace sns::mo2
