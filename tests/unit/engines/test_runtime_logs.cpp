#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "sns/runtime/log_reader.hpp"

using namespace sns::runtime;

namespace {

std::filesystem::path temp_dir(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "sns-runtime-tests" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void write(const std::filesystem::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

void backdate(const std::filesystem::path& path, std::chrono::hours age) {
  const auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(path, now - age);
}

}  // namespace

TEST_CASE("logs: FSMP version and XML error lines are recognized, text preserved",
          "[runtime-logs]") {
  const auto dir = temp_dir("fsmp-basic");
  const auto log = dir / "hdtSMP64.log";
  write(log,
        "Faster HDT-SMP version 2.5.1.0\n"
        "some chatter line that matches nothing\n"
        "Error parsing XML for meshes\\ks\\hair01.xml line 42\n"
        "Failed to load configs for NPC 00013BB9 (Skyrim.esm)\n");

  const auto scan = scan_log(log, LogKind::kFsmp);
  REQUIRE(scan.file_found);
  CHECK(scan.total_lines == 4);
  REQUIRE(scan.entries.size() == 3);
  CHECK(scan.entries[0].pattern_id == "FSMP_VERSION");
  CHECK(scan.entries[1].pattern_id == "FSMP_XML_PARSE_ERROR");
  CHECK(scan.entries[2].pattern_id == "FSMP_LOAD_FAILED");
  // Original line preserved verbatim.
  CHECK(scan.entries[1].text == "Error parsing XML for meshes\\ks\\hair01.xml line 42");
  // Mentions extracted.
  REQUIRE(scan.entries[2].form_ids.size() == 1);
  CHECK(scan.entries[2].form_ids[0] == "00013BB9");
  REQUIRE(scan.entries[2].plugins.size() == 1);
}

TEST_CASE("logs: strong form-id mention beats plugin-only mention", "[runtime-logs]") {
  const auto dir = temp_dir("mentions");
  const auto log = dir / "hdtSMP64.log";
  write(log,
        "Failed to load configs for NPC 00013BB9 (Skyrim.esm)\n"
        "Error parsing XML for NITHI NPC Enhancement - Whiterun.esp file\n");

  const auto scan = scan_log(log, LogKind::kFsmp);
  NpcMentions npc;
  npc.local_form_id = "00013BB9";
  npc.plugins = {"Skyrim.esm", "NITHI NPC Enhancement - Whiterun.esp"};

  const auto matches = find_npc_mentions(scan, npc);
  REQUIRE(matches.size() == 2);
  CHECK(matches[0].strong);        // form-id match
  CHECK_FALSE(matches[1].strong);  // plugin-only match
}

TEST_CASE("logs: rotated .log0 newer than the log marks a stale session", "[runtime-logs]") {
  const auto dir = temp_dir("stale");
  const auto log = dir / "hdtSMP64.log";
  const auto rotated = dir / "hdtSMP64.log0";
  write(log, "Failed to load configs for NPC 00013BB9 (Skyrim.esm)\n");
  write(rotated, "older session\n");
  backdate(log, std::chrono::hours(2));  // the live log is older than its rotation

  const auto scan = scan_log(log, LogKind::kFsmp);
  CHECK(scan.stale_session);
  CHECK_FALSE(scan.staleness_reason.empty());

  // Stale evidence never produces mentions.
  NpcMentions npc;
  npc.local_form_id = "00013BB9";
  CHECK(find_npc_mentions(scan, npc).empty());
}

TEST_CASE("logs: log much older than the capture is another session", "[runtime-logs]") {
  const auto dir = temp_dir("old-session");
  const auto log = dir / "hdtSMP64.log";
  write(log, "Faster HDT-SMP version 2.5.1.0\n");
  backdate(log, std::chrono::hours(5));

  const auto capture = std::filesystem::file_time_type::clock::now();
  const auto scan = scan_log(log, LogKind::kFsmp, capture, std::chrono::minutes(30));
  CHECK(scan.stale_session);
}

TEST_CASE("logs: corrupt/binary log does not throw and yields no findings", "[runtime-logs]") {
  const auto dir = temp_dir("corrupt");
  const auto log = dir / "hdtSMP64.log";
  std::string garbage(4096, '\x01');
  garbage += "\xFF\xFE\x00\x00\xC3\x28";  // invalid UTF-8 tail
  write(log, garbage);

  const auto scan = scan_log(log, LogKind::kFsmp);
  CHECK(scan.file_found);
  CHECK(scan.entries.empty());  // nothing recognizable: nothing reported
}

TEST_CASE("logs: missing file is a warning, not an exception", "[runtime-logs]") {
  const auto dir = temp_dir("missing");
  const auto scan = scan_log(dir / "no-existe.log", LogKind::kFsmp);
  CHECK_FALSE(scan.file_found);
  REQUIRE(scan.warnings.size() == 1);
}

TEST_CASE("logs: crash logger header and callstack marker recognized", "[runtime-logs]") {
  const auto dir = temp_dir("crash");
  const auto log = dir / "CrashLogger.log";
  write(log,
        "Skyrim SSE v1.6.1170\n"
        "CrashLogger SSE v1-15-0-0\n"
        "PROBABLE CALL STACK:\n"
        "\t[0x7FF6AABBCCDD] SkyrimSE.exe+0x12345\n");

  const auto scan = scan_log(log, LogKind::kCrashLogger);
  REQUIRE(scan.entries.size() == 2);
  CHECK(scan.entries[0].pattern_id == "CRASH_HEADER");
  CHECK(scan.entries[1].pattern_id == "CRASH_PROBABLE_CALLSTACK");
}

TEST_CASE("logs: unmatched warnings are never classified as defects", "[runtime-logs]") {
  const auto dir = temp_dir("warnings-only");
  const auto log = dir / "hdtSMP64.log";
  write(log,
        "warning: something mildly unusual happened\n"
        "WARN: another generic complaint\n"
        "note: physics seems unhappy today\n");

  const auto scan = scan_log(log, LogKind::kFsmp);
  CHECK(scan.entries.empty());  // known patterns only; these stay unreported
}
