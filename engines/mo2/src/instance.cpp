#include "sns/mo2/instance.hpp"

#include <windows.h>

#include <cstdlib>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/ini.hpp"

namespace sns::mo2 {
namespace fs = std::filesystem;

namespace {

constexpr const char* kIniName = "ModOrganizer.ini";

std::optional<fs::path> local_app_data() {
  wchar_t buffer[MAX_PATH * 2];
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
  if (n == 0 || n >= std::size(buffer)) return std::nullopt;
  return fs::path(buffer);
}

// MO2 stores directories either absolute or with the %BASE_DIR% placeholder.
fs::path resolve_dir(const IniDocument& ini, const char* key, const fs::path& base, const char* default_leaf) {
  auto value = ini.value("Settings", key);
  if (!value || value->empty()) return base / default_leaf;
  std::string v = *value;
  const std::string placeholder = "%BASE_DIR%";
  if (const auto pos = v.find(placeholder); pos != std::string::npos) {
    v.replace(pos, placeholder.size(), text::path_to_utf8(base));
  }
  return text::path_from_utf8(v).lexically_normal();
}

std::vector<std::string> split_list(const std::optional<std::string>& value) {
  std::vector<std::string> out;
  if (!value) return out;
  size_t start = 0;
  while (start <= value->size()) {
    auto end = value->find_first_of(";,", start);
    if (end == std::string::npos) end = value->size();
    auto item = text::trim(std::string_view(*value).substr(start, end - start));
    if (!item.empty()) out.emplace_back(item);
    start = end + 1;
  }
  return out;
}

}  // namespace

std::vector<InstanceInfo> discover_instances_in(const fs::path& mod_organizer_appdata) {
  std::vector<InstanceInfo> out;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(mod_organizer_appdata, ec)) {
    if (!entry.is_directory(ec)) continue;
    const auto ini = entry.path() / kIniName;
    if (fs::is_regular_file(ini, ec)) {
      out.push_back({text::path_to_utf8(entry.path().filename()), InstanceKind::Global, entry.path(), ini});
    }
  }
  return out;
}

std::vector<InstanceInfo> discover_global_instances() {
  const auto appdata = local_app_data();
  if (!appdata) return {};
  return discover_instances_in(*appdata / "ModOrganizer");
}

std::optional<InstanceInfo> instance_from_path(const fs::path& path) {
  std::error_code ec;
  fs::path ini = path;
  if (fs::is_directory(path, ec)) ini = path / kIniName;
  if (!fs::is_regular_file(ini, ec) || !text::iequals_ascii(text::path_to_utf8(ini.filename()), kIniName)) {
    return std::nullopt;
  }
  const auto root = ini.parent_path();
  // A folder that holds ModOrganizer.exe next to its ini is a portable instance.
  const bool portable = fs::exists(root / "ModOrganizer.exe", ec) || fs::exists(root / "portable.txt", ec);
  return InstanceInfo{portable ? "Portable (" + text::path_to_utf8(root.filename()) + ")"
                               : text::path_to_utf8(root.filename()),
                      portable ? InstanceKind::Portable : InstanceKind::Global, root, ini};
}

InstanceConfig load_instance_config(const InstanceInfo& instance) {
  const auto ini = IniDocument::parse(file_io::read_text_file(instance.ini));
  InstanceConfig c;
  c.info = instance;
  c.game_name = ini.value("General", "gameName").value_or("");
  c.game_edition = ini.value("General", "game_edition").value_or("");
  c.game_path = text::path_from_utf8(ini.value("General", "gamePath").value_or(""));
  c.selected_profile = ini.value("General", "selected_profile").value_or("Default");
  c.mo2_version = ini.value("General", "version").value_or("");

  auto base = ini.value("Settings", "base_directory");
  c.base_dir = (base && !base->empty()) ? text::path_from_utf8(*base).lexically_normal() : instance.root;
  c.mods_dir = resolve_dir(ini, "mod_directory", c.base_dir, "mods");
  c.profiles_dir = resolve_dir(ini, "profiles_directory", c.base_dir, "profiles");
  c.overwrite_dir = resolve_dir(ini, "overwrite_directory", c.base_dir, "overwrite");
  c.downloads_dir = resolve_dir(ini, "download_directory", c.base_dir, "downloads");

  c.skip_file_suffixes = split_list(ini.value("Settings", "skip_file_suffixes"));
  if (c.skip_file_suffixes.empty()) c.skip_file_suffixes = {".mohidden"};
  c.skip_directories = split_list(ini.value("Settings", "skip_directories"));
  if (c.game_path.empty()) throw Error(ErrorKind::Parse, "ModOrganizer.ini has no gamePath");
  return c;
}

bool is_supported_game(const InstanceConfig& config) {
  return config.game_name == "Skyrim Special Edition";
}

}  // namespace sns::mo2
