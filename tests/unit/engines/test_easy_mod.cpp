#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "sns/appearance/easy_mod.hpp"

using namespace sns::appearance;

namespace {

EasyModInputs loaded() {
  EasyModInputs in;
  in.npc_loaded = true;
  in.npc_label = "Lydia (0A2C8E:Skyrim.esm)";
  return in;
}

const EasyModItem* find(const std::vector<EasyModItem>& items, const std::string& key) {
  const auto it = std::find_if(items.begin(), items.end(), [&](const EasyModItem& i) { return i.key == key; });
  return it == items.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("Easy Mod Creator: without an NPC nothing can be exported", "[appearance][easymod]") {
  const auto plan = plan_easy_mod({});
  CHECK(plan.mode == EasyModMode::None);
  CHECK_FALSE(plan.can_export);
  REQUIRE(find(plan.checks, "npc"));
  CHECK(find(plan.checks, "npc")->state == EasyModState::Blocked);
}

TEST_CASE("Easy Mod Creator: no edits means nothing to export", "[appearance][easymod]") {
  const auto plan = plan_easy_mod(loaded());
  CHECK(plan.mode == EasyModMode::None);
  CHECK_FALSE(plan.can_export);
  REQUIRE(find(plan.checks, "changes"));
  CHECK(find(plan.checks, "changes")->state == EasyModState::Blocked);
  // The Skyrim check is always there and never counts as passed.
  REQUIRE(find(plan.checks, "skyrim"));
  CHECK(find(plan.checks, "skyrim")->state == EasyModState::Info);
}

TEST_CASE("Easy Mod Creator: a hair color edit is the minimal face export", "[appearance][easymod]") {
  auto in = loaded();
  in.face_kind = FaceBuildKind::HairColorSafe;
  in.face_exportable = true;
  in.face_integrity = "hair-tint-only";
  const auto plan = plan_easy_mod(in);
  CHECK(plan.mode == EasyModMode::FaceOverhaul);
  CHECK(plan.can_export);
  REQUIRE(plan.changes.size() == 1);
  CHECK(plan.changes[0].key == "hair");
  CHECK(find(plan.checks, "facegen")->state == EasyModState::Ok);

  SECTION("without the integrity guard result it is blocked") {
    in.face_integrity.clear();
    const auto p = plan_easy_mod(in);
    CHECK_FALSE(p.can_export);
    CHECK(find(p.checks, "facegen")->state == EasyModState::Blocked);
  }
  SECTION("while the head is building it waits") {
    in.face_building = true;
    const auto p = plan_easy_mod(in);
    CHECK_FALSE(p.can_export);
    CHECK(find(p.checks, "facegen")->state == EasyModState::Waiting);
  }
  SECTION("a blocked hair color operation blocks the export") {
    in.blocked_operations = {"hair-color"};
    const auto p = plan_easy_mod(in);
    CHECK_FALSE(p.can_export);
    CHECK(find(p.checks, "compat")->state == EasyModState::Blocked);
  }
}

TEST_CASE("Easy Mod Creator: the outfit alone is Mode A and needs a valid outfit", "[appearance][easymod]") {
  auto in = loaded();
  in.outfit_changed = true;
  in.outfit_pieces = 3;
  in.outfit_exportable = true;
  auto plan = plan_easy_mod(in);
  CHECK(plan.mode == EasyModMode::OutfitPatch);
  CHECK(plan.can_export);
  CHECK(find(plan.changes, "outfit"));

  in.outfit_exportable = false;
  plan = plan_easy_mod(in);
  CHECK_FALSE(plan.can_export);
  CHECK(find(plan.checks, "outfit")->state == EasyModState::Blocked);

  in.outfit_exportable = true;
  in.outfit_preview_material = true;
  plan = plan_easy_mod(in);
  CHECK_FALSE(plan.can_export);

  in.outfit_preview_material = false;
  in.outfit_pieces = 0;
  plan = plan_easy_mod(in);
  CHECK(plan.mode == EasyModMode::None);
  CHECK_FALSE(plan.can_export);
}

TEST_CASE("Easy Mod Creator: an unchanged outfit is not exported", "[appearance][easymod]") {
  // The draft starts with the NPC's current outfit: pieces without a change are not an edit.
  auto in = loaded();
  in.outfit_pieces = 4;
  in.outfit_exportable = true;
  in.face_kind = FaceBuildKind::HairColorSafe;
  in.face_exportable = true;
  in.face_integrity = "hair-tint-only";
  const auto plan = plan_easy_mod(in);
  CHECK(plan.mode == EasyModMode::FaceOverhaul);
  CHECK_FALSE(find(plan.changes, "outfit"));
}

TEST_CASE("Easy Mod Creator: face and outfit together are Mode C", "[appearance][easymod]") {
  auto in = loaded();
  in.face_kind = FaceBuildKind::Full;
  in.face_exportable = true;
  in.outfit_changed = true;
  in.outfit_pieces = 2;
  in.outfit_exportable = true;
  in.provenance_conflict = "El ESP ganador y el FaceGen vienen de mods distintos.";
  const auto plan = plan_easy_mod(in);
  CHECK(plan.mode == EasyModMode::CompleteOverhaul);
  CHECK(plan.can_export);  // a warning does not block
  CHECK(find(plan.checks, "provenance")->state == EasyModState::Warning);

  SECTION("a face edit on a head whose morphs do not match is blocked") {
    in.blocked_operations = {"face-morphs"};
    CHECK_FALSE(plan_easy_mod(in).can_export);
  }
}

TEST_CASE("Easy Mod Creator: while the FaceGen is analysed nothing is exported", "[appearance][easymod]") {
  auto in = loaded();
  in.face_kind = FaceBuildKind::WaitForAnalysis;
  const auto plan = plan_easy_mod(in);
  CHECK_FALSE(plan.can_export);
  CHECK(find(plan.checks, "analysis")->state == EasyModState::Waiting);
  CHECK_FALSE(find(plan.checks, "changes"));
}

TEST_CASE("Easy Mod Creator: mode and state names", "[appearance][easymod]") {
  CHECK(to_string(EasyModMode::OutfitPatch) == "outfit-patch");
  CHECK(to_string(EasyModMode::FaceOverhaul) == "face-overhaul");
  CHECK(to_string(EasyModMode::CompleteOverhaul) == "complete-overhaul");
  CHECK(to_string(EasyModMode::None).empty());
  CHECK(to_string(EasyModState::Blocked) == "blocked");
  CHECK(to_string(EasyModState::Waiting) == "waiting");
}
