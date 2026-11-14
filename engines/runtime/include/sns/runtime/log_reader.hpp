#pragma once

// Robust reader for the Skyrim-side logs relevant to NPC hair/SMP diagnostics.
//
// Rules (mission K3 / FASE 4):
//  - Recognizes only known patterns; an unmatched warning is never classified as a defect.
//  - Preserves the original line and its source file.
//  - Detects stale-session evidence: a log whose mtime is far from the runtime capture, or a rotated
//    ".log0" sibling newer than the log itself, is marked, and its entries are not conclusive.
//  - Correlates entries with an NPC only when a concrete mention exists (local FormID hex, plugin
//    name). Everything else stays "no concluyente".

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sns::runtime {

enum class LogKind { kSkse, kFsmp, kSpid, kCrashLogger, kGeneric };

struct LogEntry {
  std::filesystem::path source;
  long long line_number = 0;
  std::string text;                  // original line, unmodified
  std::string pattern_id;            // known-pattern identifier, empty when unrecognized
  std::vector<std::string> form_ids;  // 8-digit hex mentions (uppercased)
  std::vector<std::string> plugins;   // *.esp/.esm/.esl mentions
};

struct LogScan {
  LogKind kind = LogKind::kGeneric;
  std::filesystem::path source;
  bool file_found = false;
  bool stale_session = false;     // evidence belongs to another run of the game
  std::string staleness_reason;
  std::vector<LogEntry> entries;  // only lines matching known patterns
  long long total_lines = 0;
  std::vector<std::string> warnings;
};

struct NpcMentions {
  std::string local_form_id;             // "013BB9" uppercased; empty disables form-id matching
  std::vector<std::string> plugins;      // plugin names to match, e.g. {"Skyrim.esm"}
};

// Scans one log file. `reference_time` is the moment of the runtime capture (or now); a log whose
// last write is more than `max_gap` older is treated as a previous session. When `reference_time`
// is nullopt, session freshness is not evaluated (entries keep being returned, marked fresh=false
// only via .log0 rotation evidence).
LogScan scan_log(const std::filesystem::path& path, LogKind kind,
                 std::optional<std::filesystem::file_time_type> reference_time = std::nullopt,
                 std::chrono::minutes max_gap = std::chrono::minutes(30));

// Filters entries mentioning the NPC (form id or any of its plugins). Plugin-only matches are
// weaker: `strong` is true only for form-id matches.
struct MentionMatch {
  const LogEntry* entry = nullptr;
  bool strong = false;  // form-id mention (plugin-only mention = false)
};
std::vector<MentionMatch> find_npc_mentions(const LogScan& scan, const NpcMentions& npc);

}  // namespace sns::runtime
