#include "sns/mo2/plugin_header.hpp"

#include <cstring>
#include <fstream>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace {

constexpr std::size_t kRecordHeaderSize = 24;
constexpr std::size_t kSubrecordHeaderSize = 6;
// Real TES4 headers are a few KB even with hundreds of masters.
constexpr std::uint32_t kMaxHeaderData = 1u << 20;

template <typename T>
T read_le(std::string_view bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;  // x64 Windows is little-endian, like the file format
}

std::string zstring(std::string_view data) {
  const auto end = data.find('\0');
  return text::decode_text(data.substr(0, end));
}

}  // namespace

PluginHeader parse_plugin_header(std::string_view bytes, std::string_view file_name) {
  if (bytes.size() < kRecordHeaderSize || bytes.substr(0, 4) != "TES4") {
    throw Error(ErrorKind::Parse, std::string(file_name) + " is not a Skyrim plugin (missing TES4 header)");
  }
  const auto data_size = read_le<std::uint32_t>(bytes, 4);
  if (data_size > kMaxHeaderData || kRecordHeaderSize + data_size > bytes.size()) {
    throw Error(ErrorKind::Parse, std::string(file_name) + " has a truncated or oversized TES4 header");
  }

  PluginHeader h;
  h.flags = read_le<std::uint32_t>(bytes, 8);
  h.master_flag = (h.flags & 0x001) != 0;
  h.localized = (h.flags & 0x080) != 0;
  h.light_flag = (h.flags & 0x200) != 0;

  const auto data = bytes.substr(kRecordHeaderSize, data_size);
  std::size_t pos = 0;
  std::uint32_t big_size = 0;  // XXXX: size of the next subrecord when it exceeds 65535
  while (pos + kSubrecordHeaderSize <= data.size()) {
    const auto type = data.substr(pos, 4);
    std::uint32_t size = read_le<std::uint16_t>(data, pos + 4);
    pos += kSubrecordHeaderSize;
    if (big_size) {
      size = big_size;
      big_size = 0;
    }
    if (pos + size > data.size()) throw Error(ErrorKind::Parse, std::string(file_name) + " has a corrupt TES4 subrecord");
    const auto payload = data.substr(pos, size);
    pos += size;

    if (type == "XXXX" && size == 4) {
      big_size = read_le<std::uint32_t>(payload, 0);
    } else if (type == "HEDR" && size >= 12) {
      h.version = read_le<float>(payload, 0);
      h.record_count = read_le<std::uint32_t>(payload, 4);
    } else if (type == "CNAM") {
      h.author = zstring(payload);
    } else if (type == "SNAM") {
      h.description = zstring(payload);
    } else if (type == "MAST") {
      h.masters.push_back(zstring(payload));
    }
  }

  const bool esl_ext = text::ends_with_icase(file_name, ".esl");
  const bool esm_ext = text::ends_with_icase(file_name, ".esm");
  h.is_light = h.light_flag || esl_ext;
  h.is_master = h.master_flag || esm_ext || esl_ext;
  return h;
}

PluginHeader read_plugin_header(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Error(ErrorKind::NotFound, "cannot open plugin " + text::path_to_utf8(path));
  std::string head(kRecordHeaderSize, '\0');
  in.read(head.data(), static_cast<std::streamsize>(head.size()));
  if (in.gcount() != static_cast<std::streamsize>(kRecordHeaderSize)) {
    throw Error(ErrorKind::Parse, text::path_to_utf8(path.filename()) + " is too small to be a plugin");
  }
  const auto data_size = read_le<std::uint32_t>(head, 4);
  if (data_size > kMaxHeaderData) {
    throw Error(ErrorKind::Parse, text::path_to_utf8(path.filename()) + " has an oversized TES4 header");
  }
  head.resize(kRecordHeaderSize + data_size);
  in.read(head.data() + kRecordHeaderSize, data_size);
  head.resize(kRecordHeaderSize + static_cast<std::size_t>(in.gcount()));
  return parse_plugin_header(head, text::path_to_utf8(path.filename()));
}

}  // namespace sns::mo2
