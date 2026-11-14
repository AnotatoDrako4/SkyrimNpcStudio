#pragma once

#include <string>
#include <vector>

namespace sns::appearance {

struct FsmpVersionCompatibility {
  std::string game_version;  // e.g. "1.6.1170"
  std::string skse_version;  // e.g. "2.2.6"
  std::string fsmp_dll_version;
  bool is_compatible = true;
  std::string note;
};

struct FsmpPreflightIssue {
  std::string code;
  std::string severity;
  std::string message;
  std::string file_path;
  std::string suggestion;
};

struct FsmpPreflightReport {
  bool fsmp_dll_found = false;
  std::string fsmp_dll_path;
  FsmpVersionCompatibility version_check;
  bool has_critical_errors = false;
  std::vector<FsmpPreflightIssue> issues;
  std::vector<std::string> parsed_log_warnings;
  std::vector<std::string> attached_fsmp_diagnostics;
};

// Validates the FSMP environment: DLL presence, version compatibility, and parses log lines if provided
FsmpPreflightReport run_fsmp_preflight_check(
    bool fsmp_dll_present,
    const std::string& game_version,
    const std::string& skse_version,
    const std::vector<std::string>& log_lines = {},
    const std::vector<std::string>& external_diagnostics = {});

// Parses raw log text from hdtSMP64.log
std::vector<std::string> parse_fsmp_log_lines(const std::string& log_content);

}  // namespace sns::appearance
