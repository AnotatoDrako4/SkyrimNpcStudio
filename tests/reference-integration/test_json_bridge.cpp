#include <QCoreApplication>
#include <QJSEngine>
#include <QJSValue>
#include <QVariant>
#include <catch2/catch_test_macros.hpp>

#include "json_bridge.hpp"

using sns::app::to_json;

namespace {
// QJSEngine needs a Qt application object (Catch2WithMain's main creates none).
struct QtApp {
  int argc = 1;
  char name[2] = "t";
  char* argv[2] = {name, nullptr};
  QCoreApplication app{argc, argv};
};
const QtApp g_app;
}  // namespace

// AG04: a list chosen in the Character Forge (spells, perks, packages, factions) reaches C++ as a JS array. When it arrives wrapped in a
// QJSValue, QJsonValue::fromVariant used to turn it into null and the edit was refused ("type must be array, but is null").

TEST_CASE("to_json keeps a JS array that QML handed over", "[json][forge]") {
  QJSEngine engine;
  auto array = engine.newArray(2);
  array.setProperty(0, QStringLiteral("00234C:Skyrim.esm"));
  array.setProperty(1, QStringLiteral("00234D:Skyrim.esm"));
  const auto json = to_json(QVariant::fromValue(array));
  REQUIRE(json.is_array());
  REQUIRE(json.size() == 2);
  CHECK(json[0] == "00234C:Skyrim.esm");
  CHECK(json[1] == "00234D:Skyrim.esm");
}

TEST_CASE("to_json keeps a JS object, nested arrays included", "[json][forge]") {
  QJSEngine engine;
  const auto object = engine.evaluate(QStringLiteral("({ formKey: '05C84D:Skyrim.esm', rank: 0, tags: ['a', 'b'] })"));
  REQUIRE_FALSE(object.isError());
  const auto json = to_json(QVariant::fromValue(object));
  REQUIRE(json.is_object());
  CHECK(json["formKey"] == "05C84D:Skyrim.esm");
  CHECK(json["rank"] == 0);
  REQUIRE(json["tags"].is_array());
  CHECK(json["tags"].size() == 2);
}

TEST_CASE("to_json of an empty JS array is an empty array, not null", "[json][forge]") {
  QJSEngine engine;
  const auto json = to_json(QVariant::fromValue(engine.newArray(0)));
  CHECK(json.is_array());
  CHECK(json.empty());
}

TEST_CASE("to_json of plain Qt values is unchanged", "[json]") {
  CHECK(to_json(QVariant(QStringLiteral("text"))) == "text");
  CHECK(to_json(QVariant(true)) == true);
  CHECK(to_json(QVariant(42)) == 42);
  CHECK(to_json(QVariant()).is_null());
  const auto list = to_json(QVariant(QVariantList{QStringLiteral("a"), QStringLiteral("b")}));
  REQUIRE(list.is_array());
  CHECK(list.size() == 2);
}
