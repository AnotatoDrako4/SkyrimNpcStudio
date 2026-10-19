// Builds a SYNTHETIC portable MO2 instance whose only defect is a damaged FaceGen, for the studio's profile
// scenario of audit F-11 (`damaged-facegen-scan`, run by scripts/damaged-facegen-check.ps1). Usage:
//   sns_synthetic_mo2 <absolute directory that does not exist or is empty>
// Layout under that directory:
//   game/SkyrimSE.exe                 empty placeholder (the studio only checks that it exists)
//   game/Data/*.esm|esp               the synthetic load order of tools/FixtureGen (Lydia, Hert, guards)
//   ModOrganizer.ini                  portable instance, profile "Synthetic"
//   profiles/Synthetic/               modlist.txt (+DamagedFaceGen, +SyntheticMeshes), plugins.txt, loadorder.txt
//   mods/DamagedFaceGen/meshes/.../000A2C8E.nif   Lydia's FaceGen replaced by facegen_lengtherror.nif (72 bytes)
//   mods/SyntheticMeshes/meshes/Armor/Steel/CuirassF_1.nif   one valid triangle (nifly), so the studio has
//                                     something to draw and its automation starts (it needs a non-empty scene)
// The damaged bytes come from tests/support/audit_repro_inputs.hpp; this tool is the only place that writes
// them to disk, and only inside the directory it is given (it refuses a non-empty one, so it cannot be
// pointed at a real game or MO2 folder). Prints a JSON summary on stdout.
#include <NifFile.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "audit_repro_inputs.hpp"
#include "process.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

void write_file(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) throw std::runtime_error("cannot write " + sns::text::path_to_utf8(path));
}

// QSettings @ByteArray(...) value: MO2 stores paths with doubled backslashes.
std::string byte_array(const fs::path& path) {
  std::string text;
  for (const char c : sns::text::path_to_utf8(fs::absolute(path).make_preferred())) {
    text += c;
    if (c == '\\') text += '\\';
  }
  return "@ByteArray(" + text + ")";
}

// A valid Skyrim SE NIF with one triangle: the only drawable mesh of the synthetic profile.
std::string triangle_nif() {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> verts{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
  std::vector<nifly::Triangle> tris{{0, 1, 2}};
  std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {0, 1}};
  if (!file.CreateShapeFromData("SyntheticCuirass", &verts, &tris, &uv)) throw std::runtime_error("nifly could not create the shape");
  std::ostringstream out(std::ios::binary);
  if (file.Save(out) != 0) throw std::runtime_error("nifly could not save the mesh");
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: sns_synthetic_mo2 <absolute directory that does not exist or is empty>\n");
    return 2;
  }
  const fs::path root = sns::text::path_from_utf8(argv[1]);
  std::error_code ec;
  if (!root.is_absolute() || (fs::exists(root, ec) && !fs::is_empty(root, ec))) {
    std::fprintf(stderr, "refused: the directory must be absolute and empty (never a real game or MO2 folder)\n");
    return 2;
  }
  try {
    const auto data = root / "game" / "Data";
    fs::create_directories(data);
    const std::wstring command = L"\"" + sns::text::to_wide(SNS_FIXTURE_GEN_EXE) + L"\" \"" + data.native() + L"\"";
    if (sns::test::run_and_wait(command) != 0) throw std::runtime_error("FixtureGen failed");
    const auto fixture = Json::parse(sns::file_io::read_text_file(data / "fixture.json"));
    fs::remove(data / "fixture.json");
    write_file(root / "game" / "SkyrimSE.exe", "");

    std::string plugins_txt, loadorder_txt;
    for (const auto& plugin : fixture.at("plugins")) {
      const auto name = plugin.at("fileName").get<std::string>();
      loadorder_txt += name + "\r\n";
      if (!sns::text::iequals_ascii(name, "Skyrim.esm")) plugins_txt += "*" + name + "\r\n";  // Skyrim.esm is implicit
    }
    write_file(root / "ModOrganizer.ini", "[General]\r\ngameName=Skyrim Special Edition\r\ngamePath=" + byte_array(root / "game") +
                                              "\r\nselected_profile=@ByteArray(Synthetic)\r\n");
    const auto profile = root / "profiles" / "Synthetic";
    write_file(profile / "modlist.txt", "+DamagedFaceGen\r\n+SyntheticMeshes\r\n");
    write_file(profile / "plugins.txt", plugins_txt);
    write_file(profile / "loadorder.txt", loadorder_txt);

    // Lydia (0A2C8E:Skyrim.esm) is overridden by FaceMod.esp and LateTweak.esp, so the scan analyses her on
    // a pool thread, and her head parts come from another mod than this FaceGen, so it opens the file.
    const auto mod = root / "mods" / "DamagedFaceGen";
    write_file(mod / "meta.ini", "[General]\r\nmodid=0\r\ncomments=Synthetic: damaged FaceGen (audit F-11)\r\n");
    const auto face_gen = mod / "meshes" / "actors" / "character" / "facegendata" / "facegeom" / "Skyrim.esm" / "000A2C8E.nif";
    write_file(face_gen, sns::test::audit::facegen_lengtherror());
    const auto meshes = root / "mods" / "SyntheticMeshes";
    write_file(meshes / "meta.ini", "[General]\r\nmodid=0\r\ncomments=Synthetic: one valid mesh\r\n");
    write_file(meshes / "meshes" / "Armor" / "Steel" / "CuirassF_1.nif", triangle_nif());

    const Json summary{{"instance", sns::text::path_to_utf8(root / "ModOrganizer.ini")},
                       {"profile", "Synthetic"},
                       {"damagedNpc", fixture.at("lydia")},
                       {"faceGen", sns::text::path_to_utf8(face_gen)},
                       {"faceGenBytes", sns::test::audit::facegen_lengtherror().size()}};
    std::printf("%s\n", summary.dump(2).c_str());
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "sns_synthetic_mo2: %s\n", e.what());
    return 1;
  }
}
