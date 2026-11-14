#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "sns/appearance/npc_audit.hpp"

// NPC Health & Beauty Center, P1: coded audit findings over the control map. Every input is synthetic and
// shaped after cases of the real test profile (SNS-Cuerpos-Test): the control map runs for real, the audit
// only reads its report plus the head-part facts.
using namespace sns::appearance;

namespace {

constexpr const char* kGeom = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif";
constexpr const char* kTint = "textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds";

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
  a.kind = "loose";
  a.winner_mod = std::move(winner);
  a.loose_mods = std::move(mods);
  return a;
}

AssetSource bsa(const std::string& path, std::string mod) {
  AssetSource a;
  a.path = path;
  a.found = true;
  a.kind = "bsa";
  a.winner_mod = std::move(mod);
  return a;
}

AssetSource missing(const std::string& path) {
  AssetSource a;
  a.path = path;
  return a;
}

AuditHeadPart hair(std::string editor_id, AssetSource mesh, std::optional<bool> in_facegen = true) {
  AuditHeadPart p;
  p.form_key = "0A1B2C:KSHairdosSSE.esp";
  p.editor_id = std::move(editor_id);
  p.type = "Hair";
  p.plugin = "KSHairdosSSE.esp";
  p.record_mod = "KS Hairdos SSE";
  p.model_path = mesh.path;
  p.mesh = std::move(mesh);
  p.in_facegen = in_facegen;
  return p;
}

// A vanilla NPC whose face comes from the game's BSA: record and FaceGen are Skyrim.esm's.
ControlInputs vanilla_face() {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  in.facegen = bsa(kGeom, "<Game Data>");
  in.facetint = bsa(kTint, "<Game Data>");
  in.regeneration_deviation = 0.001f;
  return in;
}

AuditSubject subject(std::vector<AuditHeadPart> parts = {}) {
  AuditSubject s;
  s.form_key = "0A2C8E:Skyrim.esm";
  s.editor_id = "Ysolda";
  s.name = "Ysolda";
  s.defining_plugin = "Skyrim.esm";
  s.winning_plugin = "Skyrim.esm";
  s.head_parts = std::move(parts);
  return s;
}

NpcAudit run(const AuditSubject& s, const ControlInputs& in) { return audit_npc_appearance(s, in, analyze_appearance_control(in)); }

const AuditFinding* code(const NpcAudit& a, const std::string& c) {
  const auto it = std::find_if(a.findings.begin(), a.findings.end(), [&](const AuditFinding& f) { return f.code == c; });
  return it == a.findings.end() ? nullptr : &*it;
}

bool has_prefix(const NpcAudit& a, const std::string& prefix) {
  return std::any_of(a.findings.begin(), a.findings.end(), [&](const AuditFinding& f) { return f.code.rfind(prefix, 0) == 0; });
}

bool exempt(const NpcAudit& a, const std::string& e) { return std::find(a.exemptions.begin(), a.exemptions.end(), e) != a.exemptions.end(); }

const std::string kHairMesh = "meshes\\ks hairdos\\braids.nif";

}  // namespace

// ---- fixture 1: valid hair
TEST_CASE("NPC audit: a hair whose mesh is in the profile and in the FaceGen is clean", "[appearance][npcaudit]") {
  const auto a = run(subject({hair("KSHairBraids", loose(kHairMesh, "KS Hairdos SSE", {"KS Hairdos SSE"}))}), vanilla_face());
  CHECK(a.form_key == "0A2C8E:Skyrim.esm");
  CHECK(a.status == kStatusClean);
  CHECK(a.findings.empty());
  CHECK(a.exemptions.empty());
}

// ---- fixture 2: intentionally bald
TEST_CASE("NPC audit: no hair in the record nor in the FaceGen is intentional baldness, not a defect", "[appearance][npcaudit]") {
  const auto a = run(subject(), vanilla_face());
  CHECK(a.status == kStatusClean);
  CHECK_FALSE(has_prefix(a, "HAIR_"));
  CHECK(exempt(a, "calvo-intencional"));
}

// ---- fixture 3: hair head part whose mesh is missing
TEST_CASE("NPC audit: a hair mesh missing from the profile", "[appearance][npcaudit][hair]") {
  SECTION("and absent from the FaceGen is a confirmed bald cause") {
    const auto a = run(subject({hair("KSHairBraids", missing(kHairMesh), false)}), vanilla_face());
    const auto* f = code(a, "HAIR_001");
    REQUIRE(f);
    CHECK(f->status == kStatusConfirmed);
    CHECK(f->severity == "alta");
    CHECK(f->verification == kVerifyInGame);
    CHECK(f->resource == kHairMesh);
    CHECK(f->responsible == "KS Hairdos SSE");
    CHECK(f->evidence.find("KSHairBraids") != std::string::npos);
    CHECK(a.status == kStatusConfirmed);
    // The FaceGen lacks the record's hair as well: the head the game loads has no hair.
    CHECK(code(a, "HAIR_006"));
  }
  SECTION("but carried by the FaceGen is only a suspicion: the game draws the FaceGen copy") {
    const auto a = run(subject({hair("KSHairBraids", missing(kHairMesh), true)}), vanilla_face());
    const auto* f = code(a, "HAIR_001");
    REQUIRE(f);
    CHECK(f->status == kStatusSuspected);
    CHECK(f->severity == "baja");
    CHECK(a.status == kStatusSuspected);
  }
  SECTION("with no FaceGen at all the game builds the head from the missing mesh") {
    auto in = vanilla_face();
    in.facegen = missing(kGeom);
    in.facetint = missing(kTint);
    const auto a = run(subject({hair("KSHairBraids", missing(kHairMesh), std::nullopt)}), in);
    REQUIRE(code(a, "HAIR_001"));
    CHECK(code(a, "HAIR_001")->status == kStatusConfirmed);
    CHECK(code(a, "FACE_001"));
  }
}

TEST_CASE("NPC audit: a hair head part that is not in the load order is a broken reference", "[appearance][npcaudit][hair]") {
  auto part = hair("", missing(""), std::nullopt);
  part.record_resolved = false;
  part.model_path.clear();
  const auto a = run(subject({part}), vanilla_face());
  const auto* f = code(a, "HAIR_002");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->resource == "0A1B2C:KSHairdosSSE.esp");
  CHECK_FALSE(code(a, "HAIR_001"));  // nothing to resolve: the record itself is gone
}

TEST_CASE("NPC audit: a hair the viewer drew without geometry needs a look in game", "[appearance][npcaudit][hair]") {
  auto part = hair("KSHairBraids", loose(kHairMesh, "KS Hairdos SSE", {"KS Hairdos SSE"}));
  part.visible_in_viewer = false;
  const auto a = run(subject({part}), vanilla_face());
  REQUIRE(code(a, "HAIR_003"));
  CHECK(code(a, "HAIR_003")->status == kStatusSuspected);
  CHECK(code(a, "NPC_004"));
}

TEST_CASE("NPC audit: a hair texture missing from the profile", "[appearance][npcaudit][hair]") {
  auto part = hair("KSHairBraids", loose(kHairMesh, "KS Hairdos SSE", {"KS Hairdos SSE"}));
  part.textures = {loose("textures\\ks hairdos\\braids_d.dds", "KS Hairdos SSE", {"KS Hairdos SSE"}), missing("textures\\ks hairdos\\braids_n.dds")};
  const auto a = run(subject({part}), vanilla_face());
  const auto* f = code(a, "HAIR_004");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->resource == "textures\\ks hairdos\\braids_n.dds");
}

// ---- fixture 4: hair in a BSA
TEST_CASE("NPC audit: a hair mesh inside a BSA counts as present", "[appearance][npcaudit][hair]") {
  const auto a = run(subject({hair("HairFemaleNord01", bsa("meshes\\actors\\character\\character assets\\hair\\femalehair01.nif", "<Game Data>"))}),
                     vanilla_face());
  CHECK(a.status == kStatusClean);
  CHECK_FALSE(has_prefix(a, "HAIR_"));
}

// ---- fixture 5: hair overwritten by a higher-priority mod
TEST_CASE("NPC audit: the hair mod's own mesh losing to another mod is reported", "[appearance][npcaudit][hair]") {
  const auto a = run(subject({hair("KSHairBraids", loose(kHairMesh, "Other Hair Pack", {"KS Hairdos SSE", "Other Hair Pack"}))}), vanilla_face());
  const auto* f = code(a, "HAIR_005");
  REQUIRE(f);
  CHECK(f->status == kStatusSuspected);
  CHECK(f->responsible == "Other Hair Pack");
  CHECK(f->evidence.find("KS Hairdos SSE") != std::string::npos);

  // A mesh only another mod provides (the hair mod ships just the plugin) is not an overwrite.
  const auto only_other = run(subject({hair("KSHairBraids", loose(kHairMesh, "Hair Meshes", {"Hair Meshes"}))}), vanilla_face());
  CHECK_FALSE(code(only_other, "HAIR_005"));
}

TEST_CASE("NPC audit: the record's hair missing from a FaceGen that has no hair at all is a confirmed bald head", "[appearance][npcaudit][hair]") {
  auto s = subject({hair("KSHairBraids", loose(kHairMesh, "KS Hairdos SSE", {"KS Hairdos SSE"}), false)});
  s.facegen_has_hair = false;
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_006");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "alta");
}

TEST_CASE("NPC audit: record head parts partly absent from the FaceGen prove a record/FaceGen mismatch", "[appearance][npcaudit][face]") {
  auto brows = hair("FemaleBrowsHuman06", bsa("meshes\\actors\\character\\character assets\\faceparts\\femalebrows.nif", "<Game Data>"), true);
  brows.type = "Eyebrows";
  auto eyes = hair("FemaleEyesHumanLightGrey", bsa("meshes\\actors\\character\\character assets\\eyesfemale.nif", "<Game Data>"), false);
  eyes.type = "Eyes";
  const auto a = run(subject({brows, eyes}), vanilla_face());  // the control map alone sees nothing wrong here
  const auto* f = code(a, "FACE_003");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->control_code.empty());
  CHECK(f->evidence ==
        "El FaceGen efectivo no contiene piezas del registro: FemaleEyesHumanLightGrey (sí contiene FemaleBrowsHuman06).");
  CHECK_FALSE(code(a, "FACE_006"));
}

TEST_CASE("NPC audit: a FaceGen with a hair the record does not list", "[appearance][npcaudit][hair]") {
  auto s = subject();
  s.facegen_foreign_hair_shapes = {"HairFemaleNord05"};
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_006");
  REQUIRE(f);
  CHECK(f->status == kStatusSuspected);
  CHECK_FALSE(exempt(a, "calvo-intencional"));
}

// ---- fixtures 6 and 7: FaceGeom / FaceTint missing
TEST_CASE("NPC audit: missing FaceGeom and FaceTint", "[appearance][npcaudit][face]") {
  auto in = vanilla_face();
  in.facegen = missing(kGeom);
  in.facetint = missing(kTint);
  const auto none = run(subject(), in);
  const auto* geom = code(none, "FACE_001");
  REQUIRE(geom);
  CHECK(geom->status == kStatusConfirmed);
  CHECK(geom->resource == kGeom);
  CHECK(geom->control_code == "NoFaceGen");
  CHECK(geom->verification == kVerifyInGame);
  CHECK_FALSE(code(none, "FACE_002"));  // a missing tint only matters next to a geometry

  auto tint_only = vanilla_face();
  tint_only.facetint = missing(kTint);
  const auto t = run(subject(), tint_only);
  REQUIRE(code(t, "FACE_002"));
  CHECK(code(t, "FACE_002")->resource == kTint);
  CHECK_FALSE(code(t, "FACE_001"));
}

TEST_CASE("NPC audit: a damaged FaceGen", "[appearance][npcaudit][face]") {
  auto in = vanilla_face();
  in.facegen_readable = false;
  const auto a = run(subject(), in);
  REQUIRE(code(a, "FACE_005"));
  CHECK(code(a, "FACE_005")->status == kStatusConfirmed);
  CHECK(code(a, "FACE_005")->severity == "alta");
}

// ---- fixture 8: FaceGen and appearance plugin incompatible
namespace {
ControlInputs aio_over_nithi() {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}),
                    link("NITHI NPC Enhancement - Riften.esp", "NITHI NPC Enhancement - Riften",
                         {"HeadParts", "FaceMorph", "FaceParts", "TintLayers", "TextureLighting", "HairColor"}),
                    link("AI Overhaul.esp", "AI Overhaul 1.8", {"FaceMorph", "FaceParts", "TintLayers", "TextureLighting", "Spells"})});
  in.facegen = loose(kGeom, "NITHI NPC Enhancement - Riften", {"NITHI NPC Enhancement - Riften"});
  in.facetint = loose(kTint, "NITHI NPC Enhancement - Riften", {"NITHI NPC Enhancement - Riften"});
  return in;
}
}  // namespace

TEST_CASE("NPC audit: record face from one mod and FaceGen from another", "[appearance][npcaudit][face]") {
  SECTION("by attribution only it is a suspicion") {
    const auto a = run(subject(), aio_over_nithi());
    const auto* f = code(a, "FACE_003");
    REQUIRE(f);
    CHECK(f->status == kStatusSuspected);
    CHECK(f->control_code == "FaceGenRecordMismatch");
    CHECK(f->responsible == "AI Overhaul.esp");
    CHECK(f->verification == kVerifyInGame);
  }
  SECTION("measured by the facial analysis it is confirmed") {
    auto in = aio_over_nithi();
    in.regeneration_deviation = 0.84f;
    const auto a = run(subject(), in);
    REQUIRE(code(a, "FACE_003"));
    CHECK(code(a, "FACE_003")->status == kStatusConfirmed);
    CHECK(code(a, "FACE_003")->evidence.find("0.840") != std::string::npos);
  }
}

// Real shape (SNS-Cuerpos-Test, Aela 01A696:Skyrim.esm): AI Overhaul's record lists vanilla parts while NITHI's
// FaceGen holds NITHIAELA_* pieces. The FaceGen has hair, just not the record's: not bald, a record/FaceGen mismatch.
TEST_CASE("NPC audit: a FaceGen with another hair than the record's is not bald", "[appearance][npcaudit][hair]") {
  auto eyes = hair("FemaleEyesHumanLightGrey", bsa("meshes\\actors\\character\\character assets\\eyesfemale.nif", "<Game Data>"), false);
  eyes.type = "Eyes";
  auto s = subject({hair("HairFemaleNord18", bsa("meshes\\actors\\character\\character assets\\hair\\female\\hair18.nif", "<Game Data>"), false), eyes});
  s.facegen_has_hair = true;
  s.facegen_foreign_hair_shapes = {"NITHIAELA_Hair1", "NITHIAELA_Hair2"};
  const auto a = run(s, aio_over_nithi());
  const auto* f = code(a, "HAIR_006");
  REQUIRE(f);
  CHECK(f->status == kStatusSuspected);
  CHECK(f->evidence.find("NITHIAELA_Hair1") != std::string::npos);
  CHECK_FALSE(code(a, "HAIR_001"));
  CHECK_FALSE(code(a, "FACE_006"));
  // One FACE_003: the control map's attribution plus the shape evidence (no record part has a shape, so it stays
  // a suspicion: the FaceGen may use another naming convention).
  CHECK(std::count_if(a.findings.begin(), a.findings.end(), [](const AuditFinding& x) { return x.code == "FACE_003"; }) == 1);
  const auto* face = code(a, "FACE_003");
  REQUIRE(face);
  CHECK(face->status == kStatusSuspected);
  CHECK(face->control_code == "FaceGenRecordMismatch");
  CHECK(face->evidence.find("FemaleEyesHumanLightGrey") != std::string::npos);
}

// ---- fixture 9: FaceGen legitimately split between two packages
TEST_CASE("NPC audit: geometry and tint from two packages of one mod is not a broken pair", "[appearance][npcaudit][face]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Bijin.esp", "Bijin - Meshes", {"HeadParts", "FaceMorph", "TintLayers"})});
  in.facegen = loose(kGeom, "Bijin - Meshes", {"Bijin - Meshes"});
  in.facetint = loose(kTint, "Bijin - Textures", {"Bijin - Textures"});
  const auto split = run(subject(), in);
  CHECK_FALSE(code(split, "FACE_004"));
  CHECK_FALSE(code(split, "FACE_002"));

  // The geometry's mod ships its own tint and loses it to another mod: the pair is broken.
  in.facetint = loose(kTint, "Old Face Pack", {"Bijin - Meshes", "Old Face Pack"});
  const auto broken = run(subject(), in);
  const auto* f = code(broken, "FACE_004");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->responsible == "Old Face Pack");
}

TEST_CASE("NPC audit: a face head part the race cannot use or the FaceGen lacks", "[appearance][npcaudit][face]") {
  AuditHeadPart head;
  head.form_key = "000801:HighPolyHead.esm";
  head.editor_id = "00KLH_HeadFemaleNord";
  head.type = "Face";
  head.plugin = "HighPolyHead.esm";
  head.record_mod = "High Poly Head";
  head.model_path = "meshes\\actors\\character\\character assets\\highpolyhead\\femalehead.nif";
  head.mesh = loose(head.model_path, "High Poly Head", {"High Poly Head"});
  head.valid_for_race = false;
  head.in_facegen = false;
  const auto a = run(subject({head}), vanilla_face());
  const auto* f = code(a, "FACE_006");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->evidence.find("00KLH_HeadFemaleNord") != std::string::npos);
  // A part the race accepts but the FaceGen lacks is not FACE_006: it is evidence of a FaceGen made for other parts.
  head.valid_for_race = true;
  const auto b = run(subject({head}), vanilla_face());
  CHECK_FALSE(code(b, "FACE_006"));
  CHECK(code(b, "FACE_003"));
}

// ---- fixture 10: traits template
TEST_CASE("NPC audit: an NPC that takes its traits from a template is exempt from face and hair checks", "[appearance][npcaudit]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  in.facegen = missing(kGeom);
  in.facetint = missing(kTint);
  in.facegen_expected = false;
  auto s = subject();
  s.uses_traits_template = true;
  const auto a = run(s, in);
  CHECK(a.status == kStatusClean);
  CHECK(exempt(a, "plantilla-traits"));
  CHECK_FALSE(has_prefix(a, "FACE_"));
  CHECK_FALSE(has_prefix(a, "HAIR_"));

  // Creatures: the race never uses FaceGen.
  auto creature = subject();
  creature.race_uses_facegen = false;
  const auto c = run(creature, in);
  CHECK(c.status == kStatusClean);
  CHECK(exempt(c, "raza-sin-facegen"));
}

TEST_CASE("NPC audit: the player record never needs FaceGen; unplaced NPCs keep the finding at low priority", "[appearance][npcaudit]") {
  auto in = vanilla_face();
  in.facegen = missing(kGeom);
  in.facetint = missing(kTint);
  auto player = subject();
  player.form_key = "000007:Skyrim.esm";
  player.is_player = true;
  const auto p = run(player, in);
  CHECK(p.status == kStatusClean);
  CHECK(exempt(p, "jugador"));

  // Real shape: Vanto Loreio (01B1D7:USSEP) has no FaceGen in USSEP's BSA.
  auto unplaced = subject();
  unplaced.placed_references = 0;
  const auto u = run(unplaced, in);
  REQUIRE(code(u, "FACE_001"));
  CHECK(code(u, "FACE_001")->status == kStatusConfirmed);
  CHECK(code(u, "FACE_001")->severity == "baja");
  CHECK(code(u, "FACE_001")->evidence.find("no tiene referencias colocadas") != std::string::npos);
  unplaced.placed_references = 3;
  CHECK(code(run(unplaced, in), "FACE_001")->severity == "alta");
}

TEST_CASE("NPC audit: CharGen presets do not need FaceGen", "[appearance][npcaudit]") {
  auto in = vanilla_face();
  in.facegen = missing(kGeom);
  in.facetint = missing(kTint);
  auto s = subject();
  s.chargen_preset = true;
  const auto a = run(s, in);
  CHECK(exempt(a, "preset-chargen"));
  CHECK_FALSE(code(a, "FACE_001"));
}

// ---- fixture 11 (ESL) lives in the worker tests (FaceGen path); here: files left under an old FormID
TEST_CASE("NPC audit: FaceGen files under an older plugin folder or FormID", "[appearance][npcaudit]") {
  auto s = subject();
  s.stale_facegen_paths = {"meshes\\actors\\character\\facegendata\\facegeom\\mymod.esp\\00012345.nif"};
  const auto a = run(s, vanilla_face());
  REQUIRE(code(a, "NPC_003"));
  CHECK(code(a, "NPC_003")->status == kStatusSuspected);
  CHECK(code(a, "NPC_003")->resource == s.stale_facegen_paths.front());
}

TEST_CASE("NPC audit: missing masters are confirmed", "[appearance][npcaudit]") {
  auto s = subject();
  s.missing_masters = {"Missing.esm"};
  const auto a = run(s, vanilla_face());
  REQUIRE(code(a, "NPC_002"));
  CHECK(code(a, "NPC_002")->status == kStatusConfirmed);
  CHECK(code(a, "NPC_002")->severity == "alta");
}

// ---- fixture 12: a later override changes AI but reverts the appearance
TEST_CASE("NPC audit: a later override that puts an older face back", "[appearance][npcaudit]") {
  SECTION("with copied values it is a confirmed accidental revert") {
    auto in = aio_over_nithi();
    for (const auto& field : in.chain.back().changed_fields) in.chain.back().copied_from[field] = "Skyrim.esm";
    in.chain.back().copied_from.erase("Spells");
    in.lost_fields = {"FaceMorph", "FaceParts", "TintLayers", "TextureLighting"};
    const auto a = run(subject(), in);
    const auto* f = code(a, "NPC_001");
    REQUIRE(f);
    CHECK(f->status == kStatusConfirmed);
    CHECK(f->responsible == "AI Overhaul.esp");
    // The fix is the resolution patch: regenerating would replace NITHI's own head with the reverted face.
    CHECK_FALSE(a.facegen_regeneration_safe);
  }
  SECTION("with the overrider's own values it may be deliberate") {
    auto in = aio_over_nithi();
    in.lost_fields = {"FaceMorph", "FaceParts", "TintLayers", "TextureLighting"};
    const auto a = run(subject(), in);
    REQUIRE(code(a, "NPC_001"));
    CHECK(code(a, "NPC_001")->status == kStatusSuspected);
    // Discarding the overrider's own face edits is a human decision: never regenerate by itself.
    CHECK_FALSE(a.facegen_regeneration_safe);
  }
}

// ---- fixture 13: custom head that must not be regenerated
TEST_CASE("NPC audit: a replacer's sculpted head is never marked safe to regenerate", "[appearance][npcaudit]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Serana.esp", "Seranaholic", {"HeadParts", "FaceMorph", "TintLayers"})});
  in.facegen = loose(kGeom, "Seranaholic", {"Seranaholic"});
  in.facetint = loose(kTint, "Seranaholic", {"Seranaholic"});
  in.regeneration_deviation = 1.2f;  // sculpted beyond what the record produces
  const auto a = run(subject(), in);
  CHECK(a.status == kStatusClean);
  CHECK_FALSE(a.facegen_regeneration_safe);

  // Vanilla heads from the game's own BSA, or no FaceGen at all, may be regenerated.
  CHECK(run(subject(), vanilla_face()).facegen_regeneration_safe);
  in.facegen = missing(kGeom);
  in.facetint = missing(kTint);
  CHECK(run(subject(), in).facegen_regeneration_safe);
}

TEST_CASE("NPC audit: every code has a title and findings are ordered worst first", "[appearance][npcaudit]") {
  for (const auto* c : {"HAIR_001", "HAIR_002", "HAIR_003", "HAIR_004", "HAIR_005", "HAIR_006", "FACE_001", "FACE_002", "FACE_003",
                        "FACE_004", "FACE_005", "FACE_006", "FACE_007", "NPC_001", "NPC_002", "NPC_003", "NPC_004"}) {
    CHECK_FALSE(audit_code_title(c).empty());
  }
  CHECK(audit_code_title("XYZ").empty());

  auto s = subject({hair("KSHairBraids", loose(kHairMesh, "Other Hair Pack", {"KS Hairdos SSE", "Other Hair Pack"}), false)});
  s.missing_masters = {"Missing.esm"};
  const auto a = run(s, vanilla_face());
  REQUIRE(a.findings.size() >= 2);
  CHECK(a.findings.front().status == kStatusConfirmed);
  CHECK(a.findings.back().status == kStatusSuspected);
}

// ======================================================================== HAIR SMP (P0: bald NPC with SMP hair)

namespace {

AuditHairArmor wig(std::optional<int> addons_for_race, std::vector<AssetSource> meshes = {}) {
  AuditHairArmor w;
  w.form_key = "0012DA:[Dint999] BDOr_Hairstyles.esp";
  w.editor_id = "BDOrWigAlina";
  w.plugin = "[Dint999] BDOr_Hairstyles UBE patch.esp";
  w.record_mod = "(Dint999) BDOR Hairs SSE - Wigs (UBE)";
  w.smp = true;
  w.addons_for_race = addons_for_race;
  w.meshes = std::move(meshes);
  return w;
}

constexpr const char* kWigMesh = "meshes\\dint999\\bdor\\wigs\\alina.nif";
constexpr const char* kWigXml = "meshes\\dint999\\bdor\\smp\\alina.xml";
constexpr const char* kSmpHairMesh = "meshes\\dint999\\bdor\\hair\\alina.nif";
constexpr const char* kNordHair18 = "meshes\\actors\\character\\character assets\\hair\\female\\hair18.nif";

// SMP hair baked into the FaceGen: skinned to two physics bones of the hair's own NIF.
AuditSmpFaceGenHair smp_hair() {
  AuditSmpFaceGenHair h;
  h.shape = "BDOrHairAlina";
  h.source_mesh = kSmpHairMesh;
  h.physics_bones = {"HairBone01", "HairBone02"};
  h.source_physics_xml = kWigXml;
  h.facegen_has_physics_xml = true;
  h.physics_xml = loose(kWigXml, "[Dint999] BDOR Hairs SSE 0.24", {"[Dint999] BDOR Hairs SSE 0.24"});
  h.max_bone_offset = 0.0f;
  return h;
}

AuditSubject smp_subject() {
  auto s = subject({hair("BDOrHairAlina", loose(kSmpHairMesh, "[Dint999] BDOR Hairs SSE 0.24", {"[Dint999] BDOR Hairs SSE 0.24"}), true)});
  s.facegen_has_hair = true;
  s.fsmp_installed = true;
  s.smp_facegen = {smp_hair()};
  return s;
}

bool has_smp(const NpcAudit& a) { return has_prefix(a, "HAIR_SMP_"); }

}  // namespace

TEST_CASE("Hair SMP: a fully working SMP hair raises nothing", "[appearance][npcaudit][smp]") {
  const auto a = run(smp_subject(), vanilla_face());
  CHECK_FALSE(has_smp(a));
  CHECK(a.status == kStatusClean);
}

TEST_CASE("Hair SMP: a wig with nothing to draw for the NPC's race hides the hair -- the false negative", "[appearance][npcaudit][smp]") {
  auto s = subject({hair("HairFemaleNord18", bsa(kNordHair18, "<Game Data>"))});
  // Before P0 the audit saw only the record and the FaceGen: both fine, so the NPC was "SIN_ERROR_DETECTADO".
  CHECK(run(s, vanilla_face()).status == kStatusClean);
  // The default outfit puts a UBE-only wig in the Hair slot: the game hides her hair and draws nothing instead.
  s.hair_armors = {wig(0)};
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_007");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "alta");
  CHECK(f->resource == "0012DA:[Dint999] BDOr_Hairstyles.esp");
  CHECK(f->responsible == "[Dint999] BDOr_Hairstyles UBE patch.esp");
  CHECK(f->evidence.find("BDOrWigAlina") != std::string::npos);
  CHECK(a.status == kStatusConfirmed);

  SECTION("a wig whose addons could not be resolved is only suspected") {
    s.hair_armors = {wig(std::nullopt)};
    REQUIRE(code(run(s, vanilla_face()), "HAIR_SMP_007"));
    CHECK(code(run(s, vanilla_face()), "HAIR_SMP_007")->status == kStatusSuspected);
  }
  SECTION("a wig with an addon for the race and its mesh present is fine") {
    s.hair_armors = {wig(1, {loose(kWigMesh, "(Dint999) BDOR Hairs SSE - Wigs (UBE)", {"(Dint999) BDOR Hairs SSE - Wigs (UBE)"})})};
    CHECK_FALSE(has_smp(run(s, vanilla_face())));
  }
}

TEST_CASE("Hair SMP: a wig mesh missing from the profile", "[appearance][npcaudit][smp]") {
  auto s = subject();
  s.hair_armors = {wig(1, {missing(kWigMesh)})};
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_001");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "alta");
  CHECK(f->resource == kWigMesh);
}

TEST_CASE("Hair SMP: a missing physics XML leaves the hair visible but static", "[appearance][npcaudit][smp]") {
  auto s = smp_subject();
  s.smp_facegen.front().physics_xml = missing(kWigXml);
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_002");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "media");  // physics missing is not baldness
  CHECK(f->evidence.find("visible") != std::string::npos);
  CHECK_FALSE(code(a, "HAIR_SMP_004"));

  // A static hair (its own NIF declares no XML) needs no XML at all.
  auto still = smp_subject();
  still.smp_facegen.front().source_physics_xml.clear();
  still.smp_facegen.front().facegen_has_physics_xml = false;
  still.smp_facegen.front().physics_xml.reset();
  CHECK_FALSE(has_smp(run(still, vanilla_face())));
}

TEST_CASE("Hair SMP: the FaceGen lost the physics path the hair NIF declares", "[appearance][npcaudit][smp]") {
  auto s = smp_subject();
  s.smp_facegen.front().facegen_has_physics_xml = false;
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_003");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "media");
  CHECK(f->evidence.find(kWigXml) != std::string::npos);
}

TEST_CASE("Hair SMP: FaceGen bones with another rest pose than the hair's NIF displace the hair", "[appearance][npcaudit][smp]") {
  auto s = smp_subject();
  s.smp_facegen.front().max_bone_offset = 12.5f;
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_004");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->severity == "alta");
  CHECK(f->evidence.find("12.50") != std::string::npos);
  // Within tolerance it is the same pose.
  s.smp_facegen.front().max_bone_offset = kSmpBoneTolerance / 2;
  CHECK_FALSE(code(run(s, vanilla_face()), "HAIR_SMP_004"));

  // FaceGen bones the hair's NIF does not have: the FaceGen was made with another version of the hair.
  auto other = smp_subject();
  other.smp_facegen.front().bones_missing_in_source = {"HairBone07"};
  const auto o = run(other, vanilla_face());
  const auto* g = code(o, "HAIR_SMP_005");
  REQUIRE(g);
  CHECK(g->status == kStatusSuspected);
}

TEST_CASE("Hair SMP: SMP hair without the FSMP plugin", "[appearance][npcaudit][smp]") {
  auto s = smp_subject();
  s.fsmp_installed = false;
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_008");
  REQUIRE(f);
  CHECK(f->status == kStatusConfirmed);
  CHECK(f->resource == "SKSE\\Plugins\\hdtSMP64.dll");
  // Unknown FSMP state is never reported as missing.
  s.fsmp_installed.reset();
  CHECK_FALSE(code(run(s, vanilla_face()), "HAIR_SMP_008"));
}

TEST_CASE("Hair SMP: an NPC seen bald in game with no confirmed cause stays open", "[appearance][npcaudit][smp]") {
  auto s = subject({hair("HairFemaleNord18", bsa(kNordHair18, "<Game Data>"))});
  s.observed_bald_in_game = true;
  const auto a = run(s, vanilla_face());
  const auto* f = code(a, "HAIR_SMP_010");
  REQUIRE(f);
  CHECK(f->status == kStatusSuspected);
  CHECK(f->severity == "alta");
  CHECK(a.status != kStatusClean);  // never "no error detected" for a reported bald NPC
  // With a confirmed cause the open case is closed by that cause.
  s.hair_armors = {wig(0)};
  const auto b = run(s, vanilla_face());
  CHECK(code(b, "HAIR_SMP_007"));
  CHECK_FALSE(code(b, "HAIR_SMP_010"));
}

TEST_CASE("Hair SMP: every new code has a title", "[appearance][npcaudit][smp]") {
  for (const auto* c : {"HAIR_SMP_001", "HAIR_SMP_002", "HAIR_SMP_003", "HAIR_SMP_004", "HAIR_SMP_005", "HAIR_SMP_006", "HAIR_SMP_007",
                        "HAIR_SMP_008", "HAIR_SMP_009", "HAIR_SMP_010"}) {
    CHECK_FALSE(audit_code_title(c).empty());
  }
}

// ---- Reversible wig repair (real shape: Erdi 013271:Skyrim.esm, NMZ Haafingar hair-physics option without KS Wigs HDT)
TEST_CASE("Wig repair: a broken wig over a FaceGen with full hair is taken out of the outfit", "[appearance][npcaudit][smp][repair]") {
  WigRepairCandidate c;
  c.form_key = "013271:Skyrim.esm";
  c.outfit_armors = {"0A1B2C:Skyrim.esm", "000058:NMZ - Haafingar - Women.esp", "0A1B2D:Skyrim.esm"};
  c.broken_wigs = {"000058:NMZ - Haafingar - Women.esp"};
  c.facegen_hair_vertices = 21610;
  const auto d = plan_wig_repair(c);
  CHECK(d.repair);
  CHECK(d.keep_armors == std::vector<std::string>{"0A1B2C:Skyrim.esm", "0A1B2D:Skyrim.esm"});
  CHECK(d.removed == std::vector<std::string>{"000058:NMZ - Haafingar - Women.esp"});
  CHECK(d.reason.find("21610") != std::string::npos);

  SECTION("only a hairline in the FaceGen: removing the wig would not bring the hair back") {
    c.facegen_hair_vertices = 233;
    CHECK_FALSE(plan_wig_repair(c).repair);
  }
  SECTION("leveled lists in the outfit are never dropped") {
    c.outfit_other_entries = 1;
    CHECK_FALSE(plan_wig_repair(c).repair);
  }
  SECTION("nothing left to wear") {
    c.outfit_armors = {"000058:NMZ - Haafingar - Women.esp"};
    CHECK_FALSE(plan_wig_repair(c).repair);
  }
  SECTION("no confirmed broken wig, nothing to do") {
    c.broken_wigs.clear();
    CHECK_FALSE(plan_wig_repair(c).repair);
  }
}
