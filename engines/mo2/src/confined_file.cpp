#include "sns/mo2/confined_file.hpp"

#include <string>
#include <system_error>

#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace {

// Lower-case (ASCII, like the paths of mods), '/' separated, so that two spellings of one folder compare equal.
std::string normalized(const std::filesystem::path& path) {
  auto s = text::to_lower_ascii(text::path_to_utf8(path));
  for (auto& c : s) {
    if (c == '\\') c = '/';
  }
  return s;
}

// True when `path` is strictly inside `root`.
bool inside(const std::filesystem::path& path, const std::filesystem::path& root) {
  const auto a = normalized(path);
  auto b = normalized(root);
  if (b.empty() || b.back() != '/') b += '/';
  return a.size() > b.size() && a.compare(0, b.size(), b) == 0;
}

}  // namespace

std::string_view to_string(ConfineResult result) {
  switch (result) {
    case ConfineResult::Found: return "found";
    case ConfineResult::Absent: return "absent";
    case ConfineResult::UnsafePath: return "unsafe-path";
    case ConfineResult::SymbolicLink: return "symbolic-link";
    case ConfineResult::EscapesFolder: return "escapes-folder";
    case ConfineResult::NotRegularFile: return "not-a-regular-file";
  }
  return "absent";
}

ConfinedFile confined_file(const std::filesystem::path& root, std::string_view data_path) {
  namespace fs = std::filesystem;
  if (data_path.find(':') != std::string_view::npos || !text::is_safe_relative_path(data_path)) return {ConfineResult::UnsafePath, {}};
  fs::path relative;
  try {
    relative = text::path_from_utf8(data_path);
  } catch (const std::exception&) {
    return {ConfineResult::UnsafePath, {}};  // not valid UTF-8
  }
  const fs::path candidate = root / relative;
  std::error_code ec;
  const auto status = fs::symlink_status(candidate, ec);
  if (ec || status.type() == fs::file_type::not_found) return {ConfineResult::Absent, {}};
  if (fs::is_symlink(status)) return {ConfineResult::SymbolicLink, {}};
  if (!fs::is_regular_file(status)) return {ConfineResult::NotRegularFile, {}};
  std::error_code real_ec, root_ec;
  const auto real = fs::weakly_canonical(candidate, real_ec);
  const auto base = fs::weakly_canonical(root, root_ec);
  if (real_ec || root_ec || !inside(real, base)) return {ConfineResult::EscapesFolder, {}};
  return {ConfineResult::Found, candidate};
}

}  // namespace sns::mo2
