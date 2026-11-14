#include "sns/runtime/log_reader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"

namespace sns::runtime {

namespace {

const std::regex& form_id_regex() {
  static const std::regex re(R"(\b[0-9A-Fa-f]{8}\b)");
  return re;
}

const std::regex& plugin_regex() {
  static const std::regex re(R"([\w\-\[\] ]+\.(?:esp|esm|esl))", std::regex_constants::icase);
  return re;
}

std::string upper(std::string value) {
  for (auto& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

// Known patterns. Conservative on purpose: only documented, recurrent shapes of each log.
// pattern_id lets the correlator cite *which* rule fired without re-parsing the text.
struct Pattern {
  const char* id;
  LogKind kind;
  const char* regex_text;
};

const std::vector<Pattern>& patterns() {
  static const std::vector<Pattern> kPatterns = {
      // FSMP (hdtSMP64.log): initialization banner and load/parse failures.
      {"FSMP_VERSION", LogKind::kFsmp, R"(Faster HDT-SMP version )"},
      {"FSMP_XML_PARSE_ERROR", LogKind::kFsmp, R"(([Ee]rror|[Ff]ailed).*[Xx][Mm][Ll])"},
      {"FSMP_LOAD_FAILED", LogKind::kFsmp, R"(([Ff]ailed to load|[Cc]ould not load))"},
      {"FSMP_EXCEPTION", LogKind::kFsmp, R"([Ee]xception)"},
      // SKSE plugin loader.
      {"SKSE_PLUGIN_LOADED", LogKind::kSkse, R"(plugin .+ loaded)"},
      {"SKSE_PLUGIN_FAILED", LogKind::kSkse, R"(plugin .+ (couldn't be loaded|disabled|failed))"},
      {"SKSE_VERSION", LogKind::kSkse, R"(SKSE64 runtime: initialize)"},
      // SPID (po3_SpellPerkItemDistributor.log).
      {"SPID_ERROR", LogKind::kSpid, R"(ERROR)"},
      {"SPID_DISTRIBUTED", LogKind::kSpid, R"(([Dd]istributed|[Aa]dded).*\[0x[0-9A-Fa-f]+\])"},
      // Crash Logger: header line is enough to date a crash; we never parse stacks.
      {"CRASH_HEADER", LogKind::kCrashLogger, R"(Skyrim SSE v[0-9.]+)"},
      {"CRASH_PROBABLE_CALLSTACK", LogKind::kCrashLogger, R"(PROBABLE CALL STACK)"},
  };
  return kPatterns;
}

bool kind_matches(LogKind pattern_kind, LogKind scan_kind) {
  return pattern_kind == scan_kind || scan_kind == LogKind::kGeneric;
}

}  // namespace

LogScan scan_log(const std::filesystem::path& path, LogKind kind,
                 std::optional<std::filesystem::file_time_type> reference_time,
                 std::chrono::minutes max_gap) {
  LogScan scan;
  scan.kind = kind;
  scan.source = path;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    scan.warnings.push_back("log no encontrado: " + path.string());
    return scan;
  }
  scan.file_found = true;

  // Rotation evidence: FSMP/SKSE rotate the previous session to "<name>.log0".
  const std::filesystem::path rotated = path.string() + "0";
  if (std::filesystem::exists(rotated, ec) && !ec) {
    const auto log_time = std::filesystem::last_write_time(path, ec);
    const auto rotated_time = std::filesystem::last_write_time(rotated, ec);
    if (!ec && rotated_time > log_time) {
      scan.stale_session = true;
      scan.staleness_reason = "el archivo rotado '" + rotated.filename().string() +
                              "' es más reciente que el log: la evidencia no es de la sesión actual";
    }
  }

  if (reference_time && !scan.stale_session) {
    const auto log_time = std::filesystem::last_write_time(path, ec);
    if (!ec && *reference_time > log_time && (*reference_time - log_time) > max_gap) {
      scan.stale_session = true;
      scan.staleness_reason = "el log es anterior a la captura en más de " +
                              std::to_string(max_gap.count()) + " minutos";
    }
  }

  std::string content;
  try {
    content = file_io::read_text_file(path);
  } catch (const Error& e) {
    scan.warnings.push_back(std::string("no se pudo leer el log: ") + e.what());
    return scan;
  }

  // Compiled pattern set for this kind.
  std::vector<std::pair<std::string, std::regex>> active;
  for (const auto& p : patterns()) {
    if (kind_matches(p.kind, kind)) active.emplace_back(p.id, std::regex(p.regex_text));
  }

  std::istringstream stream(content);
  std::string line;
  long long line_number = 0;
  while (std::getline(stream, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string pattern_id;
    for (const auto& [id, re] : active) {
      if (std::regex_search(line, re)) {
        pattern_id = id;
        break;
      }
    }
    if (pattern_id.empty()) continue;

    LogEntry entry;
    entry.source = path;
    entry.line_number = line_number;
    entry.text = line;
    entry.pattern_id = pattern_id;
    for (std::sregex_iterator it(line.begin(), line.end(), form_id_regex()), end; it != end; ++it)
      entry.form_ids.push_back(upper(it->str()));
    for (std::sregex_iterator it(line.begin(), line.end(), plugin_regex()), end; it != end; ++it)
      entry.plugins.push_back(it->str());
    scan.entries.push_back(std::move(entry));
  }
  scan.total_lines = line_number;
  return scan;
}

std::vector<MentionMatch> find_npc_mentions(const LogScan& scan, const NpcMentions& npc) {
  std::vector<MentionMatch> matches;
  if (scan.stale_session) return matches;  // previous-session evidence is never conclusive
  const std::string wanted_id = upper(npc.local_form_id);
  for (const auto& entry : scan.entries) {
    bool strong = false;
    if (!wanted_id.empty())
      strong = std::find(entry.form_ids.begin(), entry.form_ids.end(), wanted_id) !=
               entry.form_ids.end();
    bool plugin_hit = false;
    if (!strong) {
      // Robust against over-greedy regex candidates: a plugin counts as mentioned when its exact
      // name appears anywhere in the original line (case-insensitive substring).
      std::string line = entry.text;
      std::transform(line.begin(), line.end(), line.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      for (const auto& plugin : npc.plugins) {
        std::string b = plugin;
        std::transform(b.begin(), b.end(), b.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (line.find(b) != std::string::npos) {
          plugin_hit = true;
          break;
        }
      }
    }
    if (strong || plugin_hit) matches.push_back({&entry, strong});
  }
  return matches;
}

}  // namespace sns::runtime
