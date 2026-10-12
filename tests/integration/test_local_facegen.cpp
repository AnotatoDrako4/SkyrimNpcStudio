// LOCAL fidelity test of the FaceGen regenerator (engines/face) against the Creation Kit itself:
// for vanilla NPCs, the FaceGen NIF and FaceTint DDS shipped in the vanilla BSAs were written by
// Bethesda's Creation Kit from the Skyrim.esm records. Rebuilding them from the same records and
// the same vanilla assets must give the same geometry and tint. Read-only; needs SNS_LOCAL_MO2_INI.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "local_mo2.hpp"
#include "sns/assets/bsa_archive.hpp"
#include "sns/core/file_io.hpp"
#include "sns/face/face_tint.hpp"
#include "sns/face/facegen.hpp"
#include "sns/textures/dds.hpp"

using namespace sns;
using namespace std::chrono_literals;

namespace {

// Only the vanilla archives: the Creation Kit built the vanilla FaceGen from them, and mods in the
// profile may replace the same paths with loose files.
struct VanillaArchives {
  std::vector<std::unique_ptr<assets::BsaArchive>> archives;

  explicit VanillaArchives(const std::filesystem::path& data) {
    std::vector<std::string> names{"Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa"};
    for (int i = 0; i <= 8; ++i) names.push_back("Skyrim - Textures" + std::to_string(i) + ".bsa");
    for (const auto& n : names) {
      std::error_code ec;
      if (std::filesystem::exists(data / n, ec)) archives.push_back(assets::BsaArchive::open(data / n));
    }
  }
  std::optional<std::string> read(std::string_view path) const {
    const auto key = text::data_path_key(path);
    for (const auto& a : archives) {
      if (auto bytes = a->read(key)) return bytes;
    }
    return std::nullopt;
  }
};

struct Case {
  const char* form_key;
  const char* what;
  // The shipped vanilla FaceGen predates a DLC edit of the record: it cannot match the current
  // record, and the audit must say so instead (a real dark-face/mismatch case).
  bool stale_vanilla = false;
};

}  // namespace

TEST_CASE("[local] FaceGen regenerated from vanilla records matches the Creation Kit output", "[local][facegen]") {
  auto& s = test::local_setup();
  const VanillaArchives vanilla(s.config.game_path / "Data");
  REQUIRE(vanilla.archives.size() >= 3);
  face::FaceAssetCache cache([&](std::string_view p) { return vanilla.read(p); });
  auto worker = test::indexed_worker(s, SNS_WORKER_EXE);

  const std::vector<Case> cases = {
      {"0A2C8E:Skyrim.esm", "Lydia, female Nord"},
      {"013BB8:Skyrim.esm", "Irileth, female Dark Elf"},
      {"01411A:Skyrim.esm", "Shahvee, female Argonian"},
      {"01B1D1:Skyrim.esm", "Madran, male Khajiit"},
      {"013B7E:Skyrim.esm", "Umurn, male Orc"},
      // Dawnguard.esm gives NordRaceVampire FemaleHeadNordVampire; Hert's shipped FaceGen still uses
      // FemaleHeadNord from before the DLC.
      {"01367C:Skyrim.esm", "Hert, female Nord vampire (stale vanilla FaceGen)", true},
      {"019E16:Skyrim.esm", "Malacath, Dremora (morph race DarkElfRace)"},
      {"01334D:Skyrim.esm", "Bersi, male Nord with beard"},
      {"013BBE:Skyrim.esm", "Ahlam, female Redguard, nose preset 10"},
      {"0918E2:Skyrim.esm", "Uthgerd, female Nord, weight below 100 (SkinnyMorph)"},
  };

  int verified = 0;
  for (const auto& c : cases) {
    INFO(c.form_key << " (" << c.what << ")");
    const auto json = worker->client->call(ipc::protocol::method::kGetFaceRecipe,
                                           ipc::Json{{"npcFormKey", c.form_key}, {"sourcePlugin", "Skyrim.esm"}, {"officialOnly", true}}, 60s);
    if (const auto dump = test::env("SNS_LOCAL_DUMP_DIR")) {
      auto name = std::string(c.form_key);
      std::replace(name.begin(), name.end(), ':', '_');
      file_io::write_file_atomic(text::path_from_utf8(*dump) / (name + ".recipe.json"), json.dump(2));
    }
    const auto recipe = face::recipe_from_json(json);
    REQUIRE_FALSE(recipe.parts.empty());

    // Mods may override vanilla HDPT records (e.g. a high-poly head): then the vanilla FaceGen is
    // not what these records describe, and the case is reported instead of compared.
    std::string overridden;
    for (const auto& part : recipe.parts) {
      if (!part.model.empty() && !vanilla.read(part.model)) overridden = part.editor_id + " -> " + part.model;
    }
    if (!overridden.empty()) {
      std::cout << "[local][facegen] " << c.form_key << " skipped: head part overridden by a mod (" << overridden << ")\n";
      continue;
    }

    const auto reference = vanilla.read(recipe.facegen_mesh_path);
    REQUIRE(reference);
    const auto audit = face::audit_facegen(face::inspect_facegen(*reference), recipe);
    if (c.stale_vanilla) {
      for (const auto& issue : audit) std::cout << "[local][facegen] " << c.form_key << " audit " << issue.code << ": " << issue.message << "\n";
      CHECK(std::any_of(audit.begin(), audit.end(), [](const auto& i) { return i.code == "FaceGenPartMismatch"; }));
      continue;
    }
    CHECK(audit.empty());
    const auto build = face::build_facegen(recipe, cache);
    for (const auto& issue : build.issues) std::cout << "[local][facegen]   " << issue.severity << " " << issue.code << ": " << issue.message << "\n";
    REQUIRE(build.exportable());

    const auto cmp = face::compare_facegen(build.nif, *reference);
    std::cout << "[local][facegen] " << c.form_key << " " << c.what << ": max " << cmp.max_distance << " units;";
    for (const auto& shape : cmp.shapes) {
      std::cout << " " << shape.name << "=" << (shape.in_a && shape.in_b ? std::to_string(shape.max_distance) : std::string("absent"));
    }
    std::cout << "\n";
    const auto ours = face::inspect_facegen(build.nif);
    const auto theirs = face::inspect_facegen(*reference);
    for (const auto& [name, ck] : theirs.hair_tints) {
      for (const auto& [n2, mine] : ours.hair_tints) {
        if (n2 != name) continue;
        std::cout << "[local][facegen]   hair tint " << name << ": CK (" << ck[0] * 255 << ", " << ck[1] * 255 << ", " << ck[2] * 255
                  << ") ours (" << mine[0] * 255 << ", " << mine[1] * 255 << ", " << mine[2] * 255 << ")\n";
        for (int ch = 0; ch < 3; ++ch) CHECK(std::abs(ck[static_cast<std::size_t>(ch)] - mine[static_cast<std::size_t>(ch)]) < 2.5f / 255.0f);
      }
    }
    CHECK(cmp.same_topology);
    // Measured CK deviations: <= 0.010 for female heads, <= 0.042 for male heads (a fixed offset
    // of the shipped malehead.nif) and <= 0.09 for beards of thin NPCs.
    CHECK(cmp.max_distance < 0.1f);

    const auto tint_reference = vanilla.read(recipe.facegen_tint_path);
    REQUIRE(tint_reference);
    const auto ref_image = textures::decode_rgba(textures::parse_dds(*tint_reference));
    const auto tint = face::compose_face_tint(recipe, cache, ref_image.width);
    double sum = 0;
    for (std::size_t i = 0; i < ref_image.data.size(); i += 4) {
      for (std::size_t ch = 0; ch < 3; ++ch) {
        sum += std::abs(static_cast<int>(static_cast<unsigned char>(ref_image.data[i + ch])) -
                        static_cast<int>(static_cast<unsigned char>(tint.rgba.data[i + ch])));
      }
    }
    const double mean = sum / (static_cast<double>(ref_image.data.size()) / 4.0 * 3.0);
    std::cout << "[local][facegen]   FaceTint mean abs error " << mean << " / 255 over " << tint.layers.size() << " layers\n";
    CHECK(mean < 6.0);
    ++verified;
  }
  std::cout << "[local][facegen] " << verified << " of " << cases.size() << " vanilla NPCs verified against the Creation Kit\n";
  CHECK(verified >= 5);
}
