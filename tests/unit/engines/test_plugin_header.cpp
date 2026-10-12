#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "plugin_bytes.hpp"
#include "sns/core/error.hpp"
#include "sns/mo2/plugin_header.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::mo2;

TEST_CASE("TES4 header: flags, masters and extension semantics", "[mo2][plugin]") {
  const auto bytes = test::make_tes4(0x200, {"Skyrim.esm", "Update.esm"}, 1.71f, "Juan");
  const auto esp = parse_plugin_header(bytes, "Light.esp");
  CHECK(esp.light_flag);
  CHECK(esp.is_light);
  CHECK_FALSE(esp.is_master);
  CHECK(esp.masters == std::vector<std::string>{"Skyrim.esm", "Update.esm"});
  CHECK(esp.author == "Juan");
  CHECK_THAT(esp.version, Catch::Matchers::WithinAbs(1.71, 1e-5));

  const auto esl = parse_plugin_header(test::make_tes4(0, {}), "Thing.esl");
  CHECK(esl.is_light);
  CHECK(esl.is_master);  // .esl files are always masters

  const auto esm = parse_plugin_header(test::make_tes4(0x1, {}), "Master.esp");
  CHECK(esm.is_master);
}

TEST_CASE("Corrupt headers are rejected", "[mo2][plugin][security]") {
  CHECK_THROWS_AS(parse_plugin_header("TES3xxxxxxxxxxxxxxxxxxxxxxxx", "a.esp"), Error);
  auto bytes = test::make_tes4(0, {"Skyrim.esm"});
  CHECK_THROWS_AS(parse_plugin_header(bytes.substr(0, bytes.size() - 3), "a.esp"), Error);
  bytes[4] = '\xff';  // data size far beyond the buffer
  bytes[5] = '\xff';
  bytes[6] = '\xff';
  CHECK_THROWS_AS(parse_plugin_header(bytes, "a.esp"), Error);
}

TEST_CASE("Headers are read from disk without loading the whole file", "[mo2][plugin]") {
  test::TempDir dir;
  const auto file = dir.write("Mod.esp", test::make_tes4(0, {"Skyrim.esm"}) + std::string(1 << 20, 'R'));
  const auto h = read_plugin_header(file);
  CHECK(h.masters == std::vector<std::string>{"Skyrim.esm"});
  CHECK_THROWS_AS(read_plugin_header(dir.path() / "missing.esp"), Error);
}
