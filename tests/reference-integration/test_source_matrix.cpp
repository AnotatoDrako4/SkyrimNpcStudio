#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "sns/appearance/source_matrix.hpp"

using namespace sns::appearance;

namespace {

const std::string kGeometry = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif";
const std::string kTint = "textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds";

OverrideLink link(std::string plugin, std::string mod, std::vector<std::string> changed) {
  OverrideLink l;
  l.plugin = std::move(plugin);
  l.mod = std::move(mod);
  l.changed_fields = std::move(changed);
  return l;
}

std::vector<OverrideLink> chain(std::vector<OverrideLink> links) {
  if (!links.empty()) {
    links.front().is_origin = true;
    links.back().is_winner = true;
  }
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

AssetSource bsa(const std::string& path, std::string mod) {
  AssetSource a;
  a.path = path;
  a.found = true;
  a.winner_mod = std::move(mod);
  a.kind = "bsa";
  return a;
}

ProviderFaceGen provider(const std::string& mod, bool tint = true, std::optional<bool> readable = true) {
  ProviderFaceGen p;
  p.mod = mod;
  p.has_geometry = true;
  p.has_tint = tint;
  p.geometry_file = "D:\\MO2\\mods\\" + mod + "\\" + kGeometry;
  p.tint_file = tint ? "D:\\MO2\\mods\\" + mod + "\\" + kTint : std::string();
  p.geometry_bytes = 1000;
  p.tint_bytes = tint ? 500 : -1;
  p.readable = readable;
  return p;
}

const std::vector<std::string> kFace = {"HeadParts", "FaceMorph", "FaceParts", "HeadTexture"};

// Vanilla NPC: FaceGen in the game's own BSA.
SourceInputs vanilla(const std::string& form_key = "0A2C8E:Skyrim.esm") {
  SourceInputs in;
  in.npc_form_key = form_key;
  in.profile_digest = "profile-1";
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  in.control.facegen = bsa(kGeometry, "<Game Data>");
  in.control.facetint = bsa(kTint, "<Game Data>");
  return in;
}

// ReplacerA (older, lower priority) and ReplacerB (wins) both ship a loose FaceGen; B's record wins.
// `b_reverts` = B's face values are copies of the vanilla ones (a patch put the original face back) instead of
// B's own edits.
SourceInputs two_replacers(bool b_reverts) {
  SourceInputs in;
  in.npc_form_key = "0A2C8E:Skyrim.esm";
  in.profile_digest = "profile-1";
  auto a = link("ReplacerA.esp", "ModA", kFace);
  auto b = link("ReplacerB.esp", "ModB", kFace);
  if (b_reverts) {
    for (const auto& f : kFace) b.copied_from[f] = "Skyrim.esm";
  }
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {}), a, b});
  in.control.facegen = loose(kGeometry, "ModB", {"ModA", "ModB"});
  in.control.facetint = loose(kTint, "ModB", {"ModA", "ModB"});
  in.providers = {provider("ModA"), provider("ModB")};
  return in;
}

bool has_code(const SourceCandidate& c, const std::string& code) { return std::find(c.codes.begin(), c.codes.end(), code) != c.codes.end(); }

std::string lowered(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

TEST_CASE("Source matrix: a vanilla NPC offers only the effective source", "[sourcematrix]") {
  auto in = vanilla();
  in.providers = {};  // the FaceGen lives in a BSA: no loose provider
  in.control.facegen_readable = true;
  const auto m = build_source_matrix(in);

  REQUIRE(m.candidates.size() == 1);
  const auto& c = m.candidates.front();
  CHECK(c.id == "effective");
  CHECK(c.effective);
  CHECK(c.status == SourceStatus::Valid);
  CHECK(c.renderable);
  CHECK(c.overlay.empty());
  CHECK(m.renderable_count() == 1);
}

TEST_CASE("Source matrix: nothing proves the effective FaceGen => UNVERIFIED, never VALID", "[sourcematrix]") {
  auto in = vanilla();
  const auto m = build_source_matrix(in);  // not opened, no deviation measured
  REQUIRE(m.effective());
  CHECK(m.effective()->status == SourceStatus::Unverified);

  in.control.regeneration_deviation = 0.01f;  // measured against the record
  CHECK(build_source_matrix(in).effective()->status == SourceStatus::Valid);
}

TEST_CASE("Source matrix: an NPC without FaceGen has a MISSING source and nothing to offer", "[sourcematrix]") {
  SourceInputs in;
  in.npc_form_key = "0A2C8E:Skyrim.esm";
  in.profile_digest = "profile-1";
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  in.control.facegen.path = kGeometry;  // found = false
  const auto m = build_source_matrix(in);

  REQUIRE(m.candidates.size() == 1);
  CHECK(m.effective()->status == SourceStatus::Missing);
  CHECK_FALSE(m.effective()->renderable);
  CHECK(has_code(*m.effective(), "NoFaceGen"));
  CHECK(m.renderable_count() == 0);
}

TEST_CASE("Source matrix: a shadowed replacer FaceGen is a candidate with its own verdict", "[sourcematrix]") {
  const auto m = build_source_matrix(two_replacers(/*b_copies_a=*/false));

  REQUIRE(m.candidates.size() == 2);
  CHECK(m.candidates[0].id == "effective");
  CHECK(m.candidates[0].facegen_mod == "ModB");
  CHECK(m.candidates[0].status == SourceStatus::Valid);  // record and FaceGen both ModB's, file opened
  CHECK(has_code(m.candidates[0], "ShadowedFaceGen"));

  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  CHECK(a->facegen_mod == "ModA");
  CHECK(a->facetint_mod == "ModA");
  // ModB's record discards ModA's own face edits: fitting ModA's face is a choice a person must settle (D1).
  CHECK(a->status == SourceStatus::Ambiguous);
  CHECK(a->requires_review);
  CHECK(a->resolution_available);
  CHECK(a->renderable);
}

TEST_CASE("Source matrix: when the winner only reverts to vanilla values the mismatch is a CONFLICT, not AMBIGUOUS", "[sourcematrix]") {
  const auto m = build_source_matrix(two_replacers(/*b_reverts=*/true));
  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  CHECK(a->status == SourceStatus::Conflict);
  CHECK_FALSE(a->requires_review);
  CHECK(a->resolution_available);
  CHECK(has_code(*a, "FaceGenRecordMismatch"));
  CHECK(a->codes.front() == "FaceGenRecordMismatch");  // warnings come first
}

TEST_CASE("Source matrix: a winner that forwards the other face leaves BOTH FaceGens coherent with the record", "[sourcematrix]") {
  auto in = two_replacers(false);
  for (const auto& f : kFace) in.control.chain.back().copied_from[f] = "ReplacerA.esp";  // B carries A's face values
  const auto m = build_source_matrix(in);
  // Control map rule: the mod of the FaceGen that writes the record itself owns the value (even a copy), and
  // a copy is attributed to the version that introduced it: either FaceGen matches what the record says.
  CHECK(m.find("facegen:moda")->status == SourceStatus::Valid);
  CHECK(m.effective()->status == SourceStatus::Valid);
  CHECK_FALSE(has_code(*m.find("facegen:moda"), "FaceGenRecordMismatch"));
}

TEST_CASE("Source matrix: a candidate is rendered through overlay files, the effective one through none", "[sourcematrix]") {
  const auto m = build_source_matrix(two_replacers(false));
  CHECK(m.effective()->overlay.empty());

  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  REQUIRE(a->overlay.size() == 2);
  CHECK(a->overlay[0].data_path == kGeometry);
  CHECK(a->overlay[0].file == "D:\\MO2\\mods\\ModA\\" + kGeometry);
  CHECK(a->overlay[1].data_path == kTint);
  CHECK(a->overlay[1].file == "D:\\MO2\\mods\\ModA\\" + kTint);
}

TEST_CASE("Source matrix: a provider without a tint keeps the effective tint and says so", "[sourcematrix]") {
  auto in = two_replacers(false);
  in.providers[0] = provider("ModA", /*tint=*/false);
  const auto m = build_source_matrix(in);
  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  REQUIRE(a->overlay.size() == 1);
  CHECK(a->overlay[0].data_path == kGeometry);
  CHECK(a->facetint_mod == "ModB");
  CHECK(has_code(*a, "FaceTintFromOtherMod"));
  CHECK(a->render_note.find("tinte sigue siendo") != std::string::npos);
}

TEST_CASE("Source matrix: a damaged provider FaceGen is a CONFLICT and is never drawn", "[sourcematrix]") {
  auto in = two_replacers(false);
  in.providers[0] = provider("ModA", true, /*readable=*/false);
  const auto m = build_source_matrix(in);
  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  CHECK(a->status == SourceStatus::Conflict);
  CHECK(has_code(*a, "FaceGenUnreadable"));
  CHECK_FALSE(a->renderable);
  CHECK(a->overlay.empty());
  CHECK(m.renderable_count() == 1);  // only the effective one is left to draw
}

TEST_CASE("Source matrix: a provider without a loose copy cannot be drawn", "[sourcematrix]") {
  auto in = two_replacers(false);
  in.providers[0].geometry_file.clear();
  const auto m = build_source_matrix(in);
  const auto* a = m.find("facegen:moda");
  REQUIRE(a);
  CHECK_FALSE(a->renderable);
  CHECK(a->overlay.empty());
}

TEST_CASE("Source matrix: a plugin that changes the face but ships no FaceGen is listed as MISSING, never drawn", "[sourcematrix]") {
  SourceInputs in;
  in.npc_form_key = "0A2C8E:Skyrim.esm";
  in.profile_digest = "profile-1";
  in.control.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("ReplacerA.esp", "ModA", kFace), link("PatchC.esp", "ModC", {"HairColor"})});
  in.control.facegen = loose(kGeometry, "ModA", {"ModA"});
  in.control.facetint = loose(kTint, "ModA", {"ModA"});
  in.providers = {provider("ModA")};
  const auto m = build_source_matrix(in);

  const auto* c = m.find("record:patchc.esp");
  REQUIRE(c);
  CHECK(c->status == SourceStatus::Missing);
  CHECK_FALSE(c->renderable);
  CHECK(c->overlay.empty());
  CHECK(c->record_mod == "ModC");
  CHECK(m.renderable_count() == 1);
}

TEST_CASE("Source matrix: ids are lower case, unique and ordered by descending MO2 priority", "[sourcematrix]") {
  auto in = two_replacers(false);
  in.control.facegen = loose(kGeometry, "ModC", {"ModA", "ModB", "ModC"});
  in.providers = {provider("ModA"), provider("ModB"), provider("ModC")};
  const auto m = build_source_matrix(in);

  REQUIRE(m.candidates.size() == 3);
  CHECK(m.candidates[0].id == "effective");
  CHECK(m.candidates[1].id == "facegen:modb");  // higher priority first
  CHECK(m.candidates[2].id == "facegen:moda");
  for (const auto& c : m.candidates) CHECK(c.id == lowered(c.id));
}

TEST_CASE("Source matrix: the identity is deterministic and changes with every input it depends on", "[sourcematrix]") {
  const auto base = two_replacers(false);
  const auto id = build_source_matrix(base).identity;
  CHECK(build_source_matrix(base).identity == id);
  CHECK(source_identity(base) == id);

  auto profile = base;
  profile.profile_digest = "profile-2";
  CHECK(build_source_matrix(profile).identity != id);

  auto sha = base;
  sha.providers[0].geometry_sha256 = "abc";  // the FaceGen of ModA was replaced
  CHECK(build_source_matrix(sha).identity != id);

  auto moved = base;
  moved.control.facegen.winner_mod = "ModA";  // a mod moved in MO2
  CHECK(build_source_matrix(moved).identity != id);

  auto readable = base;
  readable.providers[1].readable = false;
  CHECK(build_source_matrix(readable).identity != id);
}

TEST_CASE("Source matrix: 20 synthetic NPCs of five topologies get the expected verdicts", "[sourcematrix]") {
  std::size_t renderable_total = 0;
  for (int i = 0; i < 20; ++i) {
    char key[32];
    std::snprintf(key, sizeof(key), "%06X:Skyrim.esm", 0x0A2C00 + i);
    SourceInputs in;
    switch (i % 5) {
      case 0: in = vanilla(key); in.control.facegen_readable = true; break;
      case 1:
        in = two_replacers(false);
        in.providers.erase(in.providers.begin());
        in.control.facegen = loose(kGeometry, "ModB", {"ModB"});
        break;
      case 2: in = two_replacers(false); break;
      case 3: in = two_replacers(true); break;
      default: in = vanilla(key); in.control.facegen.found = false; break;
    }
    in.npc_form_key = key;
    const auto m = build_source_matrix(in);
    REQUIRE(m.effective());
    renderable_total += m.renderable_count();
    switch (i % 5) {
      case 0: CHECK(m.effective()->status == SourceStatus::Valid); CHECK(m.candidates.size() == 1); break;
      case 1:  // ReplacerA's plugin changes the face but its mod ships no FaceGen here: listed, never drawn
        CHECK(m.effective()->status == SourceStatus::Valid);
        REQUIRE(m.candidates.size() == 2);
        CHECK(m.candidates[1].status == SourceStatus::Missing);
        break;
      case 2: REQUIRE(m.candidates.size() == 2); CHECK(m.candidates[1].status == SourceStatus::Ambiguous); break;
      case 3: REQUIRE(m.candidates.size() == 2); CHECK(m.candidates[1].status == SourceStatus::Conflict); break;
      default: CHECK(m.effective()->status == SourceStatus::Missing); CHECK(m.renderable_count() == 0); break;
    }
    // No unverified data is ever presented as available.
    for (const auto& c : m.candidates) {
      if (c.renderable) CHECK_FALSE(c.render_note.empty());
      if (c.status == SourceStatus::Missing) CHECK_FALSE(c.renderable);
    }
  }
  CHECK(renderable_total == 4 * (1 + 1 + 2 + 2 + 0));  // renderable candidates per topology, four NPCs each
}

TEST_CASE("Source matrix: status names round-trip", "[sourcematrix]") {
  for (const auto s : {SourceStatus::Valid, SourceStatus::Unverified, SourceStatus::Conflict, SourceStatus::Ambiguous, SourceStatus::Missing}) {
    CHECK(source_status_from(to_string(s)) == s);
  }
  CHECK_FALSE(source_status_from("valid"));  // exact upper-case codes only
  CHECK_FALSE(source_status_from(""));
}
