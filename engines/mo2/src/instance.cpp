#include "sns/mo2/instance.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>

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

std::optional<fs::path> roaming_app_data() {
  wchar_t buffer[MAX_PATH * 2];
  const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
  if (n == 0 || n >= std::size(buffer)) return std::nullopt;
  return fs::path(buffer);
}

std::string normalize_path_key(const fs::path& p) {
  std::error_code ec;
  fs::path norm = fs::absolute(p, ec).lexically_normal();
  std::string s = text::to_lower_ascii(text::path_to_utf8(norm));
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
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

namespace {

void try_add_instance(const fs::path& path, std::vector<InstanceInfo>& out, std::set<std::string>& seen, const std::string& prefix = "") {
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  auto info = instance_from_path(path);
  if (!info) return;

  std::string key = normalize_path_key(info->ini);
  if (seen.insert(key).second) {
    if (!prefix.empty()) {
      info->name = prefix + ": " + info->name;
    }
    out.push_back(std::move(*info));
  }
}

void discover_from_running_processes(std::vector<InstanceInfo>& out, std::set<std::string>& seen) {
#ifdef _WIN32
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snapshot, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"ModOrganizer.exe") == 0) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (hProcess) {
          wchar_t exePath[MAX_PATH * 2] = {0};
          DWORD size = static_cast<DWORD>(std::size(exePath));
          if (QueryFullProcessImageNameW(hProcess, 0, exePath, &size)) {
            fs::path exe(exePath);
            try_add_instance(exe.parent_path(), out, seen, "Activa");
          }
          CloseHandle(hProcess);
        }
      }
    } while (Process32NextW(snapshot, &pe));
  }
  CloseHandle(snapshot);
#endif
}

#ifdef _WIN32
void check_reg_command(HKEY root, const wchar_t* subkey, std::vector<InstanceInfo>& out, std::set<std::string>& seen) {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    wchar_t val[MAX_PATH * 2] = {0};
    DWORD valSize = sizeof(val);
    DWORD type = 0;
    if (RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(val), &valSize) == ERROR_SUCCESS) {
      std::wstring cmd(val);
      auto q1 = cmd.find(L'"');
      auto q2 = (q1 != std::wstring::npos) ? cmd.find(L'"', q1 + 1) : std::wstring::npos;
      std::wstring pathStr;
      if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
        pathStr = cmd.substr(q1 + 1, q2 - q1 - 1);
      } else {
        auto sp = cmd.find(L' ');
        pathStr = (sp != std::wstring::npos) ? cmd.substr(0, sp) : cmd;
      }
      if (!pathStr.empty()) {
        fs::path p(pathStr);
        fs::path dir = p.parent_path();
        fs::path iniPath = dir / "nxmhandler.ini";
        std::error_code ec;
        if (fs::exists(iniPath, ec)) {
          auto iniDoc = IniDocument::parse(file_io::read_text_file(iniPath));
          for (int i = 1; i <= 20; ++i) {
            std::string key = std::to_string(i) + "\\executable";
            if (auto exeVal = iniDoc.value("handlers", key)) {
              if (!exeVal->empty()) {
                fs::path moExe = text::path_from_utf8(*exeVal);
                try_add_instance(moExe.parent_path(), out, seen);
              }
            }
          }
        }
        try_add_instance(dir, out, seen);
      }
    }
    RegCloseKey(hKey);
  }
}

void discover_from_registry(std::vector<InstanceInfo>& out, std::set<std::string>& seen) {
  check_reg_command(HKEY_CLASSES_ROOT, L"nxm\\shell\\open\\command", out, seen);
  check_reg_command(HKEY_CURRENT_USER, L"Software\\Classes\\nxm\\shell\\open\\command", out, seen);

  const wchar_t* subkeys[] = {
    L"Software\\Mod Organizer 2",
    L"Software\\Tannin\\Mod Organizer"
  };
  for (auto* sub : subkeys) {
    for (auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
      HKEY hKey = nullptr;
      if (RegOpenKeyExW(root, sub, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        for (const wchar_t* valName : {L"InstalledDir", L"InstallPath", L"Path", L""}) {
          wchar_t val[MAX_PATH * 2] = {0};
          DWORD valSize = sizeof(val);
          DWORD type = 0;
          if (RegQueryValueExW(hKey, valName[0] ? valName : nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(val), &valSize) == ERROR_SUCCESS) {
            if (val[0]) try_add_instance(fs::path(val), out, seen);
          }
        }
        RegCloseKey(hKey);
      }
    }
  }
}

void discover_from_drives(std::vector<InstanceInfo>& out, std::set<std::string>& seen) {
  DWORD driveMask = GetLogicalDrives();
  for (char letter = 'C'; letter <= 'Z'; ++letter) {
    if (!(driveMask & (1 << (letter - 'A')))) continue;
    std::string rootStr = std::string(1, letter) + ":\\";
    UINT driveType = GetDriveTypeA(rootStr.c_str());
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) continue;

    const std::vector<std::string> candidates = {
      "Apps",
      "Modding\\MO2",
      "Modding\\Mod Organizer 2",
      "Mod Organizer 2",
      "ModOrganizer",
      "MO2",
      "Games\\Mod Organizer 2",
      "Games\\MO2",
      "Games\\Modding\\MO2",
      "SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\MO2",
      "SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Mod Organizer 2",
      "Mods Skyrim"
    };

    for (const auto& rel : candidates) {
      fs::path p = fs::path(rootStr) / rel;
      std::error_code ec;
      if (fs::is_directory(p, ec)) {
        try_add_instance(p, out, seen);
      }
    }
  }
}
#endif

}  // namespace

std::vector<InstanceInfo> discover_all_instances() {
  std::vector<InstanceInfo> out;
  std::set<std::string> seen;

  // 1. Running instance (highest priority - user is actively running it)
  discover_from_running_processes(out, seen);

  // 2. Registry (Nexus handler and installer keys)
#ifdef _WIN32
  discover_from_registry(out, seen);
#endif

  // 3. Global AppData instances
  for (const auto& info : discover_global_instances()) {
    std::string key = normalize_path_key(info.ini);
    if (seen.insert(key).second) {
      out.push_back(info);
    }
  }

  // Also roaming AppData if present
  if (const auto roam = roaming_app_data()) {
    for (const auto& info : discover_instances_in(*roam / "ModOrganizer")) {
      std::string key = normalize_path_key(info.ini);
      if (seen.insert(key).second) {
        out.push_back(info);
      }
    }
  }

  // 4. Common portable directories on local drives
#ifdef _WIN32
  discover_from_drives(out, seen);
#endif

  return out;
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
