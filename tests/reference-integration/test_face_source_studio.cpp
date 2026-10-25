#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "sns/appearance/face_source_index.hpp"
#include "sns/appearance/face_transfer.hpp"
#include "sns/appearance/source_matrix.hpp"

using namespace sns::appearance;

namespace {

const std::string kGeometry = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif";
const std::string kTint = "textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds";
const std::vector<std::string> kFace = {"HeadParts", "FaceMorph", "FaceParts", "HeadTexture"};

OverrideLink link(std::string plugin, std::string mod, std::vector<std::string> changed) {
  OverrideLink l;
  l.plugin = std::move(plugin);
  l.mod = std::move(mod);
  l.changed_fields = std::move(changed);
  return l;
}

std::vector<OverrideLink> chain(std::vector<OverrideLink> links) {
  links.front().is_origin = true;
  links.back().is_winner = true;
  return links;
}

AssetSource loose(const std::string& path, std::string winner, std::vector<std::string> mods) {
  AssetSource a;
  a.path = path;
  a.found = true;
  a.winner_mod = std::move(winner);
  a.kind = "loose";
  a.loose_mods = std::move(mods);
  return a;
}

ProviderFaceGen provider(const std::string& mod, const std::string& archive = {}) {
  ProviderFaceGen p;
  p.mod = mod;
  p.archive = archive;
  p.has_geometry = true;
  p.has_tint = true;
  const std::string root = archive.empty() ? "D:\\MO2\\mods\\" + mod + "\\" : "C:\\cache\\bsa\\" + archive + "\\";
  p.geometry_file = root + kGeometry;
  p.tint_file = root + kTint;
  p.geometry_bytes = 1000;
  p.tint_bytes = 500;
  p.readable = true;
  return p;
}

// A replacer (loose, wins) over the vanilla face inside "Skyrim - Meshes0.bsa"; the replacer's record wins too.
SourceInputs replacer_over_vanilla_bsa() {
  SourceInputs in;
  in.npc_form_key = "0A2C8E:Skyrim.esm";
  in.profile_digest = "profile-1";
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Replacer.esp", "Replacer", kFace)});
  in.control.facegen = loose(kGeometry, "Replacer", {"Replacer"});
  in.control.facetint = loose(kTint, "Replacer", {"Replacer"});
  in.providers = {provider("Replacer"), provider("<Game Data>", "Skyrim - Meshes0.bsa")};
  return in;
}

// ModA's FaceGen wins in MO2 but ModB's record wins in the load order: the record does not match the face.
SourceInputs facegen_record_mismatch() {
  SourceInputs in;
  in.npc_form_key = "0A2C8E:Skyrim.esm";
  in.profile_digest = "profile-1";
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("ReplacerA.esp", "ModA", kFace), link("ReplacerB.esp", "ModB", kFace)});
  in.control.facegen = loose(kGeometry, "ModA", {"ModB", "ModA"});
  in.control.facetint = loose(kTint, "ModA", {"ModB", "ModA"});
  in.providers = {provider("ModB"), provider("ModA")};
  return in;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool any_contains(const std::vector<std::string>& v, const std::string& s) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& x) { return x.find(s) != std::string::npos; });
}

FaceIndexQuery q() { return {}; }

}  // namespace

// ---------------------------------------------------------------- index

TEST_CASE("Face index: FaceGen paths name the NPC, anything else is ignored", "[facestudio]") {
  const auto g = facegen_file_identity("Meshes/Actors/Character/FaceGenData/FaceGeom/Skyrim.esm/000A2C8E.NIF");
  REQUIRE(g);
  CHECK(g->geometry);
  CHECK(g->plugin == "Skyrim.esm");
  CHECK(g->form_id == "000A2C8E");
  CHECK(g->form_key == "0A2C8E:Skyrim.esm");

  const auto t = facegen_file_identity(kTint);
  REQUIRE(t);
  CHECK_FALSE(t->geometry);
  CHECK(t->form_key == "0A2C8E:skyrim.esm");

  CHECK_FALSE(facegen_file_identity("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\sub\\000a2c8e.nif"));
  CHECK_FALSE(facegen_file_identity("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\lydia.nif"));
  CHECK_FALSE(facegen_file_identity("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.dds"));
  CHECK_FALSE(facegen_file_identity("meshes\\armor\\000a2c8e.nif"));
  CHECK_FALSE(facegen_file_identity("meshes\\actors\\character\\facegendata\\facegeom\\000a2c8e.nif"));
}

TEST_CASE("Face index: loose copies beat archives, alternatives and flags are counted", "[facestudio]") {
  const std::string serana_g = "meshes\\actors\\character\\facegendata\\facegeom\\Dawnguard.esm\\00002B6C.nif";
  const std::string serana_t = "textures\\actors\\character\\facegendata\\facetint\\Dawnguard.esm\\00002B6C.dds";
  const std::string ulfric_g = "meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\0001414D.nif";
  const std::string mixed_g = "meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\00013364.nif";
  const std::string mixed_t = "textures\\actors\\character\\facegendata\\facetint\\Skyrim.esm\\00013364.dds";
  const std::vector<FaceFileObservation> files = {
      {serana_g, {"Serana Old", "Serana New"}, {{"Dawnguard.bsa", "<Game Data>"}}},
      {serana_t, {"Serana Old", "Serana New"}, {{"Dawnguard.bsa", "<Game Data>"}}},
      {ulfric_g, {}, {{"Skyrim - Meshes0.bsa", "<Game Data>"}}},
      {mixed_g, {"Bijin"}, {}},
      {mixed_t, {"NITHI"}, {}},
      {"meshes\\armor\\x.nif", {"Armor"}, {}},  // not a FaceGen file
  };
  const auto index = build_face_index(files);
  REQUIRE(index.entries.size() == 3);
  CHECK(index.with_alternatives == 1);

  const auto find = [&](const std::string& key) -> const FaceIndexEntry* {
    for (const auto& e : index.entries)
      if (e.form_key == key) return &e;
    return nullptr;
  };
  const auto* serana = find("002B6C:Dawnguard.esm");
  REQUIRE(serana);
  CHECK(serana->geometry_winner == "Serana New");
  CHECK_FALSE(serana->geometry_winner_archived);
  CHECK(serana->alternatives == 2);  // Serana Old + the vanilla copy in Dawnguard.bsa
  CHECK(has(serana->flags, "MultipleSources"));
  CHECK_FALSE(has(serana->flags, "TintFromOtherSource"));

  const auto* ulfric = find("01414D:Skyrim.esm");
  REQUIRE(ulfric);
  CHECK(ulfric->geometry_winner == "Skyrim - Meshes0.bsa");
  CHECK(ulfric->geometry_winner_archived);
  CHECK(ulfric->alternatives == 0);
  CHECK(has(ulfric->flags, "ArchivedOnly"));
  CHECK(has(ulfric->flags, "NoTint"));

  const auto* mixed = find("013364:Skyrim.esm");
  REQUIRE(mixed);
  CHECK(has(mixed->flags, "TintFromOtherSource"));  // Bijin geometry + NITHI tint: the dark-face mix

  CHECK(has(index.sources, "Dawnguard.bsa"));
  CHECK(has(index.sources, "NITHI"));
  // Sorted by plugin case-insensitively, then form id.
  CHECK(index.entries[0].plugin == "Dawnguard.esm");
  CHECK(index.entries[1].form_id == "00013364");
}

TEST_CASE("Face index: queries filter by text, name, source, flag and alternatives", "[facestudio]") {
  const std::vector<FaceFileObservation> files = {
      {"meshes\\actors\\character\\facegendata\\facegeom\\Dawnguard.esm\\00002B6C.nif", {"Serana Old", "Serana New"}, {}},
      {"meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\000A2C8E.nif", {"Housecarl Pack"}, {}},
      {"meshes\\actors\\character\\facegendata\\facegeom\\Skyrim.esm\\0001414D.nif", {}, {{"Skyrim - Meshes0.bsa", "<Game Data>"}}},
  };
  const auto index = build_face_index(files);
  const std::map<std::string, std::string> names = {{"002B6C:Dawnguard.esm", "Serana"}, {"0A2C8E:Skyrim.esm", "Lydia"}};
  std::size_t total = 0;

  CHECK(query_face_index(index, q(), &names, &total).size() == 3);
  CHECK(total == 3);
  auto query = q();
  query.text = "lydia";
  auto rows = query_face_index(index, query, &names, &total);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0]->form_key == "0A2C8E:Skyrim.esm");
  CHECK(query_face_index(index, query, nullptr, &total).empty());  // the name is only known through `names`
  query.text = "serana old";
  CHECK(query_face_index(index, query, nullptr, &total).size() == 1);
  query = q();
  query.only_alternatives = true;
  CHECK(query_face_index(index, query, nullptr, &total).size() == 1);
  query = q();
  query.source = "Skyrim - Meshes0.bsa";
  CHECK(query_face_index(index, query, nullptr, &total).size() == 1);
  query = q();
  query.flag = "ArchivedOnly";
  CHECK(query_face_index(index, query, nullptr, &total).size() == 1);
  query = q();
  query.limit = 2;
  CHECK(query_face_index(index, query, nullptr, &total).size() == 2);
  CHECK(total == 3);
}

// ---------------------------------------------------------------- matrix with BSA copies

TEST_CASE("Source matrix: a vanilla face shadowed inside a BSA is a drawable candidate", "[facestudio]") {
  const auto m = build_source_matrix(replacer_over_vanilla_bsa());
  REQUIRE(m.candidates.size() == 2);
  const auto* b = m.find("bsa:skyrim - meshes0.bsa");
  REQUIRE(b);
  CHECK(b->archive == "Skyrim - Meshes0.bsa");
  CHECK(b->facegen_mod == "<Game Data>");
  CHECK(b->renderable);
  REQUIRE(b->overlay.size() == 2);
  CHECK(b->overlay[0].data_path == kGeometry);
  CHECK(b->overlay[0].file == "C:\\cache\\bsa\\Skyrim - Meshes0.bsa\\" + kGeometry);
  CHECK(b->label.find("BSA") != std::string::npos);
}

TEST_CASE("Source matrix: archived copies change the identity, loose-only identities stay as they were", "[facestudio]") {
  auto in = replacer_over_vanilla_bsa();
  CHECK(source_identity(in).find("|archive=skyrim - meshes0.bsa") != std::string::npos);
  in.providers.pop_back();
  CHECK(source_identity(in).find("archive=") == std::string::npos);
}

TEST_CASE("Source matrix: an archived copy that cannot be extracted is listed, never drawn", "[facestudio]") {
  auto in = replacer_over_vanilla_bsa();
  in.providers.back().geometry_file.clear();
  in.providers.back().tint_file.clear();
  const auto m = build_source_matrix(in);
  const auto* b = m.find("bsa:skyrim - meshes0.bsa");
  REQUIRE(b);
  CHECK_FALSE(b->renderable);
  CHECK(b->overlay.empty());
}

// ---------------------------------------------------------------- transfer plan

TEST_CASE("Face transfer: the face the game loads with a fitting record needs no patch", "[facestudio]") {
  const auto m = build_source_matrix(replacer_over_vanilla_bsa());
  const auto plan = plan_face_transfer(m, "effective", DraftState::Fresh, "effective");
  CHECK(plan.verdict == TransferVerdict::InUse);
  CHECK(plan.chosen);
  CHECK_FALSE(plan.can_create_patch);
}

TEST_CASE("Face transfer: a shadowed FaceGen needs an MO2 change, never a plugin", "[facestudio]") {
  const auto m = build_source_matrix(replacer_over_vanilla_bsa());
  const auto plan = plan_face_transfer(m, "bsa:skyrim - meshes0.bsa", DraftState::Fresh, "bsa:skyrim - meshes0.bsa");
  CHECK(plan.verdict == TransferVerdict::NeedsLoadOrder);
  CHECK(plan.chosen);
  CHECK_FALSE(plan.can_create_patch);
  CHECK(any_contains(plan.actions, "oculta"));
  CHECK(any_contains(plan.reasons, "no cambia el orden de MO2"));

  const auto loose_matrix = build_source_matrix(facegen_record_mismatch());
  const auto other = plan_face_transfer(loose_matrix, "facegen:modb", DraftState::Empty, "");
  CHECK(other.verdict == TransferVerdict::NeedsLoadOrder);
  CHECK(any_contains(other.actions, "coloca «ModB» por debajo de «ModA»"));
}

TEST_CASE("Face transfer: a record mismatch of the winning face leads to the assisted patch once chosen", "[facestudio]") {
  const auto m = build_source_matrix(facegen_record_mismatch());
  REQUIRE(m.effective());
  REQUIRE(m.effective()->resolution_available);

  const auto not_chosen = plan_face_transfer(m, "effective", DraftState::Empty, "");
  CHECK((not_chosen.verdict == TransferVerdict::PatchReady || not_chosen.verdict == TransferVerdict::NeedsReview));
  CHECK_FALSE(not_chosen.can_create_patch);
  CHECK(any_contains(not_chosen.actions, "Seleccionar"));

  const auto stale = plan_face_transfer(m, "effective", DraftState::StaleInputs, "effective");
  CHECK_FALSE(stale.chosen);
  CHECK_FALSE(stale.can_create_patch);

  const auto chosen = plan_face_transfer(m, "effective", DraftState::Fresh, "effective");
  CHECK(chosen.chosen);
  CHECK(chosen.can_create_patch);
  CHECK(chosen.verdict == (m.effective()->requires_review ? TransferVerdict::NeedsReview : TransferVerdict::PatchReady));
}

TEST_CASE("Face transfer: no FaceGen, damaged files and unknown sources are blocked", "[facestudio]") {
  // A plugin that edits the face without shipping FaceGen.
  auto in = replacer_over_vanilla_bsa();
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Tweak.esp", "Tweak", kFace), link("Replacer.esp", "Replacer", kFace)});
  const auto m = build_source_matrix(in);
  REQUIRE(m.find("record:tweak.esp"));
  const auto plan = plan_face_transfer(m, "record:tweak.esp", DraftState::Fresh, "record:tweak.esp");
  CHECK(plan.verdict == TransferVerdict::Blocked);
  CHECK(plan.requirement == "REQUIERE_CREATION_KIT");
  CHECK_FALSE(plan.can_create_patch);

  // A damaged archived copy.
  auto damaged = replacer_over_vanilla_bsa();
  damaged.providers.back().readable = false;
  const auto dm = build_source_matrix(damaged);
  CHECK(plan_face_transfer(dm, "bsa:skyrim - meshes0.bsa", DraftState::Fresh, "bsa:skyrim - meshes0.bsa").verdict == TransferVerdict::Blocked);

  // An NPC without any FaceGen.
  SourceInputs none;
  none.npc_form_key = "0A2C8E:Skyrim.esm";
  none.profile_digest = "p";
  none.control.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  none.control.facegen.path = kGeometry;
  const auto nm = build_source_matrix(none);
  const auto np = plan_face_transfer(nm, "effective", DraftState::Fresh, "effective");
  CHECK(np.verdict == TransferVerdict::Blocked);
  CHECK(np.requirement == "REQUIERE_CREATION_KIT");

  CHECK(plan_face_transfer(m, "facegen:nothing", DraftState::Fresh, "").verdict == TransferVerdict::Blocked);
  CHECK(to_string(TransferVerdict::NeedsLoadOrder) == "NEEDS_LOAD_ORDER");
}
