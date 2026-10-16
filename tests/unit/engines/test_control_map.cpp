#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>

#include "sns/appearance/control_map.hpp"

using namespace sns::appearance;

namespace {

OverrideLink link(std::string plugin, std::string mod, std::vector<std::string> changed) {
  OverrideLink l;
  l.plugin = std::move(plugin);
  l.mod = std::move(mod);
  l.changed_fields = std::move(changed);
  return l;
}

// Origin + overrides; the first is the origin and the last the winner.
std::vector<OverrideLink> chain(std::vector<OverrideLink> links) {
  if (!links.empty()) {
    links.front().is_origin = true;
    links.back().is_winner = true;
  }
  return links;
}

AssetSource loose(std::string winner, std::vector<std::string> mods) {
  AssetSource a;
  a.path = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif";
  a.found = true;
  a.winner_mod = std::move(winner);
  a.kind = "loose";
  a.loose_mods = std::move(mods);
  return a;
}

AssetSource bsa(std::string mod) {
  AssetSource a;
  a.path = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif";
  a.found = true;
  a.winner_mod = std::move(mod);
  a.kind = "bsa";
  return a;
}

const ComponentControl* component(const ControlReport& r, const std::string& key) {
  const auto it = std::find_if(r.components.begin(), r.components.end(), [&](const ComponentControl& c) { return c.key == key; });
  return it == r.components.end() ? nullptr : &*it;
}

const ControlFinding* finding(const ControlReport& r, const std::string& code) {
  const auto it = std::find_if(r.findings.begin(), r.findings.end(), [&](const ControlFinding& f) { return f.code == code; });
  return it == r.findings.end() ? nullptr : &*it;
}

bool has_warning(const ControlReport& r) {
  return std::any_of(r.findings.begin(), r.findings.end(), [](const ControlFinding& f) { return f.severity == "warning"; });
}

}  // namespace

TEST_CASE("Control map: vanilla NPC with its FaceGen in the game BSA is consistent", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {})});
  in.facegen = bsa("<Game Data>");
  in.facetint = bsa("<Game Data>");
  in.regeneration_deviation = 0.001f;
  const auto r = analyze_appearance_control(in);

  CHECK(r.face_plugin == "Skyrim.esm");
  CHECK(r.face_mod == "<Game Data>");
  REQUIRE(component(r, "face-shape"));
  CHECK(component(r, "face-shape")->plugin == "Skyrim.esm");
  CHECK(component(r, "face-shape")->overridden.empty());
  REQUIRE(component(r, "facegen"));
  CHECK(component(r, "facegen")->kind == "bsa");
  CHECK_FALSE(has_warning(r));
  REQUIRE(finding(r, "FaceSourcesConsistent"));
  CHECK(finding(r, "FaceSourcesConsistent")->severity == "ok");
}

TEST_CASE("Control map: each part is controlled by the last version that changed it, not by the winner", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}),
                    link("Pretty Lydia.esp", "Pretty Lydia", {"FaceMorph", "HeadParts", "TintLayers", "HairColor"}),
                    link("Outfits.esp", "Better Outfits", {"DefaultOutfit"})});
  in.facegen = loose("Pretty Lydia", {"Pretty Lydia"});
  in.facetint = loose("Pretty Lydia", {"Pretty Lydia"});
  const auto r = analyze_appearance_control(in);

  CHECK(r.face_plugin == "Pretty Lydia.esp");
  CHECK(r.face_mod == "Pretty Lydia");
  CHECK(component(r, "face-shape")->plugin == "Pretty Lydia.esp");
  CHECK(component(r, "head-parts")->mod == "Pretty Lydia");
  CHECK(component(r, "hair-color")->plugin == "Pretty Lydia.esp");
  CHECK(component(r, "outfit")->plugin == "Outfits.esp");
  // Unchanged parts stay with the origin even though Outfits.esp wins the record.
  CHECK(component(r, "race")->plugin == "Skyrim.esm");
  CHECK(component(r, "body")->plugin == "Skyrim.esm");
  // What the controller overrides: the origin always, intermediate versions only if they changed the part.
  REQUIRE(component(r, "face-shape")->overridden.size() == 1);
  CHECK(component(r, "face-shape")->overridden[0] == "Skyrim.esm (<Game Data>)");
  REQUIRE(component(r, "outfit")->overridden.size() == 1);
  CHECK(component(r, "outfit")->overridden[0] == "Skyrim.esm (<Game Data>)");
  CHECK_FALSE(has_warning(r));
  CHECK(finding(r, "FaceSourcesConsistent"));
}

TEST_CASE("Control map: a later plugin re-setting the face takes control and the earlier one is listed as overridden", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("A.esp", "Mod A", {"FaceMorph"}), link("B.esp", "Mod B", {"FaceParts"})});
  in.facegen = loose("Mod B", {"Mod A", "Mod B"});
  in.facetint = loose("Mod B", {"Mod A", "Mod B"});
  const auto r = analyze_appearance_control(in);

  CHECK(component(r, "face-shape")->plugin == "B.esp");
  REQUIRE(component(r, "face-shape")->overridden.size() == 2);
  CHECK(component(r, "face-shape")->overridden[1] == "A.esp (Mod A)");
  CHECK(component(r, "facegen")->overridden == std::vector<std::string>{"Mod A"});
  REQUIRE(finding(r, "ShadowedFaceGen"));
  CHECK(finding(r, "ShadowedFaceGen")->severity == "info");
}

TEST_CASE("Control map: record face from one mod and FaceGen from another is the dark-face mismatch", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face Overhaul.esp", "Face Overhaul", {"FaceMorph", "HeadParts"})});
  in.facegen = loose("Other Replacer", {"Other Replacer"});
  in.facetint = loose("Other Replacer", {"Other Replacer"});
  in.regeneration_deviation = 0.84f;
  const auto r = analyze_appearance_control(in);

  const auto* f = finding(r, "FaceGenRecordMismatch");
  REQUIRE(f);
  CHECK(f->severity == "warning");
  CHECK(f->message.find("Face Overhaul.esp") != std::string::npos);
  CHECK(f->message.find("Other Replacer") != std::string::npos);
  CHECK(f->message.find("0.840") != std::string::npos);
  // Neither the FaceGen mod has a plugin in the chain nor the face mod a loose FaceGen: regenerate.
  CHECK(f->suggestion.find("regenerarlo") != std::string::npos);
  CHECK_FALSE(finding(r, "FaceSourcesConsistent"));
}

TEST_CASE("Control map: mismatch suggestions name the concrete fix", "[appearance][controlmap]") {
  SECTION("the FaceGen mod has its own plugin overridden by the face plugin: load it after") {
    ControlInputs in;
    in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Replacer.esp", "Replacer", {"FaceMorph"}),
                      link("AIO.esp", "AIO Overhaul", {"HeadParts"})});
    in.facegen = loose("Replacer", {"Replacer"});
    in.facetint = loose("Replacer", {"Replacer"});
    const auto r = analyze_appearance_control(in);
    const auto* f = finding(r, "FaceGenRecordMismatch");
    REQUIRE(f);
    CHECK(f->suggestion.find("«Replacer.esp»") != std::string::npos);
    CHECK(f->suggestion.find("cárgalo después") != std::string::npos);
  }
  SECTION("the face mod ships a loose FaceGen that loses in MO2: raise it") {
    ControlInputs in;
    in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph"})});
    in.facegen = loose("Texture Pack", {"Face Mod", "Texture Pack"});
    in.facetint = loose("Texture Pack", {"Face Mod", "Texture Pack"});
    const auto r = analyze_appearance_control(in);
    const auto* f = finding(r, "FaceGenRecordMismatch");
    REQUIRE(f);
    CHECK(f->suggestion.find("coloca «Face Mod» por encima") != std::string::npos);
    CHECK(finding(r, "ShadowedFaceGen"));
  }
}

TEST_CASE("Control map: a translated copy of the plugin keeps the original mod's FaceGen as a note", "[appearance][controlmap]") {
  // Shape of Shady Sam on the test profile: the translation replaces the .esp file, the BSA stays in the original mod.
  ControlInputs in;
  auto translated = link("alternate start - live another life.esp", "Alternate Start - Castellano", {});
  translated.plugin_mods = {"Alternate Start - SSE", "Alternate Start - Castellano"};
  in.chain = chain({translated});
  in.facegen = bsa("Alternate Start - SSE");
  in.facetint = bsa("Alternate Start - SSE");
  in.regeneration_deviation = 0.834f;
  const auto r = analyze_appearance_control(in);
  CHECK_FALSE(finding(r, "FaceGenRecordMismatch"));
  REQUIRE(finding(r, "FaceGenFromReplacedPluginCopy"));
  CHECK(finding(r, "FaceGenFromReplacedPluginCopy")->severity == "info");
  CHECK_FALSE(has_warning(r));

  // Without the original copy of the plugin in the FaceGen mod it stays a mismatch.
  in.chain[0].plugin_mods = {"Alternate Start - Castellano"};
  CHECK(finding(analyze_appearance_control(in), "FaceGenRecordMismatch"));
}

TEST_CASE("Control map: a head-parts-only change whose pieces are all in the FaceGen is a note", "[appearance][controlmap]") {
  // Shape of Aeri on the test profile: USSEP (translated) changes HeadParts only; FaceGen from the game BSA.
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("unofficial skyrim special edition patch.esp", "USSEP Spanish", {"HeadParts"})});
  in.facegen = bsa("<Game Data>");
  in.facetint = bsa("<Game Data>");
  in.facegen_has_record_head_parts = true;
  const auto r = analyze_appearance_control(in);
  CHECK_FALSE(finding(r, "FaceGenRecordMismatch"));
  REQUIRE(finding(r, "FaceGenHasRecordHeadParts"));
  CHECK_FALSE(has_warning(r));

  SECTION("a piece missing from the FaceGen keeps the warning") {
    in.facegen_has_record_head_parts = false;
    CHECK(finding(analyze_appearance_control(in), "FaceGenRecordMismatch"));
  }
  SECTION("unchecked keeps the warning") {
    in.facegen_has_record_head_parts.reset();
    CHECK(finding(analyze_appearance_control(in), "FaceGenRecordMismatch"));
  }
  SECTION("morphs from the other mod keep the warning even with every piece present") {
    in.chain[1].changed_fields = {"HeadParts", "FaceMorph"};
    CHECK(finding(analyze_appearance_control(in), "FaceGenRecordMismatch"));
  }
}

TEST_CASE("Control map: different mods whose FaceGen matches the record by measurement are fine", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("USSEP.esp", "USSEP", {"FaceParts"})});
  in.facegen = bsa("<Game Data>");
  in.facetint = bsa("<Game Data>");
  in.regeneration_deviation = 0.01f;
  const auto r = analyze_appearance_control(in);

  CHECK_FALSE(finding(r, "FaceGenRecordMismatch"));
  REQUIRE(finding(r, "FaceGenFromOtherModMatches"));
  CHECK(finding(r, "FaceGenFromOtherModMatches")->severity == "ok");
  CHECK_FALSE(has_warning(r));
}

TEST_CASE("Control map: a deviation at or above the threshold is not treated as a match", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("USSEP.esp", "USSEP", {"FaceParts"})});
  in.facegen = bsa("<Game Data>");
  in.facetint = bsa("<Game Data>");
  in.regeneration_deviation = kFaceGenMatchesRecord;
  CHECK(finding(analyze_appearance_control(in), "FaceGenRecordMismatch"));
}

TEST_CASE("Control map: missing FaceGen and missing FaceTint", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("NewNpc.esp", "My NPC", {})});
  const auto r = analyze_appearance_control(in);
  REQUIRE(finding(r, "NoFaceGen"));
  CHECK(finding(r, "NoFaceGen")->severity == "warning");
  CHECK(component(r, "facegen")->kind == "missing");
  CHECK(component(r, "facegen")->mod.empty());
  CHECK_FALSE(finding(r, "NoFaceTint"));  // reported only when there is geometry

  // Template traits or a creature race: no FaceGen of its own is expected.
  in.facegen_expected = false;
  const auto templated = analyze_appearance_control(in);
  CHECK_FALSE(finding(templated, "NoFaceGen"));
  REQUIRE(finding(templated, "FaceGenNotUsed"));
  CHECK_FALSE(has_warning(templated));
  in.facegen_expected = true;

  in.facegen = loose("My NPC", {"My NPC"});
  const auto r2 = analyze_appearance_control(in);
  REQUIRE(finding(r2, "NoFaceTint"));
  CHECK(finding(r2, "NoFaceTint")->severity == "info");
  CHECK(finding(r2, "FaceSourcesConsistent"));  // info does not block consistency
}

TEST_CASE("Control map: FaceTint from a different mod than the geometry", "[appearance][controlmap]") {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph"})});
  in.facegen = loose("Face Mod", {"Face Mod"});
  in.facetint = loose("Skin Pack", {"Face Mod", "Skin Pack"});
  const auto r = analyze_appearance_control(in);
  REQUIRE(finding(r, "FaceTintFromOtherMod"));
  CHECK(finding(r, "FaceTintFromOtherMod")->suggestion.find("«Face Mod»") != std::string::npos);
  CHECK(component(r, "facetint")->overridden == std::vector<std::string>{"Face Mod"});
  CHECK_FALSE(finding(r, "FaceSourcesConsistent"));
}

TEST_CASE("Control map: lost facial edits are reported, other lost fields are not", "[appearance][controlmap]") {
  // A later patch made from the vanilla record reverts the hair color and tints of a face mod.
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph", "HairColor", "TintLayers"}),
                    link("Patch.esp", "Old Patch", {"HairColor", "TintLayers", "DefaultOutfit"})});
  in.facegen = loose("Face Mod", {"Face Mod"});
  in.facetint = loose("Face Mod", {"Face Mod"});
  in.lost_fields = {"DefaultOutfit", "HairColor", "TintLayers"};
  const auto r = analyze_appearance_control(in);
  const auto* f = finding(r, "LostFaceEdits");
  REQUIRE(f);
  CHECK(f->severity == "warning");
  CHECK(f->message.find("HairColor, TintLayers") != std::string::npos);
  CHECK(f->message.find("DefaultOutfit") == std::string::npos);
  CHECK_FALSE(finding(r, "FaceReplaced"));
  CHECK_FALSE(finding(r, "FaceSourcesConsistent"));

  in.lost_fields = {"DefaultOutfit"};
  CHECK_FALSE(finding(analyze_appearance_control(in), "LostFaceEdits"));
}

TEST_CASE("Control map: a complete face replacer replacing an earlier face mod is not a lost edit", "[appearance][controlmap]") {
  // Shape of Lydia on the test profile: NITHI sets a face, Helen's replacer (record + loose FaceGen) wins it.
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("NITHI.esp", "NITHI", {"FaceMorph", "HeadParts", "HairColor"}),
                    link("Helen_Lydia_replacer.esp", "Farodadestin", {"FaceMorph", "HeadParts", "HairColor"})});
  in.facegen = loose("Farodadestin", {"NITHI", "Farodadestin"});
  in.facetint = loose("Farodadestin", {"NITHI", "Farodadestin"});
  in.regeneration_deviation = 1.129f;  // sculpted beyond the record
  in.lost_fields = {"FaceMorph", "HeadParts", "HairColor"};
  const auto r = analyze_appearance_control(in);
  CHECK_FALSE(finding(r, "LostFaceEdits"));
  REQUIRE(finding(r, "FaceReplaced"));
  CHECK(finding(r, "FaceReplaced")->severity == "info");
  CHECK(finding(r, "ShadowedFaceGen"));
  REQUIRE(finding(r, "FaceSourcesConsistent"));
  CHECK(finding(r, "FaceSourcesConsistent")->consequence.find("esculpido") != std::string::npos);
  CHECK_FALSE(has_warning(r));
}

TEST_CASE("Control map: hundreds of versions and loose copies stay cheap", "[appearance][controlmap]") {
  // A heavily modded NPC: 400 record versions alternating face and outfit edits, 300 mods with a loose FaceGen.
  ControlInputs in;
  std::vector<OverrideLink> links{link("Skyrim.esm", "<Game Data>", {})};
  std::vector<std::string> loose_mods;
  for (int i = 1; i <= 400; ++i) {
    links.push_back(link("Mod" + std::to_string(i) + ".esp", "Mod " + std::to_string(i), {i % 2 ? "FaceMorph" : "DefaultOutfit"}));
  }
  for (int i = 1; i <= 300; ++i) loose_mods.push_back("Mod " + std::to_string(i));
  in.chain = chain(std::move(links));
  in.facegen = loose("Mod 300", loose_mods);
  in.facetint = loose("Mod 300", loose_mods);
  const auto start = std::chrono::steady_clock::now();
  ControlReport r;
  for (int run = 0; run < 20; ++run) r = analyze_appearance_control(in);
  const auto per_run_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() / 20;
  CHECK(component(r, "face-shape")->plugin == "Mod399.esp");
  CHECK(component(r, "outfit")->plugin == "Mod400.esp");
  CHECK(component(r, "face-shape")->overridden.size() == 200);  // origin + the 199 earlier face edits
  CHECK(component(r, "facegen")->overridden.size() == 299);
  CHECK(finding(r, "FaceGenRecordMismatch"));  // face from Mod 399, FaceGen from Mod 300
  CHECK(per_run_us < 200000);  // generous bound for Debug/ASan builds; the measured value is logged
  WARN("control map with 400 versions and 300 loose copies: " << per_run_us << " us per analysis");
}

TEST_CASE("Control map: an empty chain still describes the face files", "[appearance][controlmap]") {
  ControlInputs in;
  in.facegen = bsa("<Game Data>");
  const auto r = analyze_appearance_control(in);
  CHECK(r.face_plugin.empty());
  CHECK_FALSE(component(r, "face-shape"));
  REQUIRE(component(r, "facegen"));
  CHECK_FALSE(finding(r, "FaceGenRecordMismatch"));
  CHECK_FALSE(finding(r, "FaceSourcesConsistent"));
}

// ---------------------------------------------------------------- copies and assisted resolution

namespace {

OverrideLink copy_link(std::string plugin, std::string mod, std::vector<std::string> changed, const std::string& from) {
  auto l = link(std::move(plugin), std::move(mod), changed);
  for (const auto& f : changed) l.copied_from[f] = from;
  return l;
}

// Shape of the 28 Riften NPCs on the test profile: NITHI sets the face and ships the loose FaceGen,
// AI Overhaul (later in load order, made from another copy of the record) sets the face fields again.
ControlInputs aio_over_nithi() {
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}),
                    link("NITHI NPC Enhancement - Riften.esp", "NITHI NPC Enhancement - Riften",
                         {"HeadParts", "FaceMorph", "FaceParts", "TintLayers", "TextureLighting", "HairColor"}),
                    link("AI Overhaul.esp", "AI Overhaul 1.8", {"FaceMorph", "FaceParts", "TintLayers", "TextureLighting", "Spells"})});
  in.facegen = loose("NITHI NPC Enhancement - Riften", {"NITHI NPC Enhancement - Riften"});
  in.facetint = loose("NITHI NPC Enhancement - Riften", {"NITHI NPC Enhancement - Riften"});
  in.lost_fields = {"FaceMorph", "FaceParts", "TintLayers", "TextureLighting"};
  return in;
}

}  // namespace

TEST_CASE("Control map: a copied value belongs to the version that introduced it", "[appearance][controlmap]") {
  // A patch forwards the vanilla face over a mod that changed it: the face is vanilla's, carried by the patch.
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph", "HeadParts"}),
                    copy_link("Forward.esp", "Forward Patch", {"FaceMorph", "HeadParts"}, "Skyrim.esm")});
  in.facegen = bsa("<Game Data>");
  in.facetint = bsa("<Game Data>");
  const auto r = analyze_appearance_control(in);
  REQUIRE(component(r, "face-shape"));
  CHECK(component(r, "face-shape")->plugin == "Skyrim.esm");
  CHECK(component(r, "face-shape")->via == "Forward.esp");
  CHECK(component(r, "face-shape")->overridden == std::vector<std::string>{"Face.esp (Face Mod)"});
  CHECK(r.face_plugin == "Skyrim.esm");
  CHECK(r.face_via == "Forward.esp");
  CHECK(field_controller(in.chain, "HeadParts") == 0);
  CHECK(finding(r, "FaceSourcesConsistent"));
  CHECK_FALSE(has_warning(r));

  // A copy that names a plugin that is not earlier in the chain stays with the version that wrote it.
  in.chain.back().copied_from["FaceMorph"] = "Missing.esp";
  CHECK(field_controller(in.chain, "FaceMorph") == 2);
  // Without copies nothing changes: the last version that changed the field controls it.
  in.chain.back().copied_from.clear();
  const auto plain = analyze_appearance_control(in);
  CHECK(component(plain, "face-shape")->plugin == "Forward.esp");
  CHECK(component(plain, "face-shape")->via.empty());
  CHECK(plain.face_via.empty());
}

TEST_CASE("Control map: AI Overhaul over NITHI offers to use NITHI's face", "[appearance][controlmap][resolution]") {
  const auto in = aio_over_nithi();
  const auto r = analyze_appearance_control(in);
  const auto* f = finding(r, "FaceGenRecordMismatch");
  REQUIRE(f);
  REQUIRE(r.resolution);
  const auto& res = *r.resolution;
  CHECK(res.source_plugin == "NITHI NPC Enhancement - Riften.esp");
  CHECK(res.source_mod == "NITHI NPC Enhancement - Riften");
  CHECK(res.winner_plugin == "AI Overhaul.esp");
  CHECK(res.winner_mod == "AI Overhaul 1.8");
  // Only the face fields whose value differs from NITHI's: HeadParts and HairColor already are NITHI's.
  CHECK(res.fields == std::vector<std::string>{"FaceMorph", "FaceParts", "TintLayers", "TextureLighting"});
  CHECK(res.load_after == std::vector<std::string>{"NITHI NPC Enhancement - Riften.esp", "AI Overhaul.esp"});
  CHECK(res.label == "Usar el rostro de «NITHI NPC Enhancement - Riften.esp»");
  CHECK(res.detail.find("No copia ni regenera FaceGen") != std::string::npos);
  CHECK(f->suggestion.find(res.label) != std::string::npos);

  SECTION("Weight is copied only when it differs") {
    auto heavier = in;
    heavier.chain.back().changed_fields.push_back("Weight");
    const auto w = analyze_appearance_control(heavier);
    REQUIRE(w.resolution);
    CHECK(w.resolution->fields.back() == "Weight");
  }
  SECTION("an outfit patch after AI Overhaul is the record the resolution overrides") {
    auto outfit = in;
    outfit.chain.back().is_winner = false;
    outfit.chain.push_back(link("Outfits.esp", "Outfit Patch", {"DefaultOutfit"}));
    outfit.chain.back().is_winner = true;
    const auto o = analyze_appearance_control(outfit);
    REQUIRE(o.resolution);
    CHECK(o.resolution->winner_plugin == "Outfits.esp");
    CHECK(o.resolution->load_after == std::vector<std::string>{"NITHI NPC Enhancement - Riften.esp", "Outfits.esp"});
    CHECK(o.resolution->fields.size() == 4);
  }
}

TEST_CASE("Control map: after the resolution patch the record and the FaceGen are consistent", "[appearance][controlmap][resolution]") {
  // What the control map sees once the patch loads last: a version that copies NITHI's face fields.
  auto in = aio_over_nithi();
  const auto before = analyze_appearance_control(in);
  REQUIRE(before.resolution);
  in.chain.back().is_winner = false;
  in.chain.push_back(copy_link("SNS_FaceFix.esp", "SNS_FaceFix", before.resolution->fields, before.resolution->source_plugin));
  in.chain.back().is_winner = true;
  const auto after = analyze_appearance_control(in);

  CHECK_FALSE(has_warning(after));
  CHECK_FALSE(finding(after, "FaceGenRecordMismatch"));
  CHECK_FALSE(after.resolution);
  REQUIRE(finding(after, "FaceSourcesConsistent"));
  CHECK(after.face_plugin == "NITHI NPC Enhancement - Riften.esp");
  CHECK(after.face_mod == "NITHI NPC Enhancement - Riften");
  CHECK(after.face_via == "SNS_FaceFix.esp");
  REQUIRE(component(after, "face-shape"));
  CHECK(component(after, "face-shape")->plugin == "NITHI NPC Enhancement - Riften.esp");
  CHECK(component(after, "face-shape")->via == "SNS_FaceFix.esp");
  // AI Overhaul's face edits no longer show: replaced by the mod whose FaceGen the game uses.
  const auto& lost = component(after, "face-shape")->overridden;
  CHECK(std::find(lost.begin(), lost.end(), "AI Overhaul.esp (AI Overhaul 1.8)") != lost.end());
  REQUIRE(finding(after, "FaceReplaced"));
  CHECK(finding(after, "FaceReplaced")->severity == "info");
  CHECK_FALSE(finding(after, "LostFaceEdits"));
  // Parts the patch did not copy keep their controller.
  CHECK(component(after, "head-parts")->plugin == "NITHI NPC Enhancement - Riften.esp");
  CHECK(component(after, "head-parts")->via.empty());
}

TEST_CASE("Control map: no resolution when copying a face cannot make record and FaceGen agree", "[appearance][controlmap][resolution]") {
  SECTION("the FaceGen's mod has no version of the record") {
    ControlInputs in;
    in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph"})});
    in.facegen = loose("Other Replacer", {"Other Replacer"});
    const auto r = analyze_appearance_control(in);
    CHECK(finding(r, "FaceGenRecordMismatch"));
    CHECK_FALSE(r.resolution);
  }
  SECTION("the winner changed race or sex: never copied") {
    auto in = aio_over_nithi();
    in.chain.back().changed_fields.push_back("Race");
    CHECK_FALSE(analyze_appearance_control(in).resolution);
    in = aio_over_nithi();
    in.chain.back().changed_fields.push_back("Sex");
    CHECK_FALSE(analyze_appearance_control(in).resolution);
  }
  SECTION("the FaceGen's mod is the winner") {
    // Its record keeps the face of the earlier version: nothing to copy onto it.
    ControlInputs in;
    in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph"}),
                      link("Replacer.esp", "Replacer", {"DefaultOutfit"})});
    in.facegen = loose("Replacer", {"Replacer"});
    const auto r = analyze_appearance_control(in);
    CHECK(finding(r, "FaceGenRecordMismatch"));
    CHECK_FALSE(r.resolution);
  }
  SECTION("not a mismatch") {
    ControlInputs in;
    in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("Face.esp", "Face Mod", {"FaceMorph"})});
    in.facegen = loose("Face Mod", {"Face Mod"});
    CHECK_FALSE(analyze_appearance_control(in).resolution);
    auto measured = aio_over_nithi();
    measured.regeneration_deviation = 0.01f;  // the FaceGen is what the record produces: nothing to fix
    const auto m = analyze_appearance_control(measured);
    CHECK(finding(m, "FaceGenFromOtherModMatches"));
    CHECK_FALSE(m.resolution);
  }
}

TEST_CASE("Control map: a replacer that writes a vanilla value keeps it as its own", "[appearance][controlmap]") {
  // Found on the full test profile: a replacer with its own FaceGen whose record keeps vanilla morphs
  // after a face mod changed them. Its FaceGen was built from those values: a full replacement, not a copy.
  ControlInputs in;
  in.chain = chain({link("Skyrim.esm", "<Game Data>", {}), link("NITHI.esp", "NITHI", {"FaceMorph", "HeadParts"}),
                    copy_link("Replacer.esp", "Replacer", {"FaceMorph"}, "Skyrim.esm")});
  in.chain.back().changed_fields.push_back("HeadParts");
  in.facegen = loose("Replacer", {"NITHI", "Replacer"});
  in.facetint = loose("Replacer", {"NITHI", "Replacer"});
  in.lost_fields = {"FaceMorph", "HeadParts"};
  const auto r = analyze_appearance_control(in);
  CHECK(r.face_plugin == "Replacer.esp");
  CHECK(r.face_via.empty());
  CHECK(component(r, "face-shape")->plugin == "Replacer.esp");
  CHECK(field_controller(in.chain, "FaceMorph", "Replacer") == 2);
  CHECK(field_controller(in.chain, "FaceMorph") == 0);
  CHECK(finding(r, "FaceReplaced"));
  CHECK_FALSE(finding(r, "LostFaceEdits"));
  CHECK(finding(r, "FaceSourcesConsistent"));
  CHECK_FALSE(has_warning(r));
}
