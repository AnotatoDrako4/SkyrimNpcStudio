#include "sns/assets/bsa_archive.hpp"

#include <lz4frame.h>

#include <cstring>
#include <fstream>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::assets {
namespace {

constexpr std::uint32_t kFlagDirectoryNames = 0x1;
constexpr std::uint32_t kFlagFileNames = 0x2;
constexpr std::uint32_t kFlagCompressed = 0x4;
constexpr std::uint32_t kFlagEmbeddedNames = 0x100;
constexpr std::uint32_t kSizeCompressionToggle = 0x40000000;
constexpr std::uint32_t kSizeMask = 0x3FFFFFFF;
constexpr std::uint32_t kMaxRecords = 4'000'000;

template <typename T>
T get(const std::string& buffer, std::size_t offset, const std::string& what) {
  if (offset + sizeof(T) > buffer.size()) throw Error(ErrorKind::Parse, "truncated BSA " + what);
  T value{};
  std::memcpy(&value, buffer.data() + offset, sizeof(T));
  return value;
}

std::string read_at(std::ifstream& in, std::uint64_t offset, std::size_t size, const std::string& what) {
  std::string data(size, '\0');
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(data.data(), static_cast<std::streamsize>(size));
  if (static_cast<std::size_t>(in.gcount()) != size) throw Error(ErrorKind::Parse, "truncated BSA " + what);
  return data;
}

std::string lz4_frame_decompress(std::string_view input, std::size_t expected) {
  LZ4F_dctx* ctx = nullptr;
  if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))) {
    throw Error(ErrorKind::Io, "cannot create LZ4 context");
  }
  std::string out(expected, '\0');
  std::size_t in_pos = 0, out_pos = 0;
  std::size_t status = 1;
  while (status != 0 && in_pos < input.size() && out_pos < expected) {
    std::size_t in_size = input.size() - in_pos;
    std::size_t out_size = expected - out_pos;
    status = LZ4F_decompress(ctx, out.data() + out_pos, &out_size, input.data() + in_pos, &in_size, nullptr);
    if (LZ4F_isError(status)) {
      const std::string message = LZ4F_getErrorName(status);
      LZ4F_freeDecompressionContext(ctx);
      throw Error(ErrorKind::Parse, "corrupt LZ4 data in BSA: " + message);
    }
    in_pos += in_size;
    out_pos += out_size;
  }
  LZ4F_freeDecompressionContext(ctx);
  if (out_pos != expected || status != 0 || in_pos != input.size())
    throw Error(ErrorKind::Parse, "LZ4 frame length or checksum is invalid");
  return out;
}

}  // namespace

std::unique_ptr<BsaArchive> BsaArchive::open(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Error(ErrorKind::NotFound, "cannot open archive " + text::path_to_utf8(path));
  const auto name = text::path_to_utf8(path.filename());
  std::unique_ptr<BsaArchive> bsa(new BsaArchive());
  bsa->path_ = path;
  bsa->file_size_ = std::filesystem::file_size(path);

  const auto header = read_at(in, 0, 36, "header");
  if (header.compare(0, 4, std::string("BSA\0", 4)) != 0) throw Error(ErrorKind::Parse, name + " is not a BSA archive");
  bsa->version_ = get<std::uint32_t>(header, 4, "header");
  if (bsa->version_ != 104 && bsa->version_ != 105) {
    throw Error(ErrorKind::Parse, name + ": unsupported BSA version " + std::to_string(bsa->version_));
  }
  const auto header_size = get<std::uint32_t>(header, 8, "header");
  bsa->flags_ = get<std::uint32_t>(header, 12, "header");
  const auto folder_count = get<std::uint32_t>(header, 16, "header");
  const auto file_count = get<std::uint32_t>(header, 20, "header");
  const auto total_file_name_length = get<std::uint32_t>(header, 28, "header");
  if (total_file_name_length > 128u * 1024 * 1024)
    throw Error(ErrorKind::Parse, name + " filename table exceeds limit");
  if (header_size != 36 || folder_count > kMaxRecords || file_count > kMaxRecords) {
    throw Error(ErrorKind::Parse, name + " has an implausible BSA header");
  }
  if ((bsa->flags_ & kFlagDirectoryNames) == 0 || (bsa->flags_ & kFlagFileNames) == 0) {
    throw Error(ErrorKind::Parse, name + " does not store file names; it cannot be indexed");
  }

  // Folder records: v104 = hash(8) count(4) offset(4); v105 = hash(8) count(4) pad(4) offset(8).
  const std::size_t folder_record = bsa->version_ == 105 ? 24 : 16;
  const auto folders = read_at(in, 36, folder_record * folder_count, "folder records");
  struct Folder {
    std::uint32_t count;
    std::uint64_t offset;
  };
  std::vector<Folder> folder_list;
  folder_list.reserve(folder_count);
  for (std::uint32_t i = 0; i < folder_count; ++i) {
    const std::size_t base = i * folder_record;
    const auto count = get<std::uint32_t>(folders, base + 8, "folder records");
    if (count > file_count) throw Error(ErrorKind::Parse, name + " has an invalid folder file count");
    const std::uint64_t offset = bsa->version_ == 105 ? get<std::uint64_t>(folders, base + 16, "folder records")
                                                      : get<std::uint32_t>(folders, base + 12, "folder records");
    folder_list.push_back({count, offset});
  }

  // File record blocks, one per folder: bzstring folder name + 16-byte file records.
  struct Pending {
    std::string folder;
    Entry entry;
  };
  std::vector<Pending> pending;
  pending.reserve(file_count);
  for (const auto& folder : folder_list) {
    if (folder.offset < total_file_name_length) throw Error(ErrorKind::Parse, name + " has a corrupt folder offset");
    const std::uint64_t at = folder.offset - total_file_name_length;
    if (at >= bsa->file_size_) throw Error(ErrorKind::Parse, name + " has a folder offset outside the file");
    in.seekg(static_cast<std::streamoff>(at));
    const auto length = static_cast<unsigned char>(in.get());
    std::string folder_name(length, '\0');
    in.read(folder_name.data(), length);
    if (!folder_name.empty() && folder_name.back() == '\0') folder_name.pop_back();
    const auto records = read_at(in, at + 1 + length, 16ull * folder.count, "file records");
    for (std::uint32_t f = 0; f < folder.count; ++f) {
      const auto raw_size = get<std::uint32_t>(records, f * 16 + 8, "file records");
      const auto offset = get<std::uint32_t>(records, f * 16 + 12, "file records");
      const bool compressed = ((bsa->flags_ & kFlagCompressed) != 0) != ((raw_size & kSizeCompressionToggle) != 0);
      pending.push_back({folder_name, {offset, raw_size & kSizeMask, compressed}});
    }
    if (pending.size() > file_count) throw Error(ErrorKind::Parse, name + " has more file records than declared");
  }
  if (pending.size() != file_count) throw Error(ErrorKind::Parse, name + " has fewer file records than declared");

  // File name block follows the last file record block: file_count NUL-terminated names, same order.
  const auto names = read_at(in, static_cast<std::uint64_t>(in.tellg()), total_file_name_length, "file names");
  std::size_t pos = 0;
  bsa->entries_.reserve(file_count);
  for (auto& item : pending) {
    const auto end = names.find('\0', pos);
    if (end == std::string::npos) throw Error(ErrorKind::Parse, name + " has a truncated file name block");
    const auto file_name = std::string_view(names).substr(pos, end - pos);
    pos = end + 1;
    if (item.entry.size > kMaxFileBytes || item.entry.offset + static_cast<std::uint64_t>(item.entry.size) > bsa->file_size_) {
      throw Error(ErrorKind::Parse, name + " has a file outside the archive");
    }
    const auto key = text::data_path_key(item.folder.empty() ? std::string(file_name) : item.folder + "\\" + std::string(file_name));
    if (!text::is_safe_relative_path(key)) throw Error(ErrorKind::Parse, name + " contains an unsafe resource path");
    bsa->entries_[key] = item.entry;
  }
  return bsa;
}

bool BsaArchive::contains(std::string_view data_path) const { return entries_.contains(text::data_path_key(data_path)); }

std::optional<std::string> BsaArchive::read(std::string_view data_path) const {
  const auto key = text::data_path_key(data_path);
  const auto it = entries_.find(key);
  if (it == entries_.end()) return std::nullopt;
  return read_entry(it->second, key);
}

std::string BsaArchive::read_entry(const Entry& entry, std::string_view key) const {
  std::ifstream in(path_, std::ios::binary);  // one stream per read: safe across threads
  if (!in) throw Error(ErrorKind::Io, "cannot reopen " + text::path_to_utf8(path_));
  auto data = read_at(in, entry.offset, entry.size, std::string(key));
  std::size_t skip = 0;
  if ((flags_ & kFlagEmbeddedNames) != 0) {
    if (data.empty()) throw Error(ErrorKind::Parse, "truncated embedded name in BSA");
    skip = 1 + static_cast<unsigned char>(data[0]);
  }
  if (skip > data.size()) throw Error(ErrorKind::Parse, "truncated embedded name in BSA");
  if (!entry.compressed) return data.substr(skip);

  const auto original = get<std::uint32_t>(data, skip, std::string(key));
  if (original > kMaxFileBytes) throw Error(ErrorKind::InvalidArgument, std::string(key) + " is larger than allowed");
  if (version_ != 105) {
    throw Error(ErrorKind::Parse, std::string(key) + " is zlib-compressed (BSA v104), which is not supported");
  }
  return lz4_frame_decompress(std::string_view(data).substr(skip + 4), original);
}

std::vector<std::string> BsaArchive::list() const {
  std::vector<std::string> keys;
  keys.reserve(entries_.size());
  for (const auto& [key, entry] : entries_) keys.push_back(key);
  return keys;
}

}  // namespace sns::assets
