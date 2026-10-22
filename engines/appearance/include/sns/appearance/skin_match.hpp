#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Head <-> body skin match of one NPC: is the head's skin (its FaceGen tint over the base head) about the color of the body, the hands
// and the feet (skin diffuse with the skin tone)? The classic failure is a visible seam at the neck. This is an ESTIMATE from the median
// colors of the textures the studio composited, in CIE76 delta E; it is not what Skyrim draws (lighting, shaders and the game's own
// blend are not reproduced) and says so: every result carries "ESTIMATE" and "UNVERIFIED_IN_SKYRIM". Pure: the caller measures.
namespace sns::appearance {

enum class SkinVerdict {
  Consistent,   // every measured part is within kConsistentMax of the head: no seam expected
  Noticeable,   // the worst part differs by a visible amount (a seam can show in good light)
  LikelySeam,   // the worst part differs clearly: a visible seam at the neck is likely
  Unknown,      // the head or every body part could not be measured (missing texture, unreadable file)
};
[[nodiscard]] std::string_view to_string(SkinVerdict verdict);

inline constexpr double kConsistentMax = 6.0;  // delta E 76 up to which two skins read as the same color
inline constexpr double kNoticeableMax = 12.0; // beyond it the difference is plain

struct SkinSample {
  std::string role;                 // "head", "body", "hands", "feet" or "other" (see skin_role_of_slots)
  std::string label;                // for the report: the piece / mesh it was measured on
  std::array<float, 3> rgb{};       // median color, sRGB 0..1
};

struct SkinPairDelta {
  std::string role;
  std::string label;
  double delta_e = 0.0;
};

struct SkinMatch {
  SkinVerdict verdict = SkinVerdict::Unknown;
  std::vector<SkinPairDelta> pairs;  // head against each body part, in the order given
  double worst = 0.0;
  std::string worst_role;
  std::string message;               // Spanish, one sentence
  std::vector<std::string> codes;    // HeadNotMeasured, BodyNotMeasured, SeamLikely, SeamPossible
  std::vector<std::string> caveats;  // always ESTIMATE and UNVERIFIED_IN_SKYRIM
};

// "body" for slot 32, "hands" for 33 (and the forearms, 34), "feet" for 37, "head" for 30 (a bit of the slot mask counts; the first of this
// order wins), else "other" (hair, circlet, amulet...).
[[nodiscard]] std::string skin_role_of_slots(std::uint32_t slot_mask);

// CIE76 delta E between two sRGB colors (0..1), D65.
[[nodiscard]] double delta_e76(const std::array<float, 3>& a, const std::array<float, 3>& b);

// The head sample is the first one with role "head". Samples of role "other" are listed in the report but never decide the verdict.
[[nodiscard]] SkinMatch evaluate_skin_match(const std::vector<SkinSample>& samples);

}  // namespace sns::appearance
