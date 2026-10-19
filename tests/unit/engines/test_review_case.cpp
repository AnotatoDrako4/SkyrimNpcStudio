#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "sns/appearance/review_case.hpp"

using namespace sns::appearance;

namespace {

OverrideLink link(std::string plugin, std::string mod, std::vector<std::string> changed) {
  OverrideLink l;
  l.plugin = std::move(plugin);
  l.mod = std::move(mod);
  l.changed_fields = std::move(changed);
  return l;
}

AssetSource loose(std::string winner, std::string path = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\0001334e.nif") {
  AssetSource a;
  a.path = std::move(path);
  a.found = true;
  a.kind = "loose";
  a.winner_mod = winner;
  a.loose_mods = {std::move(winner)};
  return a;
}

// Shape of an ambiguous case of the test profile: AI Overhaul sets its own face values over NITHI's face.
ControlInputs inputs() {
  ControlInputs in;
  in.chain = {link("Skyrim.esm", "<Game Data>", {}), link("NITHI.esp", "NITHI", {"FaceMorph", "HeadParts"}),
              link("AI Overhaul.esp", "AI Overhaul", {"FaceMorph", "HeadParts", "AIData"})};
  in.chain.front().is_origin = true;
  in.chain.back().is_winner = true;
  in.facegen = loose("NITHI");
  in.facetint = loose("NITHI", "textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\0001334e.dds");
  return in;
}

FaceResolution ambiguous() {
  FaceResolution r;
  r.source_plugin = "NITHI.esp";
  r.source_mod = "NITHI";
  r.winner_plugin = "AI Overhaul.esp";
  r.winner_mod = "AI Overhaul";
  r.fields = {"HeadParts", "FaceMorph"};
  r.discards_own_edits = {"AI Overhaul.esp"};
  return r;
}

ReviewRecord decided(const std::string& key, ReviewDecision d, std::string digest) {
  return {key, d, std::move(digest), "2026-10-17T14:30:00-03:00", "nota"};
}

}  // namespace

TEST_CASE("Review: a clear resolution needs no review; an ambiguous one is pending until decided", "[appearance][review]") {
  auto clear = ambiguous();
  clear.discards_own_edits.clear();
  CHECK_FALSE(requires_review(clear));
  const auto ok = review_gate(clear, "01334E:Skyrim.esm", "x", {});
  CHECK(ok.allowed);
  CHECK(ok.state == "not-required");

  CHECK(requires_review(ambiguous()));
  const auto pending = review_gate(ambiguous(), "01334E:Skyrim.esm", "digest", {});
  CHECK_FALSE(pending.allowed);
  CHECK(pending.state == "pending");
  CHECK(pending.reason.find("«AI Overhaul.esp»") != std::string::npos);

  ReviewBook book;
  book.records["01334E:Skyrim.esm"] = decided("01334E:Skyrim.esm", ReviewDecision::Pending, "digest");
  CHECK(review_gate(ambiguous(), "01334E:Skyrim.esm", "digest", book).state == "pending");
}

TEST_CASE("Review: an approval is bound to its NPC and to the digest of its inputs", "[appearance][review]") {
  const std::string bolli = "01334E:Skyrim.esm";
  const auto digest = review_identity(bolli, inputs(), ambiguous());
  ReviewBook book;
  book.records[bolli] = decided(bolli, ReviewDecision::Approve, digest);

  const auto approved = review_gate(ambiguous(), bolli, digest, book);
  CHECK(approved.allowed);
  CHECK(approved.state == "approved");

  SECTION("approving A does not authorize B") {
    const std::string brand_shei = "01334F:Skyrim.esm";
    CHECK(review_gate(ambiguous(), brand_shei, review_identity(brand_shei, inputs(), ambiguous()), book).state == "pending");
  }
  SECTION("a change of the MO2 load order (a new version in the chain) makes it stale") {
    auto changed = inputs();
    changed.chain.insert(changed.chain.end() - 1, link("Patch.esp", "Some Patch", {"FaceParts"}));
    const auto now = review_identity(bolli, changed, ambiguous());
    CHECK(now != digest);
    const auto stale = review_gate(ambiguous(), bolli, now, book);
    CHECK_FALSE(stale.allowed);
    CHECK(stale.state == "stale");
  }
  SECTION("another mod now providing the effective FaceGen makes it stale") {
    auto changed = inputs();
    changed.facegen.winner_mod = "Other Replacer";
    changed.facegen.loose_mods = {"NITHI", "Other Replacer"};
    CHECK(review_gate(ambiguous(), bolli, review_identity(bolli, changed, ambiguous()), book).state == "stale");
  }
  SECTION("a different proposal (more fields) makes it stale") {
    auto more = ambiguous();
    more.fields.push_back("TintLayers");
    CHECK(review_gate(more, bolli, review_identity(bolli, inputs(), more), book).state == "stale");
  }
  SECTION("an approval without digest never passes") {
    book.records[bolli].input_digest.clear();
    CHECK(review_gate(ambiguous(), bolli, digest, book).state == "stale");
  }
}

TEST_CASE("Review: reject and defer block the export", "[appearance][review]") {
  const std::string key = "01334E:Skyrim.esm";
  const auto digest = review_identity(key, inputs(), ambiguous());
  ReviewBook book;
  book.records[key] = decided(key, ReviewDecision::Reject, digest);
  CHECK(review_gate(ambiguous(), key, digest, book).state == "rejected");
  CHECK_FALSE(review_gate(ambiguous(), key, digest, book).allowed);
  book.records[key].decision = ReviewDecision::Defer;
  CHECK(review_gate(ambiguous(), key, digest, book).state == "deferred");
  CHECK_FALSE(review_gate(ambiguous(), key, digest, book).allowed);
}

TEST_CASE("Review: 53 cases are reviewed one by one; one approval authorizes exactly one", "[appearance][review]") {
  std::vector<std::string> keys;
  for (int i = 0; i < 53; ++i) keys.push_back(std::to_string(0x013000 + i) + ":Skyrim.esm");
  ReviewBook book;
  const auto first = review_identity(keys[0], inputs(), ambiguous());
  book.records[keys[0]] = decided(keys[0], ReviewDecision::Approve, first);

  int allowed = 0;
  for (const auto& k : keys) allowed += review_gate(ambiguous(), k, review_identity(k, inputs(), ambiguous()), book).allowed ? 1 : 0;
  CHECK(allowed == 1);

  // A batch is only exportable when every case passes: one unapproved ambiguous case blocks it all.
  bool batch = true;
  for (const auto& k : {keys[0], keys[1]}) batch = batch && review_gate(ambiguous(), k, review_identity(k, inputs(), ambiguous()), book).allowed;
  CHECK_FALSE(batch);
}

TEST_CASE("Review: two NPCs with the same name are told apart by FormKey", "[appearance][review]") {
  // The identity carries the FormKey, never the display name.
  CHECK(review_identity("013BB9:Skyrim.esm", inputs(), ambiguous()) != review_identity("013BBA:Skyrim.esm", inputs(), ambiguous()));
  CHECK(review_identity("013BB9:Skyrim.esm", inputs(), ambiguous()) == review_identity("013BB9:Skyrim.esm", inputs(), ambiguous()));
}

TEST_CASE("Review: the book survives a save and reopen; bad files are refused", "[appearance][review]") {
  ReviewBook book;
  book.profile_key = "Portable__Apps_-Default";
  book.records["01334E:Skyrim.esm"] = decided("01334E:Skyrim.esm", ReviewDecision::Approve, "ABC");
  book.records["01334F:Skyrim.esm"] = decided("01334F:Skyrim.esm", ReviewDecision::Defer, "DEF");
  const auto reopened = review_book_from_json(review_book_to_json(book));
  CHECK(reopened.profile_key == book.profile_key);
  REQUIRE(reopened.records.size() == 2);
  CHECK(reopened.records.at("01334E:Skyrim.esm").decision == ReviewDecision::Approve);
  CHECK(reopened.records.at("01334E:Skyrim.esm").input_digest == "ABC");
  CHECK(reopened.records.at("01334F:Skyrim.esm").decision == ReviewDecision::Defer);
  CHECK(reopened.records.at("01334F:Skyrim.esm").notes == "nota");

  CHECK_THROWS_AS(review_book_from_json("not json"), std::runtime_error);
  CHECK_THROWS_AS(review_book_from_json(R"({"schema": 99, "records": []})"), std::runtime_error);
  CHECK_THROWS_AS(review_book_from_json(R"({"schema": 1, "records": [{"npcFormKey": "1:A.esp", "decision": "yes"}]})"), std::runtime_error);
  CHECK_THROWS_AS(review_book_from_json(R"({"schema": 1, "records": [{"decision": "approve"}]})"), std::runtime_error);
}
