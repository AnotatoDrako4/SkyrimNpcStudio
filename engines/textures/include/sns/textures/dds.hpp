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
  bool has_alpha = false;  // the FORMAT can store alpha; see measure_alpha() for real usage
  std::string data;        // the selected mip level
  std::uint32_t source_width = 0;   // dimensions of mip 0 in the file
  std::uint32_t source_height = 0;
  std::uint32_t mip_level = 0;      // which stored level `data` holds
};

inline constexpr std::uint32_t kMaxTextureDimension = 16384;

// Throws sns::Error(Parse) for malformed or unsupported files. With max_dimension > 0 the first
// stored mip level whose larger side fits is returned (no resampling: real file data).
DdsImage parse_dds(std::string_view bytes, std::uint32_t max_dimension = 0);
DdsImage decode_rgba(const DdsImage& image, bool normal_map = false);

// Box-filter halving of an RGBA8 image until it fits (for files without a usable mip chain).
DdsImage downscale_rgba(const DdsImage& image, std::uint32_t max_dimension);

struct AlphaStats {
  bool uses_alpha = false;            // at least one texel below 250
  std::uint8_t min = 255;
  float mean = 255.0f;
  float translucent_fraction = 0.0f;  // texels below 250
  float below_cutoff_fraction = 0.0f; // texels below the requested cutoff
};

// Measures the alpha channel of decoded RGBA8 data (BC1 punch-through included).
AlphaStats measure_alpha(const DdsImage& image, std::uint8_t cutoff = 128);

std::string_view to_string(TextureFormat format);

// Writes an RGBA8 image as a DDS file Skyrim SE can load: RGBA8 (uncompressed), BC1, BC3 (legacy
// DX9 header) or BC7 (DX10 header), optionally with a full box-filtered mip chain.
// Throws sns::Error(InvalidArgument) for non-RGBA8 input or an unsupported target.
std::string encode_dds(const DdsImage& rgba, TextureFormat format, bool generate_mips);

// Size in bytes of one mip level.
std::uint64_t level_size(TextureFormat format, std::uint32_t width, std::uint32_t height);

}  // namespace sns::textures
