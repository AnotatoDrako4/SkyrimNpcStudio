// body::build_slider_set: the NIF files BodySlide's BuildBodies writes. Expected positions are computed by hand from
// BodySlide's arithmetic (offset * slider value, _0 with the small value, _1 with the big one); normals are checked by
// their direction after the move, not against the code under test. The equality with real BodySlide output files was
// measured on the owner's profile with `sns-probe bodyslide-write --compare` (out/local-implementation/sns-serana-20261026).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>

#include <algorithm>
#include <sstream>

#include "sns/body/bodyslide_build.hpp"
#include "sns/nif/mesh.hpp"

namespace {

using sns::body::SliderSetting;

// Two quads "A" (x 0..10) and "B" (x 20..30), flat on z = 0, normals +Z; optional BODYTRI on the root.
std::string source_nif(const std::string& body_tri = {}) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  for (const auto& [name, x] : std::vector<std::pair<std::string, float>>{{"A", 0.0f}, {"B", 20.0f}}) {
    std::vector<nifly::Vector3> v{{x, 0, 0}, {x + 10, 0, 0}, {x + 10, 10, 0}, {x, 10, 0}};
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

sns::body::SliderProject project(const std::string& shape_b_attributes = {}, const std::string& output_file_attributes = {}) {
  const std::string osp = R"(<SliderSetInfo version="1">
  <SliderSet name="T">
    <DataFolder>T</DataFolder>
    <SourceFile>T.nif</SourceFile>
    <OutputPath>meshes\t</OutputPath>
    <OutputFile GenWeights="true" )" + output_file_attributes + R"(>t</OutputFile>
    <Shape target="A">A</Shape>
    <Shape target="B" )" + shape_b_attributes + R"(>B</Shape>
    <Slider name="Lift" small="0" big="0"><Data name="A_Lift" target="A">T.osd\A_Lift</Data><Data name="B_Lift" target="B">T.osd\B_Lift</Data></Slider>
    <Slider name="ZapB" zap="true" small="0" big="0"><Data name="B_Zap" target="B">T.osd\B_Zap</Data></Slider>
    <Slider name="Slide" uv="true" small="0" big="0"><Data name="A_Slide" target="A">T.osd\A_Slide</Data></Slider>
  </SliderSet>
</SliderSetInfo>)";
  auto projects = sns::body::parse_slider_projects(osp, "CalienteTools\\BodySlide\\SliderSets\\T.osp");
  REQUIRE(projects.size() == 1);
  return projects.front();
}

sns::body::DiffSets data(std::vector<std::uint16_t> zapped = {0, 1, 2, 3}) {
  sns::body::DiffSets sets;
  sets["A_Lift"] = {"A", {{2, 0, 0, 4}}};  // vertex 2 of A up by 4 at value 1
  sets["B_Lift"] = {"B", {{2, 0, 0, 4}}};
  std::vector<sns::body::VertexDelta> zap;
  for (const auto i : zapped) zap.push_back({i, 0, 0, 0});
  sets["B_Zap"] = {"B", zap};
  sets["A_Slide"] = {"A", {{0, 0.5f, 0, 0}}};
  return sets;
}

const sns::nif::Shape* shape(const sns::nif::MeshFile& mesh, const std::string& name) {
  const auto it = std::find_if(mesh.shapes.begin(), mesh.shapes.end(), [&](const auto& s) { return s.name == name; });
  return it == mesh.shapes.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("A slider set is built into _0 and _1 NIFs like BodySlide: small and big values, normals follow the shape", "[bodyslide][write]") {
  const auto p = project();
  const std::vector<SliderSetting> preset{{"Lift", 50.0f, 100.0f}};
  const auto build = sns::body::build_slider_set(p, source_nif(), data(), preset);
  REQUIRE(build.ok());
  CHECK_FALSE(build.vertex_count_changed());
  REQUIRE(build.shapes.size() == 2);
  CHECK(build.shapes[0].sliders_applied == 1);
  CHECK(build.shapes[0].normals_recalculated);

  const auto big = sns::nif::load_mesh(build.big, "t_1.nif");
  const auto small = sns::nif::load_mesh(build.small, "t_0.nif");
  const auto* a1 = shape(big, "A");
  const auto* a0 = shape(small, "A");
  REQUIRE(a1);
  REQUIRE(a0);
  CHECK(a1->positions[2].z == Catch::Approx(4.0f));  // 4 * 100%
  CHECK(a0->positions[2].z == Catch::Approx(2.0f));  // 4 * 50%
  CHECK(a1->positions[0].z == Catch::Approx(0.0f));
  // Vertex 1 only belongs to triangle (0,1,2), which now rises toward vertex 2: normal (0,-40,100) normalized, not +Z.
  REQUIRE(a1->normals.size() == 4);
  CHECK(a1->normals[1].y < -0.1f);
  CHECK(a1->normals[1].z > 0.5f);
  // B moved too (same slider), and the UV slider at 0 changed nothing.
  CHECK(shape(big, "B")->positions[2].z == Catch::Approx(4.0f));
  CHECK(a1->uvs[0].u == Catch::Approx(0.0f));
}

TEST_CASE("LockNormals keeps a shape's normals; SmoothSeamNormals and KeepZappedShapes are read from the OSP", "[bodyslide][write]") {
  const auto p = project(R"(LockNormals="true" SmoothSeamNormals="false")", R"(KeepZappedShapes="true")");
  CHECK(p.keep_zapped_shapes);
  CHECK(p.shapes[1].lock_normals);
  CHECK_FALSE(p.shapes[1].smooth_seam_normals);
  CHECK(p.shapes[0].smooth_seam_normals);
  const auto build = sns::body::build_slider_set(p, source_nif(), data(), {{"Lift", 100.0f, 100.0f}});
  REQUIRE(build.ok());
  CHECK_FALSE(build.shapes[1].normals_recalculated);
  const auto mesh = sns::nif::load_mesh(build.big, "t_1.nif");
  const auto* b = shape(mesh, "B");
  REQUIRE(b);
  REQUIRE(b->normals.size() == 4);
  for (const auto& n : b->normals) CHECK(n.z == Catch::Approx(1.0f));
}

TEST_CASE("Zap sliders delete vertices or whole shapes in both files, or hide the shape with KeepZappedShapes", "[bodyslide][write]") {
  SECTION("whole shape zapped: deleted, the other shapes' TRI stays valid") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data(), {{"ZapB", 100.0f, 100.0f}});
    REQUIRE(build.ok());
    CHECK(build.shapes[1].deleted);
    CHECK_FALSE(build.vertex_count_changed());
    const auto big = sns::nif::load_mesh(build.big, "t_1.nif");
    const auto small = sns::nif::load_mesh(build.small, "t_0.nif");
    CHECK(shape(big, "B") == nullptr);
    CHECK(shape(small, "B") == nullptr);
    CHECK(shape(big, "A") != nullptr);
  }
  SECTION("whole shape zapped with KeepZappedShapes: kept hidden") {
    const auto build = sns::body::build_slider_set(project({}, R"(KeepZappedShapes="true")"), source_nif(), data(), {{"ZapB", 100.0f, 100.0f}});
    REQUIRE(build.ok());
    const auto mesh = sns::nif::load_mesh(build.big, "t_1.nif");
    const auto* b = shape(mesh, "B");
    REQUIRE(b);
    CHECK(b->hidden);
  }
  SECTION("one vertex zapped: the shape loses it and the existing TRI no longer fits") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data({3}), {{"ZapB", 100.0f, 100.0f}});
    REQUIRE(build.ok());
    CHECK(build.vertex_count_changed());
    const auto mesh = sns::nif::load_mesh(build.big, "t_1.nif");
    const auto* b = shape(mesh, "B");
    REQUIRE(b);
    CHECK(b->positions.size() == 3);
    CHECK(b->indices.size() == 3);  // triangle (0,2,3) went with vertex 3
  }
  SECTION("zap only at weight 0: BodySlide uses the last (_0) zap list for both files") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data(), {{"ZapB", 100.0f, 0.0f}});
    REQUIRE(build.ok());
    const auto mesh = sns::nif::load_mesh(build.big, "t_1.nif");
    CHECK(shape(mesh, "B") == nullptr);
  }
}

TEST_CASE("UV sliders move texture coordinates by their value, per size, and no vertex", "[bodyslide][write]") {
  const auto build = sns::body::build_slider_set(project(), source_nif(), data(), {{"Slide", 50.0f, 100.0f}});
  REQUIRE(build.ok());
  const auto big = sns::nif::load_mesh(build.big, "t_1.nif");
  const auto small = sns::nif::load_mesh(build.small, "t_0.nif");
  const auto* a1 = shape(big, "A");
  const auto* a0 = shape(small, "A");
  REQUIRE(a1);
  REQUIRE(a0);
  CHECK(a1->uvs[0].u == Catch::Approx(0.5f));   // offset 0.5 * 100%
  CHECK(a0->uvs[0].u == Catch::Approx(0.25f));  // offset 0.5 * 50%
  CHECK(a1->uvs[1].u == Catch::Approx(1.0f));
  CHECK(a1->positions[0].x == Catch::Approx(0.0f));
}

TEST_CASE("A build that cannot be reproduced is refused, and the source's extra data is kept as it is", "[bodyslide][write]") {
  SECTION("unreadable source") {
    const auto build = sns::body::build_slider_set(project(), "not a nif", data(), {});
    CHECK_FALSE(build.ok());
    CHECK_FALSE(build.errors.empty());
  }
  SECTION("BODYTRI is neither added nor removed") {
    const auto with = sns::body::build_slider_set(project(), source_nif("t\\t.tri"), data(), {{"Lift", 100.0f, 100.0f}});
    const auto without = sns::body::build_slider_set(project(), source_nif(), data(), {{"Lift", 100.0f, 100.0f}});
    REQUIRE(with.ok());
    REQUIRE(without.ok());
    CHECK(with.big.find("BODYTRI") != std::string::npos);
    CHECK(without.big.find("BODYTRI") == std::string::npos);
  }
}

// The BODYTRI string a shape of a built NIF carries ("" without one).
std::string body_tri_of(const std::string& nif_bytes, const std::string& shape_name) {
  nifly::NifFile file;
  std::istringstream in(nif_bytes, std::ios::binary);
  REQUIRE(file.Load(in) == 0);
  auto* s = file.FindBlockByName<nifly::NiShape>(shape_name);
  if (!s) return {};
  for (const auto& ref : s->extraDataRefs) {
    auto* extra = file.GetHeader().GetBlock<nifly::NiStringExtraData>(ref);
    if (extra && extra->name.get() == "BODYTRI") return extra->stringData.get();
  }
  return {};
}

TEST_CASE("Build Morphs writes the TRI BodySlide writes and hangs BODYTRI on the project's first shape", "[bodyslide][write][tri]") {
  // Byte equality with real BodySlide TRIs was measured on 8 slider sets of the owner's profile (OMF outfits in
  // "BodyChangeNG - BodySlide Output", out/local-implementation/sns-sra-elisif-20261101); here the content is checked by hand.
  SECTION("full offsets of every position slider, UV sliders apart, shapes by name") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data(), {{"Lift", 30.0f, 60.0f}}, true);
    REQUIRE(build.ok());
    CHECK(build.tri_path == "meshes\\t\\t.tri");
    const auto tri = sns::body::parse_tri(build.tri, "t.tri");
    CHECK(tri.warnings.empty());
    REQUIRE(tri.shapes.size() == 2);
    CHECK(tri.shapes[0].name == "A");
    CHECK(tri.shapes[1].name == "B");
    // The TRI holds the slider at 100 % whatever the preset: the game applies the preset value to it.
    const auto* lift = tri.shapes[0].find("Lift");
    REQUIRE(lift);
    REQUIRE(lift->offsets.size() == 1);
    CHECK(lift->offsets[0].index == 2);
    CHECK(lift->offsets[0].z == Catch::Approx(4.0f).margin(1e-3));
    CHECK(tri.shapes[0].find("ZapB") == nullptr);  // zap sliders are never morphs
    REQUIRE(tri.shapes[0].uv.size() == 1);
    CHECK(tri.shapes[0].uv[0].name == "Slide");
    CHECK(tri.shapes[0].uv[0].offsets[0].x == Catch::Approx(0.5f).margin(1e-4));
    // One BODYTRI, relative to meshes, on the first shape of the project, in both files.
    CHECK(body_tri_of(build.big, "A") == "t\\t.tri");
    CHECK(body_tri_of(build.small, "A") == "t\\t.tri");
    CHECK(body_tri_of(build.big, "B").empty());
  }
  SECTION("zapped vertices leave the TRI and the later indices shift down") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data({1}), {{"ZapB", 100.0f, 100.0f}}, true);
    REQUIRE(build.ok());
    const auto tri = sns::body::parse_tri(build.tri, "t.tri");
    const auto* b = tri.shape("B");
    REQUIRE(b);
    const auto* lift = b->find("Lift");
    REQUIRE(lift);
    REQUIRE(lift->offsets.size() == 1);
    CHECK(lift->offsets[0].index == 1);  // vertex 2 became vertex 1 once vertex 1 was deleted
    CHECK(tri.shape("A")->find("Lift")->offsets[0].index == 2);
  }
  SECTION("an existing BODYTRI on the first shape is rewritten, not doubled") {
    const auto build = sns::body::build_slider_set(project(), source_nif(), data(), {}, true);
    REQUIRE(build.ok());
    const auto again = sns::body::build_slider_set(project(), build.big, data(), {}, true);
    REQUIRE(again.ok());
    std::size_t count = 0;
    for (auto at = again.big.find("BODYTRI"); at != std::string::npos; at = again.big.find("BODYTRI", at + 1)) ++count;
    CHECK(count == 1);
  }
  SECTION("PreventMorphFile: no TRI and no BODYTRI") {
    const auto p = project({}, R"(PreventMorphFile="true")");
    CHECK(p.prevent_morph_file);
    const auto build = sns::body::build_slider_set(p, source_nif(), data(), {}, true);
    REQUIRE(build.ok());
    CHECK(build.tri.empty());
    CHECK(build.big.find("BODYTRI") == std::string::npos);
  }
}

TEST_CASE("Penetration: garment vertices under the body surface are counted with their depth, those outside are not", "[bodyslide][fit]") {
  sns::nif::Shape body;  // a 10 x 10 floor at z = 0 whose winding normal points to +Z (outside)
  body.positions = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  body.indices = {0, 1, 2, 0, 2, 3};
  sns::nif::Shape garment;
  garment.positions = {{5, 5, 0.5f}, {2, 2, -0.4f}, {8, 3, -0.05f}, {5, 5, 9.0f}};
  const auto r = sns::body::measure_penetration(body, garment, 3.0f, 0.1f);
  CHECK(r.garment_vertices == 4);
  CHECK(r.near_body == 3);  // the vertex 9 units away is out of reach
  CHECK(r.inside == 1);     // -0.05 is within tolerance
  CHECK(r.max_depth == Catch::Approx(0.4f));
  CHECK(r.mean_gap == Catch::Approx((0.5f - 0.4f - 0.05f) / 3.0f));
}

TEST_CASE("A batch builds only the preset's body family and names what it leaves out", "[bodyslide][batch]") {
  const auto set = [](const std::string& name, const std::vector<std::string>& sliders) {
    sns::body::SliderProject p;
    p.name = name;
    p.output_path = "meshes\\!ube\\set";
    p.output_file = "m";
    for (const auto& s : sliders) p.sliders.push_back({s});
    return p;
  };
  const std::vector<sns::body::SliderProject> projects{set("Boots UBE", {"Calf"}), set("Body UBE", {"Calf", "Hips", "Waist", "Chest"}),
                                                       set("Top 3BA", {"Breasts3BA", "Waist3BA", "Calf"}), set("Ring", {"Size"})};
  const std::vector<SliderSetting> preset{{"Calf", 10.0f, 20.0f}, {"Hips", 0.0f, 30.0f}, {"Waist", 5.0f, 5.0f}, {"Chest", 0.0f, 0.0f}};
  const auto* reference = sns::body::family_reference_project(projects, preset);
  REQUIRE(reference);
  CHECK(reference->name == "Body UBE");
  CHECK(sns::body::decide_batch_set(projects[0], *reference, preset).action == "build");  // small, but in the family's folder
  CHECK(sns::body::decide_batch_set(projects[1], *reference, preset).action == "build");
  CHECK(sns::body::decide_batch_set(projects[2], *reference, preset).action == "other-family");  // 1 of 3 sliders is the family's
  CHECK(sns::body::decide_batch_set(projects[3], *reference, preset).action == "other-family");
  // Of the family, but the preset gives none of its sliders a value: the mesh it has stays.
  auto chest = set("Chest piece", {"Chest"});
  chest.output_path = "meshes\\!ube\\armor";
  CHECK(sns::body::decide_batch_set(chest, *reference, preset).action == "untouched");
  // Same sliders, small set, another folder: its vocabulary proves nothing (gloves of "Lady Ritual CBBE 3BA" sharing hand sliders).
  auto gloves = set("Gloves CBBE", {"Calf"});
  gloves.output_path = "meshes\\armor\\lady";
  CHECK(sns::body::decide_batch_set(gloves, *reference, preset).action == "ambiguous");
  // A large set that shares the vocabulary is proof enough wherever it lives.
  auto big = set("Follower outfit", {});
  big.output_path = "meshes\\follower";
  for (int i = 0; i < 40; ++i) big.sliders.push_back({"Calf"});
  CHECK(sns::body::decide_batch_set(big, *reference, preset).action == "build");
  CHECK(sns::body::family_reference_project(projects, {}) == nullptr);
}
