#include <catch2/catch_test_macros.hpp>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "temp_dir.hpp"

using namespace sns;

TEST_CASE("UTF-8 round-trips through UTF-16", "[core]") {
  const std::string s = "Mods Skyrim/Ñandú – Lydia ✓";
  CHECK(text::to_utf8(text::to_wide(s)) == s);
  CHECK(text::path_to_utf8(text::path_from_utf8(s)) == s);
  CHECK_THROWS_AS(text::to_wide("\xff\xfe broken"), Error);
}

TEST_CASE("decode_text accepts UTF-8 with BOM and falls back to Windows-1252", "[core]") {
  CHECK(text::decode_text("\xEF\xBB\xBFSkyrim.esm") == "Skyrim.esm");
  CHECK(text::decode_text("Caf\xE9.esp") == "Caf\xC3\xA9.esp");  // 0xE9 in cp1252 is U+00E9
  CHECK(text::decode_text("Caf\xC3\xA9.esp") == "Caf\xC3\xA9.esp");
}

TEST_CASE("Data path keys are case-insensitive and separator-agnostic", "[core]") {
  CHECK(text::data_path_key("Meshes/Actors\\Character/FaceGenData") == "meshes\\actors\\character\\facegendata");
  CHECK(text::data_path_key("\\Skyrim.esm") == "skyrim.esm");
}

TEST_CASE("Unsafe relative paths are rejected", "[core][security]") {
  CHECK(text::is_safe_relative_path("meshes\\armor\\a.nif"));
  CHECK_FALSE(text::is_safe_relative_path(""));
  CHECK_FALSE(text::is_safe_relative_path("..\\evil.dll"));
  CHECK_FALSE(text::is_safe_relative_path("meshes/../../evil"));
  CHECK_FALSE(text::is_safe_relative_path("C:\\Windows"));
  CHECK_FALSE(text::is_safe_relative_path("\\root"));
}

TEST_CASE("split_lines handles CRLF and a missing final newline", "[core]") {
  const auto lines = text::split_lines("a\r\nb\n\nc");
  REQUIRE(lines.size() == 4);
  CHECK(lines[0] == "a");
  CHECK(lines[2].empty());
  CHECK(lines[3] == "c");
}

TEST_CASE("write_file_atomic replaces content and leaves no temp files", "[core]") {
  test::TempDir dir;
  const auto file = dir.path() / "sub" / "project.json";
  file_io::write_file_atomic(file, "one");
  file_io::write_file_atomic(file, "two");
  CHECK(file_io::read_file(file) == "two");
  int entries = 0;
  for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(file.parent_path())) ++entries;
  CHECK(entries == 1);
}

TEST_CASE("read_file enforces the size limit", "[core][security]") {
  test::TempDir dir;
  const auto file = dir.write("big.txt", std::string(2048, 'x'));
  CHECK_THROWS_AS(file_io::read_file(file, 1024), Error);
  CHECK(file_io::read_file(file, 4096).size() == 2048);
  CHECK_THROWS_AS(file_io::read_file(dir.path() / "missing.txt"), Error);
}
