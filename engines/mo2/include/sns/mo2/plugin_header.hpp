#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sns::mo2 {

// The TES4 header record of a Skyrim SE plugin (first record of every .esm/.esp/.esl).
struct PluginHeader {
  std::uint32_t flags = 0;
  float version = 0.0f;  // HEDR: 1.70 (SE) / 1.71 (allows 0x000-0xFFF light range)
  std::uint32_t record_count = 0;
  std::string author;
  std::string description;
  std::vector<std::string> masters;

  bool master_flag = false;     // 0x001
  bool localized = false;       // 0x080
  bool light_flag = false;      // 0x200 (ESL)

  // What the game does with the file, combining flags and extension:
  bool is_master = false;  // ESM flag, or .esm/.esl extension
  bool is_light = false;   // ESL flag, or .esl extension
};

// Parses the header from raw bytes. `file_name` decides extension-based semantics.
// Throws sns::Error(Parse) for anything that is not a well-formed TES4 header.
PluginHeader parse_plugin_header(std::string_view bytes, std::string_view file_name);

// Reads only the header from disk (bounded read; never the whole plugin).
PluginHeader read_plugin_header(const std::filesystem::path& path);

}  // namespace sns::mo2
