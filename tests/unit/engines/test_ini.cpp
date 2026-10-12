#include <catch2/catch_test_macros.hpp>

#include "sns/mo2/ini.hpp"

using namespace sns::mo2;

// Excerpt of a real ModOrganizer.ini written by MO2 2.5.2 (paths changed).
static constexpr const char* kRealIni = R"([General]
gameName=Skyrim Special Edition
selected_profile=@ByteArray(Default)
gamePath=@ByteArray(D:\\Games\\steamapps\\common\\Skyrim Special Edition)
game_edition=Steam
version=2.5.2

[PluginPersistance]
Python%20Proxy\tryInit=false

[customExecutables]
size=1
1\binary=D:/Games/steamapps/common/Skyrim Special Edition/skse64_loader.exe
1\arguments=\"D:\\Games\\steamapps\\common\\Skyrim Special Edition\\data\"

[Settings]
base_directory=F:/Mods Skyrim
style=vs15 Dark.qss
overwrittenLooseFilesColor=@Variant(\0\0\0\x43\x1@@\0\0\xff\xff\0\0\0\0)
executable_blacklist="Chrome.exe;Firefox.exe;Steam.exe"
skip_file_suffixes=.mohidden
category="-1,"
list=a, b,c
)";

TEST_CASE("Real MO2 ini values are decoded", "[mo2][ini]") {
  const auto ini = IniDocument::parse(kRealIni);
  CHECK(ini.value("General", "gameName") == "Skyrim Special Edition");
  CHECK(ini.value("General", "selected_profile") == "Default");
  CHECK(ini.value("General", "gamePath") == R"(D:\Games\steamapps\common\Skyrim Special Edition)");
  CHECK(ini.value("Settings", "base_directory") == "F:/Mods Skyrim");
  CHECK(ini.value("Settings", "executable_blacklist") == "Chrome.exe;Firefox.exe;Steam.exe");
  CHECK(ini.value("Settings", "category") == "-1,");
}

TEST_CASE("Keys are percent-decoded and sub-groups use '/'", "[mo2][ini]") {
  const auto ini = IniDocument::parse(kRealIni);
  CHECK(ini.value("PluginPersistance", "Python Proxy/tryInit") == "false");
  CHECK(ini.value("customExecutables", "1/binary") == "D:/Games/steamapps/common/Skyrim Special Edition/skse64_loader.exe");
  CHECK(ini.value("customExecutables", "1/arguments") == R"("D:\Games\steamapps\common\Skyrim Special Edition\data")");
}

TEST_CASE("Binary @Variant values are not exposed as text; lists are split", "[mo2][ini]") {
  const auto ini = IniDocument::parse(kRealIni);
  CHECK_FALSE(ini.value("Settings", "overwrittenLooseFilesColor").has_value());
  CHECK(ini.list("Settings", "list") == std::vector<std::string>{"a", "b", "c"});
  CHECK_FALSE(ini.value("Missing", "x").has_value());
}

TEST_CASE("Escapes and quoted spaces", "[mo2][ini]") {
  CHECK(unescape_ini_value(R"("  padded  ")") == std::vector<std::string>{"  padded  "});
  CHECK(unescape_ini_value(R"(tab\there)") == std::vector<std::string>{"tab\there"});
  CHECK(unescape_ini_value(R"(\x00e9t\x00e9)") == std::vector<std::string>{"\xC3\xA9t\xC3\xA9"});
  CHECK(unescape_ini_value("") == std::vector<std::string>{""});
  CHECK(decode_ini_key("Foo%20Bar\\baz") == "Foo Bar/baz");
}
