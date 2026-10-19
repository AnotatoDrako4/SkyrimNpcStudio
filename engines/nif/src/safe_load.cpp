#include "sns/nif/safe_load.hpp"

#include <NifFile.hpp>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "sns/core/error.hpp"

namespace sns::nif {
namespace {

// Bounds-checked little-endian reader over a byte range; every failure names the field and the file.
class Reader {
 public:
  Reader(std::string_view bytes, std::string_view label) : b_(bytes), label_(label) {}

  [[noreturn]] void fail(const std::string& what) const {
    throw Error(ErrorKind::Parse, "cannot read NIF " + std::string(label_) + ": rejected before parsing, " + what);
  }
  void need(std::uint64_t n, const char* what) const {
    if (n > b_.size() - p_) fail(std::string(what) + " runs past the end of its data");
  }
  void skip(std::uint64_t n, const char* what) {
    need(n, what);
    p_ += static_cast<std::size_t>(n);
  }
  std::uint8_t u8(const char* what) {
    need(1, what);
    return static_cast<std::uint8_t>(b_[p_++]);
  }
  std::uint16_t u16(const char* what) { return read<std::uint16_t>(what); }
  std::uint32_t u32(const char* what) { return read<std::uint32_t>(what); }
  std::string_view take(std::uint64_t n, const char* what) {
    need(n, what);
    const auto out = b_.substr(p_, static_cast<std::size_t>(n));
    p_ += static_cast<std::size_t>(n);
    return out;
  }
  [[nodiscard]] std::size_t pos() const { return p_; }
  void seek(std::size_t p) { p_ = p; }

 private:
  template <class T>
  T read(const char* what) {
    need(sizeof(T), what);
    T v{};
    std::memcpy(&v, b_.data() + p_, sizeof(T));
    p_ += sizeof(T);
    return v;
  }
  std::string_view b_;
  std::string_view label_;
  std::size_t p_ = 0;
};

// BSShaderTextureSet (Skyrim): int32 count, then `count` sized strings, nothing else. nifly runs each path
// through std::regex (NifFile::TrimTexturePaths): one longer than a real path is refused here.
void check_texture_set(std::string_view block, std::uint32_t index, std::string_view label) {
  Reader r(block, label);
  const auto count = static_cast<std::int32_t>(r.u32("texture set count"));
  if (count < 0 || count > 32) r.fail("texture set (block " + std::to_string(index) + ") declares " + std::to_string(count) + " textures");
  for (std::int32_t i = 0; i < count; ++i) {
    const auto length = r.u32("texture path length");
    if (length > kMaxTexturePathBytes)
      r.fail("texture path of " + std::to_string(length) + " bytes in block " + std::to_string(index) + " (limit " +
             std::to_string(kMaxTexturePathBytes) + ")");
    r.skip(length, "texture path");
  }
}

}  // namespace

void prevalidate_nif(std::string_view bytes, std::string_view label) {
  if (bytes.size() > kMaxNifBytes) throw Error(ErrorKind::Parse, "NIF exceeds the 64 MiB parser limit");
  Reader r(bytes, label);
  constexpr std::string_view kMagic = "Gamebryo File Format, Version ";
  if (bytes.substr(0, kMagic.size()) != kMagic) r.fail("not a Gamebryo NIF header");
  const auto newline = bytes.find('\n');
  if (newline == std::string_view::npos || newline > 64) r.fail("the header line has no end");
  r.seek(newline + 1);

  const auto version = r.u32("version");
  const auto endian = r.u8("endianness");
  const auto user = r.u32("user version");
  const auto blocks = r.u32("block count");
  const auto stream = r.u32("stream version");
  if (version != 0x14020007 || endian != 1 || user != 12 || stream == 0 || stream >= 130) {
    r.fail("not a Skyrim NIF (version " + std::to_string(version) + ", user " + std::to_string(user) + ", stream " +
           std::to_string(stream) + ")");
  }
  // Export info: author, process script, export script (and max filepath from stream 103): byte length + text.
  for (int i = 0, n = stream >= 103 ? 4 : 3; i < n; ++i) r.skip(r.u8("export info length"), "export info");

  const auto type_count = r.u16("block type count");
  std::vector<std::string_view> types;
  types.reserve(type_count);
  for (std::uint16_t i = 0; i < type_count; ++i) {
    const auto length = r.u32("block type name length");
    if (length > 256) r.fail("block type name of " + std::to_string(length) + " bytes");
    types.push_back(r.take(length, "block type name"));
  }
  r.need(std::uint64_t{6} * blocks, "block table");  // uint16 type index + uint32 size per block
  const auto index_at = r.pos();
  const auto sizes_at = index_at + std::size_t{2} * blocks;
  r.seek(sizes_at + std::size_t{4} * blocks);

  const auto string_count = r.u32("string count");
  (void)r.u32("max string length");
  for (std::uint32_t i = 0; i < string_count; ++i) r.skip(r.u32("header string length"), "header string");
  r.skip(std::uint64_t{4} * r.u32("group count"), "group table");
  const auto first_block = r.pos();

  // Every block must fit in the file, followed by the footer (root count + root references). Leftover bytes
  // after the footer are allowed: the game ignores them and some FaceGen files carry them.
  std::uint64_t total = 0;
  for (std::uint32_t i = 0; i < blocks; ++i) {
    std::uint16_t type = 0;
    std::uint32_t size = 0;
    std::memcpy(&type, bytes.data() + index_at + std::size_t{2} * i, 2);
    std::memcpy(&size, bytes.data() + sizes_at + std::size_t{4} * i, 4);
    if ((type & 0x7FFF) >= type_count) r.fail("block " + std::to_string(i) + " has a type index out of range");
    total += size;
    if (total > bytes.size() - first_block) r.fail("block sizes add up to more than the file (block " + std::to_string(i) + ")");
  }
  Reader footer(bytes.substr(first_block + static_cast<std::size_t>(total)), label);
  footer.skip(std::uint64_t{4} * footer.u32("footer root count"), "footer roots");

  std::uint64_t offset = first_block;
  for (std::uint32_t i = 0; i < blocks; ++i) {
    std::uint16_t type = 0;
    std::uint32_t size = 0;
    std::memcpy(&type, bytes.data() + index_at + std::size_t{2} * i, 2);
    std::memcpy(&size, bytes.data() + sizes_at + std::size_t{4} * i, 4);
    if (types[type & 0x7FFF] == "BSShaderTextureSet") check_texture_set(bytes.substr(static_cast<std::size_t>(offset), size), i, label);
    offset += size;
  }
}

void load_nifly(nifly::NifFile& nif, std::string_view bytes, std::string_view label) {
  prevalidate_nif(bytes, label);
  try {
    std::istringstream in(std::string(bytes), std::ios::binary);
    if (nif.Load(in) != 0 || !nif.IsValid()) throw Error(ErrorKind::Parse, "cannot read NIF " + std::string(label));
  } catch (const Error&) {
    throw;
  } catch (const std::exception& e) {
    throw Error(ErrorKind::Parse, "cannot read NIF " + std::string(label) + ": " + e.what());
  } catch (...) {
    throw Error(ErrorKind::Parse, "cannot read NIF " + std::string(label) + ": unknown exception in the NIF parser");
  }
}

}  // namespace sns::nif
