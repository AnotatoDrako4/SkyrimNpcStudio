#include "sns/visual_qa/image_comparator.hpp"
#include "sns/visual_qa/report_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#ifdef small
#undef small
#endif

namespace sns::visual_qa {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kFloatsPerVertex = rendering::kFloatsPerVertex;

nif::Vec3 sub_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

nif::Vec3 add_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

nif::Vec3 scale_v(const nif::Vec3& a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}

float dot_v(const nif::Vec3& a, const nif::Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

nif::Vec3 cross_v(const nif::Vec3& a, const nif::Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float length_v(const nif::Vec3& a) { return std::sqrt(dot_v(a, a)); }

nif::Vec3 normalize_v(const nif::Vec3& a) {
  const float len = length_v(a);
  if (len <= 1e-7f) {
    return {0.0f, 1.0f, 0.0f};
  }
  return {a.x / len, a.y / len, a.z / len};
}

struct ScopedComInit {
  HRESULT hr = S_OK;
  ScopedComInit() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
  ~ScopedComInit() {
    if (SUCCEEDED(hr)) {
      CoUninitialize();
    }
  }
};

bool is_pixel_covered(const std::uint8_t* px, const std::uint8_t* bg) {
  if (px[3] < 16) {
    return false;
  }
  if (bg[3] < 16 && px[3] >= 16) {
    return true;
  }
  const int dr = std::abs(static_cast<int>(px[0]) - static_cast<int>(bg[0]));
  const int dg = std::abs(static_cast<int>(px[1]) - static_cast<int>(bg[1]));
  const int db = std::abs(static_cast<int>(px[2]) - static_cast<int>(bg[2]));
  return (dr + dg + db) > 28;
}

void sample_dds_bilinear(const textures::DdsImage& img, float u, float v, std::uint8_t out_rgba[4]) {
  if (img.width == 0 || img.height == 0 || img.data.size() < 4ull * img.width * img.height) {
    out_rgba[0] = 200;
    out_rgba[1] = 200;
    out_rgba[2] = 200;
    out_rgba[3] = 255;
    return;
  }
  u = u - std::floor(u);
  v = v - std::floor(v);
  const float x = std::clamp(u * static_cast<float>(img.width - 1), 0.0f, static_cast<float>(img.width - 1));
  const float y = std::clamp(v * static_cast<float>(img.height - 1), 0.0f, static_cast<float>(img.height - 1));
  const std::uint32_t ix = static_cast<std::uint32_t>(x);
  const std::uint32_t iy = static_cast<std::uint32_t>(y);
  const auto* px = reinterpret_cast<const std::uint8_t*>(img.data.data()) +
                   (static_cast<std::size_t>(iy) * img.width + ix) * 4;
  out_rgba[0] = px[0];
  out_rgba[1] = px[1];
  out_rgba[2] = px[2];
  out_rgba[3] = px[3];
}

}  // namespace

std::string_view view_id(ControlledViewKind kind) {
  switch (kind) {
    case ControlledViewKind::FrontFullBody:
      return "01_front_full";
    case ControlledViewKind::BackFullBody:
      return "02_back_full";
    case ControlledViewKind::SideFullBody:
      return "03_side_full";
    case ControlledViewKind::Torso:
      return "04_torso";
    case ControlledViewKind::NeckAndHead:
      return "05_neck_head";
    case ControlledViewKind::ArmsAndWrists:
      return "06_arms_wrists";
    case ControlledViewKind::LegsAndAnkles:
      return "07_legs_ankles";
    case ControlledViewKind::FullOutfit:
      return "08_full_outfit";
  }
  return "01_front_full";
}

std::string_view view_label(ControlledViewKind kind) {
  switch (kind) {
    case ControlledViewKind::FrontFullBody:
      return "Frontal de cuerpo completo";
    case ControlledViewKind::BackFullBody:
      return "Posterior de cuerpo completo";
    case ControlledViewKind::SideFullBody:
      return "Lateral de cuerpo completo";
    case ControlledViewKind::Torso:
      return "Torso";
    case ControlledViewKind::NeckAndHead:
      return "Cuello y cabeza";
    case ControlledViewKind::ArmsAndWrists:
      return "Brazos y munecas";
    case ControlledViewKind::LegsAndAnkles:
      return "Piernas y tobillos";
    case ControlledViewKind::FullOutfit:
      return "Vestuario completo";
  }
  return "Frontal de cuerpo completo";
}

std::vector<CaptureCameraSpec> build_controlled_camera_set(const rendering::CharacterScene& scene,
                                                           std::uint32_t width,
                                                           std::uint32_t height) {
  float min_y = scene.min[1];
  float max_y = scene.max[1];
  if (!(max_y > min_y + 20.0f)) {
    min_y = 0.0f;
    max_y = 124.0f;
  }
  const float h = max_y - min_y;
  const float mid_y = min_y + h * 0.50f;
  const float dist = h * 1.4f;

  auto make_spec = [&](ControlledViewKind kind, nif::Vec3 eye, nif::Vec3 target, float half_h,
                       nif::Vec3 light_dir) {
    CaptureCameraSpec s;
    s.kind = kind;
    s.id = std::string(view_id(kind));
    s.label = std::string(view_label(kind));
    s.width = width;
    s.height = height;
    s.eye = eye;
    s.target = target;
    s.up = {0.0f, 1.0f, 0.0f};
    s.ortho_half_height = half_h;
    s.key_light_dir = normalize_v(light_dir);
    s.ambient_intensity = 0.34f;
    s.include_outfit = true;
    return s;
  };

  std::vector<CaptureCameraSpec> specs;
  specs.reserve(8);
  // 1. Frontal de cuerpo completo (+Z looking toward -Z)
  specs.push_back(make_spec(ControlledViewKind::FrontFullBody, {0.0f, mid_y, dist},
                            {0.0f, mid_y, 0.0f}, h * 0.56f, {0.30f, 0.40f, 0.86f}));
  // 2. Posterior (-Z looking toward +Z)
  specs.push_back(make_spec(ControlledViewKind::BackFullBody, {0.0f, mid_y, -dist},
                            {0.0f, mid_y, 0.0f}, h * 0.56f, {-0.30f, 0.40f, -0.86f}));
  // 3. Lateral (+X right/left profile looking toward -X)
  specs.push_back(make_spec(ControlledViewKind::SideFullBody, {dist, mid_y, 0.0f},
                            {0.0f, mid_y, 0.0f}, h * 0.56f, {0.86f, 0.35f, 0.35f}));
  // 4. Torso
  const float torso_y = min_y + h * 0.64f;
  specs.push_back(make_spec(ControlledViewKind::Torso, {0.0f, torso_y, dist},
                            {0.0f, torso_y, 0.0f}, h * 0.24f, {0.25f, 0.35f, 0.90f}));
  // 5. Cuello y cabeza
  const float head_y = min_y + h * 0.86f;
  specs.push_back(make_spec(ControlledViewKind::NeckAndHead, {0.0f, head_y, dist},
                            {0.0f, head_y, 0.0f}, h * 0.16f, {0.20f, 0.30f, 0.93f}));
  // 6. Brazos y muñecas
  const float arms_y = min_y + h * 0.58f;
  specs.push_back(make_spec(ControlledViewKind::ArmsAndWrists, {0.0f, arms_y, dist},
                            {0.0f, arms_y, 0.0f}, h * 0.34f, {0.30f, 0.40f, 0.86f}));
  // 7. Piernas y tobillos
  const float legs_y = min_y + h * 0.25f;
  specs.push_back(make_spec(ControlledViewKind::LegsAndAnkles, {0.0f, legs_y, dist},
                            {0.0f, legs_y, 0.0f}, h * 0.28f, {0.30f, 0.35f, 0.88f}));
  // 8. Vestuario completo (3/4 studio angle)
  specs.push_back(make_spec(ControlledViewKind::FullOutfit, {dist * 0.55f, mid_y, dist * 0.85f},
                            {0.0f, mid_y, 0.0f}, h * 0.58f, {0.48f, 0.42f, 0.77f}));
  return specs;
}

bool save_png(const std::filesystem::path& path, const RgbaBuffer& image, std::string* error_out) {
  if (image.width == 0 || image.height == 0 ||
      image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4) {
    if (error_out) *error_out = "Invalid RgbaBuffer dimensions.";
    return false;
  }

  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  ScopedComInit com;
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (error_out) *error_out = "CoCreateInstance(CLSID_WICImagingFactory) failed.";
    return false;
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) return false;

  hr = stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    if (error_out) *error_out = "InitializeFromFilename failed for " + path.string();
    return false;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) return false;

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) return false;

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> props;
  hr = encoder->CreateNewFrame(&frame, &props);
  if (FAILED(hr)) return false;

  hr = frame->Initialize(props.Get());
  if (FAILED(hr)) return false;

  hr = frame->SetSize(image.width, image.height);
  if (FAILED(hr)) return false;

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr)) return false;

  // Convert RGBA8 -> BGRA8 for WIC PNG encoder compatibility.
  std::vector<std::uint8_t> bgra(image.pixels.size());
  for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
    bgra[i + 0] = image.pixels[i + 2];
    bgra[i + 1] = image.pixels[i + 1];
    bgra[i + 2] = image.pixels[i + 0];
    bgra[i + 3] = image.pixels[i + 3];
  }

  const UINT stride = image.width * 4;
  const UINT total_bytes = stride * image.height;
  hr = frame->WritePixels(image.height, stride, total_bytes, bgra.data());
  if (FAILED(hr)) return false;

  hr = frame->Commit();
  if (FAILED(hr)) return false;

  hr = encoder->Commit();
  return SUCCEEDED(hr);
}

bool load_png(const std::filesystem::path& path, RgbaBuffer& image_out, std::string* error_out) {
  ScopedComInit com;
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (error_out) *error_out = "CoCreateInstance(CLSID_WICImagingFactory) failed.";
    return false;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                          WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr)) {
    if (error_out) *error_out = "CreateDecoderFromFilename failed for " + path.string();
    return false;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) return false;

  UINT w = 0, h = 0;
  hr = frame->GetSize(&w, &h);
  if (FAILED(hr) || w == 0 || h == 0) return false;

  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) return false;

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                             nullptr, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) return false;

  image_out.width = w;
  image_out.height = h;
  image_out.pixels.resize(static_cast<std::size_t>(w) * h * 4);
  const UINT stride = w * 4;
  hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(image_out.pixels.size()),
                             image_out.pixels.data());
  return SUCCEEDED(hr);
}

RgbaBuffer render_controlled_view(const rendering::CharacterScene& scene,
                                  const CaptureCameraSpec& spec) {
  RgbaBuffer out;
  out.width = std::max(spec.width, 64u);
  out.height = std::max(spec.height, 64u);
  out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 4, 0);

  // Fill background with dark studio slate (RGB = 22, 24, 30, Alpha = 0 so background is explicit).
  for (std::size_t i = 0; i < out.pixels.size(); i += 4) {
    out.pixels[i + 0] = 22;
    out.pixels[i + 1] = 24;
    out.pixels[i + 2] = 30;
    out.pixels[i + 3] = 0;
  }

  std::vector<float> zbuf(static_cast<std::size_t>(out.width) * out.height,
                          std::numeric_limits<float>::max());

  const nif::Vec3 forward = normalize_v(sub_v(spec.target, spec.eye));
  nif::Vec3 right = cross_v(forward, spec.up);
  if (length_v(right) < 1e-4f) {
    right = {1.0f, 0.0f, 0.0f};
  } else {
    right = normalize_v(right);
  }
  const nif::Vec3 cam_up = normalize_v(cross_v(right, forward));
  const nif::Vec3 light_dir = normalize_v(spec.key_light_dir);

  const float half_h = std::max(spec.ortho_half_height, 5.0f);
  const float aspect = static_cast<float>(out.width) / static_cast<float>(out.height);
  const float half_w = half_h * aspect;

  auto project_vertex = [&](const nif::Vec3& p) -> nif::Vec3 {
    const nif::Vec3 rel = sub_v(p, spec.target);
    const float cx = dot_v(rel, right);
    const float cy = dot_v(rel, cam_up);
    const float cz = dot_v(sub_v(p, spec.eye), forward);
    const float sx = (cx / half_w * 0.5f + 0.5f) * static_cast<float>(out.width - 1);
    const float sy = (0.5f - cy / half_h * 0.5f) * static_cast<float>(out.height - 1);
    return {sx, sy, cz};
  };

  for (const auto& part : scene.parts) {
    if (!part.shown_by_default) continue;
    if (!spec.include_outfit && part.group == "outfit") continue;

    const std::size_t vcount = part.vertices.size() / kFloatsPerVertex;
    if (vcount < 3 || part.indices.size() < 3) continue;

    const textures::DdsImage* dds_img = nullptr;
    if (!part.diffuse.empty()) {
      auto it = scene.textures.find(part.diffuse);
      if (it != scene.textures.end() && it->second != nullptr) {
        dds_img = it->second.get();
      }
    }

    // Fallback material tint when no texture is loaded in memory.
    const SurfaceRole role = classify_part_role(part);
    std::uint8_t base_r = 185, base_g = 155, base_b = 140;
    if (part.tint.has_value()) {
      base_r = static_cast<std::uint8_t>(std::clamp(part.tint->r * 255.0f, 35.0f, 245.0f));
      base_g = static_cast<std::uint8_t>(std::clamp(part.tint->g * 255.0f, 35.0f, 245.0f));
      base_b = static_cast<std::uint8_t>(std::clamp(part.tint->b * 255.0f, 35.0f, 245.0f));
    } else if (role == SurfaceRole::VisibleGarment) {
      std::size_t h = std::hash<std::string>{}(part.label.empty() ? part.shape_name : part.label);
      base_r = static_cast<std::uint8_t>(90 + (h & 0x5F));
      base_g = static_cast<std::uint8_t>(70 + ((h >> 8) & 0x5F));
      base_b = static_cast<std::uint8_t>(85 + ((h >> 16) & 0x5F));
    } else if (role == SurfaceRole::EmbeddedBodyInOutfit) {
      base_r = 212;
      base_g = 165;
      base_b = 145;
    }

    for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
      const std::uint32_t i0 = part.indices[i + 0];
      const std::uint32_t i1 = part.indices[i + 1];
      const std::uint32_t i2 = part.indices[i + 2];
      if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;

      const float* v0 = part.vertices.data() + i0 * kFloatsPerVertex;
      const float* v1 = part.vertices.data() + i1 * kFloatsPerVertex;
      const float* v2 = part.vertices.data() + i2 * kFloatsPerVertex;

      const nif::Vec3 p0{v0[0], v0[1], v0[2]};
      const nif::Vec3 p1{v1[0], v1[1], v1[2]};
      const nif::Vec3 p2{v2[0], v2[1], v2[2]};
      if (!std::isfinite(p0.x) || !std::isfinite(p1.x) || !std::isfinite(p2.x)) continue;

      const nif::Vec3 s0 = project_vertex(p0);
      const nif::Vec3 s1 = project_vertex(p1);
      const nif::Vec3 s2 = project_vertex(p2);

      const float min_fx = std::floor(std::min({s0.x, s1.x, s2.x}));
      const float max_fx = std::ceil(std::max({s0.x, s1.x, s2.x}));
      const float min_fy = std::floor(std::min({s0.y, s1.y, s2.y}));
      const float max_fy = std::ceil(std::max({s0.y, s1.y, s2.y}));

      if (max_fx < 0.0f || min_fx >= static_cast<float>(out.width) || max_fy < 0.0f ||
          min_fy >= static_cast<float>(out.height)) {
        continue;
      }

      const int min_x = std::max(0, static_cast<int>(min_fx));
      const int max_x = std::min(static_cast<int>(out.width) - 1, static_cast<int>(max_fx));
      const int min_y = std::max(0, static_cast<int>(min_fy));
      const int max_y = std::min(static_cast<int>(out.height) - 1, static_cast<int>(max_fy));

      const float area = (s1.x - s0.x) * (s2.y - s0.y) - (s2.x - s0.x) * (s1.y - s0.y);
      if (std::abs(area) < 1e-4f) continue;
      const float inv_area = 1.0f / area;

      const nif::Vec3 n0{v0[3], v0[4], v0[5]};
      const nif::Vec3 n1{v1[3], v1[4], v1[5]};
      const nif::Vec3 n2{v2[3], v2[4], v2[5]};

      for (int py = min_y; py <= max_y; ++py) {
        const float fy = static_cast<float>(py) + 0.5f;
        for (int px = min_x; px <= max_x; ++px) {
          const float fx = static_cast<float>(px) + 0.5f;
          const float w0 = ((s1.x - fx) * (s2.y - fy) - (s2.x - fx) * (s1.y - fy)) * inv_area;
          const float w1 = ((s2.x - fx) * (s0.y - fy) - (s0.x - fx) * (s2.y - fy)) * inv_area;
          const float w2 = 1.0f - w0 - w1;
          if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

          const float z = w0 * s0.z + w1 * s1.z + w2 * s2.z;
          const std::size_t pix_idx = static_cast<std::size_t>(py) * out.width + static_cast<std::size_t>(px);
          if (z >= zbuf[pix_idx]) continue;

          std::uint8_t tex_rgba[4] = {base_r, base_g, base_b, 255};
          if (dds_img != nullptr) {
            const float u = w0 * v0[6] + w1 * v1[6] + w2 * v2[6];
            const float v = w0 * v0[7] + w1 * v1[7] + w2 * v2[7];
            sample_dds_bilinear(*dds_img, u, v, tex_rgba);
            if (part.alpha_mode == rendering::AlphaMode::Mask &&
                static_cast<float>(tex_rgba[3]) / 255.0f < part.alpha_cutoff) {
              continue;
            }
            if (tex_rgba[3] < 20) continue;
          }

          zbuf[pix_idx] = z;
          const nif::Vec3 n = normalize_v({w0 * n0.x + w1 * n1.x + w2 * n2.x,
                                           w0 * n0.y + w1 * n1.y + w2 * n2.y,
                                           w0 * n0.z + w1 * n1.z + w2 * n2.z});
          const float ndotl = std::max(0.0f, dot_v(n, light_dir));
          const float rim = std::abs(dot_v(n, forward));
          const float shade = std::clamp(
              spec.ambient_intensity + (1.0f - spec.ambient_intensity) * (0.68f * ndotl + 0.32f * rim),
              0.18f, 1.12f);

          std::uint8_t* dst = out.pixels.data() + pix_idx * 4;
          dst[0] = static_cast<std::uint8_t>(std::clamp(tex_rgba[0] * shade, 0.0f, 255.0f));
          dst[1] = static_cast<std::uint8_t>(std::clamp(tex_rgba[1] * shade, 0.0f, 255.0f));
          dst[2] = static_cast<std::uint8_t>(std::clamp(tex_rgba[2] * shade, 0.0f, 255.0f));
          dst[3] = 255;
        }
      }
    }
  }

  return out;
}

VisualComparisonResult compare_images(std::string_view id, const RgbaBuffer& before,
                                      const RgbaBuffer& after,
                                      const std::filesystem::path& diff_png_out,
                                      const VisualComparisonOptions& options) {
  VisualComparisonResult res;
  res.view_id = std::string(id);
  if (before.width != after.width || before.height != after.height || before.width == 0 ||
      before.height == 0) {
    res.dimensions_match = false;
    res.verdict = QaVerdict::Fail;
    res.explanation = "Image dimensions do not match or are empty.";
    return res;
  }

  res.width = before.width;
  res.height = before.height;
  res.total_pixels = static_cast<std::size_t>(res.width) * res.height;

  const std::uint8_t* bg_before = before.pixels.data();
  const std::uint8_t* bg_after = after.pixels.data();

  std::vector<std::uint8_t> raw_exceeds(res.total_pixels, 0);
  std::vector<std::uint8_t> cov_b(res.total_pixels, 0);
  std::vector<std::uint8_t> cov_a(res.total_pixels, 0);

  double abs_diff_sum = 0.0;
  double sq_diff_sum = 0.0;

  for (std::size_t idx = 0; idx < res.total_pixels; ++idx) {
    const std::uint8_t* pb = before.pixels.data() + idx * 4;
    const std::uint8_t* pa = after.pixels.data() + idx * 4;

    const bool cb = is_pixel_covered(pb, bg_before);
    const bool ca = is_pixel_covered(pa, bg_after);
    cov_b[idx] = cb ? 1 : 0;
    cov_a[idx] = ca ? 1 : 0;
    if (cb) ++res.covered_before;
    if (ca) ++res.covered_after;

    const int dr = std::abs(static_cast<int>(pa[0]) - static_cast<int>(pb[0]));
    const int dg = std::abs(static_cast<int>(pa[1]) - static_cast<int>(pb[1]));
    const int db = std::abs(static_cast<int>(pa[2]) - static_cast<int>(pb[2]));
    const int max_ch = std::max({dr, dg, db});
    const double mean_ch = (dr + dg + db) / 3.0;

    abs_diff_sum += mean_ch;
    sq_diff_sum += mean_ch * mean_ch;

    if (max_ch > 0 || cb != ca) {
      ++res.raw_differing_pixels;
    }
    if (max_ch > options.channel_noise_tolerance || cb != ca) {
      raw_exceeds[idx] = 1;
    }
  }

  res.mean_abs_diff = abs_diff_sum / static_cast<double>(res.total_pixels);
  res.rms_diff = std::sqrt(sq_diff_sum / static_cast<double>(res.total_pixels));

  RgbaBuffer diff_img;
  diff_img.width = res.width;
  diff_img.height = res.height;
  diff_img.pixels.resize(res.total_pixels * 4, 0);

  // Grid of 4x4 macro-blocks to localize modified anatomical regions cleanly.
  constexpr std::uint32_t kGridN = 4;
  struct CellStats {
    std::uint32_t min_x = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t min_y = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t max_x = 0;
    std::uint32_t max_y = 0;
    std::size_t changed = 0;
    std::size_t disappeared = 0;
    std::size_t appeared = 0;
  };
  std::array<CellStats, kGridN * kGridN> grid{};

  for (std::uint32_t y = 0; y < res.height; ++y) {
    for (std::uint32_t x = 0; x < res.width; ++x) {
      const std::size_t idx = static_cast<std::size_t>(y) * res.width + x;
      const std::uint8_t* pa = after.pixels.data() + idx * 4;
      std::uint8_t* pd = diff_img.pixels.data() + idx * 4;

      // Dim grayscale baseline in diff heatmap
      const std::uint8_t lum = static_cast<std::uint8_t>((pa[0] + pa[1] + pa[2]) / 9);
      pd[0] = lum;
      pd[1] = lum;
      pd[2] = lum;
      pd[3] = 255;

      if (!raw_exceeds[idx]) {
        continue;
      }

      // Check 3x3 neighborhood support to filter 1-px GPU/AA edge noise.
      int neighbors = 0;
      if (options.ignore_isolated_aa_edge_pixels) {
        for (int dy = -1; dy <= 1; ++dy) {
          const int ny = static_cast<int>(y) + dy;
          if (ny < 0 || ny >= static_cast<int>(res.height)) continue;
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = static_cast<int>(x) + dx;
            if (nx < 0 || nx >= static_cast<int>(res.width)) continue;
            if (raw_exceeds[static_cast<std::size_t>(ny) * res.width + static_cast< std::size_t>(nx)]) {
              ++neighbors;
            }
          }
        }
      } else {
        neighbors = 9;
      }

      if (neighbors < 3) {
        ++res.noise_filtered_pixels;
        // Subtle blue tint for filtered AA/lighting noise
        pd[0] = lum;
        pd[1] = static_cast<std::uint8_t>(std::min(255, lum + 35));
        pd[2] = static_cast<std::uint8_t>(std::min(255, lum + 70));
        continue;
      }

      ++res.structural_changed_pixels;
      const std::uint32_t gx = std::min(kGridN - 1, (x * kGridN) / res.width);
      const std::uint32_t gy = std::min(kGridN - 1, (y * kGridN) / res.height);
      auto& cell = grid[gy * kGridN + gx];
      cell.min_x = std::min(cell.min_x, x);
      cell.min_y = std::min(cell.min_y, y);
      cell.max_x = std::max(cell.max_x, x);
      cell.max_y = std::max(cell.max_y, y);
      ++cell.changed;

      if (cov_b[idx] && !cov_a[idx]) {
        ++res.disappeared_geometry_pixels;
        ++cell.disappeared;
        // Bright magenta for disappeared geometry
        pd[0] = 255;
        pd[1] = 25;
        pd[2] = 190;
      } else if (!cov_b[idx] && cov_a[idx]) {
        ++res.newly_visible_pixels;
        ++cell.appeared;
        // Bright green/cyan for newly visible / expanded geometry
        pd[0] = 30;
        pd[1] = 235;
        pd[2] = 120;
      } else {
        // Amber/red for modified surface shading/geometry
        pd[0] = 255;
        pd[1] = 140;
        pd[2] = 20;
      }
    }
  }

  res.noise_filtered_pixels +=
      (res.raw_differing_pixels > res.structural_changed_pixels + res.noise_filtered_pixels)
          ? (res.raw_differing_pixels - res.structural_changed_pixels - res.noise_filtered_pixels)
          : 0;
  res.structural_change_pct =
      static_cast<double>(res.structural_changed_pixels) * 100.0 / static_cast<double>(res.total_pixels);

  for (std::uint32_t gy = 0; gy < kGridN; ++gy) {
    for (std::uint32_t gx = 0; gx < kGridN; ++gx) {
      const auto& c = grid[gy * kGridN + gx];
      if (c.changed < 12) continue;
      ModifiedPixelRegion reg;
      reg.min_x = c.min_x;
      reg.min_y = c.min_y;
      reg.max_x = c.max_x;
      reg.max_y = c.max_y;
      reg.changed_pixels = c.changed;
      reg.disappeared_pixels = c.disappeared;
      reg.appeared_pixels = c.appeared;
      if (gy == 0) {
        reg.anatomical_hint = "Head / Neck";
      } else if (gy == 1) {
        reg.anatomical_hint = (gx == 0 || gx == 3) ? "Shoulders / Arms" : "Chest / Torso";
      } else if (gy == 2) {
        reg.anatomical_hint = (gx == 0 || gx == 3) ? "Wrists / Hands" : "Waist / Pelvis / Hip";
      } else {
        reg.anatomical_hint = "Legs / Ankles / Feet";
      }
      res.modified_regions.push_back(std::move(reg));
    }
  }

  if (res.covered_after < res.total_pixels / 40) {
    res.unexpected_transparent_or_empty_zone = true;
  }
  if (res.covered_before > 0) {
    const double lost_ratio =
        static_cast<double>(res.disappeared_geometry_pixels) / static_cast<double>(res.covered_before);
    if (lost_ratio >= options.disappeared_coverage_warning_ratio ||
        static_cast<double>(res.covered_after) < static_cast<double>(res.covered_before) * 0.85) {
      res.geometry_disappeared_warning = true;
    }
  }

  if (!diff_png_out.empty()) {
    if (save_png(diff_png_out, diff_img)) {
      res.diff_image_path = diff_png_out.string();
    }
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "View '" << res.view_id << "': structural change=" << res.structural_change_pct
      << "% (" << res.structural_changed_pixels << " px, filtered AA/GPU noise="
      << res.noise_filtered_pixels << " px, RMS=" << res.rms_diff << ").";
  if (res.geometry_disappeared_warning) {
    oss << " WARNING: Significant geometry disappeared (" << res.disappeared_geometry_pixels << " px lost).";
    res.verdict = QaVerdict::Fail;
  } else if (res.unexpected_transparent_or_empty_zone) {
    oss << " WARNING: Capture contains unexpected blank/transparent character zone.";
    res.verdict = QaVerdict::Fail;
  } else {
    res.verdict = QaVerdict::Pass;
  }
  res.explanation = oss.str();
  return res;
}

std::vector<CaptureArtifact> capture_controlled_scene_views(const rendering::CharacterScene& scene,
                                                            const std::filesystem::path& out_dir,
                                                            std::uint32_t width,
                                                            std::uint32_t height) {
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);

  const auto specs = build_controlled_camera_set(scene, width, height);
  std::vector<CaptureArtifact> artifacts;
  artifacts.reserve(specs.size());

  nlohmann::json meta_arr = nlohmann::json::array();

  for (const auto& spec : specs) {
    const RgbaBuffer buf = render_controlled_view(scene, spec);
    const std::filesystem::path png_path = out_dir / (spec.id + ".png");
    save_png(png_path, buf);

    CaptureArtifact art;
    art.spec = spec;
    art.image_path = png_path.string();
    art.sha256 = sha256_file(png_path);

    const std::size_t total_px = static_cast<std::size_t>(buf.width) * buf.height;
    const std::uint8_t* bg = buf.pixels.data();
    for (std::size_t idx = 0; idx < total_px; ++idx) {
      const std::uint8_t* p = buf.pixels.data() + idx * 4;
      if (is_pixel_covered(p, bg)) {
        ++art.covered_pixels;
        if (p[0] > 250 && p[1] > 250 && p[2] > 250) {
          ++art.near_white_pixels;
        }
      }
    }
    art.coverage_ratio =
        total_px > 0 ? static_cast<double>(art.covered_pixels) / static_cast<double>(total_px) : 0.0;
    if (art.coverage_ratio < 0.03) {
      art.unexpected_blank_zone = true;
      art.blank_zone_reason = "Character coverage ratio below 3% of frame.";
    }

    meta_arr.push_back(to_json(art));
    artifacts.push_back(std::move(art));
  }

  std::ofstream meta_out(out_dir / "captures-metadata.json", std::ios::binary);
  if (meta_out) {
    meta_out << meta_arr.dump(2);
  }

  return artifacts;
}

nlohmann::json to_json(const CaptureArtifact& art) {
  return {{"id", art.spec.id},
          {"label", art.spec.label},
          {"width", art.spec.width},
          {"height", art.spec.height},
          {"eye", {art.spec.eye.x, art.spec.eye.y, art.spec.eye.z}},
          {"target", {art.spec.target.x, art.spec.target.y, art.spec.target.z}},
          {"up", {art.spec.up.x, art.spec.up.y, art.spec.up.z}},
          {"ortho_half_height", art.spec.ortho_half_height},
          {"key_light_dir",
           {art.spec.key_light_dir.x, art.spec.key_light_dir.y, art.spec.key_light_dir.z}},
          {"ambient_intensity", art.spec.ambient_intensity},
          {"image_path", art.image_path},
          {"sha256", art.sha256},
          {"covered_pixels", art.covered_pixels},
          {"near_white_pixels", art.near_white_pixels},
          {"coverage_ratio", art.coverage_ratio},
          {"unexpected_blank_zone", art.unexpected_blank_zone},
          {"blank_zone_reason", art.blank_zone_reason}};
}

nlohmann::json to_json(const VisualComparisonResult& cmp) {
  nlohmann::json regs = nlohmann::json::array();
  for (const auto& r : cmp.modified_regions) {
    regs.push_back({{"min_x", r.min_x},
                    {"min_y", r.min_y},
                    {"max_x", r.max_x},
                    {"max_y", r.max_y},
                    {"changed_pixels", r.changed_pixels},
                    {"disappeared_pixels", r.disappeared_pixels},
                    {"appeared_pixels", r.appeared_pixels},
                    {"anatomical_hint", r.anatomical_hint}});
  }

  return {{"view_id", cmp.view_id},
          {"before_path", cmp.before_path},
          {"after_path", cmp.after_path},
          {"diff_image_path", cmp.diff_image_path},
          {"verdict", to_string(cmp.verdict)},
          {"dimensions_match", cmp.dimensions_match},
          {"width", cmp.width},
          {"height", cmp.height},
          {"total_pixels", cmp.total_pixels},
          {"covered_before", cmp.covered_before},
          {"covered_after", cmp.covered_after},
          {"raw_differing_pixels", cmp.raw_differing_pixels},
          {"noise_filtered_pixels", cmp.noise_filtered_pixels},
          {"structural_changed_pixels", cmp.structural_changed_pixels},
          {"disappeared_geometry_pixels", cmp.disappeared_geometry_pixels},
          {"newly_visible_pixels", cmp.newly_visible_pixels},
          {"structural_change_pct", cmp.structural_change_pct},
          {"mean_abs_diff", cmp.mean_abs_diff},
          {"rms_diff", cmp.rms_diff},
          {"geometry_disappeared_warning", cmp.geometry_disappeared_warning},
          {"unexpected_transparent_or_empty_zone", cmp.unexpected_transparent_or_empty_zone},
          {"modified_regions", std::move(regs)},
          {"explanation", cmp.explanation}};
}

}  // namespace sns::visual_qa
