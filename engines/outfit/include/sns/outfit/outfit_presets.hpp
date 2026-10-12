#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sns/outfit/outfit_draft.hpp"
#include "sns/projects/project.hpp"

namespace sns::outfit {

struct OutfitPreset {
  std::string id;
  std::string name;
  std::string category;     // e.g. "Warrior", "Mage", "Hunter", "Noble", "Casual", "Armor"
  std::string description;
  std::vector<Piece> pieces;

  bool operator==(const OutfitPreset&) const = default;
};

// Returns standard builtin archetypes using vanilla Skyrim items.
std::vector<OutfitPreset> builtin_outfit_presets();

// Load / save preset files (.json)
std::vector<OutfitPreset> load_outfit_presets_from_dir(const std::filesystem::path& dir);
bool save_outfit_preset_to_file(const std::filesystem::path& file_path, const OutfitPreset& preset);

// Comprehensive biped slot helper functions (Skyrim slots 30..61)
std::string slot_display_name(int slot);
std::string slot_category_name(int slot);
std::uint32_t slot_bit(int slot);

}  // namespace sns::outfit
