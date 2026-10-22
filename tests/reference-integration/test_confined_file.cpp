#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "sns/mo2/confined_file.hpp"

using sns::mo2::ConfineResult;
using sns::mo2::confined_file;
namespace fs = std::filesystem;

namespace {

// A scratch folder of its own under %TEMP%, removed when the test ends. Nothing else on disk is touched.
struct Scratch {
  fs::path dir;
  Scratch() {
    dir = fs::temp_directory_path() / ("sns-confined-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  fs::path make_file(const fs::path& relative, const std::string& content = "NIF") const {
    const auto file = dir / relative;
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << content;
    return file;
  }
};

bool make_junction(const fs::path& link, const fs::path& target) {
  const auto command = "cmd /c \"mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >nul 2>nul\"";
  return std::system(command.c_str()) == 0 && fs::exists(link);
}

}  // namespace

TEST_CASE("Confined file: a regular file of the mod is found, whatever the case or the separator", "[confined]") {
  Scratch s;
  const auto root = s.dir / "mod";
  const auto file = s.make_file("mod/meshes/actors/character/facegendata/facegeom/skyrim.esm/00013364.nif");
  const auto found = confined_file(root, "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\00013364.nif");
  REQUIRE(found.result == ConfineResult::Found);
  CHECK(fs::equivalent(found.path, file));
  CHECK(confined_file(root, "MESHES/Actors/Character/FaceGenData/FaceGeom/Skyrim.esm/00013364.NIF").result == ConfineResult::Found);
}

TEST_CASE("Confined file: a mod without the file is Absent, not an incident", "[confined]") {
  Scratch s;
  s.make_file("mod/meshes/other.nif");
  CHECK(confined_file(s.dir / "mod", "meshes\\none.nif").result == ConfineResult::Absent);
  CHECK(confined_file(s.dir / "mod", "textures\\deep\\missing\\folder\\x.dds").result == ConfineResult::Absent);
  CHECK(confined_file(s.dir / "no-such-mod", "meshes\\other.nif").result == ConfineResult::Absent);
}

TEST_CASE("Confined file: hostile data paths are refused before the disk is looked at", "[confined]") {
  Scratch s;
  s.make_file("mod/meshes/x.nif");
  s.make_file("secret.nif");  // one level above the mod folder
  for (const auto* path : {"", "..\\secret.nif", "../secret.nif", "meshes\\..\\..\\secret.nif", "..", "\\secret.nif", "/secret.nif",
                           "C:\\Windows\\win.ini", "C:win.ini", "meshes\\x.nif:hidden-stream", "\\\\server\\share\\x.nif"}) {
    INFO("data path: '" << path << "'");
    CHECK(confined_file(s.dir / "mod", path).result == ConfineResult::UnsafePath);
  }
}

TEST_CASE("Confined file: invalid UTF-8 is an unsafe path, not an exception", "[confined]") {
  Scratch s;
  s.make_file("mod/meshes/x.nif");
  CHECK_NOTHROW(confined_file(s.dir / "mod", "meshes\\\xC3\x28.nif"));
  CHECK(confined_file(s.dir / "mod", "meshes\\\xC3\x28.nif").result == ConfineResult::UnsafePath);
}

TEST_CASE("Confined file: a folder is not a file", "[confined]") {
  Scratch s;
  fs::create_directories(s.dir / "mod" / "meshes" / "folder.nif");
  CHECK(confined_file(s.dir / "mod", "meshes\\folder.nif").result == ConfineResult::NotRegularFile);
}

TEST_CASE("Confined file: a junction inside the mod that points outside is refused", "[confined]") {
  Scratch s;
  s.make_file("outside/secret.nif");
  fs::create_directories(s.dir / "mod" / "meshes");
  if (!make_junction(s.dir / "mod" / "meshes" / "link", s.dir / "outside")) SKIP("cannot create a junction here (mklink /J failed)");
  // The data path looks innocent and the file is a regular file, but its real location is outside the mod folder.
  CHECK(confined_file(s.dir / "mod", "meshes\\link\\secret.nif").result == ConfineResult::EscapesFolder);
}

TEST_CASE("Confined file: a junction that stays inside the mod is fine", "[confined]") {
  Scratch s;
  s.make_file("mod/real/x.nif");
  fs::create_directories(s.dir / "mod" / "meshes");
  if (!make_junction(s.dir / "mod" / "meshes" / "alias", s.dir / "mod" / "real")) SKIP("cannot create a junction here (mklink /J failed)");
  CHECK(confined_file(s.dir / "mod", "meshes\\alias\\x.nif").result == ConfineResult::Found);
}

TEST_CASE("Confined file: a symbolic link is refused", "[confined]") {
  Scratch s;
  const auto target = s.make_file("outside/secret.nif");
  fs::create_directories(s.dir / "mod" / "meshes");
  std::error_code ec;
  fs::create_symlink(target, s.dir / "mod" / "meshes" / "link.nif", ec);
  if (ec) SKIP("cannot create a symbolic link here (needs developer mode or privilege): " << ec.message());
  CHECK(confined_file(s.dir / "mod", "meshes\\link.nif").result == ConfineResult::SymbolicLink);
}

TEST_CASE("Confined file: a sibling folder with the same prefix is not 'inside'", "[confined]") {
  Scratch s;
  s.make_file("mod-evil/meshes/x.nif");
  fs::create_directories(s.dir / "mod" / "meshes");
  if (!make_junction(s.dir / "mod" / "meshes" / "link", s.dir / "mod-evil" / "meshes")) SKIP("cannot create a junction here (mklink /J failed)");
  // "...\mod-evil\..." starts with "...\mod" as text, but is another folder.
  CHECK(confined_file(s.dir / "mod", "meshes\\link\\x.nif").result == ConfineResult::EscapesFolder);
}

TEST_CASE("Confined file: non-ASCII names work", "[confined]") {
  Scratch s;
  // "cabeza-ñandú.nif" spelled with explicit UTF-8 bytes (the source stays ASCII whatever the compiler's encoding).
  std::u8string name = u8"cabeza-";
  name += static_cast<char8_t>(0xC3);
  name += static_cast<char8_t>(0xB1);
  name += u8"and";
  name += static_cast<char8_t>(0xC3);
  name += static_cast<char8_t>(0xBA);
  name += u8".nif";
  s.make_file(fs::path(u8"mod") / u8"meshes" / fs::path(name));
  const auto found = confined_file(s.dir / "mod", "meshes\\cabeza-\xC3\xB1" "and\xC3\xBA.nif");
  CHECK(found.result == ConfineResult::Found);
}

TEST_CASE("Confined file: result names are stable", "[confined]") {
  CHECK(sns::mo2::to_string(ConfineResult::Found) == "found");
  CHECK(sns::mo2::to_string(ConfineResult::EscapesFolder) == "escapes-folder");
  CHECK(sns::mo2::to_string(ConfineResult::SymbolicLink) == "symbolic-link");
}
