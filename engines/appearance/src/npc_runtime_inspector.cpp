#include "sns/appearance/npc_runtime_inspector.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

namespace sns::appearance {

std::string correlation_state_to_string(CorrelationState state) {
  switch (state) {
    case CorrelationState::StaticValid: return "STATIC_VALID";
    case CorrelationState::RuntimeAssetMissing: return "RUNTIME_ASSET_MISSING";
    case CorrelationState::RuntimeGeometryMissing: return "RUNTIME_GEOMETRY_MISSING";
    case CorrelationState::SmpDependencyError: return "SMP_DEPENDENCY_ERROR";
    case CorrelationState::DynamicAssignmentSuspected: return "DYNAMIC_ASSIGNMENT_SUSPECTED";
    case CorrelationState::VisualFailureConfirmed: return "VISUAL_FAILURE_CONFIRMED";
    case CorrelationState::Inconclusive: return "INCONCLUSIVE";
    case CorrelationState::ValidatedInSkyrim: return "VALIDATED_IN_SKYRIM";
  }
  return "UNKNOWN";
}

std::optional<NpcRuntimeReport> parse_npc_runtime_report(std::string_view json_str) {
  try {
    const auto j = nlohmann::json::parse(json_str);
    if (!j.is_object()) return std::nullopt;

    NpcRuntimeReport report;
    report.schema_version = j.value("schemaVersion", 1);
    report.capture_timestamp_utc = j.value("captureTimestampUtc", "");
    report.game_version = j.value("gameVersion", "");
    report.skse_version = j.value("skseVersion", "");
    report.profile = j.value("profile", "");

    if (j.contains("npc") && j["npc"].is_object()) {
      const auto& n = j["npc"];
      report.base_form_key = n.value("baseFormKey", "");
      report.reference_form_id = n.value("referenceFormId", "");
      report.editor_id = n.value("editorId", "");
      report.name = n.value("name", "");
      report.effective_race = n.value("effectiveRace", "");
      report.origin_plugin = n.value("originPlugin", "");
      report.winning_plugin = n.value("winningPlugin", "");
    }

    if (j.contains("hair") && j["hair"].is_object()) {
      const auto& h = j["hair"];
      report.hair.assigned_source = h.value("assignedSource", "UNKNOWN");
      report.hair.head_part_form_key = h.value("headPartFormKey", "");
      report.hair.facegen_geometry_observed = h.value("faceGenGeometryObserved", false);
      report.hair.facegen_hair_shape_name = h.value("faceGenHairShapeName", "");
      if (h.contains("wornWigFormKey") && !h["wornWigFormKey"].is_null()) {
        report.hair.worn_wig_form_key = h["wornWigFormKey"].get<std::string>();
      }
      if (h.contains("wornWigSlot") && !h["wornWigSlot"].is_null()) {
        report.hair.worn_wig_slot = h["wornWigSlot"].get<uint32_t>();
      }
      report.hair.smp_active = h.value("smpActive", false);
      report.hair.smp_xml_path = h.value("smpXmlPath", "");
      report.hair.visual_status = h.value("visualStatus", "UNKNOWN");
    }

    if (j.contains("headNodes") && j["headNodes"].is_array()) {
      for (const auto& item : j["headNodes"]) {
        NpcRuntimeNode node;
        node.name = item.value("name", "");
        node.attached = item.value("attached", false);
        node.visible = item.value("visible", false);
        if (item.contains("bones") && item["bones"].is_array()) {
          node.bones = item["bones"].get<std::vector<std::string>>();
        }
        report.head_nodes.push_back(std::move(node));
      }
    }

    if (j.contains("equippedItems") && j["equippedItems"].is_array()) {
      for (const auto& item : j["equippedItems"]) {
        NpcRuntimeEquippedItem eq;
        eq.slot = item.value("slot", 0u);
        eq.form_key = item.value("formKey", "");
        eq.name = item.value("name", "");
        eq.model_path = item.value("modelPath", "");
        eq.is_wig = item.value("isWig", false);
        eq.has_addon_for_race = item.value("hasAddonForRace", true);
        report.equipped_items.push_back(std::move(eq));
      }
    }

    if (j.contains("diagnostics") && j["diagnostics"].is_array()) {
      report.diagnostics = j["diagnostics"].get<std::vector<std::string>>();
    }
    if (j.contains("evidence") && j["evidence"].is_array()) {
      report.evidence = j["evidence"].get<std::vector<std::string>>();
    }

    return report;
  } catch (...) {
    return std::nullopt;
  }
}

std::string serialize_npc_runtime_report(const NpcRuntimeReport& report) {
  nlohmann::json j;
  j["schemaVersion"] = report.schema_version;
  j["captureTimestampUtc"] = report.capture_timestamp_utc;
  j["gameVersion"] = report.game_version;
  j["skseVersion"] = report.skse_version;
  j["profile"] = report.profile;

  j["npc"] = {
      {"baseFormKey", report.base_form_key},
      {"referenceFormId", report.reference_form_id},
      {"editorId", report.editor_id},
      {"name", report.name},
      {"effectiveRace", report.effective_race},
      {"originPlugin", report.origin_plugin},
      {"winningPlugin", report.winning_plugin},
  };

  nlohmann::json hj;
  hj["assignedSource"] = report.hair.assigned_source;
  hj["headPartFormKey"] = report.hair.head_part_form_key;
  hj["faceGenGeometryObserved"] = report.hair.facegen_geometry_observed;
  hj["faceGenHairShapeName"] = report.hair.facegen_hair_shape_name;
  if (report.hair.worn_wig_form_key) {
    hj["wornWigFormKey"] = *report.hair.worn_wig_form_key;
  } else {
    hj["wornWigFormKey"] = nullptr;
  }
  if (report.hair.worn_wig_slot) {
    hj["wornWigSlot"] = *report.hair.worn_wig_slot;
  } else {
    hj["wornWigSlot"] = nullptr;
  }
  hj["smpActive"] = report.hair.smp_active;
  hj["smpXmlPath"] = report.hair.smp_xml_path;
  hj["visualStatus"] = report.hair.visual_status;
  j["hair"] = hj;

  nlohmann::json nodes = nlohmann::json::array();
  for (const auto& node : report.head_nodes) {
    nodes.push_back({
        {"name", node.name},
        {"attached", node.attached},
        {"visible", node.visible},
        {"bones", node.bones},
    });
  }
  j["headNodes"] = nodes;

  nlohmann::json eq_list = nlohmann::json::array();
  for (const auto& eq : report.equipped_items) {
    eq_list.push_back({
        {"slot", eq.slot},
        {"formKey", eq.form_key},
        {"name", eq.name},
        {"modelPath", eq.model_path},
        {"isWig", eq.is_wig},
        {"hasAddonForRace", eq.has_addon_for_race},
    });
  }
  j["equippedItems"] = eq_list;

  j["diagnostics"] = report.diagnostics;
  j["evidence"] = report.evidence;

  return j.dump(2);
}

RuntimeCorrelationReport correlate_static_and_runtime(const NpcAudit& static_audit, const NpcRuntimeReport& runtime_report) {
  RuntimeCorrelationReport r;
  r.npc_form_key = static_audit.form_key.empty() ? runtime_report.base_form_key : static_audit.form_key;

  // Verify form key match if both present
  if (!static_audit.form_key.empty() && !runtime_report.base_form_key.empty() &&
      static_audit.form_key != runtime_report.base_form_key) {
    r.state = CorrelationState::Inconclusive;
    r.discrepancies.push_back("FormKey estático (" + static_audit.form_key + ") difiere de runtime (" + runtime_report.base_form_key + ").");
    r.explanation = "Los datos estáticos y de runtime corresponden a NPC distintos.";
    return r;
  }

  const bool is_bald_at_runtime = (runtime_report.hair.visual_status == "BALD") ||
                                  (!runtime_report.hair.facegen_geometry_observed &&
                                   !runtime_report.hair.worn_wig_form_key &&
                                   runtime_report.head_nodes.empty());

  if (is_bald_at_runtime) {
    r.state = CorrelationState::VisualFailureConfirmed;

    // Check if static audit marked it clean!
    if (static_audit.status == kStatusClean) {
      r.false_negative_detected = true;

      // Identify the exact failure cause
      if (runtime_report.hair.worn_wig_form_key.has_value()) {
        r.false_negative_reason =
            "Falso negativo por equipamiento en slot de cabello: el auditor analizó únicamente HeadParts/FaceGen estáticos "
            "(que eran válidos), ignorando que la peluca equipada (" + *runtime_report.hair.worn_wig_form_key +
            ") en el slot 31 oculta el cabello del registro y carece de addon ARMA para la raza " + runtime_report.effective_race + ".";
        r.suggested_actions.push_back("Instalar o crear el parche de raza/cuerpo (ej. UBE) para la peluca " + *runtime_report.hair.worn_wig_form_key + ".");
      } else if (runtime_report.hair.smp_active && !runtime_report.hair.smp_xml_path.empty()) {
        r.false_negative_reason =
            "Falso negativo por física SMP rota: la malla del cabello requiere físicas XML (" + runtime_report.hair.smp_xml_path +
            ") pero FSMP no pudo cargar las dependencias en runtime, provocando colapso de la geometría.";
        r.suggested_actions.push_back("Corregir la ruta XML de físicas o regenerar FaceGen preservando jerarquía ósea.");
      } else {
        r.false_negative_reason =
            "Falso negativo: el NPC aparece calvo en Skyrim a pesar de que el registro y FaceGen parecen válidos en el análisis estático.";
        r.suggested_actions.push_back("Inspeccionar distribuciones dinámicas SPID/HDF y verificar shaders de cabello en el visor.");
      }

      r.explanation = "DISCORDANCIA GRAVE: El auditor estático dio 'SIN_ERROR_DETECTADO', pero en Skyrim el NPC está calvo.";
    } else {
      r.false_negative_detected = false;
      r.explanation = "Fallo visual confirmado por observación en Skyrim y respaldado por inconsistencias en el auditor estático.";
    }
    return r;
  }

  if (runtime_report.hair.visual_status == "VISIBLE_PHYSICS" || runtime_report.hair.visual_status == "VALIDATED_IN_SKYRIM") {
    r.state = CorrelationState::ValidatedInSkyrim;
    if (static_audit.status != kStatusClean) {
      r.discrepancies.push_back("El auditor estático emitió alertas sospechosas pero el cabello se renderiza y simula con éxito en Skyrim.");
    }
    r.explanation = "Validado dentro de Skyrim: el peinado cuenta con geometría visible y físicas operativas.";
    return r;
  }

  if (runtime_report.hair.visual_status == "VISIBLE_STATIC") {
    if (runtime_report.hair.smp_active) {
      r.state = CorrelationState::SmpDependencyError;
      r.discrepancies.push_back("El cabello es visible pero las físicas SMP no responden en runtime (cabello rígido).");
      r.explanation = "Geometría renderizada pero físicas inactivas: posible fallo en DLL de FSMP o XML no parseado.";
      r.suggested_actions.push_back("Revisar hdtSMP64.log y validar compatibilidad de versiones de Skyrim y SKSE.");
    } else {
      r.state = CorrelationState::StaticValid;
      r.explanation = "Peinado estático convencional renderizado sin errores en Skyrim.";
    }
    return r;
  }

  r.state = CorrelationState::Inconclusive;
  r.explanation = "Estado en tiempo de ejecución inconcluso o pendiente de confirmación visual.";
  return r;
}

std::vector<std::string> generate_ingame_inspection_commands(const std::string& runtime_form_id) {
  std::vector<std::string> cmds;
  if (runtime_form_id.empty()) return cmds;

  // 1. Target the NPC reference
  cmds.push_back("prid " + runtime_form_id);
  // 2. Query inventory to inspect equipped wigs and head items
  cmds.push_back("showinventory");
  // 3. Dump headparts and race data
  cmds.push_back("smp reset");
  // 4. Print confirmation to console log
  cmds.push_back("echo SNS_INSPECT: " + runtime_form_id);
  return cmds;
}

}  // namespace sns::appearance
