#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sns/assets/asset_resolver.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::outfit {

struct LooseResourceAssociation {
  std::string armor_form_key;
  std::string armor_addon_form_key;
  std::string confidence = "unconfirmed";  // "confirmed", "probable", "unconfirmed"
  std::vector<std::string> evidence;
};

struct LooseResourceCapabilities {
  bool discovered = true;
  bool previewable = false;
  bool equippable = false;  // Always false for loose resources without ESP in RC-A
  std::string convertible = "requires-inspection";
  bool exportable = false;
};

struct LooseResource {
  std::string resource_id;         // Stable unique identifier: "<provider>:<data_relative_path>"
  std::string display_name;        // Derived readable name
  std::string kind;                // "loose-mesh", "bodyslide-project", "replacer-mesh"
  std::string data_relative_path;  // Canonical relative path inside virtual Data
  std::string winning_provider;    // Winning MO2 mod name (or "<Game Data>", "<Overwrite>")
  std::vector<std::string> overridden_providers;
  std::uint64_t size_bytes = 0;
  std::string body_family = "unknown";  // "CBBE 3BA", "CBBE", "UBE", "BHUNP", "UNP", "unknown"
  std::string category = "other";       // "cuirass", "boots", "gauntlets", "helmet", "accessory", "set", "other"

  LooseResourceAssociation association;
  LooseResourceCapabilities capabilities;

  std::vector<std::string> associated_files;  // Paired weight meshes (_0/_1), textures, tri, etc.
  std::vector<std::string> issues;            // Diagnostics, missing files, warnings
};

struct LooseOutfitIndexOptions {
  // Lowercase normalized Data-relative path (e.g. "meshes/armor/iron/cuirass_1.nif") -> ARMO FormKey
  std::unordered_map<std::string, std::string> registered_mesh_to_armo;
  std::function<bool()> cancelled;
  std::function<void(std::size_t done, std::size_t total, std::string_view current)> progress;
};

struct LooseOutfitIndexResult {
  std::vector<LooseResource> resources;
  std::size_t loose_mesh_count = 0;
  std::size_t bodyslide_project_count = 0;
  std::size_t replacer_mesh_count = 0;
  std::size_t weapon_object_mesh_count = 0;
  std::size_t facegen_count = 0;
  std::size_t texture_dds_count = 0;
  std::size_t bsa_archive_count = 0;
  std::size_t unsupported_file_count = 0;
  std::vector<std::string> issues;
};

// Security boundary check: verifies that the path does not escape Data (e.g. via '..') and has valid form (RES-03)
bool is_safe_data_path(std::string_view path);

// Infers clothing category from file name or directory name
std::string infer_clothing_category(std::string_view path);

// Infers body family from string tokens (shape names, path tokens, mod names)
std::string infer_body_family(std::string_view text);

// Discovers and indexes loose clothing meshes, replacers, and BodySlide projects without ESP
// from the virtual Data folder, respecting MO2 provider priorities and conflict resolution.
class LooseOutfitIndexer {
 public:
  static LooseOutfitIndexResult index(
      const mo2::VirtualFileSystem& vfs,
      const assets::AssetResolver* resolver = nullptr,
      const LooseOutfitIndexOptions& options = {});
};

}  // namespace sns::outfit
