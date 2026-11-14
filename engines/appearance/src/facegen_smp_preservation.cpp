#include "sns/appearance/facegen_smp_preservation.hpp"

#include <cstdio>
#include <sstream>

namespace sns::appearance {

FacegenSmpPreservationReport audit_facegen_smp_preservation(const std::vector<AuditSmpFaceGenHair>& smp_shapes) {
  FacegenSmpPreservationReport report;
  report.intact = true;

  if (smp_shapes.empty()) {
    report.summary = "Sin formas de cabello SMP registradas en FaceGen.";
    return report;
  }

  for (const auto& shape : smp_shapes) {
    // 1. Check if source mesh had physics XML but FaceGen stripped NiStringExtraData
    if (!shape.source_physics_xml.empty() && !shape.facegen_has_physics_xml) {
      report.intact = false;
      FacegenSmpPreservationIssue issue;
      issue.code = "HAIR_SMP_003";
      issue.severity = "media";
      issue.shape_name = shape.shape;
      issue.message = "FaceGen perdió la ruta de físicas XML (" + shape.source_physics_xml +
                      ") declarada en la malla original (" + shape.source_mesh + ").";
      issue.cause = "Creation Kit descarta metadatos NiStringExtraData durante la exportación de FaceGen.";
      issue.suggestion = "Restaurar la ruta XML en el FaceGen mediante la herramienta smp-facegen.";
      report.issues.push_back(std::move(issue));
    }

    // 2. Check if bone rest poses were displaced beyond tolerance (0.5 game units)
    if (shape.max_bone_offset.has_value() && *shape.max_bone_offset > kSmpBoneTolerance) {
      report.intact = false;
      char offset_buf[32];
      std::snprintf(offset_buf, sizeof(offset_buf), "%.2f", *shape.max_bone_offset);
      FacegenSmpPreservationIssue issue;
      issue.code = "HAIR_SMP_004";
      issue.severity = "alta";
      issue.shape_name = shape.shape;
      issue.message = "Huesos de cabello SMP en FaceGen alterados respecto a la pose de reposo original: desviación de " +
                      std::string(offset_buf) + " unidades.";
      issue.cause = "Creation Kit alteró la jerarquía de huesos o matrices de transformación rest pose al exportar.";
      issue.suggestion = "Transferir la pose de reposo de la malla original con smp-facegen para evitar cabello desplazado.";
      report.issues.push_back(std::move(issue));
    }

    // 3. Check if FaceGen has bones missing from the source NIF
    if (!shape.bones_missing_in_source.empty()) {
      report.intact = false;
      FacegenSmpPreservationIssue issue;
      issue.code = "HAIR_SMP_005";
      issue.severity = "media";
      issue.shape_name = shape.shape;
      std::ostringstream ss;
      for (size_t i = 0; i < shape.bones_missing_in_source.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << shape.bones_missing_in_source[i];
      }
      issue.message = "FaceGen contiene huesos SMP ausentes en la malla original: " + ss.str() + ".";
      issue.cause = "FaceGen generado con una variante distinta de malla de peinado.";
      issue.suggestion = "Alinear la versión del NIF de origen con la utilizada para generar el FaceGen.";
      report.issues.push_back(std::move(issue));
    }
  }

  if (report.intact) {
    report.summary = "Todas las formas de cabello SMP en FaceGen preservan físicas, XML y jerarquías óseas.";
  } else {
    report.summary = "Se detectaron alteraciones o pérdidas de datos SMP en FaceGen que requieren corrección.";
  }

  return report;
}

}  // namespace sns::appearance
