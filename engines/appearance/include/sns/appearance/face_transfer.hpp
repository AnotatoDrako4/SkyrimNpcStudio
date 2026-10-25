#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "sns/appearance/source_draft.hpp"
#include "sns/appearance/source_matrix.hpp"

// Assisted face transfer: «Elegir NPC -> Examinar rostros -> Comparar -> Seleccionar -> Validar -> Crear parche».
// Given the NPC's Appearance Source Matrix and the user's draft, says what giving the NPC one source's face takes and
// whether the studio can do it. It never invents a way: the only patch it can lead to is the existing assisted
// resolution («Usar el rostro de X»: the NPC override takes the face data of the plugin whose FaceGen the game loads,
// with its D1 review), which only exists for the source the game loads. A FaceGen that does not win in MO2 cannot be
// made to win by a plugin (the file path is the same for every mod): that takes a load order change the studio
// does not make. A source without its own FaceGen would need a FaceGen the studio cannot generate.
//
// Pure: the studio supplies the matrix and the draft state.
namespace sns::appearance {

enum class TransferVerdict {
  InUse,           // this is the face the game loads and its record fits it: nothing to do
  PatchReady,      // the assisted resolution can write a patch that makes the record match this face
  NeedsReview,     // as PatchReady, but the case is ambiguous: a person must approve it (D1) before export
  NeedsLoadOrder,  // this mod's FaceGen exists but does not win in MO2: the user must change MO2, not a plugin
  Blocked,         // cannot be transferred safely (no FaceGen, damaged file, conflict without a resolution)
};
[[nodiscard]] std::string_view to_string(TransferVerdict verdict);

struct TransferPlan {
  std::string candidate_id;
  TransferVerdict verdict = TransferVerdict::Blocked;
  // The draft is fresh and prefers this candidate (step «Seleccionar» done).
  bool chosen = false;
  // «Crear parche» may start now: chosen, and PatchReady or NeedsReview (the review gate stays in the export path).
  bool can_create_patch = false;
  // "REQUIERE_CREATION_KIT" when only a FaceGen the studio cannot build would make it work; "" otherwise.
  std::string requirement;
  std::vector<std::string> reasons;  // Spanish: what was found
  std::vector<std::string> actions;  // Spanish: what the user does next, in order
};

// `draft_chosen`: the candidate the NPC's draft prefers ("" when none). An unknown candidate is Blocked.
[[nodiscard]] TransferPlan plan_face_transfer(const SourceMatrix& matrix, std::string_view candidate_id, DraftState draft,
                                              std::string_view draft_chosen);

}  // namespace sns::appearance
