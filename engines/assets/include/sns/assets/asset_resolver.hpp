#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/assets/bsa_archive.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::assets {

struct ArchiveRef {
  std::string name;               // "Skyrim - Meshes0.bsa"
  std::string provider;           // MO2 mod (or "<Game Data>") whose copy of the archive wins
  std::filesystem::path path;
  std::string reason;             // "ini" or the plugin that loads it
};

struct AssetLocation {
  enum class Kind { Loose, Archive };
  Kind kind = Kind::Loose;
  std::string provider;  // mod name for loose files, archive file name for archives
  std::string archive_provider;  // for archives: the mod that provides the archive
};

// The archives the game will load for a profile, in ascending priority:
// 1. sResourceArchiveList + sResourceArchiveList2 from the effective Skyrim.ini (vanilla archives);
// 2. "<plugin>.bsa" and "<plugin> - Textures.bsa" for every active plugin, in load order.
std::vector<ArchiveRef> archive_load_order(const mo2::Profile& profile, const mo2::EffectiveLoadOrder& load_order,
                                           const mo2::VirtualFileSystem& vfs,
                                           const std::optional<std::filesystem::path>& my_games_override = std::nullopt);

// Looks at a BSA that failed to open: a file whose header is not "BSA\0" is reported as damaged,
// with evidence (header bytes, size, share of zero bytes) and an external repair recommendation.
struct ArchiveDiagnosis {
  bool corrupt = false;
  std::string message;
};
ArchiveDiagnosis diagnose_archive(const std::filesystem::path& path);

// Resolves Data-relative assets like the game: a loose file (from MO2's virtual Data folder) always
// wins; otherwise the last loaded archive that contains it. Thread-safe for reads once built.
class AssetResolver {
 public:
  struct OpenIssue {
    std::string archive;
    std::string code;  // "ArchiveCorrupt" (damaged file) or "ArchiveUnreadable" (unsupported/other)
    std::string message;
  };

  // Opens every archive (directory only). Unreadable archives are skipped and reported.
  AssetResolver(const mo2::VirtualFileSystem& vfs, const std::vector<ArchiveRef>& archives,
                const std::function<bool()>& cancelled = {});

  [[nodiscard]] std::optional<AssetLocation> locate(std::string_view data_path) const;
  // Bytes of the winning copy, or nullopt if no loose file or archive has it.
  [[nodiscard]] std::optional<std::string> read(std::string_view data_path) const;

  [[nodiscard]] const std::vector<OpenIssue>& issues() const noexcept { return issues_; }
  [[nodiscard]] std::size_t archive_count() const noexcept { return archives_.size(); }
  [[nodiscard]] std::size_t archived_file_count() const noexcept { return archived_files_; }

 private:
  struct Opened {
    ArchiveRef ref;
    std::unique_ptr<BsaArchive> archive;
  };
  const mo2::VirtualFileSystem* vfs_;
  std::vector<Opened> archives_;  // ascending priority
  std::vector<OpenIssue> issues_;
  std::size_t archived_files_ = 0;
};

}  // namespace sns::assets
