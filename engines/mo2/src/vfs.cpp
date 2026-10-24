#include "sns/mo2/vfs.hpp"

#include <algorithm>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace fs = std::filesystem;

namespace {

bool has_skipped_suffix(std::string_view name, const std::vector<std::string>& suffixes) {
  return std::any_of(suffixes.begin(), suffixes.end(),
                     [&](const std::string& s) { return !s.empty() && text::ends_with_icase(name, s); });
}

bool is_skipped_dir(std::string_view name, const std::vector<std::string>& dirs) {
  return std::any_of(dirs.begin(), dirs.end(), [&](const std::string& d) { return text::iequals_ascii(name, d); });
}

}  // namespace

VirtualFileSystem VirtualFileSystem::build_from(std::vector<Provider> providers, const VfsBuildOptions& options) {
  VirtualFileSystem vfs;
  vfs.providers_ = std::move(providers);
  vfs.files_.reserve(1u << 16);

  for (std::uint32_t index = 0; index < vfs.providers_.size(); ++index) {
    auto& provider = vfs.providers_[index];
    if (options.cancelled && options.cancelled()) throw Error(ErrorKind::Cancelled, "virtual file system build cancelled");
    if (options.progress) options.progress(index, vfs.providers_.size(), provider.name);

    std::error_code ec;
    if (!fs::is_directory(provider.root, ec)) continue;
    fs::recursive_directory_iterator it(provider.root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const auto& entry = *it;
      const auto name = text::path_to_utf8(entry.path().filename());
      if (entry.is_directory(ec)) {
        if (is_skipped_dir(name, options.skip_directories)) it.disable_recursion_pending();
        continue;
      }
      if (!entry.is_regular_file(ec)) continue;
      if (has_skipped_suffix(name, options.skip_file_suffixes)) continue;

      auto relative = text::path_to_utf8(entry.path().lexically_relative(provider.root));
      // meta.ini at a mod's root is MO2 metadata, never deployed to Data.
      if (provider.kind == ProviderKind::Mod && text::iequals_ascii(relative, "meta.ini")) continue;

      auto& file = vfs.files_[text::data_path_key(relative)];
      if (file.display.empty()) file.display = std::move(relative);
      file.providers.push_back(index);
      ++provider.file_count;
    }
  }
  if (options.progress) options.progress(vfs.providers_.size(), vfs.providers_.size(), {});
  return vfs;
}

VirtualFileSystem VirtualFileSystem::build(const InstanceConfig& config, const Profile& profile, const GameInstall& game,
                                           VfsBuildOptions options) {
  std::vector<Provider> providers;
  providers.push_back({"<Game Data>", game.data, ProviderKind::GameData});
  for (const auto& mod : profile.mods) {  // ascending priority
    if (!mod.enabled || mod.kind != ModKind::Regular) continue;
    providers.push_back({mod.name, config.mods_dir / text::path_from_utf8(mod.name), ProviderKind::Mod});
  }
  providers.push_back({"<Overwrite>", config.overwrite_dir, ProviderKind::Overwrite});
  if (options.skip_file_suffixes.empty()) options.skip_file_suffixes = config.skip_file_suffixes;
  if (options.skip_directories.empty()) options.skip_directories = config.skip_directories;
  return build_from(std::move(providers), options);
}

std::optional<ResolvedFile> VirtualFileSystem::resolve(std::string_view data_relative) const {
  const auto it = files_.find(text::data_path_key(data_relative));
  if (it == files_.end() || it->second.providers.empty()) return std::nullopt;
  const auto winner = it->second.providers.back();
  return ResolvedFile{providers_[winner].root / text::path_from_utf8(it->second.display), winner};
}

std::span<const std::uint32_t> VirtualFileSystem::providers_of(std::string_view data_relative) const {
  const auto it = files_.find(text::data_path_key(data_relative));
  if (it == files_.end()) return {};
  return it->second.providers;
}

std::optional<std::uint32_t> VirtualFileSystem::find_provider(std::string_view name) const {
  for (std::uint32_t i = 0; i < providers_.size(); ++i) {
    if (providers_[i].name == name) return i;
  }
  return std::nullopt;
}

ProviderConflicts VirtualFileSystem::conflicts(std::uint32_t provider, std::size_t sample_limit) const {
  ProviderConflicts c;
  for (const auto& [key, entry] : files_) {
    const auto pos = std::find(entry.providers.begin(), entry.providers.end(), provider);
    if (pos == entry.providers.end()) continue;
    ++c.files;
    if (entry.providers.size() < 2) continue;
    if (entry.providers.back() == provider) {
      if (c.overrides.size() < sample_limit) c.overrides.push_back(entry.display);
      ++c.overrides_total;
    } else {
      if (c.overridden.size() < sample_limit) c.overridden.push_back(entry.display);
      ++c.overridden_total;
    }
  }
  std::sort(c.overrides.begin(), c.overrides.end());
  std::sort(c.overridden.begin(), c.overridden.end());
  return c;
}

std::vector<std::string> VirtualFileSystem::root_plugins() const {
  std::vector<std::string> out;
  for (const auto& [key, entry] : files_) {
    if (key.find('\\') != std::string::npos) continue;
    if (text::ends_with_icase(key, ".esp") || text::ends_with_icase(key, ".esm") || text::ends_with_icase(key, ".esl")) {
      out.push_back(entry.display);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> VirtualFileSystem::list_directory(std::string_view data_relative_dir, bool recursive) const {
  auto prefix = text::data_path_key(data_relative_dir);
  while (!prefix.empty() && prefix.back() == '\\') prefix.pop_back();
  if (!prefix.empty()) prefix += '\\';
  std::vector<std::string> out;
  for (const auto& [key, entry] : files_) {
    if (entry.providers.empty() || key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
    if (!recursive && key.find('\\', prefix.size()) != std::string::npos) continue;
    out.push_back(entry.display);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace sns::mo2
