#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <cstdlib>

#include "export_destination.hpp"
#include "temp_dir.hpp"

using namespace sns::app;

namespace {

QString qpath(const std::filesystem::path& p) { return QDir::toNativeSeparators(QString::fromStdWString(p.wstring())); }

// The parts of an MO2 snapshot forbidden_roots_of() reads (no MO2 headers, no real profile).
struct FakeSnapshot {
  struct {
    std::filesystem::path root;
  } game;
  struct {
    struct {
      std::filesystem::path root;
    } info;
    std::filesystem::path game_path, base_dir, mods_dir, overwrite_dir, profiles_dir, downloads_dir;
  } config;
};

// A portable MO2 instance and a game folder, synthetic, under a temporary folder.
struct Layout {
  sns::test::TempDir tmp;
  FakeSnapshot snap;
  Layout() {
    tmp.write("Skyrim/Data/Skyrim.esm", "TES4");
    tmp.write("MO2/ModOrganizer.ini", "[General]\n");
    tmp.write("MO2/mods/SomeMod/meta.ini", "[General]\n");
    tmp.write("MO2/overwrite/keep.txt", "x");
    tmp.write("MO2/profiles/Default/modlist.txt", "+SomeMod\n");
    tmp.write("MO2/downloads/a.7z", "x");
    tmp.write("Exports/readme.txt", "x");
    snap.game.root = tmp.path() / "Skyrim";
    snap.config.info.root = tmp.path() / "MO2";
    snap.config.game_path = tmp.path() / "Skyrim";
    snap.config.base_dir = tmp.path() / "MO2";
    snap.config.mods_dir = tmp.path() / "MO2" / "mods";
    snap.config.overwrite_dir = tmp.path() / "MO2" / "overwrite";
    snap.config.profiles_dir = tmp.path() / "MO2" / "profiles";
    snap.config.downloads_dir = tmp.path() / "MO2" / "downloads";
  }
  [[nodiscard]] QString refused(const std::filesystem::path& p) const {
    return refused_export_directory(qpath(p), forbidden_roots_of(snap));
  }
};

}  // namespace

TEST_CASE("Export folders inside the game or MO2 are refused, subfolders included", "[export-destination]") {
  Layout l;
  const auto& t = l.tmp.path();
  CHECK_FALSE(l.refused(t / "MO2" / "mods").isEmpty());
  CHECK_FALSE(l.refused(t / "MO2" / "mods" / "SomeMod").isEmpty());                 // an existing mod
  CHECK_FALSE(l.refused(t / "MO2" / "mods" / "NewMod").isEmpty());                  // a mod that would be created
  CHECK_FALSE(l.refused(t / "MO2" / "overwrite" / "Exports").isEmpty());
  CHECK_FALSE(l.refused(t / "MO2" / "profiles" / "Default").isEmpty());
  CHECK_FALSE(l.refused(t / "MO2" / "downloads").isEmpty());
  CHECK_FALSE(l.refused(t / "MO2").isEmpty());                                      // the instance itself
  CHECK_FALSE(l.refused(t / "Skyrim").isEmpty());
  CHECK_FALSE(l.refused(t / "Skyrim" / "Data" / "Exports").isEmpty());
  // The message names the folder it protects.
  CHECK(l.refused(t / "MO2" / "mods" / "NewMod").contains(QStringLiteral("MO2 mods")));
  // Nothing is created by the check.
  CHECK_FALSE(std::filesystem::exists(t / "MO2" / "mods" / "NewMod"));
}

TEST_CASE("Spelling does not get a path past the check", "[export-destination]") {
  Layout l;
  const auto mods = qpath(l.tmp.path() / "MO2" / "mods");
  const auto roots = forbidden_roots_of(l.snap);
  CHECK_FALSE(refused_export_directory(mods.toUpper() + QStringLiteral("\\X"), roots).isEmpty());   // case
  CHECK_FALSE(refused_export_directory(QDir::fromNativeSeparators(mods) + "/X/", roots).isEmpty());  // '/' and trailing '/'
  CHECK_FALSE(refused_export_directory(mods + QStringLiteral("\\Other\\..\\X"), roots).isEmpty());   // '..'
  CHECK_FALSE(refused_export_directory(QStringLiteral("  ") + mods + QStringLiteral("  "), roots).isEmpty());
  CHECK_FALSE(refused_export_directory(QStringLiteral("relative\\folder"), roots).isEmpty());
  CHECK_FALSE(refused_export_directory(QString(), roots).isEmpty());
}

TEST_CASE("Own folders next to the game and MO2 are allowed", "[export-destination]") {
  Layout l;
  const auto& t = l.tmp.path();
  CHECK(l.refused(t / "Exports").isEmpty());
  CHECK(l.refused(t / "Exports" / "Lydia").isEmpty());          // not created yet
  CHECK(l.refused(t / "MO2-exports").isEmpty());                // shares a prefix with "MO2", is not inside it
  CHECK(l.refused(t / "Skyrim Exports" / "x").isEmpty());
  // Without a profile only the absolute-path rule applies.
  CHECK(refused_export_directory(qpath(t / "Exports"), {}).isEmpty());
}

TEST_CASE("A junction that leads into MO2's mods folder is refused", "[export-destination]") {
  Layout l;
  const auto link = l.tmp.path() / "Shortcut";
  const auto command = QStringLiteral("cmd /c mklink /J \"%1\" \"%2\" >nul 2>&1")
                           .arg(qpath(link), qpath(l.tmp.path() / "MO2" / "mods"))
                           .toStdWString();
  if (_wsystem(command.c_str()) != 0 || !std::filesystem::exists(link / "SomeMod")) SKIP("mklink /J is not available here");
  CHECK_FALSE(l.refused(link).isEmpty());
  CHECK_FALSE(l.refused(link / "NewMod").isEmpty());       // through the junction, to a folder not created yet
  CHECK_FALSE(std::filesystem::exists(link / "NewMod"));
  std::filesystem::remove(link);                          // the junction only, never its target
  CHECK(std::filesystem::exists(l.tmp.path() / "MO2" / "mods" / "SomeMod" / "meta.ini"));
}
