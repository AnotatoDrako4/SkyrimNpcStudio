// FaceGen regeneration on synthetic assets: TRI parsing, the Creation Kit morph rules, NIF
// structure, preserve-existing mode, FaceTint composition and the record/FaceGen audit.
#include <NifFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/face/face_tint.hpp"
#include "sns/face/facegen.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/textures/dds.hpp"

using namespace sns;
using namespace sns::face;
using Catch::Matchers::WithinAbs;

namespace {

using Vec = std::array<float, 3>;

void put_i32(std::string& out, std::int32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void put_f32(std::string& out, float v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void put_label(std::string& out, const std::string& s) {
  put_i32(out, static_cast<std::int32_t>(s.size() + 1));
  out.append(s);
  out.push_back('\0');
}

struct StatMorph {
  std::string name;
  std::vector<std::pair<std::int32_t, Vec>> targets;  // vertex index, absolute target position
};

// FRTRI003 writer: difference morphs quantized with `scale`, optional statistical morphs.
std::string tri_bytes(const std::vector<Vec>& base, const std::vector<std::pair<std::string, std::vector<Vec>>>& diffs,
                      const std::vector<StatMorph>& stats = {}, float scale = 0.0001f) {
  std::int32_t stat_vertices = 0;
  for (const auto& s : stats) stat_vertices += static_cast<std::int32_t>(s.targets.size());
  std::string out = "FRTRI003";
  for (const std::int32_t v : {static_cast<std::int32_t>(base.size()), 0, 0, 0, 0, 0, 0,
                               static_cast<std::int32_t>(diffs.size()), static_cast<std::int32_t>(stats.size()), stat_vertices}) {
    put_i32(out, v);
  }
  out.append(16, '\0');
  for (const auto& p : base) for (const float c : p) put_f32(out, c);
  for (const auto& s : stats) for (const auto& [i, p] : s.targets) for (const float c : p) put_f32(out, c);
  for (const auto& [name, deltas] : diffs) {
    put_label(out, name);
    put_f32(out, scale);
    for (const auto& d : deltas) {
      for (const float c : d) {
        const auto q = static_cast<std::int16_t>(std::lround(c / scale));
        out.append(reinterpret_cast<const char*>(&q), 2);
      }
    }
  }
  for (const auto& s : stats) {
    put_label(out, s.name);
    put_i32(out, static_cast<std::int32_t>(s.targets.size()));
    for (const auto& [i, p] : s.targets) put_i32(out, i);
  }
  return out;
}

const std::vector<Vec> kQuad{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};

std::vector<Vec> delta_on(std::size_t vertex, Vec d) {
  std::vector<Vec> out(kQuad.size(), Vec{0, 0, 0});
  out[vertex] = d;
  return out;
}

// One-shape SSE NIF (a quad) with a lighting shader of the given type.
std::string quad_nif(const std::string& shape_name, std::uint32_t shader_type, const std::string& diffuse = {}) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> v;
  for (const auto& p : kQuad) v.emplace_back(p[0], p[1], p[2]);
  std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}};
  std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
  auto* shape = file.CreateShapeFromData(shape_name, &v, &t, &uv, &n);
  file.GetShader(shape)->SetShaderType(shader_type);
  if (!diffuse.empty()) {
    auto d = diffuse;
    file.SetTextureSlot(shape, d, 0);
  }
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

std::vector<nifly::Vector3> shape_vertices(const std::string& nif_bytes, const std::string& shape_name) {
  std::istringstream in(nif_bytes, std::ios::binary);
  nifly::NifFile nif;
  REQUIRE(nif.Load(in) == 0);
  std::vector<nifly::Vector3> v;
  for (auto* s : nif.GetShapes()) {
    if (s->name.get() == shape_name) nif.GetVertsForShape(s, v);
  }
  INFO("shape " << shape_name);
  CHECK_FALSE(v.empty());
  return v;
}

struct Assets {
  std::map<std::string, std::string> files;
  void put(const std::string& path, std::string bytes) { files[text::data_path_key(path)] = std::move(bytes); }
  AssetReader reader() const {
    return [this](std::string_view p) -> std::optional<std::string> {
      const auto it = files.find(text::data_path_key(p));
      return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
  }
};

// Head (FaceTint shader) with race + chargen + expression TRIs, and a hair part (HairTint shader).
Assets synthetic_assets() {
  Assets a;
  a.put("meshes\\actors\\character\\character assets\\femalehead.nif", quad_nif("FemaleHead", 4, "textures\\head.dds"));
  a.put("meshes\\actors\\character\\character assets\\femaleheadraces.tri",
        tri_bytes(kQuad, {{"BretonRace", delta_on(0, {5, 5, 5})}, {"NordRace", delta_on(0, {1, 0, 0})}}));
  a.put("meshes\\actors\\character\\character assets\\femaleheadchargen.tri",
        tri_bytes(kQuad, {{"NoseLong", delta_on(1, {0, 0, 2})},
                          {"NoseShort", delta_on(1, {0, 0, -3})},
                          {"JawUp", delta_on(2, {0, 1, 0})},
                          {"NoseType16", delta_on(3, {0.5f, 0, 0})},
                          {"VampireMorph", delta_on(3, {0, 0, 1})}}));
  a.put("meshes\\actors\\character\\character assets\\femalehead.tri",
        tri_bytes(kQuad, {{"Aah", delta_on(0, {9, 9, 9})}, {"SkinnyMorph", delta_on(2, {-1, 0, 0})}}));
  a.put("meshes\\actors\\character\\character assets\\hair\\hair02.nif", quad_nif("Hair02", 6, "textures\\hair.dds"));
  return a;
}

FaceRecipe synthetic_recipe() {
  FaceRecipe r;
  r.npc_form_key = "0A2C8E:Skyrim.esm";
  r.facegen_mesh_path = "meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\000A2C8E.nif";
  r.facegen_tint_path = "textures\\actors\\character\\facegendata\\facetint\\Skyrim.esm\\000A2C8E.dds";
  r.female = true;
  r.race_editor_id = "NordRace";
  HeadPartData head;
  head.form_key = "051623:Skyrim.esm";
  head.editor_id = "FemaleHeadNord";
  head.type = HeadPartType::Face;
  head.model = "meshes\\actors\\character\\character assets\\femalehead.nif";
  head.tris = {{TriKind::Race, "meshes\\actors\\character\\character assets\\femaleheadraces.tri"},
               {TriKind::Expression, "meshes\\actors\\character\\character assets\\femalehead.tri"},
               {TriKind::Chargen, "meshes\\actors\\character\\character assets\\femaleheadchargen.tri"}};
  HeadPartData hair;
  hair.form_key = "051193:Skyrim.esm";
  hair.editor_id = "HairFemaleNord02";
  hair.type = HeadPartType::Hair;
  hair.model = "meshes\\actors\\character\\character assets\\hair\\hair02.nif";
  HeadPartData scar;
  scar.editor_id = "MarksFemaleHumanoid00NoGash";
  scar.type = HeadPartType::Scars;
  r.parts = {head, hair, scar};
  r.values.weight = 100;
  return r;
}

}  // namespace

TEST_CASE("TRI parser reads difference and statistical morphs", "[face][tri]") {
  const auto bytes = tri_bytes(kQuad,
                               {{"NoseLong", delta_on(1, {0, 0, 2})}, {"NoseType9", delta_on(2, {1, 0, 0})},
                                {"NoseType9", delta_on(3, {7, 0, 0})}},
                               {{"Stat", {{1, Vec{10, 1, 0}}}}});
  const auto tri = parse_tri(bytes, "test.tri");
  CHECK(tri.vertex_count == 4);
  REQUIRE(tri.morphs.size() == 4);
  REQUIRE(tri.find("noselong") != nullptr);  // names are case-insensitive
  CHECK_THAT(tri.find("NoseLong")->deltas[1][2], WithinAbs(2.0, 1e-3));
  // A repeated name resolves to the first occurrence, like the Creation Kit.
  CHECK_THAT(tri.find("NoseType9")->deltas[2][0], WithinAbs(1.0, 1e-3));
  CHECK_THAT(tri.find("NoseType9")->deltas[3][0], WithinAbs(0.0, 1e-6));
  // Statistical morph: target minus base for the listed vertex only.
  CHECK_THAT(tri.find("Stat")->deltas[1][1], WithinAbs(1.0, 1e-6));
  CHECK_THAT(tri.find("Stat")->deltas[0][0], WithinAbs(0.0, 1e-6));
  CHECK(tri.find("Missing") == nullptr);

  CHECK_THROWS_AS(parse_tri(bytes.substr(0, bytes.size() - 3), "cut.tri"), sns::Error);
  CHECK_THROWS_AS(parse_tri("FRTRI002" + bytes.substr(8), "old.tri"), sns::Error);
}

TEST_CASE("Morph plan follows the measured Creation Kit rules", "[face]") {
  auto recipe = synthetic_recipe();
  recipe.morph_race_editor_id = "BretonRace";  // morph race wins over the race
  recipe.vampire = true;
  recipe.values.sliders[0] = 0.3f;   // NoseLongVsShort > 0 -> NoseLong
  recipe.values.sliders[2] = -0.5f;  // JawUpVsDown < 0 -> JawUp (inverted pair)
  recipe.values.sliders[15] = 0.2f;  // ChinUpVsDown > 0 -> ChinMoveDown (inverted pair)
  recipe.values.presets = {16, -1, 9, 0};
  recipe.values.weight = 25;
  const auto plan = plan_morphs(recipe);
  CHECK(plan.race_morph == "BretonRace");
  auto weight_of = [&](const std::vector<MorphWeight>& list, const std::string& name) {
    for (const auto& w : list) {
      if (w.name == name) return w.weight;
    }
    return -99.0f;
  };
  CHECK(weight_of(plan.chargen, "NoseLong") == 0.3f);
  CHECK(weight_of(plan.chargen, "JawUp") == 0.5f);
  CHECK(weight_of(plan.chargen, "ChinMoveDown") == 0.2f);
  CHECK(weight_of(plan.chargen, "NoseType16") == 1.0f);
  CHECK(weight_of(plan.chargen, "EyesType9") == 1.0f);
  CHECK(weight_of(plan.chargen, "LipType0") == -99.0f);  // 0 selects nothing
  CHECK(weight_of(plan.chargen, "VampireMorph") == 1.0f);
  CHECK_THAT(weight_of(plan.expression, "SkinnyMorph"), WithinAbs(0.75, 1e-6));

  recipe.values.weight = 100;
  CHECK(plan_morphs(recipe).expression.empty());
}

TEST_CASE("FaceGen build regenerates morphed head parts in a vanilla-shaped NIF", "[face]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  auto recipe = synthetic_recipe();
  recipe.values.sliders[0] = 0.5f;  // NoseLong 0.5 -> vertex 1 z += 1
  recipe.values.presets = {16, -1, 0, 0};
  recipe.values.weight = 40;        // SkinnyMorph 0.6 -> vertex 2 x -= 0.6
  recipe.values.hair_color = Rgb8{51, 102, 204};
  recipe.parts[1].texture_set = std::array<std::string, 9>{"textures\\hair\\red.dds", "textures\\hair\\red_n.dds"};

  const auto build = build_facegen(recipe, cache);
  INFO(build.issues.size() << " issues; first: " << (build.issues.empty() ? "" : build.issues.front().message));
  REQUIRE(build.exportable());
  REQUIRE(build.parts.size() == 3);
  CHECK(build.parts[0].status == "built");
  CHECK(build.parts[0].geometry_source == "regenerated");
  CHECK(build.parts[2].status == "no-model");

  const auto info = inspect_facegen(build.nif);
  REQUIRE(info.readable);
  CHECK(info.root_type == "BSFadeNode");
  CHECK(info.root_name == "000A2C8E.NIF");
  CHECK(info.has_facegen_node);
  REQUIRE(info.shapes.size() == 2);
  CHECK(info.shapes[0].first == "FemaleHeadNord");
  CHECK(info.shapes[1].first == "HairFemaleNord02");
  CHECK(text::iequals_ascii(info.face_tint_slot, recipe.facegen_tint_path));

  const auto head = shape_vertices(build.nif, "FemaleHeadNord");
  REQUIRE(head.size() == 4);
  CHECK_THAT(head[0].x, WithinAbs(1.0, 1e-3));    // NordRace (race morph), BretonRace untouched
  CHECK_THAT(head[1].z, WithinAbs(1.0, 1e-3));    // NoseLong 0.5 * 2
  CHECK_THAT(head[2].x, WithinAbs(10 - 0.6, 1e-3));  // SkinnyMorph 0.6 * -1
  CHECK_THAT(head[3].x, WithinAbs(0.5, 1e-3));    // NoseType16
  CHECK_THAT(head[3].z, WithinAbs(0.0, 1e-6));    // not a vampire

  // The preview renderer reads the very same bytes.
  const auto mesh = sns::nif::load_mesh(build.nif, recipe.facegen_mesh_path);
  REQUIRE(mesh.shapes.size() == 2);
  CHECK(mesh.shapes[1].shader_type == nif::kShaderHairTint);
  CHECK(text::iequals_ascii(mesh.shapes[1].textures[0], "textures\\hair\\red.dds"));
  CHECK(text::iequals_ascii(mesh.shapes[0].textures[6], recipe.facegen_tint_path));

  std::istringstream in(build.nif, std::ios::binary);
  nifly::NifFile nif;
  REQUIRE(nif.Load(in) == 0);
  for (auto* shape : nif.GetShapes()) {
    auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(nif.GetShader(shape));
    REQUIRE(lighting != nullptr);
    if (shape->name.get() == "HairFemaleNord02") {
      CHECK_THAT(lighting->hairTintColor.x, WithinAbs(0.4, 1e-3));  // 2 x color, as the Creation Kit writes it
      CHECK_THAT(lighting->hairTintColor.z, WithinAbs(1.6, 1e-3));
    }
  }

  // Determinism: the same recipe gives byte-identical output (preview == export).
  CHECK(build_facegen(recipe, cache).nif == build.nif);
}

TEST_CASE("FaceGen build reports missing assets and refuses to export", "[face]") {
  auto assets = synthetic_assets();
  assets.files.erase(text::data_path_key("meshes\\actors\\character\\character assets\\femaleheadchargen.tri"));
  assets.files.erase(text::data_path_key("meshes\\actors\\character\\character assets\\hair\\hair02.nif"));
  FaceAssetCache cache(assets.reader());
  const auto build = build_facegen(synthetic_recipe(), cache);
  CHECK_FALSE(build.exportable());
  auto has = [&](const char* code) {
    return std::any_of(build.issues.begin(), build.issues.end(), [&](const auto& i) { return i.code == code; });
  };
  CHECK(has("TriMissing"));
  CHECK(has("HeadPartMeshMissing"));

  auto mismatch = synthetic_assets();
  mismatch.put("meshes\\actors\\character\\character assets\\femaleheadchargen.tri",
               tri_bytes({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, {{"NoseLong", {Vec{0, 0, 1}, Vec{}, Vec{}}}}));
  FaceAssetCache cache2(mismatch.reader());
  // A slider needs the chargen TRI, whose vertex count does not match the mesh: blocked.
  auto slid = synthetic_recipe();
  slid.values.sliders[0] = 0.5f;
  const auto build2 = build_facegen(slid, cache2);
  CHECK_FALSE(build2.exportable());
  CHECK(std::any_of(build2.issues.begin(), build2.issues.end(), [](const auto& i) { return i.code == "TriVertexMismatch"; }));
  // No slider set: that TRI contributes nothing (as in the Creation Kit), so it is only noted.
  const auto build3 = build_facegen(synthetic_recipe(), cache2);
  CHECK(build3.exportable());
  CHECK(std::any_of(build3.issues.begin(), build3.issues.end(), [](const auto& i) { return i.code == "TriVertexMismatchUnused"; }));

  FaceRecipe empty;
  CHECK_FALSE(build_facegen(empty, cache).exportable());
}

TEST_CASE("PreserveExisting keeps sculpting the record cannot express and applies only the edit", "[face]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());

  // "Existing" FaceGen: what RaceMenu exported, i.e. the record values plus a sculpt the record lacks.
  auto recipe = synthetic_recipe();
  recipe.values.sliders[0] = 0.2f;
  auto sculpted = recipe;
  sculpted.values.sculpt = {{"Actors\\Character\\Character Assets\\FemaleHeadCharGen.tri", 4, {{2, Vec{0, 0, 3}}}}};
  const auto existing = build_facegen(sculpted, cache);
  REQUIRE(existing.exportable());

  // Edit: the nose slider goes from 0.2 to -0.4 (NoseLong 0.2 -> NoseShort 0.4).
  auto edited = recipe;
  edited.values.sliders[0] = -0.4f;
  FaceGenBuildOptions options;
  options.mode = GeometryMode::PreserveExisting;
  options.existing_facegen = std::make_shared<const std::string>(existing.nif);
  options.baseline = recipe.values;
  const auto preserved = build_facegen(edited, cache, options);
  REQUIRE(preserved.exportable());
  CHECK(preserved.parts[0].geometry_source == "preserved");

  // Expected: the edit regenerated WITH the sculpt.
  auto expected_recipe = edited;
  expected_recipe.values.sculpt = sculpted.values.sculpt;
  const auto expected = build_facegen(expected_recipe, cache);
  const auto diff = compare_facegen(preserved.nif, expected.nif);
  CHECK(diff.same_topology);
  CHECK(diff.max_distance < 1e-3f);

  // Regenerating instead would lose the sculpt (z of vertex 2).
  const auto plain = build_facegen(edited, cache);
  CHECK(compare_facegen(plain.nif, expected.nif).max_distance > 2.9f);
}

TEST_CASE("Hair color patch changes only the HairTint color of the built FaceGen", "[face][hair]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  auto recipe = synthetic_recipe();
  recipe.values.sliders[0] = 0.5f;
  recipe.values.hair_color = Rgb8{51, 102, 204};
  const auto built = build_facegen(recipe, cache);
  REQUIRE(built.exportable());

  auto recolored = recipe;
  recolored.values.hair_color = Rgb8{165, 58, 30};
  const auto patch = patch_hair_tint(built.nif, recolored);
  REQUIRE_FALSE(patch.nif.empty());
  CHECK(patch.issues.empty());
  REQUIRE(patch.patched == std::vector<std::string>{"HairFemaleNord02"});
  const auto info = inspect_facegen(patch.nif);
  REQUIRE(info.hair_tints.size() == 1);
  CHECK_THAT(info.hair_tints[0].second[0], WithinAbs(2.0 * 165 / 255.0, 1e-4));
  CHECK_THAT(info.hair_tints[0].second[2], WithinAbs(2.0 * 30 / 255.0, 1e-4));
  // Geometry untouched, and the result is what a full regeneration with the new color writes.
  const auto same = compare_facegen(patch.nif, built.nif);
  CHECK(same.same_topology);
  CHECK(same.max_distance == 0.0f);
  const auto integrity = verify_hair_tint_integrity(built.nif, patch.nif);
  CHECK(integrity.readable);
  CHECK(integrity.unchanged_except_hair_tint);
  CHECK(integrity.differences.empty());
  CHECK(patch.nif == build_facegen(recolored, cache).nif);

  // Every selection starts from the original winner. Twenty consecutive choices therefore keep
  // the exact same topology and complete NIF graph without accumulating serialization changes.
  for (int i = 0; i < 20; ++i) {
    auto choice = recipe;
    choice.values.hair_color = Rgb8{static_cast<std::uint8_t>(i * 11), static_cast<std::uint8_t>(255 - i * 7),
                                    static_cast<std::uint8_t>(i * 5)};
    const auto selected = patch_hair_tint(built.nif, choice);
    INFO("hair color selection " << i);
    REQUIRE_FALSE(selected.nif.empty());
    CHECK(verify_hair_tint_integrity(built.nif, selected.nif).unchanged_except_hair_tint);
    CHECK(compare_facegen(built.nif, selected.nif).max_distance == 0.0f);
  }

  // A semantically different head can never pass as a legal color-only patch.
  auto reshaped = recipe;
  reshaped.values.sliders[0] = -0.5f;
  const auto changed = build_facegen(reshaped, cache);
  const auto rejected = verify_hair_tint_integrity(built.nif, changed.nif);
  CHECK(rejected.readable);
  CHECK_FALSE(rejected.unchanged_except_hair_tint);
  CHECK_FALSE(rejected.differences.empty());

  // No hair color: black, as the Creation Kit writes it.
  auto none = recipe;
  none.values.hair_color.reset();
  const auto black = inspect_facegen(patch_hair_tint(built.nif, none).nif);
  REQUIRE(black.hair_tints.size() == 1);
  CHECK(black.hair_tints[0].second == std::array<float, 3>{0, 0, 0});

  // Leftover bytes after the footer (some mod FaceGen files are rewritten without truncation; the
  // game ignores them): the color is still found by reading the header forward, only the tint
  // bytes change and the leftover bytes are kept as they were.
  {
    auto padded = built.nif;
    padded += std::string(31260, '\x5A');
    const auto padded_patch = patch_hair_tint(padded, recolored);
    REQUIRE_FALSE(padded_patch.nif.empty());
    CHECK(padded_patch.issues.empty());
    CHECK(padded_patch.nif.size() == padded.size());
    CHECK(padded_patch.nif.substr(built.nif.size()) == padded.substr(built.nif.size()));
    CHECK(padded_patch.nif.substr(0, built.nif.size()) == patch.nif);
    CHECK(verify_hair_tint_integrity(padded, padded_patch.nif).unchanged_except_hair_tint);
  }

  // Not a NIF: reported, never a crash.
  const auto broken = patch_hair_tint("not a nif", recolored);
  CHECK(broken.nif.empty());
  REQUIRE(broken.issues.size() == 1);
  CHECK(broken.issues[0].code == "FaceGenUnreadable");
}

TEST_CASE("Hair parts without a HairTint shader are reported instead of colored", "[face][hair]") {
  auto assets = synthetic_assets();
  assets.put("meshes\\actors\\character\\character assets\\hair\\hair02.nif", quad_nif("Hair02", 0, "textures\\hair.dds"));
  FaceAssetCache cache(assets.reader());
  auto recipe = synthetic_recipe();
  recipe.values.hair_color = Rgb8{10, 20, 30};
  const auto build = build_facegen(recipe, cache);
  auto count = [](const std::vector<FaceIssue>& issues, const char* code) {
    return std::count_if(issues.begin(), issues.end(), [&](const auto& i) { return i.code == code; });
  };
  CHECK(count(build.issues, "HairTintUnsupported") == 1);
  CHECK(build.exportable());  // a warning: the record still gets the color, the piece just ignores it
  // Hair Color Safe Mode on that FaceGen: nothing could take the color, so nothing is produced and
  // nothing is claimed; the shader is not converted.
  const auto patch = patch_hair_tint(build.nif, recipe);
  CHECK(patch.patched.empty());
  CHECK(patch.nif.empty());
  CHECK(count(patch.issues, "HairTintUnsupported") == 1);
  CHECK(count(patch.issues, "HAIR_TINT_NOT_APPLIED") == 1);
}

TEST_CASE("Hair Color Safe Mode recolors a sculpted FaceGen byte for byte", "[face][hair]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  // The profile's FaceGen was sculpted: it is not what the record's values produce.
  auto sculpted = synthetic_recipe();
  sculpted.values.sliders[0] = 0.8f;
  sculpted.values.sliders[2] = -0.6f;
  sculpted.values.hair_color = Rgb8{40, 30, 20};
  const auto original = build_facegen(sculpted, cache).nif;
  REQUIRE_FALSE(original.empty());
  auto record = synthetic_recipe();  // record values: no slider set
  record.values.hair_color = Rgb8{40, 30, 20};
  REQUIRE(compare_facegen(build_facegen(record, cache).nif, original).max_distance > 0.0f);  // regenerating would lose it

  auto edited = record;
  edited.values.hair_color = Rgb8{200, 10, 90};
  const auto patch = patch_hair_tint(original, edited);
  REQUIRE_FALSE(patch.nif.empty());
  CHECK(patch.patched == std::vector<std::string>{"HairFemaleNord02"});
  REQUIRE(patch.nif.size() == original.size());
  std::size_t differing = 0;
  for (std::size_t i = 0; i < original.size(); ++i) differing += original[i] != patch.nif[i];
  CHECK(differing > 0);
  CHECK(differing <= 12);  // one HairTint shape: at most its three floats
  const auto integrity = verify_hair_tint_integrity(original, patch.nif);
  CHECK(integrity.byte_identical_outside_hair_tint);
  CHECK(integrity.unchanged_except_hair_tint);
  CHECK(compare_facegen(original, patch.nif).max_distance == 0.0f);  // the sculpt is kept
  const auto info = inspect_facegen(patch.nif);
  REQUIRE(info.hair_tints.size() == 1);
  CHECK_THAT(info.hair_tints[0].second[0], WithinAbs(2.0 * 200 / 255.0, 1e-6));
}

TEST_CASE("FaceGen Geometry Integrity Guard names the piece, property and values that changed", "[face][hair]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  const auto recipe = synthetic_recipe();
  const auto original = build_facegen(recipe, cache).nif;
  REQUIRE_FALSE(original.empty());
  auto edit = [&](const std::function<void(nifly::NifFile&)>& change) {
    std::istringstream in(original, std::ios::binary);
    nifly::NifFile nif;
    REQUIRE(nif.Load(in) == 0);
    change(nif);
    std::ostringstream out(std::ios::binary);
    REQUIRE(nif.Save(out) == 0);
    return out.str();
  };
  auto shape = [](nifly::NifFile& nif, const char* name) {
    nifly::NiShape* found = nullptr;
    for (auto* s : nif.GetShapes()) {
      if (s->name.get() == name) found = s;
    }
    INFO("shape " << name);
    REQUIRE(found);
    return found;
  };
  auto find = [](const FaceGenIntegrity& r, const std::string& shape_name, const std::string& property) {
    INFO(r.summary());
    const auto it = std::find_if(r.differences.begin(), r.differences.end(),
                                 [&](const FaceGenDifference& d) { return d.shape == shape_name && d.property == property; });
    REQUIRE(it != r.differences.end());
    return *it;
  };

  // Re-serialized without any change: semantically identical, allowed.
  const auto same = verify_hair_tint_integrity(original, edit([](nifly::NifFile&) {}));
  CHECK(same.unchanged_except_hair_tint);

  // Moved vertices (a regeneration with another slider).
  auto reshaped = recipe;
  reshaped.values.sliders[0] = 0.5f;
  const auto moved = verify_hair_tint_integrity(original, build_facegen(reshaped, cache).nif);
  CHECK_FALSE(moved.unchanged_except_hair_tint);
  CHECK(find(moved, "FemaleHeadNord", "positions").after.find("vertices moved") != std::string::npos);

  // Another texture on the hair.
  const auto retextured = verify_hair_tint_integrity(original, edit([&](nifly::NifFile& nif) {
    std::string path = "textures\\other.dds";
    nif.SetTextureSlot(shape(nif, "HairFemaleNord02"), path, 0);
  }));
  const auto texture = find(retextured, "HairFemaleNord02", "texture slot 0");
  CHECK(texture.before == "textures\\hair.dds");
  CHECK(texture.after == "textures\\other.dds");

  // Same vertices, other triangles (a different topology on the same points).
  const auto rewired = verify_hair_tint_integrity(original, edit([&](nifly::NifFile& nif) {
    auto* s = shape(nif, "FemaleHeadNord");
    std::vector<nifly::Triangle> tris;
    s->GetTriangles(tris);
    REQUIRE_FALSE(tris.empty());
    std::swap(tris[0].p2, tris[0].p3);
    s->SetTriangles(tris);
  }));
  CHECK(find(rewired, "FemaleHeadNord", "indices").after == "1 triangles with other vertex indices");

  // Other UV coordinates.
  const auto remapped = verify_hair_tint_integrity(original, edit([&](nifly::NifFile& nif) {
    auto* s = shape(nif, "FemaleHeadNord");
    std::vector<nifly::Vector2> uv;
    nif.GetUvsForShape(s, uv);
    REQUIRE_FALSE(uv.empty());
    uv[0].u += 0.25f;
    nif.SetUvsForShape(s, uv);
  }));
  find(remapped, "FemaleHeadNord", "uv");

  // A head part removed.
  const auto removed = verify_hair_tint_integrity(original, edit([&](nifly::NifFile& nif) { nif.DeleteShape(shape(nif, "HairFemaleNord02")); }));
  CHECK(find(removed, "", "shape count").before == "2");
  CHECK(find(removed, "HairFemaleNord02", "shape").after == "missing");

  // Not a NIF.
  const auto unreadable = verify_hair_tint_integrity(original, "garbage");
  CHECK_FALSE(unreadable.readable);
  CHECK_FALSE(unreadable.unchanged_except_hair_tint);
}

TEST_CASE("Face compatibility is diagnosed from the FaceGen, meshes and TRI files, not from names", "[face][compat]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  const auto recipe = synthetic_recipe();
  const auto regenerated = build_facegen(recipe, cache);
  REQUIRE(regenerated.exportable());
  auto allowed = [](const FaceCompatibilityReport& r, const std::string& op) {
    return std::find(r.allowed.begin(), r.allowed.end(), op) != r.allowed.end();
  };
  auto blocked = [](const FaceCompatibilityReport& r, const std::string& op) {
    return std::any_of(r.blocked.begin(), r.blocked.end(), [&](const auto& b) { return b.first == op && !b.second.empty(); });
  };

  // The profile's FaceGen is what the record produces.
  const auto vanilla = diagnose_face_compatibility(recipe, cache, &regenerated.nif, regenerated);
  CHECK(vanilla.kind == FaceCompatibility::VanillaCompatible);
  CHECK(vanilla.code == "vanilla-compatible");
  for (const char* op : {"hair-color", "outfit", "face-morphs", "head-parts", "regenerate"}) CHECK(allowed(vanilla, op));

  // Sculpted FaceGen, TRI files fit: edits as differences, never a regeneration.
  auto sculpt = recipe;
  sculpt.values.sliders[0] = 0.9f;
  const auto sculpted = build_facegen(sculpt, cache).nif;
  const auto limited = diagnose_face_compatibility(recipe, cache, &sculpted, regenerated);
  CHECK(limited.kind == FaceCompatibility::CustomLimited);
  CHECK(limited.regeneration_deviation > 0.0f);
  CHECK(allowed(limited, "hair-color"));
  CHECK(allowed(limited, "face-morphs"));
  CHECK(blocked(limited, "regenerate"));

  // A replacer head whose TRI files do not fit its mesh (the Serana case: 3832-vertex head, 996-vertex TRIs).
  auto broken = synthetic_assets();
  for (const char* tri : {"femaleheadraces.tri", "femaleheadchargen.tri"}) {
    broken.put(std::string("meshes\\actors\\character\\character assets\\") + tri,
               tri_bytes({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, {{"NoseLong", {Vec{0, 0, 1}, Vec{}, Vec{}}}}));
  }
  FaceAssetCache broken_cache(broken.reader());
  const auto failed_regeneration = build_facegen(recipe, broken_cache);
  const auto incompatible = diagnose_face_compatibility(recipe, broken_cache, &sculpted, failed_regeneration);
  CHECK(incompatible.kind == FaceCompatibility::CustomIncompatibleMorphs);
  REQUIRE(incompatible.tri_mismatches.size() == 2);
  CHECK(incompatible.tri_mismatches[0].part == "FemaleHeadNord");
  CHECK(incompatible.tri_mismatches[0].tri_vertices == 3);
  CHECK(incompatible.tri_mismatches[0].mesh_vertices == 4);
  CHECK(allowed(incompatible, "hair-color"));
  CHECK(blocked(incompatible, "face-morphs"));
  CHECK(blocked(incompatible, "regenerate"));

  // No FaceGen in the profile: nothing to recolor.
  const auto missing = diagnose_face_compatibility(recipe, cache, nullptr, regenerated);
  CHECK(missing.kind == FaceCompatibility::FaceGenMissing);
  CHECK(blocked(missing, "hair-color"));
  CHECK(allowed(missing, "regenerate"));

  // Unreadable FaceGen.
  const std::string garbage = "not a nif";
  const auto unreadable = diagnose_face_compatibility(recipe, cache, &garbage, regenerated);
  CHECK(unreadable.kind == FaceCompatibility::FaceGenInconsistent);
  CHECK(blocked(unreadable, "hair-color"));

  // Hair without HairTint: the color cannot reach it.
  auto plain = synthetic_assets();
  plain.put("meshes\\actors\\character\\character assets\\hair\\hair02.nif", quad_nif("Hair02", 0, "textures\\hair.dds"));
  FaceAssetCache plain_cache(plain.reader());
  const auto plain_facegen = build_facegen(recipe, plain_cache);
  const auto untinted = diagnose_face_compatibility(recipe, plain_cache, &plain_facegen.nif, plain_facegen);
  CHECK_FALSE(untinted.hair_tint);
  CHECK(blocked(untinted, "hair-color"));
}

TEST_CASE("A head whose TRI files do not match blocks only edits that need those morphs", "[face]") {
  // A replacer head: 4-vertex mesh whose record still names 3-vertex TRI files.
  auto assets = synthetic_assets();
  for (const char* tri : {"femaleheadraces.tri", "femaleheadchargen.tri", "femalehead.tri"}) {
    assets.put(std::string("meshes\\actors\\character\\character assets\\") + tri,
               tri_bytes({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, {{"NoseLong", {Vec{0, 0, 1}, Vec{}, Vec{}}}}));
  }
  FaceAssetCache cache(assets.reader());
  auto recipe = synthetic_recipe();
  FaceGenBuildOptions options;
  options.mode = GeometryMode::PreserveExisting;
  options.existing_facegen = std::make_shared<const std::string>(
      [&] {
        // The exported head the profile ships (built here without the broken TRIs).
        auto good = synthetic_assets();
        FaceAssetCache good_cache(good.reader());
        return build_facegen(recipe, good_cache).nif;
      }());
  options.baseline = recipe.values;

  // Hair color only: nothing to morph, the exported head is kept and the result is exportable.
  auto recolored = recipe;
  recolored.values.hair_color = Rgb8{1, 2, 3};
  const auto color_only = build_facegen(recolored, cache, options);
  CHECK(color_only.exportable());
  CHECK(std::any_of(color_only.issues.begin(), color_only.issues.end(),
                    [](const auto& i) { return i.code == "TriVertexMismatchUnused" && i.severity == "info"; }));

  // A slider edit needs the chargen TRI: blocked with the reason.
  auto slid = recipe;
  slid.values.sliders[0] = 0.4f;
  const auto blocked = build_facegen(slid, cache, options);
  CHECK_FALSE(blocked.exportable());
  CHECK(std::any_of(blocked.issues.begin(), blocked.issues.end(), [](const auto& i) { return i.code == "TriVertexMismatch"; }));
}

TEST_CASE("A superseded FaceGen build stops cooperatively", "[face]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  FaceGenBuildOptions options;
  int checks = 0;
  options.cancelled = [&] { return ++checks > 1; };  // cancelled after the first head part
  const auto build = build_facegen(synthetic_recipe(), cache, options);
  CHECK(build.cancelled);
  CHECK(build.nif.empty());
  CHECK_FALSE(build.exportable());
  CHECK(build.parts.size() == 1);
}

TEST_CASE("FaceTint composition blends layers over the skin tone and encodes BC3 with mips", "[face][tint]") {
  auto make_mask = [](std::uint32_t size, bool left_half_only) {
    textures::DdsImage img;
    img.width = img.height = img.source_width = img.source_height = size;
    img.format = textures::TextureFormat::RGBA8;
    img.data.assign(static_cast<std::size_t>(size) * size * 4, '\0');
    for (std::uint32_t y = 0; y < size; ++y) {
      for (std::uint32_t x = 0; x < size; ++x) {
        const bool on = !left_half_only || x < size / 2;
        const auto o = (static_cast<std::size_t>(y) * size + x) * 4;
        for (int c = 0; c < 3; ++c) img.data[o + static_cast<std::size_t>(c)] = static_cast<char>(on ? 255 : 0);
        img.data[o + 3] = static_cast<char>(255);
      }
    }
    return textures::encode_dds(img, textures::TextureFormat::RGBA8, false);
  };
  Assets assets;
  assets.put("textures\\tintmasks\\skintone.dds", make_mask(64, false));
  assets.put("textures\\tintmasks\\lips.dds", make_mask(64, true));
  FaceAssetCache cache(assets.reader());

  FaceRecipe recipe;
  recipe.tint_masks = {{1, "textures\\tintmasks\\skintone.dds", kTintMaskSkinTone}, {2, "textures\\tintmasks\\lips.dds", 1}};
  recipe.values.skin_tone = Rgb8{200, 100, 50};
  recipe.values.tints = {{1, Rgb8{0, 0, 0}, 0.3f, 0},  // skin tone layer: QNAM at full strength, its own color ignored
                         {2, Rgb8{0, 0, 250}, 0.5f, 0},
                         {9, Rgb8{1, 2, 3}, 1.0f, 0}};  // no such mask in the race
  const auto tint = compose_face_tint(recipe, cache, 32);
  REQUIRE(tint.rgba.width == 32);
  auto px = [&](std::uint32_t x, std::uint32_t y, int c) {
    return static_cast<int>(static_cast<unsigned char>(tint.rgba.data[(static_cast<std::size_t>(y) * 32 + x) * 4 + static_cast<std::size_t>(c)]));
  };
  CHECK(px(28, 10, 0) == 200);  // right half: skin tone only
  CHECK(px(28, 10, 2) == 50);
  CHECK(px(2, 10, 0) == 100);   // left half: lerp(200, 0, 0.5)
  CHECK(px(2, 10, 2) == 150);   // lerp(50, 250, 0.5)
  CHECK(px(2, 10, 3) == 255);
  REQUIRE(tint.issues.size() == 1);
  CHECK(tint.issues[0].code == "TintMaskUnknown");
  CHECK(tint.layers.size() == 2);

  const auto full = compose_face_tint(recipe, cache);
  const auto dds = encode_face_tint(full.rgba);
  const auto parsed = textures::parse_dds(dds);
  CHECK(parsed.format == textures::TextureFormat::BC3);
  CHECK(parsed.width == 512);
  CHECK(parsed.mip_count == 10);
  const auto decoded = textures::decode_rgba(parsed);
  const auto right = (static_cast<std::size_t>(100) * 512 + 400) * 4;
  CHECK(std::abs(static_cast<int>(static_cast<unsigned char>(decoded.data[right])) - 200) <= 4);
}

TEST_CASE("FaceGen audit reports record/FaceGen mismatches (dark-face risk)", "[face]") {
  auto assets = synthetic_assets();
  FaceAssetCache cache(assets.reader());
  auto recipe = synthetic_recipe();
  const auto build = build_facegen(recipe, cache);
  CHECK(audit_facegen(inspect_facegen(build.nif), recipe).empty());

  auto changed = recipe;
  changed.parts[1].editor_id = "HairFemaleNord05";  // the record now names another hair
  const auto issues = audit_facegen(inspect_facegen(build.nif), changed);
  CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& i) { return i.code == "FaceGenPartMismatch"; }));
  CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& i) { return i.code == "FaceGenExtraShape"; }));
  CHECK_FALSE(inspect_facegen("not a nif").readable);
}
