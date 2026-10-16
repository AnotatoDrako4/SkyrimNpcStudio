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
