#include "sns/assets/asset_resolver.hpp"

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
      issues_.push_back({ref.name, e.what()});
    }
  }
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
