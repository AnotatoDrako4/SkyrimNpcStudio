#include "sns/core/text.hpp"

#include <windows.h>

#include <algorithm>

#include "sns/core/error.hpp"

namespace sns::text {

std::wstring to_wide(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (n <= 0) throw Error(ErrorKind::Parse, "invalid UTF-8 text");
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

std::string to_utf8(std::wstring_view wide) {
  if (wide.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) throw Error(ErrorKind::Parse, "invalid UTF-16 text");
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr,
                      nullptr);
  return out;
}

std::filesystem::path path_from_utf8(std::string_view utf8) { return std::filesystem::path(to_wide(utf8)); }

std::string path_to_utf8(const std::filesystem::path& path) { return to_utf8(path.native()); }

bool is_valid_utf8(std::string_view bytes) {
  if (bytes.empty()) return true;
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) > 0;
}

std::string decode_text(std::string_view bytes) {
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
      static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.remove_prefix(3);
  }
  if (is_valid_utf8(bytes)) return std::string(bytes);

  const int n = MultiByteToWideChar(1252, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(1252, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), n);
  return to_utf8(wide);
}

std::string to_lower_ascii(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
  return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
  return a.size() == b.size() && to_lower_ascii(a) == to_lower_ascii(b);
}

bool ends_with_icase(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && iequals_ascii(s.substr(s.size() - suffix.size()), suffix);
}

std::string_view trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    auto line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (end != text.size() || !line.empty()) lines.push_back(line);
    start = end + 1;
  }
  return lines;
}

std::string data_path_key(std::string_view relative) {
  std::string key = to_lower_ascii(relative);
  std::replace(key.begin(), key.end(), '/', '\\');
  const auto first = key.find_first_not_of('\\');
  return first == std::string::npos ? std::string{} : key.substr(first);
}

bool is_safe_relative_path(std::string_view relative) {
  if (relative.empty()) return false;
  if (relative.front() == '\\' || relative.front() == '/') return false;
  if (relative.find(':') != std::string_view::npos) return false;
  size_t start = 0;
  while (start <= relative.size()) {
    auto end = relative.find_first_of("\\/", start);
    if (end == std::string_view::npos) end = relative.size();
    const auto part = relative.substr(start, end - start);
    if (part == "..") return false;
    start = end + 1;
  }
  return true;
}

}  // namespace sns::text
