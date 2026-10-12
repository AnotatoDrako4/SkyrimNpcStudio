#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sns::file_io {

// Default ceiling for text files we parse (ini, modlist, project json): 32 MiB.
inline constexpr std::uintmax_t kDefaultMaxBytes = 32ull * 1024 * 1024;

// Reads a whole file as bytes. Throws sns::Error(NotFound/Io/InvalidArgument when too large).
std::string read_file(const std::filesystem::path& path, std::uintmax_t max_bytes = kDefaultMaxBytes);

// Reads a text file and normalizes its encoding to UTF-8 (see text::decode_text).
std::string read_text_file(const std::filesystem::path& path, std::uintmax_t max_bytes = kDefaultMaxBytes);

// Writes through a temporary file in the same directory, flushes it, then atomically replaces
// the destination (MoveFileEx with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH).
// Readers see either the old or the new content, never a partial file.
void write_file_atomic(const std::filesystem::path& path, std::string_view content);

}  // namespace sns::file_io
