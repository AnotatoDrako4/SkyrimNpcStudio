// NPC fidelity report (sns/rendering/fidelity.hpp): game-vs-viewer verdicts from synthetic scenes whose geometry has a known
// answer (spheres of known radius, half covers), the grid penetration measure against body::measure_penetration, and the
// per-triangle dismember partitions of a NIF written with nifly.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "sns/assets/asset_resolver.hpp"
#include "sns/body/bodyslide_build.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/rendering/fidelity.hpp"
#include "temp_dir.hpp"

namespace {

using sns::rendering::RenderPart;

struct Mesh {
  std::vector<float> positions, normals;  // xyz triples
  std::vector<std::uint32_t> indices;
};

// UV sphere centred at (cx, cy, cz); `y_min`/`y_max` keep only the band of latitudes inside (view-space Y is up).
Mesh sphere(float radius, int rings, int segments, float cx = 0, float cy = 0, float cz = 0, float y_min = -1e9f, float y_max = 1e9f) {
  Mesh m;
  for (int r = 0; r <= rings; ++r) {
    const float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
    for (int s = 0; s <= segments; ++s) {
      const float theta = 2.0f * 3.14159265f * static_cast<float>(s) / static_cast<float>(segments);
      // Exact poles: in float sin(pi) is -8.7e-8, which would leave near-degenerate pole triangles with flipped normals.
      const float sp = (r == 0 || r == rings) ? 0.0f : std::sin(phi);
      const float nx = sp * std::cos(theta), ny = r == 0 ? 1.0f : (r == rings ? -1.0f : std::cos(phi)), nz = sp * std::sin(theta);
      m.positions.insert(m.positions.end(), {cx + radius * nx, cy + radius * ny, cz + radius * nz});
      m.normals.insert(m.normals.end(), {nx, ny, nz});
    }
  }
  const auto id = [&](int r, int s) { return static_cast<std::uint32_t>(r * (segments + 1) + s); };
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      const std::uint32_t a = id(r, s), b = id(r + 1, s), c = id(r + 1, s + 1), d = id(r, s + 1);
      const float y = (m.positions[a * 3 + 1] + m.positions[c * 3 + 1]) * 0.5f;
      if (y < y_min || y > y_max) continue;
      // Counter-clockwise seen from outside: the winding normal points out, as measure_penetration expects.
      m.indices.insert(m.indices.end(), {a, c, b, a, d, c});
    }
  }
  return m;
}

RenderPart part_of(const Mesh& m, std::string id, std::string group, std::string armor, std::uint32_t slot_mask, std::uint32_t shader = 0) {
  RenderPart p;
  p.id = std::move(id);
  p.group = std::move(group);
  p.label = p.id;
  p.shape_name = p.id;
  p.armor_form_key = std::move(armor);
  p.addon_form_key = p.armor_form_key + "-AA";
  p.mesh_path = "meshes\\" + p.id + ".nif";
  p.mesh_location = "loose:Test";
  p.slot_mask = slot_mask;
  p.shader_type = shader;
  p.indices = m.indices;
  const std::size_t n = m.positions.size() / 3;
  p.vertex_count = n;
  p.vertices.assign(n * sns::rendering::kFloatsPerVertex, 0.0f);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t k = 0; k < 3; ++k) {
      p.vertices[i * sns::rendering::kFloatsPerVertex + k] = m.positions[i * 3 + k];
      p.vertices[i * sns::rendering::kFloatsPerVertex + 3 + k] = m.normals[i * 3 + k];
    }
  }
  return p;
}

constexpr std::uint32_t slot(int s) { return 1u << (s - 30); }

const sns::rendering::FidelityFinding* find(const sns::rendering::FidelityReport& r, std::string_view code) {
  const auto it = std::find_if(r.findings.begin(), r.findings.end(), [&](const auto& f) { return f.code == code; });
  return it == r.findings.end() ? nullptr : &*it;
}

sns::rendering::Appearance appearance_with(std::vector<sns::rendering::AppearancePiece> pieces) {
  sns::rendering::Appearance a;
  a.npc_form_key = "000001:Test.esp";
  a.female = true;
  a.pieces = std::move(pieces);
  return a;
}

}  // namespace

TEST_CASE("The grid penetration measure gives body::measure_penetration's numbers", "[fidelity][fit]") {
  const auto body = sphere(10.0f, 24, 32);
  for (const float garment_radius : {9.6f, 10.4f, 11.5f}) {
    const auto garment = sphere(garment_radius, 18, 20);
    sns::nif::Shape body_shape, garment_shape;
    for (std::size_t i = 0; i < body.positions.size(); i += 3) body_shape.positions.push_back({body.positions[i], body.positions[i + 1], body.positions[i + 2]});
    body_shape.indices = body.indices;
    for (std::size_t i = 0; i < garment.positions.size(); i += 3)
      garment_shape.positions.push_back({garment.positions[i], garment.positions[i + 1], garment.positions[i + 2]});
    const auto reference = sns::body::measure_penetration(body_shape, garment_shape, 3.0f, 0.1f);
    const auto fit = sns::rendering::measure_fit(body.positions, body.indices, garment.positions, 3.0f, 0.1f);
    INFO("garment radius " << garment_radius);
    CHECK(fit.vertices == reference.garment_vertices);
    CHECK(fit.near_body == reference.near_body);
    CHECK(fit.inside == reference.inside);
    CHECK(fit.max_depth == Catch::Approx(reference.max_depth).margin(1e-4));
    CHECK(fit.mean_gap == Catch::Approx(reference.mean_gap).margin(1e-4));
  }
  // A sphere 0.4 inside the body: every vertex is within reach and sunk; 1.5 outside: near, none inside.
  CHECK(sns::rendering::measure_fit(body.positions, body.indices, sphere(9.6f, 18, 20).positions, 3.0f, 0.1f).inside_ratio == Catch::Approx(1.0f));
  CHECK(sns::rendering::measure_fit(body.positions, body.indices, sphere(11.5f, 18, 20).positions, 3.0f, 0.1f).inside == 0);
}

TEST_CASE("A garment smaller than the body it is worn over is reported as clipping, with the body pieces to isolate", "[fidelity][fit]") {
  auto body = part_of(sphere(10.0f, 24, 32), "0", "skin", "SKIN:Test.esp", slot(32), sns::nif::kShaderSkinTint);
  auto snug = part_of(sphere(10.3f, 16, 20), "1", "outfit", "SHIRT:Test.esp", slot(32) | slot(46));
  auto tight = part_of(sphere(9.5f, 16, 20, 0, 0, 0), "2", "outfit", "PANTS:Test.esp", slot(49));
  // For this test the body stays drawn (as if the shirt NIF carried it).
  sns::rendering::CharacterScene scene;
  scene.parts = {body, snug, tight};
  const auto report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  REQUIRE(report.body_part_id == "0");
  CHECK(report.body_source == "skin");
  REQUIRE(report.fits.size() == 2);
  CHECK(report.fits[0].verdict == "fits");
  CHECK(report.fits[1].verdict == "clips-badly");
  const auto* clip = find(report, "GarmentClipsBody");
  REQUIRE(clip);
  CHECK(clip->category == sns::rendering::FindingCategory::Compatibility);
  CHECK(clip->confidence == sns::rendering::FindingConfidence::Confirmed);
  CHECK(clip->part_ids == std::vector<std::string>{"2", "0"});
  // Clipping is a compatibility problem to review before exporting, not a broken record: a warning, never a blocker.
  CHECK(report.export_blockers.empty());
  CHECK(std::find(report.export_warnings.begin(), report.export_warnings.end(), std::string("GarmentClipsBody: 2")) != report.export_warnings.end());
}

TEST_CASE("Hidden skin that nothing drawn replaces: a missing part, a reshaped part, or a view through the character", "[fidelity][exposure]") {
  const auto hidden = [](float radius, float cz = 0.0f) {
    auto p = part_of(sphere(radius, 24, 32, 0, 0, cz), "0", "skin", "SKIN:Test.esp", slot(32), sns::nif::kShaderSkinTint);
    p.shown_by_default = false;
    p.hidden_reason = "covered by outfit (Top) on slot(s) 32 (Body)";
    return p;
  };
  sns::rendering::CharacterScene scene;
  // A body 40 units tall whose garment covers only the upper half: the lower half (20 units tall) is missing in game.
  scene.parts = {hidden(20.0f), part_of(sphere(20.4f, 24, 32, 0, 0, 0, 0.0f, 1e9f), "1", "outfit", "TOP:Test.esp", slot(32))};
  auto report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  REQUIRE(report.exposed.size() == 1);
  CHECK(report.exposed[0].exposed_ratio > 0.35f);
  CHECK(report.exposed[0].exposed_ratio < 0.65f);
  CHECK(report.exposed[0].outside > report.exposed[0].see_through);
  CHECK(report.exposed[0].exposed_max_z <= 1.0f);  // nothing exposed above the cover's edge
  const auto* missing = find(report, "HiddenSkinMissingPart");
  REQUIRE(missing);
  CHECK(missing->severity == "error");
  CHECK(missing->confidence == sns::rendering::FindingConfidence::Probable);
  CHECK(report.export_blockers.empty());  // probable findings never block on their own
  CHECK(report.export_warnings.size() == 1);

  // The same shape at foot size (4 units of uncovered height): the garment replaces it with another shape, an info only.
  scene.parts = {hidden(4.0f), part_of(sphere(4.4f, 24, 32, 0, 0, 0, 0.0f, 1e9f), "1", "outfit", "TOP:Test.esp", slot(32))};
  report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  CHECK_FALSE(find(report, "HiddenSkinMissingPart"));
  const auto* reshaped = find(report, "GarmentReshapesHiddenSkin");
  REQUIRE(reshaped);
  CHECK(reshaped->severity == "info");

  // Fully covered: nothing exposed.
  scene.parts = {hidden(20.0f), part_of(sphere(20.4f, 24, 32), "1", "outfit", "TOP:Test.esp", slot(32))};
  report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  REQUIRE(report.exposed.size() == 1);
  CHECK(report.exposed[0].exposed == 0);

  // A smaller body copy inside the garment replaces the hidden skin (the hidden one sticks out by 0.5): not exposed.
  scene.parts = {hidden(20.0f), part_of(sphere(19.5f, 24, 32), "1", "outfit", "TOP:Test.esp", slot(32), sns::nif::kShaderSkinTint)};
  report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  CHECK(report.exposed[0].far_from_drawn == 0);
  CHECK(report.exposed[0].exposed == 0);

  // Only something far behind is drawn (a shield 25 units back): from the front the camera sees through to it.
  scene.parts = {hidden(10.0f), part_of(sphere(12.0f, 24, 32, 0, 0, -30.0f), "1", "outfit", "SHIELD:Test.esp", slot(39))};
  report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  CHECK(report.exposed[0].see_through > 0);
  CHECK(find(report, "HiddenSkinSeeThrough"));
}

TEST_CASE("Partitions outside the addon's slots are what the game leaves out; the viewer draws them", "[fidelity][partitions]") {
  auto mesh = sphere(10.0f, 8, 8);
  auto corset = part_of(mesh, "0", "outfit", "CORSET:Test.esp", slot(32), sns::nif::kShaderSkinTint);
  corset.partitions = {32, 53};
  // First half of the triangles in partition 32, second half in 53.
  const std::size_t triangles = corset.indices.size() / 3;
  for (std::size_t t = 0; t < triangles; ++t) corset.triangle_partitions.push_back(t < triangles / 2 ? 32 : 53);
  sns::rendering::AppearancePiece piece{"CORSET:Test.esp", "Corset", "outfit", slot(32), {{"CORSET:Test.esp-AA", "meshes\\0.nif", false, slot(32)}}};
  sns::rendering::CharacterScene scene;
  scene.parts = {corset};
  scene.outfit_diagnostics.push_back({});
  scene.outfit_diagnostics.back().armor_form_key = "CORSET:Test.esp";
  const auto report = sns::rendering::analyze_fidelity(appearance_with({piece}), scene);
  const auto* partition = find(report, "PartitionNotInAddonSlots");
  REQUIRE(partition);
  CHECK(partition->confidence == sns::rendering::FindingConfidence::Probable);
  CHECK(partition->evidence.find(std::to_string(triangles - triangles / 2) + " de " + std::to_string(triangles)) == 0);
  REQUIRE(report.pieces.size() == 1);
  CHECK(report.pieces[0].game == "partial");
  CHECK(report.pieces[0].viewer == "drawn");
  CHECK_FALSE(report.pieces[0].viewer_matches_game);
}

TEST_CASE("Record and file problems are DATA findings; a piece with no addon for the race blocks the export", "[fidelity][data]") {
  sns::rendering::CharacterScene scene;
  sns::rendering::OutfitPieceDiagnostic no_addon;
  no_addon.armor_form_key = "HELM:Test.esp";
  no_addon.name = "Helm";
  no_addon.slot_indices = {31};
  no_addon.no_addon_for_race = true;
  scene.outfit_diagnostics.push_back(no_addon);
  sns::rendering::OutfitPieceDiagnostic unequipped;
  unequipped.armor_form_key = "GLOVES:Test.esp";
  unequipped.name = "Gloves";
  unequipped.equipped = false;
  unequipped.unequipped_by = "Gauntlets";
  unequipped.conflicting_slots = {33};
  scene.outfit_diagnostics.push_back(unequipped);
  scene.issues.push_back({"warning", "MeshNotFound", "Mesh not found as loose file or in any loaded BSA: meshes\\boots_1.nif", "Boots"});
  sns::rendering::TextureInfo missing;
  missing.path = "textures\\boots_d.dds";
  missing.location = "missing";
  scene.texture_info["textures\\boots_d.dds"] = missing;
  const auto report = sns::rendering::analyze_fidelity(
      appearance_with({{"HELM:Test.esp", "Helm", "outfit", slot(31), {}}, {"GLOVES:Test.esp", "Gloves", "outfit", slot(33), {}}}), scene);
  const auto* helm = find(report, "PieceNoAddonForRace");
  REQUIRE(helm);
  CHECK(helm->category == sns::rendering::FindingCategory::Data);
  CHECK(helm->severity == "error");
  CHECK(find(report, "PieceUnequipped"));
  CHECK(find(report, "MeshMissing"));
  CHECK(find(report, "TextureMissing"));
  CHECK(report.export_blockers.size() == 2);  // the helm and the missing mesh; unequip and the texture are not blockers
  REQUIRE(report.pieces.size() == 2);
  CHECK(report.pieces[0].game == "not-drawn");
  CHECK(report.pieces[0].viewer == "nothing");
  CHECK(report.pieces[0].viewer_matches_game);
  CHECK(report.data_findings >= 4);
  // The JSON the viewer and the harness read.
  const auto j = sns::rendering::to_json(report);
  CHECK(j.at("findings").size() == report.findings.size());
  CHECK(j.at("counts").at("data") == report.data_findings);
  CHECK(j.at("exportBlockers").size() == 2);
}

TEST_CASE("Two drawn addons with geometry in the same slot are an uncertain overlap, not a guess", "[fidelity][partitions]") {
  auto corset = part_of(sphere(10.0f, 8, 8), "0", "outfit", "CORSET:Test.esp", slot(32) | slot(53), sns::nif::kShaderSkinTint);
  corset.partitions = {32, 53};
  auto pants = part_of(sphere(10.5f, 8, 8), "1", "outfit", "PANTS:Test.esp", slot(53));
  pants.partitions = {53};
  sns::rendering::CharacterScene scene;
  scene.parts = {corset, pants};
  const auto report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  const auto* overlap = find(report, "AddonSlotOverlap");
  REQUIRE(overlap);
  CHECK(overlap->confidence == sns::rendering::FindingConfidence::Uncertain);
  CHECK(overlap->part_ids.size() == 2);
  CHECK(report.export_blockers.empty());
}

// Two triangles in partition 32 (upper quad) and one in 53 (a flap below), skinned to one bone, as nifly writes them.
static std::string partitioned_nif() {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> v{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, -10}, {10, 0, -10}};
  std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}, {4, 5, 1}};
  std::vector<nifly::Vector2> uv(v.size(), nifly::Vector2(0, 0));
  std::vector<nifly::Vector3> n(v.size(), nifly::Vector3(0, 0, 1));
  auto* shape = file.CreateShapeFromData("Body", &v, &t, &uv, &n);
  if (!shape) throw std::runtime_error("nifly did not create the shape");
  file.CreateSkinning(shape);
  nifly::MatTransform identity;
  auto* node = file.AddNode("NPC Pelvis [Pelv]", identity);
  if (!node) throw std::runtime_error("nifly did not create the bone");
  std::vector<int> bones{static_cast<int>(file.GetBlockID(node))};
  file.SetShapeBoneIDList(shape, bones);
  for (std::uint16_t i = 0; i < v.size(); ++i) {
    std::vector<std::uint8_t> ids{0, 0, 0, 0};
    std::vector<float> weights{1.0f, 0.0f, 0.0f, 0.0f};
    file.SetShapeVertWeights("Body", i, ids, weights);
  }
  nifly::NiVector<nifly::BSDismemberSkinInstance::PartitionInfo> parts;
  nifly::BSDismemberSkinInstance::PartitionInfo body_part, legs_part;
  body_part.flags = nifly::PF_EDITOR_VISIBLE;
  body_part.partID = 32;
  legs_part.flags = nifly::PF_EDITOR_VISIBLE;
  legs_part.partID = 53;
  parts.push_back(body_part);
  parts.push_back(legs_part);
  file.SetShapePartitions(shape, parts, {0, 0, 1});
  file.UpdateSkinPartitions(shape);
  std::ostringstream out(std::ios::binary);
  if (file.Save(out) != 0) throw std::runtime_error("nifly could not save");
  return out.str();
}

TEST_CASE("load_mesh reads the dismember partition of every triangle", "[fidelity][nif]") {
  const auto bytes = partitioned_nif();
  const auto mesh = sns::nif::load_mesh(bytes, "partitions.nif");
  REQUIRE(mesh.shapes.size() == 1);
  const auto& s = mesh.shapes[0];
  CHECK(s.partitions == std::vector<std::uint16_t>{32, 53});
  REQUIRE(s.triangle_partitions.size() * 3 == s.indices.size());
  // The partition of each triangle follows its own vertices, whatever order the skin partition stores them in.
  std::size_t in_legs = 0;
  for (std::size_t k = 0; k < s.triangle_partitions.size(); ++k) {
    const bool lower = s.indices[k * 3] >= 4 || s.indices[k * 3 + 1] >= 4 || s.indices[k * 3 + 2] >= 4;
    CHECK(s.triangle_partitions[k] == (lower ? 53 : 32));
    in_legs += s.triangle_partitions[k] == 53;
  }
  CHECK(in_legs == 1);
}

TEST_CASE("The scene hides the partitions its addon does not claim, and the report says the viewer now matches the game", "[fidelity][partitions][e2e]") {
  sns::test::TempDir dir;
  dir.write("Garment Mod/meshes/test/corset_1.nif", partitioned_nif());
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Garment Mod", dir.path() / "Garment Mod"}});
  sns::assets::AssetResolver resolver(vfs, {});
  sns::rendering::Appearance a;
  a.npc_form_key = "000001:Test.esp";
  // The ARMA claims slot 32 only; the NIF has triangles in 32 and 53 (the Wine Duchess "released" corset case).
  a.pieces.push_back({"000800:Test.esp", "Corset", "outfit", slot(32), {{"000801:Test.esp", "meshes\\test\\corset_1.nif", false, slot(32)}}});
  const auto scene = sns::rendering::build_character_scene(a, resolver, {});
  REQUIRE(scene.parts.size() == 2);
  const auto& drawn = scene.parts[0];
  const auto& hidden = scene.parts[1];
  CHECK(drawn.shown_by_default);
  CHECK(drawn.indices.size() == 6);
  CHECK(drawn.triangle_partitions == std::vector<std::uint16_t>{32, 32});
  CHECK_FALSE(hidden.shown_by_default);
  CHECK(hidden.partition_hidden);
  CHECK(hidden.indices.size() == 3);
  CHECK(hidden.hidden_reason.find("dismember partition 53") != std::string::npos);
  CHECK(hidden.vertices == drawn.vertices);  // same geometry, only the triangle list differs
  CHECK(scene.stats.triangles == 2);

  const auto report = sns::rendering::analyze_fidelity(a, scene);
  const auto* partition = find(report, "PartitionNotInAddonSlots");
  REQUIRE(partition);
  CHECK(partition->part_ids == std::vector<std::string>{hidden.id});
  REQUIRE(report.pieces.size() == 1);
  CHECK(report.pieces[0].game == "partial");
  CHECK(report.pieces[0].viewer == "partial");
  CHECK(report.pieces[0].viewer_matches_game);

  // Claiming 53 as well: nothing is split and nothing is reported.
  a.pieces[0].addons[0].slot_mask = slot(32) | slot(53);
  const auto whole = sns::rendering::build_character_scene(a, resolver, {});
  REQUIRE(whole.parts.size() == 1);
  CHECK(whole.parts[0].indices.size() == 9);
  CHECK_FALSE(find(sns::rendering::analyze_fidelity(a, whole), "PartitionNotInAddonSlots"));
}

TEST_CASE("Grid against brute-force penetration timing on body-sized meshes", "[.][fidelity][benchmark]") {
  // Not run by default: `sns_engine_tests "[benchmark]"`. A UBE body has ~56k triangles; pants or a skirt ~10-20k vertices.
  const auto body = sphere(60.0f, 160, 180);     // 57,600 triangles
  const auto garment = sphere(60.5f, 120, 140);  // 17,061 vertices
  sns::nif::Shape body_shape, garment_shape;
  for (std::size_t i = 0; i < body.positions.size(); i += 3) body_shape.positions.push_back({body.positions[i], body.positions[i + 1], body.positions[i + 2]});
  body_shape.indices = body.indices;
  for (std::size_t i = 0; i < garment.positions.size(); i += 3)
    garment_shape.positions.push_back({garment.positions[i], garment.positions[i + 1], garment.positions[i + 2]});
  const auto t0 = std::chrono::steady_clock::now();
  const auto reference = sns::body::measure_penetration(body_shape, garment_shape, 3.0f, 0.1f);
  const auto t1 = std::chrono::steady_clock::now();
  const auto fit = sns::rendering::measure_fit(body.positions, body.indices, garment.positions, 3.0f, 0.1f);
  const auto t2 = std::chrono::steady_clock::now();
  const sns::rendering::TriangleGrid grid(body.positions, body.indices, 3.0f);
  const auto t3 = std::chrono::steady_clock::now();
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  WARN("brute force " << ms(t0, t1) << " ms, grid " << ms(t1, t2) << " ms (of which building the grid " << ms(t2, t3) << " ms); body "
                      << body.indices.size() / 3 << " triangles, garment " << garment.positions.size() / 3 << " vertices");
  CHECK(fit.near_body == reference.near_body);
  CHECK(fit.inside == reference.inside);

  // The whole report on a scene of Serana's size: a hidden 57.6k-triangle body, a drawn body copy and five garments (~340k drawn
  // triangles), the shape of the real Wine Duchess scene.
  sns::rendering::CharacterScene scene;
  auto hidden_body = part_of(body, "0", "skin", "SKIN:Test.esp", slot(32), sns::nif::kShaderSkinTint);
  hidden_body.shown_by_default = false;
  hidden_body.hidden_reason = "covered by outfit (Top) on slot(s) 32 (Body)";
  scene.parts.push_back(hidden_body);
  scene.parts.push_back(part_of(sphere(59.8f, 160, 180), "1", "outfit", "TOP:Test.esp", slot(32), sns::nif::kShaderSkinTint));
  for (int g = 0; g < 5; ++g)
    scene.parts.push_back(part_of(sphere(60.5f + static_cast<float>(g) * 0.3f, 150, 170), std::to_string(2 + g), "outfit", "G:Test.esp", slot(49)));
  const auto t4 = std::chrono::steady_clock::now();
  const auto report = sns::rendering::analyze_fidelity(appearance_with({}), scene);
  const auto t5 = std::chrono::steady_clock::now();
  WARN("whole report " << ms(t4, t5) << " ms (fit " << report.fit_ms << " ms, exposure " << report.exposure_ms << " ms), " << report.fits.size()
                       << " garments");
  CHECK(report.fits.size() == 5);
}
