#include "sns/appearance/hair_graph.hpp"
#include "sns/appearance/npc_audit.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace sns::appearance {
namespace {

bool iequals(const std::string& a, const char* b) {
  const std::string bs(b);
  return a.size() == bs.size() &&
         std::equal(a.begin(), a.end(), bs.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

bool icontains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [](char ch1, char ch2) {
                          return std::tolower(static_cast<unsigned char>(ch1)) ==
                                 std::tolower(static_cast<unsigned char>(ch2));
                        });
  return it != haystack.end();
}

}  // namespace

bool HairAssignmentGraph::has_active_hair() const {
  for (const auto& node : nodes) {
    if (!node.suppressed_by_equipment && !node.model_path.empty()) return true;
  }
  return false;
}

const HairResourceNode* HairAssignmentGraph::primary_hair() const {
  // Wigs in the Hair slot take precedence over HeadParts in game rendering
  for (const auto& node : nodes) {
    if ((node.source_kind == HairSourceKind::Wig || node.source_kind == HairSourceKind::WornEquipment) &&
        !node.suppressed_by_equipment) {
      return &node;
    }
  }
  // Next check winning headpart
  for (const auto& node : nodes) {
    if (node.source_kind == HairSourceKind::HeadPartWinning && !node.suppressed_by_equipment) return &node;
  }
  // Dynamic assignments
  for (const auto& node : nodes) {
    if ((node.source_kind == HairSourceKind::Hdf || node.source_kind == HairSourceKind::Spid) &&
        !node.suppressed_by_equipment) return &node;
  }
  // Fall back to original headpart or facegen
  for (const auto& node : nodes) {
    if (!node.model_path.empty() && !node.suppressed_by_equipment) return &node;
  }
  return nullptr;
}

std::vector<const HairResourceNode*> HairAssignmentGraph::active_wigs() const {
  std::vector<const HairResourceNode*> out;
  for (const auto& node : nodes) {
    if ((node.source_kind == HairSourceKind::Wig || (node.slot_mask & (1u << 31)) != 0) &&
        !node.suppressed_by_equipment) {
      out.push_back(&node);
    }
  }
  return out;
}

std::vector<const HairResourceNode*> HairAssignmentGraph::dynamic_assignments() const {
  std::vector<const HairResourceNode*> out;
  for (const auto& node : nodes) {
    if (node.stage == AssignmentStage::PlannedDynamic || node.source_kind == HairSourceKind::Hdf ||
        node.source_kind == HairSourceKind::Spid || node.source_kind == HairSourceKind::OtherDistributor) {
      out.push_back(&node);
    }
  }
  return out;
}

bool HairAssignmentGraph::has_smp() const {
  for (const auto& node : nodes) {
    if (node.requires_fsmp || !node.physics_xml_path.empty() || !node.bones.empty()) return true;
  }
  return false;
}

HairAssignmentGraph build_hair_assignment_graph(const AuditSubject& s,
                                              const std::string& effective_race,
                                              const std::string& body_family) {
  HairAssignmentGraph g;
  g.npc_form_key = s.form_key;
  g.npc_editor_id = s.editor_id;
  g.effective_race = effective_race;
  g.body_family = body_family;
  g.is_ube = icontains(effective_race, "ube") || icontains(body_family, "ube");

  // Check wigs first: if any wig has no addons for race, note suppression
  bool has_blocking_wig = false;
  std::string blocking_wig_name;
  for (const auto& w : s.hair_armors) {
    HairResourceNode wn;
    wn.form_key = w.form_key;
    wn.editor_id = w.editor_id;
    wn.source_kind = HairSourceKind::Wig;
    wn.stage = AssignmentStage::StaticRecord;
    wn.origin_plugin = w.plugin;
    wn.winning_plugin = w.plugin;
    wn.record_mod = w.record_mod;
    wn.effective_race = effective_race;
    wn.body_family = body_family;
    wn.slot_mask = (1u << 31);
    wn.requires_fsmp = w.smp;
    if (!w.meshes.empty()) {
      wn.model_path = w.meshes.front().path;
      wn.mesh_source = w.meshes.front();
    }
    if (!w.physics.empty()) {
      wn.physics_xml_path = w.physics.front().path;
      wn.physics_xml_source = w.physics.front();
      wn.requires_fsmp = true;
    }
    if (w.addons_for_race.has_value() && *w.addons_for_race == 0) {
      wn.suppressed_by_equipment = true;
      wn.suppressing_equipment = "Sin modelos ARMA para raza (" + effective_race + ")";
      has_blocking_wig = true;
      blocking_wig_name = w.editor_id.empty() ? w.form_key : w.editor_id;
    }
    g.nodes.push_back(std::move(wn));
  }

  // HeadParts from winning record
  for (const auto& p : s.head_parts) {
    const bool is_hair = iequals(p.type, "Hair");
    if (!is_hair && !p.type.empty()) continue;

    HairResourceNode n;
    n.form_key = p.form_key;
    n.editor_id = p.editor_id;
    n.source_kind = (p.plugin == s.defining_plugin) ? HairSourceKind::HeadPartOriginal : HairSourceKind::HeadPartWinning;
    n.stage = AssignmentStage::StaticRecord;
    n.origin_plugin = s.defining_plugin;
    n.winning_plugin = p.plugin;
    n.record_mod = p.record_mod;
    n.effective_race = effective_race;
    n.body_family = body_family;
    n.model_path = p.model_path;
    n.mesh_source = p.mesh;
    n.slot_mask = (1u << 31);

    for (const auto& t : p.textures) {
      n.texture_paths.push_back(t.path);
      n.texture_sources.push_back(t);
    }

    if (has_blocking_wig) {
      n.suppressed_by_equipment = true;
      n.suppressing_equipment = "Peluca sin modelo: " + blocking_wig_name;
    }

    // Check SMP FaceGen information
    for (const auto& h : s.smp_facegen) {
      if (h.shape == p.editor_id || h.source_mesh == p.model_path) {
        n.physics_xml_path = h.source_physics_xml;
        n.physics_xml_source = h.physics_xml;
        n.bones = h.physics_bones;
        n.requires_fsmp = !h.source_physics_xml.empty() || !h.physics_bones.empty();
      }
    }

    g.nodes.push_back(std::move(n));
  }

  return g;
}

std::string describe_hair_graph(const HairAssignmentGraph& graph) {
  std::ostringstream ss;
  ss << "NPC " << graph.npc_editor_id << " (" << graph.npc_form_key << ") [Raza: " << graph.effective_race
     << ", Cuerpo: " << graph.body_family << "]: ";
  if (graph.nodes.empty()) {
    ss << "sin nodos de cabello.";
    return ss.str();
  }
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    if (i > 0) ss << " | ";
    const auto& n = graph.nodes[i];
    ss << "[" << (n.editor_id.empty() ? n.form_key : n.editor_id) << " via ";
    switch (n.source_kind) {
      case HairSourceKind::HeadPartOriginal: ss << "HDPT-Origen"; break;
      case HairSourceKind::HeadPartWinning: ss << "HDPT-Ganador"; break;
      case HairSourceKind::FaceGen: ss << "FaceGen"; break;
      case HairSourceKind::WornEquipment: ss << "ARMO-Equipado"; break;
      case HairSourceKind::Wig: ss << "Peluca"; break;
      case HairSourceKind::Spid: ss << "SPID"; break;
      case HairSourceKind::Hdf: ss << "HDF"; break;
      case HairSourceKind::OtherDistributor: ss << "Distribuidor"; break;
    }
    if (n.requires_fsmp || !n.physics_xml_path.empty()) ss << " (SMP)";
    if (n.suppressed_by_equipment) ss << " (SUPRIMIDO por " << n.suppressing_equipment << ")";
    ss << "]";
  }
  return ss.str();
}

}  // namespace sns::appearance
