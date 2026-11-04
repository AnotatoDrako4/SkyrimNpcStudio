#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/body/bodyslide_presets.hpp"
#include "sns/nif/mesh.hpp"

namespace sns::mo2 {
class VirtualFileSystem;
}

// Real BodySlide geometry: the slider data BodySlide and RaceMenu use, read from the files themselves, and applied to vertices.
// Formats and arithmetic follow BodySlide-and-Outfit-Studio (ousnius, branch dev): TriFile::Read, ReadOSDData, DiffDataSets::LoadSet,
// SliderSet / SliderData XML, SliderManager defaults and BodySlideApp::ApplySliders. Nothing here approximates a slider by its name.
namespace sns::body {

// One vertex offset, in the shape's own vertex space (what nifly's GetVertsForShape returns: skin space for skinned shapes).
struct VertexDelta {
  std::uint16_t index = 0;
  float x = 0, y = 0, z = 0;
};

struct MorphData {
  std::string name;  // the BodySlide slider name ("Breasts") in a TRI; the data name ("3BA_Breasts") in an OSD
  std::vector<VertexDelta> offsets;
};

// ---------------------------------------------------------------- TRI ("PIRT" magic, written by BodySlide "Build Morphs")
struct TriShape {
  std::string name;                // NIF shape name the morphs belong to
  std::vector<MorphData> position;  // offsets already multiplied by the stored multiplier (int16 * mult)
  std::vector<MorphData> uv;        // UV morphs (x = du, y = dv); never applied to positions
  [[nodiscard]] const MorphData* find(std::string_view slider) const;  // exact name, like RaceMenu
};

struct TriFile {
  std::vector<TriShape> shapes;
  std::vector<std::string> warnings;  // e.g. a truncated UV section (position morphs stay valid)
  [[nodiscard]] const TriShape* shape(std::string_view name) const;  // exact NIF shape name
};

// Throws sns::Error(Parse) for a wrong magic or a truncated file.
TriFile parse_tri(std::string_view bytes, std::string_view label);

// ---------------------------------------------------------------- OSD / BSD (Outfit Studio slider data)
struct OsdFile {
  std::uint32_t version = 1;
  std::map<std::string, std::vector<VertexDelta>> data;  // data name -> offsets
};
OsdFile parse_osd(std::string_view bytes, std::string_view label);
std::vector<VertexDelta> parse_bsd(std::string_view bytes, std::string_view label);

// ---------------------------------------------------------------- OSP slider sets (CalienteTools\BodySlide\SliderSets\*.osp)
struct ProjectSliderData {
  std::string name;    // <Data name>: the data set the slider moves
  std::string target;  // <Data target>: the shape target it applies to
  bool local = false;  // read from the set's own DataFolder instead of the shape's folders
  std::string file;    // "Set.osd\3BA_Breasts" or "Breasts.bsd" (separators as written)
};

struct ProjectSlider {
  std::string name;
  bool invert = false, zap = false, clamp = false, uv = false, hidden = false;
  float default_small = 0.0f;  // percent, like the XML (GenWeights sets: "small"/"big"; otherwise "default")
  float default_big = 0.0f;
  std::vector<ProjectSliderData> data;
};

struct ProjectShape {
  std::string name;                       // NIF shape name
  std::string target;                     // slider data target (defaults to the name)
  std::vector<std::string> data_folders;  // ShapeData folders searched for its non-local data
  // Normal recalculation of a build (SliderSetShape): SmoothSeamNormals (default true) and LockNormals (default false).
  bool smooth_seam_normals = true;
  bool lock_normals = false;
};

struct SliderProject {
  std::string name;
  std::string file;          // the .osp it came from (virtual Data path)
  std::string data_folder;   // ShapeData\<data_folder>
  std::string source_file;   // base NIF inside the data folder
  std::string output_path;   // "meshes\armor\x"
  std::string output_file;   // "shirt" -> shirt_0.nif / shirt_1.nif (GenWeights) or shirt.nif
  bool gen_weights = true;
  bool keep_zapped_shapes = false;  // <OutputFile KeepZappedShapes>: a fully zapped shape is hidden instead of deleted
  bool prevent_morph_file = false;  // <OutputFile PreventMorphFile>: «Build Morphs» writes no TRI (and no BODYTRI) for this set
  std::vector<ProjectShape> shapes;
  std::vector<ProjectSlider> sliders;

  // Data-relative key of the built mesh ("meshes\armor\x\shirt_1.nif"); variant -1 = without weight suffix.
  [[nodiscard]] std::string output_mesh(int variant) const;
  [[nodiscard]] std::string source_mesh() const;  // "CalienteTools\BodySlide\ShapeData\<folder>\<source>"
  [[nodiscard]] const ProjectShape* shape_for_target(std::string_view target) const;
  [[nodiscard]] const ProjectShape* shape(std::string_view shape_name) const;
};

inline constexpr std::string_view kShapeDataDir = "CalienteTools\\BodySlide\\ShapeData";

// Every <SliderSet> of an .osp document (version 0 tags "BaseShapeName"/"SetFolder"/"datafile" are accepted like BodySlide).
std::vector<SliderProject> parse_slider_projects(std::string_view xml_text, std::string_view virtual_file);

// ---------------------------------------------------------------- slider values
// The value a preset gives a slider at an NPC weight (0..100): small at 0, big at 100, linear in between, as a fraction
// (percent / 100). A preset that sets only one size uses it for both. nullopt when the preset does not name the slider.
std::optional<float> preset_value(const SliderSetting& setting, float weight);
const SliderSetting* find_setting(const std::vector<SliderSetting>& preset, std::string_view slider);

// ---------------------------------------------------------------- application
struct SliderApplied {
  std::string slider;
  float value = 0.0f;              // what the offsets were multiplied by
  std::size_t vertices = 0;        // vertices the morph moves
  float max_displacement = 0.0f;   // largest offset length after scaling, Skyrim units
  bool inverted = false;           // BodySlide "invert" slider (only known when an .osp was consulted)
};

struct ShapeMorphReport {
  std::string shape;
  std::size_t vertex_count = 0;
  std::size_t morphs_available = 0;   // position morphs the data offers for this shape
  std::size_t preset_sliders = 0;     // sliders the preset names
  std::size_t compatible = 0;         // preset sliders that have a morph for this shape
  std::vector<SliderApplied> applied;           // non-zero values actually applied
  std::vector<std::string> not_in_data;         // preset sliders (non-zero value) with no morph for this shape
  std::vector<std::string> notes;               // uv/zap/clamp sliders skipped, inverted sliders, etc.
  std::string error;                            // the data does not fit the mesh (index past the last vertex): nothing applied
  float max_displacement = 0.0f;
  [[nodiscard]] bool changed() const { return error.empty() && !applied.empty(); }
};

// RaceMenu BodyMorph, as Body Change NG drives it in game: each TRI morph of the shape times the preset value at the NPC
// weight (`weight` 0..100). Offsets are mapped into the space of `shape.positions` with shape.to_root (rotation and scale).
// Normals are rotated with the change of the geometric normals; tangents are rebuilt by the caller when it needs them.
// Vertex indices past the shape's vertex count mean the TRI belongs to another mesh: nothing is applied and `error` says so.
ShapeMorphReport apply_tri_morphs(nif::Shape& shape, const TriShape& morphs, const std::vector<SliderSetting>& preset, float weight);

// How much of a preset's (non-zero) slider vocabulary a shape's morph data covers. Real 3BA and UBE TRIs share 3 of ~150-200
// slider names, so a low ratio is evidence of a different body family, decided from the files rather than from mesh names.
inline constexpr float kMinFamilyCoverage = 0.25f;
struct SliderCoverage {
  std::size_t considered = 0;  // non-zero preset sliders
  std::size_t found = 0;       // ... with a morph for the shape
  float ratio = 1.0f;          // found / considered (1 when nothing is considered)
};
SliderCoverage slider_coverage(const ShapeMorphReport& report);

// Two morph vocabularies (e.g. the NPC's body TRI and an outfit's TRI): same family when most outfit sliders exist on the body.
struct VocabularyMatch {
  std::size_t body = 0, outfit = 0, shared = 0;
  float ratio = 0.0f;       // shared / outfit
  std::string verdict;      // "compatible" (>= 0.6), "partial" (>= kMinFamilyCoverage), "incompatible", "unknown" (no data)
  std::string reason;       // Spanish, with the numbers
};
VocabularyMatch compare_vocabulary(const std::vector<std::string>& body_sliders, const std::vector<std::string>& outfit_sliders);
std::vector<std::string> morph_names(const TriFile& tri);  // every position morph name of every shape, sorted and unique

// One slider's data as BodySlide loads it for a project (data name -> target + offsets).
struct DiffSet {
  std::string target;
  std::vector<VertexDelta> offsets;
};
using DiffSets = std::map<std::string, DiffSet>;

// Reads every data file the project's sliders name, like SliderSet::LoadSetDiffData: .bsd files and entries of .osd files in
// ShapeData\<folder>\..., searching the shape's data folders (or the set's folder for local data). `read` returns a Data-relative
// file's bytes or nullopt. Missing files are listed in `missing` and their sliders simply contribute nothing, as in BodySlide.
struct ProjectData {
  DiffSets sets;
  std::vector<std::string> missing;
  std::vector<std::string> errors;
};
using DataReader = std::function<std::optional<std::string>(const std::string& data_path)>;
ProjectData load_project_data(const SliderProject& project, const DataReader& read);

// BodySlide's build of one shape: BodySlideApp::ApplySliders over the project's sliders, values from the preset (or the
// project's defaults for sliders the preset does not set), `big` = the _1 (weight 100) build, else _0. Inverted sliders use
// 1 - value; zap sliders with a value > 0 collect their vertex indices in `zapped` (not removed here); UV sliders are skipped;
// clamp sliders overwrite positions. Offsets are mapped with shape.to_root like apply_tri_morphs.
struct BuildReport {
  ShapeMorphReport report;
  std::vector<std::uint16_t> zapped;
};
BuildReport apply_project_sliders(nif::Shape& shape, const SliderProject& project, const DiffSets& data,
                                  const std::vector<SliderSetting>& preset, bool big);

// Every slider set of the profile (CalienteTools\BodySlide\SliderSets\*.osp, winning copies), indexed by the meshes it builds.
struct ProjectIndex {
  std::vector<SliderProject> projects;
  std::multimap<std::string, std::size_t> by_output;  // data_path_key of "<OutputPath>\<OutputFile>[_0|_1].nif" -> project
  std::vector<std::string> errors;                     // unreadable .osp files
  [[nodiscard]] std::vector<const SliderProject*> for_mesh(std::string_view data_path) const;
};
inline constexpr std::string_view kSliderSetsDir = "CalienteTools\\BodySlide\\SliderSets";
ProjectIndex build_project_index(std::vector<SliderProject> projects);
ProjectIndex load_project_index(const mo2::VirtualFileSystem& vfs);

// Is a built mesh the base RaceMenu morphs expect? BodyMorph adds each TRI morph to the mesh in game, so the mesh must be built
// without a preset (the project's defaults or all sliders at zero, "Zeroed Sliders"); a mesh built with a preset gets the preset
// twice. Decided by rebuilding both references from the project's own data and comparing vertex by vertex (1e-3 units).
struct BaseBuildCheck {
  std::string mesh;
  std::string project;
  std::string osp;
  // "base"            matches a build without preset (BodyMorph adds the preset correctly)
  // "preset-baked"    differs from both references: built with a preset (stale for BodyMorph; rebuild with Zeroed Sliders)
  // "no-project"      no slider set of the profile builds this mesh (cannot be checked)
  // "not-comparable"  shapes or vertex counts differ from the project's source (another version of the outfit)
  // "error"           unreadable data
  std::string verdict;
  std::string reference;      // "defaults" | "zeroed" when base
  float max_distance = 0.0f;  // against the closest reference
  std::size_t shapes_compared = 0;
  std::vector<std::string> missing_data;
  std::string detail;  // Spanish
};
BaseBuildCheck check_base_build(const SliderProject& project, const DataReader& read, const nif::MeshFile& built, std::string_view mesh_path);

// Largest and mean distance between two shapes vertex by vertex (same count required); used to compare a mesh against a
// reference build. nullopt when the vertex counts differ.
struct ShapeDistance {
  float max = 0.0f;
  float mean = 0.0f;
  std::size_t over_tolerance = 0;
};
std::optional<ShapeDistance> compare_positions(const std::vector<nif::Vec3>& a, const std::vector<nif::Vec3>& b, float tolerance);

}  // namespace sns::body
