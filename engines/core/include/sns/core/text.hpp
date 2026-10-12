#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sns::text {

// UTF-8 <-> UTF-16 for Win32 APIs. Invalid input throws sns::Error(Parse).
std::wstring to_wide(std::string_view utf8);
std::string to_utf8(std::wstring_view wide);

std::filesystem::path path_from_utf8(std::string_view utf8);
std::string path_to_utf8(const std::filesystem::path& path);

// Decodes a text file written by MO2 or the game: strips a UTF-8 BOM, accepts valid UTF-8 as is,
// otherwise interprets the bytes as Windows-1252 (older tools write plugins.txt that way).
std::string decode_text(std::string_view bytes);

bool is_valid_utf8(std::string_view bytes);

std::string to_lower_ascii(std::string_view s);
bool iequals_ascii(std::string_view a, std::string_view b);
bool ends_with_icase(std::string_view s, std::string_view suffix);
std::string_view trim(std::string_view s);

// Splits on \n, removing a trailing \r from each line.
std::vector<std::string_view> split_lines(std::string_view text);

// Canonical key for a path inside the game's Data folder: lower-case ASCII, '\' separators,
// no leading separator. Skyrim and MO2 treat Data paths case-insensitively.
std::string data_path_key(std::string_view relative);

// True for a non-empty relative path with no drive, root, or ".." component.
bool is_safe_relative_path(std::string_view relative);

}  // namespace sns::text
