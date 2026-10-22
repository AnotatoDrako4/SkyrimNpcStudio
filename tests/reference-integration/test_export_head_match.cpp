#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "sns/presets/export_head_match.hpp"

using namespace sns::presets;

namespace {

// A preset like the HPH ones: a Face part, eyes, brows and a hair.
PresetHeadFacts facts(std::string name = "1 Okiri V3") {
  PresetHeadFacts f;
  f.preset_name = std::move(name);
  f.shape_names = {"KLHeadF", "KLEyesF", "KLBrowsF", "HairLongA"};
  f.face_part = "KLHeadF";
  f.face_vertices = 3832;
  return f;
}

HeadCandidate head(std::string stem, std::vector<std::string> shapes, std::int64_t modified = 1000, bool readable = true) {
  HeadCandidate c;
  c.path = "SKSE/Plugins/CharGen/" + stem + ".nif";
  c.stem = stem;
  c.readable = readable;
  for (auto& s : shapes) {
    const std::uint32_t vertices = (s == "KLHeadF" || s == "klheadf") ? 3832u : 700u;  // the face part's real mesh size
    c.shapes.push_back({std::move(s), vertices});
  }
  c.modified = modified;
  return c;
}

const std::vector<std::string> kFull = {"KLHeadF", "KLEyesF", "KLBrowsF", "HairLongA"};

bool has(const HeadMatch& m, const std::string& code) { return std::find(m.codes.begin(), m.codes.end(), code) != m.codes.end(); }

}  // namespace

TEST_CASE("Export head: an exact name and the same parts is READY_FOR_REVIEW, never 'verified in Skyrim'", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  REQUIRE(m.chosen);
  CHECK(*m.chosen == 0);
  CHECK(m.ranked.front().exact_name);
  CHECK(std::find(m.caveats.begin(), m.caveats.end(), "UNVERIFIED_IN_SKYRIM") != m.caveats.end());
  CHECK(to_string(m.verdict) == "READY_FOR_REVIEW");
}

// Found with the real profile (Domitia Atrotia): the Export Head carries the whole hairstyle, 100+ shapes the preset knows nothing
// about. A symmetric (Jaccard) overlap sank the pairing to "no head"; the preset's own parts are what must be found.
TEST_CASE("Export head: a head that also carries a 110-shape hairstyle still pairs with its preset", "[exporthead]") {
  auto shapes = kFull;
  for (int i = 0; i < 110; ++i) shapes.push_back("BDOr_H_PEW02_Base_" + std::to_string(i));
  const auto m = match_export_head(facts(), {head("1 Okiri V3", shapes)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  REQUIRE(m.chosen);
  CHECK(m.ranked.front().overlap == 1.0);
  CHECK(m.ranked.front().common == 4);
  // Even without the name hint a complete cover pairs, because no part of the preset is missing.
  CHECK_FALSE(has(m, "PairedByContentOnly"));
  const auto unnamed = match_export_head(facts("Serphina"), {head("1 Okiri V3", shapes)});
  CHECK(unnamed.verdict == HeadVerdict::ReadyForReview);
  CHECK(has(unnamed, "PairedByContentOnly"));  // honest: the name did not help, a person should confirm
}

TEST_CASE("Export head: no export head in the profile is NEEDS_ASSET", "[exporthead]") {
  const auto m = match_export_head(facts(), {});
  CHECK(m.verdict == HeadVerdict::NeedsAsset);
  CHECK(has(m, "NoExportHead"));
  CHECK_FALSE(m.chosen);
}

TEST_CASE("Export head: an unrelated name with unrelated parts matches nothing", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("Serphina", {"OtherHead", "OtherEyes", "OtherBrows"})});
  CHECK(m.verdict == HeadVerdict::NeedsAsset);
  CHECK(has(m, "NoExportHead"));
  CHECK(m.ranked.empty());
}

TEST_CASE("Export head: a close name ('Cara Mason HPH f' for '...final') is a hint, content still has to fit", "[exporthead]") {
  const auto f = facts("Cara Mason HPH final");
  const auto ok = match_export_head(f, {head("Cara Mason HPH f", kFull)});
  CHECK(ok.verdict == HeadVerdict::ReadyForReview);
  CHECK(ok.ranked.front().near_name);
  // The same name with parts that do not fit (overlap 1/7 < 0.5) is not taken.
  const auto bad = match_export_head(f, {head("Cara Mason HPH f", {"KLHeadF", "X1", "X2", "X3", "X4", "X5", "X6"})});
  CHECK(bad.verdict == HeadVerdict::NeedsAsset);
}

TEST_CASE("Export head: piled-up extensions (Okiri.nif.nif.nif) are one name", "[exporthead]") {
  CHECK(normalize_head_stem("1 Okiri.nif.nif.nif") == "1 Okiri");
  CHECK(normalize_head_stem("C:\\Mods\\x\\Head.NIF") == "Head");
  CHECK(normalize_head_stem(".nif") == ".nif");  // nothing left to strip
  const auto m = match_export_head(facts("1 Okiri"), {head("1 Okiri.nif.nif.nif", kFull)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(m.ranked.front().exact_name);
}

TEST_CASE("Export head: the head that carries the preset's name but cannot be opened is FACEGEN_UNREADABLE", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("1 Okiri V3", {}, 1000, /*readable=*/false)});
  CHECK(m.verdict == HeadVerdict::FaceGenUnreadable);
  CHECK(has(m, "HeadUnreadable"));
  REQUIRE(m.chosen);
}

TEST_CASE("Export head: a damaged file of another name is ignored, not blamed on this preset", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("Somebody Else", {}, 1000, false), head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(*m.chosen == 1);
  const auto none = match_export_head(facts(), {head("Somebody Else", {}, 1000, false)});
  CHECK(none.verdict == HeadVerdict::NeedsAsset);
}

TEST_CASE("Export head: a head of the right name without the preset's face part is HEAD_PART_MISMATCH", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("1 Okiri V3", {"VanillaHead", "KLEyesF", "KLBrowsF"})});
  CHECK(m.verdict == HeadVerdict::HeadPartMismatch);
  CHECK(has(m, "FaceShapeMissing"));
}

TEST_CASE("Export head: a race the head parts do not fit is HEAD_PART_MISMATCH before any search", "[exporthead]") {
  auto f = facts();
  f.race_ok = false;
  const auto m = match_export_head(f, {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::HeadPartMismatch);
  CHECK(has(m, "RaceIncompatible"));
  CHECK_FALSE(m.chosen);
}

TEST_CASE("Export head: a preset whose head parts could not be resolved is HEAD_PART_MISMATCH", "[exporthead]") {
  PresetHeadFacts f;
  f.preset_name = "1 Okiri V3";
  f.unresolved = {"05150F:Skyrim.esm"};
  const auto m = match_export_head(f, {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::HeadPartMismatch);
  CHECK(has(m, "NoHeadPartsResolved"));
  CHECK(has(m, "HeadPartsUnresolved"));
}

TEST_CASE("Export head: some unresolved parts are reported but do not stop the pairing", "[exporthead]") {
  auto f = facts();
  f.unresolved = {"0A1B2C:Mod.esp"};
  const auto m = match_export_head(f, {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(has(m, "HeadPartsUnresolved"));
}

TEST_CASE("Export head: textures the head needs that the profile lacks are NEEDS_ASSET", "[exporthead]") {
  auto c = head("1 Okiri V3", kFull);
  c.missing_textures = {"textures\\okiri\\femalehead_msn.dds", "textures\\okiri\\femalehead_sk.dds"};
  const auto m = match_export_head(facts(), {c});
  CHECK(m.verdict == HeadVerdict::NeedsAsset);
  CHECK(has(m, "TexturesMissing"));
  CHECK(m.message.find("2 texturas") != std::string::npos);
  CHECK(m.message.find("femalehead_msn.dds") != std::string::npos);
}

TEST_CASE("Export head: another vertex count than the face part (HPH vs vanilla) is UNSUPPORTED_TOPOLOGY", "[exporthead]") {
  auto f = facts();
  f.face_vertices = 1300;  // the preset's face part is the vanilla mesh
  const auto m = match_export_head(f, {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::UnsupportedTopology);
  CHECK(has(m, "TopologyDiffers"));
  CHECK(m.message.find("3832") != std::string::npos);
  CHECK(m.message.find("1300") != std::string::npos);
}

TEST_CASE("Export head: when the topology could not be checked the pairing says so", "[exporthead]") {
  auto f = facts();
  f.face_vertices.reset();
  const auto m = match_export_head(f, {head("1 Okiri V3", kFull)});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(has(m, "TopologyUnchecked"));
  // Matching vertex counts: nothing to flag.
  CHECK_FALSE(has(match_export_head(facts(), {head("1 Okiri V3", kFull)}), "TopologyUnchecked"));
}

TEST_CASE("Export head: two equally likely heads need a manual choice, and the choice settles it", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("Alpha", kFull), head("Beta", kFull)};
  const auto m = match_export_head(facts("Gamma"), cs);
  CHECK(m.verdict == HeadVerdict::NeedsChoice);
  CHECK(m.needs_manual_choice);
  CHECK_FALSE(m.chosen);
  CHECK(has(m, "AmbiguousHead"));
  CHECK(m.ranked.size() == 2);
  const auto picked = match_export_head(facts("Gamma"), cs, 1);
  CHECK(picked.verdict == HeadVerdict::ReadyForReview);
  CHECK(*picked.chosen == 1);
}

TEST_CASE("Export head: a manual choice is checked like any other head", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("1 Okiri V3", kFull), head("Broken", {}, 1000, false)};
  CHECK(match_export_head(facts(), cs, 1).verdict == HeadVerdict::FaceGenUnreadable);
  const std::vector<HeadCandidate> wrong = {head("1 Okiri V3", kFull), head("Wrong", {"Other"})};
  CHECK(match_export_head(facts(), wrong, 1).verdict == HeadVerdict::HeadPartMismatch);
  // An index that does not exist is ignored.
  CHECK(match_export_head(facts(), cs, 99).verdict == HeadVerdict::ReadyForReview);
}

TEST_CASE("Export head: an exact name beats an unnamed head with a higher overlap", "[exporthead]") {
  auto partial = kFull;
  partial.push_back("ExtraBeard");  // overlap 4/5 = 0.8
  const std::vector<HeadCandidate> cs = {head("Unnamed", kFull), head("1 Okiri V3", partial)};
  const auto m = match_export_head(facts(), cs);
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(*m.chosen == 1);
  CHECK(m.ranked.front().index == 1);
}

TEST_CASE("Export head: among heads of the same name the newer export wins, so no choice is needed", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("1 Okiri V3", kFull, 1000), head("1 Okiri V3", kFull, 5000)};
  const auto m = match_export_head(facts(), cs);
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK(*m.chosen == 1);
  // Same modification time: it cannot be decided.
  CHECK(match_export_head(facts(), {head("1 Okiri V3", kFull, 7), head("1 Okiri V3", kFull, 7)}).verdict == HeadVerdict::NeedsChoice);
}

TEST_CASE("Export head: shape names are compared without case", "[exporthead]") {
  const auto m = match_export_head(facts(), {head("1 Okiri V3", {"klheadf", "KLEYESF", "klbrowsf", "hairlonga"})});
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
}

TEST_CASE("Export head: one head shared by three presets pairs with each of them (Rose Sculpt UBE)", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("Rose Sculpt UBE", kFull)};
  for (const auto* name : {"Rose - Bun Hair UBE", "Rose - Long Hair UBE", "Rose - Ponytail Hair UBE"}) {
    INFO(name);
    const auto m = match_export_head(facts(name), cs);
    CHECK(m.verdict == HeadVerdict::ReadyForReview);
    CHECK(m.ranked.front().near_name);  // 'rose' is the hint
  }
}

TEST_CASE("Export head: time-stamped RaceMenu names pair by content, not by the date in the name", "[exporthead]") {
  // Preset_5-1-2025_14-10-43 / Head_5-1-2025_14-10-51: only numbers in common, which are no name hint.
  const std::vector<HeadCandidate> cs = {head("Head_5-1-2025_14-10-51", kFull)};
  const auto m = match_export_head(facts("Preset_5-1-2025_14-10-43"), cs);
  CHECK(m.verdict == HeadVerdict::ReadyForReview);
  CHECK_FALSE(m.ranked.front().near_name);
  CHECK_FALSE(m.ranked.front().exact_name);
  CHECK(m.ranked.front().overlap == 1.0);
  // Same date in the name, different parts: the date alone matches nothing.
  CHECK(match_export_head(facts("Preset_5-1-2025_14-10-43"), {head("Head_5-1-2025_14-10-51", {"KLHeadF", "A", "B", "C", "D"})}).verdict ==
        HeadVerdict::NeedsAsset);
}

TEST_CASE("Export head: without any name hint it takes an almost complete overlap", "[exporthead]") {
  const std::vector<std::string> most = {"KLHeadF", "KLEyesF", "KLBrowsF"};  // the Face part is there, one of 4 parts is not: 3/4 = 0.75 < 0.85
  CHECK(match_export_head(facts("Nothing In Common"), {head("Other Name", most)}).verdict == HeadVerdict::NeedsAsset);
  CHECK(match_export_head(facts("Nothing In Common"), {head("Other Name", kFull)}).verdict == HeadVerdict::ReadyForReview);
}

TEST_CASE("Export head: the result is deterministic and never READY for an unreadable head", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("1 Okiri V3", {}, 1, false), head("Alpha", kFull), head("Beta", kFull)};
  const auto a = match_export_head(facts(), cs);
  const auto b = match_export_head(facts(), cs);
  CHECK(a.verdict == b.verdict);
  CHECK(a.message == b.message);
  CHECK(a.codes == b.codes);
  CHECK(a.verdict != HeadVerdict::ReadyForReview);
}

TEST_CASE("Export head: a damaged head with the exact name is not replaced by another head that merely fits", "[exporthead]") {
  const std::vector<HeadCandidate> cs = {head("1 Okiri V3", {}, 1, false), head("Alpha", kFull)};
  const auto m = match_export_head(facts(), cs);
  CHECK(m.verdict == HeadVerdict::FaceGenUnreadable);
  CHECK(*m.chosen == 0);
  // A readable head of the same exact name does win over the damaged one.
  const std::vector<HeadCandidate> both = {head("1 Okiri V3", {}, 1, false), head("1 Okiri V3", kFull, 9)};
  CHECK(match_export_head(facts(), both).verdict == HeadVerdict::ReadyForReview);
}

TEST_CASE("Export head: verdict names are the documented codes", "[exporthead]") {
  CHECK(to_string(HeadVerdict::NeedsAsset) == "NEEDS_ASSET");
  CHECK(to_string(HeadVerdict::NeedsChoice) == "NEEDS_CHOICE");
  CHECK(to_string(HeadVerdict::FaceGenUnreadable) == "FACEGEN_UNREADABLE");
  CHECK(to_string(HeadVerdict::HeadPartMismatch) == "HEAD_PART_MISMATCH");
  CHECK(to_string(HeadVerdict::UnsupportedTopology) == "UNSUPPORTED_TOPOLOGY");
}
