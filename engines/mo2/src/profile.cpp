#include "sns/mo2/profile.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/ini.hpp"

namespace sns::mo2 {
namespace fs = std::filesystem;

std::vector<ModEntry> parse_modlist(std::string_view content) {
  std::vector<ModEntry> top_down;
  for (auto line : text::split_lines(content)) {
    line = text::trim(line);
    if (line.empty() || line.front() == '#') continue;
    ModEntry e;
    const char marker = line.front();
    e.name = std::string(text::trim(line.substr(1)));
    if (e.name.empty()) continue;
    switch (marker) {
      case '+': e.enabled = true; break;
      case '-': e.enabled = false; break;
      case '*':
        e.enabled = true;
        e.kind = ModKind::Foreign;
        break;
      default: continue;  // not a modlist line
    }
    if (e.kind == ModKind::Regular && e.name.ends_with("_separator")) e.kind = ModKind::Separator;
    top_down.push_back(std::move(e));
  }
  // First line = highest priority. Reverse so index == priority.
  std::reverse(top_down.begin(), top_down.end());
  for (size_t i = 0; i < top_down.size(); ++i) top_down[i].priority = static_cast<int>(i);
  return top_down;
}

std::vector<PluginListEntry> parse_plugins_txt(std::string_view content) {
  std::vector<PluginListEntry> out;
  for (auto line : text::split_lines(content)) {
    line = text::trim(line);
    if (line.empty() || line.front() == '#') continue;
    const bool active = line.front() == '*';
    if (active) line.remove_prefix(1);
    if (!line.empty()) out.push_back({std::string(line), active});
  }
  return out;
}

std::vector<std::string> parse_loadorder_txt(std::string_view content) {
  std::vector<std::string> out;
  for (auto line : text::split_lines(content)) {
    line = text::trim(line);
    if (!line.empty() && line.front() != '#') out.emplace_back(line);
  }
  return out;
}

std::vector<std::string> list_profiles(const InstanceConfig& config) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(config.profiles_dir, ec)) {
    if (entry.is_directory(ec) && fs::exists(entry.path() / "modlist.txt", ec)) {
      out.push_back(text::path_to_utf8(entry.path().filename()));
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

Profile load_profile(const InstanceConfig& config, std::string_view name) {
  if (!text::is_safe_relative_path(name) || name.find_first_of("\\/") != std::string_view::npos) {
    throw Error(ErrorKind::InvalidArgument, "invalid profile name '" + std::string(name) + "'");
  }
  Profile p;
  p.name = std::string(name);
  p.dir = config.profiles_dir / text::path_from_utf8(name);
  std::error_code ec;
  if (!fs::is_directory(p.dir, ec)) throw Error(ErrorKind::NotFound, "profile '" + p.name + "' does not exist");

  p.mods = parse_modlist(file_io::read_text_file(p.dir / "modlist.txt"));
  if (fs::exists(p.dir / "plugins.txt", ec)) p.plugins = parse_plugins_txt(file_io::read_text_file(p.dir / "plugins.txt"));
  if (fs::exists(p.dir / "loadorder.txt", ec)) {
    p.load_order = parse_loadorder_txt(file_io::read_text_file(p.dir / "loadorder.txt"));
  }
  if (fs::exists(p.dir / "settings.ini", ec)) {
    const auto ini = IniDocument::parse(file_io::read_text_file(p.dir / "settings.ini"));
    p.settings.local_saves = ini.value("General", "LocalSaves").value_or("false") == "true";
    p.settings.local_settings = ini.value("General", "LocalSettings").value_or("false") == "true";
    p.settings.archive_invalidation = ini.value("General", "AutomaticArchiveInvalidation").value_or("false") == "true";
  }
  return p;
}

namespace {

std::optional<fs::path> documents_my_games() {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw))) return std::nullopt;
  fs::path docs(raw);
  CoTaskMemFree(raw);
  // Steam and GOG builds use different folder names.
  for (const auto* name : {L"Skyrim Special Edition", L"Skyrim Special Edition GOG", L"Skyrim Special Edition EPIC"}) {
    std::error_code ec;
    const auto dir = docs / L"My Games" / name;
    if (fs::exists(dir / L"Skyrim.ini", ec)) return dir;
  }
  return std::nullopt;
}

std::optional<std::string> value_from(const fs::path& dir, std::string_view section, std::string_view key) {
  std::optional<std::string> result;
  std::error_code ec;
  // SkyrimCustom.ini is applied after Skyrim.ini and wins.
  for (const auto* name : {L"Skyrim.ini", L"SkyrimCustom.ini"}) {
    const auto file = dir / name;
    if (!fs::exists(file, ec)) continue;
    if (auto value = IniDocument::parse(file_io::read_text_file(file)).value(section, key)) {
      if (!value->empty()) result = *value;
    }
  }
  return result;
}

}  // namespace

std::optional<std::string> game_ini_value(const Profile& profile, std::string_view section, std::string_view key,
                                          const std::optional<fs::path>& my_games_override) {
  if (profile.settings.local_settings) {
    if (auto value = value_from(profile.dir, section, key)) return value;
  }
  const auto global = my_games_override ? my_games_override : documents_my_games();
  if (global) {
    if (auto value = value_from(*global, section, key)) return value;
  }
  return value_from(profile.dir, section, key);
}

std::string detect_game_language(const Profile& profile, const std::optional<fs::path>& my_games_override) {
  return game_ini_value(profile, "General", "sLanguage", my_games_override).value_or("ENGLISH");
}

}  // namespace sns::mo2
