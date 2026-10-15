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
#include "sns/assets/asset_resolver.hpp"
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

TEST_CASE("[local] Serana replacer keeps its effective FaceGen through twenty hair colors",
          "[local][facegen][serana]") {
  auto& s = test::local_setup();
  const auto active = std::find_if(s.load_order.active.begin(), s.load_order.active.end(), [](const auto& p) {
    return text::iequals_ascii(p.name, "Priscilla.Serana.Replacer.esp");
  });
  if (active == s.load_order.active.end()) SKIP("Priscilla.Serana.Replacer.esp is not active in this MO2 profile");

  auto worker = test::indexed_worker(s, SNS_WORKER_EXE);
  constexpr const char* kSerana = "002B6C:Dawnguard.esm";
  const auto npc = worker->client->call(ipc::protocol::method::kGetNpc, ipc::Json{{"formKey", kSerana}}, 30s);
  const auto winner = npc["summary"].value("winningPlugin", "");
  REQUIRE(winner == "Priscilla.Serana.Replacer.esp");
  const auto recipe_json = worker->client->call(
      ipc::protocol::method::kGetFaceRecipe,
      ipc::Json{{"npcFormKey", kSerana}, {"sourcePlugin", winner}, {"officialOnly", false}}, 60s);
  const auto recipe = face::recipe_from_json(recipe_json);
  REQUIRE(recipe.npc_form_key == kSerana);

  assets::AssetResolver resolver(s.vfs, assets::archive_load_order(s.profile, s.load_order, s.vfs));
  const auto original = resolver.read(recipe.facegen_mesh_path);
  const auto tint = resolver.read(recipe.facegen_tint_path);
  const auto geometry_provider = resolver.locate(recipe.facegen_mesh_path);
  const auto tint_provider = resolver.locate(recipe.facegen_tint_path);
  REQUIRE(original);
  REQUIRE(tint);
  REQUIRE(geometry_provider);
  REQUIRE(tint_provider);
  const auto original_info = face::inspect_facegen(*original);
  REQUIRE(original_info.readable);

  ipc::Json colors = ipc::Json::array();
  for (int i = 0; i < 20; ++i) {
    auto recolored = recipe;
    recolored.values.hair_color = face::Rgb8{static_cast<std::uint8_t>(17 + i * 9),
                                             static_cast<std::uint8_t>(230 - i * 7),
                                             static_cast<std::uint8_t>(31 + i * 5)};
    const auto patch = face::patch_hair_tint(*original, recolored);
    INFO("Serana hair color selection " << i);
    REQUIRE_FALSE(patch.nif.empty());
    REQUIRE_FALSE(patch.patched.empty());
    const auto integrity = face::verify_hair_tint_integrity(*original, patch.nif);
    CHECK(integrity.unchanged_except_hair_tint);
    CHECK(face::compare_facegen(*original, patch.nif).max_distance == 0.0f);
    colors.push_back({{"selection", i}, {"patchedShapes", patch.patched}});
  }

  ipc::Json shapes = ipc::Json::array();
  for (const auto& [name, vertices] : original_info.shapes) shapes.push_back({{"name", name}, {"vertices", vertices}});
  ipc::Json parts = ipc::Json::array();
  for (const auto& part : recipe.parts) {
    parts.push_back({{"editorId", part.editor_id}, {"plugin", part.plugin}, {"model", part.model}, {"official", part.official}});
  }
  const auto kind = [](assets::AssetLocation::Kind k) { return k == assets::AssetLocation::Kind::Loose ? "loose" : "archive"; };
  const ipc::Json report{{"title", "Serana Compatibility Report"},
                         {"npc", kSerana},
                         {"editorId", npc["summary"].value("editorId", "")},
                         {"winningPlugin", winner},
                         {"overrideChain", npc["overrides"]},
                         {"faceGen", {{"path", recipe.facegen_mesh_path},
                                      {"provider", geometry_provider->provider},
                                      {"kind", kind(geometry_provider->kind)},
                                      {"bytes", original->size()},
                                      {"shapes", shapes}}},
                         {"faceTint", {{"path", recipe.facegen_tint_path},
                                       {"provider", tint_provider->provider},
                                       {"kind", kind(tint_provider->kind)},
                                       {"bytes", tint->size()}}},
                         {"headParts", parts},
                         {"classification", "custom geometry; verified for Hair Color Safe Mode"},
                         {"allowed", {"Hair Color Safe Mode"}},
                         {"blockedUnlessMorphCompatible", {"face sliders", "sculpt regeneration"}},
                         {"hairColorSelections", colors}};
  std::cout << "[local][serana] " << report.dump() << "\n";
  if (const auto dump = test::env("SNS_LOCAL_DUMP_DIR")) {
    file_io::write_file_atomic(text::path_from_utf8(*dump) / "serana-compatibility-report.json", report.dump(2));
  }
}

// P0 reproduction of the Serana report: what the studio did on the FIRST hair color edit before
// Hair Color Safe Mode. AppearanceEditor had no previous build to patch, so it called
// build_facegen(): PreserveExisting when analyse_existing_facegen() had found that the profile's
// FaceGen does not come from the record, Regenerate otherwise. Both are measured against the
// winning FaceGen; the safe patch of the same color is measured too.
TEST_CASE("[local] Serana: first hair color edit, pre-fix build_facegen path vs Hair Color Safe Mode",
          "[local][facegen][serana]") {
  auto& s = test::local_setup();
  const auto active = std::find_if(s.load_order.active.begin(), s.load_order.active.end(), [](const auto& p) {
    return text::iequals_ascii(p.name, "Priscilla.Serana.Replacer.esp");
  });
  if (active == s.load_order.active.end()) SKIP("Priscilla.Serana.Replacer.esp is not active in this MO2 profile");

  auto worker = test::indexed_worker(s, SNS_WORKER_EXE);
  constexpr const char* kSerana = "002B6C:Dawnguard.esm";
  // The request AppearanceEditor::request_recipe(initial) sends.
  auto recipe = face::recipe_from_json(worker->client->call(ipc::protocol::method::kGetFaceRecipe,
                                                            ipc::Json{{"npcFormKey", kSerana}}, 60s));
  assets::AssetResolver resolver(s.vfs, assets::archive_load_order(s.profile, s.load_order, s.vfs));
  face::FaceAssetCache cache([&](std::string_view p) { return resolver.read(p); });
  for (const auto& plugin : s.load_order.active) {  // AppearanceEditor::morph_extensions
    if (auto ini = resolver.read(std::string(face::kFaceGenMorphsRoot) + plugin.name + "\\morphs.ini")) {
      auto parsed = face::parse_morphs_ini(*ini);
      recipe.morph_extensions.insert(recipe.morph_extensions.end(), parsed.begin(), parsed.end());
    }
  }
  const auto original = resolver.read(recipe.facegen_mesh_path);
  REQUIRE(original);
  const auto existing = std::make_shared<const std::string>(*original);
  for (const auto& part : recipe.parts) {
    std::string tris;
    for (const auto& t : part.tris) tris += " " + std::to_string(static_cast<int>(t.kind)) + ":" + t.path;
    std::cout << "[local][serana] head part " << part.editor_id << " (" << part.plugin << ") model=" << part.model << " tris" << tris << "\n";
  }
  for (const auto& [name, vertices] : face::inspect_facegen(*original).shapes) {
    std::cout << "[local][serana] winning FaceGen shape " << name << ": " << vertices << " vertices\n";
  }

  // analyse_existing_facegen(): regenerate from the record and compare.
  const auto regen = face::build_facegen(recipe, cache);
  const auto analysis = face::compare_facegen(regen.nif, *original);
  const bool preserve = !(regen.exportable() && analysis.same_topology && analysis.max_distance < 0.05f);
  std::cout << "[local][serana] analysis: same_shapes=" << analysis.same_shapes << " same_topology=" << analysis.same_topology
            << " max=" << analysis.max_distance << " -> geometry mode " << (preserve ? "preserve" : "regenerate") << "\n";
  // The draft's geometry mode is "regenerate" until that analysis has finished: an edit made (or
  // restored by undo/redo) with that mode was built like this, with the new hair color.
  {
    auto first = recipe;
    first.values.hair_color = face::Rgb8{165, 58, 30};
    const auto regenerated = face::build_facegen(first, cache);
    std::cout << "[local][serana] regenerate path: exportable=" << regenerated.exportable() << "\n";
    for (const auto& i : regenerated.issues) std::cout << "[local][serana] regenerate issue " << i.severity << " " << i.code << ": " << i.message << "\n";
    for (const auto& sh : face::compare_facegen(regenerated.nif, *original).shapes) {
      std::cout << "[local][serana] regenerate vs winner " << sh.name << ": vertices " << sh.vertices_a << "/" << sh.vertices_b
                << " max=" << sh.max_distance << " mean=" << sh.mean_distance << "\n";
    }
  }

  // First edit: only HCLF changes.
  const auto baseline = recipe.values;
  auto edited = recipe;
  edited.values.hair_color = face::Rgb8{165, 58, 30};
  face::FaceGenBuildOptions options;
  if (preserve) {
    options.mode = face::GeometryMode::PreserveExisting;
    options.existing_facegen = existing;
    options.baseline = baseline;
  }
  const auto old_path = face::build_facegen(edited, cache, options);
  for (const auto& i : old_path.issues) std::cout << "[local][serana] pre-fix build issue " << i.severity << " " << i.code << ": " << i.message << "\n";
  for (const auto& p : old_path.parts) {
    std::cout << "[local][serana] pre-fix part " << p.editor_id << " " << p.status << " " << p.geometry_source << " "
              << p.vertex_count << " vertices\n";
  }
  const auto old_cmp = face::compare_facegen(old_path.nif, *original);
  for (const auto& sh : old_cmp.shapes) {
    std::cout << "[local][serana] pre-fix vs winner " << sh.name << ": in_new=" << sh.in_a << " in_winner=" << sh.in_b << " vertices "
              << sh.vertices_a << "/" << sh.vertices_b << " max=" << sh.max_distance << " mean=" << sh.mean_distance << "\n";
  }
  const auto old_integrity = face::verify_hair_tint_integrity(*original, old_path.nif);
  std::cout << "[local][serana] pre-fix integrity: " << old_integrity.summary() << "\n";
  for (const auto& d : old_integrity.differences) {
    std::cout << "[local][serana]   " << (d.shape.empty() ? "(file)" : d.shape) << " " << d.property << ": " << d.before << " -> " << d.after << "\n";
  }

  // Hair Color Safe Mode on the same edit.
  const auto safe = face::patch_hair_tint(*original, edited);
  for (const auto& i : safe.issues) std::cout << "[local][serana] safe issue " << i.severity << " " << i.code << ": " << i.message << "\n";
  REQUIRE_FALSE(safe.nif.empty());
  CHECK_FALSE(safe.patched.empty());
  const auto safe_integrity = face::verify_hair_tint_integrity(*original, safe.nif);
  CHECK(safe_integrity.unchanged_except_hair_tint);
  CHECK(safe_integrity.byte_identical_outside_hair_tint);

  // The compatibility diagnosis on the real replacer: custom geometry whose TRI files do not fit.
  const auto compat = face::diagnose_face_compatibility(recipe, cache, &*original, regen);
  std::cout << "[local][serana] compatibility " << compat.code << "\n";
  for (const auto& e : compat.evidence) std::cout << "[local][serana]   evidence: " << e << "\n";
  for (const auto& [op, why] : compat.blocked) std::cout << "[local][serana]   blocked " << op << ": " << why << "\n";
  CHECK(compat.kind == face::FaceCompatibility::CustomIncompatibleMorphs);
  CHECK(std::find(compat.allowed.begin(), compat.allowed.end(), "hair-color") != compat.allowed.end());
  const auto safe_cmp = face::compare_facegen(safe.nif, *original);
  CHECK(safe_cmp.same_topology);
  CHECK(safe_cmp.max_distance == 0.0f);
  std::cout << "[local][serana] safe patch: shapes " << safe.patched.size() << " recolored, max " << safe_cmp.max_distance << "\n";
}

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
