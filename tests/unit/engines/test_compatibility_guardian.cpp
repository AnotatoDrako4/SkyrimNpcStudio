#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include "sns/appearance/compatibility_guardian.hpp"

using namespace sns;
using namespace sns::appearance;

TEST_CASE("CompatibilityGuardian: Body system detection", "[compatibility]") {
  SECTION("Detects vanilla when no mods present") {
    auto body = CompatibilityGuardian::inspect_body_system({}, {}, true);
    REQUIRE(body.body_type == DetectedBodyType::Vanilla);
    REQUIRE_FALSE(body.physics_enabled);
  }

  SECTION("Detects CBBE from plugin") {
    auto body = CompatibilityGuardian::inspect_body_system({"Skyrim.esm", "CBBE.esp"}, {}, true);
    REQUIRE(body.body_type == DetectedBodyType::CBBE);
    REQUIRE(body.supports_weight_sliders);
  }

  SECTION("Detects 3BA and physics from paths") {
    auto body = CompatibilityGuardian::inspect_body_system(
        {"Skyrim.esm"},
        {"meshes/actors/character/character assets/femalebody_3ba.nif", "meshes/smp/physics.xml"},
        true);
    REQUIRE(body.body_type == DetectedBodyType::ThreeBA);
    REQUIRE(body.physics_enabled);
  }

  SECTION("Detects HIMBO for male actor") {
    auto body = CompatibilityGuardian::inspect_body_system({"Skyrim.esm", "HIMBO.esp"}, {}, false);
    REQUIRE(body.body_type == DetectedBodyType::HIMBO);
  }
}

TEST_CASE("CompatibilityGuardian: Draft evaluation", "[compatibility]") {
  AppearanceDraft draft;

  SECTION("Reports error when no target NPC is selected") {
    auto report = CompatibilityGuardian::evaluate(draft);
    REQUIRE(report.badge == HealthBadge::BlockingError);
    REQUIRE_FALSE(report.can_export);
    REQUIRE(report.error_count() >= 1);
  }

  SECTION("Reports slot conflict when two pieces share slot 32") {
    draft.set_target(projects::NpcTarget{
        .form_key = "000A2C8E:Skyrim.esm",
        .editor_id = "Lydia",
        .name = "Lydia",
        .winning_plugin = "Skyrim.esm",
    });

    FaceState face;
    face.head_parts = {"051193:Skyrim.esm", "051194:Skyrim.esm"};
    draft.set_face_baseline(face);

    projects::OutfitPieceRef cuirass_a{
        .form_key = "013952:Skyrim.esm",
        .name = "Steel Cuirass",
        .plugin = "Skyrim.esm",
        .slot_mask = 1u << (32 - 30),  // slot 32 mask (bit 2)
    };

    projects::OutfitPieceRef cuirass_b{
        .form_key = "0896A3:Skyrim.esm",
        .name = "Elven Cuirass",
        .plugin = "Skyrim.esm",
        .slot_mask = 1u << (32 - 30),  // slot 32 mask (bit 2)
    };

    draft.outfit().assign({cuirass_a, cuirass_b});

    auto report = CompatibilityGuardian::evaluate(draft);
    REQUIRE(report.badge == HealthBadge::BlockingError);
    REQUIRE_FALSE(report.can_export);
    REQUIRE(report.error_count() == 1);
    REQUIRE(report.issues[0].code == "SlotConflict");
  }

  SECTION("Reports Verified when draft has compatible pieces and valid face") {
    draft.set_target(projects::NpcTarget{
        .form_key = "000A2C8E:Skyrim.esm",
        .editor_id = "Lydia",
        .name = "Lydia",
        .winning_plugin = "Skyrim.esm",
    });

    FaceState face;
    face.head_parts = {"051193:Skyrim.esm", "051194:Skyrim.esm"};
    draft.set_face_baseline(face);

    projects::OutfitPieceRef cuirass{
        .form_key = "013952:Skyrim.esm",
        .name = "Steel Cuirass",
        .plugin = "Skyrim.esm",
        .slot_mask = 1u << (32 - 30),  // slot 32
    };

    projects::OutfitPieceRef boots{
        .form_key = "013951:Skyrim.esm",
        .name = "Steel Boots",
        .plugin = "Skyrim.esm",
        .slot_mask = 1u << (37 - 30),  // slot 37
    };

    draft.outfit().assign({cuirass, boots});
    draft.set_initial_outfit(draft.outfit().pieces());

    auto report = CompatibilityGuardian::evaluate(draft);
    REQUIRE(report.badge == HealthBadge::Verified);
    REQUIRE(report.can_export);
    REQUIRE(report.error_count() == 0);
    // A preliminary check: the summary must not claim in-game compatibility.
    CHECK(report.summary.find("100%") == std::string::npos);
    CHECK(report.summary.find("Skyrim") != std::string::npos);  // it points to the in-game test instead
  }

  SECTION("Blocks a material edit whose Skyrim resources are not generated") {
    draft.set_target(projects::NpcTarget{.form_key = "000A2C8E:Skyrim.esm", .name = "Lydia"});
    projects::OutfitPieceRef cuirass{.form_key = "013952:Skyrim.esm", .name = "Steel Cuirass",
                                      .plugin = "Skyrim.esm", .slot_mask = 1u << (32 - 30)};
    projects::PieceMaterialOverride material;
    material.name = "Dark steel";
    material.tint_color = projects::ColorRgb{32, 32, 32};
    cuirass.material = material;
    draft.outfit().assign({cuirass});

    const auto report = CompatibilityGuardian::evaluate(draft);
    REQUIRE_FALSE(report.can_export);
    REQUIRE(report.badge == HealthBadge::BlockingError);
    REQUIRE(report.error_count() == 1);
    REQUIRE(std::any_of(report.issues.begin(), report.issues.end(),
                        [](const auto& issue) { return issue.code == "MaterialVariantPreviewOnly"; }));
  }
}
