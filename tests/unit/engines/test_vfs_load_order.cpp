#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "plugin_bytes.hpp"
#include "sns/mo2/load_order.hpp"
#include "sns/mo2/vfs.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::mo2;

namespace {

bool has_diag(const EffectiveLoadOrder& lo, std::string_view code, std::string_view subject = {}) {
  return std::any_of(lo.diagnostics.begin(), lo.diagnostics.end(), [&](const Diagnostic& d) {
    return d.code == code && (subject.empty() || d.subject == subject);
  });
}

std::vector<std::string> names(const std::vector<ResolvedPlugin>& plugins) {
  std::vector<std::string> out;
  for (const auto& p : plugins) out.push_back(p.name);
  return out;
}

// Synthetic MO2 instance: game Data + 3 mods + overwrite.
struct Mo2Fixture {
  test::TempDir dir;
  InstanceConfig config;
  GameInstall game;
  Profile profile;

  Mo2Fixture() {
    dir.write("game/SkyrimSE.exe", "");
    dir.write("game/Data/Skyrim.esm", test::make_tes4(0x1, {}));
    dir.write("game/Data/Update.esm", test::make_tes4(0x1, {"Skyrim.esm"}));
    dir.write("game/Data/ccBGSSSE001-Fish.esm", test::make_tes4(0x201, {"Skyrim.esm"}));
    dir.write("game/Skyrim.ccc", "ccBGSSSE001-Fish.esm\r\nccNotInstalled.esl\r\n");
    dir.write("game/Data/textures/sky.dds", "vanilla");

    dir.write("mods/Armor Pack/ArmorPack.esp", test::make_tes4(0x200, {"Skyrim.esm"}));
    dir.write("mods/Armor Pack/meshes/armor/cuirass.nif", "pack");
    dir.write("mods/Armor Pack/textures/SKY.dds", "pack");
    dir.write("mods/Armor Pack/meta.ini", "[General]\n");
    dir.write("mods/Armor Pack/.git/config", "x");
    dir.write("mods/Retex/textures/sky.dds", "retex");
    dir.write("mods/Retex/meshes/armor/cuirass.nif.mohidden", "hidden");
    dir.write("mods/Late Master/LateMaster.esm", test::make_tes4(0x1, {"Skyrim.esm"}));
    dir.write("mods/Disabled/textures/sky.dds", "disabled");
    dir.write("mods/Patch/Patch.esp", test::make_tes4(0, {"Skyrim.esm", "ArmorPack.esp", "Missing.esm"}));
    dir.write("overwrite/textures/sky.dds", "overwrite");
    dir.write("mods/Unlisted/Unlisted.esp", test::make_tes4(0, {"Skyrim.esm"}));

    config.mods_dir = dir.path() / "mods";
    config.overwrite_dir = dir.path() / "overwrite";
    config.skip_directories = {".git"};
    config.skip_file_suffixes = {".mohidden"};
    game = inspect_game(dir.path() / "game");

    profile.mods = parse_modlist(
        "+Patch\n+Unlisted\n-Disabled\n+Retex\n+Late Master\n+Armor Pack\n*DLC: Fish\n");
    profile.plugins = parse_plugins_txt("*ArmorPack.esp\n*Patch.esp\n*LateMaster.esm\nUnlisted.esp\n");
    profile.load_order = parse_loadorder_txt("Skyrim.esm\nUpdate.esm\nccBGSSSE001-Fish.esm\nLateMaster.esm\nArmorPack.esp\nPatch.esp\n");
  }
};

}  // namespace

TEST_CASE("VFS: highest priority provider wins, overwrite beats everything", "[mo2][vfs]") {
  Mo2Fixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);

  const auto sky = vfs.resolve("TEXTURES/Sky.DDS");
  REQUIRE(sky);
  CHECK(vfs.providers()[sky->provider].name == "<Overwrite>");
  const auto chain = vfs.providers_of("textures\\sky.dds");
  REQUIRE(chain.size() == 4);  // game, Armor Pack, Retex, overwrite (Disabled excluded)
  CHECK(vfs.providers()[chain[1]].name == "Armor Pack");
  CHECK(vfs.providers()[chain[2]].name == "Retex");

  CHECK(vfs.providers()[vfs.resolve("meshes/armor/cuirass.nif")->provider].name == "Armor Pack");
  CHECK_FALSE(vfs.resolve("meta.ini"));             // mod metadata is not deployed
  CHECK_FALSE(vfs.resolve(".git/config"));          // skip_directories
  CHECK(vfs.providers_of("meshes/armor/cuirass.nif").size() == 1);  // .mohidden ignored
}

TEST_CASE("VFS: conflict report per mod", "[mo2][vfs]") {
  Mo2Fixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);
  const auto retex = *vfs.find_provider("Retex");
  const auto c = vfs.conflicts(retex);
  CHECK(c.files == 1);
  CHECK(c.overridden_total == 1);  // overwrite wins sky.dds
  CHECK(c.overrides_total == 0);

  const auto pack = vfs.conflicts(*vfs.find_provider("Armor Pack"));
  // Display case comes from the first (lowest) provider that has the file: the game's Data.
  CHECK(pack.overridden == std::vector<std::string>{"textures\\sky.dds"});
}

TEST_CASE("Load order: implicit masters, CC, master hoisting and diagnostics", "[mo2][loadorder]") {
  Mo2Fixture f;
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);
  const auto lo = resolve_load_order(f.profile, vfs, f.game);

  CHECK(names(lo.active) == std::vector<std::string>{"Skyrim.esm", "Update.esm", "ccBGSSSE001-Fish.esm",
                                                     "LateMaster.esm", "ArmorPack.esp", "Patch.esp"});
  CHECK(lo.active[0].implicit);
  CHECK(lo.active[2].implicit);                 // from Skyrim.ccc
  CHECK(lo.active[4].provider == "Armor Pack");
  CHECK(lo.active[4].header->is_light);
  CHECK(has_diag(lo, "MasterHoisted", "LateMaster.esm"));
  CHECK(has_diag(lo, "MissingMaster", "Patch.esp"));
  CHECK(has_diag(lo, "UnlistedPlugin") == false);  // Unlisted.esp is listed (inactive) in plugins.txt
  CHECK(names(lo.inactive) == std::vector<std::string>{"Unlisted.esp"});
  CHECK_FALSE(has_diag(lo, "LoadOrderTxtMismatch"));
}

TEST_CASE("Load order: missing files and loadorder.txt disagreement", "[mo2][loadorder]") {
  Mo2Fixture f;
  f.profile.plugins.push_back({"Ghost.esp", true});
  f.profile.load_order = parse_loadorder_txt("Skyrim.esm\nPatch.esp\nArmorPack.esp\n");
  const auto vfs = VirtualFileSystem::build(f.config, f.profile, f.game);
  const auto lo = resolve_load_order(f.profile, vfs, f.game);
  CHECK(has_diag(lo, "PluginMissing", "Ghost.esp"));
  CHECK(has_diag(lo, "LoadOrderTxtMismatch"));
}
