#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "sns/core/error.hpp"
#include "sns/creation/skeleton_mapping.hpp"
#include "sns/creation/skyrim_conversion.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace skyrim = sns::creation::skyrim;

namespace {

// goblet.nif + goblet_report.json: a synthetic prop exported by tools/blender/forge_skyrim.py through PyNifly 29.1.0
// (Blender 5.2.3) from make_test_models.py skyrim_prop. No third-party asset.
const fs::path kFixtures = fs::path(SNS_TEST_MODELS_DIR) / "skyrim";

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Json report() { return Json::parse(read(kFixtures / "goblet_report.json")); }

Json find_issue(const Json& result, const std::string& code) {
  for (const auto& issue : result.at("issues"))
    if (issue.at("code") == code) return issue;
  return Json::object();
}

Json find_check(const Json& result, const std::string& code) {
  for (const auto& check : result.at("checks"))
    if (check.at("code") == code) return check;
  FAIL_CHECK("missing check " << code);
  return Json::object();
}

Json minimal_mesh() {
  return {{"name", "Prop"}, {"vertices", 8}, {"triangles", 12}, {"uvLayers", {"UVMap"}}, {"materials", {"M"}},
          {"scale", {1, 1, 1}}, {"rotationEuler", {0, 0, 0}}, {"parent", nullptr}, {"armature", nullptr},
          {"shapeKeys", Json::array()}, {"boundsMin", {0, 0, 0}}, {"boundsMax", {10, 10, 10}}};
}

Json minimal_report() {
  return {{"schema", "sns.forge.skyrim/1"},
          {"pynifly", {{"available", true}, {"tested", true}}},
          {"inspection", {{"meshes", {minimal_mesh()}}, {"materials", Json::array()}, {"images", Json::array()},
                          {"armatures", Json::array()}, {"duplicates", Json::array()}, {"unitScale", 1.0},
                          {"dimensions", {10, 10, 10}}}}};
}

}  // namespace

TEST_CASE("Skyrim conversion classifies the real goblet report", "[forge][skyrim]") {
  const auto result = skyrim::classify(report());
  CHECK(result.at("exportable") == true);
  CHECK(result.at("counts").at("blocking") == 0);
  const auto transforms = find_issue(result, "unappliedTransforms");
  REQUIRE(transforms.contains("severity"));
  CHECK(transforms.at("severity") == "autoFixable");
  CHECK(transforms.at("fixAvailable") == true);
  CHECK(find_issue(result, "textureStaging").value("severity", "") == "autoFixable");
  CHECK(find_issue(result, "scaleCompatible").value("severity", "") == "compatible");
  // Worst first, compatible notes last.
  CHECK(result.at("issues").front().at("severity") != "compatible");
  CHECK(result.at("issues").back().at("severity") == "compatible");
}

TEST_CASE("Skyrim conversion blocks what the exporter cannot do", "[forge][skyrim]") {
  SECTION("PyNifly missing") {
    auto r = minimal_report();
    r["pynifly"] = {{"available", false}, {"error", "PyNifly add-on is not installed"}};
    const auto result = skyrim::classify(r);
    CHECK(result.at("exportable") == false);
    CHECK(find_issue(result, "pyniflyMissing").value("severity", "") == "blocking");
  }
  SECTION("untested PyNifly is only a warning") {
    auto r = minimal_report();
    r["pynifly"]["tested"] = false;
    CHECK(find_issue(skyrim::classify(r), "pyniflyUntested").value("severity", "") == "warning");
  }
  SECTION("no UV") {
    auto r = minimal_report();
    r["inspection"]["meshes"][0]["uvLayers"] = Json::array();
    const auto issue = find_issue(skyrim::classify(r), "missingUv");
    CHECK(issue.value("severity", "") == "blocking");
    CHECK(issue.value("fixAvailable", true) == false);
  }
  SECTION("no mesh") {
    auto r = minimal_report();
    r["inspection"]["meshes"] = Json::array();
    CHECK(skyrim::classify(r).at("exportable") == false);
  }
  SECTION("too many vertices for a BSTriShape") {
    auto r = minimal_report();
    r["inspection"]["meshes"][0]["vertices"] = 70000;
    // A rigid mesh is decimated on the work copy (forge_skyrim.py reduce_polycount)...
    const auto rigid = find_issue(skyrim::classify(r), "shapeTooBig");
    CHECK(rigid.value("severity", "") == "autoFixable");
    CHECK(rigid.value("fixAvailable", false) == true);
    // ...one with morphs (or a rig, or a parent) is not: reducing it would break its vertex order.
    r["inspection"]["meshes"][0]["shapeKeys"] = {"Basis", "Smile"};
    CHECK(find_issue(skyrim::classify(r), "shapeTooBig").value("severity", "") == "blocking");
  }
  SECTION("skinned mesh in the static profile") {
    auto r = minimal_report();
    r["inspection"]["meshes"][0]["armature"] = "Rig";
    r["inspection"]["armatures"] = Json::array({{{"name", "Rig"}, {"bones", 3}}});
    const auto result = skyrim::classify(r);
    CHECK(find_issue(result, "skinnedStatic").value("severity", "") == "blocking");
    CHECK(find_issue(result, "armatureIgnored").value("severity", "") == "manualReview");
  }
  SECTION("armor and npc profiles are refused, not faked") {
    for (const auto* profile : {"armor", "npc"}) {
      const auto result = skyrim::classify(minimal_report(), profile);
      CHECK(result.at("exportable") == false);
      CHECK(find_issue(result, "profileNotImplemented").value("severity", "") == "blocking");
    }
  }
  SECTION("foreign report schema") {
    auto r = minimal_report();
    r["schema"] = "something/else";
    CHECK(skyrim::classify(r).at("exportable") == false);
  }
}

TEST_CASE("Skyrim conversion never auto-applies transforms on morphs or rigs", "[forge][skyrim]") {
  auto r = minimal_report();
  r["inspection"]["meshes"][0]["scale"] = {1, 1, 2};
  CHECK(find_issue(skyrim::classify(r), "unappliedTransforms").value("severity", "") == "autoFixable");
  r["inspection"]["meshes"][0]["shapeKeys"] = {"Basis", "Smile"};
  const auto result = skyrim::classify(r);
  CHECK(find_issue(result, "unappliedTransforms").value("severity", "") == "manualReview");
  CHECK(find_issue(result, "unappliedTransforms").value("fixAvailable", true) == false);
  CHECK(find_issue(result, "shapeKeysDropped").value("severity", "") == "warning");
}

TEST_CASE("Skyrim conversion reports textures, PBR channels and scale", "[forge][skyrim]") {
  auto r = minimal_report();
  r["inspection"]["images"] = Json::array(
      {{{"name", "gone"}, {"filepath", "C:/x/gone.png"}, {"exists", false}, {"packed", false}, {"source", "FILE"}},
       {{"name", "odd"}, {"exists", true}, {"width", 300}, {"height", 512}, {"source", "FILE"}}});
  r["inspection"]["materials"] = Json::array(
      {{{"name", "M"}, {"shader", "ShaderNodeBsdfPrincipled"}, {"unsupportedChannels", {"roughness"}}, {"alpha", true}}});
  r["inspection"]["dimensions"] = {0.4, 0.4, 1.8};  // a model built in meters
  const auto result = skyrim::classify(r);
  CHECK(find_issue(result, "textureMissing").value("severity", "") == "warning");
  CHECK(find_issue(result, "textureNotPowerOfTwo").value("severity", "") == "warning");
  CHECK(find_issue(result, "pbrChannelDropped").value("severity", "") == "warning");
  CHECK(find_issue(result, "alphaReview").value("severity", "") == "manualReview");
  // Meters on a rigid scene: converted on the work copy (forge_skyrim.py meters_to_units, x70.03).
  CHECK(find_issue(result, "suspiciousScale").value("severity", "") == "autoFixable");
  CHECK(result.at("exportable") == true);
  // With an armature the scale is only reported: the rig would have to be scaled too.
  r["inspection"]["armatures"] = Json::array({{{"name", "Rig"}, {"bones", 3}}});
  CHECK(find_issue(skyrim::classify(r), "suspiciousScale").value("severity", "") == "warning");
}

TEST_CASE("Skyrim conversion verifies the real PyNifly NIF with the studio nifly", "[forge][skyrim][nif]") {
  const auto bytes = read(kFixtures / "goblet.nif");
  CHECK(skyrim::nif_stream_version(bytes) == skyrim::kSkyrimSeStream);
  const auto result = skyrim::verify_nif(bytes, report());
  INFO(result.dump(1));
  CHECK(result.at("passed") == true);
  CHECK(result.at("triangles") == 92);
  for (const auto* code : {"formatSkyrimSE", "readable", "geometry", "triangles", "bounds", "texturePathsRelative",
                           "texturesPackaged", "shaderBlocks", "noSkin", "pyniflyAgrees"})
    CHECK(find_check(result, code).value("passed", false) == true);
  const auto& shape = result.at("shapes").at(0);
  CHECK(shape.at("shaderBlock") == "BSLightingShaderProperty");
  CHECK(shape.at("textures").at("0") == "textures\\characterforge\\goblet\\goblet_diffuse.dds");
}

TEST_CASE("Skyrim conversion rejects NIFs that do not match the job", "[forge][skyrim][nif]") {
  const auto bytes = read(kFixtures / "goblet.nif");
  SECTION("corrupt bytes") {
    const auto result = skyrim::verify_nif("not a nif at all, just text", report());
    CHECK(result.at("passed") == false);
    CHECK(find_check(result, "formatSkyrimSE").value("passed", true) == false);
    CHECK_THROWS_AS(skyrim::nif_stream_version("garbage"), sns::Error);
  }
  SECTION("truncated file") {
    const auto result = skyrim::verify_nif(bytes.substr(0, bytes.size() / 2), report());
    CHECK(result.at("passed") == false);
    CHECK(find_check(result, "readable").value("passed", true) == false);
  }
  SECTION("Oldrim stream") {
    auto le = bytes;
    const std::size_t at = std::string_view("Gamebryo File Format, Version 20.2.0.7\n").size() + 13;
    le[at] = static_cast<char>(skyrim::kSkyrimLeStream);
    CHECK(skyrim::nif_stream_version(le) == skyrim::kSkyrimLeStream);
    CHECK(find_check(skyrim::verify_nif(le, report()), "formatSkyrimSE").value("passed", true) == false);
  }
  SECTION("topology differs from Blender") {
    auto r = report();
    r["prepared"]["meshes"][0]["triangles"] = 90;
    const auto result = skyrim::verify_nif(bytes, r);
    CHECK(result.at("passed") == false);
    CHECK(find_check(result, "triangles").value("passed", true) == false);
  }
  SECTION("scale or orientation differs from Blender") {
    auto r = report();
    r["prepared"]["meshes"][0]["boundsMax"][2] = 23.5;
    CHECK(find_check(skyrim::verify_nif(bytes, r), "bounds").value("passed", true) == false);
  }
  SECTION("texture the job did not stage") {
    auto r = report();
    r["prepare"]["textures"]["staged"] = Json::array();
    CHECK(find_check(skyrim::verify_nif(bytes, r), "texturesPackaged").value("passed", true) == false);
  }
  SECTION("PyNifly readback disagrees") {
    auto r = report();
    r["readback"]["shapes"][0]["triangles"] = 1;
    CHECK(find_check(skyrim::verify_nif(bytes, r), "pyniflyAgrees").value("passed", true) == false);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// SkinnedMesh profile (Character Forge 3.0). Synthetic skeleton: invented names and transforms, no game data.
namespace {

skyrim::ReferenceSkeleton synthetic_reference() {
  skyrim::ReferenceSkeleton ref{"synthetic", "00", {}};
  ref.bones.push_back({"Root", "", {0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, 1.0});
  ref.bones.push_back({"Spine [Sp0]", "Root", {0, 0, 50}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, 1.0});
  ref.bones.push_back({"Neck [Nk]", "Spine [Sp0]", {0, 0, 80}, {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}}, 1.0});  // 90 deg about X
  ref.bones.push_back({"Arm", "Spine [Sp0]", {20, 0, 70}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, 1.0});
  return ref;
}

Json bone(const std::string& name, const Json& parent, std::array<double, 3> t, Json rotation = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}) {
  return {{"name", name}, {"parent", parent}, {"deform", true}, {"translation", t}, {"rotation", rotation}, {"scale", 1.0}};
}

Json synthetic_armature() {
  return {{"name", "Rig"}, {"bones", 6}, {"worldIdentity", true}, {"poseDelta", 0.0}, {"bonesTruncated", false},
          {"bonesDetail",
           {bone("Root", nullptr, {0, 0, 0}),
            bone("Spine [Sp0]", "Root", {0, 0, 50.004}),                              // within 0.01 u: verified
            bone("NECK", "Spine [Sp0]", {0, 0, 80}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}),  // only a textual match
            bone("Arm", "Root", {20, 0, 70}),                                         // parent differs: incompatible
            bone("Tail", "Root", {0, -10, 40}),                                       // not in the reference
            bone("Helper", "Root", {0, 0, 1})}}};                                     // not in the reference, no weights
}

Json skinned_mesh() {
  auto m = minimal_mesh();
  m["name"] = "Body";
  m["armature"] = "Rig";
  m["skin"] = {{"armature", "Rig"}, {"weightedBones", {{"Root", 4}, {"Spine [Sp0]", 4}}}, {"unweightedVertices", 0},
               {"verticesOver4Influences", 0}, {"partitionGroups", {"SBP_32_BODY"}}, {"nonBoneGroups", Json::array()},
               {"boundsMin", {0, 0, 0}}, {"boundsMax", {10, 10, 10}}, {"weightSamples", Json::array()}};
  return m;
}

Json skinned_report(bool with_mapping = true) {
  auto r = minimal_report();
  r["status"] = "ok";
  r["profile"] = "skinned";
  r["inspection"]["meshes"] = Json::array({skinned_mesh()});
  r["inspection"]["armatures"] = Json::array({synthetic_armature()});
  if (with_mapping) r["skeletonMapping"] = skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {"Root", "Spine [Sp0]"});
  return r;
}

std::string status_of(const Json& mapping, const std::string& name) {
  for (const auto& b : mapping.at("bones"))
    if (b.at("name") == name) return b.at("status").get<std::string>();
  return "missing";
}

}  // namespace

TEST_CASE("Skeleton mapping classifies bones without guessing", "[forge][skyrim][skinned]") {
  CHECK(skyrim::normalized_bone_name("NPC Spine [Spn0]") == "npc spine");
  CHECK(skyrim::normalized_bone_name("  NPC_L-Thigh.  ") == "npc l thigh");
  CHECK(skyrim::rotation_error_deg({{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}}) > 89.9);

  const auto mapping = skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {"Root", "Spine [Sp0]", "Tail"});
  CHECK(status_of(mapping, "Root") == "verified");
  CHECK(status_of(mapping, "Spine [Sp0]") == "verified");
  CHECK(status_of(mapping, "NECK") == "suggested");      // "neck" == normalized "Neck [Nk]": a human must confirm
  CHECK(status_of(mapping, "Arm") == "incompatible");    // same name, parent Root instead of Spine
  CHECK(status_of(mapping, "Tail") == "unrecognized");
  CHECK(mapping.at("weighted").at("unrecognized") == 1);
  CHECK(mapping.at("allWeightedVerified") == false);    // Tail carries weights

  SECTION("a bind moved beyond the tolerance is incompatible, never verified") {
    auto armature = synthetic_armature();
    armature["bonesDetail"][1]["translation"] = {0, 0, 50.5};
    CHECK(status_of(skyrim::map_skeleton(armature, synthetic_reference(), {"Spine [Sp0]"}), "Spine [Sp0]") == "incompatible");
    armature = synthetic_armature();
    armature["bonesDetail"][1]["rotation"] = {{1, 0, 0}, {0, 0.9998, -0.0175}, {0, 0.0175, 0.9998}};  // ~1 degree
    CHECK(status_of(skyrim::map_skeleton(armature, synthetic_reference(), {"Spine [Sp0]"}), "Spine [Sp0]") == "incompatible");
  }
  SECTION("a weighted bone missing from the armature report is not verified") {
    const auto m = skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {"Root", "Ghost"});
    CHECK(status_of(m, "Ghost") == "unrecognized");
    CHECK(m.at("allWeightedVerified") == false);
  }
  SECTION("only verified weighted bones verify the mapping") {
    CHECK(skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {"Root", "Spine [Sp0]"}).at("allWeightedVerified") == true);
    CHECK(skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {}).at("allWeightedVerified") == false);
  }
}

TEST_CASE("Skinned profile blocks what the skin cannot carry and keeps the static rules", "[forge][skyrim][skinned]") {
  const auto ok = skyrim::classify(skinned_report(), "skinned");
  INFO(ok.dump(1));
  CHECK(ok.at("exportable") == true);
  CHECK(find_issue(ok, "profileNotImplemented").empty());
  CHECK(find_issue(ok, "unusedCustomBones").value("severity", "") == "manualReview");  // Tail + Helper, no weights here

  SECTION("no reference skeleton") {
    CHECK(find_issue(skyrim::classify(skinned_report(false), "skinned"), "noReferenceSkeleton").value("severity", "") == "blocking");
  }
  SECTION("weighted bones that are not verified") {
    auto r = skinned_report(false);
    r["inspection"]["meshes"][0]["skin"]["weightedBones"] = {{"Root", 1}, {"NECK", 1}, {"Arm", 1}, {"Tail", 1}};
    r["skeletonMapping"] = skyrim::map_skeleton(synthetic_armature(), synthetic_reference(), {"Root", "NECK", "Arm", "Tail"});
    const auto result = skyrim::classify(r, "skinned");
    CHECK(find_issue(result, "boneSuggested").value("severity", "") == "blocking");
    CHECK(find_issue(result, "boneIncompatible").value("severity", "") == "blocking");
    CHECK(find_issue(result, "boneUnrecognized").value("severity", "") == "blocking");
    CHECK(result.at("exportable") == false);
  }
  SECTION("skin problems") {
    auto r = skinned_report();
    auto& skin = r["inspection"]["meshes"][0]["skin"];
    skin["unweightedVertices"] = 3;
    skin["verticesOver4Influences"] = 2;
    skin["partitionGroups"] = Json::array();
    r["inspection"]["meshes"][0]["unnormalizedVertices"] = 5;
    r["inspection"]["meshes"][0]["shapeKeys"] = {"Basis", "Breathe"};
    const auto result = skyrim::classify(r, "skinned");
    CHECK(find_issue(result, "unweightedVertices").value("severity", "") == "blocking");
    CHECK(find_issue(result, "influencesTrimmed").value("severity", "") == "warning");
    CHECK(find_issue(result, "weightsNormalized").value("severity", "") == "warning");
    CHECK(find_issue(result, "noPartitions").value("severity", "") == "warning");
    CHECK(find_issue(result, "morphsNotExported").value("severity", "") == "manualReview");
  }
  SECTION("a big skinned mesh is never decimated") {
    auto r = skinned_report();
    r["inspection"]["meshes"][0]["vertices"] = 89184;
    const auto issue = find_issue(skyrim::classify(r, "skinned"), "shapeTooBig");
    CHECK(issue.value("severity", "") == "blocking");
    CHECK(issue.value("fixAvailable", true) == false);
  }
  SECTION("an unbound mesh, two armatures, a moved or posed rig") {
    auto r = skinned_report();
    r["inspection"]["meshes"].push_back(minimal_mesh());
    auto other = skinned_mesh();
    other["name"] = "Cape";
    other["armature"] = "Rig2";
    r["inspection"]["meshes"].push_back(other);
    r["inspection"]["armatures"][0]["worldIdentity"] = false;
    r["inspection"]["armatures"][0]["poseDelta"] = 0.5;
    const auto result = skyrim::classify(r, "skinned");
    CHECK(find_issue(result, "notSkinned").value("severity", "") == "blocking");
    CHECK(find_issue(result, "multipleArmatures").value("severity", "") == "blocking");
    CHECK(find_issue(result, "armatureTransform").value("severity", "") == "manualReview");
    CHECK(find_issue(result, "poseNotRest").value("severity", "") == "warning");
  }
  SECTION("the static profile still refuses a skinned mesh") {
    CHECK(find_issue(skyrim::classify(skinned_report(), "static"), "skinnedStatic").value("severity", "") == "blocking");
  }
}

TEST_CASE("Skinned states never show an incomplete export as validated", "[forge][skyrim][skinned]") {
  const auto current = [](const Json& r, const Json* verification) {
    return skyrim::skinned_states(r, skyrim::classify(r, "skinned"), verification).at("current").get<std::string>();
  };
  auto r = skinned_report();
  CHECK(current(r, nullptr) == "SKIN_VALIDATED");
  CHECK(current(skinned_report(false), nullptr) == "RIG_INSPECTED");
  auto unbound = skinned_report();
  unbound["inspection"]["armatures"] = Json::array();
  CHECK(current(unbound, nullptr) == "PREVIEW_ONLY");

  r["operation"] = "exportNif";
  r["export"] = {{"sha256", "AB"}, {"output", "Data/meshes/x.nif"}};
  r["readback"] = {{"shapes", Json::array()}};
  CHECK(current(r, nullptr) == "NIF_EXPORTED");
  const Json failed{{"passed", false}};
  CHECK(current(r, &failed) == "NIF_EXPORTED");
  const Json passed{{"passed", true}};
  const auto states = skyrim::skinned_states(r, skyrim::classify(r, "skinned"), &passed);
  CHECK(states.at("current") == "PENDIENTE_SKYRIM");
  CHECK(states.at("states").size() == 7);
  // A later state is never reached when an earlier one is not, whatever the later facts say.
  r["skeletonMapping"]["allWeightedVerified"] = false;
  const auto broken = skyrim::skinned_states(r, skyrim::classify(r, "skinned"), &passed);
  CHECK(broken.at("current") == "RIG_INSPECTED");
  for (const auto& s : broken.at("states"))
    if (s.at("state") == "NIF_VERIFIED" || s.at("state") == "PENDIENTE_SKYRIM") CHECK(s.at("reached") == false);
}
