#include "sns/face/face_tint.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "sns/core/error.hpp"

namespace sns::face {

namespace {

// Red channel of an RGBA8 image sampled bilinearly at the center of output texel (x, y).
float sample_red(const textures::DdsImage& img, std::uint32_t x, std::uint32_t y, std::uint32_t size) {
  const float u = (static_cast<float>(x) + 0.5f) * static_cast<float>(img.width) / static_cast<float>(size) - 0.5f;
  const float v = (static_cast<float>(y) + 0.5f) * static_cast<float>(img.height) / static_cast<float>(size) - 0.5f;
  const auto clamp_x = [&](int i) { return static_cast<std::uint32_t>(std::clamp(i, 0, static_cast<int>(img.width) - 1)); };
  const auto clamp_y = [&](int i) { return static_cast<std::uint32_t>(std::clamp(i, 0, static_cast<int>(img.height) - 1)); };
  const int x0 = static_cast<int>(std::floor(u));
  const int y0 = static_cast<int>(std::floor(v));
  const float fx = u - static_cast<float>(x0);
  const float fy = v - static_cast<float>(y0);
  auto at = [&](int xi, int yi) {
    const auto offset = (static_cast<std::size_t>(clamp_y(yi)) * img.width + clamp_x(xi)) * 4;
    return static_cast<unsigned char>(img.data[offset]) / 255.0f;
  };
  const float top = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
  const float bottom = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
  return top * (1 - fy) + bottom * fy;
}

}  // namespace

FaceTintBuild compose_face_tint(const FaceRecipe& recipe, FaceAssetCache& assets, std::uint32_t size) {
  FaceTintBuild build;
  const auto& v = recipe.values;
  const std::array<float, 3> skin{v.skin_tone.r / 255.0f, v.skin_tone.g / 255.0f, v.skin_tone.b / 255.0f};
  std::vector<float> rgb(static_cast<std::size_t>(size) * size * 3);
  for (std::size_t i = 0; i < rgb.size(); i += 3) {
    rgb[i] = skin[0];
    rgb[i + 1] = skin[1];
    rgb[i + 2] = skin[2];
  }

  for (const auto& layer : v.tints) {
    if (layer.value <= 0.0f) continue;
    const auto* mask = recipe.tint_mask(layer.index);
    if (!mask) {
      build.issues.push_back({"warning", "TintMaskUnknown",
                              std::format("Tint layer {} has no mask in the race for this sex; it is skipped", layer.index),
                              recipe.race_editor_id});
      continue;
    }
    const auto raw = assets.bytes(mask->mask_path);
    if (!raw) {
      build.issues.push_back({"warning", "TintMaskMissing", "Tint mask texture not found; layer skipped", mask->mask_path});
      continue;
    }
    textures::DdsImage image;
    try {
      image = textures::decode_rgba(textures::parse_dds(*raw, 1024));
    } catch (const std::exception& e) {
      build.issues.push_back({"warning", "TintMaskUnreadable", e.what(), mask->mask_path});
      continue;
    }
    const bool skin_layer = mask->mask_type == kTintMaskSkinTone;
    const std::array<float, 3> color = skin_layer ? skin
                                                  : std::array<float, 3>{layer.color.r / 255.0f, layer.color.g / 255.0f,
                                                                         layer.color.b / 255.0f};
    const float strength = skin_layer ? 1.0f : std::clamp(layer.value, 0.0f, 1.0f);
    for (std::uint32_t y = 0; y < size; ++y) {
      for (std::uint32_t x = 0; x < size; ++x) {
        const float a = sample_red(image, x, y, size) * strength;
        if (a <= 0.0f) continue;
        auto* px = &rgb[(static_cast<std::size_t>(y) * size + x) * 3];
        for (int c = 0; c < 3; ++c) px[c] = px[c] * (1.0f - a) + color[static_cast<std::size_t>(c)] * a;
      }
    }
    build.layers.push_back(std::format("{}:{} ({:.2f})", layer.index, mask->mask_path, strength));
  }

  auto& out = build.rgba;
  out.width = out.height = out.source_width = out.source_height = size;
  out.format = textures::TextureFormat::RGBA8;
  out.has_alpha = false;
  out.data.resize(static_cast<std::size_t>(size) * size * 4);
  for (std::size_t i = 0, j = 0; i < rgb.size(); i += 3, j += 4) {
    for (int c = 0; c < 3; ++c) {
      out.data[j + static_cast<std::size_t>(c)] =
          static_cast<char>(static_cast<unsigned char>(std::lround(std::clamp(rgb[i + static_cast<std::size_t>(c)], 0.0f, 1.0f) * 255.0f)));
    }
    out.data[j + 3] = static_cast<char>(255);
  }
  return build;
}

std::string encode_face_tint(const textures::DdsImage& rgba) {
  return textures::encode_dds(rgba, textures::TextureFormat::BC3, true);
}

}  // namespace sns::face
