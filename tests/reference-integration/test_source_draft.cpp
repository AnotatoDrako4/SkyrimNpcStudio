#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "sns/appearance/source_draft.hpp"

using namespace sns::appearance;

namespace {

SourceMatrix matrix_with(std::vector<std::string> ids, const std::string& form_key = "0A2C8E:Skyrim.esm", const std::string& profile = "profile-1") {
  SourceMatrix m;
  m.npc_form_key = form_key;
  m.profile_digest = profile;
  for (auto& id : ids) {
    SourceCandidate c;
    c.id = std::move(id);
    m.candidates.push_back(std::move(c));
  }
  return m;
}

constexpr const char* kNow = "2026-10-21T10:15:00-03:00";

}  // namespace

TEST_CASE("Source draft: no entry is EMPTY; a saved one is FRESH", "[sourcedraft]") {
  SourceDraftBook book;
  book.profile_key = "Default";
  const auto m = matrix_with({"effective", "facegen:moda"});
  CHECK(evaluate_draft(book, m, "digest-1") == DraftState::Empty);

  std::string error;
  REQUIRE(set_draft(book, m, "digest-1", "effective", "facegen:moda", "", kNow, "", &error));
  CHECK(error.empty());
  CHECK(evaluate_draft(book, m, "digest-1") == DraftState::Fresh);
  CHECK(book.entries.at("0A2C8E:Skyrim.esm").saved_at == kNow);
}

TEST_CASE("Source draft: another profile makes it stale and it is never reused", "[sourcedraft]") {
  SourceDraftBook book;
  const auto m = matrix_with({"effective", "facegen:moda"});
  REQUIRE(set_draft(book, m, "digest-1", "effective", "facegen:moda", "facegen:moda", kNow, "", nullptr));
  const auto other_profile = matrix_with({"effective", "facegen:moda"}, "0A2C8E:Skyrim.esm", "profile-2");
  CHECK(evaluate_draft(book, other_profile, "digest-1") == DraftState::StaleProfile);
}

TEST_CASE("Source draft: changed inputs (a FaceGen replaced, a mod moved) make it stale", "[sourcedraft]") {
  SourceDraftBook book;
  const auto m = matrix_with({"effective", "facegen:moda"});
  REQUIRE(set_draft(book, m, "digest-1", "effective", "facegen:moda", "", kNow, "", nullptr));
  CHECK(evaluate_draft(book, m, "digest-2") == DraftState::StaleInputs);
}

TEST_CASE("Source draft: a candidate that disappeared is reported, not silently dropped", "[sourcedraft]") {
  SourceDraftBook book;
  const auto before = matrix_with({"effective", "facegen:moda"});
  REQUIRE(set_draft(book, before, "digest-1", "effective", "facegen:moda", "facegen:moda", kNow, "", nullptr));
  const auto after = matrix_with({"effective"});
  CHECK(evaluate_draft(book, after, "digest-1") == DraftState::UnknownCandidate);
}

TEST_CASE("Source draft: an unknown candidate is refused and leaves the book untouched", "[sourcedraft]") {
  SourceDraftBook book;
  const auto m = matrix_with({"effective"});
  std::string error;
  CHECK_FALSE(set_draft(book, m, "digest-1", "effective", "facegen:ghost", "", kNow, "", &error));
  CHECK(error.find("facegen:ghost") != std::string::npos);
  CHECK(book.entries.empty());
}

TEST_CASE("Source draft: drafts of different NPCs are independent and clearing one keeps the other", "[sourcedraft]") {
  SourceDraftBook book;
  const auto a = matrix_with({"effective", "facegen:moda"}, "0A2C8E:Skyrim.esm");
  const auto b = matrix_with({"effective", "facegen:modb"}, "013BBB:Skyrim.esm");
  REQUIRE(set_draft(book, a, "da", "effective", "facegen:moda", "", kNow, "", nullptr));
  REQUIRE(set_draft(book, b, "db", "effective", "facegen:modb", "facegen:modb", kNow, "", nullptr));
  clear_draft(book, "0A2C8E:Skyrim.esm");
  CHECK(evaluate_draft(book, a, "da") == DraftState::Empty);
  CHECK(evaluate_draft(book, b, "db") == DraftState::Fresh);
}

TEST_CASE("Source draft: JSON is deterministic and round-trips byte for byte", "[sourcedraft]") {
  SourceDraftBook book;
  book.profile_key = "Default";
  REQUIRE(set_draft(book, matrix_with({"effective", "facegen:modb"}, "013BBB:Skyrim.esm"), "db", "effective", "facegen:modb", "", kNow, "nota «ñ»", nullptr));
  REQUIRE(set_draft(book, matrix_with({"effective", "facegen:moda"}), "da", "effective", "facegen:moda", "facegen:moda", kNow, "", nullptr));

  const auto text = source_draft_to_json(book);
  CHECK(text.back() == '\n');
  CHECK(source_draft_to_json(book) == text);  // same book, same bytes
  const auto back = source_draft_from_json(text);
  CHECK(source_draft_to_json(back) == text);
  CHECK(back.profile_key == "Default");
  CHECK(back.entries.at("013BBB:Skyrim.esm").notes == "nota «ñ»");
  // keys sorted: the NPC 013BBB entry is written before 0A2C8E
  CHECK(text.find("013BBB:Skyrim.esm") < text.find("0A2C8E:Skyrim.esm"));
}

TEST_CASE("Source draft: malformed, foreign-schema or incomplete files are rejected", "[sourcedraft]") {
  CHECK_THROWS_AS(source_draft_from_json("not json"), std::runtime_error);
  CHECK_THROWS_AS(source_draft_from_json("{}"), std::runtime_error);
  CHECK_THROWS_AS(source_draft_from_json(R"({"schema":99,"profileKey":"x","entries":{}})"), std::runtime_error);
  CHECK_THROWS_AS(source_draft_from_json(R"({"schema":1,"profileKey":"x"})"), std::runtime_error);
  CHECK_THROWS_AS(source_draft_from_json(R"({"schema":1,"profileKey":"x","entries":{"k":{"npcFormKey":"k"}}})"), std::runtime_error);
  CHECK_THROWS_AS(source_draft_from_json(R"({"schema":1,"profileKey":"x","entries":{"k":7}})"), std::runtime_error);
  CHECK_NOTHROW(source_draft_from_json(R"({"schema":1,"profileKey":"x","entries":{}})"));
}
