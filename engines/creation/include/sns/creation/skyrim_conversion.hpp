#pragma once

#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

// Character Forge -> Skyrim conversion (contract sns.forge.skyrim/1 of tools/blender/forge_skyrim.py).
// Blender only collects facts; every verdict is made here, deterministically, so the same report always
// yields the same issues, and the NIF is re-read with the studio's own nifly build, not PyNifly's.
namespace sns::creation::skyrim {

inline constexpr std::string_view kReportSchema = "sns.forge.skyrim/1";
inline constexpr std::string_view kJobSchema = "sns.forge.skyrim.job/1";
// Skyrim SE NIF: Gamebryo 20.2.0.7, user version 12, BS stream 100 (Oldrim uses 83).
inline constexpr std::uint32_t kSkyrimSeStream = 100;
inline constexpr std::uint32_t kSkyrimLeStream = 83;
// Skyrim units: 1 unit = 1.428 cm (70 units ~ 1 m). Blender meters are exported 1:1 by PyNifly.
inline constexpr double kCentimetersPerUnit = 1.428;

// Severities, worst first: "blocking", "autoFixable", "warning", "manualReview", "compatible".
// Profiles: "static" (Type A, implemented), "skinned" (meshes bound to one armature, validated against
// report.skeletonMapping; never decimated). "armor" and "npc" are refused with an explicit blocking issue.
//
// classify(report, profile) -> {
//   profile, issues: [{code, severity, object, problem, consequence, solution, nonDestructive, fixAvailable}],
//   counts: {blocking, autoFixable, warning, manualReview, compatible}, exportable }
// fixAvailable is true only for repairs forge_skyrim.py really performs on the working copy (safe transforms,
// texture staging, detaching a missing image). exportable == no blocking issue.
nlohmann::json classify(const nlohmann::json& report, std::string_view profile = "static");

// Reads the BS stream version of NIF bytes (throws sns::Error(Parse) on a header that is not Skyrim's).
std::uint32_t nif_stream_version(std::string_view bytes);

// Re-reads an exported NIF with sns::nif and checks it against the Blender report of the same job:
// format SE, readable geometry, triangle total equal to the prepared meshes, UV/normals per vertex,
// bounds equal to the prepared Blender bounds, Data-relative texture paths that were staged by the job,
// Bethesda shader blocks, no skin for the static profile, and agreement with PyNifly's own readback.
// -> {passed, checks: [{code, passed, detail}], shapes: [{name, vertices, triangles, uvs, normals, bones,
//     shaderBlock, textures}], streamVersion, triangles, boundsMin, boundsMax}
// (boundsMin/boundsMax: the nifly-read box in NIF root space, only when the NIF has triangles)
nlohmann::json verify_nif(std::string_view nif_bytes, const nlohmann::json& report, std::string_view profile = "static");
// Profile "skinned" adds: skinned, bonesMatchWeights, influencesValid (1..4, sum 1), skinPartitions (NiSkinPartition covers
// every triangle; SBP groups became partitions), bindMatchesBlender, bindMatchesReference (report.skeletonMapping) and
// weightsMatchBlender (sampled exporter-side weights == NIF weights at the same position). Bounds use the skin frame.

// SkinnedMesh pipeline states, in order: PREVIEW_ONLY, RIG_INSPECTED, SKELETON_MAPPED, SKIN_VALIDATED, NIF_EXPORTED,
// NIF_VERIFIED, PENDIENTE_SKYRIM. Each one requires the previous; the chain stops at the first unmet state, so an
// incomplete export never shows as validated. PENDIENTE_SKYRIM is the ceiling: in-game verification is human only.
// skinned_states(report, classification, verification or null) -> {current, states: [{state, reached, detail}]}
nlohmann::json skinned_states(const nlohmann::json& report, const nlohmann::json& classification, const nlohmann::json* verification);

}  // namespace sns::creation::skyrim
