#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sns::assets {

// Read-only Bethesda archive (BSA) for Skyrim: version 105 (Special Edition, LZ4-frame compressed)
// and version 104 (original Skyrim; only uncompressed files are readable, zlib is not supported).
// Opening reads only the directory; file data is read on demand. Thread-safe for concurrent reads.
class BsaArchive {
 public:
  static constexpr std::uint64_t kMaxFileBytes = 512ull * 1024 * 1024;

  // Throws sns::Error(Parse) for anything that is not a well-formed BSA.
  static std::unique_ptr<BsaArchive> open(const std::filesystem::path& path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
  [[nodiscard]] std::size_t file_count() const noexcept { return entries_.size(); }

  // Keys are Data-relative, lower-case, '\' separated ("meshes\actors\character\...\skeleton.nif").
  [[nodiscard]] bool contains(std::string_view data_path) const;
  // Decompressed file contents, or nullopt if the archive does not contain the path.
  [[nodiscard]] std::optional<std::string> read(std::string_view data_path) const;
  [[nodiscard]] std::vector<std::string> list() const;

 private:
  struct Entry {
    std::uint64_t offset = 0;
    std::uint32_t size = 0;
    bool compressed = false;
  };

  BsaArchive() = default;
  std::string read_entry(const Entry& entry, std::string_view key) const;

  std::filesystem::path path_;
  std::uint32_t version_ = 0;
  std::uint32_t flags_ = 0;
  std::uint64_t file_size_ = 0;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace sns::assets
