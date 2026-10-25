#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>
#include <sstream>

#include "plugin_bytes.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/outfit/loose_outfit_indexer.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::mo2;
using namespace sns::outfit;

namespace {

// Helper to construct a valid minimal SSE NIF with a named shape
std::string make_test_nif(const std::string& shape_name) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> v{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}};
  std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
  file.CreateShapeFromData(shape_name, &v, &t, &uv, &n);
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

struct LooseTestFixture {
  test::TempDir dir;
  InstanceConfig config;
  GameInstall game;
  Profile profile;

  LooseTestFixture() {
    // 1. Base Game Data
    dir.write("game/SkyrimSE.exe", "");
    dir.write("game/Data/Skyrim.esm", test::make_tes4(0x1, {}));
    dir.write("game/Data/meshes/armor/iron/cuirass_0.nif", make_test_nif("IronCuirass"));
    dir.write("game/Data/meshes/armor/iron/cuirass_1.nif", make_test_nif("IronCuirass"));

    // 2. Mod A: Loose outfit pack without ESP (NIF and DDS loose files)
    dir.write("mods/Crimson Outfit/meshes/armor/crimson/dress_0.nif", make_test_nif("3BA_Dress"));
    dir.write("mods/Crimson Outfit/meshes/armor/crimson/dress_1.nif", make_test_nif("3BA_Dress"));
    dir.write("mods/Crimson Outfit/meshes/armor/crimson/dress.tri", "tri_bytes");
    dir.write("mods/Crimson Outfit/textures/armor/crimson/dress_d.dds", "dds_bytes");

    // 3. Mod B: Replacer mod without ESP (overrides vanilla iron armor cuirass with 3BA mesh)
    dir.write("mods/Iron 3BA Replacer/meshes/armor/iron/cuirass_0.nif", make_test_nif("3BA_IronCuirass"));
    dir.write("mods/Iron 3BA Replacer/meshes/armor/iron/cuirass_1.nif", make_test_nif("3BA_IronCuirass"));

    // 4. Mod C: BodySlide Project pack (contains .osp and shapes data without ESP)
    const std::string osp_xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<SliderSetInfo version="1">
    <SliderSet name="Crimson BodySlide Boots">
        <DataFolder>Crimson Boots</DataFolder>
        <SourceFile>Crimson Boots.nif</SourceFile>
        <OutputPath>Meshes\Armor\crimson</OutputPath>
        <OutputFile GenWeights="true">boots</OutputFile>
        <Shape target="3BA_Boots">3BA_Boots</Shape>
    </SliderSet>
</SliderSetInfo>
)";
    dir.write("mods/Crimson BodySlide/CalienteTools/BodySlide/SliderSets/CrimsonBoots.osp", osp_xml);

    // 5. Mod D: Corrupted asset mod to test RES-03 safety
    dir.write("mods/Damaged Pack/meshes/armor/damaged/corrupt_1.nif", "NOT_A_VALID_NIF_HEADER");

    config.mods_dir = dir.path() / "mods";
    config.overwrite_dir = dir.path() / "overwrite";
    game = inspect_game(dir.path() / "game");

    // Profile ordering: Game < Crimson Outfit < Iron 3BA Replacer < Crimson BodySlide < Damaged Pack
    profile.mods = parse_modlist("+Damaged Pack\n+Crimson BodySlide\n+Iron 3BA Replacer\n+Crimson Outfit\n");
    profile.plugins = parse_plugins_txt("");
    profile.load_order = parse_loadorder_txt("Skyrim.esm\n");
  }
};

}  // namespace

TEST_CASE("RES-01: Mod NIF/DDS without ESP is discovered as loose-mesh with equippable=false", "[outfit][loose][RES-01]") {
  LooseTestFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  LooseOutfitIndexOptions options;
  // Vanilla iron cuirass is registered in Skyrim.esm, but Crimson Outfit is NOT
  options.registered_mesh_to_armo["meshes\\armor\\iron\\cuirass_1.nif"] = "012E49:Skyrim.esm";

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, options);

  // Find Crimson dress resource
  auto it = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.data_relative_path.find("dress_1.nif") != std::string::npos;
  });

  REQUIRE(it != result.resources.end());
  CHECK(it->kind == "loose-mesh");
  CHECK(it->winning_provider == "Crimson Outfit");
  CHECK(it->body_family == "CBBE 3BA");
  CHECK(it->category == "cuirass");
  CHECK_FALSE(it->capabilities.equippable);  // Cannot equip loose mesh directly in RC-A
  CHECK(it->capabilities.discovered);
  CHECK(it->capabilities.previewable);
  CHECK(it->association.armor_form_key.empty());  // NO invented FormKey!
  CHECK(it->association.confidence == "unconfirmed");
  // Grouping check: both _0 and _1 and tri are in associated_files
  CHECK(it->associated_files.size() >= 2);
}

TEST_CASE("RES-02: Armor replacer without ESP links to the replaced ARMO record", "[outfit][loose][RES-02]") {
  LooseTestFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  LooseOutfitIndexOptions options;
  options.registered_mesh_to_armo["meshes\\armor\\iron\\cuirass_1.nif"] = "012E49:Skyrim.esm";

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, options);

  // Find iron cuirass resource
  auto it = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.data_relative_path.find("cuirass_1.nif") != std::string::npos;
  });

  REQUIRE(it != result.resources.end());
  CHECK(it->kind == "replacer-mesh");
  CHECK(it->winning_provider == "Iron 3BA Replacer");  // High priority mod overrides vanilla
  CHECK(it->association.armor_form_key == "012E49:Skyrim.esm");
  CHECK(it->association.confidence == "confirmed");
  CHECK(it->body_family == "CBBE 3BA");
  CHECK_FALSE(it->capabilities.equippable);  // In loose view, direct equipping is blocked
}

TEST_CASE("RES-03: Corrupted NIF and path backtracking are safely rejected with diagnostics", "[outfit][loose][RES-03]") {
  // Test 1: is_safe_data_path rejects unsafe paths
  CHECK_FALSE(is_safe_data_path("../outside/mod.nif"));
  CHECK_FALSE(is_safe_data_path("C:\\Windows\\System32\\calc.exe"));
  CHECK_FALSE(is_safe_data_path("/etc/shadow"));
  CHECK_FALSE(is_safe_data_path(""));
  CHECK(is_safe_data_path("meshes\\armor\\valid.nif"));

  // Test 2: Damaged NIF in fixture is diagnosed and previewable is set to false
  LooseTestFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, {});

  auto it = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.data_relative_path.find("corrupt_1.nif") != std::string::npos;
  });

  REQUIRE(it != result.resources.end());
  CHECK_FALSE(it->capabilities.previewable);
  REQUIRE_FALSE(it->issues.empty());
  CHECK(it->issues[0].find("dañado") != std::string::npos);
}

TEST_CASE("Loose Outfit Indexer: BodySlide OSP project parsing and family classification", "[outfit][loose][bodyslide]") {
  LooseTestFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, {});

  auto it = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.kind == "bodyslide-project";
  });

  REQUIRE(it != result.resources.end());
  CHECK(it->display_name == "Crimson BodySlide Boots");
  CHECK(it->winning_provider == "Crimson BodySlide");
  CHECK(it->category == "boots");
  CHECK(it->body_family == "CBBE 3BA");
  CHECK(it->capabilities.convertible == "requires-bodyslide-build");
  CHECK_FALSE(it->capabilities.previewable);
  CHECK_FALSE(it->capabilities.equippable);
}

TEST_CASE("Loose Outfit Indexer: BodySlide project with pre-built mesh in VFS is previewable", "[outfit][loose][bodyslide]") {
  LooseTestFixture f;
  f.dir.write("mods/Crimson BodySlide/Meshes/Armor/crimson/boots_1.nif", make_test_nif("3BA_Boots"));
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, {});

  auto it = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.kind == "bodyslide-project" && r.display_name == "Crimson BodySlide Boots";
  });

  REQUIRE(it != result.resources.end());
  CHECK(it->capabilities.previewable);
  CHECK(it->data_relative_path.find("boots_1.nif") != std::string::npos);
}

TEST_CASE("Universal Resource Explorer: weight-variant ARMO replacers, DDS textures, weapons, FaceGen, and unsupported files", "[outfit][loose][universal]") {
  LooseTestFixture f;
  f.dir.write("mods/Crimson Outfit/meshes/weapons/crimson/sword.nif", make_test_nif("CrimsonSword"));
  f.dir.write("mods/Crimson Outfit/meshes/actors/character/facegendata/facegeom/Skyrim.esm/00013478.nif", make_test_nif("FaceHead"));
  f.dir.write("mods/Crimson Outfit/textures/actors/character/facegendata/facetint/Skyrim.esm/00013478.dds", std::string(256, 'D'));
  f.dir.write("mods/Crimson Outfit/scripts/crimson_quest.pex", "PEX_BINARY_DATA");
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  LooseOutfitIndexOptions options;
  // Register _0.nif only; indexer should still match primary _1.nif as a confirmed replacer-mesh!
  options.registered_mesh_to_armo["meshes\\armor\\iron\\cuirass_0.nif"] = "012E49:Skyrim.esm";

  const auto result = LooseOutfitIndexer::index(vfs, nullptr, options);

  CHECK(result.replacer_mesh_count >= 1);
  CHECK(result.weapon_object_mesh_count >= 1);
  CHECK(result.facegen_count >= 2);
  CHECK(result.texture_dds_count >= 1);
  CHECK(result.unsupported_file_count >= 1);

  auto it_weap = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.kind == "weapon-mesh";
  });
  REQUIRE(it_weap != result.resources.end());
  CHECK(it_weap->capabilities.previewable);

  auto it_unsup = std::find_if(result.resources.begin(), result.resources.end(), [](const LooseResource& r) {
    return r.kind == "unsupported-file";
  });
  REQUIRE(it_unsup != result.resources.end());
  CHECK_FALSE(it_unsup->capabilities.previewable);
  CHECK(it_unsup->capabilities.convertible == "UNSUPPORTED_PREVIEW");
}
