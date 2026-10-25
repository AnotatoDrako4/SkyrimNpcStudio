#include "sns/outfit/loose_outfit_indexer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::outfit {

namespace {

std::string to_lower_copy(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

std::string normalize_path_key(std::string_view p) {
  std::string out;
  out.reserve(p.size());
  for (char c : p) {
    if (c == '/') out.push_back('\\');
    else out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string derive_display_name_from_path(std::string_view data_path) {
  std::filesystem::path p(data_path);
  std::string stem = p.stem().string();
  if (stem.ends_with("_0") || stem.ends_with("_1")) {
    stem = stem.substr(0, stem.size() - 2);
  }
  // Replace underscores and hyphens with spaces for clean UI display
  for (char& c : stem) {
    if (c == '_' || c == '-') c = ' ';
  }
  // Trim leading/trailing whitespace
  while (!stem.empty() && stem.front() == ' ') stem.erase(stem.begin());
  while (!stem.empty() && stem.back() == ' ') stem.pop_back();
  return stem.empty() ? p.stem().string() : stem;
}

// Lightweight XML tag and attribute extractor for .osp files
struct OspSliderSet {
  std::string name;
  std::string data_folder;
  std::string source_file;
  std::string output_path;
  std::string output_file;
  std::vector<std::string> shapes;
};

std::vector<OspSliderSet> parse_osp_file(std::string_view content) {
  std::vector<OspSliderSet> sets;
  std::size_t pos = 0;
  while ((pos = content.find("<SliderSet", pos)) != std::string_view::npos) {
    // Ensure it's <SliderSet and not <SliderSetInfo or <SliderSets
    if (pos + 10 < content.size() && content[pos + 10] != ' ' && content[pos + 10] != '>' && content[pos + 10] != '\t' && content[pos + 10] != '\r' && content[pos + 10] != '\n') {
      pos += 10;
      continue;
    }
    std::size_t set_end = content.find("</SliderSet>", pos);
    if (set_end == std::string_view::npos) break;
    std::string_view block = content.substr(pos, set_end + 12 - pos);

    OspSliderSet s;
    // Extract name attribute: <SliderSet name="...">
    std::size_t name_attr = block.find("name=\"");
    if (name_attr != std::string_view::npos) {
      name_attr += 6;
      std::size_t name_end = block.find('"', name_attr);
      if (name_end != std::string_view::npos) {
        s.name = std::string(block.substr(name_attr, name_end - name_attr));
      }
    }

    // Extract <DataFolder>...</DataFolder>
    std::size_t df_p = block.find("<DataFolder>");
    if (df_p != std::string_view::npos) {
      df_p += 12;
      std::size_t df_end = block.find("</DataFolder>", df_p);
      if (df_end != std::string_view::npos) {
        s.data_folder = std::string(block.substr(df_p, df_end - df_p));
      }
    }

    // Extract <SourceFile>...</SourceFile>
    std::size_t sf_p = block.find("<SourceFile>");
    if (sf_p != std::string_view::npos) {
      sf_p += 12;
      std::size_t sf_end = block.find("</SourceFile>", sf_p);
      if (sf_end != std::string_view::npos) {
        s.source_file = std::string(block.substr(sf_p, sf_end - sf_p));
      }
    }

    // Extract <OutputPath>...</OutputPath>
    std::size_t out_p = block.find("<OutputPath>");
    if (out_p != std::string_view::npos) {
      out_p += 12;
      std::size_t out_p_end = block.find("</OutputPath>", out_p);
      if (out_p_end != std::string_view::npos) {
        s.output_path = std::string(block.substr(out_p, out_p_end - out_p));
      }
    }

    // Extract <OutputFile...>...</OutputFile>
    std::size_t out_f = block.find("<OutputFile");
    if (out_f != std::string_view::npos) {
      std::size_t out_f_tag_end = block.find('>', out_f);
      if (out_f_tag_end != std::string_view::npos) {
        std::size_t out_f_end = block.find("</OutputFile>", out_f_tag_end);
        if (out_f_end != std::string_view::npos) {
          s.output_file = std::string(block.substr(out_f_tag_end + 1, out_f_end - (out_f_tag_end + 1)));
        }
      }
    }

    // Extract <Shape target="...">
    std::size_t shape_pos = 0;
    while ((shape_pos = block.find("<Shape", shape_pos)) != std::string_view::npos) {
      std::size_t tag_end = block.find('>', shape_pos);
      if (tag_end == std::string_view::npos) break;
      std::size_t target_attr = block.find("target=\"", shape_pos);
      if (target_attr != std::string_view::npos && target_attr < tag_end) {
        target_attr += 8;
        std::size_t attr_end = block.find('"', target_attr);
        if (attr_end != std::string_view::npos && attr_end < tag_end) {
          s.shapes.push_back(std::string(block.substr(target_attr, attr_end - target_attr)));
        }
      }
      shape_pos = tag_end + 1;
    }

    if (!s.name.empty()) {
      sets.push_back(std::move(s));
    }
    pos = set_end + 12;
  }
  return sets;
}

bool is_ignored_mesh_path(std::string_view lower_path) {
  // Exclude non-clothing and non-character directories to keep index focused and high-performance
  static const std::string_view kIgnoredPrefixes[] = {
      "meshes\\actors\\character\\animations",
      "meshes\\actors\\character\\behaviors",
      "meshes\\actors\\character\\facegendata",
      "meshes\\actors\\character\\facegenmorphs",
      "meshes\\actors\\ambient",
      "meshes\\actors\\critters",
      "meshes\\terrain",
      "meshes\\architecture",
      "meshes\\weapons",
      "meshes\\plants",
      "meshes\\clutter",
      "meshes\\effects",
      "meshes\\interface",
      "meshes\\traps",
      "meshes\\dungeons",
      "meshes\\furniture",
      "meshes\\landscape",
      "meshes\\sky",
      "meshes\\lod",
      "meshes\\magic",
      "meshes\\markers",
      "meshes\\props",
      "meshes\\scenery",
      "meshes\\water",
      "meshes\\vegetation",
      "meshes\\loadscreens",
  };
  for (const auto& prefix : kIgnoredPrefixes) {
    if (lower_path.starts_with(prefix)) return true;
  }
  // Exclude non-humanoid actor meshes (creatures, monsters, animals)
  if (lower_path.starts_with("meshes\\actors\\") && !lower_path.starts_with("meshes\\actors\\character\\")) {
    return true;
  }
  // Exclude standalone skeletons
  if (lower_path.ends_with("skeleton.nif") || lower_path.ends_with("skeleton_female.nif")) {
    return true;
  }
  return false;
}

bool is_clothing_mesh_candidate(std::string_view lower_path) {
  // Exclude inventory ground models (not worn outfits)
  if (lower_path.ends_with("_gnd.nif") || lower_path.ends_with("gnd.nif") ||
      lower_path.ends_with("_go.nif") || lower_path.ends_with("go.nif")) {
    return false;
  }
  // Exclude first person hand models
  if (lower_path.find("1stperson") != std::string_view::npos ||
      lower_path.find("1st_person") != std::string_view::npos) {
    return false;
  }
  if (lower_path.starts_with("meshes\\armor\\") ||
      lower_path.starts_with("meshes\\clothes\\") ||
      lower_path.starts_with("meshes\\actors\\character\\")) {
    return !is_ignored_mesh_path(lower_path);
  }
  // Mod-specific folders: only clothing/armor meshes with weight slider or recognizable clothing categories
  if (lower_path.ends_with("_0.nif") || lower_path.ends_with("_1.nif")) {
    return !is_ignored_mesh_path(lower_path);
  }
  return false;
}

}  // namespace

bool is_safe_data_path(std::string_view path) {
  if (path.empty()) return false;
  if (path.front() == '/' || path.front() == '\\') return false;
  // Reject drive letters or absolute paths
  if (path.size() >= 2 && path[1] == ':') return false;
  // Reject backtracking sequences
  if (path.find("..") != std::string_view::npos) return false;
  // Reject control characters or null bytes
  for (char c : path) {
    if (static_cast<unsigned char>(c) < 32) return false;
  }
  return true;
}

std::string infer_clothing_category(std::string_view path) {
  const auto lower = to_lower_copy(path);
  if (lower.find("boot") != std::string_view::npos || lower.find("shoe") != std::string_view::npos ||
      lower.find("feet") != std::string_view::npos || lower.find("foot") != std::string_view::npos ||
      lower.find("sandals") != std::string_view::npos || lower.find("heels") != std::string_view::npos ||
      lower.find("greaves") != std::string_view::npos) {
    return "boots";
  }
  if (lower.find("glove") != std::string_view::npos || lower.find("gauntlet") != std::string_view::npos ||
      lower.find("hand") != std::string_view::npos || lower.find("bracer") != std::string_view::npos ||
      lower.find("cuff") != std::string_view::npos || lower.find("mitt") != std::string_view::npos) {
    return "gauntlets";
  }
  if (lower.find("helm") != std::string_view::npos || lower.find("hood") != std::string_view::npos ||
      lower.find("hat") != std::string_view::npos || lower.find("circlet") != std::string_view::npos ||
      lower.find("mask") != std::string_view::npos || lower.find("crown") != std::string_view::npos ||
      lower.find("tiara") != std::string_view::npos || lower.find("cap") != std::string_view::npos) {
    return "helmet";
  }
  if (lower.find("ring") != std::string_view::npos || lower.find("amulet") != std::string_view::npos ||
      lower.find("necklace") != std::string_view::npos || lower.find("cape") != std::string_view::npos ||
      lower.find("cloak") != std::string_view::npos || lower.find("earring") != std::string_view::npos ||
      lower.find("belt") != std::string_view::npos || lower.find("scarf") != std::string_view::npos ||
      lower.find("choker") != std::string_view::npos) {
    return "accessory";
  }
  if (lower.find("cuirass") != std::string_view::npos || lower.find("robe") != std::string_view::npos ||
      lower.find("dress") != std::string_view::npos || lower.find("armor") != std::string_view::npos ||
      lower.find("top") != std::string_view::npos || lower.find("corset") != std::string_view::npos ||
      lower.find("shirt") != std::string_view::npos || lower.find("pant") != std::string_view::npos ||
      lower.find("skirt") != std::string_view::npos || lower.find("suit") != std::string_view::npos ||
      lower.find("tunic") != std::string_view::npos || lower.find("body") != std::string_view::npos ||
      lower.find("bra") != std::string_view::npos || lower.find("legs") != std::string_view::npos ||
      lower.find("bottom") != std::string_view::npos) {
    return "cuirass";
  }
  return "other";
}

std::string infer_body_family(std::string_view text) {
  const auto lower = to_lower_copy(text);
  if (lower.find("3ba") != std::string_view::npos || lower.find("3bbp") != std::string_view::npos) {
    return "CBBE 3BA";
  }
  if (lower.find("!ube") != std::string_view::npos || lower.find(" ube") != std::string_view::npos ||
      lower.find("_ube") != std::string_view::npos || lower.find("-ube") != std::string_view::npos ||
      lower.find("ube ") != std::string_view::npos || lower.find("ube_") != std::string_view::npos) {
    return "UBE";
  }
  if (lower.find("bhunp") != std::string_view::npos) {
    return "BHUNP";
  }
  if (lower.find("cbbe") != std::string_view::npos) {
    return "CBBE";
  }
  if (lower.find("unp") != std::string_view::npos) {
    return "UNP";
  }
  return "unknown";
}

LooseOutfitIndexResult LooseOutfitIndexer::index(
    const mo2::VirtualFileSystem& vfs,
    const assets::AssetResolver* resolver,
    const LooseOutfitIndexOptions& options) {
  LooseOutfitIndexResult result;

  // --------------------------------------------------------------------------
  // 1. Scan BodySlide SliderSets (*.osp and *.xml) under CalienteTools\BodySlide\SliderSets
  // --------------------------------------------------------------------------
  const auto slider_set_files = vfs.list_directory("CalienteTools\\BodySlide\\SliderSets", true);
  for (const auto& file_rel : slider_set_files) {
    if (options.cancelled && options.cancelled()) return result;

    if (!is_safe_data_path(file_rel)) {
      result.issues.push_back("Ruta no segura rechazada en SliderSets: " + file_rel);
      continue;
    }

    const auto lower = to_lower_copy(file_rel);
    if (!lower.ends_with(".osp") && !lower.ends_with(".xml")) continue;

    const auto provider_chain = vfs.providers_of(file_rel);
    if (provider_chain.empty()) continue;

    const auto winning_idx = provider_chain.back();
    const auto& winning_prov = vfs.providers()[winning_idx];

    std::vector<std::string> overridden;
    for (std::size_t i = 0; i + 1 < provider_chain.size(); ++i) {
      overridden.push_back(vfs.providers()[provider_chain[i]].name);
    }

    // Read content: check direct loose file first for high performance
    std::string content;
    if (auto resolved = vfs.resolve(file_rel)) {
      std::ifstream in(resolved->absolute, std::ios::binary);
      if (in) {
        in.seekg(0, std::ios::end);
        const auto sz = in.tellg();
        if (sz > 0 && sz < 20 * 1024 * 1024) {  // Sanity limit: max 20MB per OSP
          content.resize(static_cast<std::size_t>(sz));
          in.seekg(0, std::ios::beg);
          in.read(content.data(), sz);
        }
      }
    } else if (resolver) {
      if (auto bytes = resolver->read(file_rel)) {
        content = *bytes;
      }
    }

    if (content.empty()) {
      result.issues.push_back("Archivo BodySlide no legible o vacío: " + file_rel);
      continue;
    }

    auto parsed_sets = parse_osp_file(content);
    if (parsed_sets.empty()) {
      // Malformed or empty XML
      result.issues.push_back("Proyecto BodySlide con XML inválido o sin SliderSet: " + file_rel);
      continue;
    }

    for (const auto& set : parsed_sets) {
      LooseResource res;
      res.resource_id = winning_prov.name + ":" + file_rel + "#" + set.name;
      res.display_name = set.name;
      res.kind = "bodyslide-project";
      res.data_relative_path = file_rel;
      res.winning_provider = winning_prov.name;
      res.overridden_providers = overridden;
      res.size_bytes = content.size();
      res.category = infer_clothing_category(set.name + " " + set.output_file + " " + set.output_path);

      // Infer body family from set name, shape names, and mod provider name
      std::string family_evidence = set.name + " " + winning_prov.name;
      for (const auto& sh : set.shapes) family_evidence += " " + sh;
      res.body_family = infer_body_family(family_evidence);

      res.association.confidence = "unconfirmed";
      res.association.evidence.push_back("Proyecto BodySlide en " + file_rel);

      res.capabilities.discovered = true;
      res.capabilities.equippable = false;   // Cannot equip loose project directly in RC-A
      res.capabilities.convertible = "requires-bodyslide-build";
      res.capabilities.exportable = false;

      res.associated_files.push_back(file_rel);

      std::string preview_mesh_path;
      if (!set.output_path.empty() && !set.output_file.empty()) {
        std::string out_dir = set.output_path;
        while (!out_dir.empty() && (out_dir.back() == '\\' || out_dir.back() == '/')) out_dir.pop_back();
        auto out_dir_lower = to_lower_copy(out_dir);
        std::string out_dir_meshes = out_dir;
        if (!out_dir_lower.starts_with("meshes\\") && !out_dir_lower.starts_with("meshes/")) {
          out_dir_meshes = "meshes\\" + out_dir;
        }

        std::string exp_1 = out_dir + "\\" + set.output_file + "_1.nif";
        std::string exp_s = out_dir + "\\" + set.output_file + ".nif";
        std::string exp_0 = out_dir + "\\" + set.output_file + "_0.nif";
        std::string exp_m1 = out_dir_meshes + "\\" + set.output_file + "_1.nif";
        std::string exp_ms = out_dir_meshes + "\\" + set.output_file + ".nif";
        std::string exp_m0 = out_dir_meshes + "\\" + set.output_file + "_0.nif";

        res.associated_files.push_back(exp_1);

        if (vfs.resolve(exp_1)) preview_mesh_path = exp_1;
        else if (vfs.resolve(exp_s)) preview_mesh_path = exp_s;
        else if (vfs.resolve(exp_0)) preview_mesh_path = exp_0;
        else if (vfs.resolve(exp_m1)) preview_mesh_path = exp_m1;
        else if (vfs.resolve(exp_ms)) preview_mesh_path = exp_ms;
        else if (vfs.resolve(exp_m0)) preview_mesh_path = exp_m0;
      }

      if (preview_mesh_path.empty() && !set.source_file.empty()) {
        std::string shape_with_folder = set.data_folder.empty() ? "" : ("CalienteTools\\BodySlide\\ShapeData\\" + set.data_folder + "\\" + set.source_file);
        std::string shape_direct = "CalienteTools\\BodySlide\\ShapeData\\" + set.source_file;
        std::string shape_folder_direct = set.data_folder.empty() ? "" : (set.data_folder + "\\" + set.source_file);

        if (!shape_with_folder.empty() && vfs.resolve(shape_with_folder)) {
          preview_mesh_path = shape_with_folder;
          res.associated_files.push_back(shape_with_folder);
        } else if (vfs.resolve(shape_direct)) {
          preview_mesh_path = shape_direct;
          res.associated_files.push_back(shape_direct);
        } else if (!shape_folder_direct.empty() && vfs.resolve(shape_folder_direct)) {
          preview_mesh_path = shape_folder_direct;
          res.associated_files.push_back(shape_folder_direct);
        }
      }

      if (!preview_mesh_path.empty()) {
        res.capabilities.previewable = true;
        res.data_relative_path = preview_mesh_path;
      } else {
        res.capabilities.previewable = false;
        res.issues.push_back("Requiere ser construido con BodySlide antes de poder previsualizar o equipar");
      }

      result.resources.push_back(std::move(res));
      ++result.bodyslide_project_count;
    }
  }

  // --------------------------------------------------------------------------
  // 2. Scan loose meshes under meshes/
  // --------------------------------------------------------------------------
  const auto mesh_files = vfs.list_directory("meshes", true);

  // Group _0 and _1 meshes by base key to represent single logical pieces
  struct MeshPairGroup {
    std::string base_display;
    std::string path_0;
    std::string path_1;
    std::string single_path;
  };
  std::unordered_map<std::string, MeshPairGroup> mesh_groups;

  for (const auto& file_rel : mesh_files) {
    if (options.cancelled && options.cancelled()) return result;

    if (!is_safe_data_path(file_rel)) {
      result.issues.push_back("Ruta no segura rechazada en mallas: " + file_rel);
      continue;
    }

    const auto lower = normalize_path_key(file_rel);
    if (!lower.ends_with(".nif")) continue;
    if (!is_clothing_mesh_candidate(lower)) continue;

    if (lower.ends_with("_0.nif")) {
      std::string base = lower.substr(0, lower.size() - 6);
      auto& grp = mesh_groups[base];
      grp.path_0 = file_rel;
      if (grp.base_display.empty()) grp.base_display = file_rel.substr(0, file_rel.size() - 6);
    } else if (lower.ends_with("_1.nif")) {
      std::string base = lower.substr(0, lower.size() - 6);
      auto& grp = mesh_groups[base];
      grp.path_1 = file_rel;
      if (grp.base_display.empty()) grp.base_display = file_rel.substr(0, file_rel.size() - 6);
    } else {
      auto& grp = mesh_groups[lower];
      grp.single_path = file_rel;
      grp.base_display = file_rel;
    }
  }

  for (const auto& [base_key, grp] : mesh_groups) {
    if (options.cancelled && options.cancelled()) return result;

    // Prefer _1.nif as the primary representative, then single_path, then _0.nif
    const std::string& primary_path = !grp.path_1.empty() ? grp.path_1 : (!grp.single_path.empty() ? grp.single_path : grp.path_0);
    if (primary_path.empty()) continue;

    const auto provider_chain = vfs.providers_of(primary_path);
    if (provider_chain.empty()) continue;

    const auto winning_idx = provider_chain.back();
    const auto& winning_prov = vfs.providers()[winning_idx];

    std::vector<std::string> overridden;
    for (std::size_t i = 0; i + 1 < provider_chain.size(); ++i) {
      overridden.push_back(vfs.providers()[provider_chain[i]].name);
    }

    LooseResource res;
    res.resource_id = winning_prov.name + ":" + primary_path;
    res.data_relative_path = primary_path;
    res.winning_provider = winning_prov.name;
    res.overridden_providers = overridden;
    res.category = infer_clothing_category(primary_path);

    if (!grp.path_0.empty()) res.associated_files.push_back(grp.path_0);
    if (!grp.path_1.empty() && grp.path_1 != grp.path_0) res.associated_files.push_back(grp.path_1);
    if (!grp.single_path.empty() && grp.single_path != grp.path_0 && grp.single_path != grp.path_1) {
      res.associated_files.push_back(grp.single_path);
    }

    // Check file size and verify NIF integrity (RES-03)
    bool nif_valid = false;
    char header_buf[8192] = {};
    std::size_t header_len = 0;

    if (auto resolved = vfs.resolve(primary_path)) {
      std::ifstream in(resolved->absolute, std::ios::binary);
      if (in) {
        in.seekg(0, std::ios::end);
        res.size_bytes = static_cast<std::uint64_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        in.read(header_buf, sizeof(header_buf));
        header_len = static_cast<std::size_t>(in.gcount());
      }
    } else if (resolver) {
      if (auto b = resolver->read(primary_path)) {
        res.size_bytes = b->size();
        header_len = std::min<std::size_t>(sizeof(header_buf), b->size());
        std::memcpy(header_buf, b->data(), header_len);
      }
    }

    // Safety check on NIF format (RES-03)
    std::string_view header_view(header_buf, header_len);
    if (res.size_bytes < 40 || !header_view.starts_with("Gamebryo File Format")) {
      res.issues.push_back("Archivo NIF dañado o formato incompatible");
      res.capabilities.previewable = false;
    } else {
      nif_valid = true;
      res.capabilities.previewable = true;
    }

    // Check for replacer vs loose mesh (RES-01 vs RES-02)
    const auto norm_key = normalize_path_key(primary_path);
    auto it_match = options.registered_mesh_to_armo.find(norm_key);
    if (it_match == options.registered_mesh_to_armo.end() && !grp.path_0.empty()) {
      it_match = options.registered_mesh_to_armo.find(normalize_path_key(grp.path_0));
    }

    if (it_match != options.registered_mesh_to_armo.end()) {
      // RES-02: Replacer of an existing registered ARMO
      res.kind = "replacer-mesh";
      res.association.armor_form_key = it_match->second;
      res.association.confidence = "confirmed";
      res.association.evidence.push_back("Reemplaza la malla registrada para el ARMO " + it_match->second);
      res.display_name = derive_display_name_from_path(primary_path) + " (Replacer de " + it_match->second + ")";
      ++result.replacer_mesh_count;
    } else {
      // RES-01: Loose mesh without ESP
      res.kind = "loose-mesh";
      res.association.armor_form_key = "";
      res.association.confidence = "unconfirmed";
      res.display_name = derive_display_name_from_path(primary_path);
      res.issues.push_back("Malla suelta sin registro ARMO asociado en el orden de carga (Pack sin ESP)");
      ++result.loose_mesh_count;
    }

    // Infer body family from path & provider first
    std::string family_evidence = primary_path + " " + winning_prov.name;
    res.body_family = infer_body_family(family_evidence);

    // If still unknown and NIF is valid, inspect string table from the header buffer
    if (res.body_family == "unknown" && nif_valid && header_len > 40) {
      std::string words;
      words.reserve(header_len);
      for (std::size_t i = 0; i < header_len; ++i) {
        char c = header_buf[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
          words.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
          words.push_back(' ');
        }
      }
      res.body_family = infer_body_family(words);
    }

    // Look for associated .tri file
    std::string tri_path = grp.base_display + ".tri";
    if (vfs.resolve(tri_path)) {
      res.associated_files.push_back(tri_path);
    }

    res.capabilities.discovered = true;
    res.capabilities.equippable = false;  // Equipping loose meshes directly without ARMO is blocked in RC-A
    res.capabilities.convertible = "requires-inspection";
    res.capabilities.exportable = false;

    result.resources.push_back(std::move(res));
  }

  // Sort resources deterministically by display name and resource id
  std::sort(result.resources.begin(), result.resources.end(), [](const LooseResource& a, const LooseResource& b) {
    if (a.display_name != b.display_name) return a.display_name < b.display_name;
    return a.resource_id < b.resource_id;
  });

  return result;
}

}  // namespace sns::outfit
