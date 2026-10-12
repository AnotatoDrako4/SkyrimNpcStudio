#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sns::textures {

enum class TextureFormat { RGBA8, BC1, BC2, BC3, BC4, BC5, BC6H, BC7 };

// The first mip level of a DDS texture, ready for upload. Block-compressed formats are passed
// through unchanged (the GPU decodes them); uncompressed layouts are converted to RGBA8.
struct DdsImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t mip_count = 1;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool cubemap = false;  // only the first face is kept
  bool has_alpha = false;
  std::string data;      // mip 0
};

inline constexpr std::uint32_t kMaxTextureDimension = 16384;

// Throws sns::Error(Parse) for malformed or unsupported files.
DdsImage parse_dds(std::string_view bytes);
DdsImage decode_rgba(const DdsImage& image, bool normal_map = false);

std::string_view to_string(TextureFormat format);

// Size in bytes of one mip level.
std::uint64_t level_size(TextureFormat format, std::uint32_t width, std::uint32_t height);

}  // namespace sns::textures
