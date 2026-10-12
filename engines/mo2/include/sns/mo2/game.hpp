#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sns::mo2 {

enum class SkyrimEdition { Unknown, SpecialEdition, AnniversaryEdition };

struct GameInstall {
  std::filesystem::path root;
  std::filesystem::path data;
  std::string exe_version;  // "1.6.1170.0"
  SkyrimEdition edition = SkyrimEdition::Unknown;
  bool exists = false;
};

// Inspects SkyrimSE.exe: 1.5.x is Special Edition, 1.6.x Anniversary Edition (the AE update).
GameInstall inspect_game(const std::filesystem::path& game_root);

// Official masters that the game loads implicitly, in order, before plugins.txt entries.
const std::vector<std::string>& implicit_masters();

// Creation Club plugins listed in <game>/Skyrim.ccc (loaded implicitly after the official masters).
std::vector<std::string> creation_club_plugins(const GameInstall& game);

std::string to_string(SkyrimEdition edition);

}  // namespace sns::mo2
