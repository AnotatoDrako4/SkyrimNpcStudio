#pragma once

// NPC fidelity report: what the 3D view shows of an NPC against what the game would draw, from the effective appearance (worker
// records) and the built scene (MO2 files, real geometry). Every finding says whether the cause is the DATA (records/files), a
// COMPATIBILITY problem between correct files (body family, fit, skeleton) or the RENDERER (what this viewer does not reproduce),
// with the measured evidence and how sure it is. Nothing is decided from folder or file names.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sns/rendering/scene.hpp"

namespace sns::rendering {

enum class FindingCategory { Data, Compatibility, Renderer };
// Confirmed: measured on the files/records, or an engine rule the scene already applies (slot unequip, race filter).
// Probable: a known engine behaviour this viewer does not reproduce (dismember partitions, addon priority), or a geometric measure
// taken in the reference pose only. Uncertain: not enough information; the finding says what is missing.
enum class FindingConfidence { Confirmed, Probable, Uncertain };

std::string_view to_string(FindingCategory category);
std::string_view to_string(FindingConfidence confidence);

struct FidelityFinding {
  std::string code;  // stable identifier, e.g. "GarmentClipsBody"
  FindingCategory category = FindingCategory::Data;
  std::string severity;  // "error" (wrong in game), "warning" (likely visible problem), "info"
  FindingConfidence confidence = FindingConfidence::Confirmed;
  std::string armor_form_key;  // piece concerned ("" = the whole NPC)
  std::string piece;
  std::string subject;   // file path or record the finding is about
  std::string message;   // Spanish, plain language
  std::string evidence;  // measured facts, Spanish
  std::string action;    // concrete next step, Spanish
  std::vector<std::string> part_ids;  // scene parts to isolate in the viewer
};

// Where every resource of the scene came from and why it was the one used.
struct ResourceTrace {
  std::string kind;      // "armor", "addon", "mesh", "texture", "tri", "physics", "skeleton", "facegen-geometry", "facegen-tint"
  std::string key;       // FormKey or Data path
  std::string provider;  // "loose:<mod>", "bsa:<archive> (<mod>)", "generated (draft)", "missing", or the record source
  std::string piece;     // piece / part label it belongs to ("" = NPC-wide)
  std::string reason;    // why this one (Spanish)
};

// One garment shape measured against the body it is worn over, both as drawn (same pose, same space, Skyrim units).
struct GarmentFit {
  std::string part_id;
  std::string body_part_id;
  std::string piece;
  std::string shape;
  std::string body_shape;
  std::size_t vertices = 0;
  std::size_t near_body = 0;  // garment vertices within the reach of the body surface
  std::size_t inside = 0;     // deeper than the tolerance inside the body: the body pokes through there
  float inside_ratio = 0.0f;  // inside / near_body
  float max_depth = 0.0f;
  float mean_gap = 0.0f;      // mean signed distance of the near vertices (negative = sunk)
  std::string verdict;        // "fits", "clips", "clips-badly", "not-near-body"
};

// A body/hands/feet addon the outfit hides (slot covered) measured against what is drawn instead: its vertices farther than the
// margin from every drawn surface (nothing replaces the skin there) and seen by some camera around the character without anything
// in front. There the game shows a hole or a missing limb. Skin that a garment replaces nearby (a shoe shaped differently from the
// foot, a body copy inside the garment) is not counted.
struct ExposedSkin {
  std::string part_id;
  std::string piece;
  std::string covered_by;
  std::size_t vertices = 0;
  std::size_t far_from_drawn = 0;  // farther than the margin from every drawn surface (covered or not)
  std::size_t exposed = 0;         // far from drawn surfaces and visible from some view (see_through + outside, without overlap)
  // Exposed vertices by what a camera finds there: something drawn far behind (looking through an opening into the character) or
  // nothing at all (outside the drawn silhouette: a missing limb, or a garment shaped differently from the skin it replaces).
  std::size_t see_through = 0, outside = 0;
  float see_through_min_z = 0.0f, see_through_max_z = 0.0f;
  float outside_min_z = 0.0f, outside_max_z = 0.0f;
  float exposed_ratio = 0.0f;
  float exposed_min_z = 0.0f, exposed_max_z = 0.0f;  // view-space height range of the exposed vertices (Skyrim units)
};

struct PieceVerdict {
  std::string armor_form_key;
  std::string name;
  std::string source;  // "outfit" / "skin"
  // What the game should draw for this piece, from the records and files: "drawn", "not-drawn", "partial", "unknown".
  std::string game;
  // What this viewer draws by default: "drawn", "hidden", "partial", "nothing".
  std::string viewer;
  bool viewer_matches_game = true;
  std::size_t shapes_drawn = 0;
  std::size_t shapes_hidden = 0;
  std::vector<std::string> part_ids;
  std::vector<std::string> finding_codes;
};

struct FidelityOptions {
  float penetration_reach = 3.0f;      // Skyrim units: garment vertices farther from the body are not compared
  float penetration_tolerance = 0.1f;  // sunk deeper than this counts as inside
  float clip_ratio = 0.05f;            // more inside than this share of the near vertices: "clips"
  float clip_badly_ratio = 0.25f;      // "clips-badly"
  float exposed_ratio = 0.08f;         // a hidden body with more exposed surface than this: holes in game
  float exposure_margin = 1.0f;        // Skyrim units: a drawn surface this close replaces the hidden skin
  float see_through_gap = 2.0f;        // something drawn farther than this behind the skin is the far side seen through an opening
  float missing_part_span = 15.0f;     // an uncovered region taller than this is a missing body part, not a reshaped one
  int exposure_resolution = 512;       // z-buffer side in pixels for the exposure test
  std::function<bool()> cancelled;     // checked between steps; a cancelled report says so and is incomplete
};

struct FidelityReport {
  std::string npc_form_key;
  std::string body_part_id;  // the body the garments are measured against ("" = none drawn)
  std::string body_source;   // "skin" (race/NPC skin) or "outfit" (a body shape inside a garment NIF)
  std::string body_mesh;
  std::string body_provider;
  std::vector<ResourceTrace> resources;
  std::vector<PieceVerdict> pieces;
  std::vector<GarmentFit> fits;
  std::vector<ExposedSkin> exposed;
  std::vector<FidelityFinding> findings;
  std::size_t data_findings = 0, compatibility_findings = 0, renderer_findings = 0;
  // Confirmed DATA errors (a piece with no addon for the race, a missing mesh or skeleton): the export would ship an NPC that is
  // broken in game, so the preflight refuses while any is present.
  std::vector<std::string> export_blockers;
  // Other errors (compatibility, or probable): the preflight asks the user to review them before exporting.
  std::vector<std::string> export_warnings;
  double analysis_ms = 0.0;
  double fit_ms = 0.0;       // garment fit against the body (grid build + queries)
  double exposure_ms = 0.0;  // hidden skin: distance grid + 16 z-buffers
  bool cancelled = false;
};

FidelityReport analyze_fidelity(const Appearance& appearance, const CharacterScene& scene, const FidelityOptions& options = {});

nlohmann::json to_json(const FidelityReport& report);

// ---------------------------------------------------------------- building blocks (tested directly)

// Triangle soup with a uniform grid, for closest-point queries within a fixed reach.
class TriangleGrid {
 public:
  TriangleGrid(const std::vector<float>& positions, const std::vector<std::uint32_t>& indices, float reach);
  // Closest point on the surface within reach of p: signed distance (negative behind the triangle's winding normal). false = none.
  bool closest(const std::array<float, 3>& p, float& signed_distance) const;
  [[nodiscard]] std::size_t triangle_count() const { return tris_.size(); }

 private:
  struct Tri {
    std::array<float, 3> a, b, c, n;
    std::array<float, 3> lo, hi;  // bounds, to skip the exact test when the box is already farther than the best hit
  };
  std::vector<Tri> tris_;
  std::vector<std::vector<std::uint32_t>> cells_;
  std::array<float, 3> origin_{};
  std::array<int, 3> dims_{1, 1, 1};
  float cell_ = 1.0f;
  float reach_ = 1.0f;
  [[nodiscard]] std::size_t cell_index(int x, int y, int z) const;
};

// Penetration of a garment into a body (positions: x,y,z triples in one space), the semantics of body::measure_penetration with a
// grid instead of a scan of every triangle.
GarmentFit measure_fit(const std::vector<float>& body_positions, const std::vector<std::uint32_t>& body_indices,
                       const std::vector<float>& garment_positions, float reach, float tolerance);
// The same against a grid built once for the body (several garments over one body).
GarmentFit measure_fit(const TriangleGrid& body, const std::vector<float>& garment_positions, float tolerance);

// Positions and normals (x,y,z triples) of a part's interleaved vertices.
std::vector<float> part_positions(const RenderPart& part);
std::vector<float> part_normals(const RenderPart& part);

}  // namespace sns::rendering
