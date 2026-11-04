// Geometric tests of the real BodySlide pipeline (sns/body/bodyslide_morphs.hpp) and of its use by the 3D scene.
// Every expected position below is computed by hand from BodySlide's own arithmetic (BodySlideApp::ApplySliders,
// DiffDataSets::ApplyDiff/ApplyClamp, TriFile int16 * multiplier), never read back from the code under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <NifFile.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "temp_dir.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/body/bodyslide_morphs.hpp"
#include "sns/core/error.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/rendering/scene.hpp"

namespace {

using sns::body::SliderSetting;

template <class T>
void put(std::string& out, T v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}
void put_name(std::string& out, const std::string& s) {
  put<std::uint8_t>(out, static_cast<std::uint8_t>(s.size()));
  out += s;
}

struct TriOffset {
  std::uint16_t index;
  std::int16_t x, y, z;
};
struct TriMorphSpec {
  std::string name;
  float mult;
  std::vector<TriOffset> offsets;
};
struct TriShapeSpec {
  std::string shape;
  std::vector<TriMorphSpec> morphs;
};

// TriFile::Write layout: "PIRT", u16 shapes, {name, u16 morphs, {name, f32 mult, u16 n, {u16 id, i16 x, y, z}}}.
std::string tri_bytes(const std::vector<TriShapeSpec>& shapes, bool with_uv_section = false) {
  std::string out = "PIRT";
  put<std::uint16_t>(out, static_cast<std::uint16_t>(shapes.size()));
  for (const auto& s : shapes) {
    put_name(out, s.shape);
    put<std::uint16_t>(out, static_cast<std::uint16_t>(s.morphs.size()));
    for (const auto& m : s.morphs) {
      put_name(out, m.name);
      put<float>(out, m.mult);
      put<std::uint16_t>(out, static_cast<std::uint16_t>(m.offsets.size()));
      for (const auto& o : m.offsets) {
        put<std::uint16_t>(out, o.index);
        put<std::int16_t>(out, o.x);
        put<std::int16_t>(out, o.y);
        put<std::int16_t>(out, o.z);
      }
    }
  }
  if (with_uv_section) {
    put<std::uint16_t>(out, 1);
    put_name(out, shapes.front().shape);
    put<std::uint16_t>(out, 1);
    put_name(out, "UVSlide");
    put<float>(out, 0.5f);
    put<std::uint16_t>(out, 1);
    put<std::uint16_t>(out, 0);
    put<std::int16_t>(out, 2);
    put<std::int16_t>(out, -4);
  }
  return out;
}

// ReadOSDData layout: bytes 00 'D' 'S' 'O', u32 version, u32 count, {name, u16 n, {u16 index, f32 x, y, z}}.
std::string osd_bytes(const std::vector<std::pair<std::string, std::vector<sns::body::VertexDelta>>>& entries) {
  std::string out("\0DSO", 4);
  put<std::uint32_t>(out, 1);
  put<std::uint32_t>(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto& [name, diffs] : entries) {
    put_name(out, name);
    put<std::uint16_t>(out, static_cast<std::uint16_t>(diffs.size()));
    for (const auto& d : diffs) {
      put<std::uint16_t>(out, d.index);
      put<float>(out, d.x);
      put<float>(out, d.y);
      put<float>(out, d.z);
    }
  }
  return out;
}

sns::nif::Shape quad_shape(const std::string& name) {
  sns::nif::Shape s;
  s.name = name;
  s.positions = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  s.normals = std::vector<sns::nif::Vec3>(4, {0, 0, 1});
  s.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  s.indices = {0, 1, 2, 0, 2, 3};
  return s;
}

// A small SSE NIF: one quad per shape, optional BODYTRI on the root (what BodySlide "Build Morphs" writes).
std::string nif_bytes(const std::vector<std::pair<std::string, int>>& shapes, const std::string& body_tri, float lift = 0.0f) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  for (const auto& [name, x] : shapes) {
    const float x0 = static_cast<float>(x);
    std::vector<nifly::Vector3> v{{x0, 0, lift}, {x0 + 10, 0, lift}, {x0 + 10, 10, lift}, {x0, 10, lift}};
    std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}};
    std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
    auto* shape = file.CreateShapeFromData(name, &v, &t, &uv, &n);
    std::string texture = "textures\\x.dds";
    file.SetTextureSlot(shape, texture, 0);
  }
  if (!body_tri.empty()) {
    auto extra = std::make_unique<nifly::NiStringExtraData>();
    extra->name.get() = "BODYTRI";
    extra->stringData.get() = body_tri;
    file.AssignExtraData(file.GetRootNode(), std::move(extra));
  }
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

std::vector<sns::nif::Vec3> view_positions(const sns::rendering::RenderPart& part) {
  std::vector<sns::nif::Vec3> out;
  for (std::size_t i = 0; i + sns::rendering::kFloatsPerVertex <= part.vertices.size(); i += sns::rendering::kFloatsPerVertex) {
    out.push_back({part.vertices[i], part.vertices[i + 1], part.vertices[i + 2]});
  }
  return out;
}

const sns::rendering::RenderPart& part_by_shape(const sns::rendering::CharacterScene& scene, const std::string& shape) {
  const auto it = std::find_if(scene.parts.begin(), scene.parts.end(), [&](const auto& p) { return p.shape_name == shape; });
  REQUIRE(it != scene.parts.end());
  return *it;
}

const sns::rendering::ShapeMorphState& state_of(const sns::rendering::CharacterScene& scene, const std::string& shape) {
  const auto it = std::find_if(scene.body_morphs.begin(), scene.body_morphs.end(), [&](const auto& s) { return s.shape == shape; });
  REQUIRE(it != scene.body_morphs.end());
  return *it;
}

float max_distance(const std::vector<sns::nif::Vec3>& a, const std::vector<sns::nif::Vec3>& b) {
  const auto d = sns::body::compare_positions(a, b, 0.0f);
  REQUIRE(d);
  return d->max;
}

}  // namespace

TEST_CASE("TRI files are read exactly like BodySlide's TriFile::Read", "[bodyslide][tri]") {
  const auto bytes = tri_bytes({{"3BA", {{"Breasts", 0.001f, {{0, 1000, -2000, 3000}, {3, 0, 0, 32767}}}, {"Butt", 0.5f, {{1, 2, 0, 0}}}}}},
                               true);
  const auto tri = sns::body::parse_tri(bytes, "test.tri");
  REQUIRE(tri.shapes.size() == 1);
  const auto* shape = tri.shape("3BA");
  REQUIRE(shape);
  CHECK(tri.shape("3ba") == nullptr);  // shape names are exact, like the NIF block names
  const auto* breasts = shape->find("Breasts");
  REQUIRE(breasts);
  REQUIRE(breasts->offsets.size() == 2);
  CHECK(breasts->offsets[0].x == Catch::Approx(1.0f));  // 1000 * 0.001
  CHECK(breasts->offsets[0].y == Catch::Approx(-2.0f));
  CHECK(breasts->offsets[0].z == Catch::Approx(3.0f));
  CHECK(breasts->offsets[1].z == Catch::Approx(32.767f));
  CHECK(shape->find("Butt")->offsets[0].x == Catch::Approx(1.0f));  // 2 * 0.5
  REQUIRE(shape->uv.size() == 1);  // UV section kept apart, never applied to positions
  CHECK(shape->uv[0].offsets[0].x == Catch::Approx(1.0f));
  CHECK(shape->uv[0].offsets[0].y == Catch::Approx(-2.0f));

  CHECK_THROWS_AS(sns::body::parse_tri("TRIP" + bytes.substr(4), "bad.tri"), sns::Error);       // wrong magic
  CHECK_THROWS_AS(sns::body::parse_tri(bytes.substr(0, 20), "short.tri"), sns::Error);          // truncated morphs
  const auto truncated_uv = sns::body::parse_tri(bytes.substr(0, bytes.size() - 3), "uv.tri");  // positions still valid
  CHECK(truncated_uv.shape("3BA")->find("Breasts"));
  CHECK_FALSE(truncated_uv.warnings.empty());
}

TEST_CASE("OSD, BSD and OSP slider sets are read like BodySlide", "[bodyslide][osd]") {
  const auto osd = sns::body::parse_osd(osd_bytes({{"3BABreasts", {{2, 0.5f, -1.25f, 4.0f}}}}), "x.osd");
  REQUIRE(osd.data.count("3BABreasts"));
  CHECK(osd.data.at("3BABreasts")[0].index == 2);
  CHECK(osd.data.at("3BABreasts")[0].z == 4.0f);
  CHECK_THROWS_AS(sns::body::parse_osd(std::string("OSD") + std::string(9, '\0'), "bad.osd"), sns::Error);

  std::string bsd;
  put<std::uint32_t>(bsd, 1);
  put<std::uint32_t>(bsd, 7);
  put<float>(bsd, 1.0f);
  put<float>(bsd, 2.0f);
  put<float>(bsd, 3.0f);
  const auto b = sns::body::parse_bsd(bsd, "x.bsd");
  REQUIRE(b.size() == 1);
  CHECK(b[0].index == 7);
  CHECK_THROWS_AS(sns::body::parse_bsd(bsd.substr(0, 10), "x.bsd"), sns::Error);

  const auto projects = sns::body::parse_slider_projects(R"(<?xml version="1.0"?>
<SliderSetInfo version="1">
  <SliderSet name="Shirt RED">
    <DataFolder>Temio Shirt</DataFolder>
    <SourceFile>Shirt.nif</SourceFile>
    <OutputPath>meshes\armor\Temio</OutputPath>
    <OutputFile GenWeights="true">Shirt RED</OutputFile>
    <Shape target="3BA" DataFolder="CBBE 3BA;Temio Shirt">3BA</Shape>
    <Shape target="Shirt">Shirt</Shape>
    <Slider name="Breasts" invert="false" small="10" big="40">
      <Data name="3BABreasts" target="3BA">Temio Shirt.osd\3BABreasts</Data>
      <Data name="ShirtBreasts" target="Shirt" local="true">Temio Shirt.osd/ShirtBreasts</Data>
    </Slider>
    <Slider name="7B Upper" invert="True" small="0" big="0"><Data target="3BA">X.bsd</Data></Slider>
    <Slider name="ZapIt" zap="true" small="100" big="100"><Data target="3BA">Temio Shirt.osd\Zap</Data></Slider>
  </SliderSet>
</SliderSetInfo>)", "CalienteTools\\BodySlide\\SliderSets\\temio.osp");
  REQUIRE(projects.size() == 1);
  const auto& p = projects[0];
  CHECK(p.name == "Shirt RED");
  CHECK(p.output_mesh(1) == "meshes\\armor\\temio\\shirt red_1.nif");
  CHECK(p.output_mesh(-1) == "meshes\\armor\\temio\\shirt red.nif");
  CHECK(p.source_mesh() == "CalienteTools\\BodySlide\\ShapeData\\Temio Shirt\\Shirt.nif");
  REQUIRE(p.shapes.size() == 2);
  CHECK(p.shapes[0].data_folders == std::vector<std::string>{"CBBE 3BA", "Temio Shirt"});
  CHECK(p.shapes[1].data_folders == std::vector<std::string>{"Temio Shirt"});  // the set's folder by default
  REQUIRE(p.sliders.size() == 3);
  CHECK(p.sliders[0].default_small == 10.0f);
  CHECK(p.sliders[0].default_big == 40.0f);
  CHECK(p.sliders[0].data[1].local);
  CHECK(p.sliders[1].invert);                       // "True": BodySlide compares 4 characters case-insensitively
  CHECK(p.sliders[1].data[0].name == "7B Upper");  // <Data> without a name uses the slider's
  CHECK(p.sliders[2].zap);

  // load_project_data resolves "file.osd\entry" in the shape's folders and keeps BodySlide's lookup by entry name.
  const std::string temio_osd = osd_bytes({{"3BABreasts", {{0, 0, 0, 1}}}, {"ShirtBreasts", {{1, 1, 0, 0}}}});
  const auto loaded = sns::body::load_project_data(p, [&](const std::string& path) -> std::optional<std::string> {
    if (path == "CalienteTools\\BodySlide\\ShapeData\\Temio Shirt\\Temio Shirt.osd") return temio_osd;
    return std::nullopt;  // CBBE 3BA folder, X.bsd: not installed
  });
  CHECK(loaded.sets.count("3BABreasts"));  // found in the shape's second folder
  CHECK(loaded.sets.count("ShirtBreasts"));
  CHECK(loaded.sets.at("ShirtBreasts").target == "Shirt");
  CHECK(std::any_of(loaded.missing.begin(), loaded.missing.end(), [](const auto& m) { return m.find("X.bsd") != std::string::npos; }));
}

TEST_CASE("Test A: BodySlide build arithmetic reproduced vertex by vertex (OSD) and the TRI path agrees", "[bodyslide][build]") {
  // A shape stored in skin space with a non-trivial transform: 90 degrees about Z, scale 2, translation (5, 0, 1).
  auto shape = quad_shape("3BA");
  sns::nif::Transform t;
  t.rotation = {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
  t.scale = 2.0f;
  t.translation = {5, 0, 1};
  shape.to_root = t;
  const std::vector<sns::nif::Vec3> raw = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  for (std::size_t i = 0; i < raw.size(); ++i) shape.positions[i] = t.apply(raw[i]);

  sns::body::SliderProject project;
  project.name = "Body";
  project.shapes = {{"3BA", "3BA", {"Body"}}};
  project.sliders = {
      {"Breasts", false, false, false, false, false, 0, 0, {{"3BABreasts", "3BA", false, "Body.osd\\3BABreasts"}}},
      {"7B Upper", true, false, false, false, false, 0, 0, {{"3BA7B Upper", "3BA", false, "Body.osd\\3BA7B Upper"}}},
      {"Defaulted", false, false, false, false, false, 50, 100, {{"3BADefaulted", "3BA", false, "Body.osd\\3BADefaulted"}}},
      {"Holes", false, true, false, false, false, 0, 0, {{"3BAHoles", "3BA", false, "Body.osd\\3BAHoles"}}},
  };
  sns::body::DiffSets data;
  data["3BABreasts"] = {"3BA", {{1, 0, 0, 2.0f}, {2, 1.0f, 0, 0}}};
  data["3BA7B Upper"] = {"3BA", {{3, 0, 4.0f, 0}}};
  data["3BADefaulted"] = {"3BA", {{0, 0, 0, -1.0f}}};
  data["3BAHoles"] = {"3BA", {{2, 9, 9, 9}}};
  data["OtherShapeBreasts"] = {"Shirt", {{0, 100, 100, 100}}};  // another target: must not move this shape

  // Preset: Breasts small 20 / big 60; 7B Upper big 25 (inverted -> 1 - 0.25); Holes big 100 (zap); Defaulted unset -> default.
  const std::vector<SliderSetting> preset = {{"Breasts", 20.0f, 60.0f}, {"7B Upper", std::nullopt, 25.0f}, {"Holes", 0.0f, 100.0f}};
  auto big = shape;
  const auto build = sns::body::apply_project_sliders(big, project, data, preset, true);
  REQUIRE(build.report.error.empty());
  // Raw-space expectations (ApplyDiff, then to_root): v1 += (0,0,2)*0.6; v2 += (1,0,0)*0.6; v3 += (0,4,0)*0.75; v0 += (0,0,-1)*1.0.
  const std::vector<sns::nif::Vec3> expected_raw = {{0, 0, -1.0f}, {10, 0, 1.2f}, {10.6f, 10, 0}, {0, 13.0f, 0}};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto e = t.apply(expected_raw[i]);
    CHECK(big.positions[i].x == Catch::Approx(e.x).margin(1e-5));
    CHECK(big.positions[i].y == Catch::Approx(e.y).margin(1e-5));
    CHECK(big.positions[i].z == Catch::Approx(e.z).margin(1e-5));
  }
  CHECK(build.zapped == std::vector<std::uint16_t>{2});  // zap collects; BodySlide deletes them from the built mesh

  // The _0 build uses the small values: Breasts 0.2, 7B Upper has no small value -> default 0 -> inverted 1.0, Defaulted 0.5.
  auto small = shape;
  sns::body::apply_project_sliders(small, project, data, preset, false);
  const auto v1 = t.apply({10, 0, 0.4f});
  const auto v3 = t.apply({0, 14.0f, 0});
  const auto v0 = t.apply({0, 0, -0.5f});
  CHECK(small.positions[1].z == Catch::Approx(v1.z).margin(1e-5));
  CHECK(small.positions[3].x == Catch::Approx(v3.x).margin(1e-5));
  CHECK(small.positions[0].z == Catch::Approx(v0.z).margin(1e-5));

  // RaceMenu's TRI for the same data (BodySlide writes each diff at 1.0 as int16 * multiplier) applied with the same preset at
  // weight 100 reproduces the BodySlide build for the non-inverted sliders, within the int16 quantization.
  auto from_tri = shape;
  const float mult = 2.0f / 32767.0f;
  const auto tri = sns::body::parse_tri(tri_bytes({{"3BA", {{"Breasts", mult, {{1, 0, 0, 32767}, {2, 16384, 0, 0}}}}}}), "b.tri");
  const auto report = sns::body::apply_tri_morphs(from_tri, *tri.shape("3BA"), {{"Breasts", 20.0f, 60.0f}}, 100.0f);
  REQUIRE(report.changed());
  CHECK(report.applied[0].value == Catch::Approx(0.6f));
  CHECK(report.applied[0].vertices == 2);
  const auto e1 = t.apply({10, 0, 1.2f});
  const auto e2 = t.apply({10.6f, 10, 0});
  CHECK(from_tri.positions[1].z == Catch::Approx(e1.z).margin(1e-3));
  CHECK(from_tri.positions[1].x == Catch::Approx(e1.x).margin(1e-3));
  CHECK(from_tri.positions[2].y == Catch::Approx(e2.y).margin(1e-3));
  CHECK(from_tri.positions[0].x == Catch::Approx(shape.positions[0].x));  // untouched vertex
}

TEST_CASE("TRI morphs: missing sliders and a TRI of another mesh are reported, never faked", "[bodyslide][tri]") {
  const auto tri = sns::body::parse_tri(
      tri_bytes({{"3BA", {{"Breasts", 0.01f, {{0, 0, 0, 100}}}, {"Butt", 0.01f, {{1, 0, -100, 0}}}}}, {"Bad", {{"Breasts", 0.01f, {{9, 0, 0, 100}}}}}}),
      "t.tri");
  auto shape = quad_shape("3BA");
  const auto before = shape.positions;
  const auto r = sns::body::apply_tri_morphs(shape, *tri.shape("3BA"), {{"Breasts", 0.0f, 100.0f}, {"Thighs", 50.0f, 50.0f}, {"Butt", 0.0f, 0.0f}}, 50.0f);
  CHECK(r.compatible == 2);                                    // Breasts and Butt exist
  CHECK(r.not_in_data == std::vector<std::string>{"Thighs"});  // non-zero and absent: "Morph no encontrado"
  REQUIRE(r.applied.size() == 1);                              // Butt is 0: nothing to apply
  CHECK(shape.positions[0].z == Catch::Approx(before[0].z + 0.5f));  // 100 * 0.01 * (0 + (100 - 0) * 0.5) / 100
  CHECK(shape.positions[1].y == Catch::Approx(before[1].y));
  CHECK(sns::body::slider_coverage(r).ratio == Catch::Approx(0.5f));

  auto bad = quad_shape("Bad");  // 4 vertices, the TRI moves vertex 9
  const auto bad_report = sns::body::apply_tri_morphs(bad, *tri.shape("Bad"), {{"Breasts", 100.0f, 100.0f}}, 100.0f);
  CHECK_FALSE(bad_report.error.empty());
  CHECK_FALSE(bad_report.changed());
  CHECK(max_distance(bad.positions, quad_shape("Bad").positions) == 0.0f);
}

TEST_CASE("Test D: a UBE outfit's morphs against a CBBE 3BA body give a grounded verdict, not an approval", "[bodyslide][family]") {
  // Vocabularies shaped like the real profile's: the UBE body TRI has 196 morphs and shares 3 names with 3BA's 154.
  const std::vector<std::string> body_3ba = {"Breasts", "BreastsSmall", "Butt", "BigButt", "Thighs", "Hips", "Waist", "ForearmSize",
                                             "NippleLength", "PregnancyBelly", "7B Upper", "7B Lower"};
  const std::vector<std::string> ube_outfit = {"SkinnyMorph", "Amazon", "Juicy_body", "Peachy", "Venus", "ForearmSize", "NippleLength",
                                               "PregnancyBelly", "Upper_TorsoSize n|p", "ChestUpperSize n|p", "Clavicles_Width n|p",
                                               "Clavicle_Center_upper", "ClaviclesFront", "ClaviclesAngle", "NipplesShowUp", "Donaught"};
  const auto m = sns::body::compare_vocabulary(body_3ba, ube_outfit);
  CHECK(m.shared == 3);
  CHECK(m.verdict == "incompatible");
  CHECK(m.reason.find("3 de 16") != std::string::npos);
  CHECK(sns::body::compare_vocabulary(body_3ba, {"Breasts", "Butt", "Thighs"}).verdict == "compatible");
  CHECK(sns::body::compare_vocabulary({}, ube_outfit).verdict == "unknown");

  // A 3BA preset on a UBE-shaped TRI: the coverage of its non-zero sliders is below the floor (the scene then undoes it).
  const auto tri = sns::body::parse_tri(tri_bytes({{"BaseShape", {{"Juicy_body", 0.01f, {{0, 0, 0, 100}}}, {"ForearmSize", 0.01f, {{1, 0, 0, 100}}}}}}), "u.tri");
  auto shape = quad_shape("BaseShape");
  const auto r = sns::body::apply_tri_morphs(shape, *tri.shape("BaseShape"),
                                             {{"Breasts", 100.0f, 100.0f}, {"Butt", 80.0f, 80.0f}, {"Thighs", 40.0f, 40.0f},
                                              {"Hips", 40.0f, 40.0f}, {"ForearmSize", 30.0f, 30.0f}},
                                             100.0f);
  CHECK(sns::body::slider_coverage(r).ratio < sns::body::kMinFamilyCoverage);
}

TEST_CASE("Hands and feet follow the family decided on the body: few sliders of their own are not another family", "[bodyslide][scene]") {
  sns::test::TempDir dir;
  dir.write("Mod/meshes/body/feet_1.nif", nif_bytes({{"Feet", 0}}, "body\\feet.tri"));
  dir.write("Mod/meshes/body/feet.tri", tri_bytes({{"Feet", {{"Heel", 0.01f, {{2, 0, 0, 200}}}}}}));
  dir.write("Mod/meshes/body/body_1.nif", nif_bytes({{"BaseShape", 20}}, "body\\body.tri"));
  const std::vector<TriMorphSpec> body_morphs{{"Heel", 0.01f, {{0, 0, 0, 100}}}, {"Breasts", 0.01f, {{1, 0, 0, 100}}},
                                              {"Hips", 0.01f, {{2, 0, 0, 100}}}, {"Waist", 0.01f, {{3, 0, 0, 100}}}};
  dir.write("Mod/meshes/body/body.tri", tri_bytes({{"BaseShape", body_morphs}}));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Mod", dir.path() / "Mod"}});
  sns::assets::AssetResolver resolver(vfs, {});

  sns::rendering::Appearance a;
  a.npc_form_key = "002B6C:Dawnguard.esm";
  a.female = true;
  a.weight = 100.0f;
  // The feet come first in the record: the decision on the body must not depend on the addon order.
  a.pieces = {{"", "Skin", "skin", (1u << 7) | (1u << 2),
               {{"", "meshes\\body\\feet_1.nif", false, 1u << 7}, {"", "meshes\\body\\body_1.nif", false, 1u << 2}}}};

  sns::rendering::SceneOptions same_family;
  same_family.body_preset = sns::rendering::BodyPresetOption{"Preset", "UBE", {{"Heel", 100.0f, 100.0f}, {"Breasts", 100.0f, 100.0f}, {"Hips", 100.0f, 100.0f},
                                                                              {"Waist", 100.0f, 100.0f}, {"Missing A", 100.0f, 100.0f}}};
  const auto ok = sns::rendering::build_character_scene(a, resolver, same_family);
  // 1 of 5 sliders on the feet (under the 25% that means another family on a body), and still applied: heel raised by 2.
  CHECK(state_of(ok, "BaseShape").applied.size() == 4);
  REQUIRE(state_of(ok, "Feet").applied.size() == 1);
  CHECK(state_of(ok, "Feet").status != "incompatible-family");
  const auto base = sns::rendering::build_character_scene(a, resolver, {});
  CHECK(max_distance(view_positions(part_by_shape(base, "Feet")), view_positions(part_by_shape(ok, "Feet"))) == Catch::Approx(2.0f).margin(1e-4));

  // The body refuses a preset of another family: the feet are not half-applied, even with a matching name.
  sns::rendering::SceneOptions foreign;
  foreign.body_preset = sns::rendering::BodyPresetOption{"Foreign", "CBBE", {{"Heel", 100.0f, 100.0f}, {"X1", 100.0f, 100.0f}, {"X2", 100.0f, 100.0f},
                                                                            {"X3", 100.0f, 100.0f}, {"X4", 100.0f, 100.0f}}};
  const auto refused = sns::rendering::build_character_scene(a, resolver, foreign);
  CHECK(state_of(refused, "BaseShape").status == "incompatible-family");
  CHECK(state_of(refused, "Feet").status == "incompatible-family");
  CHECK(max_distance(view_positions(part_by_shape(base, "Feet")), view_positions(part_by_shape(refused, "Feet"))) == 0.0f);
}

TEST_CASE("Garments of the body's family follow its decision on the preset; a garment of another family does not", "[bodyslide][scene]") {
  sns::test::TempDir dir;
  dir.write("Mod/meshes/body/body_1.nif", nif_bytes({{"BaseShape", 0}}, "body\\body.tri"));
  const std::vector<TriMorphSpec> body_morphs{{"Heel", 0.01f, {{0, 0, 0, 100}}}, {"Breasts", 0.01f, {{1, 0, 0, 100}}},
                                              {"Hips", 0.01f, {{2, 0, 0, 100}}}, {"Waist", 0.01f, {{3, 0, 0, 100}}}};
  dir.write("Mod/meshes/body/body.tri", tri_bytes({{"BaseShape", body_morphs}}));
  dir.write("Mod/meshes/armor/boots_1.nif", nif_bytes({{"Boots", 20}}, "armor\\boots.tri"));
  dir.write("Mod/meshes/armor/boots.tri", tri_bytes({{"Boots", {{"Heel", 0.01f, {{2, 0, 0, 200}}}}}}));
  dir.write("Mod/meshes/armor/alien_1.nif", nif_bytes({{"Alien", 40}}, "armor\\alien.tri"));
  dir.write("Mod/meshes/armor/alien.tri", tri_bytes({{"Alien", {{"Heel", 0.01f, {{2, 0, 0, 200}}}, {"Other1", 0.01f, {{0, 0, 0, 100}}},
                                                                {"Other2", 0.01f, {{0, 0, 0, 100}}}, {"Other3", 0.01f, {{0, 0, 0, 100}}}}}}));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Mod", dir.path() / "Mod"}});
  sns::assets::AssetResolver resolver(vfs, {});

  sns::rendering::Appearance a;
  a.npc_form_key = "002B6C:Dawnguard.esm";
  a.female = true;
  a.weight = 100.0f;
  // The garments come first in the record: the skin still decides for them.
  a.pieces = {{"B", "Boots", "outfit", 1u << 7, {{"", "meshes\\armor\\boots_1.nif", false, 1u << 7}}},
              {"A", "Alien", "outfit", 1u << 14, {{"", "meshes\\armor\\alien_1.nif", false, 1u << 14}}},
              {"", "Skin", "skin", 1u << 2, {{"", "meshes\\body\\body_1.nif", false, 1u << 2}}}};

  sns::rendering::SceneOptions options;
  options.body_preset = sns::rendering::BodyPresetOption{"Preset", "UBE", {{"Heel", 100.0f, 100.0f}, {"Breasts", 100.0f, 100.0f}, {"Hips", 100.0f, 100.0f},
                                                                          {"Waist", 100.0f, 100.0f}, {"Missing A", 100.0f, 100.0f}}};
  const auto scene = sns::rendering::build_character_scene(a, resolver, options);
  const auto base = sns::rendering::build_character_scene(a, resolver, {});
  // Boots: 1 of 5 sliders (under 25%), all their vocabulary on the body: applied, heel raised by 2.
  REQUIRE(state_of(scene, "Boots").applied.size() == 1);
  CHECK(max_distance(view_positions(part_by_shape(base, "Boots")), view_positions(part_by_shape(scene, "Boots"))) == Catch::Approx(2.0f).margin(1e-4));
  // Same coverage, but 1 of its 4 morphs on the body: another family, unchanged.
  CHECK(state_of(scene, "Alien").status == "incompatible-family");
  CHECK(max_distance(view_positions(part_by_shape(base, "Alien")), view_positions(part_by_shape(scene, "Alien"))) == 0.0f);
}

TEST_CASE("Test B and C: the scene morphs body AND outfit with their own TRIs, follows NPC weight, and says what it could not do",
          "[bodyslide][scene]") {
  sns::test::TempDir dir;
  // Body (skin, hands slot so it stays visible) and an outfit (slot 32 shirt) with its own body shape and TRI, a piece without
  // BODYTRI and a UBE-family piece. _0 meshes are lower than _1 to check the weight blend.
  const float low = -2.0f;
  dir.write("Mod/meshes/body/femalebody_1.nif", nif_bytes({{"3BA", 0}}, "body\\femalebody.tri"));
  dir.write("Mod/meshes/body/femalebody_0.nif", nif_bytes({{"3BA", 0}}, "body\\femalebody.tri", low));
  dir.write("Mod/meshes/body/femalebody.tri", tri_bytes({{"3BA", {{"Breasts", 0.01f, {{2, 0, 0, 300}}}}}}));
  dir.write("Mod/meshes/armor/shirt_1.nif", nif_bytes({{"Shirt", 20}, {"ShirtBody", 40}}, "armor\\shirt.tri"));
  dir.write("Mod/meshes/armor/shirt_0.nif", nif_bytes({{"Shirt", 20}, {"ShirtBody", 40}}, "armor\\shirt.tri", low));
  dir.write("Mod/meshes/armor/shirt.tri",
            tri_bytes({{"Shirt", {{"Breasts", 0.01f, {{1, 0, 200, 0}}}}}, {"ShirtBody", {{"Breasts", 0.01f, {{3, 100, 0, 0}}}}}}));
  dir.write("Mod/meshes/armor/boots_1.nif", nif_bytes({{"Boots", 60}}, ""));
  dir.write("Mod/meshes/armor/ube_1.nif", nif_bytes({{"BaseShape", 80}}, "armor\\ube.tri"));
  dir.write("Mod/meshes/armor/ube.tri", tri_bytes({{"BaseShape", {{"Juicy_body", 0.01f, {{0, 0, 0, 500}}}}}}));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Mod", dir.path() / "Mod"}});
  sns::assets::AssetResolver resolver(vfs, {});

  sns::rendering::Appearance a;
  a.npc_form_key = "002B6C:Dawnguard.esm";
  a.female = true;
  a.weight = 100.0f;
  sns::rendering::AppearancePiece skin{"", "Skin", "skin", 1u << 3, {{"", "meshes\\body\\femalebody_1.nif", true, 1u << 3}}};
  sns::rendering::AppearancePiece shirt{"S", "Shirt", "outfit", 1u << 2, {{"", "meshes\\armor\\shirt_1.nif", true, 1u << 2}}};
  sns::rendering::AppearancePiece boots{"B", "Boots", "outfit", 1u << 7, {{"", "meshes\\armor\\boots_1.nif", false, 1u << 7}}};
  sns::rendering::AppearancePiece ube{"U", "UBE top", "outfit", 1u << 14, {{"", "meshes\\armor\\ube_1.nif", false, 1u << 14}}};
  a.pieces = {skin, shirt, boots, ube};

  sns::rendering::SceneOptions plain;
  const auto base = sns::rendering::build_character_scene(a, resolver, plain);
  // No preset: the morph data is reported (available / missing), nothing is claimed as applied.
  CHECK(state_of(base, "3BA").status == "available");
  CHECK(state_of(base, "3BA").morphs_available == 1);
  CHECK(state_of(base, "Boots").status == "missing-tri");
  CHECK(std::none_of(base.body_morphs.begin(), base.body_morphs.end(), [](const auto& s) { return !s.applied.empty(); }));
  // Outfit families decided from the morph vocabularies against the body's TRI.
  CHECK(state_of(base, "Shirt").family_verdict == "compatible");
  CHECK(state_of(base, "BaseShape").family_verdict == "incompatible");
  CHECK(state_of(base, "3BA").family_verdict == "body");

  sns::rendering::SceneOptions options;
  options.body_preset = sns::rendering::BodyPresetOption{"Test 3BA", "CBBE 3BA", {{"Breasts", 0.0f, 100.0f}, {"Butt", 50.0f, 50.0f}}};
  const auto scene = sns::rendering::build_character_scene(a, resolver, options);

  // Body: vertex 2 moves +3 along Skyrim Z (view +Y) at weight 100.
  const auto body_before = view_positions(part_by_shape(base, "3BA"));
  const auto body_after = view_positions(part_by_shape(scene, "3BA"));
  CHECK(body_after[2].y - body_before[2].y == Catch::Approx(3.0f).margin(1e-4));
  CHECK(max_distance(body_before, body_after) == Catch::Approx(3.0f).margin(1e-4));
  CHECK(state_of(scene, "3BA").status == "partial");  // Butt (non-zero) has no morph on this body
  CHECK(state_of(scene, "3BA").missing == std::vector<std::string>{"Butt"});

  // Outfit: its own morph data moves its own vertices (Skyrim +Y = view +Z), not a copy of the body's.
  const auto shirt_before = view_positions(part_by_shape(base, "Shirt"));
  const auto shirt_after = view_positions(part_by_shape(scene, "Shirt"));
  CHECK(shirt_after[1].z - shirt_before[1].z == Catch::Approx(2.0f).margin(1e-4));
  CHECK(max_distance(shirt_before, shirt_after) == Catch::Approx(2.0f).margin(1e-4));
  CHECK(state_of(scene, "Shirt").status == "partial");
  CHECK(state_of(scene, "Shirt").applied.size() == 1);
  CHECK(max_distance(view_positions(part_by_shape(base, "ShirtBody")), view_positions(part_by_shape(scene, "ShirtBody"))) ==
        Catch::Approx(1.0f).margin(1e-4));

  // Without BODYTRI: unchanged and labelled; another family: unchanged and labelled.
  CHECK(max_distance(view_positions(part_by_shape(base, "Boots")), view_positions(part_by_shape(scene, "Boots"))) == 0.0f);
  CHECK(state_of(scene, "Boots").status == "missing-tri");
  CHECK(max_distance(view_positions(part_by_shape(base, "BaseShape")), view_positions(part_by_shape(scene, "BaseShape"))) == 0.0f);
  CHECK(state_of(scene, "BaseShape").status == "incompatible-family");
  const bool applied_issue = std::any_of(scene.issues.begin(), scene.issues.end(), [](const auto& i) { return i.code == "BodyPresetApplied"; });
  CHECK(applied_issue);

  // Test C: weight 0 / 50 / 100 with real _0/_1 meshes. Preset Breasts small 0 big 100: at weight 0 the body is the _0 mesh,
  // unmorphed; at 50 the blend (z = -1) plus half the morph; at 100 the _1 mesh plus the full morph.
  for (const float weight : {0.0f, 50.0f, 100.0f}) {
    a.weight = weight;
    const auto s = sns::rendering::build_character_scene(a, resolver, options);
    const auto body = view_positions(part_by_shape(s, "3BA"));
    const float blend_y = low * (1.0f - weight / 100.0f);  // Skyrim Z of the flat quad after the _0/_1 blend
    CHECK(body[0].y == Catch::Approx(blend_y).margin(1e-4));  // not moved by the morph
    CHECK(body[2].y == Catch::Approx(blend_y + 3.0f * weight / 100.0f).margin(1e-4));
    CHECK(part_by_shape(s, "3BA").weight_blended);
  }

  // A preset whose sliders the meshes never mention changes nothing and says so.
  a.weight = 100.0f;
  sns::rendering::SceneOptions foreign;
  foreign.body_preset = sns::rendering::BodyPresetOption{"UBE preset", "UBE", {{"Amazon", 100.0f, 100.0f}}};
  const auto none = sns::rendering::build_character_scene(a, resolver, foreign);
  CHECK(state_of(none, "3BA").status == "incompatible-family");
  CHECK(max_distance(view_positions(part_by_shape(base, "3BA")), view_positions(part_by_shape(none, "3BA"))) == 0.0f);
  const bool not_applied = std::any_of(none.issues.begin(), none.issues.end(), [](const auto& i) { return i.code == "BodyPresetNotApplied"; });
  CHECK(not_applied);
}

TEST_CASE("A body whose BODYTRI path is won by another body's TRI is never morphed and says why (Serana's 3BA vs a CBBE TRI)",
          "[bodyslide][scene]") {
  sns::test::TempDir dir;
  // The real case: Priscilla's 3BA body names the GLOBAL actors\character\character assets\femalebody.tri, which another mod wins
  // with a CBBE body TRI (shape "CBBE"). RaceMenu finds no "3BA" morphs there; the viewer must not pretend otherwise.
  dir.write("Body/meshes/priscilla/femalebody_1.nif", nif_bytes({{"3BA", 0}}, "actors\\character\\character assets\\femalebody.tri"));
  dir.write("Other/meshes/actors/character/character assets/femalebody.tri", tri_bytes({{"CBBE", {{"Breasts", 0.01f, {{0, 0, 0, 100}}}}}}));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Body", dir.path() / "Body"}, {"Other", dir.path() / "Other"}});
  sns::assets::AssetResolver resolver(vfs, {});
  sns::rendering::Appearance a;
  a.female = true;
  a.pieces = {{"", "Skin", "skin", 1u << 2, {{"", "meshes\\priscilla\\femalebody_1.nif", false, 1u << 2}}}};
  sns::rendering::SceneOptions options;
  options.body_preset = sns::rendering::BodyPresetOption{"P", "CBBE 3BA", {{"Breasts", 100.0f, 100.0f}}};
  const auto plain = sns::rendering::build_character_scene(a, resolver, {});
  const auto scene = sns::rendering::build_character_scene(a, resolver, options);
  const auto& st = state_of(scene, "3BA");
  CHECK(st.status == "missing-tri");
  CHECK(st.detail.find("«CBBE»") != std::string::npos);
  CHECK(st.tri_provider.find("Other") != std::string::npos);
  CHECK(max_distance(view_positions(part_by_shape(plain, "3BA")), view_positions(part_by_shape(scene, "3BA"))) == 0.0f);
  CHECK(std::any_of(scene.issues.begin(), scene.issues.end(), [](const auto& i) { return i.code == "BodyPresetNotApplied"; }));
}

TEST_CASE("A built mesh is checked against its BodySlide project: base without preset, baked preset, or not comparable",
          "[bodyslide][build]") {
  // Project "Shirt": one shape, one slider whose default big value is 50 (so the defaults build moves vertex 1 by 0.5 * 4 = 2).
  const auto projects = sns::body::parse_slider_projects(R"(<SliderSetInfo version="1"><SliderSet name="Shirt">
    <DataFolder>Shirt</DataFolder><SourceFile>Shirt.nif</SourceFile><OutputPath>meshes\armor\x</OutputPath>
    <OutputFile GenWeights="true">shirt</OutputFile><Shape target="Shirt">Shirt</Shape>
    <Slider name="Breasts" small="0" big="50"><Data name="ShirtBreasts" target="Shirt">Shirt.osd\ShirtBreasts</Data></Slider>
  </SliderSet></SliderSetInfo>)", "CalienteTools\\BodySlide\\SliderSets\\shirt.osp");
  const auto index = sns::body::build_project_index(projects);
  REQUIRE(index.for_mesh("meshes\\armor\\x\\shirt_1.nif").size() == 1);
  REQUIRE(index.for_mesh("Meshes\\Armor\\X\\Shirt_0.nif").size() == 1);
  CHECK(index.for_mesh("meshes\\armor\\x\\other_1.nif").empty());
  const auto& project = *index.for_mesh("meshes\\armor\\x\\shirt_1.nif").front();

  const auto source = nif_bytes({{"Shirt", 0}}, "");
  const auto osd = osd_bytes({{"ShirtBreasts", {{1, 0, 0, 4.0f}}}});
  const sns::body::DataReader read = [&](const std::string& path) -> std::optional<std::string> {
    if (path == "CalienteTools\\BodySlide\\ShapeData\\Shirt\\Shirt.nif") return source;
    if (path == "CalienteTools\\BodySlide\\ShapeData\\Shirt\\Shirt.osd") return osd;
    return std::nullopt;
  };
  // What BodySlide writes for _1 without a preset: vertex 1 lifted by 2 (default big 50 % of 4).
  auto base = sns::nif::load_mesh(source, "s");
  base.shapes[0].positions[1].z += 2.0f;
  const auto ok = sns::body::check_base_build(project, read, base, "meshes\\armor\\x\\shirt_1.nif");
  CHECK(ok.verdict == "base");
  CHECK(ok.reference == "defaults");
  CHECK(ok.max_distance < 1e-5f);
  // The _0 without a preset is the zeroed source (default small 0): both references agree.
  CHECK(sns::body::check_base_build(project, read, sns::nif::load_mesh(source, "s"), "meshes\\armor\\x\\shirt_0.nif").verdict == "base");

  // Built with a preset (Breasts big 100): 2 units away from the base -> baked.
  auto baked = sns::nif::load_mesh(source, "s");
  baked.shapes[0].positions[1].z += 4.0f;
  const auto bad = sns::body::check_base_build(project, read, baked, "meshes\\armor\\x\\shirt_1.nif");
  CHECK(bad.verdict == "preset-baked");
  CHECK(bad.max_distance == Catch::Approx(2.0f).margin(1e-4));
  CHECK(bad.detail.find("Zeroed Sliders") != std::string::npos);

  // Another version of the outfit (different vertex count): never called base.
  const auto other = sns::nif::load_mesh(nif_bytes({{"Shirt", 0}, {"Extra", 20}}, ""), "o");
  auto bigger = other;
  bigger.shapes[0].positions.push_back({1, 1, 1});
  CHECK(sns::body::check_base_build(project, read, bigger, "meshes\\armor\\x\\shirt_1.nif").verdict == "not-comparable");
  // Missing source data is an error, not a verdict.
  CHECK(sns::body::check_base_build(project, [](const std::string&) { return std::optional<std::string>{}; }, base,
                                    "meshes\\armor\\x\\shirt_1.nif").verdict == "error");
}

TEST_CASE("A body hidden under the outfit is morphed but not counted as visible; mesh and TRI from different builds are flagged",
          "[bodyslide][scene]") {
  sns::test::TempDir dir;
  dir.write("Old/meshes/armor/shirt_1.nif", nif_bytes({{"Shirt", 0}}, "armor\\shirt.tri"));
  dir.write("New/meshes/armor/shirt.tri", tri_bytes({{"Shirt", {{"Breasts", 0.01f, {{0, 0, 0, 100}}}}}}));
  dir.write("New/meshes/body/femalebody_1.nif", nif_bytes({{"3BA", 30}}, "body\\femalebody.tri"));
  dir.write("New/meshes/body/femalebody.tri", tri_bytes({{"3BA", {{"Breasts", 0.01f, {{0, 0, 0, 100}}}}}}));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"New", dir.path() / "New"}, {"Old", dir.path() / "Old"}});
  sns::assets::AssetResolver resolver(vfs, {});
  sns::rendering::Appearance a;
  a.female = true;
  a.weight = 100.0f;
  a.pieces = {{"", "Skin", "skin", 1u << 2, {{"", "meshes\\body\\femalebody_1.nif", false, 1u << 2}}},
              {"S", "Shirt", "outfit", 1u << 2, {{"", "meshes\\armor\\shirt_1.nif", false, 1u << 2}}}};
  sns::rendering::SceneOptions options;
  options.body_preset = sns::rendering::BodyPresetOption{"P", "CBBE 3BA", {{"Breasts", 100.0f, 100.0f}}};
  const auto scene = sns::rendering::build_character_scene(a, resolver, options);
  const auto& body = state_of(scene, "3BA");
  CHECK(body.status == "applied");
  CHECK_FALSE(body.shown);  // covered by the shirt: deformed, but the user does not see it
  const auto& shirt = state_of(scene, "Shirt");
  CHECK(shirt.status == "needs-rebuild");
  CHECK(shirt.shown);
  CHECK(shirt.detail.find("builds distintos") != std::string::npos);
  const bool stale = std::any_of(scene.issues.begin(), scene.issues.end(), [](const auto& i) { return i.code == "BodySlideStaleBuild"; });
  CHECK(stale);
}
