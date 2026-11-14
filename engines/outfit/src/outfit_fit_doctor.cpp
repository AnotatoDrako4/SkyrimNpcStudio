#include "sns/outfit/outfit_fit_doctor.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "sns/core/error.hpp"
#include "sns/outfit/outfit_draft.hpp"
#include "sns/core/text.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/nif/safe_load.hpp"

namespace sns::outfit {

namespace {

std::string to_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

std::string normalize_vfs_path(std::string_view p) {
  std::string out;
  out.reserve(p.size());
  for (char c : p) {
    if (c == '/') out.push_back('\\');
    else out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  // Strip leading backslashes or "data\"
  while (out.starts_with('\\')) out.erase(0, 1);
  if (out.starts_with("data\\")) out.erase(0, 5);
  return out;
}

bool contains_token_icase(std::string_view haystack, std::string_view needle) {
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

std::string read_file_bytes(const std::filesystem::path& abs_path) {
  std::ifstream f(abs_path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string slot_name_for(int slot) {
  switch (slot) {
    case 30: return "30 (Head)";
    case 31: return "31 (Hair)";
    case 32: return "32 (Body)";
    case 33: return "33 (Hands)";
    case 34: return "34 (Forearms)";
    case 35: return "35 (Amulet)";
    case 36: return "36 (Ring)";
    case 37: return "37 (Feet)";
    case 38: return "38 (Calves)";
    case 39: return "39 (Shield)";
    case 40: return "40 (Tail)";
    case 41: return "41 (LongHair)";
    case 42: return "42 (Circlet)";
    case 43: return "43 (Ears)";
    case 44: return "44 (Face)";
    case 45: return "45 (Torso/Corset)";
    case 46: return "46 (Cape/Cloak)";
    case 47: return "47 (Backpack)";
    case 48: return "48 (Misc)";
    case 49: return "49 (Pelvis/Legs)";
    case 50: return "50 (DecapitateHead)";
    case 51: return "51 (Decapitate)";
    case 52: return "52 (PelvisUnder)";
    case 53: return "53 (LegsUnder)";
    case 54: return "54 (LegsUnder2)";
    case 55: return "55 (FaceGenHead)";
    case 56: return "56 (FaceGenEyes)";
    case 57: return "57 (FaceGenHair)";
    case 58: return "58 (FaceGenBeard)";
    case 59: return "59 (FaceGenMouth)";
    case 60: return "60 (Misc2)";
    case 61: return "61 (FX01)";
    default: return std::to_string(slot);
  }
}

}  // namespace

std::string FitIssue::code_string() const {
  return OutfitFitDoctor::diagnostic_code_to_string(code);
}

std::string FitIssue::severity_string() const {
  return OutfitFitDoctor::severity_to_string(severity);
}

std::string OutfitFitDoctor::family_to_string(MeshFamily f) {
  switch (f) {
    case MeshFamily::Cbbe3ba: return "CBBE 3BA";
    case MeshFamily::Cbbe: return "CBBE";
    case MeshFamily::Ube: return "UBE";
    case MeshFamily::Bhunp: return "BHUNP";
    case MeshFamily::Unp: return "UNP";
    case MeshFamily::Vanilla: return "Vanilla";
    case MeshFamily::Other: return "Other";
    case MeshFamily::Unknown:
    default: return "Unknown";
  }
}

MeshFamily OutfitFitDoctor::family_from_string(std::string_view s) {
  const auto lower = to_lower(s);
  if (lower.find("3ba") != std::string::npos || lower.find("3bbb") != std::string::npos) return MeshFamily::Cbbe3ba;
  if (lower.find("cbbe") != std::string::npos) return MeshFamily::Cbbe;
  if (lower.find("ube") != std::string::npos) return MeshFamily::Ube;
  if (lower.find("bhunp") != std::string::npos) return MeshFamily::Bhunp;
  if (lower.find("unp") != std::string::npos) return MeshFamily::Unp;
  if (lower.find("vanilla") != std::string::npos) return MeshFamily::Vanilla;
  if (lower.find("other") != std::string::npos) return MeshFamily::Other;
  return MeshFamily::Unknown;
}

std::string OutfitFitDoctor::confidence_to_string(ConfidenceLevel c) {
  switch (c) {
    case ConfidenceLevel::ConfirmedGeometric: return "confirmed-geometric";
    case ConfidenceLevel::ProbableMetadata: return "probable-metadata";
    case ConfidenceLevel::IncompatibleConfirmed: return "incompatible-confirmed";
    case ConfidenceLevel::Indeterminate:
    default: return "indeterminate";
  }
}

ConfidenceLevel OutfitFitDoctor::confidence_from_string(std::string_view s) {
  const auto lower = to_lower(s);
  if (lower == "confirmed-geometric") return ConfidenceLevel::ConfirmedGeometric;
  if (lower == "probable-metadata") return ConfidenceLevel::ProbableMetadata;
  if (lower == "incompatible-confirmed") return ConfidenceLevel::IncompatibleConfirmed;
  return ConfidenceLevel::Indeterminate;
}

std::string OutfitFitDoctor::diagnostic_code_to_string(DiagnosticCode c) {
  switch (c) {
    case DiagnosticCode::BodyOutfitFamilyMismatch: return "BodyOutfitFamilyMismatch";
    case DiagnosticCode::OutfitMorphMissing: return "OutfitMorphMissing";
    case DiagnosticCode::OutfitWeightVariantMissing: return "OutfitWeightVariantMissing";
    case DiagnosticCode::OutfitTopologyMismatch: return "OutfitTopologyMismatch";
    case DiagnosticCode::OutfitSkeletonMismatch: return "OutfitSkeletonMismatch";
    case DiagnosticCode::OutfitSlotConflict: return "OutfitSlotConflict";
    case DiagnosticCode::OutfitMeshMissing: return "OutfitMeshMissing";
    case DiagnosticCode::OutfitWrongProvider: return "OutfitWrongProvider";
    case DiagnosticCode::BodyPartCovered: return "BodyPartCovered";
    case DiagnosticCode::OutfitNeedsRefit: return "OutfitNeedsRefit";
    case DiagnosticCode::OutfitFaceNeckMismatch: return "OutfitFaceNeckMismatch";
  }
  return "UnknownIssue";
}

DiagnosticCode OutfitFitDoctor::diagnostic_code_from_string(std::string_view s) {
  if (s == "BodyOutfitFamilyMismatch") return DiagnosticCode::BodyOutfitFamilyMismatch;
  if (s == "OutfitMorphMissing") return DiagnosticCode::OutfitMorphMissing;
  if (s == "OutfitWeightVariantMissing") return DiagnosticCode::OutfitWeightVariantMissing;
  if (s == "OutfitTopologyMismatch") return DiagnosticCode::OutfitTopologyMismatch;
  if (s == "OutfitSkeletonMismatch") return DiagnosticCode::OutfitSkeletonMismatch;
  if (s == "OutfitSlotConflict") return DiagnosticCode::OutfitSlotConflict;
  if (s == "OutfitMeshMissing") return DiagnosticCode::OutfitMeshMissing;
  if (s == "OutfitWrongProvider") return DiagnosticCode::OutfitWrongProvider;
  if (s == "BodyPartCovered") return DiagnosticCode::BodyPartCovered;
  if (s == "OutfitNeedsRefit") return DiagnosticCode::OutfitNeedsRefit;
  if (s == "OutfitFaceNeckMismatch") return DiagnosticCode::OutfitFaceNeckMismatch;
  return DiagnosticCode::BodyOutfitFamilyMismatch;
}

std::string OutfitFitDoctor::severity_to_string(DiagnosticSeverity s) {
  switch (s) {
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Info:
    default: return "info";
  }
}

DiagnosticSeverity OutfitFitDoctor::severity_from_string(std::string_view s) {
  const auto lower = to_lower(s);
  if (lower == "error") return DiagnosticSeverity::Error;
  if (lower == "warning") return DiagnosticSeverity::Warning;
  return DiagnosticSeverity::Info;
}

static GeometricEvidence inspect_nif_geometry(
    const std::string& nif_bytes,
    const std::string& data_path,
    const mo2::VirtualFileSystem& vfs) {
  GeometricEvidence ev;
  if (nif_bytes.empty()) return ev;

  try {
    const auto mesh = nif::load_mesh(nif_bytes, data_path);
    std::unordered_set<std::string> unique_bones;

    for (const auto& sh : mesh.shapes) {
      ev.vertex_count += sh.positions.size();
      ev.triangle_count += sh.indices.size() / 3;
      ev.shape_names.push_back(sh.name);

      if (!sh.has_shader || sh.hidden) {
        ev.hidden_or_collision_shapes.push_back(sh.name);
      }

      if (sh.skinned) {
        ev.has_skinning = true;
        for (const auto& b : sh.bones) {
          if (unique_bones.insert(b).second) {
            ev.bones_used.push_back(b);
            const auto lower_b = to_lower(b);
            if (lower_b.find("breast") != std::string::npos || lower_b.find("butt") != std::string::npos ||
                lower_b.find("belly") != std::string::npos || lower_b.find("thigh") != std::string::npos) {
              ev.physics_bones.push_back(b);
            }
          }
        }
      }

      for (auto slot : sh.partitions) {
        if (std::find(ev.biped_slots.begin(), ev.biped_slots.end(), static_cast<int>(slot)) == ev.biped_slots.end()) {
          ev.biped_slots.push_back(static_cast<int>(slot));
        }
      }

      for (const auto& tex : sh.textures) {
        if (!tex.empty() && std::find(ev.textures.begin(), ev.textures.end(), tex) == ev.textures.end()) {
          ev.textures.push_back(tex);
        }
      }
    }
  } catch (...) {
    // If nifly fails to load full mesh, proceed with string extra data check
  }

  // Check extra string data for BODYTRI
  try {
    const auto extras = nif::string_extra_data(nif_bytes, data_path);
    for (const auto& ex : extras) {
      if (ex.name == "BODYTRI") {
        ev.has_body_tri = true;
        ev.body_tri_path = ex.value;
      }
    }
  } catch (...) {
  }

  // Check for sibling .tri in VFS
  const std::filesystem::path p(data_path);
  std::string tri_path = p.parent_path().string();
  if (!tri_path.empty()) tri_path += "\\";
  std::string stem = p.stem().string();
  if (stem.ends_with("_0") || stem.ends_with("_1")) {
    stem = stem.substr(0, stem.size() - 2);
  }
  tri_path += stem + ".tri";
  const auto tri_entry = vfs.resolve(normalize_vfs_path(tri_path));
  if (tri_entry) {
    ev.sibling_tri_exists = true;
    ev.sibling_tri_path = tri_path;
  }

  // Check for _0 / _1 weight counterparts
  const std::string lower = to_lower(data_path);
  if (lower.ends_with("_0.nif")) {
    ev.has_weight_0 = true;
    std::string other = data_path;
    other.replace(other.size() - 6, 6, "_1.nif");
    if (vfs.resolve(normalize_vfs_path(other))) ev.has_weight_1 = true;
  } else if (lower.ends_with("_1.nif")) {
    ev.has_weight_1 = true;
    std::string other = data_path;
    other.replace(other.size() - 6, 6, "_0.nif");
    if (vfs.resolve(normalize_vfs_path(other))) ev.has_weight_0 = true;
  } else {
    // Single static mesh
    ev.has_weight_0 = true;
    ev.has_weight_1 = true;
  }

  return ev;
}

static std::pair<MeshFamily, ConfidenceLevel> classify_family(
    const GeometricEvidence& geom,
    std::string_view data_path,
    std::string_view provider) {
  // 1. Shape names and physics bones: the strongest metadata hints, but names and bones are not a measurement (UBE bodies carry
  // breast/butt physics bones too; any author can name a shape "3BA"). The measured evidence is the scene's fidelity report
  // (garment fit, TRI vocabulary), so these stay ProbableMetadata.
  for (const auto& sh : geom.shape_names) {
    const auto lower = to_lower(sh);
    if (lower == "3ba" || lower == "3bbb" || lower.starts_with("3ba_") || lower.find("body [3ba]") != std::string::npos) {
      return {MeshFamily::Cbbe3ba, ConfidenceLevel::ProbableMetadata};
    }
    if (lower == "ubebody" || lower == "ube" || lower.starts_with("ube ")) {
      return {MeshFamily::Ube, ConfidenceLevel::ProbableMetadata};
    }
    if (lower == "bhunp" || lower.find("bhunp 3bbb") != std::string::npos) {
      return {MeshFamily::Bhunp, ConfidenceLevel::ProbableMetadata};
    }
    if (lower == "cbbe" || lower == "body [cbbe]") {
      return {MeshFamily::Cbbe, ConfidenceLevel::ProbableMetadata};
    }
    if (lower == "baseshape") {
      if (contains_token_icase(data_path, "cbbe")) {
        return {MeshFamily::Cbbe, ConfidenceLevel::ProbableMetadata};
      }
      return {MeshFamily::Unp, ConfidenceLevel::ProbableMetadata};
    }
  }

  if (!geom.physics_bones.empty()) {
    // Has breast/butt physics bones characteristic of 3BA or BHUNP
    if (contains_token_icase(data_path, "bhunp") || contains_token_icase(provider, "bhunp")) {
      return {MeshFamily::Bhunp, ConfidenceLevel::ProbableMetadata};
    }
    return {MeshFamily::Cbbe3ba, ConfidenceLevel::ProbableMetadata};
  }

  // 2. Probable Metadata: path or provider indicators
  const std::string meta = std::string(data_path) + " " + std::string(provider);
  if (contains_token_icase(meta, "3ba") || contains_token_icase(meta, "3bbb")) {
    return {MeshFamily::Cbbe3ba, ConfidenceLevel::ProbableMetadata};
  }
  if (contains_token_icase(meta, "ube")) {
    return {MeshFamily::Ube, ConfidenceLevel::ProbableMetadata};
  }
  if (contains_token_icase(meta, "bhunp")) {
    return {MeshFamily::Bhunp, ConfidenceLevel::ProbableMetadata};
  }
  if (contains_token_icase(meta, "cbbe")) {
    return {MeshFamily::Cbbe, ConfidenceLevel::ProbableMetadata};
  }
  if (contains_token_icase(meta, "unp") || contains_token_icase(meta, "steel") ||
      contains_token_icase(meta, "iron") || contains_token_icase(meta, "actors/character") ||
      contains_token_icase(meta, "actors\\character")) {
    return {MeshFamily::Unp, ConfidenceLevel::ProbableMetadata};
  }

  return {MeshFamily::Unknown, ConfidenceLevel::Indeterminate};
}

nlohmann::json OutfitFitDoctor::npc_details_from_appearance(const nlohmann::json& appearance, std::string_view npc_name) {
  const auto text = [](const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
  };
  const auto strip_meshes = [](std::string path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.starts_with("meshes\\") || lower.starts_with("meshes/")) path.erase(0, 7);
    return path;
  };
  const auto mask_of = [](const nlohmann::json& j) {
    const auto it = j.find("slotMask");
    if (it == j.end() || !it->is_number_integer()) return 0u;
    return static_cast<std::uint32_t>(it->get<std::int64_t>());
  };
  nlohmann::json details{{"formKey", text(appearance, "npcFormKey")},
                         {"name", std::string(npc_name)},
                         {"sex", text(appearance, "sex") == "male" ? "Male" : "Female"}};
  nlohmann::json skin = nlohmann::json::array();
  nlohmann::json armors = nlohmann::json::array();
  const auto pieces = appearance.find("pieces");
  if (pieces != appearance.end() && pieces->is_array()) {
    for (const auto& piece : *pieces) {
      const auto source = text(piece, "source");
      const auto addons = piece.find("addons");
      if (source == "skin") {
        if (addons == piece.end() || !addons->is_array()) continue;
        for (const auto& addon : *addons) {
          const auto mesh = text(addon, "meshPath");
          if (mesh.empty()) continue;
          skin.push_back({{"formKey", text(addon, "formKey")}, {"bodySlots", slots_of(mask_of(addon))}, {"femaleModel", strip_meshes(mesh)}});
        }
      } else if (source == "outfit") {
        nlohmann::json armor{{"formKey", text(piece, "armorFormKey")}, {"name", text(piece, "name")},
                             {"slots", slots_of(mask_of(piece))}, {"addonFormKey", ""}, {"femaleModel", ""}};
        if (addons != piece.end() && addons->is_array()) {
          for (const auto& addon : *addons) {
            const auto mesh = text(addon, "meshPath");
            if (mesh.empty()) continue;
            armor["addonFormKey"] = text(addon, "formKey");
            armor["femaleModel"] = strip_meshes(mesh);
            break;
          }
        }
        armors.push_back(std::move(armor));
      }
    }
  }
  details["skinAddons"] = std::move(skin);
  details["defaultOutfit"] = {{"formKey", text(appearance, "npcFormKey")}, {"outfit", std::string(npc_name)}, {"armors", std::move(armors)}};
  return details;
}

OutfitFitReport OutfitFitDoctor::analyze(
    const mo2::VirtualFileSystem& vfs,
    const nlohmann::json& npc_details,
    const assets::AssetResolver* resolver) {
  OutfitFitReport report;

  // Extract body info
  report.body.form_key = npc_details.value("formKey", "");
  report.body.name = npc_details.value("name", "");
  report.body.sex = npc_details.value("sex", "Female");
  report.body.race = npc_details.value("race", "");

  // Resolve body mesh paths
  std::string female_body_path;
  if (npc_details.contains("skinAddons") && npc_details["skinAddons"].is_array()) {
    for (const auto& a : npc_details["skinAddons"]) {
      const auto slots = a.value("bodySlots", std::vector<int>{});
      const std::string model = a.value("femaleModel", "");
      if (std::find(slots.begin(), slots.end(), 32) != slots.end() && !model.empty()) {
        female_body_path = model;
      }
      if (std::find(slots.begin(), slots.end(), 33) != slots.end() && !model.empty()) {
        report.body.hands_mesh_path = model;
      }
      if (std::find(slots.begin(), slots.end(), 37) != slots.end() && !model.empty()) {
        report.body.feet_mesh_path = model;
      }
    }
  }

  if (female_body_path.empty()) {
    // Default fallback
    female_body_path = "actors\\character\\character assets\\femalebody_1.nif";
  }
  report.body.body_mesh_path = female_body_path;

  // Analyze body mesh
  std::string norm_body_path = normalize_vfs_path("meshes\\" + female_body_path);
  const auto body_entry = vfs.resolve(norm_body_path);
  if (body_entry) {
    const auto& prov = vfs.providers()[body_entry->provider];
    report.body.body_provider = prov.name;
    std::string bytes;
    if (resolver) {
      const auto res = resolver->read(norm_body_path);
      if (res) bytes = *res;
    }
    if (bytes.empty() && !body_entry->absolute.empty()) {
      bytes = read_file_bytes(body_entry->absolute);
    }

    if (!bytes.empty()) {
      const auto geom = inspect_nif_geometry(bytes, norm_body_path, vfs);
      const auto [fam, conf] = classify_family(geom, norm_body_path, prov.name);
      report.body.body_family = fam;
      report.body.confidence = conf;
      report.body.body_has_tri = geom.has_body_tri || geom.sibling_tri_exists;
      report.body.physics_bones = geom.physics_bones;
    }
  }

  // Cache directory listings lazily across all pieces in this analysis
  std::vector<std::string> cached_meshes_files;
  bool meshes_cached = false;
  std::vector<std::string> cached_bs_files;
  bool bs_cached = false;

  // Extract outfit pieces
  if (npc_details.contains("defaultOutfit") && npc_details["defaultOutfit"].is_object()) {
    const auto& do_obj = npc_details["defaultOutfit"];
    report.outfit_form_key = do_obj.value("outfitFormKey", do_obj.value("formKey", ""));
    report.outfit_name = do_obj.value("outfit", do_obj.value("editorId", ""));

    if (do_obj.contains("armors") && do_obj["armors"].is_array()) {
      for (const auto& armo : do_obj["armors"]) {
        PieceFitDiagnosis piece;
        piece.armor_form_key = armo.value("formKey", "");
        piece.armor_name = armo.value("name", armo.value("editorId", ""));
        piece.addon_form_key = armo.value("addonFormKey", "");
        piece.winning_provider = armo.value("provider", armo.value("winningPlugin", ""));

        if (armo.contains("slots") && armo["slots"].is_array()) {
          for (const auto& s : armo["slots"]) {
            if (s.is_number()) {
              const int slot_id = s.get<int>();
              piece.biped_slots.push_back(slot_id);
              piece.slot_mask |= (1u << (slot_id - 30));
              if (!piece.slot_description.empty()) piece.slot_description += ", ";
              piece.slot_description += slot_name_for(slot_id);
            }
          }
        }

        std::string mesh_path = armo.value("femaleModel", "");
        if (mesh_path.empty() && armo.contains("meshPaths") && armo["meshPaths"].is_array() && !armo["meshPaths"].empty()) {
          mesh_path = armo["meshPaths"][0].get<std::string>();
        }
        if (mesh_path.empty()) {
          mesh_path = armo.value("model", "");
        }

        if (mesh_path.empty()) {
          // If no mesh path known, record issue
          FitIssue is;
          is.code = DiagnosticCode::OutfitMeshMissing;
          is.severity = DiagnosticSeverity::Error;
          is.file_path = "<Sin Malla Declarada>";
          is.provider = piece.winning_provider;
          is.evidence = "El registro ARMO/ARMA no especifica una malla femenina válida.";
          is.possible_fix = "Asignar un modelo ARMA válido o seleccionar una prenda con malla.";
          piece.issues.push_back(std::move(is));
          report.pieces.push_back(std::move(piece));
          continue;
        }

        piece.effective_nif_path = mesh_path;
        std::string norm_piece_path = normalize_vfs_path(mesh_path.starts_with("meshes\\") ? mesh_path : ("meshes\\" + mesh_path));
        const auto entry = vfs.resolve(norm_piece_path);

        if (!entry) {
          piece.issues.push_back(FitIssue{
              DiagnosticCode::OutfitMeshMissing,
              DiagnosticSeverity::Error,
              norm_piece_path,
              piece.winning_provider,
              std::format("El archivo «{}» no existe en la carpeta virtual Data de MO2.", norm_piece_path),
              "Reinstalar el mod proveedor o corregir la ruta del modelo."});
          report.pieces.push_back(std::move(piece));
          continue;
        }

        const auto& prov = vfs.providers()[entry->provider];
        piece.winning_provider = prov.name;
        piece.is_game_data = (prov.kind == mo2::ProviderKind::GameData);
        piece.is_overwrite = (prov.kind == mo2::ProviderKind::Overwrite);

        std::string bytes;
        if (resolver) {
          const auto res = resolver->read(norm_piece_path);
          if (res) bytes = *res;
        }
        if (bytes.empty() && !entry->absolute.empty()) {
          bytes = read_file_bytes(entry->absolute);
        }

        if (bytes.empty()) {
          piece.issues.push_back(FitIssue{
              DiagnosticCode::OutfitMeshMissing,
              DiagnosticSeverity::Error,
              norm_piece_path,
              prov.name,
              "No se pudieron leer los bytes de la malla desde el disco o archivo BSA.",
              "Verificar la integridad del archivo NIF."});
          report.pieces.push_back(std::move(piece));
          continue;
        }

        // Geometry Inspection
        piece.geometry = inspect_nif_geometry(bytes, norm_piece_path, vfs);
        const auto [fam, conf] = classify_family(piece.geometry, norm_piece_path, prov.name);
        piece.detected_family = fam;
        piece.confidence = conf;
        piece.family_reason = std::format("{} (confianza: {})", family_to_string(fam), confidence_to_string(conf));

        // Weight variants validation
        if (!piece.geometry.has_weight_0 || !piece.geometry.has_weight_1) {
          piece.weight_variants_valid = false;
          piece.issues.push_back(FitIssue{
              DiagnosticCode::OutfitWeightVariantMissing,
              DiagnosticSeverity::Warning,
              norm_piece_path,
              prov.name,
              std::format("La prenda carece del par completo _0/_1 (tiene _0: {}, tiene _1: {}).",
                          piece.geometry.has_weight_0 ? "sí" : "no", piece.geometry.has_weight_1 ? "sí" : "no"),
              "Localizar o generar el par de peso correspondiente para permitir interpolación con el peso del NPC."});
        } else {
          piece.weight_variants_valid = true;
        }

        // Morphs check
        if (piece.geometry.has_body_tri || piece.geometry.sibling_tri_exists) {
          piece.morphs_available = true;
        } else {
          piece.morphs_available = false;
          piece.issues.push_back(FitIssue{
              DiagnosticCode::OutfitMorphMissing,
              DiagnosticSeverity::Info,
              norm_piece_path,
              prov.name,
              "La malla no tiene BODYTRI ni archivo .tri asociado (no responderá a sliders de BodySlide).",
              "Construir con 'Build Morphs' en BodySlide si existe proyecto disponible."});
        }

        // Family cross-check with body
        if (report.body.body_family != MeshFamily::Unknown && piece.detected_family != MeshFamily::Unknown) {
          if (report.body.body_family == MeshFamily::Cbbe3ba && piece.detected_family == MeshFamily::Ube) {
            piece.topology_compatible = false;
            piece.issues.push_back(FitIssue{
                DiagnosticCode::BodyOutfitFamilyMismatch,
                DiagnosticSeverity::Warning,
                norm_piece_path,
                prov.name,
                "Indicio por nombres y metadatos (sin medir): prenda UBE sobre cuerpo CBBE 3BA. Comprueba el ajuste medido en la pestaña Fidelidad.",
                "Si la Fidelidad confirma penetración o huecos, sustituir por la variante CBBE/3BA de la prenda."});
          } else if (report.body.body_family == MeshFamily::Ube && (piece.detected_family == MeshFamily::Cbbe || piece.detected_family == MeshFamily::Cbbe3ba)) {
            piece.topology_compatible = false;
            piece.issues.push_back(FitIssue{
                DiagnosticCode::BodyOutfitFamilyMismatch,
                DiagnosticSeverity::Warning,
                norm_piece_path,
                prov.name,
                "Indicio por nombres y metadatos (sin medir): prenda CBBE/3BA sobre cuerpo UBE. Comprueba el ajuste medido en la pestaña Fidelidad.",
                "Si la Fidelidad confirma penetración o huecos, sustituir por la variante UBE de la prenda."});
          }
        }

        // Needs refit check
        if (report.body.body_has_tri && !piece.morphs_available) {
          piece.issues.push_back(FitIssue{
              DiagnosticCode::OutfitNeedsRefit,
              DiagnosticSeverity::Warning,
              norm_piece_path,
              prov.name,
              "El cuerpo de Serana cuenta con deformaciones BodySlide activas, pero esta prenda es estática.",
              "Reconstruir en BodySlide o usar un preset con morphs."});
        }

        // Covered body check (if slot 32, 33, 37)
        for (int s : piece.biped_slots) {
          if (s == 32) {
            piece.issues.push_back(FitIssue{
                DiagnosticCode::BodyPartCovered,
                DiagnosticSeverity::Info,
                norm_piece_path,
                prov.name,
                "Esta prenda cubre el torso (Slot 32: Body), ocultando la piel subyacente para evitar clipping.",
                "Comportamiento estándar de Skyrim."});
          } else if (s == 33) {
            piece.issues.push_back(FitIssue{
                DiagnosticCode::BodyPartCovered,
                DiagnosticSeverity::Info,
                norm_piece_path,
                prov.name,
                "Esta prenda cubre las manos (Slot 33: Hands).",
                "Comportamiento estándar de Skyrim."});
          } else if (s == 37) {
            piece.issues.push_back(FitIssue{
                DiagnosticCode::BodyPartCovered,
                DiagnosticSeverity::Info,
                norm_piece_path,
                prov.name,
                "Esta prenda cubre los pies (Slot 37: Feet).",
                "Comportamiento estándar de Skyrim."});
          }
        }

        // Search for compatible alternatives using cached directory listings
        if (!meshes_cached) {
          cached_meshes_files = vfs.list_directory("meshes", true);
          meshes_cached = true;
        }
        if (!bs_cached) {
          cached_bs_files = vfs.list_directory("calientetools\\bodyslide", true);
          bs_cached = true;
        }

        piece.compatible_alternatives = find_alternatives(vfs, piece, report.body.body_family, resolver,
                                                          &cached_meshes_files, &cached_bs_files);

        report.pieces.push_back(std::move(piece));
      }
    }
  }

  // Calculate totals
  for (const auto& p : report.pieces) {
    report.total_issues_count += p.issues.size();
    if (!p.topology_compatible || p.detected_family != report.body.body_family) {
      if (p.detected_family != MeshFamily::Unknown && report.body.body_family != MeshFamily::Unknown) {
        report.incompatible_pieces_count++;
      }
    }
    if (!p.compatible_alternatives.empty()) {
      report.fixable_alternatives_count += p.compatible_alternatives.size();
    }
    if (!p.morphs_available && report.body.body_has_tri) {
      report.needs_bodyslide_refit = true;
    }
  }

  return report;
}

std::vector<CompatibleAlternative> OutfitFitDoctor::find_alternatives(
    const mo2::VirtualFileSystem& vfs,
    const PieceFitDiagnosis& piece,
    MeshFamily target_family,
    const assets::AssetResolver* resolver,
    const std::vector<std::string>* cached_meshes_files,
    const std::vector<std::string>* cached_bs_files) {
  std::vector<CompatibleAlternative> results;
  const std::filesystem::path current_p(piece.effective_nif_path);
  const std::string stem = current_p.stem().string();

  const std::string search_stem = (stem.ends_with("_0") || stem.ends_with("_1")) ? stem.substr(0, stem.size() - 2) : stem;
  if (search_stem.empty()) return results;

  std::vector<std::string> local_meshes;
  const auto& meshes_files = cached_meshes_files ? *cached_meshes_files : (local_meshes = vfs.list_directory("meshes", true), local_meshes);

  const std::string lower_stem = to_lower(search_stem);
  const std::string norm_current = normalize_vfs_path(piece.effective_nif_path);

  int candidates_inspected = 0;
  for (const auto& data_path : meshes_files) {
    if (!data_path.ends_with(".nif")) continue;
    if (to_lower(data_path).find(lower_stem) == std::string::npos) continue;
    if (data_path == norm_current) continue;

    const auto entry = vfs.resolve(data_path);
    if (!entry) continue;
    const auto& prov = vfs.providers()[entry->provider];

    std::string bytes;
    if (resolver) {
      const auto res = resolver->read(data_path);
      if (res) bytes = *res;
    }
    if (bytes.empty() && !entry->absolute.empty()) {
      bytes = read_file_bytes(entry->absolute);
    }

    if (bytes.empty()) continue;

    const auto geom = inspect_nif_geometry(bytes, data_path, vfs);
    const auto [alt_fam, conf] = classify_family(geom, data_path, prov.name);

    if (target_family != MeshFamily::Unknown && alt_fam != target_family && alt_fam != MeshFamily::Cbbe3ba && alt_fam != MeshFamily::Cbbe) {
      continue;
    }

    CompatibleAlternative alt;
    alt.resource_id = std::format("{}:{}", prov.name, data_path);
    alt.mod_name = prov.name;
    alt.data_path = data_path;
    alt.family = alt_fam;
    alt.confidence = conf;
    alt.has_tri = geom.has_body_tri || geom.sibling_tri_exists;
    alt.has_weight_variants = geom.has_weight_0 && geom.has_weight_1;
    alt.description = std::format("Variante «{}» encontrada en mod «{}» (Familia: {}, TRI: {})",
                                  alt.data_path, alt.mod_name, family_to_string(alt_fam), alt.has_tri ? "Sí" : "No");

    results.push_back(std::move(alt));
    if (++candidates_inspected >= 10) break;
  }

  // Also check for BodySlide ShapeData project files in CalienteTools\BodySlide
  std::vector<std::string> local_bs;
  const auto& bs_files = cached_bs_files ? *cached_bs_files : (local_bs = vfs.list_directory("calientetools\\bodyslide", true), local_bs);
  for (const auto& data_path : bs_files) {
    if (!data_path.ends_with(".nif") && !data_path.ends_with(".osp")) continue;
    if (to_lower(data_path).find(lower_stem) != std::string::npos) {
      const auto entry = vfs.resolve(data_path);
      if (!entry) continue;
      const auto& prov = vfs.providers()[entry->provider];

      CompatibleAlternative bs_alt;
      bs_alt.resource_id = std::format("{}:{}", prov.name, data_path);
      bs_alt.mod_name = prov.name;
      bs_alt.data_path = data_path;
      bs_alt.bodyslide_project = entry->absolute.string();
      bs_alt.family = target_family;
      bs_alt.confidence = ConfidenceLevel::ProbableMetadata;
      bs_alt.has_tri = true;
      bs_alt.description = std::format("Proyecto BodySlide listo para generar en «{}»", prov.name);
      results.push_back(std::move(bs_alt));
      if (results.size() >= 15) break;
    }
  }

  return results;
}

nlohmann::json OutfitFitDoctor::to_json(const OutfitFitReport& report) {
  nlohmann::json j;
  j["body"] = {
      {"formKey", report.body.form_key},
      {"name", report.body.name},
      {"sex", report.body.sex},
      {"race", report.body.race},
      {"bodyFamily", family_to_string(report.body.body_family)},
      {"confidence", confidence_to_string(report.body.confidence)},
      {"bodyProvider", report.body.body_provider},
      {"bodyMeshPath", report.body.body_mesh_path},
      {"handsMeshPath", report.body.hands_mesh_path},
      {"feetMeshPath", report.body.feet_mesh_path},
      {"bodyHasTri", report.body.body_has_tri},
      {"physicsBones", report.body.physics_bones}};

  j["outfit"] = {
      {"formKey", report.outfit_form_key},
      {"name", report.outfit_name}};

  j["summary"] = {
      {"totalIssuesCount", report.total_issues_count},
      {"incompatiblePiecesCount", report.incompatible_pieces_count},
      {"fixableAlternativesCount", report.fixable_alternatives_count},
      {"needsBodyslideRefit", report.needs_bodyslide_refit}};

  nlohmann::json pieces_json = nlohmann::json::array();
  for (const auto& p : report.pieces) {
    nlohmann::json pj;
    pj["armorFormKey"] = p.armor_form_key;
    pj["armorName"] = p.armor_name;
    pj["addonFormKey"] = p.addon_form_key;
    pj["slots"] = p.biped_slots;
    pj["slotDescription"] = p.slot_description;
    pj["slotMask"] = p.slot_mask;
    pj["effectiveNifPath"] = p.effective_nif_path;
    pj["winningProvider"] = p.winning_provider;
    pj["isOverwrite"] = p.is_overwrite;
    pj["isGameData"] = p.is_game_data;
    pj["detectedFamily"] = family_to_string(p.detected_family);
    pj["confidence"] = confidence_to_string(p.confidence);
    pj["familyReason"] = p.family_reason;
    pj["topologyCompatible"] = p.topology_compatible;
    pj["morphsAvailable"] = p.morphs_available;
    pj["weightVariantsValid"] = p.weight_variants_valid;
    pj["bipedPartitionsCoherent"] = p.biped_partitions_coherent;
    pj["skeletonCompatible"] = p.skeleton_compatible;

    pj["geometry"] = {
        {"vertexCount", p.geometry.vertex_count},
        {"triangleCount", p.geometry.triangle_count},
        {"shapeNames", p.geometry.shape_names},
        {"physicsBones", p.geometry.physics_bones},
        {"hasBodyTri", p.geometry.has_body_tri},
        {"siblingTriExists", p.geometry.sibling_tri_exists},
        {"hasWeight0", p.geometry.has_weight_0},
        {"hasWeight1", p.geometry.has_weight_1},
        {"hiddenOrCollisionShapes", p.geometry.hidden_or_collision_shapes}};

    nlohmann::json issues_json = nlohmann::json::array();
    for (const auto& is : p.issues) {
      issues_json.push_back({
          {"code", is.code_string()},
          {"severity", is.severity_string()},
          {"filePath", is.file_path},
          {"provider", is.provider},
          {"evidence", is.evidence},
          {"possibleFix", is.possible_fix}});
    }
    pj["issues"] = issues_json;

    nlohmann::json alts_json = nlohmann::json::array();
    for (const auto& alt : p.compatible_alternatives) {
      alts_json.push_back({
          {"resourceId", alt.resource_id},
          {"modName", alt.mod_name},
          {"dataPath", alt.data_path},
          {"family", family_to_string(alt.family)},
          {"confidence", confidence_to_string(alt.confidence)},
          {"hasTri", alt.has_tri},
          {"hasWeightVariants", alt.has_weight_variants},
          {"bodyslideProject", alt.bodyslide_project},
          {"description", alt.description}});
    }
    pj["compatibleAlternatives"] = alts_json;

    pieces_json.push_back(std::move(pj));
  }
  j["pieces"] = pieces_json;

  return j;
}

OutfitFitReport OutfitFitDoctor::from_json(const nlohmann::json& j) {
  OutfitFitReport report;
  if (j.contains("body") && j["body"].is_object()) {
    const auto& b = j["body"];
    report.body.form_key = b.value("formKey", "");
    report.body.name = b.value("name", "");
    report.body.sex = b.value("sex", "Female");
    report.body.race = b.value("race", "");
    report.body.body_family = family_from_string(b.value("bodyFamily", "Unknown"));
    report.body.confidence = confidence_from_string(b.value("confidence", "indeterminate"));
    report.body.body_provider = b.value("bodyProvider", "");
    report.body.body_mesh_path = b.value("bodyMeshPath", "");
    report.body.hands_mesh_path = b.value("handsMeshPath", "");
    report.body.feet_mesh_path = b.value("feetMeshPath", "");
    report.body.body_has_tri = b.value("bodyHasTri", false);
    report.body.physics_bones = b.value("physicsBones", std::vector<std::string>{});
  }

  if (j.contains("outfit") && j["outfit"].is_object()) {
    report.outfit_form_key = j["outfit"].value("formKey", "");
    report.outfit_name = j["outfit"].value("name", "");
  }

  if (j.contains("pieces") && j["pieces"].is_array()) {
    for (const auto& pj : j["pieces"]) {
      PieceFitDiagnosis p;
      p.armor_form_key = pj.value("armorFormKey", "");
      p.armor_name = pj.value("armorName", "");
      p.addon_form_key = pj.value("addonFormKey", "");
      p.biped_slots = pj.value("slots", std::vector<int>{});
      p.slot_description = pj.value("slotDescription", "");
      p.slot_mask = pj.value("slotMask", 0u);
      p.effective_nif_path = pj.value("effectiveNifPath", "");
      p.winning_provider = pj.value("winningProvider", "");
      p.is_overwrite = pj.value("isOverwrite", false);
      p.is_game_data = pj.value("isGameData", false);
      p.detected_family = family_from_string(pj.value("detectedFamily", "Unknown"));
      p.confidence = confidence_from_string(pj.value("confidence", "indeterminate"));
      p.family_reason = pj.value("familyReason", "");
      p.topology_compatible = pj.value("topologyCompatible", true);
      p.morphs_available = pj.value("morphsAvailable", false);
      p.weight_variants_valid = pj.value("weightVariantsValid", false);
      p.biped_partitions_coherent = pj.value("bipedPartitionsCoherent", true);
      p.skeleton_compatible = pj.value("skeletonCompatible", true);

      if (pj.contains("geometry") && pj["geometry"].is_object()) {
        const auto& gj = pj["geometry"];
        p.geometry.vertex_count = gj.value("vertexCount", 0ull);
        p.geometry.triangle_count = gj.value("triangleCount", 0ull);
        p.geometry.shape_names = gj.value("shapeNames", std::vector<std::string>{});
        p.geometry.physics_bones = gj.value("physicsBones", std::vector<std::string>{});
        p.geometry.has_body_tri = gj.value("hasBodyTri", false);
        p.geometry.sibling_tri_exists = gj.value("siblingTriExists", false);
        p.geometry.has_weight_0 = gj.value("hasWeight0", false);
        p.geometry.has_weight_1 = gj.value("hasWeight1", false);
        p.geometry.hidden_or_collision_shapes = gj.value("hiddenOrCollisionShapes", std::vector<std::string>{});
      }

      if (pj.contains("issues") && pj["issues"].is_array()) {
        for (const auto& ij : pj["issues"]) {
          FitIssue is;
          is.code = diagnostic_code_from_string(ij.value("code", ""));
          is.severity = severity_from_string(ij.value("severity", "info"));
          is.file_path = ij.value("filePath", "");
          is.provider = ij.value("provider", "");
          is.evidence = ij.value("evidence", "");
          is.possible_fix = ij.value("possibleFix", "");
          p.issues.push_back(std::move(is));
        }
      }

      if (pj.contains("compatibleAlternatives") && pj["compatibleAlternatives"].is_array()) {
        for (const auto& aj : pj["compatibleAlternatives"]) {
          CompatibleAlternative alt;
          alt.resource_id = aj.value("resourceId", "");
          alt.mod_name = aj.value("modName", "");
          alt.data_path = aj.value("dataPath", "");
          alt.family = family_from_string(aj.value("family", "Unknown"));
          alt.confidence = confidence_from_string(aj.value("confidence", "indeterminate"));
          alt.has_tri = aj.value("hasTri", false);
          alt.has_weight_variants = aj.value("hasWeightVariants", false);
          alt.bodyslide_project = aj.value("bodyslideProject", "");
          alt.description = aj.value("description", "");
          p.compatible_alternatives.push_back(std::move(alt));
        }
      }

      report.pieces.push_back(std::move(p));
    }
  }

  return report;
}

bool OutfitFitDoctor::export_doctor_patch(
    const mo2::VirtualFileSystem& vfs,
    const OutfitFitReport& report,
    const std::vector<PieceRepairPatch>& patches,
    const std::filesystem::path& target_mod_dir) {
  std::error_code ec;
  std::filesystem::create_directories(target_mod_dir, ec);
  if (ec) return false;

  // 1. Write manifest.json
  nlohmann::json manifest;
  manifest["modName"] = target_mod_dir.filename().string();
  manifest["targetNpc"] = report.body.form_key;
  manifest["npcName"] = report.body.name;
  manifest["bodyFamily"] = family_to_string(report.body.body_family);
  manifest["safePatch"] = true;
  manifest["inGameVerified"] = false;
  manifest["patchesCount"] = patches.size();

  nlohmann::json p_arr = nlohmann::json::array();
  for (const auto& p : patches) {
    nlohmann::json pj;
    pj["armorFormKey"] = p.armor_form_key;
    pj["addonFormKey"] = p.addon_form_key;
    pj["originalNif"] = p.original_nif;
    pj["repairedNif"] = p.repaired_nif;
    pj["sourceMod"] = p.source_mod;
    p_arr.push_back(pj);

    // Deploy repaired mesh if found in VFS
    if (auto res = vfs.resolve(p.repaired_nif)) {
      const auto dest = target_mod_dir / p.original_nif;
      std::filesystem::create_directories(dest.parent_path(), ec);
      std::filesystem::copy_file(res->absolute, dest, std::filesystem::copy_options::overwrite_existing, ec);
    }
  }
  manifest["patches"] = p_arr;

  std::ofstream mf(target_mod_dir / "manifest.json");
  if (!mf.is_open()) return false;
  mf << manifest.dump(2);
  mf.close();

  // 2. Write README.md
  std::ofstream rf(target_mod_dir / "README.md");
  if (rf.is_open()) {
    rf << "# " << target_mod_dir.filename().string() << " — Outfit Fit Doctor Safe Patch\n\n";
    rf << "Target NPC: " << report.body.name << " (" << report.body.form_key << ")\n";
    rf << "Body Family: " << family_to_string(report.body.body_family) << " from " << report.body.body_provider << "\n\n";
    rf << "## Patches\n";
    for (const auto& p : patches) {
      rf << "- " << p.original_nif << " -> " << p.repaired_nif << " (from " << p.source_mod << ")\n";
    }
    rf.close();
  }

  // 3. Write fit-report.json
  std::ofstream repf(target_mod_dir / "fit-report.json");
  if (repf.is_open()) {
    repf << to_json(report).dump(2);
    repf.close();
  }

  return true;
}

}  // namespace sns::outfit
