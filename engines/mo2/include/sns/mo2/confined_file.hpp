#pragma once

#include <filesystem>
#include <string_view>

// Read-side guard for a file of ONE MO2 provider folder (a mod): the Data-relative path must stay inside that folder.
// Used before the studio reads another mod's copy of a file (the A/B view of the appearance sources reads the FaceGen
// of mods that lose in MO2). A data path comes from the worker or a plugin, so it is treated as untrusted: no absolute
// or drive paths, no ".." component, no alternate data stream, no symbolic link, and the real location of the file
// (junctions and links of the folders on the way resolved) must still be inside the folder.
namespace sns::mo2 {

enum class ConfineResult {
  Found,           // `path` is a regular file inside the folder
  Absent,          // nothing there: this mod simply has no such file (not an incident)
  UnsafePath,      // empty, absolute, with a drive, a ".." component or a ':' (alternate data stream)
  SymbolicLink,    // the file itself is a symbolic link
  EscapesFolder,   // its real location is outside the folder (a junction or link on the way)
  NotRegularFile,  // a folder, a device...
};

struct ConfinedFile {
  ConfineResult result = ConfineResult::Absent;
  std::filesystem::path path;  // the file, only when result == Found
};

[[nodiscard]] ConfinedFile confined_file(const std::filesystem::path& root, std::string_view data_path);
[[nodiscard]] std::string_view to_string(ConfineResult result);

}  // namespace sns::mo2
