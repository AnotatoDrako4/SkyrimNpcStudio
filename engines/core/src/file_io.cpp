#include "sns/core/file_io.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <system_error>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::file_io {
namespace {

std::string describe(const std::filesystem::path& path) { return text::path_to_utf8(path); }

std::string last_error_message() {
  return std::system_category().message(static_cast<int>(GetLastError()));
}

}  // namespace

std::string read_file(const std::filesystem::path& path, std::uintmax_t max_bytes) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    throw Error(ec == std::errc::no_such_file_or_directory ? ErrorKind::NotFound : ErrorKind::Io,
                "cannot read '" + describe(path) + "': " + ec.message());
  }
  if (size > max_bytes) {
    throw Error(ErrorKind::InvalidArgument, "'" + describe(path) + "' is larger than the allowed " +
                                                std::to_string(max_bytes) + " bytes");
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Error(ErrorKind::Io, "cannot open '" + describe(path) + "'");
  std::string data(static_cast<size_t>(size), '\0');
  in.read(data.data(), static_cast<std::streamsize>(size));
  data.resize(static_cast<size_t>(in.gcount()));
  return data;
}

std::string read_text_file(const std::filesystem::path& path, std::uintmax_t max_bytes) {
  return text::decode_text(read_file(path, max_bytes));
}

void write_file_atomic(const std::filesystem::path& path, std::string_view content) {
  const auto dir = path.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) throw Error(ErrorKind::Io, "cannot create '" + describe(dir) + "': " + ec.message());

  auto temp = path;
  temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());

  HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw Error(ErrorKind::Io, "cannot create '" + describe(temp) + "': " + last_error_message());

  bool ok = true;
  size_t offset = 0;
  while (ok && offset < content.size()) {
    DWORD written = 0;
    const auto chunk = static_cast<DWORD>(std::min<size_t>(content.size() - offset, 1u << 20));
    ok = WriteFile(h, content.data() + offset, chunk, &written, nullptr) != 0;
    offset += written;
  }
  ok = ok && FlushFileBuffers(h) != 0;
  const auto write_error = ok ? std::string{} : last_error_message();
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(temp.c_str());
    throw Error(ErrorKind::Io, "cannot write '" + describe(temp) + "': " + write_error);
  }

  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto message = last_error_message();
    DeleteFileW(temp.c_str());
    throw Error(ErrorKind::Io, "cannot replace '" + describe(path) + "': " + message);
  }
}

}  // namespace sns::file_io
