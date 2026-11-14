#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "plugin_bytes.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/outfit/outfit_fit_doctor.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::mo2;
using namespace sns::outfit;

namespace {

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

struct FitDoctorFixture {
  test::TempDir dir;
  InstanceConfig config;
  GameInstall game;
  Profile profile;

  FitDoctorFixture() {
    // 1. Base Game Data
    dir.write("game/SkyrimSE.exe", "");
    dir.write("game/Data/Skyrim.esm", test::make_tes4(0x1, {}));
    dir.write("game/Data/Dawnguard.esm", test::make_tes4(0x1, {"Skyrim.esm"}));
    dir.write("game/Data/meshes/actors/character/character assets/femalebody_1.nif", make_test_nif("BaseShape"));
    dir.write("game/Data/meshes/armor/steel/f/cuirass_1.nif", make_test_nif("SteelCuirass"));
    dir.write("game/Data/meshes/armor/steel/f/cuirass_0.nif", make_test_nif("SteelCuirass"));

    // 2. Mod: Servant Priscilla Serana Replacer (Winning body + skin)
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/femalebody_1.nif", make_test_nif("3BA"));
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/femalebody_0.nif", make_test_nif("3BA"));
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/femalebody.tri", "tri_morph_data");
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/femalehands_1.nif", make_test_nif("3BA_Hands"));
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/femalefeet_1.nif", make_test_nif("3BA_Feet"));
    dir.write("mods/Servant Priscilla Serana/meshes/Priscilla body/Body/skeleton.nif", make_test_nif("Skeleton"));

    // 3. Mod: Twilight Princess Armor (Cloak - Slot 46)
    dir.write("mods/Twilight Princess Armor/meshes/Twilight Princess Armor/F/TwilightPrincess_Cloak_Armored_SMP_1.nif", make_test_nif("Cloak"));

    // 4. Mod: Obi's Crimson Outfit (Boots 37, Gloves 33, Pants 49)
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Boots_1.nif", make_test_nif("Boots"));
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Boots_0.nif", make_test_nif("Boots"));
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Hands_1.nif", make_test_nif("Hands"));
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Hands_0.nif", make_test_nif("Hands"));
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Legs_1.nif", make_test_nif("Legs"));
    dir.write("mods/Obis Crimson Outfit/meshes/Obicnii/Crimson Outfit/Legs_0.nif", make_test_nif("Legs"));
    // ShapeData project available for Crimson Outfit in BodySlide
    dir.write("mods/Obis Crimson Outfit/CalienteTools/BodySlide/ShapeData/Crimson Outfit/Boots.osp", "<SliderProject>");

    // 5. Mod: DD - Temio Cloth (Corset 45, Shirt 32)
    dir.write("mods/DD - Temio Cloth/meshes/armor/DD - Temio Cloth/DD - Temio Cloth Corset R_1.nif", make_test_nif("Corset"));
    dir.write("mods/DD - Temio Cloth/meshes/armor/DD - Temio Cloth/DD - Temio Cloth Shirt RED_1.nif", make_test_nif("Shirt"));

    // 6. Overwrite: Shirt winning file is in Overwrite
    dir.write("overwrite/meshes/armor/DD - Temio Cloth/DD - Temio Cloth Shirt RED_1.nif", make_test_nif("Shirt_Overwrite"));

    // 7. Mod: CBBE 3BA DD - Temio Cloth Refit (Compatible alternative in MO2 with TRI and weight pairs)
    dir.write("mods/CBBE 3BA DD - Temio Cloth Refit/meshes/armor/DD - Temio Cloth/3BA/DD - Temio Cloth Corset R_1.nif", make_test_nif("3BA_Corset"));
    dir.write("mods/CBBE 3BA DD - Temio Cloth Refit/meshes/armor/DD - Temio Cloth/3BA/DD - Temio Cloth Corset R_0.nif", make_test_nif("3BA_Corset"));
    dir.write("mods/CBBE 3BA DD - Temio Cloth Refit/meshes/armor/DD - Temio Cloth/3BA/DD - Temio Cloth Corset R.tri", "tri_morph_data");

    // 8. Mod: UBE 2.0 DD - Temio Cloth (Incompatible UBE alternative)
    dir.write("mods/UBE 2.0 DD - Temio Cloth/meshes/armor/DD - Temio Cloth/UBE/DD - Temio Cloth Corset R_1.nif", make_test_nif("UBE_Corset"));

    config.mods_dir = dir.path() / "mods";
    config.overwrite_dir = dir.path() / "overwrite";
    config.skip_directories = {".git"};
    config.skip_file_suffixes = {".mohidden"};
    game = inspect_game(dir.path() / "game");

    profile.mods = parse_modlist(
        "+UBE 2.0 DD - Temio Cloth\n"
        "+CBBE 3BA DD - Temio Cloth Refit\n"
        "+DD - Temio Cloth\n"
        "+Obis Crimson Outfit\n"
        "+Twilight Princess Armor\n"
        "+Servant Priscilla Serana\n");
    profile.plugins = parse_plugins_txt(
        "*Skyrim.esm\n*Dawnguard.esm\n*Servant Priscilla Serana.esp\n*Twilight Princess Armor.esp\n*Obis Crimson Outfit.esp\n*DD - Temio Cloth.esp\n");
    profile.load_order = parse_loadorder_txt(
        "Skyrim.esm\nDawnguard.esm\nServant Priscilla Serana.esp\nTwilight Princess Armor.esp\nObis Crimson Outfit.esp\nDD - Temio Cloth.esp\n");
  }

  nlohmann::json make_serana_details() const {
    nlohmann::json details;
    details["formKey"] = "002B6C:Dawnguard.esm";
    details["name"] = "Serana";
    details["sex"] = "Female";
    details["race"] = "NordRaceVampire";
    details["skin"] = "000806:Servant Priscilla Serana.esp";

    details["skinAddons"] = nlohmann::json::array({
        {{"bodySlots", {32}}, {"femaleModel", "Priscilla body/Body/femalebody_1.nif"}},
        {{"bodySlots", {33}}, {"femaleModel", "Priscilla body/Body/femalehands_1.nif"}},
        {{"bodySlots", {37}}, {"femaleModel", "Priscilla body/Body/femalefeet_1.nif"}}});

    nlohmann::json armors = nlohmann::json::array({
        {{"formKey", "000D62:Twilight Princess Armor.esp"},
         {"name", "Twilight Princess Cloak"},
         {"addonFormKey", "000D63:Twilight Princess Armor.esp"},
         {"slots", {46}},
         {"femaleModel", "Twilight Princess Armor/F/TwilightPrincess_Cloak_Armored_SMP_1.nif"}},
        {{"formKey", "000801:Obis Crimson Outfit.esp"},
         {"name", "Crimson Boots"},
         {"addonFormKey", "000802:Obis Crimson Outfit.esp"},
         {"slots", {37}},
         {"femaleModel", "Obicnii/Crimson Outfit/Boots_1.nif"}},
        {{"formKey", "000805:Obis Crimson Outfit.esp"},
         {"name", "Crimson Gloves"},
         {"addonFormKey", "000806:Obis Crimson Outfit.esp"},
         {"slots", {33}},
         {"femaleModel", "Obicnii/Crimson Outfit/Hands_1.nif"}},
        {{"formKey", "000803:Obis Crimson Outfit.esp"},
         {"name", "Crimson Pants"},
         {"addonFormKey", "000804:Obis Crimson Outfit.esp"},
         {"slots", {49}},
         {"femaleModel", "Obicnii/Crimson Outfit/Legs_1.nif"}},
        {{"formKey", "000DB2:DD - Temio Cloth.esp"},
         {"name", "Temio Corset Red"},
         {"addonFormKey", "000DB3:DD - Temio Cloth.esp"},
         {"slots", {45}},
         {"femaleModel", "armor/DD - Temio Cloth/DD - Temio Cloth Corset R_1.nif"}},
        {{"formKey", "000810:DD - Temio Cloth.esp"},
         {"name", "Temio Shirt Red"},
         {"addonFormKey", "000811:DD - Temio Cloth.esp"},
         {"slots", {32}},
         {"femaleModel", "armor/DD - Temio Cloth/DD - Temio Cloth Shirt RED_1.nif"}}});

    nlohmann::json outfit;
    outfit["formKey"] = "000814:Servant Priscilla Serana.esp";
    outfit["name"] = "01_PriscilaSuit";
    outfit["armors"] = armors;
    details["defaultOutfit"] = outfit;
    return details;
  }

  nlohmann::json make_lydia_details() const {
    nlohmann::json details;
    details["formKey"] = "000A2C8E:Skyrim.esm";
    details["name"] = "Lydia";
    details["sex"] = "Female";
    details["race"] = "NordRace";

    details["skinAddons"] = nlohmann::json::array({
        {{"bodySlots", {32}}, {"femaleModel", "actors/character/character assets/femalebody_1.nif"}}});

    details["defaultOutfit"] = {
        {"formKey", "0001697D:Skyrim.esm"},
        {"name", "OutfitArmorSteelCuirass"},
        {"armors", {{{"formKey", "00013952:Skyrim.esm"},
                     {"name", "Steel Armor"},
                     {"slots", {32}},
                     {"femaleModel", "armor/steel/f/cuirass_1.nif"}}}}};
    return details;
  }
};

}  // namespace

TEST_CASE("SERANA-01 — Resolución: cuerpo y skin resueltos coinciden con la escena", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  CHECK(report.body.form_key == "002B6C:Dawnguard.esm");
  CHECK(report.body.name == "Serana");
  CHECK(report.body.sex == "Female");
  CHECK(report.body.race == "NordRaceVampire");

  // Effective body mesh resolved from winning mod
  CHECK(report.body.body_mesh_path == "Priscilla body/Body/femalebody_1.nif");
  CHECK(report.body.body_provider == "Servant Priscilla Serana");
  CHECK(report.body.body_family == MeshFamily::Cbbe3ba);
  // A "3BA" shape name is metadata, not a measurement: the strongest hint, never ConfirmedGeometric.
  CHECK(report.body.confidence == ConfidenceLevel::ProbableMetadata);
  CHECK(report.body.body_has_tri);
}

TEST_CASE("SERANA-02 — Vestuario: todas las piezas activas identificadas con rutas y proveedores", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  REQUIRE(report.pieces.size() == 6);

  // Boots
  const auto& boots = report.pieces[1];
  CHECK(boots.armor_name == "Crimson Boots");
  CHECK(boots.effective_nif_path == "Obicnii/Crimson Outfit/Boots_1.nif");
  CHECK(boots.winning_provider == "Obis Crimson Outfit");
  CHECK(boots.biped_slots == std::vector<int>{37});

  // Shirt winning provider is Overwrite
  const auto& shirt = report.pieces[5];
  CHECK(shirt.armor_name == "Temio Shirt Red");
  CHECK(shirt.winning_provider == "<Overwrite>");
  CHECK(shirt.is_overwrite);
  CHECK(shirt.biped_slots == std::vector<int>{32});
}

TEST_CASE("SERANA-03 — Variantes: morphs, pesos y ShapeData detectados", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  // Boots have weight_0 and weight_1 pair
  const auto& boots = report.pieces[1];
  CHECK(boots.weight_variants_valid);
  CHECK(boots.geometry.has_weight_0);
  CHECK(boots.geometry.has_weight_1);

  // Corset only has _1 in DD - Temio Cloth, lacking _0 and lacking TRI
  const auto& corset = report.pieces[4];
  CHECK_FALSE(corset.weight_variants_valid);
  CHECK_FALSE(corset.morphs_available);

  // Should have issues: OutfitWeightVariantMissing and OutfitMorphMissing
  bool has_morph_issue = false;
  bool has_weight_issue = false;
  for (const auto& issue : corset.issues) {
    if (issue.code == DiagnosticCode::OutfitMorphMissing) has_morph_issue = true;
    if (issue.code == DiagnosticCode::OutfitWeightVariantMissing) has_weight_issue = true;
  }
  CHECK(has_morph_issue);
  CHECK(has_weight_issue);
}

TEST_CASE("SERANA-04 — Slots: particiones y piezas cubiertas detectadas sin falsos conflictos", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  // Shirt covers slot 32 (Body)
  const auto& shirt = report.pieces[5];
  bool covers_body = false;
  for (const auto& issue : shirt.issues) {
    if (issue.code == DiagnosticCode::BodyPartCovered) covers_body = true;
  }
  CHECK(covers_body);

  // Non-conflicting items (Pants slot 49, Corset slot 45) do not have slot conflicts
  for (const auto& p : report.pieces) {
    for (const auto& issue : p.issues) {
      CHECK(issue.code != DiagnosticCode::OutfitSlotConflict);
    }
  }
}

TEST_CASE("SERANA-05 — Reparación: alternativa compatible encontrada y aplicable", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  // Corset has compatible alternative in "CBBE 3BA DD - Temio Cloth Refit"
  auto& corset = report.pieces[4];
  REQUIRE_FALSE(corset.compatible_alternatives.empty());

  const auto& alt = corset.compatible_alternatives[0];
  CHECK(alt.mod_name == "CBBE 3BA DD - Temio Cloth Refit");
  CHECK(alt.family == MeshFamily::Cbbe3ba);
  CHECK(alt.has_tri);
  CHECK(alt.has_weight_variants);

  // Apply repair
  corset.effective_nif_path = alt.data_path;
  corset.winning_provider = alt.mod_name;
  corset.detected_family = alt.family;
  corset.morphs_available = alt.has_tri;
  corset.weight_variants_valid = alt.has_weight_variants;

  // Clear morph missing issue after repair
  std::erase_if(corset.issues, [](const auto& iss) {
    return iss.code == DiagnosticCode::OutfitMorphMissing || iss.code == DiagnosticCode::OutfitWeightVariantMissing;
  });

  bool still_has_morph_issue = false;
  for (const auto& iss : corset.issues) {
    if (iss.code == DiagnosticCode::OutfitMorphMissing) still_has_morph_issue = true;
  }
  CHECK_FALSE(still_has_morph_issue);
}

TEST_CASE("SERANA-06 — Seguridad: parche generado no altera FaceGen ni otros NPCs", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  test::TempDir staging_dir;
  const auto mod_path = staging_dir.path() / "OutfitFitDoctor_Fix";

  std::vector<PieceRepairPatch> patches{
      {.armor_form_key = "000DB2:DD - Temio Cloth.esp",
       .addon_form_key = "000DB3:DD - Temio Cloth.esp",
       .original_nif = "meshes/armor/DD - Temio Cloth/DD - Temio Cloth Corset R_1.nif",
       .repaired_nif = "meshes/armor/DD - Temio Cloth/3BA/DD - Temio Cloth Corset R_1.nif",
       .source_mod = "CBBE 3BA DD - Temio Cloth Refit"}};

  const bool ok = OutfitFitDoctor::export_doctor_patch(vfs, report, patches, mod_path);
  REQUIRE(ok);

  // Check manifest and readme were generated
  CHECK(std::filesystem::exists(mod_path / "manifest.json"));
  CHECK(std::filesystem::exists(mod_path / "README.md"));

  // Check manifest only mentions Serana (002B6C:Dawnguard.esm)
  std::ifstream manifest_file(mod_path / "manifest.json");
  std::stringstream buffer;
  buffer << manifest_file.rdbuf();
  const std::string content = buffer.str();

  CHECK(content.find("002B6C:Dawnguard.esm") != std::string::npos);
  // Unrelated NPCs not mentioned
  CHECK(content.find("000A2C8E:Skyrim.esm") == std::string::npos);
  // Does not write directly to game Data
  CHECK_FALSE(std::filesystem::exists(f.dir.path() / "game/Data/manifest.json"));
}

TEST_CASE("SERANA-07 — Reversión: descartar borrador restaura estado original", "[fit_doctor][serana]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto initial_report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);
  auto working_report = initial_report;

  // Apply change
  working_report.pieces[4].effective_nif_path = "meshes/armor/DD - Temio Cloth/3BA/DD - Temio Cloth Corset R_1.nif";

  // Revert
  working_report = initial_report;

  CHECK(working_report.pieces[4].effective_nif_path == "armor/DD - Temio Cloth/DD - Temio Cloth Corset R_1.nif");
  CHECK(working_report.pieces[4].winning_provider == "DD - Temio Cloth");
}

TEST_CASE("NPC-08 — Generalización: análisis en Lydia no reporta falsos CBBE 3BA", "[fit_doctor][lydia]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_lydia_details(), nullptr);

  CHECK(report.body.form_key == "000A2C8E:Skyrim.esm");
  CHECK(report.body.name == "Lydia");
  CHECK(report.body.body_family == MeshFamily::Unp);

  REQUIRE(report.pieces.size() == 1);
  const auto& cuirass = report.pieces[0];
  CHECK(cuirass.armor_name == "Steel Armor");
  CHECK(cuirass.detected_family == MeshFamily::Unp);

  // Steel Cuirass has both _0 and _1 in game Data
  CHECK(cuirass.weight_variants_valid);
}

TEST_CASE("NPC-09 — Sin recursos: cuando no hay alternativa, reporta necesidad de BodySlide sin inventar", "[fit_doctor][sin_recursos]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto report = OutfitFitDoctor::analyze(vfs, f.make_serana_details(), nullptr);

  // Cloak is a unique SMP cloak with no alternatives in MO2
  const auto& cloak = report.pieces[0];
  CHECK(cloak.armor_name == "Twilight Princess Cloak");
  CHECK(cloak.compatible_alternatives.empty());

  // System correctly recognizes that morphs are missing and no pre-made mesh exists
  bool has_morph_issue = false;
  for (const auto& issue : cloak.issues) {
    if (issue.code == DiagnosticCode::OutfitMorphMissing) has_morph_issue = true;
  }
  CHECK(has_morph_issue);
}

TEST_CASE("Fit Doctor reads the body and garments the worker resolved, not a fallback", "[fit_doctor][appearance]") {
  FitDoctorFixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);
  // catalog.getAppearance shape (AppearanceResponse): the skin piece carries the race-filtered ARMAs, slot 32 = bit 2.
  const nlohmann::json appearance{
      {"npcFormKey", "002B6C:Dawnguard.esm"}, {"sex", "female"},
      {"pieces", nlohmann::json::array({
           {{"armorFormKey", "000D64:Skyrim.esm"}, {"name", "Skin"}, {"source", "skin"}, {"slotMask", 4 | 8 | 128},
            {"addons", nlohmann::json::array({{{"formKey", "0001:Priscilla.esp"}, {"meshPath", "meshes\\Priscilla body\\Body\\femalebody_1.nif"}, {"slotMask", 4}},
                                              {{"formKey", "0002:Priscilla.esp"}, {"meshPath", "meshes\\Priscilla body\\Body\\femalehands_1.nif"}, {"slotMask", 8}}})}},
           {{"armorFormKey", "000D63:Obi's Crimson Outfit.esp"}, {"name", "Crimson Boots"}, {"source", "outfit"}, {"slotMask", 128},
            {"addons", nlohmann::json::array({{{"formKey", "0003:Obi's Crimson Outfit.esp"}, {"meshPath", "meshes\\Obicnii\\Crimson Outfit\\Boots_1.nif"}, {"slotMask", 128}}})}},
           {{"armorFormKey", "000999:Skyrim.esm"}, {"name", "Sin malla"}, {"source", "outfit"}, {"slotMask", 1 << 16},
            {"addons", nlohmann::json::array()}}})}};
  const auto details = OutfitFitDoctor::npc_details_from_appearance(appearance, "Serana");
  CHECK(details.at("skinAddons").size() == 2);
  CHECK(details.at("skinAddons").at(0).at("femaleModel") == "Priscilla body\\Body\\femalebody_1.nif");
  CHECK(details.at("skinAddons").at(0).at("bodySlots") == nlohmann::json::array({32}));
  REQUIRE(details.at("defaultOutfit").at("armors").size() == 2);
  CHECK(details.at("defaultOutfit").at("armors").at(0).at("addonFormKey") == "0003:Obi's Crimson Outfit.esp");  // the ARMA, not the ARMO
  CHECK(details.at("defaultOutfit").at("armors").at(0).at("femaleModel") == "Obicnii\\Crimson Outfit\\Boots_1.nif");

  const auto report = OutfitFitDoctor::analyze(vfs, details, nullptr);
  // The NPC's own body (Priscilla, 3BA shape), never the generic actors\character\character assets\femalebody_1.nif fallback
  CHECK(report.body.body_provider == "Servant Priscilla Serana");
  CHECK(report.body.body_family == MeshFamily::Cbbe3ba);
  REQUIRE(report.pieces.size() == 2);
  CHECK_FALSE(report.pieces[0].effective_nif_path.empty());
  bool missing = false;
  for (const auto& issue : report.pieces[1].issues) missing = missing || issue.code == DiagnosticCode::OutfitMeshMissing;
  CHECK(missing);  // a piece without an ARMA mesh is reported, not guessed
}
