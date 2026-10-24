#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "sns/body/bodyslide_presets.hpp"
#include "sns/body/xml_lite.hpp"
#include "sns/core/error.hpp"
#include "sns/mo2/vfs.hpp"
#include "temp_dir.hpp"

using namespace sns;
using namespace sns::body;

namespace {

constexpr auto k3ba = R"(<?xml version="1.0" encoding="UTF-8"?>
<!-- saved by BodySlide -->
<SliderPresets>
    <Preset name="Curvy &amp; Toned" set="CBBE 3BBB Body Amazing">
        <Group name="CBBE"/>
        <Group name="3BA"/>
        <SetSlider name="Breasts" size="small" value="40"/>
        <SetSlider name="Breasts" size="big" value="65"/>
        <SetSlider name="Butt" size="big" value="0"/>
        <SetSlider name="Butt" size="big" value="-10"/>
        <SetSlider name="Butt" size="big" value="25"/>
    </Preset>
    <Preset name="CBBE Classic" set="CBBE Body">
        <Group name="CBBE"/>
        <SetSlider name="Breasts" size="small" value="10"/>
    </Preset>
</SliderPresets>
)";

constexpr auto kUbe = R"(<SliderPresets>
  <Preset name="UBE Athletic" set="UBE Body">
    <Group name="UBE"/>
    <SetSlider name="Breasts" size="small" value="30"/>
    <SetSlider name="Breasts" size="big" value="50"/>
  </Preset>
  <Preset name="Curvy &amp; Toned" set="UBE Body"><Group name="UBE"/></Preset>
</SliderPresets>)";

FamilyGuess classify(std::string set, std::vector<std::string> groups, std::string name = {}, std::string source = {}) {
  BodySlidePreset p;
  p.set = std::move(set);
  p.groups = std::move(groups);
  p.name = std::move(name);
  p.source = std::move(source);
  return classify_family(p);
}

}  // namespace

TEST_CASE("BodySlide XML: presets, groups, sliders and entities are read as declared", "[body]") {
  const auto r = parse_slider_presets(k3ba, "CalienteTools\\BodySlide\\SliderPresets\\Sub\\Pack.xml", "Pack Mod");
  REQUIRE(r.presets.size() == 2);
  const auto& p = r.presets[0];
  CHECK(p.name == "Curvy & Toned");
  CHECK(p.set == "CBBE 3BBB Body Amazing");
  CHECK(p.groups == std::vector<std::string>{"CBBE", "3BA"});
  REQUIRE(p.sliders.size() == 2);
  CHECK(p.sliders[0].name == "Breasts");
  CHECK(p.sliders[0].small == 40.0f);
  CHECK(p.sliders[0].big == 65.0f);
  CHECK_FALSE(p.sliders[1].small.has_value());
  CHECK(p.sliders[1].big == -10.0f);  // BCNG/OBody: a zero endpoint is replaced once, then kept
  CHECK(p.provider == "Pack Mod");
  CHECK(p.source == "Sub/Pack.xml");  // what BCNG's qualified {"name","file"} reference expects
  CHECK(p.uid == "calientetools\\bodyslide\\sliderpresets\\sub\\pack.xml|Curvy & Toned");
  CHECK_FALSE(p.refit);
  CHECK(r.warnings.empty());
}

TEST_CASE("BodySlide XML: family classification follows BCNG 1.3.7 (set, then groups, then name/file)", "[body]") {
  const auto r3 = parse_slider_presets(k3ba, "a.xml", "m");
  CHECK(r3.presets[0].family.family == BodyFamily::Cbbe3ba);
  CHECK(r3.presets[0].family.mask == kFamCbbe);
  CHECK(r3.presets[1].family.family == BodyFamily::Cbbe);  // same BCNG family, plain CBBE set
  CHECK(r3.presets[1].family.mask == kFamCbbe);
  const auto ru = parse_slider_presets(kUbe, "b.xml", "m");
  CHECK(ru.presets[0].family.family == BodyFamily::Ube);
  CHECK(ru.presets[0].family.mask == kFamUbe);

  // The set decides before the groups.
  CHECK(classify("UBE 2.0 Body", {"3BA"}).family == BodyFamily::Ube);
  // "UBE Anus" inside a 3BA set name is not a UBE claim.
  const auto anus = classify("CBBE 3BBB Body Amazing UBE Anus", {});
  CHECK(anus.mask == kFamCbbe);
  CHECK(anus.family == BodyFamily::Cbbe3ba);
  // Several female families in one evidence: kept as a mask, shown as Unknown (no automatic use here).
  const auto both = classify("", {"UBE", "3BA"});
  CHECK(both.mask == (kFamUbe | kFamCbbe));
  CHECK(both.family == BodyFamily::Unknown);
  CHECK(mask_label(both.mask) == "CBBE 3BA / UBE");
  // Female and male families together: BCNG conflict -> Unclassified.
  const auto conflict = classify("", {"CBBE", "HIMBO"});
  CHECK(conflict.conflict);
  CHECK(conflict.mask == 0);
  // 3BBB alone belongs to both CBBE and BHUNP worlds and never decides; BHUNP does.
  CHECK(classify("", {"BHUNP 3BBB Advanced"}).mask == kFamUnp);
  CHECK(classify("", {"3BBB"}).mask == 0);
  const auto himbo = classify("", {"HIMBO"});
  CHECK(himbo.family == BodyFamily::Other);
  CHECK(himbo.male);
  CHECK(himbo.other_name == "HIMBO");
  // Whole tokens for "ube": "CUBE" is not UBE.
  CHECK(classify("", {"CUBE presets"}).mask == 0);
  // Last resort: preset name + file path, flagged as such.
  const auto meta = classify("", {}, "Tiamat UBE", "Tiamat.xml");
  CHECK(meta.family == BodyFamily::Ube);
  CHECK(meta.metadata_fallback);
  CHECK(classify("Some Outfit", {"My Favourites"}, "Nice", "Nice.xml").family == BodyFamily::Unknown);
}

TEST_CASE("BodySlide XML: malformed files and incomplete entries are reported, not guessed", "[body]") {
  CHECK_THROWS_AS(parse_slider_presets("<SliderPresets><Preset name=\"x\">", "a.xml", "m"), Error);
  CHECK_THROWS_AS(parse_slider_presets("<SliderGroups/>", "a.xml", "m"), Error);
  CHECK_THROWS_AS(parse_slider_presets("", "a.xml", "m"), Error);
  CHECK_THROWS_AS(parse_xml("<a b=\"1\" b=\"2\"/>"), Error);
  CHECK_THROWS_AS(parse_xml("<a b=\"&bogus;\"/>"), Error);
  CHECK(parse_xml("<a b=\"&#x41;&#66;\"/>").attribute("b") == "AB");

  const auto r = parse_slider_presets(R"(<SliderPresets>
    <Preset set="no name"/>
    <Preset name="P"><SetSlider name="Breasts" size="huge" value="1"/><SetSlider name="Butt" size="big" value="abc"/></Preset>
    <Preset name="P"/>
    <Preset name="p"/>
    <Preset name="Outfit-Refit" set="CBBE Body"/>
  </SliderPresets>)", "a.xml", "m");
  REQUIRE(r.presets.size() == 3);           // "P", "p" (names are case-sensitive), "Outfit-Refit"
  CHECK(r.presets[0].sliders.size() == 1);  // "Breasts" kept with no values, "Butt" ignored
  CHECK(r.presets[2].refit);
  CHECK(r.warnings.size() == 4);            // nameless, unknown size, non-numeric, exact duplicate within file
}

TEST_CASE("Preset library: recursive scan, MO2 winners, overridden copies and duplicate names", "[body]") {
  test::TempDir dir;
  dir.write("modA/CalienteTools/BodySlide/SliderPresets/Pack.xml", "<SliderPresets/>");  // loses to modB
  dir.write("modB/CalienteTools/BodySlide/SliderPresets/Pack.xml", k3ba);
  dir.write("modC/CalienteTools/BodySlide/SliderPresets/UBE.xml", kUbe);
  dir.write("modC/CalienteTools/BodySlide/SliderPresets/Broken.xml", "<SliderPresets><Preset");
  dir.write("modC/CalienteTools/BodySlide/SliderPresets/readme.txt", "not xml");
  dir.write("modC/CalienteTools/BodySlide/SliderPresets/Sub/Nested.xml", kUbe);  // BCNG scans recursively
  dir.write("modC/CalienteTools/BodySlide/SliderGroups/Groups.xml", "<SliderGroups/>");

  const auto vfs = mo2::VirtualFileSystem::build_from({
      {"Mod A", dir.path() / "modA", mo2::ProviderKind::Mod, 0},
      {"Mod B", dir.path() / "modB", mo2::ProviderKind::Mod, 0},
      {"Mod C", dir.path() / "modC", mo2::ProviderKind::Mod, 0},
  });
  CHECK(vfs.list_directory("calientetools/bodyslide/sliderpresets/").size() == 4);
  CHECK(vfs.list_directory("calientetools/bodyslide/sliderpresets", true).size() == 5);

  const auto lib = load_preset_library(vfs);
  REQUIRE(lib.files.size() == 4);
  const auto pack = std::find_if(lib.files.begin(), lib.files.end(), [](const auto& f) { return f.file.ends_with("Pack.xml"); });
  REQUIRE(pack != lib.files.end());
  CHECK(pack->provider == "Mod B");
  CHECK(pack->hidden_providers == std::vector<std::string>{"Mod A"});
  const auto broken = std::find_if(lib.files.begin(), lib.files.end(), [](const auto& f) { return f.file.ends_with("Broken.xml"); });
  REQUIRE(broken != lib.files.end());
  CHECK_FALSE(broken->error.empty());

  CHECK(lib.presets.size() == 6);
  CHECK(lib.family_counts.at(BodyFamily::Cbbe3ba) == 1);
  CHECK(lib.family_counts.at(BodyFamily::Cbbe) == 1);
  CHECK(lib.family_counts.at(BodyFamily::Ube) == 4);

  // Same exact name in several files: all kept, told apart by file and provider.
  REQUIRE(lib.duplicates.size() == 2);
  const auto* curvy = lib.duplicate_of("Curvy & Toned");
  REQUIRE(curvy != nullptr);
  CHECK(curvy->uids.size() == 3);
  CHECK_FALSE(curvy->identical);
  const auto* athletic = lib.duplicate_of("UBE Athletic");
  REQUIRE(athletic != nullptr);
  CHECK(athletic->identical);  // UBE.xml and Sub/Nested.xml are the same content
  CHECK(lib.duplicate_of("curvy & toned") == nullptr);  // BCNG names are case-sensitive
  const auto same = lib.by_name("Curvy & Toned");
  REQUIRE(same.size() == 3);
  CHECK(lib.find(same[2]->uid) == same[2]);
  const auto nested = std::find_if(same.begin(), same.end(), [](const auto* p) { return p->source == "Sub/Nested.xml"; });
  CHECK(nested != same.end());
}
