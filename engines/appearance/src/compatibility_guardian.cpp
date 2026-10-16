#include "sns/appearance/compatibility_guardian.hpp"

#include <algorithm>
#include <string_view>

namespace sns::appearance {

namespace {

bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [](char ch1, char ch2) {
                          return std::tolower(static_cast<unsigned char>(ch1)) ==
                                 std::tolower(static_cast<unsigned char>(ch2));
                        });
  return it != haystack.end();
}

}  // namespace

BodyInspectionResult CompatibilityGuardian::inspect_body_system(
    const std::vector<std::string>& active_plugins,
    const std::vector<std::string>& active_file_paths,
    bool is_female) {
  BodyInspectionResult result;

  auto has_plugin = [&](std::string_view name) {
    for (const auto& p : active_plugins) {
      if (contains_ignore_case(p, name)) return true;
    }
    return false;
  };

  auto has_path = [&](std::string_view fragment) {
    for (const auto& path : active_file_paths) {
      if (contains_ignore_case(path, fragment)) return true;
    }
    return false;
  };

  // Physics check across paths
  for (const auto& path : active_file_paths) {
    if (contains_ignore_case(path, "smp") || contains_ignore_case(path, "hdt") ||
        contains_ignore_case(path, "cbpc") || contains_ignore_case(path, "physics")) {
      result.physics_enabled = true;
      break;
    }
  }

  if (is_female) {
    if (has_plugin("3BA") || has_path("3BA") || has_path("3BBB")) {
      result.body_type = DetectedBodyType::ThreeBA;
      result.body_name = "CBBE 3BA (3BBB Extended)";
      result.supports_weight_sliders = true;
      result.physics_enabled = true;
      result.details = "Malla de cuerpo con soporte para sliders de peso y física SMP/CBPC.";
      return result;
    }
    if (has_plugin("CalienteBeautifulBodiesEnhancer") || has_plugin("CBBE") ||
        has_path("femalebody_cbbe") || has_path("BodySlide")) {
      result.body_type = DetectedBodyType::CBBE;
      result.body_name = "CBBE (Caliente's Beautiful Bodies Enhancer)";
      result.supports_weight_sliders = true;
      result.details = "Malla CBBE estándar con soporte de peso _0 y _1.";
      return result;
    }
    if (has_plugin("UNP") || has_path("UNP") || has_path("dimon945")) {
      result.body_type = DetectedBodyType::UNP;
      result.body_name = "UNP (Unified UNP Body)";
      result.supports_weight_sliders = true;
      result.details = "Malla base UNP estándar.";
      return result;
    }
  } else {
    if (has_plugin("HIMBO") || has_path("HIMBO") || has_path("malebody_himbo")) {
      result.body_type = DetectedBodyType::HIMBO;
      result.body_name = "HIMBO (Highly Improved Male Body Overhaul)";
      result.supports_weight_sliders = true;
      result.details = "Malla masculina con deformación por peso y soporte de sliders.";
      return result;
    }
  }

  result.body_type = DetectedBodyType::Vanilla;
  result.body_name = "Vanilla Bethesda Standard";
  result.supports_weight_sliders = true;
  result.details = "Malla por defecto del juego base.";
  return result;
}

DiagnosticReport CompatibilityGuardian::evaluate(
    const AppearanceDraft& draft,
    const std::optional<face::FaceRecipe>& recipe,
    const std::vector<std::string>& active_plugins,
    const std::vector<std::string>& active_file_paths) {
  DiagnosticReport report;

  // 1. Target check
  if (draft.target().form_key.empty()) {
    report.issues.push_back({
        .code = "NoTargetNpc",
        .severity = DiagnosticSeverity::Error,
        .component = DiagnosticComponent::Project,
        .message = "No se ha seleccionado ningún NPC de destino.",
        .target = "",
        .suggested_action = "Selecciona un NPC del catálogo para comenzar a personalizar.",
    });
  }

  // 2. Face Integrity
  if (!draft.face_loaded()) {
    report.issues.push_back({
        .code = "FaceNotLoaded",
        .severity = DiagnosticSeverity::Warning,
        .component = DiagnosticComponent::Face,
        .message = "La línea base facial no ha sido cargada desde el worker.",
        .target = draft.target().form_key,
        .suggested_action = "Espera a que el worker resuelva el registro o refresca el personaje.",
    });
  } else {
    const auto& face = draft.face();
    if (face.head_parts.empty()) {
      report.issues.push_back({
          .code = "MissingHeadParts",
          .severity = DiagnosticSeverity::Error,
          .component = DiagnosticComponent::Face,
          .message = "El personaje no tiene partes de la cabeza asignadas (cabello, ojos, rostro).",
          .target = draft.target().form_key,
          .suggested_action = "Asegúrate de que el NPC conserve al menos su rostro y cabello base.",
      });
    }

    if (draft.is_face_dirty()) {
      report.issues.push_back({
          .code = "FaceGenRegenerationPending",
          .severity = DiagnosticSeverity::Info,
          .component = DiagnosticComponent::Face,
          .message = "Se detectaron cambios faciales. Se empaquetará FaceGen NIF/DDS generado para garantizar fidelidad y evitar dark face.",
          .target = draft.target().form_key,
          .suggested_action = "Al exportar, el estudio generará y verificará la geometría de cabeza.",
      });
    }

    if (recipe.has_value()) {
      if (recipe->vampire) {
        report.issues.push_back({
            .code = "VampireMorphRace",
            .severity = DiagnosticSeverity::Info,
            .component = DiagnosticComponent::Face,
            .message = "Este NPC pertenece a una raza vampírica con raza de morph '" + recipe->morph_race_editor_id + "'.",
            .target = draft.target().form_key,
            .suggested_action = "Los morphs faciales se proyectarán en concordancia con la raza base del vampiro.",
        });
      }
    }
  }

  // 3. Wardrobe Integrity
  auto conflicts = draft.outfit().conflicts();
  for (const auto& conflict : conflicts) {
    report.issues.push_back({
        .code = "SlotConflict",
        .severity = DiagnosticSeverity::Error,
        .component = DiagnosticComponent::Wardrobe,
        .message = "Conflicto de biped slot en " + outfit::slot_display_name(conflict.slot) +
                   " (" + std::to_string(conflict.slot) + ") entre '" +
                   conflict.first + "' y '" + conflict.second + "'.",
        .target = conflict.first,
        .suggested_action = "Retira una de las dos prendas para evitar que una desequipe a la otra en Skyrim.",
    });
  }

  if (draft.outfit().pieces().empty()) {
    report.issues.push_back({
        .code = "EmptyOutfit",
        .severity = DiagnosticSeverity::Info,
        .component = DiagnosticComponent::Wardrobe,
        .message = "El atuendo no tiene piezas equipadas. El NPC utilizará únicamente su piel por defecto.",
        .target = draft.target().form_key,
        .suggested_action = "Agrega prendas desde el catálogo de armaduras o aplica un Outfit Preset.",
    });
  }

  // Check pieces for material recolors
  for (const auto& piece : draft.outfit().pieces()) {
    if (piece.material.has_value()) {
      report.issues.push_back({
          .code = "MaterialVariantPreviewOnly",
          .severity = DiagnosticSeverity::Error,
          .component = DiagnosticComponent::Wardrobe,
          .message = "La pieza '" + (piece.name.empty() ? piece.form_key : piece.name) + "' tiene un material solo previsualizable; aún no se generan DDS y referencias NIF verificadas.",
          .target = piece.form_key,
          .suggested_action = "Restaura el material original antes de exportar.",
      });
    }
  }

  // 4. Body System Inspection
  bool is_female = true;
  if (recipe.has_value()) {
    is_female = recipe->female;
  }
  report.body_inspection = inspect_body_system(active_plugins, active_file_paths, is_female);

  // 5. Project & Save Game Persistence Notice
  if (draft.is_dirty()) {
    report.issues.push_back({
        .code = "SaveGamePersistence",
        .severity = DiagnosticSeverity::Info,
        .component = DiagnosticComponent::Project,
        .message = "En partidas guardadas previas, Skyrim puede mantener el atuendo cargado hasta reiniciar la celda (30 días en el juego o 'resurrect').",
        .target = draft.target().form_key,
        .suggested_action = "Para ver el cambio de inmediato en un save en curso, usa la consola de Skyrim: prid <FormID> y resurrect.",
    });
  }

  // 6. Overall Evaluation & Health Badge
  if (report.error_count() > 0) {
    report.badge = HealthBadge::BlockingError;
    report.can_export = false;
    report.summary = "Error Bloqueante: Se encontraron conflictos que impiden la exportación limpia.";
  } else if (report.warning_count() > 0) {
    report.badge = HealthBadge::ExportableWarning;
    report.can_export = true;
    report.summary = "Exportable con Advertencias: El mod puede compilarse pero revisa las sugerencias.";
  } else {
    report.badge = HealthBadge::Verified;
    report.can_export = true;
    // Preliminary check (no inventory of the winning body meshes): never a claim of in-game compatibility.
    report.summary = "Sin errores en la comprobación preliminar. No sustituye la prueba dentro de Skyrim.";
  }

  return report;
}

}  // namespace sns::appearance
