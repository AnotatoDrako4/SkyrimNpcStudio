#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sns/assets/asset_resolver.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::outfit {

enum class MeshFamily {
  Unknown,
  Vanilla,
  Cbbe,
  Cbbe3ba,
  Bhunp,
  Unp,
  Ube,
  Other
};

enum class ConfidenceLevel {
  Indeterminate,
  ProbableMetadata,
  ConfirmedGeometric,
  IncompatibleConfirmed
};

enum class DiagnosticSeverity {
  Info,
  Warning,
  Error
};

enum class DiagnosticCode {
  BodyOutfitFamilyMismatch,
  OutfitMorphMissing,
  OutfitWeightVariantMissing,
  OutfitTopologyMismatch,
  OutfitSkeletonMismatch,
  OutfitSlotConflict,
  OutfitMeshMissing,
  OutfitWrongProvider,
  BodyPartCovered,
  OutfitNeedsRefit,
  OutfitFaceNeckMismatch
};

struct FitIssue {
  DiagnosticCode code = DiagnosticCode::BodyOutfitFamilyMismatch;
  DiagnosticSeverity severity = DiagnosticSeverity::Info;
  std::string file_path;      // Real NIF or asset path
  std::string provider;       // Winning MO2 mod or provider
  std::string evidence;       // Concrete proof
  std::string possible_fix;   // Actionable resolution

  [[nodiscard]] std::string code_string() const;
  [[nodiscard]] std::string severity_string() const;
};

struct GeometricEvidence {
  std::size_t vertex_count = 0;
  std::size_t triangle_count = 0;
  std::vector<std::string> shape_names;
  std::vector<std::string> bones_used;
  std::vector<std::string> physics_bones;
  std::vector<int> biped_slots;
  std::vector<std::string> textures;
  bool has_skinning = false;
  bool has_body_tri = false;
  std::string body_tri_path;
  bool sibling_tri_exists = false;
  std::string sibling_tri_path;
  bool has_weight_0 = false;
  bool has_weight_1 = false;
  std::vector<std::string> hidden_or_collision_shapes;
};

struct CompatibleAlternative {
  std::string resource_id;       // "<mod>:<path>"
  std::string mod_name;
  std::string data_path;
  MeshFamily family = MeshFamily::Unknown;
  ConfidenceLevel confidence = ConfidenceLevel::Indeterminate;
  bool has_tri = false;
  bool has_weight_variants = false;
  std::string bodyslide_project; // If available in ShapeData
  std::string description;
};

struct PieceFitDiagnosis {
  std::string armor_form_key;
  std::string armor_name;
  std::string addon_form_key;
  std::string slot_description;
  std::vector<int> biped_slots;
  std::uint32_t slot_mask = 0;

  std::string effective_nif_path;
  std::string winning_provider;
  bool is_game_data = false;
  bool is_overwrite = false;

  MeshFamily detected_family = MeshFamily::Unknown;
  ConfidenceLevel confidence = ConfidenceLevel::Indeterminate;
  std::string family_reason;

  GeometricEvidence geometry;

  bool topology_compatible = true;
  bool morphs_available = false;
  bool weight_variants_valid = false;
  bool biped_partitions_coherent = true;
  bool skeleton_compatible = true;
  bool is_covered_by_other_piece = false;
  std::string covered_by_piece;

  std::vector<FitIssue> issues;
  std::vector<CompatibleAlternative> compatible_alternatives;
};

struct BodyFitDiagnosis {
  std::string form_key;
  std::string name;
  std::string sex;
  std::string race;
  MeshFamily body_family = MeshFamily::Unknown;
  ConfidenceLevel confidence = ConfidenceLevel::Indeterminate;
  std::string body_provider;
  std::string body_mesh_path;
  std::string hands_mesh_path;
  std::string feet_mesh_path;
  std::string head_mesh_path;
  std::string skeleton_path;
  bool body_has_tri = false;
  std::vector<std::string> physics_bones;
};

struct OutfitFitReport {
  BodyFitDiagnosis body;
  std::string outfit_form_key;
  std::string outfit_name;
  std::vector<PieceFitDiagnosis> pieces;
  std::vector<FitIssue> global_issues;
  std::size_t total_issues_count = 0;
  std::size_t incompatible_pieces_count = 0;
  std::size_t fixable_alternatives_count = 0;
  bool needs_bodyslide_refit = false;
};

struct PieceRepairPatch {
  std::string armor_form_key;
  std::string addon_form_key;
  std::string original_nif;
  std::string repaired_nif;
  std::string source_mod;
};

class OutfitFitDoctor {
 public:
  static OutfitFitReport analyze(
      const mo2::VirtualFileSystem& vfs,
      const nlohmann::json& npc_details,
      const assets::AssetResolver* resolver = nullptr);

  static std::vector<CompatibleAlternative> find_alternatives(
      const mo2::VirtualFileSystem& vfs,
      const PieceFitDiagnosis& piece,
      MeshFamily target_family,
      const assets::AssetResolver* resolver = nullptr,
      const std::vector<std::string>* cached_meshes_files = nullptr,
      const std::vector<std::string>* cached_bs_files = nullptr);

  static bool export_doctor_patch(
      const mo2::VirtualFileSystem& vfs,
      const OutfitFitReport& report,
      const std::vector<PieceRepairPatch>& patches,
      const std::filesystem::path& target_mod_dir);

  static nlohmann::json to_json(const OutfitFitReport& report);
  static OutfitFitReport from_json(const nlohmann::json& j);

  static std::string family_to_string(MeshFamily f);
  static MeshFamily family_from_string(std::string_view s);

  static std::string confidence_to_string(ConfidenceLevel c);
  static ConfidenceLevel confidence_from_string(std::string_view s);

  static std::string diagnostic_code_to_string(DiagnosticCode c);
  static DiagnosticCode diagnostic_code_from_string(std::string_view s);

  static std::string severity_to_string(DiagnosticSeverity s);
  static DiagnosticSeverity severity_from_string(std::string_view s);
};

}  // namespace sns::outfit
