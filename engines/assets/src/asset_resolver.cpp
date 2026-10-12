#include "sns/assets/asset_resolver.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"

namespace sns::assets {
namespace {

// Skyrim SE defaults when no Skyrim.ini provides the lists.
const std::vector<std::string>& default_archives() {
  static const std::vector<std::string> list = {
      "Skyrim - Misc.bsa",      "Skyrim - Shaders.bsa",   "Skyrim - Interface.bsa", "Skyrim - Animations.bsa",
      "Skyrim - Meshes0.bsa",   "Skyrim - Meshes1.bsa",   "Skyrim - Sounds.bsa",    "Skyrim - Voices_en0.bsa",
      "Skyrim - Textures0.bsa", "Skyrim - Textures1.bsa", "Skyrim - Textures2.bsa", "Skyrim - Textures3.bsa",
      "Skyrim - Textures4.bsa", "Skyrim - Textures5.bsa", "Skyrim - Textures6.bsa", "Skyrim - Textures7.bsa",
      "Skyrim - Textures8.bsa", "Skyrim - Patch.bsa"};
  return list;
}

std::vector<std::string> split_archive_list(const std::string& value) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= value.size()) {
    auto end = value.find(',', start);
    if (end == std::string::npos) end = value.size();
    const auto item = text::trim(std::string_view(value).substr(start, end - start));
    if (!item.empty()) out.emplace_back(item);
    start = end + 1;
  }
  return out;
}

std::string strip_extension(const std::string& name) {
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

std::vector<ArchiveRef> archive_load_order(const mo2::Profile& profile, const mo2::EffectiveLoadOrder& load_order,
                                           const mo2::VirtualFileSystem& vfs,
                                           const std::optional<std::filesystem::path>& my_games_override) {
  std::vector<ArchiveRef> out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::string& name, const std::string& reason) {
    if (!seen.insert(text::to_lower_ascii(name)).second) return;
    const auto file = vfs.resolve(name);
    if (!file) return;
    out.push_back({name, vfs.providers()[file->provider].name, file->absolute, reason});
  };

  std::vector<std::string> ini;
  for (const auto* key : {"sResourceArchiveList", "sResourceArchiveList2"}) {
    if (auto value = mo2::game_ini_value(profile, "Archive", key, my_games_override)) {
      for (auto& name : split_archive_list(*value)) ini.push_back(std::move(name));
    }
  }
  if (ini.empty()) ini = default_archives();
  for (const auto& name : ini) add(name, "ini");

  for (const auto& plugin : load_order.active) {
    const auto base = strip_extension(plugin.name);
    add(base + ".bsa", plugin.name);
    add(base + " - Textures.bsa", plugin.name);
  }
  return out;
}

AssetResolver::AssetResolver(const mo2::VirtualFileSystem& vfs, const std::vector<ArchiveRef>& archives,
                             const std::function<bool()>& cancelled)
    : vfs_(&vfs) {
  for (const auto& ref : archives) {
    if (cancelled && cancelled()) throw Error(ErrorKind::Cancelled, "archive indexing cancelled");
    try {
      auto archive = BsaArchive::open(ref.path);
      archived_files_ += archive->file_count();
      archives_.push_back({ref, std::move(archive)});
    } catch (const Error& e) {
      auto diagnosis = diagnose_archive(ref.path);
      issues_.push_back({ref.name, diagnosis.corrupt ? "ArchiveCorrupt" : "ArchiveUnreadable",
                         diagnosis.corrupt ? diagnosis.message : std::string(e.what())});
    }
  }
}

ArchiveDiagnosis diagnose_archive(const std::filesystem::path& path) {
  ArchiveDiagnosis d;
  const auto name = text::path_to_utf8(path.filename());
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  std::ifstream in(path, std::ios::binary);
  if (ec || !in) {
    d.message = name + " cannot be opened";
    return d;
  }
  // Header plus two 64 KiB samples (start and middle): enough to tell a zero-filled file apart.
  std::string head(16, '\0');
  in.read(head.data(), 16);
  head.resize(static_cast<std::size_t>(in.gcount()));
  std::size_t zeros = 0, sampled = 0;
  for (const std::uint64_t at : {std::uint64_t{0}, size / 2}) {
    std::string block(64 * 1024, '\0');
    in.clear();
    in.seekg(static_cast<std::streamoff>(at));
    in.read(block.data(), static_cast<std::streamsize>(block.size()));
    const auto got = static_cast<std::size_t>(in.gcount());
    zeros += static_cast<std::size_t>(std::count(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got), '\0'));
    sampled += got;
  }
  std::string hex;
  for (const char c : head.substr(0, 8)) {
    static constexpr char digits[] = "0123456789ABCDEF";
    hex += digits[(static_cast<unsigned char>(c) >> 4) & 0xF];
    hex += digits[static_cast<unsigned char>(c) & 0xF];
  }
  d.corrupt = head.size() < 4 || head.compare(0, 4, std::string("BSA\0", 4)) != 0;
  if (!d.corrupt) return d;
  const auto zero_pct = sampled ? 100 * zeros / sampled : 100;
  d.message = name + " is not a valid BSA: header bytes " + hex + " instead of 42534100 ('BSA'), " +
              std::to_string(size / (1024 * 1024)) + " MB, " + std::to_string(zero_pct) +
              "% of sampled bytes are zero. The file is damaged or incomplete; only resources inside it are "
              "affected. Repair it outside the studio (Steam: Properties > Installed Files > Verify integrity of "
              "game files). The studio never modifies game files.";
  return d;
}

std::optional<AssetLocation> AssetResolver::locate(std::string_view data_path) const {
  if (auto file = vfs_->resolve(data_path)) {
    return AssetLocation{AssetLocation::Kind::Loose, vfs_->providers()[file->provider].name, {}};
  }
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
    if (it->archive->contains(data_path)) {
      return AssetLocation{AssetLocation::Kind::Archive, it->ref.name, it->ref.provider};
    }
  }
  return std::nullopt;
}

std::optional<std::string> AssetResolver::read(std::string_view data_path) const {
  if (auto file = vfs_->resolve(data_path)) return file_io::read_file(file->absolute, BsaArchive::kMaxFileBytes);
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
    if (auto data = it->archive->read(data_path)) return data;
  }
  return std::nullopt;
}

}  // namespace sns::assets
