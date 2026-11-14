#include "sns/appearance/fsmp_preflight.hpp"

#include <algorithm>
#include <sstream>

namespace sns::appearance {

std::vector<std::string> parse_fsmp_log_lines(const std::string& log_content) {
  std::vector<std::string> lines;
  std::istringstream stream(log_content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

FsmpPreflightReport run_fsmp_preflight_check(
    bool fsmp_dll_present,
    const std::string& game_version,
    const std::string& skse_version,
    const std::vector<std::string>& log_lines,
    const std::vector<std::string>& external_diagnostics) {
  FsmpPreflightReport report;
  report.fsmp_dll_found = fsmp_dll_present;
  report.fsmp_dll_path = "SKSE\\Plugins\\hdtSMP64.dll";
  report.attached_fsmp_diagnostics = external_diagnostics;

  report.version_check.game_version = game_version;
  report.version_check.skse_version = skse_version;

  // 1. Check DLL Presence
  if (!fsmp_dll_present) {
    report.has_critical_errors = true;
    report.version_check.is_compatible = false;
    FsmpPreflightIssue issue;
    issue.code = "HAIR_SMP_008";
    issue.severity = "alta";
    issue.message = "Falta el binario de Faster HDT-SMP (hdtSMP64.dll) en SKSE\\Plugins\\.";
    issue.file_path = report.fsmp_dll_path;
    issue.suggestion = "Instala Faster HDT-SMP compatible con tu versión de Skyrim y SKSE.";
    report.issues.push_back(std::move(issue));
    return report;
  }

  // 2. Version Compatibility Check
  // Skyrim 1.6.1170 expects SKSE 2.2.6
  if (!game_version.empty() && !skse_version.empty()) {
    if (game_version.find("1.6.1170") != std::string::npos && skse_version.find("2.2.6") == std::string::npos) {
      report.version_check.is_compatible = false;
      FsmpPreflightIssue issue;
      issue.code = "HAIR_SMP_008";
      issue.severity = "alta";
      issue.message = "Discrepancia de versión: Skyrim 1.6.1170 requiere SKSE 2.2.6, detectado: " + skse_version;
      issue.file_path = "skse64_1_6_1170.dll";
      issue.suggestion = "Actualiza SKSE a la versión 2.2.6 compatible con runtime 1.6.1170.";
      report.issues.push_back(std::move(issue));
    } else {
      report.version_check.is_compatible = true;
      report.version_check.note = "Versión de Skyrim " + game_version + " y SKSE " + skse_version + " compatibles con FSMP.";
    }
  }

  // 3. Parse Log Lines for warnings or errors
  for (const auto& line : log_lines) {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos || lower.find("crash") != std::string::npos) {
      report.parsed_log_warnings.push_back(line);
      FsmpPreflightIssue issue;
      issue.code = "HAIR_SMP_008";
      issue.severity = "media";
      issue.message = "Mensaje de advertencia o error en registro FSMP: " + line;
      issue.file_path = "SKSE\\hdtSMP64.log";
      issue.suggestion = "Inspeccionar las dependencias de malla y XML reportadas en el registro.";
      report.issues.push_back(std::move(issue));
    }
  }

  return report;
}

}  // namespace sns::appearance
