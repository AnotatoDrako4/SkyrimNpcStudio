#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sns/appearance/appearance_draft.hpp"
#include "sns/face/face_recipe.hpp"
#include "sns/outfit/outfit_draft.hpp"
#include "sns/outfit/outfit_presets.hpp"

namespace sns::appearance {

enum class DiagnosticSeverity {
  Info,
  Warning,
  Error
};

enum class DiagnosticComponent {
  Face,
  Wardrobe,
  Body,
  Project
};

struct DiagnosticIssue {
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Info;
  DiagnosticComponent component = DiagnosticComponent::Project;
  std::string message;
  std::string target;
  std::string suggested_action;

  [[nodiscard]] std::string severity_string() const {
    switch (severity) {
      case DiagnosticSeverity::Error: return "error";
      case DiagnosticSeverity::Warning: return "warning";
      case DiagnosticSeverity::Info: return "info";
    }
    return "info";
  }

  [[nodiscard]] std::string component_string() const {
    switch (component) {
      case DiagnosticComponent::Face: return "face";
      case DiagnosticComponent::Wardrobe: return "wardrobe";
      case DiagnosticComponent::Body: return "body";
      case DiagnosticComponent::Project: return "project";
    }
    return "general";
  }
};

enum class HealthBadge {
  Verified,           // "Verificado (Compatible)"
  ExportableWarning,  // "Exportable con Advertencias"
  BlockingError       // "Error Bloqueante"
};

enum class DetectedBodyType {
  Unknown,
  Vanilla,
  CBBE,
  ThreeBA,
  UNP,
  HIMBO,
  UBE
};

struct BodyInspectionResult {
  DetectedBodyType body_type = DetectedBodyType::Unknown;
  std::string body_name = "Vanilla / No detectado";
  bool supports_weight_sliders = true;
  bool physics_enabled = false;
  std::string details;
};

struct DiagnosticReport {
  HealthBadge badge = HealthBadge::Verified;
  std::string summary = "Verificado: Sin problemas detectados";
  bool can_export = true;
  std::vector<DiagnosticIssue> issues;
  BodyInspectionResult body_inspection;

  [[nodiscard]] int error_count() const {
    int count = 0;
    for (const auto& i : issues) {
      if (i.severity == DiagnosticSeverity::Error) ++count;
    }
    return count;
  }

  [[nodiscard]] int warning_count() const {
    int count = 0;
    for (const auto& i : issues) {
      if (i.severity == DiagnosticSeverity::Warning) ++count;
    }
    return count;
  }
};

class CompatibilityGuardian {
 public:
  // Detects active body system from active plugins or virtual file paths.
  [[nodiscard]] static BodyInspectionResult inspect_body_system(
      const std::vector<std::string>& active_plugins,
      const std::vector<std::string>& active_file_paths,
      bool is_female);

  // Evaluates complete draft integrity (Face, Wardrobe, Body, and Project constraints).
  [[nodiscard]] static DiagnosticReport evaluate(
      const AppearanceDraft& draft,
      const std::optional<face::FaceRecipe>& recipe = std::nullopt,
      const std::vector<std::string>& active_plugins = {},
      const std::vector<std::string>& active_file_paths = {});
};

}  // namespace sns::appearance
