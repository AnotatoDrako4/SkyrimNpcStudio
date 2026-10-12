#include "sns/face/tri_file.hpp"

#include <cstring>
#include <limits>
#include <string>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::face {

namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::int32_t kMaxCount = 1 << 24;  // far above any head part (HPH heads have ~4k vertices)

class Reader {
 public:
  Reader(std::string_view bytes, std::string_view label) : bytes_(bytes), label_(label) {}

  void need(std::size_t n) const {
    if (offset_ > bytes_.size() || bytes_.size() - offset_ < n) fail("truncated");
  }
  std::int32_t i32() {
    need(4);
    std::int32_t v = 0;
    std::memcpy(&v, bytes_.data() + offset_, 4);
    offset_ += 4;
    return v;
  }
  float f32() {
    need(4);
    float v = 0;
    std::memcpy(&v, bytes_.data() + offset_, 4);
    offset_ += 4;
    return v;
  }
  std::int16_t i16() {
    need(2);
    std::int16_t v = 0;
    std::memcpy(&v, bytes_.data() + offset_, 2);
    offset_ += 2;
    return v;
  }
  std::int32_t count(const char* what) {
    const auto n = i32();
    if (n < 0 || n > kMaxCount) fail(std::string("invalid ") + what + " count");
    return n;
  }
  void skip(std::uint64_t n) {
    if (n > bytes_.size()) fail("truncated");
    need(static_cast<std::size_t>(n));
    offset_ += static_cast<std::size_t>(n);
  }
  // Length-prefixed label (the length includes the terminating NUL).
  std::string label() {
    const auto n = count("label");
    need(static_cast<std::size_t>(n));
    std::string s(bytes_.data() + offset_, static_cast<std::size_t>(n));
    offset_ += static_cast<std::size_t>(n);
    if (const auto nul = s.find('\0'); nul != std::string::npos) s.resize(nul);
    return s;
  }
  [[noreturn]] void fail(const std::string& why) const {
    throw Error(ErrorKind::Parse, "TRI file '" + std::string(label_) + "': " + why);
  }
  void seek(std::size_t offset) { offset_ = offset; }

 private:
  std::string_view bytes_;
  std::string_view label_;
  std::size_t offset_ = 0;
};

}  // namespace

const TriMorph* TriFile::find(std::string_view name) const {
  for (const auto& morph : morphs) {
    if (text::iequals_ascii(morph.name, name)) return &morph;
  }
  return nullptr;
}

TriFile parse_tri(std::string_view bytes, std::string_view label) {
  Reader r(bytes, label);
  r.need(kHeaderSize);
  if (bytes.substr(0, 8) != "FRTRI003") r.fail("not a FRTRI003 file");
  r.seek(8);
  const auto vertices = r.count("vertex");
  const auto triangles = r.count("triangle");
  const auto quads = r.count("quad");
  const auto labelled_vertices = r.count("labelled vertex");
  const auto labelled_points = r.count("labelled surface point");
  const auto texture_coords = r.count("texture coordinate");
  r.i32();  // extension flags
  const auto diff_morphs = r.count("difference morph");
  const auto stat_morphs = r.count("statistical morph");
  const auto stat_vertices = r.count("statistical morph vertex");
  r.seek(kHeaderSize);

  TriFile tri;
  tri.vertex_count = static_cast<std::uint32_t>(vertices);
  const auto all_vertices = static_cast<std::size_t>(vertices) + static_cast<std::size_t>(stat_vertices);
  std::vector<std::array<float, 3>> positions(all_vertices);
  for (auto& p : positions) p = {r.f32(), r.f32(), r.f32()};
  tri.base.assign(positions.begin(), positions.begin() + vertices);

  r.skip(static_cast<std::uint64_t>(triangles) * 12 + static_cast<std::uint64_t>(quads) * 16);
  for (std::int32_t i = 0; i < labelled_vertices; ++i) {
    r.i32();
    r.label();
  }
  for (std::int32_t i = 0; i < labelled_points; ++i) {
    r.i32();
    r.skip(8);
    r.label();
  }
  if (texture_coords > 0) {
    r.skip(static_cast<std::uint64_t>(texture_coords) * 8);
    r.skip(static_cast<std::uint64_t>(triangles) * 12 + static_cast<std::uint64_t>(quads) * 16);
  }

  tri.morphs.reserve(static_cast<std::size_t>(diff_morphs) + static_cast<std::size_t>(stat_morphs));
  for (std::int32_t m = 0; m < diff_morphs; ++m) {
    TriMorph morph;
    morph.name = r.label();
    const float scale = r.f32();
    morph.deltas.resize(static_cast<std::size_t>(vertices));
    for (auto& d : morph.deltas) {
      const auto x = r.i16();
      const auto y = r.i16();
      const auto z = r.i16();
      d = {x * scale, y * scale, z * scale};
    }
    tri.morphs.push_back(std::move(morph));
  }

  std::size_t next_target = static_cast<std::size_t>(vertices);
  for (std::int32_t m = 0; m < stat_morphs; ++m) {
    TriMorph morph;
    morph.name = r.label();
    const auto n = r.count("statistical morph index");
    morph.deltas.assign(static_cast<std::size_t>(vertices), {0.0f, 0.0f, 0.0f});
    for (std::int32_t i = 0; i < n; ++i) {
      const auto index = r.i32();
      if (index < 0 || index >= vertices || next_target >= positions.size()) r.fail("statistical morph index out of range");
      const auto& target = positions[next_target++];
      const auto& base = positions[static_cast<std::size_t>(index)];
      morph.deltas[static_cast<std::size_t>(index)] = {target[0] - base[0], target[1] - base[1], target[2] - base[2]};
    }
    tri.morphs.push_back(std::move(morph));
  }
  return tri;
}

}  // namespace sns::face
