#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>
#include <algorithm>
#include <sstream>

#include "sns/body/bcng_rules.hpp"
#include "sns/body/body_analysis.hpp"
#include "sns/core/error.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/vfs.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::body;

namespace {

// SSE body NIF: one quad per shape name, a diffuse texture, and BodySlide's BODYTRI on the root when `tri` is set.
std::string body_nif(const std::vector<std::string>& shapes, const std::string& diffuse, const std::string& tri) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  for (const auto& name : shapes) {
    std::vector<nifly::Vector3> v{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}};
    std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
    auto* s = file.CreateShapeFromData(name, &v, &t, &uv, &n);
    if (!diffuse.empty()) {
      auto path = diffuse;  // nifly takes the path by non-const reference
      file.SetTextureSlot(s, path, 0);
    }
  }
  if (!tri.empty()) {
    auto extra = std::make_unique<nifly::NiStringExtraData>();
    extra->name.get() = "BODYTRI";
    extra->stringData.get() = tri;
    file.AssignExtraData(file.GetRootNode(), std::move(extra));
  }
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

NpcBodyInfo woman(const std::string& body_model, const std::string& race = "NordRace") {
  NpcBodyInfo n;
  n.form_key = "0A2C8E:Skyrim.esm";
  n.editor_id = "HousecarlWhiterun";
  n.name = "Lydia";
  n.sex = "female";
  n.race = {"013746:Skyrim.esm", race, "Nord"};
  n.skin = {"000D64:Skyrim.esm", "SkinNaked", ""};
  SkinAddon body;
  body.form_key = "000D6E:Skyrim.esm";
  body.editor_id = "NakedTorso";
  body.female_model = body_model;
  body.body_slots = {32};
  body.applies_to_race = true;
  n.skin_addons.push_back(body);
  n.facegen_geometry = "meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\000A2C8E.nif";
  return n;
}

BodySlidePreset preset(std::string name, std::string set, std::string source = "Pack.xml") {
  BodySlidePreset p;
  p.name = std::move(name);
  p.set = std::move(set);
  p.source = std::move(source);
  p.file = "CalienteTools\\BodySlide\\SliderPresets\\" + p.source;
  p.uid = p.file + "|" + p.name;
  p.refit = p.name.ends_with("-Refit");
  p.family = classify_family(p);
  return p;
}

BodyEnvironment ready_env() {
  BodyEnvironment env;
  env.bcng_dll_provider = "Body Change NG";
  env.racemenu_dll_provider = "RaceMenu";
  env.installed_female_mask = kFamCbbe;
  return env;
}

bool has_code(const std::vector<Issue>& issues, std::string_view code) {
  return std::any_of(issues.begin(), issues.end(), [&](const auto& i) { return i.code == code; });
}

}  // namespace

TEST_CASE("Body inspect JSON: the worker's camelCase NPC round-trips", "[body]") {
  const auto j = nlohmann::json::parse(R"({
    "formKey":"0A2C8E:Skyrim.esm","formId":"000A2C8E","editorId":"HousecarlWhiterun","name":"Lydia","sex":"female",
    "effectiveSex":null,"originPlugin":"Skyrim.esm","winningPlugin":"Helen_Lydia_replacer.esp","isUnique":true,
    "usesTemplate":false,"templateFlags":[],"race":{"formKey":"013746:Skyrim.esm","recordType":"RACE","editorId":"NordRace"},
    "isChild":false,"isPlayable":true,"raceKeywords":["ActorTypeNPC"],"templateUnresolved":false,
    "keywords":[],"factions":[{"formKey":"05C84D:Skyrim.esm","editorId":"PotentialFollowerFaction","rank":0}],
    "skinSource":"race","skinAddons":[{"formKey":"000D6E:Skyrim.esm","editorId":"NakedTorso","race":"013746:Skyrim.esm",
      "additionalRaces":[],"femaleModel":"Actors\\Character\\Character Assets\\FemaleBody_1.nif","maleModel":null,
      "bodySlots":[32],"appliesToRace":true}],
    "headParts":[],"faceGen":{"geometryPath":"meshes\\x.nif","tintPath":"textures\\x.dds"},"notes":["n"]})");
  const auto n = npc_body_from_json(j);
  CHECK(n.form_key == "0A2C8E:Skyrim.esm");
  CHECK(n.defining_plugin() == "Skyrim.esm");
  CHECK(n.sex_for_rules() == "female");  // a null effectiveSex falls back to the record's own sex
  CHECK(n.race.editor_id == "NordRace");
  CHECK(n.race_for_rules().editor_id == "NordRace");
  REQUIRE(n.skin_addons.size() == 1);
  CHECK(n.skin_addons[0].male_model.empty());
  CHECK(n.skin_addons[0].body_slots == std::vector<int>{32});
  REQUIRE(n.factions.size() == 1);
  CHECK(n.factions[0].faction.editor_id == "PotentialFollowerFaction");
  CHECK(n.facegen_geometry == "meshes\\x.nif");
  const auto again = npc_body_from_json(npc_body_to_json(n));
  CHECK(again.skin_addons[0].female_model == n.skin_addons[0].female_model);
  CHECK(again.notes == n.notes);
  CHECK_THROWS_AS(npc_body_from_json(nlohmann::json::object()), Error);
}

TEST_CASE("Body environment: BCNG, OBody, settings and framework plugins come from the winning files", "[body]") {
  test::TempDir dir;
  dir.write("bcng/SKSE/Plugins/BodyChangeNG.dll", "dll");
  dir.write("bcng/SKSE/Plugins/BodyChangeNG/settings.json", R"({"femaleNpcBodyType": 2, "other": true})");
  dir.write("dist/SKSE/Plugins/BodyChangeNGdistribution.json", R"({"schemaVersion":8,"rules":[]})");
  dir.write("obody/SKSE/Plugins/OBody.dll", "dll");
  const auto vfs = mo2::VirtualFileSystem::build_from({
      {"Body Change NG", dir.path() / "bcng", mo2::ProviderKind::Mod, 0},
      {"Runtime Distribution", dir.path() / "dist", mo2::ProviderKind::Mod, 0},
      {"OBody NG", dir.path() / "obody", mo2::ProviderKind::Mod, 0},
  });
  mo2::EffectiveLoadOrder lo;
  for (const auto* name : {"Skyrim.esm", "3BBB.esp", "UBE_AllRace.esp", "RaceCompatibility.esm"}) {
    mo2::ResolvedPlugin p;
    p.name = name;
    p.active = true;
    lo.active.push_back(p);
  }
  const auto env = inspect_environment(vfs, &lo);
  CHECK(env.bcng_dll_provider == "Body Change NG");
  CHECK(env.obody_dll_provider == "OBody NG");
  CHECK(env.racemenu_dll_provider.empty());
  CHECK(env.settings.from_file);
  CHECK(env.settings.female_mask() == kFamUbe);
  CHECK(env.distribution_provider == "Runtime Distribution");
  CHECK(env.distribution_text.find("schemaVersion") != std::string::npos);
  CHECK(env.ube_race_plugin_active);
  CHECK(env.race_compatibility_active);
  CHECK(env.installed_female_mask == (kFamCbbe | kFamUbe));

  test::TempDir bad;
  bad.write("m/SKSE/Plugins/BodyChangeNG/settings.json", "{ not json");
  const auto vfs2 = mo2::VirtualFileSystem::build_from({{"M", bad.path() / "m", mo2::ProviderKind::Mod, 0}});
  const auto env2 = inspect_environment(vfs2, nullptr);
  CHECK_FALSE(env2.settings_error.empty());
  CHECK_FALSE(env2.settings.from_file);  // BCNG defaults stay in force
}

TEST_CASE("Body family: shapes, paths and TRI decide; unnamed bodies stay unknown", "[body]") {
  test::TempDir dir;
  dir.write("Body3BA/meshes/actors/character/character assets/femalebody_1.nif",
            body_nif({"3BA", "3BA_Vagina"}, "textures\\actors\\character\\female\\femalebody_1.dds", "actors\\character\\character assets\\femalebody.tri"));
  dir.write("Body3BA/meshes/actors/character/character assets/femalebody.tri", "TRIP");
  dir.write("Plain/meshes/plain/femalebody_1.nif", body_nif({"CBBE"}, "textures\\actors\\character\\female\\femalebody_1.dds", "plain\\femalebody.tri"));
  dir.write("Ube/meshes/!UBE/Body/femalebody_tangent_1.nif",
            body_nif({"BaseShape"}, "textures\\actors\\character\\UBE\\femalebody_1.dds", "!UBE\\Body\\femalebody_tangent.tri"));
  dir.write("Nameless/meshes/nameless/femalebody_1.nif", body_nif({"BaseShape"}, "", ""));
  dir.write("Broken/meshes/broken/femalebody_1.nif", "not a nif");
  dir.write("Face/meshes/actors/character/facegendata/facegeom/Skyrim.esm/000A2C8E.nif", "face");
  const auto vfs = mo2::VirtualFileSystem::build_from({
      {"Body3BA", dir.path() / "Body3BA", mo2::ProviderKind::Mod, 0},
      {"Plain", dir.path() / "Plain", mo2::ProviderKind::Mod, 0},
      {"Ube", dir.path() / "Ube", mo2::ProviderKind::Mod, 0},
      {"Nameless", dir.path() / "Nameless", mo2::ProviderKind::Mod, 0},
      {"Broken", dir.path() / "Broken", mo2::ProviderKind::Mod, 0},
      {"Face Replacer", dir.path() / "Face", mo2::ProviderKind::Mod, 0},
  });
  auto env = ready_env();
  env.installed_female_mask = 0;
  const BodyInspector inspector(vfs, env);

  SECTION("3BA shape names give CBBE 3BA, the TRI is found, the face replacer is named") {
    const auto a = inspector.analyze(woman("Actors\\Character\\Character Assets\\FemaleBody_1.nif"));
    CHECK(a.female);
    CHECK(a.has_body_addon);
    CHECK(a.body.provider == "Body3BA");
    CHECK(a.body.body_tri == "actors\\character\\character assets\\femalebody.tri");
    CHECK(a.body.body_tri_provider == "Body3BA");
    CHECK(a.family_mask == kFamCbbe);
    CHECK(a.family == BodyFamily::Cbbe3ba);
    CHECK(a.layout == TextureLayout::Standard);
    CHECK(a.custom_appearance);
    CHECK(a.facegen_provider == "Face Replacer");
    CHECK_FALSE(has_code(a.issues, "FamilyUnknown"));
  }
  SECTION("a plain CBBE shape without physics bones is CBBE, not 3BA; a missing TRI needs resources") {
    const auto a = inspector.analyze(woman("plain\\femalebody_1.nif"));
    CHECK(a.family == BodyFamily::Cbbe);
    CHECK(a.family_mask == kFamCbbe);
    CHECK(has_code(a.issues, "BodyTriMissing"));
  }
  SECTION("the !UBE folder and the UBE texture layout give UBE") {
    const auto a = inspector.analyze(woman("!UBE\\Body\\femalebody_tangent_1.nif"));
    CHECK(a.family == BodyFamily::Ube);
    CHECK(a.layout == TextureLayout::Ube);
    CHECK_FALSE(has_code(a.issues, "TextureLayoutMismatch"));
  }
  SECTION("a body that names nothing stays unknown and says why") {
    const auto a = inspector.analyze(woman("nameless\\femalebody_1.nif"));
    CHECK(a.family == BodyFamily::Unknown);
    CHECK(a.family_mask == 0);
    CHECK(has_code(a.issues, "FamilyUnknown"));
    CHECK(has_code(a.issues, "NoBodyTri"));
  }
  SECTION("missing, unreadable and absent body addons are reported, never guessed") {
    CHECK(has_code(inspector.analyze(woman("nowhere\\femalebody_1.nif")).issues, "BodyMeshMissing"));
    CHECK(has_code(inspector.analyze(woman("broken\\femalebody_1.nif")).issues, "BodyMeshUnreadable"));
    auto no_addon = woman("x.nif");
    no_addon.skin_addons[0].applies_to_race = false;
    const auto a = inspector.analyze(no_addon);
    CHECK_FALSE(a.has_body_addon);
    CHECK(has_code(a.issues, "NoBodyAddon"));
  }
  SECTION("with one framework installed, an unnamed body gets a weak family flagged as such") {
    env.installed_female_mask = kFamCbbe;
    const BodyInspector weak(vfs, env);
    const auto a = weak.analyze(woman("nameless\\femalebody_1.nif"));
    CHECK(a.family_mask == kFamCbbe);
    CHECK(has_code(a.issues, "FamilyFromFramework"));
    const auto p = preset("Athletic", "CBBE 3BBB Body Amazing");
    CHECK(assess_preset(a, p, {}, env, false).verdict == Verdict::NeedsResources);  // NoBodyTri outranks Experimental
    CHECK(has_code(assess_preset(a, p, {}, env, false).issues, "FamilyFromFramework"));
    CHECK(assess_preset(a, p, {}, env, true).verdict == Verdict::Blocked);
  }
  SECTION("a UBE race EditorID names the family when the mesh does not") {
    const auto a = inspector.analyze(woman("nameless\\femalebody_1.nif", "UBE_NordRace"));
    CHECK(a.family == BodyFamily::Ube);
  }
}

TEST_CASE("Preset assessment: same family is compatible, other families and children are blocked", "[body]") {
  NpcBodyAnalysis npc;
  npc.form_key = "0A2C8E:Skyrim.esm";
  npc.female = true;
  npc.family_mask = kFamCbbe;
  npc.family = BodyFamily::Cbbe3ba;
  npc.has_body_addon = true;
  const auto env = ready_env();
  const auto p3ba = preset("Athletic", "CBBE 3BBB Body Amazing");
  const auto pube = preset("UBE Athletic", "UBE Body");
  const auto pmale = preset("Himbo Strong", "HIMBO Body");
  const auto refit = preset("Athletic-Refit", "CBBE 3BBB Body Amazing");
  const auto unknown = preset("Mystery", "Some Body");

  CHECK(assess_preset(npc, p3ba, {}, env, true).verdict == Verdict::Compatible);
  const auto mismatch = assess_preset(npc, pube, {}, env, false);
  CHECK(mismatch.verdict == Verdict::Blocked);
  CHECK(has_code(mismatch.issues, "FamilyMismatch"));
  CHECK(has_code(assess_preset(npc, pmale, {}, env, false).issues, "PresetSexMismatch"));
  CHECK(assess_preset(npc, refit, {}, env, false).verdict == Verdict::Blocked);
  CHECK(assess_preset(npc, unknown, {}, env, false).verdict == Verdict::Experimental);
  CHECK(assess_preset(npc, unknown, {}, env, true).verdict == Verdict::Blocked);

  auto child = npc;
  child.child = true;
  CHECK(assess_preset(child, p3ba, {}, env, false).verdict == Verdict::Blocked);

  auto unknown_body = npc;
  unknown_body.family_mask = 0;
  unknown_body.family = BodyFamily::Unknown;
  CHECK(assess_preset(unknown_body, p3ba, {}, env, false).verdict == Verdict::Experimental);
  CHECK(assess_preset(unknown_body, p3ba, {}, env, true).verdict == Verdict::Blocked);

  // Profile problems need resources, not a different preset.
  auto no_bcng = env;
  no_bcng.bcng_dll_provider.clear();
  CHECK(has_code(assess_preset(npc, p3ba, {}, no_bcng, false).issues, "BcngMissing"));
  CHECK(assess_preset(npc, p3ba, {}, no_bcng, false).verdict == Verdict::NeedsResources);
  auto two = env;
  two.obody_dll_provider = "OBody NG";
  CHECK(has_code(assess_preset(npc, p3ba, {}, two, false).issues, "TwoBodyControllers"));
  auto missing_tri = npc;
  missing_tri.issues.push_back({"error", "BodyTriMissing", "x"});
  CHECK(assess_preset(missing_tri, p3ba, {}, env, false).verdict == Verdict::NeedsResources);

  // BCNG's automatic distribution only hands out presets of the global family.
  auto global_ube = env;
  global_ube.settings.female_npc_body_type = 2;
  CHECK(has_code(assess_preset(npc, p3ba, {}, global_ube, true).issues, "GlobalFamilyMismatch"));
  CHECK(assess_preset(npc, p3ba, {}, global_ube, false).verdict == Verdict::Compatible);  // a named NPC rule is not automatic

  // Duplicate names are allowed but named by file.
  auto a = preset("Curvy", "CBBE 3BBB Body Amazing", "A.xml");
  auto b = preset("Curvy", "CBBE 3BBB Body Amazing", "B.xml");
  b.sliders.push_back({"Breasts", 10.0f, 20.0f});
  const auto lib = build_library({}, {a, b});
  const auto dup = assess_preset(npc, *lib.find(a.uid), lib, env, false);
  CHECK(dup.verdict == Verdict::Compatible);
  CHECK(has_code(dup.issues, "DuplicateName"));
  CHECK(verdict_id(Verdict::NeedsResources) == "needs-resources");
  CHECK(verdict_label(Verdict::Blocked) == "Bloqueada");
}

TEST_CASE("Family change is a separate operation that is never simply compatible", "[body]") {
  NpcBodyAnalysis npc;
  npc.female = true;
  npc.family_mask = kFamCbbe;
  npc.family = BodyFamily::Cbbe3ba;
  auto env = ready_env();

  const auto to_ube = assess_family_change(npc, BodyFamily::Ube, env);
  CHECK(to_ube.verdict == Verdict::NeedsResources);  // UBE race plugin not active
  CHECK(has_code(to_ube.issues, "UbeRaceMissing"));
  CHECK(has_code(to_ube.issues, "FamilyChangeExperimental"));

  env.ube_race_plugin_active = true;
  CHECK(assess_family_change(npc, BodyFamily::Ube, env).verdict == Verdict::Experimental);
  CHECK(assess_family_change(npc, BodyFamily::Cbbe3ba, env).verdict == Verdict::Blocked);  // already there
  CHECK(assess_family_change(npc, BodyFamily::Other, env).verdict == Verdict::Blocked);

  auto custom = npc;
  custom.custom_appearance = true;
  custom.facegen_provider = "Serana Replacer";
  CHECK(has_code(assess_family_change(custom, BodyFamily::Ube, env).issues, "CustomHeadNeck"));

  auto unknown = npc;
  unknown.family_mask = 0;
  unknown.family = BodyFamily::Unknown;
  CHECK(assess_family_change(unknown, BodyFamily::Ube, env).verdict == Verdict::Blocked);

  NpcBodyAnalysis ube;
  ube.female = true;
  ube.family_mask = kFamUbe;
  ube.family = BodyFamily::Ube;
  CHECK(assess_family_change(ube, BodyFamily::Cbbe3ba, env).verdict == Verdict::Experimental);
  env.installed_female_mask = 0;
  CHECK(has_code(assess_family_change(ube, BodyFamily::Cbbe3ba, env).issues, "CbbeMissing"));
}

TEST_CASE("assess_outfit_mesh_compatibility evaluates family and weight variants", "[body][outfit]") {
  test::TempDir dir;
  // 3BA outfit mesh with _1 and _0
  dir.write("Base/meshes/armor/dress_1.nif", body_nif({"BaseShape", "Dress3BA"}, "textures\\d.dds", "CalienteTools\\BodySlide\\SliderSets\\Dress.tri"));
  dir.write("Base/meshes/armor/dress_0.nif", body_nif({"BaseShape", "Dress3BA"}, "textures\\d.dds", "CalienteTools\\BodySlide\\SliderSets\\Dress.tri"));

  // UBE outfit mesh
  dir.write("Base/meshes/armor/ube_suit_1.nif", body_nif({"BaseShape", "UbeArmor"}, "textures\\d.dds", ""));

  const auto vfs = mo2::VirtualFileSystem::build_from({{"Base", dir.path() / "Base"}});
  auto lookup = loose_lookup(vfs);
  BodyEnvironment env = ready_env();
  BodyInspector inspector(env, lookup);

  NpcBodyAnalysis cbbe_npc;
  cbbe_npc.form_key = "0A2C8E:Skyrim.esm";
  cbbe_npc.female = true;
  cbbe_npc.family = BodyFamily::Cbbe3ba;
  cbbe_npc.family_mask = kFamCbbe;

  NpcBodyAnalysis ube_npc;
  ube_npc.form_key = "002B6C:Dawnguard.esm";
  ube_npc.female = true;
  ube_npc.family = BodyFamily::Ube;
  ube_npc.family_mask = kFamUbe;

  SECTION("CBBE 3BA outfit on CBBE 3BA NPC is compatible and finds weight variants and TRI") {
    const auto comp = assess_outfit_mesh_compatibility(cbbe_npc, "armor/dress_1.nif", inspector);
    CHECK(comp.verdict == Verdict::Compatible);
    CHECK(comp.mesh_family == BodyFamily::Cbbe3ba);
    CHECK(comp.weight_variants_found);
    CHECK(comp.morph_tri_found);
  }

  SECTION("UBE outfit on CBBE 3BA NPC gives FamilyMismatch warning") {
    const auto comp = assess_outfit_mesh_compatibility(cbbe_npc, "armor/ube_suit_1.nif", inspector);
    CHECK(comp.verdict == Verdict::Experimental);
    CHECK(comp.mesh_family == BodyFamily::Ube);
    CHECK(has_code(comp.issues, "FamilyMismatch"));
  }

  SECTION("CBBE 3BA outfit on UBE NPC gives FamilyMismatch warning") {
    const auto comp = assess_outfit_mesh_compatibility(ube_npc, "armor/dress_1.nif", inspector);
    CHECK(comp.verdict == Verdict::Experimental);
    CHECK(has_code(comp.issues, "FamilyMismatch"));
  }

  SECTION("Missing mesh returns OutfitMeshMissing") {
    const auto comp = assess_outfit_mesh_compatibility(cbbe_npc, "armor/nonexistent.nif", inspector);
    CHECK(comp.verdict == Verdict::NeedsResources);
    CHECK(has_code(comp.issues, "OutfitMeshMissing"));
  }
}

