#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "sns/body/bodyslide_morphs.hpp"
#include "sns/body/bodyslide_presets.hpp"

namespace sns::body {

// The NIF files BodySlide writes for one slider set and one preset, built the way BodySlideApp::BuildBodies does it:
// the project's source NIF (ShapeData), sliders applied to each project shape's own vertices (apply_project_sliders,
// values from `preset` or the project defaults), then per shape normals recalculated unless the shape locks them
// (CalcNormalsForShape with its SmoothSeamNormals setting), tangents recalculated, zapped vertices deleted (a fully
// zapped shape is deleted, or hidden with KeepZappedShapes), and the file saved without optimization. GenWeights
// sets give `small` (_0, weight 0) and `big` (_1, weight 100); other sets only `big`. Without `build_morphs` no TRI is
// written: the caller keeps the morph file the profile already has, which is valid only while no shape loses part of its
// vertices (`vertex_count_changed`), and the source NIF's extra data is kept as it is. With `build_morphs` (BodySlide's
// «Build Morphs»): `tri` holds <OutputPath>\<OutputFile>.tri as BodySlideApp::WriteMorphTRI writes it (each position
// slider's full offsets per shape, zapped vertices removed, near-zero offsets dropped) and both NIFs get a BODYTRI
// string pointing at it, so RaceMenu / Body Change NG morph the garment in game like the body.
// Not reproduced: BodySlide's optional clipping fix and "force body normals" (both off by default).
struct SliderSetShapeBuild {
  std::string shape;
  std::size_t vertices = 0;          // before zapping
  std::size_t zapped = 0;            // vertices deleted (the larger of _0 and _1)
  std::size_t sliders_applied = 0;   // _1 build
  float max_displacement = 0.0f;     // _1 build, from the source NIF
  bool normals_recalculated = false;
  bool deleted = false;              // every vertex zapped
};
struct SliderSetBuild {
  std::string small;  // _0 NIF bytes ("" without GenWeights or on error)
  std::string big;    // _1 (or the single) NIF bytes ("" on error)
  std::string tri;       // with build_morphs: the TRI bytes ("" otherwise or on error)
  std::string tri_path;  // with build_morphs: Data-relative path of the TRI ("meshes\armor\x\shirt.tri")
  std::vector<SliderSetShapeBuild> shapes;
  std::vector<std::string> errors;
  [[nodiscard]] bool ok() const { return errors.empty() && !big.empty(); }
  [[nodiscard]] bool vertex_count_changed() const;
};
SliderSetBuild build_slider_set(const SliderProject& project, std::string_view source_nif, const DiffSets& data,
                                const std::vector<SliderSetting>& preset, bool build_morphs = false);
// TriFile::Write: "PIRT", the position morphs of each shape (multiplier = largest |component| / 32767, offsets stored as int16
// truncated toward zero), then the UV section (count 0 when there are none). Offsets are written in the order given.
std::string serialize_tri(const TriFile& tri);

// Batch builds for one preset. A preset only fits slider sets of its own body family: the sliders of another family's set are not
// in its vocabulary, and the few names that match move other anatomy, so those sets need a conversion, not a preset.
// family_reference_project: the set whose sliders cover most of the preset's non-zero values (decided from the files, not names).
const SliderProject* family_reference_project(const std::vector<SliderProject>& projects, const std::vector<SliderSetting>& preset);
// action: "build"; "other-family" (the set's sliders are not the reference's); "untouched" (no slider of the set has a value);
// "ambiguous": a small set (hands, gloves, boots) shares sliders with every family, so its vocabulary proves nothing. It is only
// taken for the family when its meshes live under the reference's own top folder ("meshes\\!ube"); else it is left out.
inline constexpr std::size_t kMinFamilyProofSliders = 40;
struct BatchDecision {
  std::string action;
  std::string reason;  // Spanish
};
BatchDecision decide_batch_set(const SliderProject& project, const SliderProject& reference, const std::vector<SliderSetting>& preset);

// How far a garment sinks into a body, both in the same space (shape.positions of meshes loaded with nif::load_mesh, i.e.
// the reference pose): for each garment vertex within `reach` of the body surface, the signed distance to the closest
// body triangle (negative = inside, the triangle's winding normal points out). `inside` counts vertices deeper than
// `tolerance`: the body pokes through the garment there in the rest pose. Static geometry only: no animation or physics.
struct PenetrationReport {
  std::size_t garment_vertices = 0;
  std::size_t near_body = 0;     // within reach
  std::size_t inside = 0;        // deeper than tolerance
  float max_depth = 0.0f;        // deepest point (positive number)
  float mean_gap = 0.0f;         // mean signed distance of the vertices near the body
};
PenetrationReport measure_penetration(const nif::Shape& body, const nif::Shape& garment, float reach = 3.0f, float tolerance = 0.1f);

}  // namespace sns::body
