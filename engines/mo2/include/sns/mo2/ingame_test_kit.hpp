#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sns/mo2/load_order.hpp"

namespace sns::mo2 {

// "0A2C8E:Skyrim.esm" -> {0x0A2C8E, "Skyrim.esm"}. The local id must fit in 24 bits; nullopt for anything else.
struct FormKeyRef {
  std::uint32_t local_id = 0;
  std::string plugin;
};
std::optional<FormKeyRef> parse_form_key(std::string_view text);

// The FormID the game assigns at runtime to `local_id` of `plugin` under this load order (the number the console
// takes): full plugins 0xIILLLLLL (II = index among active non-light plugins, at most 0xFD); light plugins
// 0xFEIIILLL (III = index among active light plugins, LLL = the 12-bit local id). Either `id` or `error`.
struct RuntimeFormId {
  std::optional<std::uint32_t> id;
  std::string error;
};
RuntimeFormId runtime_form_id(const EffectiveLoadOrder& load_order, std::string_view plugin, std::uint32_t local_id);
std::string format_form_id(std::uint32_t id);  // 8 upper-case hex digits

// What the in-game test needs from an export's manifest.json (format "skyrim-npc-studio/export").
struct ExportedRecordRef {
  std::string form_key;
  std::string record_type;
  std::string editor_id;
  std::string kind;  // "new" / "override"
};
struct ExportManifestSummary {
  std::string format;
  std::string mode;
  std::string plugin_file;
  std::string plugin_sha256;
  bool esl_flagged = false;
  std::vector<ExportedRecordRef> records;
};
// Throws sns::Error(Parse) when the bytes are not an export manifest (a UTF-8 BOM is accepted).
ExportManifestSummary parse_export_manifest(std::string_view bytes);

// Console command the kit uses for a record type: "player.placeatme" (actors and placeable objects),
// "player.additem" (inventory items) or empty (no console command tests it).
std::string console_command_for(std::string_view record_type);

struct InGameTestLine {
  ExportedRecordRef record;
  std::string runtime_form_id;  // empty when it could not be computed
  std::string command;          // full console line, empty when the record has no console test
  std::string note;
};

struct InGameTestKitContext {
  std::string profile;                  // MO2 profile the load order belongs to
  std::string installed_plugin_sha256;  // SHA-256 of the plugin file the profile loads (empty: not readable)
  std::string created_utc;              // ISO 8601
  std::string export_folder;            // where the exported mod was read from (for the report)
};

// The batch file and the acceptance report for one export under one profile. `ok` is false (and nothing should be
// packaged) when the plugin is not the active one of the profile, its bytes differ from the export, or no record can
// be tested from the console. Commands never include "coc" (UESP: it crashes the game inside a batch file).
struct InGameTestKit {
  bool ok = false;
  std::vector<std::string> errors;
  std::string kit_name;     // SNS_Test_<plugin stem>, a safe MO2 mod / console name
  std::string batch_file;   // <kit_name>.txt, placed at the root of Data
  std::string batch_text;   // CRLF lines, commands only
  std::string report_file;  // ACTA_<kit_name>.md
  std::string report_text;
  std::vector<InGameTestLine> lines;
};
InGameTestKit build_ingame_test_kit(const ExportManifestSummary& manifest, const EffectiveLoadOrder& load_order,
                                    const InGameTestKitContext& context);

}  // namespace sns::mo2
