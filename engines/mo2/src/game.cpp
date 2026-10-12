#include "sns/mo2/game.hpp"

#include <windows.h>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace fs = std::filesystem;

namespace {

std::string file_version(const fs::path& exe) {
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
  if (size == 0) return {};
  std::string buffer(size, '\0');
  if (!GetFileVersionInfoW(exe.c_str(), 0, size, buffer.data())) return {};
  VS_FIXEDFILEINFO* info = nullptr;
  UINT len = 0;
  if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) return {};
  return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
         std::to_string(HIWORD(info->dwFileVersionLS)) + "." + std::to_string(LOWORD(info->dwFileVersionLS));
}

}  // namespace

GameInstall inspect_game(const fs::path& game_root) {
  GameInstall g;
  g.root = game_root;
  g.data = game_root / "Data";
  std::error_code ec;
  const auto exe = game_root / "SkyrimSE.exe";
  g.exists = fs::is_regular_file(exe, ec) && fs::is_directory(g.data, ec);
  if (!g.exists) return g;
  g.exe_version = file_version(exe);
  if (g.exe_version.starts_with("1.5.")) g.edition = SkyrimEdition::SpecialEdition;
  if (g.exe_version.starts_with("1.6.")) g.edition = SkyrimEdition::AnniversaryEdition;
  return g;
}

const std::vector<std::string>& implicit_masters() {
  static const std::vector<std::string> masters = {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
                                                    "Dragonborn.esm"};
  return masters;
}

std::vector<std::string> creation_club_plugins(const GameInstall& game) {
  std::vector<std::string> out;
  std::error_code ec;
  const auto ccc = game.root / "Skyrim.ccc";
  if (!fs::is_regular_file(ccc, ec)) return out;
  // Keep the buffer alive: split_lines returns views into it.
  const auto content = file_io::read_text_file(ccc, 1024 * 1024);
  for (auto line : text::split_lines(content)) {
    line = text::trim(line);
    if (!line.empty()) out.emplace_back(line);
  }
  return out;
}

std::string to_string(SkyrimEdition edition) {
  switch (edition) {
    case SkyrimEdition::SpecialEdition: return "Special Edition";
    case SkyrimEdition::AnniversaryEdition: return "Anniversary Edition";
    default: return "Unknown";
  }
}

}  // namespace sns::mo2
