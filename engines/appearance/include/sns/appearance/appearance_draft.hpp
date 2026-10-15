#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sns/face/face_recipe.hpp"
#include "sns/outfit/outfit_draft.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"

namespace sns::appearance {

// The face of the NPC being edited, in the terms the export writes: NPC_ record values (NAM9,
// NAMA, weight, QNAM, hair color, tints), the PNAM head part list, and the RaceMenu-only data
// (custom morphs, sculpt) that is baked into the regenerated FaceGen.
struct FaceState {
  face::FaceValues values;
  std::vector<std::string> head_parts;  // PNAM, FormKeys ("051193:Skyrim.esm")
  std::string head_texture;             // FTST face texture set FormKey ("" = race default)
  std::string source;                   // provenance for the UI: "", "RaceMenu: Carrot.jslot", "Donante: ..."
  // "regenerate" or "preserve"; "pending" while the profile's FaceGen has not been analysed yet
  // (AppearanceEditor accepts no edit and starts no build in that state).
  std::string geometry_mode = "regenerate";

  bool operator==(const FaceState&) const = default;
};
inline constexpr const char* kGeometryPending = "pending";

// The data a face state writes to the NPC_ record and bakes into the FaceGen: values (NAM9, NAMA,
// weight, QNAM, HCLF, tints, RaceMenu morphs and sculpt), PNAM and FTST. `source` is provenance only.
[[nodiscard]] bool same_face_record(const FaceState& a, const FaceState& b);
// Only the hair color (HCLF) differs between the record data of the two states, with the same
// geometry mode.
[[nodiscard]] bool only_hair_color_differs(const FaceState& baseline, const FaceState& state);

// What the FaceGen pipeline must do for `state` compared with the record (`baseline`).
enum class FaceBuildKind {
  Unchanged,        // same record data and geometry mode: nothing to build or export
  WaitForAnalysis,  // the profile's FaceGen is still being analysed: build nothing yet
  HairColorSafe,    // only HCLF changed: recolor the winning FaceGen (Hair Color Safe Mode), never rebuild
  Full,             // other data changed (or the user chose another geometry mode): build_facegen
};
[[nodiscard]] FaceBuildKind face_build_kind(const FaceState& baseline, const FaceState& state);

// One draft for everything the studio edits on an NPC: face, body height and outfit. Face and outfit
// changes go through the same command stack (shared undo/redo), so the project, the comparison
// and the export always see one consistent state.
class AppearanceDraft {
 public:
  [[nodiscard]] const projects::NpcTarget& target() const noexcept { return target_; }
  void set_target(projects::NpcTarget target);

  [[nodiscard]] const outfit::OutfitDraft& outfit() const noexcept { return outfit_; }
  [[nodiscard]] outfit::OutfitDraft& outfit() noexcept { return outfit_; }
  [[nodiscard]] const std::vector<outfit::Piece>& initial_outfit() const noexcept { return initial_outfit_; }
  void set_initial_outfit(std::vector<outfit::Piece> pieces);

  // The face as the active load order defines it (from face.getRecipe). Until it is set the face
  // cannot be edited or exported.
  void set_face_baseline(FaceState baseline);
  [[nodiscard]] bool face_loaded() const noexcept { return face_loaded_; }
  [[nodiscard]] const FaceState& face() const noexcept { return face_; }
  [[nodiscard]] const FaceState& face_baseline() const noexcept { return face_baseline_; }
  void set_face(FaceState state) { face_ = std::move(state); }  // used by commands
  // The geometry mode is decided after loading (by comparing the existing FaceGen with a
  // regeneration). It updates the baseline, and the current face too unless the user changed it.
  void set_baseline_geometry_mode(const std::string& mode) {
    if (face_.geometry_mode == face_baseline_.geometry_mode) face_.geometry_mode = mode;
    face_baseline_.geometry_mode = mode;
  }

  [[nodiscard]] float height() const noexcept { return height_; }
  [[nodiscard]] float height_baseline() const noexcept { return height_baseline_; }
  void set_height_baseline(float height);
  void set_height(float height) { height_ = height; }

  [[nodiscard]] bool is_face_dirty() const noexcept { return face_loaded_ && face_ != face_baseline_; }
  [[nodiscard]] bool is_height_dirty() const noexcept { return height_ != height_baseline_; }
  [[nodiscard]] bool is_outfit_dirty() const noexcept { return outfit_.pieces() != initial_outfit_; }
  [[nodiscard]] bool is_dirty() const noexcept { return is_face_dirty() || is_height_dirty() || is_outfit_dirty(); }

  // Project mapping. The face is stored complete (format 3) only when it differs from the record.
  [[nodiscard]] projects::NpcEdit to_npc_edit() const;
  void reset();

 private:
  projects::NpcTarget target_;
  outfit::OutfitDraft outfit_;
  std::vector<outfit::Piece> initial_outfit_;
  FaceState face_;
  FaceState face_baseline_;
  bool face_loaded_ = false;
  float height_ = 1.0f;
  float height_baseline_ = 1.0f;
};

// FaceConfig (project file) <-> FaceState. A complete (format 3) config replaces the baseline; a
// format 2 config is applied on top of it: listed sliders, head parts replaced by type (needs the
// baseline recipe to know the types), colors and tints. Format 2 weight is ignored unless the
// config came from a donor, because format 2 always wrote the default 100 (see docs/PROJECT_FORMAT.md).
projects::FaceConfig to_face_config(const FaceState& state, const face::FaceRecipe& recipe);
FaceState from_face_config(const projects::FaceConfig& config, const FaceState& baseline, const face::FaceRecipe& recipe,
                           std::vector<std::string>* notes = nullptr);

// Face values and PNAM as the worker's recipe describes the record.
FaceState face_state_from_recipe(const face::FaceRecipe& recipe);

// Undoable edits. `merge_key` lets a continuous gesture (dragging one slider) become one step:
// consecutive commands with the same non-empty key are merged by the command stack.
std::unique_ptr<projects::Command> make_set_face(AppearanceDraft& draft, FaceState next, std::string description,
                                                 std::string merge_key = {});
std::unique_ptr<projects::Command> make_set_height(AppearanceDraft& draft, float height);

}  // namespace sns::appearance
