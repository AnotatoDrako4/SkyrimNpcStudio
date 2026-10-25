#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Face Source Index: every NPC of the MO2 profile whose FaceGen (FaceGeom NIF / FaceTint DDS) exists in the virtual
// Data folder, with every copy the profile has of those two files: loose copies (one per MO2 mod, ascending priority)
// and copies inside BSA archives (ascending load order). It answers «which NPCs can I give another mod's face, and
// from which mods» across the whole profile, before any NPC is opened.
//
// Pure, no I/O: the studio lists the VFS and the archives and hands the observations in. Nothing here judges whether
// a face fits its record: that is the Appearance Source Matrix of each NPC (source_matrix.hpp), built on demand.
namespace sns::appearance {

// A copy of a FaceGen file inside a BSA archive.
struct ArchiveCopy {
  std::string archive;  // "Skyrim - Meshes0.bsa"
  std::string mod;      // MO2 mod (or "<Game Data>") that provides the archive
};

// One FaceGen file of the virtual Data and every copy of it.
struct FaceFileObservation {
  std::string data_path;               // as listed ("meshes\actors\character\facegendata\facegeom\skyrim.esm\000a2c8e.nif")
  std::vector<std::string> loose_mods;  // ascending MO2 priority (winner last)
  std::vector<ArchiveCopy> archives;   // ascending load order (the last one wins when there is no loose copy)
};

// The FaceGen kind and identity a Data path names, or nullopt for any other path.
struct FaceFileIdentity {
  bool geometry = false;  // FaceGeom NIF (true) or FaceTint DDS (false)
  std::string plugin;     // as written in the path
  std::string form_id;    // 8 upper-case hex digits of the file name
  std::string form_key;   // "0A2C8E:Skyrim.esm" (the low 6 digits, the project's FormKey text)
};
[[nodiscard]] std::optional<FaceFileIdentity> facegen_file_identity(std::string_view data_path);

struct FaceIndexEntry {
  std::string form_key;
  std::string plugin;
  std::string form_id;
  std::string geometry_path;  // "" when the NPC has only a tint
  std::string tint_path;
  std::vector<std::string> geometry_loose, tint_loose;
  std::vector<ArchiveCopy> geometry_archives, tint_archives;
  // Who the game loads: a loose copy always beats an archive.
  std::string geometry_winner;  // mod name, or the archive name when no loose copy exists
  bool geometry_winner_archived = false;
  std::string tint_winner;
  bool tint_winner_archived = false;
  // Distinct geometry sources (each loose mod, each archive) minus the one the game loads.
  int alternatives = 0;
  // "MultipleSources", "TintFromOtherSource" (tint winner != geometry winner), "NoTint", "TintOnly", "ArchivedOnly".
  std::vector<std::string> flags;
  // Every source name (mods and archives), for filtering.
  std::vector<std::string> sources;
};

struct FaceIndex {
  std::vector<FaceIndexEntry> entries;  // sorted by plugin (case-insensitive), then form id
  std::vector<std::string> sources;     // every source name, sorted, unique
  int with_alternatives = 0;
};

[[nodiscard]] FaceIndex build_face_index(const std::vector<FaceFileObservation>& files);

struct FaceIndexQuery {
  std::string text;      // matches FormKey, plugin, any source or the NPC's name (case-insensitive substring)
  std::string source;    // exact source name ("" = any)
  bool only_alternatives = false;
  std::string flag;      // exact flag ("" = any)
  std::size_t limit = 300;
};

// Matching entries in index order (at most `limit`); `total` receives the number of matches before the limit.
// `names` (FormKey -> display name) is optional.
[[nodiscard]] std::vector<const FaceIndexEntry*> query_face_index(const FaceIndex& index, const FaceIndexQuery& query,
                                                                  const std::map<std::string, std::string>* names, std::size_t* total);

}  // namespace sns::appearance
