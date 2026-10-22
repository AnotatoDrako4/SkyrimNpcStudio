#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <NifFile.hpp>
#include <lz4frame.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <array>
#include <cmath>
#include "temp_dir.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/assets/bsa_archive.hpp"
#include "sns/core/error.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/rendering/scene.hpp"
#include "sns/rendering/smp_xml.hpp"
#include "sns/textures/dds.hpp"

namespace {
void put(std::string& b, std::size_t offset, std::uint32_t v) { std::memcpy(b.data()+offset, &v, 4); }
std::string dds() {
  std::string b(136, '\0'); b.replace(0,4,"DDS ");
  put(b,4,124); put(b,12,4); put(b,16,4); put(b,76,32); put(b,80,4); b.replace(84,4,"DXT1");
  return b;
}
std::string bsa(std::string data, bool compress, bool embedded = false) {
  const std::string folder("meshes\0",7), filename("test.nif\0",9);
  std::string payload;
  if (compress) {
    payload.resize(4 + LZ4F_compressFrameBound(data.size(),nullptr)); put(payload,0,static_cast<std::uint32_t>(data.size()));
    const auto n=LZ4F_compressFrame(payload.data()+4,payload.size()-4,data.data(),data.size(),nullptr);
    payload.resize(n+4);
  } else payload=data;
  if (embedded) payload=std::string(1,static_cast<char>(8))+"test.nif"+payload;
  const auto offset=36+24+1+folder.size()+16+filename.size();
  std::string b(offset,'\0'); b.replace(0,4,std::string("BSA\0",4));
  put(b,4,105); put(b,8,36); put(b,12,3u | (compress?4u:0u) | (embedded?0x100u:0u));
  put(b,16,1); put(b,20,1); put(b,24,static_cast<std::uint32_t>(folder.size())); put(b,28,static_cast<std::uint32_t>(filename.size()));
  put(b,44,1); put(b,52,static_cast<std::uint32_t>(60+filename.size()));
  b[60]=static_cast<char>(folder.size()); b.replace(61,folder.size(),folder);
  const auto record=61+folder.size(); put(b,record+8,static_cast<std::uint32_t>(payload.size())); put(b,record+12,static_cast<std::uint32_t>(offset));
  b.replace(record+16,filename.size(),filename); b+=payload; return b;
}
}
TEST_CASE("BSA 105 reads LZ4, plain and embedded files", "[assets]") {
  sns::test::TempDir dir;
  for (bool compressed : {false,true}) for (bool embedded : {false,true}) {
    const auto file=dir.write("test.bsa", bsa("real fixture bytes",compressed,embedded));
    auto archive=sns::assets::BsaArchive::open(file);
    CHECK(archive->file_count()==1);
    CHECK(archive->read("MESHES/test.nif")=="real fixture bytes");
    CHECK_FALSE(archive->read("missing.nif").has_value());
  }
}
TEST_CASE("BSA rejects corrupt headers and compressed frames", "[assets]") {
  sns::test::TempDir dir;
  CHECK_THROWS_AS(sns::assets::BsaArchive::open(dir.write("bad.bsa", std::string(36,'\0'))), sns::Error);
  auto bad=bsa("fixture",true); bad.back()^=0x55;
  auto archive=sns::assets::BsaArchive::open(dir.write("bad-frame.bsa",bad));
  CHECK_THROWS_AS(archive->read("meshes/test.nif"),sns::Error);
}
TEST_CASE("DDS block sizes and malformed data", "[textures]") {
  using namespace sns::textures;
  auto bytes=dds(); auto image=parse_dds(bytes);
  CHECK(image.width==4); CHECK(image.height==4); CHECK(image.format==TextureFormat::BC1); CHECK(image.data.size()==8);
  const auto rgba=decode_rgba(image); CHECK(rgba.format==TextureFormat::RGBA8); CHECK(rgba.data.size()==64);
  CHECK(static_cast<unsigned char>(rgba.data[3])==255);
  CHECK(level_size(TextureFormat::BC7,5,7)==64);
  CHECK_THROWS_AS(parse_dds(bytes.substr(0,130)),sns::Error);
  put(bytes,16,0); CHECK_THROWS_AS(parse_dds(bytes),sns::Error);
  image.data.clear(); CHECK_THROWS_AS(decode_rgba(image),sns::Error);
}
TEST_CASE("nifly roundtrip extracts actual geometry and transforms", "[nif]") {
  nifly::NifFile file; file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> verts{{0,0,0},{10,0,0},{0,10,0}};
  std::vector<nifly::Triangle> tris{{0,1,2}};
  std::vector<nifly::Vector2> uv{{0,0},{1,0},{0,1}};
  auto* shape=file.CreateShapeFromData("Fixture",&verts,&tris,&uv);
  REQUIRE(shape!=nullptr);
  std::ostringstream out(std::ios::binary); REQUIRE(file.Save(out)==0);
  const auto parsed=sns::nif::load_mesh(out.str(),"synthetic.nif");
  REQUIRE(parsed.shapes.size()==1); CHECK(parsed.shapes[0].positions.size()==3); CHECK(parsed.shapes[0].indices.size()==3);
  CHECK(parsed.shapes[0].uvs[1].u==Catch::Approx(1));
  CHECK_THROWS_AS(sns::nif::load_mesh("not a nif","invalid"),sns::Error);
}
TEST_CASE("Skinning applies bone pose and reports unresolved bones", "[scene]") {
  sns::nif::Shape shape; shape.skinned=true; shape.positions={{0,0,1}}; shape.normals={{0,0,1}};
  shape.bones={"bone"}; shape.skin_to_bone.resize(1); shape.influences.resize(1); shape.influences[0].weight[0]=1;
  sns::nif::Skeleton skeleton; sns::nif::Bone bone; bone.name="bone"; bone.to_global.translation={0,0,10}; skeleton.bones.push_back(bone);
  CHECK(sns::rendering::apply_skinning(shape,skeleton).missing.empty()); CHECK(shape.positions[0].z==Catch::Approx(11));
  skeleton.bones.clear(); CHECK(sns::rendering::apply_skinning(shape,skeleton).missing.size()==1);
  CHECK(shape.positions[0].z==Catch::Approx(11));  // unresolved bone keeps the bind pose, never collapses to the origin
}
TEST_CASE("Weight morph rejects mismatched topology and preserves endpoints", "[scene]") {
  sns::nif::Shape heavy,thin; heavy.positions={{0,0,10}}; thin.positions={{0,0,0}};
  CHECK(sns::rendering::blend_by_weight(heavy,thin,50)); CHECK(heavy.positions[0].z==Catch::Approx(5));
  heavy.indices={0,0,0}; CHECK_FALSE(sns::rendering::blend_by_weight(heavy,thin,50));
  CHECK(sns::rendering::weight_variant("Meshes/body_1.nif",0)=="Meshes/body_0.nif");
  auto v=sns::rendering::to_view_space({1,2,3}); CHECK(v.x==-1); CHECK(v.y==3); CHECK(v.z==2);
}

// ---- Renderer fidelity: synthetic fixtures only (no game data) ------------------------------------
namespace {
using Rgba = std::array<std::uint8_t, 4>;
// Uncompressed 32-bit RGBA DDS (masked layout) filled with one colour, or with `pixels`.
std::string dds_rgba(std::uint32_t w, std::uint32_t h, Rgba fill, const std::vector<Rgba>& pixels = {}) {
  std::string b(128, '\0'); b.replace(0, 4, "DDS ");
  put(b,4,124); put(b,8,0x100F); put(b,12,h); put(b,16,w); put(b,20,w*4); put(b,28,1);
  put(b,76,32); put(b,80,0x41); put(b,88,32); put(b,92,0xFF); put(b,96,0xFF00); put(b,100,0xFF0000); put(b,104,0xFF000000u); put(b,108,0x1000);
  for (std::uint32_t i = 0; i < w * h; ++i) {
    const auto& px = pixels.empty() ? fill : pixels[i];
    b.append(reinterpret_cast<const char*>(px.data()), 4);
  }
  return b;
}
// Block-compressed DDS header; `dxgi` != 0 adds a DX10 extension (BC7 needs it).
std::string dds_blocks(std::uint32_t w, std::uint32_t h, std::uint32_t mips, const char* fourcc, std::uint32_t dxgi = 0) {
  std::string b(128, '\0'); b.replace(0, 4, "DDS ");
  put(b,4,124); put(b,8,0x1007 | (mips > 1 ? 0x20000u : 0u)); put(b,12,h); put(b,16,w); put(b,28,mips);
  put(b,76,32); put(b,80,4); b.replace(84,4,fourcc); put(b,108,0x1000);
  if (dxgi) { std::string ext(20, '\0'); put(ext,0,dxgi); put(ext,4,3); put(ext,12,1); b += ext; }
  return b;
}
// BC1 block whose 16 texels all take colour0 (RGB565).
std::string bc1(std::uint16_t color0) { std::string k(8, '\0'); std::memcpy(k.data(), &color0, 2); return k; }
sns::textures::DdsImage image(std::uint32_t w, std::uint32_t h, Rgba fill) {
  sns::textures::DdsImage img; img.width = w; img.height = h; img.format = sns::textures::TextureFormat::RGBA8;
  for (std::uint32_t i = 0; i < w * h; ++i) img.data.append(reinterpret_cast<const char*>(fill.data()), 4);
  return img;
}
Rgba pixel(const sns::textures::DdsImage& img, std::size_t i) {
  Rgba out; std::memcpy(out.data(), img.data.data() + i * 4, 4); return out;
}

// SSE NIF built with nifly: quads with UVs and normals under a BSLightingShaderProperty.
struct NifBuilder {
  nifly::NifFile file;
  NifBuilder() { file.Create(nifly::NiVersion::getSSE()); }
  nifly::NiShape* quad(const std::string& name, float x0 = 0, float size = 10, bool flat_uv = false) {
    std::vector<nifly::Vector3> v{{x0,0,0},{x0+size,0,0},{x0+size,size,0},{x0,size,0}};
    std::vector<nifly::Triangle> t{{0,1,2},{0,2,3}};
    std::vector<nifly::Vector2> uv = flat_uv ? std::vector<nifly::Vector2>(4, nifly::Vector2(0.5f, 0.5f))
                                             : std::vector<nifly::Vector2>{{0,0},{1,0},{1,1},{0,1}};
    std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
    return file.CreateShapeFromData(name, &v, &t, &uv, &n);
  }
  void texture(nifly::NiShape* s, std::string path, std::uint32_t slot = 0) { file.SetTextureSlot(s, path, slot); }
  void alpha(nifly::NiShape* s, std::uint16_t flags, std::uint8_t threshold) {
    auto a = std::make_unique<nifly::NiAlphaProperty>(); a->flags = flags; a->threshold = threshold;
    file.AssignAlphaProperty(s, std::move(a));
  }
  std::string bytes() { std::ostringstream out(std::ios::binary); REQUIRE(file.Save(out) == 0); return out.str(); }
};
const sns::rendering::RenderPart& part_named(const sns::rendering::CharacterScene& scene, const std::string& name) {
  const auto it = std::find_if(scene.parts.begin(), scene.parts.end(), [&](const auto& p) { return p.shape_name == name; });
  REQUIRE(it != scene.parts.end());
  return *it;
}
}  // namespace

TEST_CASE("Alpha translation follows the NiAlphaProperty and the alpha that really reaches the material", "[scene][alpha]") {
  using sns::rendering::AlphaMode; using sns::rendering::translate_alpha;
  CHECK(translate_alpha(false, false, 128, true, false).mode == AlphaMode::Opaque);   // no property: opaque
  CHECK(translate_alpha(true, true, 128, false, false).mode == AlphaMode::Opaque);    // flags, but every texel opaque
  const auto mask = translate_alpha(false, true, 200, true, false);                   // test only: NIF threshold, not 0.5
  CHECK(mask.mode == AlphaMode::Mask); CHECK(mask.cutoff == Catch::Approx(200 / 255.0f));
  CHECK(translate_alpha(true, false, 0, true, false).mode == AlphaMode::Blend);       // blend only (eyes, brows)
  CHECK(translate_alpha(true, true, 40, true, false).mode == AlphaMode::BlendWithDepthPrepass);  // hair: test + blend
  CHECK(translate_alpha(true, false, 0, false, true).mode == AlphaMode::Blend);       // vertex alpha alone counts
  CHECK(sns::rendering::to_string(AlphaMode::BlendWithDepthPrepass) == "blendPrepass");
}

TEST_CASE("Median skin color is measured where the shape's UVs are, ignoring padding, make-up, tattoos and transparent texels", "[scene][skin]") {
  using sns::rendering::median_skin_color;
  using sns::rendering::RenderPart;
  // A shape whose UV triangles cover the left half of the atlas (u 0..0.5): two triangles, 18 floats per vertex, UV at 6 and 7.
  const auto make_part = [](float u0, float v0, float u1, float v1) {
    RenderPart part;
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    part.vertices.assign(4 * sns::rendering::kFloatsPerVertex, 0.0f);
    for (int i = 0; i < 4; ++i) {
      part.vertices[i * sns::rendering::kFloatsPerVertex + 6] = uv[i][0];
      part.vertices[i * sns::rendering::kFloatsPerVertex + 7] = uv[i][1];
    }
    part.indices = {0, 1, 2, 0, 2, 3};
    return part;
  };
  // The atlas: skin on the left half; the right half is padding in an unrelated dark red (as in real skin atlases, where the islands
  // of a hands shape are a small part of the canvas). Inside the covered half: a 12 % dark patch (tattoo), a 10 % transparent band
  // and one row of strong red (lips). None of them may move the median, and the padding must not count at all.
  auto atlas = image(128, 128, Rgba{210, 160, 130, 255});
  auto* data = reinterpret_cast<std::uint8_t*>(atlas.data.data());
  for (std::uint32_t y = 0; y < 128; ++y)
    for (std::uint32_t x = 0; x < 128; ++x) {
      auto* p = data + (static_cast<std::size_t>(y) * 128 + x) * 4;
      if (x >= 64) { p[0] = 110; p[1] = 56; p[2] = 57; p[3] = 255; continue; }
      if (y < 15) { p[0] = 20; p[1] = 20; p[2] = 25; }
      else if (y >= 115) { p[3] = 0; p[0] = 255; p[1] = 0; p[2] = 255; }
      else if (y == 60) { p[0] = 180; p[1] = 60; p[2] = 70; }
    }
  const auto left = median_skin_color(atlas, make_part(0.0f, 0.0f, 0.5f, 1.0f));
  REQUIRE(left);
  CHECK((*left)[0] == Catch::Approx(210.0f / 255.0f).margin(0.01));
  CHECK((*left)[1] == Catch::Approx(160.0f / 255.0f).margin(0.01));
  CHECK((*left)[2] == Catch::Approx(130.0f / 255.0f).margin(0.01));
  // The same atlas read through a shape that lives on the right half measures the padding color: it is the UVs that choose.
  const auto right = median_skin_color(atlas, make_part(0.5f, 0.0f, 1.0f, 1.0f));
  REQUIRE(right);
  CHECK((*right)[0] == Catch::Approx(110.0f / 255.0f).margin(0.01));
  CHECK((*right)[1] == Catch::Approx(56.0f / 255.0f).margin(0.01));
  // A shape using the whole canvas sees half skin, half padding: it still answers (one of the two, never an invented color).
  CHECK(median_skin_color(atlas, make_part(0.0f, 0.0f, 1.0f, 1.0f)));

  // Without anything to measure there is no answer rather than a wrong color.
  RenderPart no_triangles;
  CHECK_FALSE(median_skin_color(atlas, no_triangles));
  CHECK_FALSE(median_skin_color(atlas, make_part(0.0f, 0.0f, 0.0f, 0.0f)));    // degenerate (zero-area) triangles
  CHECK_FALSE(median_skin_color(atlas, make_part(1.5f, 1.5f, 2.5f, 2.5f)));    // UVs outside the atlas
  CHECK_FALSE(median_skin_color(image(32, 32, Rgba{200, 150, 120, 0}), make_part(0.0f, 0.0f, 1.0f, 1.0f)));  // nothing opaque
  CHECK_FALSE(median_skin_color(sns::textures::DdsImage{}, make_part(0.0f, 0.0f, 1.0f, 1.0f)));
  auto compressed = image(32, 32, Rgba{200, 150, 120, 255});
  compressed.format = sns::textures::TextureFormat::BC1;
  CHECK_FALSE(median_skin_color(compressed, make_part(0.0f, 0.0f, 1.0f, 1.0f)));
  // A very large texture is sampled on a grid, not walked texel by texel: same answer.
  const auto big = median_skin_color(image(4096, 2048, Rgba{100, 120, 140, 255}), make_part(0.0f, 0.0f, 1.0f, 1.0f));
  REQUIRE(big);
  CHECK((*big)[2] == Catch::Approx(140.0f / 255.0f).margin(0.005));
}

TEST_CASE("DDS uses the stored mip that fits and decodes real alpha of BC1, BC3 and BC7", "[textures]") {
  using namespace sns::textures;
  // 8x8 BC1 with three stored levels: red (8x8), green (4x4), blue (2x2).
  auto chain = dds_blocks(8, 8, 3, "DXT1") + bc1(0xF800) + bc1(0xF800) + bc1(0xF800) + bc1(0xF800) + bc1(0x07E0) + bc1(0x001F);
  const auto full = parse_dds(chain);
  CHECK(full.width == 8); CHECK(full.mip_level == 0);
  CHECK(pixel(decode_rgba(full), 0) == Rgba{255, 0, 0, 255});
  const auto fit = parse_dds(chain, 4);
  CHECK(fit.width == 4); CHECK(fit.mip_level == 1); CHECK(fit.source_width == 8);
  CHECK(pixel(decode_rgba(fit), 0) == Rgba{0, 255, 0, 255});
  const auto smallest = parse_dds(chain, 1);  // never below the last stored level
  CHECK(smallest.width == 2); CHECK(smallest.mip_level == 2);
  CHECK(pixel(decode_rgba(smallest), 0) == Rgba{0, 0, 255, 255});
  CHECK_THROWS_AS(parse_dds(chain.substr(0, chain.size() - 4), 1), sns::Error);  // truncated level

  // BC3: alpha endpoints 255/0, every index 1 -> alpha 0 everywhere.
  std::string bc3_block("\xFF\x00\x49\x92\x24\x49\x92\x24", 8);
  bc3_block += bc1(0xFFFF);
  const auto bc3 = decode_rgba(parse_dds(dds_blocks(4, 4, 1, "DXT5") + bc3_block));
  const auto bc3_alpha = measure_alpha(bc3);
  CHECK(bc3_alpha.uses_alpha); CHECK(bc3_alpha.min == 0); CHECK(bc3_alpha.translucent_fraction == Catch::Approx(1));

  // BC7 mode 6: all zero endpoints and p-bits -> alpha 0; A0=A1=127 with p-bits 1 -> alpha 255.
  std::string bc7_clear(16, '\0'); bc7_clear[0] = 0x40;
  std::string bc7_opaque = bc7_clear; bc7_opaque[6] = static_cast<char>(0xFE); bc7_opaque[7] = static_cast<char>(0xFF); bc7_opaque[8] = 0x01;
  const auto clear = decode_rgba(parse_dds(dds_blocks(4, 4, 1, "DX10", 98) + bc7_clear));
  const auto opaque = decode_rgba(parse_dds(dds_blocks(4, 4, 1, "DX10", 98) + bc7_opaque));
  CHECK(measure_alpha(clear).uses_alpha); CHECK(measure_alpha(clear).min == 0);
  CHECK_FALSE(measure_alpha(opaque).uses_alpha); CHECK(measure_alpha(opaque).min == 255);
}

TEST_CASE("Preview textures: box downscale and alpha statistics", "[textures]") {
  using namespace sns::textures;
  auto img = image(4, 4, {0, 0, 0, 255});
  for (std::uint32_t i = 0; i < 16; ++i) img.data[i * 4] = static_cast<char>((i % 2) ? 200 : 0);  // alternating red columns
  const auto half = downscale_rgba(img, 2);
  CHECK(half.width == 2); CHECK(half.height == 2); CHECK(pixel(half, 0)[0] == 100);
  CHECK(downscale_rgba(img, 0).width == 4);

  auto a = image(2, 2, {9, 9, 9, 255}); a.data[11] = static_cast<char>(100); a.data[15] = 0;
  const auto stats = measure_alpha(a, 128);
  CHECK(stats.uses_alpha); CHECK(stats.min == 0);
  CHECK(stats.translucent_fraction == Catch::Approx(0.5)); CHECK(stats.below_cutoff_fraction == Catch::Approx(0.5));
  CHECK_FALSE(measure_alpha(image(2, 2, {1, 2, 3, 255})).uses_alpha);
}

TEST_CASE("FaceGen tint composition is neutral at 0.5 and keeps the base size", "[scene][facegen]") {
  using sns::rendering::apply_face_tint;
  const auto base = image(4, 4, {200, 100, 50, 255});
  const auto neutral = apply_face_tint(base, image(2, 2, {128, 128, 128, 255}), nullptr);
  CHECK(neutral.width == 4);
  for (int c = 0; c < 3; ++c) CHECK(std::abs(int(pixel(neutral, 5)[c]) - int(pixel(base, 5)[c])) <= 2);
  const auto detail = image(2, 2, {64, 64, 64, 255});  // neutral detail: 64/255 * 3.984375 ~ 1
  const auto with_detail = apply_face_tint(base, image(2, 2, {128, 128, 128, 255}), &detail);
  for (int c = 0; c < 3; ++c) CHECK(std::abs(int(pixel(with_detail, 5)[c]) - int(pixel(base, 5)[c])) <= 4);
  CHECK(pixel(apply_face_tint(base, image(1, 1, {255, 255, 255, 255}), nullptr), 0)[1] > 100);  // lighter
  CHECK(pixel(apply_face_tint(base, image(1, 1, {0, 0, 0, 255}), nullptr), 0)[1] < 100);         // darker (b^2)
  CHECK_THROWS_AS(apply_face_tint(base, sns::textures::DdsImage{}, nullptr), sns::Error);
}

TEST_CASE("SkinTint applies the skin tone as a soft-light blend, not a multiply", "[scene][facegen]") {
  using sns::rendering::apply_skin_tint;
  const auto base = image(4, 4, {153, 153, 153, 255});  // 0.6
  // Neutral tone (0.5) keeps the texture, times the shader's (1.0117, 0.9961, 1.0117) constant.
  const auto neutral = apply_skin_tint(base, {0.5f, 0.5f, 0.5f});
  CHECK(neutral.width == 4);
  CHECK(std::abs(int(pixel(neutral, 5)[1]) - 152) <= 1);
  // A typical QNAM (0.8): b^2 + 2*t*b*(1-b) = 0.36 + 0.384 = 0.744 -> 190 (green scale 0.99609375 -> 189),
  // brighter than the base; the old material multiply gave 0.48 (122): the body ended darker than the face.
  const auto qnam = apply_skin_tint(base, {0.8f, 0.8f, 0.8f});
  CHECK(std::abs(int(pixel(qnam, 0)[1]) - 189) <= 1);
  CHECK(pixel(qnam, 0)[1] > pixel(base, 0)[1]);
  CHECK(pixel(apply_skin_tint(base, {0.0f, 0.0f, 0.0f}), 0)[1] < 100);  // black tone: b^2
  CHECK(pixel(qnam, 0)[3] == 255);                                       // alpha untouched
  CHECK_THROWS_AS(apply_skin_tint(sns::textures::DdsImage{}, {0.5f, 0.5f, 0.5f}), sns::Error);
}

namespace {
// _msn texel for model normal n, encoded the way Skyrim decodes it (n = rgb.xzy * 2 - 1).
std::array<std::uint8_t, 4> msn_texel(float x, float y, float z, bool skyrim_swizzle = true) {
  auto enc = [](float c) { return static_cast<std::uint8_t>(std::lround(c * 127.5f + 127.5f)); };
  return skyrim_swizzle ? std::array<std::uint8_t, 4>{enc(x), enc(z), enc(y), 255} : std::array<std::uint8_t, 4>{enc(x), enc(y), enc(z), 255};
}
}  // namespace

TEST_CASE("Model-space normal maps bake to the tangent space of the mesh", "[scene][msn]") {
  using sns::rendering::bake_model_space_normals;
  using sns::rendering::model_space_normal_agreement;
  // One triangle covering the upper-left half of an 8x8 map (uv (0,0), (1,0), (0,1)).
  const std::vector<float> uvs = {0, 0, 1, 0, 0, 1};
  const std::vector<std::uint32_t> tri = {0, 1, 2};
  const float nx = 0.6f, ny = 0.0f, nz = 0.8f;
  const auto t = msn_texel(nx, ny, nz);
  const auto msn = image(8, 8, {t[0], t[1], t[2], 255});

  SECTION("identity frame: the tangent-space normal is the decoded model normal (xzy swizzle)") {
    const std::vector<float> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<float> frames;
    for (int v = 0; v < 3; ++v) frames.insert(frames.end(), identity.begin(), identity.end());
    const auto baked = bake_model_space_normals(msn, uvs, frames, tri);
    const auto px = pixel(baked, 1);  // texel (1, 0): inside the triangle
    CHECK(std::abs(int(px[0]) - 204) <= 1);  // x = 0.6
    CHECK(std::abs(int(px[1]) - 128) <= 1);  // y = 0
    CHECK(std::abs(int(px[2]) - 230) <= 1);  // z = 0.8
    CHECK(model_space_normal_agreement(msn, uvs, frames) == Catch::Approx(0.8f).margin(0.01));
  }

  SECTION("rotated frame: frame * baked value gives the model normal back") {
    // T = +Y, B = -X, N = +Z (a 90 degree turn around the normal).
    const std::vector<float> rotated = {0, 1, 0, -1, 0, 0, 0, 0, 1};
    std::vector<float> frames;
    for (int v = 0; v < 3; ++v) frames.insert(frames.end(), rotated.begin(), rotated.end());
    const auto baked = bake_model_space_normals(msn, uvs, frames, tri);
    const auto px = pixel(baked, 1);
    const float c0 = px[0] / 127.5f - 1, c1 = px[1] / 127.5f - 1, c2 = px[2] / 127.5f - 1;
    // n = T*c0 + B*c1 + N*c2 = (-c1, c0, c2)
    CHECK(-c1 == Catch::Approx(nx).margin(0.02));
    CHECK(c0 == Catch::Approx(ny).margin(0.02));
    CHECK(c2 == Catch::Approx(nz).margin(0.02));
  }

  SECTION("agreement exposes a map in another convention; uncovered texels are dilated, then flat") {
    std::vector<float> frames;
    for (int v = 0; v < 3; ++v) frames.insert(frames.end(), {1, 0, 0, 0, 1, 0, 0.6f, 0.0f, 0.8f});
    CHECK(model_space_normal_agreement(msn, uvs, frames) > 0.99f);
    // The same normal stored without Skyrim's swizzle decodes as (0.6, 0.8, 0): agreement drops.
    const auto w = msn_texel(nx, ny, nz, false);
    CHECK(model_space_normal_agreement(image(8, 8, {w[0], w[1], w[2], 255}), uvs, frames) < 0.5f);

    const auto baked = bake_model_space_normals(msn, uvs, frames, tri, 1);
    const auto corner = pixel(baked, 63);  // (7, 7): far from the triangle, beyond one dilation pass
    CHECK(int(corner[0]) == 128); CHECK(int(corner[1]) == 128); CHECK(int(corner[2]) == 255);
  }

  CHECK_THROWS_AS(bake_model_space_normals(sns::textures::DdsImage{}, uvs, {}, tri), sns::Error);
  CHECK_THROWS_AS(model_space_normal_agreement(msn, uvs, {1, 0, 0}), sns::Error);
}

TEST_CASE("Outfit items sharing a slot: the last one stays equipped", "[scene]") {
  CHECK(sns::rendering::resolve_equipped({1u << 16, (1u << 16) | (1u << 2), 1u << 4}) == std::vector<bool>{false, true, true});
  CHECK(sns::rendering::resolve_equipped({0, 0}) == std::vector<bool>{true, true});
}

TEST_CASE("Tangent basis follows the UV gradient; degenerate UVs carry no orientation", "[nif]") {
  sns::nif::Shape s; s.positions = {{0,0,0},{1,0,0},{0,1,0}}; s.uvs = {{0,0},{1,0},{0,1}}; s.indices = {0,1,2};
  s.tangents.assign(3, {0, 1, 0}); s.bitangents.assign(3, {1, 0, 0});  // stored the other way round
  CHECK(sns::nif::orient_tangent_basis(s)); CHECK(s.tangents[0].x == Catch::Approx(1));
  sns::nif::Shape ok = s; ok.tangents_swapped = false;
  CHECK_FALSE(sns::nif::orient_tangent_basis(ok));  // already +U
  sns::nif::Shape flat = s; flat.tangents_swapped = false; flat.uvs.assign(3, {0.5f, 0.5f});
  flat.tangents.assign(3, {0, 1, 0}); flat.bitangents.assign(3, {1, 0, 0});
  CHECK_FALSE(sns::nif::orient_tangent_basis(flat));
}

TEST_CASE("Bones missing from the race skeleton use the NIF's own node (SMP bones)", "[scene]") {
  sns::nif::Shape shape; shape.skinned = true; shape.positions = {{0,0,1}}; shape.normals = {{0,0,1}};
  shape.bones = {"A-S-000001"}; shape.skin_to_bone.resize(1); shape.influences.resize(1); shape.influences[0].weight[0] = 1;
  sns::nif::Transform node; node.translation = {0, 0, 5};
  shape.mesh_bone_to_global = {node};
  const auto report = sns::rendering::apply_skinning(shape, sns::nif::Skeleton{});
  CHECK(report.missing.empty()); REQUIRE(report.from_mesh.size() == 1); CHECK(report.from_mesh[0] == "A-S-000001");
  CHECK(shape.positions[0].z == Catch::Approx(6));
}

TEST_CASE("SMP XML collision shapes and why the game would not draw a shape", "[scene][smp]") {
  const auto names = sns::rendering::smp_collision_shapes(
      "<system><!-- <per-triangle-shape name=\"Off\"/> -->\n<per-triangle-shape name=\"CPhy\"><margin>1</margin></per-triangle-shape>"
      "<per-vertex-shape   name = 'Skirt &amp; Cape' /><per-triangle-shape name=\"CPhy\"/><bone name=\"NPC Spine\"/></system>");
  CHECK(names == std::vector<std::string>{"CPhy", "Skirt & Cape"});
  sns::nif::Shape s; s.name = "CPhy"; s.has_shader = true;
  CHECK(sns::rendering::hidden_reason(s, names).find("physics proxy") != std::string::npos);
  s.textures[0] = "textures\\a.dds";
  CHECK(sns::rendering::hidden_reason(s, names).empty());  // textured shapes stay visible even if listed
  s.has_shader = false; CHECK(sns::rendering::hidden_reason(s, {}).find("no shader") != std::string::npos);
  s.has_shader = true; s.hidden = true; CHECK(sns::rendering::hidden_reason(s, {}).find("Hidden") != std::string::npos);
}

TEST_CASE("SMP XML parser rejects malformed, DTD and oversized input", "[scene][smp]") {
  const auto valid = sns::rendering::parse_smp_xml(
      "<system><bone name='A-S-000001'/><per-triangle-shape name='CPhy'/></system>");
  REQUIRE(valid.well_formed);
  CHECK(valid.bones == std::vector<std::string>{"A-S-000001"});
  CHECK(valid.collision_shapes == std::vector<std::string>{"CPhy"});

  CHECK_FALSE(sns::rendering::parse_smp_xml("<system><bone></system>").well_formed);
  CHECK_FALSE(sns::rendering::parse_smp_xml(
      "<!DOCTYPE system [<!ENTITY x SYSTEM 'file:///C:/secret'>]><system>&x;</system>").well_formed);
  CHECK_FALSE(sns::rendering::parse_smp_xml(std::string(4u * 1024u * 1024u + 1, 'x')).well_formed);
}

TEST_CASE("A zero-filled BSA is diagnosed as damaged with an external repair hint", "[assets]") {
  sns::test::TempDir dir;
  const auto broken = dir.write("Broken.bsa", std::string(256 * 1024, '\0'));
  const auto d = sns::assets::diagnose_archive(broken);
  CHECK(d.corrupt);
  CHECK(d.message.find("42534100") != std::string::npos);
  CHECK(d.message.find("100% of sampled bytes are zero") != std::string::npos);
  CHECK(d.message.find("Verify integrity") != std::string::npos);
  CHECK_FALSE(sns::assets::diagnose_archive(dir.write("Good.bsa", bsa("x", false))).corrupt);

  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Game", dir.path(), sns::mo2::ProviderKind::GameData}});
  sns::assets::AssetResolver resolver(vfs, {{"Broken.bsa", "<Game Data>", broken, "ini"}});
  REQUIRE(resolver.issues().size() == 1);
  CHECK(resolver.issues()[0].code == "ArchiveCorrupt");
}

TEST_CASE("Synthetic character scene: MO2 resolution, transforms, alpha, helpers, FaceGen and UV checks", "[scene][e2e]") {
  sns::test::TempDir dir;
  // -- armor NIF ----------------------------------------------------------------------------------
  NifBuilder armor;
  auto* cloth = armor.quad("Cloth"); armor.texture(cloth, "textures\\test\\cloth_d.dds"); armor.alpha(cloth, 4845, 40);
  auto* plate = armor.quad("Plate"); armor.texture(plate, "textures\\test\\plate_d.dds"); armor.alpha(plate, 4844, 128);
  auto* skirt = armor.quad("Skirt"); armor.texture(skirt, "textures\\test\\plate_d.dds");
  auto* ground = armor.quad("VirtualGround", -80, 160); ground->ShaderPropertyRef()->index = nifly::NIF_NPOS;
  armor.quad("CPhy");
  auto* ghost = armor.quad("Ghost"); armor.texture(ghost, "textures\\test\\plate_d.dds"); ghost->flags |= 1;
  auto* flat = armor.quad("Flat", 0, 10, true); armor.texture(flat, "textures\\test\\plate_d.dds");
  auto* tinted = armor.quad("Tinted"); armor.texture(tinted, "textures\\test\\plate_d.dds"); armor.alpha(tinted, 4333, 0);
  armor.file.GetShader(tinted)->SetVertexColors(true);
  armor.file.SetColorsForShape(tinted, std::vector<nifly::Color4>(4, nifly::Color4(1, 0.5f, 0.25f, 0.5f)));
  nifly::MatTransform outer_xf; outer_xf.translation = {100, 0, 0};
  nifly::MatTransform pivot_xf; pivot_xf.translation = {0, 0, 5}; pivot_xf.scale = 2;
  auto* outer = armor.file.AddNode("Outer", outer_xf);
  auto* pivot = armor.file.AddNode("Pivot", pivot_xf, outer);
  auto* nested = armor.quad("Nested"); armor.texture(nested, "textures\\test\\plate_d.dds"); armor.file.SetParentNode(nested, pivot);
  auto smp = std::make_unique<nifly::NiStringExtraData>();
  smp->name.get() = "HDT Skinned Mesh Physics Object"; smp->stringData.get() = "Data\\meshes\\test\\armor.xml";
  armor.file.AssignExtraData(armor.file.GetRootNode(), std::move(smp));
  dir.write("Base Mod/meshes/test/armor.nif", armor.bytes());
  dir.write("Base Mod/meshes/test/armor.xml",
            "<system><per-triangle-shape name=\"CPhy\"/><per-vertex-shape name=\"Skirt\"/><per-triangle-shape name=\"VirtualGround\"/></system>");
  // -- FaceGen head with tint, detail and hair --------------------------------------------------
  NifBuilder face;
  auto* head = face.quad("Head"); face.texture(head, "textures\\test\\head_d.dds"); face.texture(head, "textures\\test\\head_detail.dds", 3);
  face.file.GetShader(head)->SetShaderType(sns::nif::kShaderFaceTint);
  auto* hair = face.quad("Hair"); face.texture(hair, "textures\\test\\hair_d.dds"); face.alpha(hair, 4844, 200);
  face.file.GetShader(hair)->SetShaderType(sns::nif::kShaderHairTint);
  // Model-space normal map on a shape whose vertex normals are unreliable (+X on a quad facing +Z), as on
  // real bodies: the scene must light it with geometric normals and bake the _msn to tangent space.
  auto* msn_skin = face.quad("MsnSkin", 20);
  face.texture(msn_skin, "textures\\test\\head_d.dds"); face.texture(msn_skin, "textures\\test\\skin_msn.dds", 1);
  auto* msn_shader = dynamic_cast<nifly::BSLightingShaderProperty*>(face.file.GetShader(msn_skin));
  REQUIRE(msn_shader != nullptr);
  msn_shader->shaderFlags1 |= nifly::SLSF1_MODEL_SPACE_NORMALS;
  face.file.SetNormalsForShape(msn_skin, std::vector<nifly::Vector3>(4, nifly::Vector3(1, 0, 0)));
  dir.write("Base Mod/meshes/test/head.nif", face.bytes());
  // -- textures: the override mod wins for plate_d.dds -------------------------------------------
  std::vector<Rgba> half_clear(16, Rgba{90, 90, 90, 255}); for (int i = 0; i < 8; ++i) half_clear[i][3] = 0;
  dir.write("Base Mod/textures/test/cloth_d.dds", dds_rgba(4, 4, {}, half_clear));
  dir.write("Base Mod/textures/test/plate_d.dds", dds_rgba(4, 4, {255, 0, 0, 255}));
  dir.write("Override Mod/textures/test/plate_d.dds", dds_rgba(4, 4, {0, 0, 255, 255}));
  dir.write("Base Mod/textures/test/head_d.dds", dds_rgba(4, 4, {180, 140, 120, 255}));
  dir.write("Base Mod/textures/test/head_detail.dds", dds_rgba(4, 4, {64, 64, 64, 255}));
  dir.write("Base Mod/textures/test/tint.dds", dds_rgba(4, 4, {128, 128, 128, 255}));
  dir.write("Base Mod/textures/test/hair_d.dds", dds_rgba(4, 4, {120, 120, 120, 100}));
  dir.write("Base Mod/textures/test/skin_msn.dds", dds_rgba(4, 4, {128, 255, 128, 255}));  // +Z, Skyrim's xzy encoding

  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Base Mod", dir.path() / "Base Mod"}, {"Override Mod", dir.path() / "Override Mod"}});
  sns::assets::AssetResolver resolver(vfs, {});
  sns::rendering::Appearance a;
  a.npc_form_key = "000001:Test.esp";
  a.face_geometry = "meshes\\test\\head.nif";
  a.face_tint = "textures\\test\\tint.dds";
  a.hair_color = sns::rendering::Rgb{0.5f, 0.25f, 0.125f};
  a.pieces.push_back({"000800:Test.esp", "Synthetic Armor", "outfit", 1u << 2, {{"000801:Test.esp", "meshes\\test\\armor.nif", false, 1u << 2}}});
  const auto scene = sns::rendering::build_character_scene(a, resolver, {});

  using sns::rendering::AlphaMode;
  // MO2: the later provider's texture wins; the mesh comes from the only provider that has it.
  CHECK(part_named(scene, "Plate").mesh_location == "loose:Base Mod");
  CHECK(scene.texture_info.at("textures\\test\\plate_d.dds").location == "loose:Override Mod");
  CHECK(pixel(*scene.textures.at("textures\\test\\plate_d.dds"), 0) == Rgba{0, 0, 255, 255});
  CHECK(part_named(scene, "Plate").addon_form_key == "000801:Test.esp");
  CHECK(part_named(scene, "Plate").physics_xml == "meshes\\test\\armor.xml");
  CHECK(part_named(scene, "Plate").physics_xml_location == "loose:Base Mod");
  CHECK(part_named(scene, "Plate").collision_shapes == std::vector<std::string>{"CPhy", "Skirt", "VirtualGround"});
  // Alpha: decided by the NIF flags and by the alpha measured on the decoded texture.
  CHECK(part_named(scene, "Cloth").alpha_mode == AlphaMode::BlendWithDepthPrepass);
  CHECK(part_named(scene, "Plate").alpha_mode == AlphaMode::Opaque);  // test flag, opaque texels
  CHECK(part_named(scene, "Skirt").alpha_mode == AlphaMode::Opaque);
  CHECK(part_named(scene, "Tinted").alpha_mode == AlphaMode::Blend);  // opaque texture, vertex alpha 0.5
  const auto& tinted_part = part_named(scene, "Tinted");
  CHECK(tinted_part.vertex_colors);
  CHECK(tinted_part.vertices[sns::rendering::kColorOffset + 1] == Catch::Approx(0.5).margin(0.01));
  CHECK(tinted_part.vertices[sns::rendering::kColorOffset + 3] == Catch::Approx(0.5).margin(0.01));
  CHECK_FALSE(part_named(scene, "Plate").vertex_colors);
  CHECK(part_named(scene, "Plate").vertices[sns::rendering::kColorOffset + 3] == 1.0f);
  // Shapes the game does not draw: listed, hidden by default, out of the bounds.
  CHECK_FALSE(part_named(scene, "VirtualGround").shown_by_default);
  CHECK(part_named(scene, "VirtualGround").hidden_reason.find("no shader") != std::string::npos);
  CHECK(part_named(scene, "CPhy").hidden_reason.find("physics proxy") != std::string::npos);
  CHECK(part_named(scene, "Ghost").hidden_reason.find("Hidden flag") != std::string::npos);
  CHECK(part_named(scene, "Skirt").shown_by_default);
  CHECK(scene.stats.hidden_shapes == 3);
  CHECK(scene.max[0] <= 0.001f);   // the 160-unit ground plane (view x up to +80) is not framed
  CHECK(scene.min[0] == Catch::Approx(-120));
  // Hierarchy: Outer(100,0,0) * Pivot(0,0,5, scale 2): Skyrim (10,0,0) -> (120,0,5) -> view (-120,5,0).
  const auto& nested_part = part_named(scene, "Nested");
  CHECK(nested_part.parent_node == "Pivot");
  CHECK(nested_part.min[0] == Catch::Approx(-120)); CHECK(nested_part.max[0] == Catch::Approx(-100));
  CHECK(nested_part.min[1] == Catch::Approx(5)); CHECK(nested_part.to_root.scale == Catch::Approx(2));
  // UVs: degenerate layout is flagged, a valid one is not.
  CHECK(part_named(scene, "Flat").degenerate_uvs);
  CHECK(std::any_of(part_named(scene, "Flat").warnings.begin(), part_named(scene, "Flat").warnings.end(),
                    [](const auto& w) { return w.find("degenerate") != std::string::npos; }));
  CHECK_FALSE(part_named(scene, "Plate").degenerate_uvs);
  // FaceGen: tint composited over the head diffuse with the detail map, nothing replaced.
  const auto& head_part = part_named(scene, "Head");
  CHECK(head_part.original_diffuse == "textures\\test\\head_d.dds");
  CHECK(head_part.diffuse == "textures\\test\\head_d.dds|facetint:textures\\test\\tint.dds");
  CHECK(scene.texture_info.at(head_part.diffuse).derived_from.find("head_detail.dds") != std::string::npos);
  const auto composite = pixel(*scene.textures.at(head_part.diffuse), 0);
  CHECK(std::abs(int(composite[0]) - 180) <= 4); CHECK(std::abs(int(composite[2]) - 120) <= 4);
  // Hair: its own threshold (no fixed 0.5 cutoff, no blanket Mask) and the record's hair colour.
  // _msn: geometric normal (same as the correctly lit Head quad), map baked to a flat tangent-space normal.
  const auto& msn_part = part_named(scene, "MsnSkin");
  const auto& head_quad = part_named(scene, "Head");
  CHECK(msn_part.msn_agreement > 0.99f);
  REQUIRE(msn_part.normal.find("|tangent:") != std::string::npos);
  const auto ts = pixel(*scene.textures.at(msn_part.normal), 5);
  CHECK(std::abs(int(ts[0]) - 128) <= 2); CHECK(std::abs(int(ts[1]) - 128) <= 2); CHECK(int(ts[2]) >= 250);
  for (int c = 3; c < 6; ++c) CHECK(msn_part.vertices[c] == Catch::Approx(head_quad.vertices[c]).margin(0.01));  // Head normals are byte-packed (1/255)
  CHECK(std::any_of(scene.issues.begin(), scene.issues.end(), [](const auto& i) { return i.code == "ModelSpaceNormalsBaked"; }));

  const auto& hair_part = part_named(scene, "Hair");
  CHECK(hair_part.alpha_mode == AlphaMode::Mask); CHECK(hair_part.alpha_cutoff == Catch::Approx(200 / 255.0f));
  REQUIRE(hair_part.tint.has_value()); CHECK(hair_part.tint->g == Catch::Approx(0.25f));
  CHECK(std::none_of(scene.issues.begin(), scene.issues.end(), [](const auto& i) { return i.severity == "error"; }));

  // Portraits: only the FaceGen head is read and built, with the same FaceGen tint composite; no outfit.
  sns::rendering::SceneOptions head_only;
  head_only.head_only = true;
  const auto portrait = sns::rendering::build_character_scene(a, resolver, head_only);
  CHECK_FALSE(portrait.parts.empty());
  CHECK(std::all_of(portrait.parts.begin(), portrait.parts.end(), [](const auto& p) { return p.group == "head"; }));
  CHECK(std::none_of(portrait.parts.begin(), portrait.parts.end(), [](const auto& p) { return p.shape_name == "Plate"; }));
  CHECK(part_named(portrait, "Head").diffuse == head_part.diffuse);
  CHECK(part_named(portrait, "Hair").tint.has_value());
  CHECK(portrait.texture_info.count("textures\\test\\plate_d.dds") == 0);

  // Missing XML is a dependency warning; the NIF still loads in its reference pose.
  std::filesystem::remove(dir.path() / "Base Mod/meshes/test/armor.xml");
  const auto missing_vfs = sns::mo2::VirtualFileSystem::build_from({{"Base Mod", dir.path() / "Base Mod"}, {"Override Mod", dir.path() / "Override Mod"}});
  sns::assets::AssetResolver missing_resolver(missing_vfs, {});
  const auto missing_scene = sns::rendering::build_character_scene(a, missing_resolver, {});
  CHECK(part_named(missing_scene, "Plate").physics_xml_location == "missing");
  CHECK(std::any_of(missing_scene.issues.begin(), missing_scene.issues.end(), [](const auto& i) {
    return i.code == "PhysicsXmlMissing" && i.severity == "warning";
  }));
}

TEST_CASE("Skeleton posing engine supports Reference, A-Pose, Relaxed, and Combat presets", "[scene][pose]") {
  sns::nif::Skeleton skel;

  auto add_bone = [&](std::string name, int parent, sns::nif::Vec3 pos) {
    sns::nif::Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.to_global.translation = pos;
    b.to_global.rotation = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    b.to_global.scale = 1.0f;
    skel.bones.push_back(b);
    return static_cast<int>(skel.bones.size() - 1);
  };

  const int root = add_bone("NPC Root [Root]", -1, {0, 0, 0});
  const int spine = add_bone("NPC Spine1 [Spn1]", root, {0, 0, 100});
  const int l_arm = add_bone("NPC L UpperArm [LUar]", spine, {20, 0, 120});
  const int l_forearm = add_bone("NPC L Forearm [LLar]", l_arm, {40, 0, 120});
  const int r_arm = add_bone("NPC R UpperArm [RUar]", spine, {-20, 0, 120});
  const int r_forearm = add_bone("NPC R Forearm [RLar]", r_arm, {-40, 0, 120});
  const int l_thigh = add_bone("NPC L Thigh [LThg]", root, {10, 0, 80});
  const int r_thigh = add_bone("NPC R Thigh [RThg]", root, {-10, 0, 80});

  SECTION("Reference / TPose preserves original positions and rotations") {
    const auto ref = sns::rendering::pose_skeleton(skel, "reference");
    REQUIRE(ref.bones.size() == skel.bones.size());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
      CHECK(ref.bones[i].to_global.translation.x == Catch::Approx(skel.bones[i].to_global.translation.x));
      CHECK(ref.bones[i].to_global.translation.y == Catch::Approx(skel.bones[i].to_global.translation.y));
      CHECK(ref.bones[i].to_global.translation.z == Catch::Approx(skel.bones[i].to_global.translation.z));
      CHECK(ref.bones[i].to_global.rotation[0][0] == Catch::Approx(1.0f));
    }

    const auto tpose = sns::rendering::pose_skeleton(skel, "tpose");
    CHECK(tpose.bones[l_forearm].to_global.translation.x == Catch::Approx(40.0f));
  }

  SECTION("A-Pose drops upper arms and moves forearm descendants accordingly") {
    const auto apose = sns::rendering::pose_skeleton(skel, "apose");
    // Root and spine unaffected
    CHECK(apose.bones[root].to_global.translation.z == Catch::Approx(0.0f));
    CHECK(apose.bones[spine].to_global.translation.z == Catch::Approx(100.0f));

    // Upper arm pivot translation remains the same
    CHECK(apose.bones[l_arm].to_global.translation.x == Catch::Approx(20.0f));
    CHECK(apose.bones[l_arm].to_global.translation.z == Catch::Approx(120.0f));

    // Upper arm rotation changed from identity
    CHECK(apose.bones[l_arm].to_global.rotation[0][0] != Catch::Approx(1.0f));

    // Forearm translation moved: dx = 20, rot_y(-0.70f): x = 20 + cos(-0.7)*20, z = 120 - sin(-0.7)*20
    CHECK(apose.bones[l_forearm].to_global.translation.x < 40.0f);
    CHECK(apose.bones[l_forearm].to_global.translation.z < 120.0f);

    // Right arm symmetrically rotated
    CHECK(apose.bones[r_forearm].to_global.translation.x > -40.0f);
    CHECK(apose.bones[r_forearm].to_global.translation.z < 120.0f);
  }

  SECTION("Relaxed and Combat poses modify upper body and leg descendants") {
    const auto relaxed = sns::rendering::pose_skeleton(skel, "relaxed");
    CHECK(relaxed.bones[l_forearm].to_global.translation.z < 120.0f);

    const auto combat = sns::rendering::pose_skeleton(skel, "combat");
    // Spine rotates around X, moving everything above it
    CHECK(combat.bones[spine].to_global.rotation[1][1] != Catch::Approx(1.0f));
    // Thighs rotated
    CHECK(combat.bones[l_thigh].to_global.rotation[1][1] != Catch::Approx(1.0f));
    CHECK(combat.bones[r_thigh].to_global.rotation[1][1] != Catch::Approx(1.0f));
  }
}

TEST_CASE("AssetResolver locates assets packed inside BSA archives", "[assets]") {
  sns::test::TempDir dir;
  const auto bsa_path = dir.write("Skyrim - Meshes0.bsa", bsa("nif header bytes", false));
  std::vector<sns::assets::ArchiveRef> archives = {
    {"Skyrim - Meshes0.bsa", "<Game Data>", bsa_path, "ini"}
  };
  sns::mo2::VirtualFileSystem empty_vfs;
  sns::assets::AssetResolver resolver(empty_vfs, archives);
  const auto loc = resolver.locate("meshes/test.nif");
  REQUIRE(loc.has_value());
  CHECK(loc->kind == sns::assets::AssetLocation::Kind::Archive);
  CHECK(loc->provider == "Skyrim - Meshes0.bsa");
  CHECK(loc->archive_provider == "<Game Data>");
  CHECK_FALSE(resolver.locate("meshes/nonexistent.nif").has_value());
}

