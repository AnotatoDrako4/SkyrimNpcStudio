#include "sns/textures/dds.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>
#include <cmath>
#include <DirectXTex.h>

#include "sns/core/error.hpp"

namespace sns::textures {
namespace {

constexpr std::uint32_t kHeaderSize = 124;
constexpr std::uint32_t kFlagAlphaPixels = 0x1;
constexpr std::uint32_t kFlagFourCC = 0x4;
constexpr std::uint32_t kFlagRgb = 0x40;
constexpr std::uint32_t kFlagLuminance = 0x20000;
constexpr std::uint32_t kCaps2Cubemap = 0x200;

constexpr std::uint32_t fourcc(const char (&s)[5]) {
  return static_cast<std::uint32_t>(s[0]) | (static_cast<std::uint32_t>(s[1]) << 8) |
         (static_cast<std::uint32_t>(s[2]) << 16) | (static_cast<std::uint32_t>(s[3]) << 24);
}

std::uint32_t u32(std::string_view b, std::size_t offset) {
  std::uint32_t v = 0;
  std::memcpy(&v, b.data() + offset, 4);
  return v;
}

bool block_compressed(TextureFormat f) { return f != TextureFormat::RGBA8; }

// Extracts a channel from a packed pixel given its bit mask and widens it to 8 bits.
std::uint8_t channel(std::uint32_t pixel, std::uint32_t mask) {
  if (mask == 0) return 0;
  const int shift = std::countr_zero(mask);
  const int bits = std::popcount(mask);
  const std::uint32_t value = (pixel & mask) >> shift;
  return static_cast<std::uint8_t>(bits >= 8 ? value >> (bits - 8) : (value * 255u) / ((1u << bits) - 1u));
}

}  // namespace

std::string_view to_string(TextureFormat format) {
  switch (format) {
    case TextureFormat::RGBA8: return "RGBA8";
    case TextureFormat::BC1: return "BC1";
    case TextureFormat::BC2: return "BC2";
    case TextureFormat::BC3: return "BC3";
    case TextureFormat::BC4: return "BC4";
    case TextureFormat::BC5: return "BC5";
    case TextureFormat::BC6H: return "BC6H";
    case TextureFormat::BC7: return "BC7";
  }
  return "?";
}

std::uint64_t level_size(TextureFormat format, std::uint32_t width, std::uint32_t height) {
  if (!block_compressed(format)) return 4ull * width * height;
  const std::uint64_t blocks = static_cast<std::uint64_t>(std::max(1u, (width + 3) / 4)) * std::max(1u, (height + 3) / 4);
  const bool eight = format == TextureFormat::BC1 || format == TextureFormat::BC4;
  return blocks * (eight ? 8 : 16);
}

DdsImage parse_dds(std::string_view b, std::uint32_t max_dimension) {
  if (b.size() < 4 + kHeaderSize || b.substr(0, 4) != "DDS ") throw Error(ErrorKind::Parse, "not a DDS file");
  if (u32(b, 4) != kHeaderSize) throw Error(ErrorKind::Parse, "invalid DDS header size");

  DdsImage img;
  img.height = u32(b, 12);
  img.width = u32(b, 16);
  img.mip_count = std::max(1u, u32(b, 28));
  const std::uint32_t pf_flags = u32(b, 80);
  const std::uint32_t pf_fourcc = u32(b, 84);
  const std::uint32_t pf_bits = u32(b, 88);
  const std::uint32_t masks[4] = {u32(b, 92), u32(b, 96), u32(b, 100), u32(b, 104)};
  img.cubemap = (u32(b, 112) & kCaps2Cubemap) != 0;
  if (u32(b, 76) != 32 || img.cubemap || u32(b, 24) > 1)
    throw Error(ErrorKind::Parse, "Unsupported DDS header, cubemap or volume");
  if (img.width == 0 || img.height == 0 || img.width > kMaxTextureDimension || img.height > kMaxTextureDimension) {
    throw Error(ErrorKind::Parse, "DDS dimensions out of range");
  }
  img.source_width = img.width;
  img.source_height = img.height;

  std::size_t data_offset = 4 + kHeaderSize;
  // Skips whole mip levels until the image fits max_dimension (the DDS already stores them).
  const auto pick_level = [&](auto level_bytes) -> std::uint64_t {
    std::uint32_t w = img.width, h = img.height, level = 0;
    std::uint64_t offset = data_offset;
    while (max_dimension && std::max(w, h) > max_dimension && level + 1 < img.mip_count) {
      offset += level_bytes(w, h);
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      ++level;
    }
    img.width = w;
    img.height = h;
    img.mip_level = level;
    return offset;
  };
  bool uncompressed = false;
  if (pf_flags & kFlagFourCC) {
    switch (pf_fourcc) {
      case fourcc("DXT1"): img.format = TextureFormat::BC1; break;
      case fourcc("DXT2"):
      case fourcc("DXT3"): img.format = TextureFormat::BC2; break;
      case fourcc("DXT4"):
      case fourcc("DXT5"): img.format = TextureFormat::BC3; break;
      case fourcc("ATI1"):
      case fourcc("BC4U"): img.format = TextureFormat::BC4; break;
      case fourcc("ATI2"):
      case fourcc("BC5U"): img.format = TextureFormat::BC5; break;
      case fourcc("DX10"): {
        if (b.size() < data_offset + 20) throw Error(ErrorKind::Parse, "truncated DX10 header");
        const auto dxgi = u32(b, data_offset);
        if (u32(b, data_offset + 4) != 3 || u32(b, data_offset + 12) != 1 || (u32(b, data_offset + 8) & 4))
          throw Error(ErrorKind::Parse, "Only a single 2D DDS image is supported");
        data_offset += 20;
        switch (dxgi) {
          case 71: img.format = TextureFormat::BC1; break;
          case 72: img.format = TextureFormat::BC1; img.srgb = true; break;
          case 74: img.format = TextureFormat::BC2; break;
          case 75: img.format = TextureFormat::BC2; img.srgb = true; break;
          case 77: img.format = TextureFormat::BC3; break;
          case 78: img.format = TextureFormat::BC3; img.srgb = true; break;
          case 80: img.format = TextureFormat::BC4; break;
          case 83: img.format = TextureFormat::BC5; break;
          case 95: img.format = TextureFormat::BC6H; break;
          case 98: img.format = TextureFormat::BC7; break;
          case 99: img.format = TextureFormat::BC7; img.srgb = true; break;
          case 28: case 29:  // R8G8B8A8
            uncompressed = true;
            break;
          case 87: case 88: case 91:  // B8G8R8A8 / B8G8R8X8
            uncompressed = true;
            break;
          default: throw Error(ErrorKind::Parse, "unsupported DXGI format " + std::to_string(dxgi));
        }
        if (uncompressed) {
          const auto offset = pick_level([](std::uint32_t w, std::uint32_t h) { return 4ull * w * h; });
          const auto bytes = level_size(TextureFormat::RGBA8, img.width, img.height);
          if (b.size() < offset + bytes) throw Error(ErrorKind::Parse, "truncated DDS pixel data");
          img.format = TextureFormat::RGBA8;
          img.data.assign(b.substr(offset, bytes));
          if (dxgi != 28 && dxgi != 29) {
            for (std::size_t i = 0; i < img.data.size(); i += 4) std::swap(img.data[i], img.data[i + 2]);  // BGRA -> RGBA
          }
          if (dxgi == 88) {
            for (std::size_t i = 3; i < img.data.size(); i += 4) img.data[i] = static_cast<char>(0xFF);
          }
          img.srgb = dxgi == 29 || dxgi == 91;
          img.has_alpha = dxgi != 88;
          return img;
        }
        break;
      }
      default: throw Error(ErrorKind::Parse, "unsupported DDS FourCC");
    }
  } else if (pf_flags & (kFlagRgb | kFlagLuminance)) {
    // Uncompressed 8/16/24/32-bit layouts described by bit masks.
    if (pf_bits != 8 && pf_bits != 16 && pf_bits != 24 && pf_bits != 32) throw Error(ErrorKind::Parse, "unsupported DDS bit depth");
    const std::size_t bpp = pf_bits / 8;
    const auto offset = pick_level([bpp](std::uint32_t w, std::uint32_t h) { return static_cast<std::uint64_t>(w) * h * bpp; });
    const std::uint64_t pixels = static_cast<std::uint64_t>(img.width) * img.height;
    if (b.size() < offset + pixels * bpp) throw Error(ErrorKind::Parse, "truncated DDS pixel data");
    img.format = TextureFormat::RGBA8;
    img.has_alpha = (pf_flags & kFlagAlphaPixels) != 0;
    img.data.resize(pixels * 4);
    const bool luminance = (pf_flags & kFlagLuminance) != 0;
    for (std::uint64_t i = 0; i < pixels; ++i) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel, b.data() + offset + i * bpp, bpp);
      auto* out = reinterpret_cast<std::uint8_t*>(img.data.data()) + i * 4;
      if (luminance) {
        out[0] = out[1] = out[2] = channel(pixel, masks[0]);
      } else {
        out[0] = channel(pixel, masks[0]);
        out[1] = channel(pixel, masks[1]);
        out[2] = channel(pixel, masks[2]);
      }
      out[3] = img.has_alpha ? channel(pixel, masks[3]) : 0xFF;
    }
    return img;
  } else {
    throw Error(ErrorKind::Parse, "unsupported DDS pixel format");
  }

  const auto format = img.format;
  const auto offset = pick_level([format](std::uint32_t w, std::uint32_t h) { return level_size(format, w, h); });
  const auto bytes = level_size(img.format, img.width, img.height);
  if (b.size() < offset + bytes) throw Error(ErrorKind::Parse, "truncated DDS block data");
  img.data.assign(b.substr(offset, bytes));
  img.has_alpha = img.format == TextureFormat::BC2 || img.format == TextureFormat::BC3 || img.format == TextureFormat::BC7;
  return img;
}

DdsImage decode_rgba(const DdsImage& image, bool normal_map) {
  const auto decoded_size = level_size(TextureFormat::RGBA8, image.width, image.height);
  if (!image.width || !image.height || decoded_size > 256ull * 1024 * 1024 ||
      image.data.size() != level_size(image.format, image.width, image.height))
    throw Error(ErrorKind::Parse, "DDS decode dimensions or payload exceed limits");
  if (image.format == TextureFormat::RGBA8) return image;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  switch (image.format) {
    case TextureFormat::BC1: format = DXGI_FORMAT_BC1_UNORM; break;
    case TextureFormat::BC2: format = DXGI_FORMAT_BC2_UNORM; break;
    case TextureFormat::BC3: format = DXGI_FORMAT_BC3_UNORM; break;
    case TextureFormat::BC4: format = DXGI_FORMAT_BC4_UNORM; break;
    case TextureFormat::BC5: format = DXGI_FORMAT_BC5_UNORM; break;
    case TextureFormat::BC6H: format = DXGI_FORMAT_BC6H_UF16; break;
    case TextureFormat::BC7: format = DXGI_FORMAT_BC7_UNORM; break;
    default: break;
  }
  std::size_t row_pitch = 0, slice_pitch = 0;
  if (FAILED(DirectX::ComputePitch(format, image.width, image.height, row_pitch, slice_pitch)))
    throw Error(ErrorKind::Parse, "Cannot compute DDS row pitch");
  DirectX::Image source{image.width, image.height, format, row_pitch, slice_pitch,
    reinterpret_cast<std::uint8_t*>(const_cast<char*>(image.data.data()))};
  DirectX::ScratchImage result;
  if (FAILED(DirectX::Decompress(source, DXGI_FORMAT_R8G8B8A8_UNORM, result)))
    throw Error(ErrorKind::Parse, "DirectXTex could not decompress DDS");
  const auto* rgba = result.GetImage(0, 0, 0);
  if (!rgba) throw Error(ErrorKind::Parse, "DirectXTex returned no DDS image");
  DdsImage decoded = image;
  decoded.format = TextureFormat::RGBA8;
  decoded.data.resize(static_cast<std::size_t>(decoded_size));
  const auto row_size = static_cast<std::size_t>(image.width) * 4;
  for (std::size_t y = 0; y < image.height; ++y)
    std::memcpy(decoded.data.data() + y * row_size, rgba->pixels + y * rgba->rowPitch, row_size);
  if (normal_map && image.format == TextureFormat::BC5) {
    for (std::size_t i = 0; i < decoded.data.size(); i += 4) {
      const float x = static_cast<unsigned char>(decoded.data[i]) / 127.5f - 1;
      const float y = static_cast<unsigned char>(decoded.data[i + 1]) / 127.5f - 1;
      const float z = std::sqrt(std::max(0.0f, 1 - x*x - y*y));
      decoded.data[i + 2] = static_cast<char>(static_cast<unsigned char>((z + 1) * 127.5f));
    }
  }
  return decoded;
}

}  // namespace sns::textures

namespace sns::textures {

DdsImage downscale_rgba(const DdsImage& image, std::uint32_t max_dimension) {
  if (image.format != TextureFormat::RGBA8) throw Error(ErrorKind::InvalidArgument, "downscale_rgba needs RGBA8");
  DdsImage out = image;
  while (max_dimension && std::max(out.width, out.height) > max_dimension && (out.width > 1 || out.height > 1)) {
    const std::uint32_t w = std::max(1u, out.width / 2), h = std::max(1u, out.height / 2);
    std::string half(static_cast<std::size_t>(w) * h * 4, '\0');
    const auto* src = reinterpret_cast<const std::uint8_t*>(out.data.data());
    auto* dst = reinterpret_cast<std::uint8_t*>(half.data());
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        for (int c = 0; c < 4; ++c) {
          unsigned sum = 0, count = 0;
          for (std::uint32_t dy = 0; dy < 2; ++dy) {
            for (std::uint32_t dx = 0; dx < 2; ++dx) {
              const std::uint32_t sx = std::min(out.width - 1, x * 2 + dx), sy = std::min(out.height - 1, y * 2 + dy);
              sum += src[(static_cast<std::size_t>(sy) * out.width + sx) * 4 + c];
              ++count;
            }
          }
          dst[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>((sum + count / 2) / count);
        }
      }
    }
    out.data = std::move(half);
    out.width = w;
    out.height = h;
  }
  return out;
}

AlphaStats measure_alpha(const DdsImage& image, std::uint8_t cutoff) {
  if (image.format != TextureFormat::RGBA8) throw Error(ErrorKind::InvalidArgument, "measure_alpha needs RGBA8");
  AlphaStats stats;
  const auto* px = reinterpret_cast<const std::uint8_t*>(image.data.data());
  const std::size_t n = image.data.size() / 4;
  if (n == 0) return stats;
  std::size_t translucent = 0, below = 0;
  double sum = 0;
  stats.min = 255;
  for (std::size_t i = 0; i < n; ++i) {
    const auto a = px[i * 4 + 3];
    stats.min = std::min(stats.min, a);
    sum += a;
    if (a < 250) ++translucent;
    if (a < cutoff) ++below;
  }
  stats.mean = static_cast<float>(sum / static_cast<double>(n));
  stats.translucent_fraction = static_cast<float>(translucent) / static_cast<float>(n);
  stats.below_cutoff_fraction = static_cast<float>(below) / static_cast<float>(n);
  stats.uses_alpha = translucent > 0;
  return stats;
}

}  // namespace sns::textures

namespace sns::textures {

std::string encode_dds(const DdsImage& rgba, TextureFormat format, bool generate_mips) {
  if (rgba.format != TextureFormat::RGBA8 || !rgba.width || !rgba.height ||
      rgba.data.size() != level_size(TextureFormat::RGBA8, rgba.width, rgba.height))
    throw Error(ErrorKind::InvalidArgument, "encode_dds needs a complete RGBA8 image");
  DXGI_FORMAT target = DXGI_FORMAT_UNKNOWN;
  switch (format) {
    case TextureFormat::RGBA8: target = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case TextureFormat::BC1: target = DXGI_FORMAT_BC1_UNORM; break;
    case TextureFormat::BC3: target = DXGI_FORMAT_BC3_UNORM; break;
    case TextureFormat::BC7: target = DXGI_FORMAT_BC7_UNORM; break;
    default: throw Error(ErrorKind::InvalidArgument, "encode_dds: unsupported target format");
  }
  DirectX::ScratchImage source;
  if (FAILED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, rgba.width, rgba.height, 1, 1)))
    throw Error(ErrorKind::Io, "encode_dds: cannot allocate the source image");
  const auto* image = source.GetImage(0, 0, 0);
  const auto row = static_cast<std::size_t>(rgba.width) * 4;
  for (std::size_t y = 0; y < rgba.height; ++y)
    std::memcpy(image->pixels + y * image->rowPitch, rgba.data.data() + y * row, row);

  DirectX::ScratchImage mipped;
  const DirectX::ScratchImage* chain = &source;
  if (generate_mips && (rgba.width > 1 || rgba.height > 1)) {
    // Non-WIC filters: WIC needs COM on the calling thread, which worker threads do not have.
    if (FAILED(DirectX::GenerateMipMaps(*image, DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC, 0, mipped)) &&
        FAILED(DirectX::GenerateMipMaps(*image, DirectX::TEX_FILTER_LINEAR | DirectX::TEX_FILTER_FORCE_NON_WIC, 0, mipped)))
      throw Error(ErrorKind::Io, "encode_dds: mip generation failed");
    chain = &mipped;
  }
  DirectX::ScratchImage compressed;
  if (target != DXGI_FORMAT_R8G8B8A8_UNORM) {
    if (FAILED(DirectX::Compress(chain->GetImages(), chain->GetImageCount(), chain->GetMetadata(), target,
                                 DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed)))
      throw Error(ErrorKind::Io, "encode_dds: block compression failed");
    chain = &compressed;
  }
  DirectX::Blob blob;
  if (FAILED(DirectX::SaveToDDSMemory(chain->GetImages(), chain->GetImageCount(), chain->GetMetadata(),
                                      DirectX::DDS_FLAGS_NONE, blob)))
    throw Error(ErrorKind::Io, "encode_dds: DDS serialization failed");
  return std::string(reinterpret_cast<const char*>(blob.GetBufferPointer()), blob.GetBufferSize());
}

}  // namespace sns::textures
