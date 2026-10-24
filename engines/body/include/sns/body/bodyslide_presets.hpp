#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sns::mo2 {
class VirtualFileSystem;
}

namespace sns::body {

// Body families as Body Change NG v1.3.7 sees them (src/BodyChangeNG/BodyFamily*.cpp): a bit mask, because a
// combined BodySlide set may serve several same-sex families. kCbbe covers CBBE and CBBE 3BA (BCNG labels it
// "CBBE 3BA"); 3BA is a superset of the CBBE sliders.
enum FamilyBit : unsigned { kFamCbbe = 1u, kFamUnp = 2u, kFamUbe = 4u, kFamHimbo = 8u, kFamSam = 16u };
inline constexpr unsigned kFemaleFamilies = kFamCbbe | kFamUnp | kFamUbe;
inline constexpr unsigned kMaleFamilies = kFamHimbo | kFamSam;

// What the director shows and filters by. Unknown (BCNG "Unclassified" or several families) never receives
// automatic assignments here, even though BCNG would let an unclassified preset match any body.
enum class BodyFamily { Cbbe3ba, Cbbe, Ube, Other, Unknown };

std::string_view family_id(BodyFamily family);     // "3ba", "cbbe", "ube", "other", "unknown"
std::string_view family_label(BodyFamily family);  // "CBBE 3BA", "CBBE", "UBE", "Otra reconocida", "Desconocida"
std::optional<BodyFamily> family_from_id(std::string_view id);
std::string mask_label(unsigned mask);              // BCNG's label: "CBBE 3BA / UBE", "Unclassified"

struct SliderSetting {
  std::string name;
  std::optional<float> small;  // BodySlide "small" (weight 0), percent as stored (0..100, may exceed)
  std::optional<float> big;    // BodySlide "big" (weight 100)
};

struct FamilyGuess {
  unsigned mask = 0;               // FamilyBit set; 0 = BCNG "Unclassified"
  bool conflict = false;           // female and male families in the same evidence
  bool male = false;               // mask holds male families
  bool metadata_fallback = false;  // decided by preset name + file path, not by set/groups
  BodyFamily family = BodyFamily::Unknown;
  std::string other_name;          // for Other: "BHUNP / UNP", "HIMBO", "SAM"
  std::string reason;              // the evidence that decided it, in plain language
};

// One <Preset> of a SliderPresets XML, exactly as the file declares it.
struct BodySlidePreset {
  std::string name;                 // <Preset name="...">: what Body Change NG / OBody reference
  std::string set;                  // <Preset set="...">: the BodySlide outfit/body set it was saved for
  std::vector<std::string> groups;  // <Group name="...">
  std::vector<SliderSetting> sliders;
  FamilyGuess family;
  std::string file;      // virtual Data path as found on disk ("CalienteTools\BodySlide\SliderPresets\X.xml")
  std::string source;    // path relative to SliderPresets with '/' (BCNG's "file" in qualified preset refs)
  std::string provider;  // MO2 mod name, "Overwrite" or the game Data provider name
  std::string uid;       // stable identity: data_path_key(file) + "|" + name (names alone can repeat)
  bool refit = false;    // name ends in "-Refit": BCNG never distributes it
};

struct PresetFileResult {
  std::vector<BodySlidePreset> presets;
  std::vector<std::string> warnings;  // presets skipped or values ignored, in plain language
};

// Parses one SliderPresets document. Throws sns::Error(Parse) when the XML itself is malformed or the
// root is not <SliderPresets>; a <Preset> without a name is skipped with a warning.
PresetFileResult parse_slider_presets(std::string_view xml_text, std::string_view virtual_file,
                                      std::string_view provider);

// Same decision as BCNG's ClassifyPreset: Preset/@set, then the Group names, then preset name + source path;
// the first evidence that names a family (or conflicts) decides. "UBE Anus" in set/group text is ignored.
FamilyGuess classify_family(const BodySlidePreset& preset);

struct PresetFileEntry {
  std::string file;                          // virtual path (winning copy)
  std::string provider;                      // winning provider
  std::vector<std::string> hidden_providers; // lower-priority providers whose copy of the same file is not used
  std::size_t presets = 0;
  std::vector<std::string> warnings;
  std::string error;                         // parse/read error; the file contributes no preset
};

// The same exact preset name in several winning files. BCNG then needs {"name","file"} to pick one.
struct DuplicateName {
  std::string name;
  std::vector<std::string> uids;  // every winning preset with this exact name
  bool identical = false;         // all copies declare the same sliders and values
};

struct PresetLibrary {
  std::vector<BodySlidePreset> presets;  // from winning files only, sorted by name then file
  std::vector<PresetFileEntry> files;
  std::vector<DuplicateName> duplicates;
  std::map<BodyFamily, std::size_t> family_counts;

  [[nodiscard]] const BodySlidePreset* find(std::string_view uid) const;
  [[nodiscard]] std::vector<const BodySlidePreset*> by_name(std::string_view name) const;  // exact, like BCNG
  [[nodiscard]] const DuplicateName* duplicate_of(std::string_view name) const;            // exact
};

inline constexpr std::string_view kSliderPresetsDir = "CalienteTools\\BodySlide\\SliderPresets";

// Reads every *.xml under CalienteTools\BodySlide\SliderPresets of the virtual Data folder, recursively like
// BCNG, the winning copy of each (MO2 priority), and records which providers lose. Files that are empty or
// larger than 8 MB are listed with an error, as BCNG skips them. `read_file` returns the file's bytes (tests
// inject it; the default reads from disk). Nothing is written.
using FileReader = std::function<std::string(const std::string& absolute_utf8_path)>;
PresetLibrary load_preset_library(const mo2::VirtualFileSystem& vfs, const FileReader& read_file = {});

// Builds the library from already parsed files (used by load_preset_library and tests).
PresetLibrary build_library(std::vector<PresetFileEntry> files, std::vector<BodySlidePreset> presets);

}  // namespace sns::body
