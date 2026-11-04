// Shared data types, enums, anatomical zones, and JSON contracts for the Skyrim NPC Studio Visual QA Lab.
// Used by the clipping, seam, morph, visual diff, and regression analyzers without requiring Qt.
// Output reports are written to out/visual-qa/<run-id>/ with full provenance and SHA-256 hashes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/nif/mesh.hpp"
#include "sns/rendering/scene.hpp"

namespace sns::visual_qa {

// Four-state verdict required by the Visual QA Lab specification (never fabricate PASS when data is missing).
enum class QaVerdict {
  Pass,          // Check passed with concrete geometric/visual evidence.
  Fail,          // Defect reproduced and traced to specific meshes/shapes.
  Skip,          // Required data, resources, or profile conditions are absent.
  Inconclusive   // Data was analyzed, but evidence is insufficient to confirm or rule out a visual defect.
};

[[nodiscard]] std::string_view to_string(QaVerdict verdict);
[[nodiscard]] QaVerdict combine_verdicts(QaVerdict a, QaVerdict b);

// Anatomical zones in Y-up view space (where Y = Skyrim Z height, X = -Skyrim X left/right, Z = Skyrim Y front/back).
enum class AnatomicalZone {
  Head,
  Neck,
  ChestBreasts,
  UpperBack,
  WaistAbdomen,
  PelvisGroin,
  ButtGlutes,
  ShoulderUpperArmLeft,
  ShoulderUpperArmRight,
  ForearmWristLeft,
  ForearmWristRight,
  HandLeft,
  HandRight,
  ThighLeft,
  ThighRight,
  CalfAnkleLeft,
  CalfAnkleRight,
  FootLeft,
  FootRight,
  CloakOrAccessory
};

[[nodiscard]] std::string_view to_string(AnatomicalZone zone);

// Classifies a 3D point in Y-up view space into an anatomical region relative to the character's vertical span.
[[nodiscard]] AnatomicalZone classify_anatomical_zone(const nif::Vec3& view_pos, float char_min_y = 0.0f,
                                                      float char_max_y = 124.0f);

// Role of a RenderPart in the scene so hidden helpers, SMP proxies, and covered skin are separated from visible geometry.
enum class SurfaceRole {
  VisibleBodySkin,        // Uncovered body/hands/feet from group "skin" with shown_by_default == true.
  EmbeddedBodyInOutfit,   // Body/hand/foot reference shape embedded inside an equipped outfit NIF (shown_by_default == true).
  VisibleHeadSkin,        // FaceGen main head skin mesh (shown_by_default == true).
  VisibleGarment,         // Equipped visible clothing/armor shape (shown_by_default == true).
  VisibleHeadPart,        // Hair, eyes, brows, mouth, horns (shown_by_default == true).
  CoveredBodyBySlot,      // Skin addon hidden because an equipped outfit piece occupies its Biped slot.
  HiddenNifShape,         // NIF Hidden flag or no shader property (e.g. VirtualGround, TopC, PantsC).
  SmpCollisionProxy,      // Untextured SMP collision body referenced in physics XML.
  UnequippedOutfitPiece   // Outfit piece displaced by a later piece sharing the same Biped slot.
};

[[nodiscard]] std::string_view to_string(SurfaceRole role);
[[nodiscard]] SurfaceRole classify_part_role(const rendering::RenderPart& part);

// Four-level distinction for intersections plus explicit false-positive exclusion categories.
enum class ClippingClassification {
  GeometricIntersectionDetected,   // Triangles intersect, but depth is tiny or along a border/seam.
  PossibleVisibleClipping,         // Visible body and garment intersect with moderate outward protrusion.
  ConfirmedVisualClipping,         // Visible body clearly protrudes outside the exterior surface of a visible garment.
  Indeterminate,                   // Alpha-blended, double-sided, or ambiguous normal orientation.
  ExcludedCoveredByBipedSlot,      // Body part is hidden in-game by Biped slot rules.
  ExcludedHiddenNifShape,          // Shape is hidden by NIF flags or lacks a shader.
  ExcludedSmpCollision,            // Shape is an SMP physics collision proxy.
  ExcludedIntentionalOverlap,      // Layered sub-shapes of the same garment or intentional accessory overlap.
  ExcludedReferencePoseOnly        // Crease intersection localized to a limb joint in the static reference pose.
};

[[nodiscard]] std::string_view to_string(ClippingClassification cls);

// Traceable QA issue linked back to the originating mesh, shape, armor FormKey, and MO2 provider.
struct VisualQaIssue {
  std::string severity;        // "error", "warning", "info"
  std::string code;            // Machine-readable issue code (e.g. "ConfirmedVisualClipping", "NeckSeamGap")
  std::string category;        // "clipping", "seam", "morph", "fit", "weight", "family", "visibility", "capture"
  std::string message;         // Plain-language explanation of the finding and why it was flagged
  std::string mesh_path;       // Data-relative NIF path
  std::string shape_name;      // NIF shape name
  std::string piece_label;     // Outfit/skin piece name
  std::string armor_form_key;  // ARMO FormKey if applicable
  std::string addon_form_key;  // ARMA FormKey if applicable
  std::string provider;        // MO2 mod / BSA provider
  std::string zone;            // Anatomical zone name
};

[[nodiscard]] nlohmann::json to_json(const VisualQaIssue& issue);

}  // namespace sns::visual_qa
