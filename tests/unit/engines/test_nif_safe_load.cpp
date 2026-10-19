// Audit F-10/F-12 regressions: damaged NIF bytes are refused before nifly parses them (no 4 GB allocation, no
// exception other than sns::Error, no regex over an impossible texture path), valid files still load, and one
// unreadable armor mesh costs that mesh only, never the whole character preview.
#include <catch2/catch_test_macros.hpp>

#include <NifFile.hpp>

#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include "audit_repro_inputs.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/core/error.hpp"
#include "sns/face/facegen.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/nif/safe_load.hpp"
#include "sns/rendering/scene.hpp"
#include "temp_dir.hpp"

namespace {

namespace audit = sns::test::audit;

struct Repro {
  const char* name;
  std::string_view bytes;
};

const Repro kRepros[] = {{"oom_238b", audit::oom_238b()},
                         {"bad_lengtherror", audit::bad_lengtherror()},
                         {"regex_overflow", audit::regex_overflow()},
                         {"facegen_lengtherror", audit::facegen_lengtherror()}};

// A real one-shape NIF written by nifly, with a lit shader and a texture set.
std::string synthetic_nif(const nifly::NiVersion& version, const std::string& texture = "textures\\test\\body_d.dds") {
  nifly::NifFile file;
  file.Create(version);
  std::vector<nifly::Vector3> verts{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
  std::vector<nifly::Triangle> tris{{0, 1, 2}};
  std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {0, 1}};
  auto* shape = file.CreateShapeFromData("Fixture", &verts, &tris, &uv);
  REQUIRE(shape != nullptr);
  auto tex = texture;
  file.SetTextureSlot(shape, tex, 0);
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

long long elapsed_ms(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

TEST_CASE("Audit repro NIFs are refused before nifly parses them, as sns::Error and fast", "[nif][safe-load][audit]") {
  for (const auto& r : kRepros) {
    INFO(r.name);
    const auto start = std::chrono::steady_clock::now();
    try {
      (void)sns::nif::load_mesh(r.bytes, r.name);
      FAIL("a damaged NIF was accepted");
    } catch (const sns::Error& e) {
      CHECK(e.kind() == sns::ErrorKind::Parse);
      CHECK(std::string(e.what()).find("rejected before parsing") != std::string::npos);
    }
    // Before the fix: oom_238b took 2.4 s and 4.1 GB, regex_overflow 5.6 s (MSVC Debug, sns_nif_load_probe).
    CHECK(elapsed_ms(start) < 100);
    CHECK_THROWS_AS(sns::nif::prevalidate_nif(r.bytes, r.name), sns::Error);
    CHECK_THROWS_AS(sns::nif::load_skeleton(r.bytes, r.name), sns::Error);
  }
}

TEST_CASE("A damaged FaceGen is unreadable, never an escaping exception", "[nif][safe-load][facegen][audit]") {
  for (const auto& r : kRepros) {
    INFO(r.name);
    sns::face::FaceGenInfo info;
    REQUIRE_NOTHROW(info = sns::face::inspect_facegen(r.bytes));  // F-11: this ran on a pool thread without a catch
    CHECK_FALSE(info.readable);
  }
}

TEST_CASE("Damaged NIFs are refused on a thread with the default stack", "[nif][safe-load][audit]") {
  // Scene builds and profile scans run on pool threads (1 MB stack on Windows): no crash there either.
  int refused = 0;
  std::thread worker([&] {
    for (const auto& r : kRepros) {
      try {
        (void)sns::nif::load_mesh(r.bytes, r.name);
      } catch (const sns::Error&) {
        ++refused;
      }
    }
  });
  worker.join();
  CHECK(refused == 4);
}

TEST_CASE("Valid Skyrim LE and SE NIFs pass the prevalidation and load", "[nif][safe-load]") {
  for (const auto& version : {nifly::NiVersion::getSSE(), nifly::NiVersion::getSK()}) {
    const auto bytes = synthetic_nif(version);
    REQUIRE_NOTHROW(sns::nif::prevalidate_nif(bytes, "synthetic.nif"));
    const auto mesh = sns::nif::load_mesh(bytes, "synthetic.nif");
    REQUIRE(mesh.shapes.size() == 1);
    CHECK(sns::face::inspect_facegen(bytes).readable);
    // Some files carry leftover bytes after the footer (FaceGen rewritten without truncation): allowed.
    CHECK_NOTHROW(sns::nif::prevalidate_nif(bytes + std::string(16, '\0'), "leftover.nif"));
  }
}

TEST_CASE("Each structural defect of a NIF is refused with its reason", "[nif][safe-load]") {
  const auto good = synthetic_nif(nifly::NiVersion::getSSE());
  auto refused_because = [](const std::string& bytes, const char* reason) {
    try {
      sns::nif::prevalidate_nif(bytes, "mutated.nif");
      return false;
    } catch (const sns::Error& e) {
      UNSCOPED_INFO(e.what());
      return std::string(e.what()).find(reason) != std::string::npos;
    }
  };
  // A texture path longer than any Skyrim path (what nifly would run through std::regex).
  CHECK(refused_because(synthetic_nif(nifly::NiVersion::getSSE(), "textures\\" + std::string(4000, 'a') + ".dds"), "texture path of"));
  // Truncated anywhere: the header or the blocks run out.
  CHECK(refused_because(good.substr(0, 60), "runs past"));
  CHECK(refused_because(good.substr(0, good.size() - 12), "more than the file"));
  // Not Skyrim's version.
  auto other = good;
  other[good.find('\n') + 1] = 0x06;  // version 20.2.0.6
  CHECK(refused_because(other, "not a Skyrim NIF"));
  CHECK(refused_because("not a nif", "not a Gamebryo NIF header"));
  // oom_238b is first refused for its header line ("Veirson", a fuzzer mutation); with that line repaired,
  // its block sizes (4.3 GB in 238 bytes, what made nifly allocate) are what refuses it.
  auto oom = std::string(audit::oom_238b());
  const auto typo = oom.find("Veirson");
  REQUIRE(typo != std::string::npos);
  oom.replace(typo, 7, "Version");
  CHECK(refused_because(oom, "more than the file"));
  CHECK(refused_because(std::string(sns::nif::kMaxNifBytes + 1, 'x'), "64 MiB"));
}

TEST_CASE("One unreadable armor NIF costs that piece only, not the character", "[nif][safe-load][scene][audit]") {
  // F-12: build_character_scene caught sns::Error only; a std::length_error from nifly emptied the preview.
  sns::test::TempDir dir;
  dir.write("Mod/meshes/test/head.nif", synthetic_nif(nifly::NiVersion::getSSE()));
  dir.write("Mod/meshes/test/armor.nif", std::string(audit::bad_lengtherror()));
  const auto vfs = sns::mo2::VirtualFileSystem::build_from({{"Mod", dir.path() / "Mod"}});
  const sns::assets::AssetResolver resolver(vfs, {});
  sns::rendering::Appearance a;
  a.npc_form_key = "000001:Test.esp";
  a.face_geometry = "meshes\\test\\head.nif";
  a.pieces.push_back({"000800:Test.esp", "Armor", "outfit", 1u << 2, {{"000801:Test.esp", "meshes\\test\\armor.nif", false, 1u << 2}}});
  sns::rendering::CharacterScene scene;
  REQUIRE_NOTHROW(scene = sns::rendering::build_character_scene(a, resolver, {}));
  CHECK_FALSE(scene.parts.empty());  // the head is still there
  bool armor_reported = false;
  for (const auto& issue : scene.issues) armor_reported |= issue.code == "MeshUnreadable";
  CHECK(armor_reported);
}
