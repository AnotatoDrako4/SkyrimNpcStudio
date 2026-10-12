#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace sns::test {

// A unique directory under %TEMP% removed on destruction.
class TempDir {
 public:
  TempDir() {
    std::random_device rd;
    path_ = std::filesystem::temp_directory_path() /
            ("sns-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(rd()));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  // Writes a file (creating parent folders) and returns its path.
  std::filesystem::path write(const std::filesystem::path& relative, std::string_view content) const {
    const auto full = path_ / relative;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return full;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace sns::test
