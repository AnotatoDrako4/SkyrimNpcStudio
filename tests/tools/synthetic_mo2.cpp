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

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audit_repro_inputs.hpp"
#include "process.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/textures/dds.hpp"

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

using Vec3 = std::array<float, 3>;

void put_i32(std::string& out, std::int32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void put_f32(std::string& out, float v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void put_label(std::string& out, const std::string& s) {
  put_i32(out, static_cast<std::int32_t>(s.size() + 1));
  out.append(s);
  out.push_back('\0');
}

std::string build_tri_bytes(const std::vector<nifly::Vector3>& base,
                            const std::vector<std::pair<std::string, std::vector<Vec3>>>& diffs,
                            float scale = 0.001f) {
  std::string out = "FRTRI003";
  for (const std::int32_t v : {static_cast<std::int32_t>(base.size()), 0, 0, 0, 0, 0, 0,
                               static_cast<std::int32_t>(diffs.size()), 0, 0}) {
    put_i32(out, v);
  }
  out.append(16, '\0');
  for (const auto& p : base) {
    put_f32(out, p.x);
    put_f32(out, p.y);
    put_f32(out, p.z);
  }
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
  return out;
}

struct GeneratedMesh {
  std::string nif_bytes;
  std::vector<nifly::Vector3> verts;
};

GeneratedMesh make_ellipsoid_nif(const std::string& shape_name,
                                 std::uint32_t shader_type,
                                 float cx, float cy, float cz,
                                 float rx, float ry, float rz,
                                 int rings, int sectors,
                                 const std::string& diffuse = {},
                                 const std::string& normal = {},
                                 float z_min_clip = -1.0f) {
  std::vector<nifly::Vector3> verts;
  std::vector<nifly::Vector3> norms;
  std::vector<nifly::Vector2> uvs;
  std::vector<nifly::Triangle> tris;

  constexpr float kPi = 3.14159265358979323846f;
  for (int r = 0; r <= rings; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(rings);
    const float phi = v * kPi;
    for (int s = 0; s <= sectors; ++s) {
      const float u = static_cast<float>(s) / static_cast<float>(sectors);
      const float theta = u * 2.0f * kPi;
      float nx = std::sin(phi) * std::cos(theta);
      float ny = std::sin(phi) * std::sin(theta);
      float nz = std::cos(phi);
      if (z_min_clip > -0.99f && nz < z_min_clip) nz = z_min_clip;
      verts.emplace_back(cx + rx * nx, cy + ry * ny, cz + rz * nz);
      norms.emplace_back(nx, ny, nz);
      uvs.emplace_back(u, v);
    }
  }
  const int stride = sectors + 1;
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < sectors; ++s) {
      const auto i0 = static_cast<std::uint16_t>(r * stride + s);
      const auto i1 = static_cast<std::uint16_t>(i0 + 1);
      const auto i2 = static_cast<std::uint16_t>((r + 1) * stride + s);
      const auto i3 = static_cast<std::uint16_t>(i2 + 1);
      tris.emplace_back(i0, i2, i1);
      tris.emplace_back(i1, i2, i3);
    }
  }

  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  auto* shape = file.CreateShapeFromData(shape_name, &verts, &tris, &uvs, &norms);
  if (!shape) throw std::runtime_error("nifly failed to create " + shape_name);
  file.GetShader(shape)->SetShaderType(shader_type);
  if (!diffuse.empty()) {
    auto d = diffuse;
    file.SetTextureSlot(shape, d, 0);
  }
  if (!normal.empty()) {
    auto n = normal;
    file.SetTextureSlot(shape, n, 1);
  }
  std::ostringstream out(std::ios::binary);
  if (file.Save(out) != 0) throw std::runtime_error("nifly failed to save " + shape_name);
  return {out.str(), std::move(verts)};
}

std::string solid_dds(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255, std::uint32_t dim = 16) {
  sns::textures::DdsImage img;
  img.width = dim;
  img.height = dim;
  img.mip_count = 1;
  img.format = sns::textures::TextureFormat::RGBA8;
  img.data.resize(static_cast<std::size_t>(dim) * dim * 4);
  for (std::size_t i = 0; i < static_cast<std::size_t>(dim) * dim; ++i) {
    img.data[i * 4 + 0] = static_cast<char>(r);
    img.data[i * 4 + 1] = static_cast<char>(g);
    img.data[i * 4 + 2] = static_cast<char>(b);
    img.data[i * 4 + 3] = static_cast<char>(a);
  }
  return sns::textures::encode_dds(img, sns::textures::TextureFormat::RGBA8, false);
}

void populate_beauty_lab_assets(const fs::path& root, const fs::path& meshes_mod) {
  const fs::path char_assets = meshes_mod / "meshes" / "Actors" / "Character" / "Character Assets";
  const auto head = make_ellipsoid_nif(
      "FemaleHeadNord", 4, 0.0f, 0.0f, 120.0f, 7.2f, 8.2f, 9.5f, 12, 16,
      R"(textures\Actors\Character\Female\FemaleHead.dds)",
      R"(textures\Actors\Character\Female\FemaleHead_msn.dds)");
  write_file(char_assets / "FemaleHead.nif", head.nif_bytes);

  const std::size_t n = head.verts.size();
  auto make_delta = [&](std::size_t idx, Vec3 d) {
    std::vector<Vec3> out(n, Vec3{0.0f, 0.0f, 0.0f});
    if (n > 0) out[idx % n] = d;
    return out;
  };

  write_file(char_assets / "FemaleHeadRaces.tri",
             build_tri_bytes(head.verts, {
                 {"NordRace", make_delta(0, {0.2f, -0.1f, 0.1f})},
                 {"NordRaceVampire", make_delta(1, {0.2f, -0.15f, 0.1f})}
             }));
  write_file(char_assets / "FemaleHead.tri",
             build_tri_bytes(head.verts, {
                 {"Aah", make_delta(2, {0.0f, 0.0f, -0.4f})},
                 {"SkinnyMorph", make_delta(3, {-0.2f, 0.0f, 0.0f})}
             }));

  std::vector<std::pair<std::string, std::vector<Vec3>>> chargen_morphs{
      {"NoseLong", make_delta(4, {0.0f, -0.6f, -0.2f})},
      {"NoseShort", make_delta(4, {0.0f, 0.6f, 0.2f})},
      {"NoseUp", make_delta(5, {0.0f, 0.0f, 0.5f})},
      {"NoseDown", make_delta(5, {0.0f, 0.0f, -0.5f})},
      {"JawUp", make_delta(6, {0.0f, 0.0f, 0.5f})},
      {"JawDown", make_delta(6, {0.0f, 0.0f, -0.5f})},
      {"JawNarrow", make_delta(7, {-0.5f, 0.0f, 0.0f})},
      {"JawWide", make_delta(7, {0.5f, 0.0f, 0.0f})},
      {"JawForward", make_delta(8, {0.0f, -0.5f, 0.0f})},
      {"JawBack", make_delta(8, {0.0f, 0.5f, 0.0f})},
      {"CheeksUp", make_delta(9, {0.0f, 0.0f, 0.4f})},
      {"CheeksDown", make_delta(9, {0.0f, 0.0f, -0.4f})},
      {"CheeksForward", make_delta(10, {0.0f, -0.4f, 0.0f})},
      {"CheeksBack", make_delta(10, {0.0f, 0.4f, 0.0f})},
      {"EyesUp", make_delta(11, {0.0f, 0.0f, 0.4f})},
      {"EyesDown", make_delta(11, {0.0f, 0.0f, -0.4f})},
      {"EyesIn", make_delta(12, {-0.3f, 0.0f, 0.0f})},
      {"EyesOut", make_delta(12, {0.3f, 0.0f, 0.0f})},
      {"EyesForward", make_delta(13, {0.0f, -0.3f, 0.0f})},
      {"EyesBack", make_delta(13, {0.0f, 0.3f, 0.0f})},
      {"BrowsUp", make_delta(14, {0.0f, 0.0f, 0.4f})},
      {"BrowsDown", make_delta(14, {0.0f, 0.0f, -0.4f})},
      {"BrowsIn", make_delta(15, {-0.3f, 0.0f, 0.0f})},
      {"BrowsOut", make_delta(15, {0.3f, 0.0f, 0.0f})},
      {"BrowsForward", make_delta(16, {0.0f, -0.3f, 0.0f})},
      {"BrowsBack", make_delta(16, {0.0f, 0.3f, 0.0f})},
      {"LipsUp", make_delta(17, {0.0f, 0.0f, 0.3f})},
      {"LipsDown", make_delta(17, {0.0f, 0.0f, -0.3f})},
      {"LipsIn", make_delta(18, {0.0f, 0.3f, 0.0f})},
      {"LipsOut", make_delta(18, {0.0f, -0.3f, 0.0f})},
      {"ChinThin", make_delta(19, {-0.4f, 0.0f, 0.0f})},
      {"ChinWide", make_delta(19, {0.4f, 0.0f, 0.0f})},
      {"ChinUp", make_delta(20, {0.0f, 0.0f, 0.4f})},
      {"ChinDown", make_delta(20, {0.0f, 0.0f, -0.4f})},
      {"ChinUnderbite", make_delta(21, {0.0f, -0.4f, 0.0f})},
      {"ChinOverbite", make_delta(21, {0.0f, 0.4f, 0.0f})},
      {"VampireMorph", make_delta(22, {0.0f, -0.2f, 0.1f})}
  };
  for (int i = 1; i <= 20; ++i) {
    chargen_morphs.emplace_back("NoseType" + std::to_string(i), make_delta(25 + i, {0.0f, -0.05f * (i % 5), 0.03f}));
    chargen_morphs.emplace_back("EyesType" + std::to_string(i), make_delta(45 + i, {0.02f * (i % 4), 0.0f, 0.02f}));
    chargen_morphs.emplace_back("BrowType" + std::to_string(i), make_delta(65 + i, {0.0f, 0.0f, 0.03f * (i % 4)}));
    chargen_morphs.emplace_back("LipType" + std::to_string(i), make_delta(85 + i, {0.0f, -0.03f * (i % 4), 0.0f}));
  }
  write_file(char_assets / "FemaleHeadCharGen.tri", build_tri_bytes(head.verts, chargen_morphs));

  const auto hair = make_ellipsoid_nif(
      "HairFemaleNord01", 6, 0.0f, 0.4f, 122.2f, 7.9f, 8.9f, 9.2f, 10, 16,
      R"(textures\Actors\Character\Hair\HairLong.dds)",
      R"(textures\Actors\Character\Hair\HairLong_n.dds)", -0.2f);
  write_file(char_assets / "Hair" / "FemaleHair01.nif", hair.nif_bytes);

  const auto hairline = make_ellipsoid_nif(
      "HairLineFemaleNord01", 6, 0.0f, 0.3f, 122.0f, 7.7f, 8.7f, 9.0f, 8, 12,
      R"(textures\Actors\Character\Hair\HairLong.dds)",
      R"(textures\Actors\Character\Hair\HairLong_n.dds)", 0.0f);
  write_file(char_assets / "Hair" / "Female" / "Hairline01.nif", hairline.nif_bytes);

  const auto eyes = make_ellipsoid_nif(
      "FemaleEyesHumanBrown", 5, 0.0f, -6.8f, 121.0f, 3.2f, 1.4f, 1.2f, 6, 10,
      R"(textures\Actors\Character\Eyes\EyeGreen.dds)",
      R"(textures\Actors\Character\Eyes\EyeBrown_n.dds)");
  write_file(char_assets / "EyesFemale.nif", eyes.nif_bytes);
  write_file(char_assets / "EyesFemaleChargen.tri", build_tri_bytes(eyes.verts, {}));

  // Torso & boots so full-body mode also renders a clean silhouette
  const auto torso = make_ellipsoid_nif(
      "SyntheticCuirass", 1, 0.0f, 0.0f, 88.0f, 13.0f, 8.5f, 22.0f, 10, 14,
      R"(textures\Actors\Character\Female\FemaleHead.dds)");
  write_file(meshes_mod / "meshes" / "Armor" / "Steel" / "CuirassF_1.nif", torso.nif_bytes);
  const auto boots = make_ellipsoid_nif(
      "SyntheticBoots", 1, 0.0f, 0.0f, 22.0f, 8.0f, 7.0f, 20.0f, 8, 12,
      R"(textures\Actors\Character\Female\FemaleHead.dds)");
  write_file(meshes_mod / "meshes" / "Armor" / "Steel" / "BootsF_1.nif", boots.nif_bytes);

  // Textures
  const fs::path tex = meshes_mod / "textures";
  write_file(tex / "Actors" / "Character" / "Female" / "FemaleHead.dds", solid_dds(218, 182, 164));
  write_file(tex / "Actors" / "Character" / "Female" / "FemaleHead_msn.dds", solid_dds(128, 128, 255));
  write_file(tex / "Actors" / "Character" / "Female" / "FemaleHead_sk.dds", solid_dds(200, 140, 120));
  write_file(tex / "Actors" / "Character" / "Female" / "FemaleHead_S.dds", solid_dds(90, 90, 90));
  write_file(tex / "Actors" / "Character" / "Male" / "BlankDetailmap.dds", solid_dds(128, 128, 128));
  write_file(tex / "Actors" / "Character" / "Hair" / "HairLong.dds", solid_dds(180, 165, 150));
  write_file(tex / "Actors" / "Character" / "Hair" / "HairLong_n.dds", solid_dds(128, 128, 255));
  write_file(tex / "Actors" / "Character" / "Eyes" / "EyeGreen.dds", solid_dds(92, 148, 112));
  write_file(tex / "Actors" / "Character" / "Eyes" / "EyeBrown_n.dds", solid_dds(128, 128, 255));
  write_file(tex / "Actors" / "Character" / "Eyes" / "EyeEnvironmentMask_M.dds", solid_dds(180, 180, 180));
  write_file(tex / "Cubemaps" / "EyeCubeMap.dds", solid_dds(200, 210, 220));
  write_file(tex / "Actors" / "Character" / "Character Assets" / "TintMasks" / "SkinTone.dds", solid_dds(255, 255, 255));
  write_file(tex / "Actors" / "Character" / "Character Assets" / "TintMasks" / "FemaleHeadNord_Lips.dds", solid_dds(255, 255, 255));

  // Add a second MO2 mod that overrides the hair mesh to showcase MO2 override provenance in Beauty Lab
  const fs::path hd_mod = root / "mods" / "NordicHairHD_Replacer";
  write_file(hd_mod / "meta.ini", "[General]\r\nmodid=0\r\ncomments=Synthetic HD Hair Override\r\n");
  write_file(hd_mod / "meshes" / "Actors" / "Character" / "Character Assets" / "Hair" / "FemaleHair01.nif", hair.nif_bytes);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: sns_synthetic_mo2 <absolute directory that does not exist or is empty> [--beauty-lab]\n");
    return 2;
  }
  const bool beauty_lab_mode = (argc == 3 && std::string_view(argv[2]) == "--beauty-lab");
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
    if (beauty_lab_mode) {
      write_file(profile / "modlist.txt", "+NordicHairHD_Replacer\r\n+SyntheticMeshes\r\n-DamagedFaceGen\r\n");
    } else {
      write_file(profile / "modlist.txt", "+DamagedFaceGen\r\n+SyntheticMeshes\r\n");
    }
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
    if (beauty_lab_mode) {
      populate_beauty_lab_assets(root, meshes);
    }

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
