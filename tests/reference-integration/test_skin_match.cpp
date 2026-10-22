#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "sns/appearance/skin_match.hpp"

using namespace sns::appearance;

namespace {

constexpr std::array<float, 3> kSkin{0.82f, 0.62f, 0.52f};

std::array<float, 3> scaled(float k) { return {kSkin[0] * k, kSkin[1] * k, kSkin[2] * k}; }

SkinSample sample(const std::string& role, std::array<float, 3> rgb, const std::string& label = "") {
  return {role, label.empty() ? role + " mesh" : label, rgb};
}

bool has(const SkinMatch& m, const std::string& code) { return std::find(m.codes.begin(), m.codes.end(), code) != m.codes.end(); }

}  // namespace

TEST_CASE("Skin match: delta E 76 is a distance (zero, symmetric) and black against white is 100", "[skinmatch]") {
  CHECK(delta_e76(kSkin, kSkin) == 0.0);
  CHECK(delta_e76(kSkin, scaled(0.8f)) == delta_e76(scaled(0.8f), kSkin));
  CHECK(delta_e76({0, 0, 0}, {1, 1, 1}) > 99.9);
  CHECK(delta_e76({0, 0, 0}, {1, 1, 1}) < 100.1);
  CHECK(delta_e76(kSkin, scaled(0.9f)) < delta_e76(kSkin, scaled(0.7f)));  // darker = farther
  // Out-of-range values are clamped, not extrapolated.
  CHECK(delta_e76({2.0f, 2.0f, 2.0f}, {1, 1, 1}) == 0.0);
}

TEST_CASE("Skin match: the role of a shape comes from its biped slots", "[skinmatch]") {
  CHECK(skin_role_of_slots(1u << 2) == "body");     // 32
  CHECK(skin_role_of_slots(1u << 3) == "hands");    // 33
  CHECK(skin_role_of_slots(1u << 4) == "hands");    // 34 forearms
  CHECK(skin_role_of_slots(1u << 7) == "feet");     // 37
  CHECK(skin_role_of_slots(1u << 0) == "head");     // 30
  CHECK(skin_role_of_slots(1u << 1) == "other");    // 31 hair
  CHECK(skin_role_of_slots(0) == "other");
  CHECK(skin_role_of_slots((1u << 2) | (1u << 3) | (1u << 7)) == "body");  // a full-body addon is the body
}

TEST_CASE("Skin match: the verdict follows the worst difference and its thresholds", "[skinmatch]") {
  int consistent = 0, noticeable = 0, seam = 0;
  for (const float k : {1.0f, 0.99f, 0.96f, 0.92f, 0.88f, 0.82f, 0.74f, 0.6f, 0.4f}) {
    const auto m = evaluate_skin_match({sample("head", kSkin), sample("body", scaled(k))});
    REQUIRE(m.pairs.size() == 1);
    CHECK(m.worst == m.pairs.front().delta_e);
    if (m.worst <= kConsistentMax) {
      CHECK(m.verdict == SkinVerdict::Consistent);
      CHECK(m.codes.empty());
      ++consistent;
    } else if (m.worst <= kNoticeableMax) {
      CHECK(m.verdict == SkinVerdict::Noticeable);
      CHECK(has(m, "SeamPossible"));
      ++noticeable;
    } else {
      CHECK(m.verdict == SkinVerdict::LikelySeam);
      CHECK(has(m, "SeamLikely"));
      ++seam;
    }
  }
  // The scales above reach all three verdicts, so the mapping is exercised both sides of each threshold.
  CHECK(consistent >= 2);
  CHECK(noticeable >= 1);
  CHECK(seam >= 2);
  CHECK(evaluate_skin_match({sample("head", kSkin), sample("body", kSkin)}).verdict == SkinVerdict::Consistent);
  CHECK(evaluate_skin_match({sample("head", kSkin), sample("body", scaled(0.4f))}).verdict == SkinVerdict::LikelySeam);
}

TEST_CASE("Skin match: the worst of body, hands and feet decides and is named", "[skinmatch]") {
  const auto m = evaluate_skin_match({sample("head", kSkin), sample("body", scaled(0.99f)), sample("hands", scaled(0.5f), "hands mesh"), sample("feet", scaled(0.98f))});
  REQUIRE(m.pairs.size() == 3);
  CHECK(m.worst_role == "hands");
  CHECK(m.verdict == SkinVerdict::LikelySeam);
  CHECK(m.message.find("hands") != std::string::npos);
  // Order of the report is the order given; the head is never compared with itself.
  CHECK(m.pairs[0].role == "body");
  CHECK(m.pairs[1].role == "hands");
  CHECK(m.pairs[2].role == "feet");
}

TEST_CASE("Skin match: without a head or without a body part it says so instead of guessing", "[skinmatch]") {
  const auto no_head = evaluate_skin_match({sample("body", kSkin)});
  CHECK(no_head.verdict == SkinVerdict::Unknown);
  CHECK(has(no_head, "HeadNotMeasured"));
  CHECK(no_head.pairs.empty());

  const auto no_body = evaluate_skin_match({sample("head", kSkin)});
  CHECK(no_body.verdict == SkinVerdict::Unknown);
  CHECK(has(no_body, "BodyNotMeasured"));

  CHECK(evaluate_skin_match({}).verdict == SkinVerdict::Unknown);
  // Hair and other shapes are not skin: they never decide the verdict.
  const auto other_only = evaluate_skin_match({sample("head", kSkin), sample("other", scaled(0.3f))});
  CHECK(other_only.verdict == SkinVerdict::Unknown);
  CHECK(has(other_only, "BodyNotMeasured"));
}

TEST_CASE("Skin match: every result is an estimate and never claims to be seen in Skyrim", "[skinmatch]") {
  for (const auto& samples : {std::vector<SkinSample>{}, std::vector<SkinSample>{sample("head", kSkin), sample("body", kSkin)},
                              std::vector<SkinSample>{sample("head", kSkin), sample("body", scaled(0.3f))}}) {
    const auto m = evaluate_skin_match(samples);
    CHECK(std::find(m.caveats.begin(), m.caveats.end(), "ESTIMATE") != m.caveats.end());
    CHECK(std::find(m.caveats.begin(), m.caveats.end(), "UNVERIFIED_IN_SKYRIM") != m.caveats.end());
    CHECK_FALSE(m.message.empty());
  }
  CHECK(to_string(SkinVerdict::Consistent) == "CONSISTENT");
  CHECK(to_string(SkinVerdict::Noticeable) == "NOTICEABLE");
  CHECK(to_string(SkinVerdict::LikelySeam) == "LIKELY_SEAM");
  CHECK(to_string(SkinVerdict::Unknown) == "UNKNOWN");
}

TEST_CASE("Skin match: the result is deterministic", "[skinmatch]") {
  const std::vector<SkinSample> samples{sample("head", kSkin), sample("body", scaled(0.85f)), sample("feet", scaled(0.9f))};
  const auto a = evaluate_skin_match(samples), b = evaluate_skin_match(samples);
  CHECK(a.verdict == b.verdict);
  CHECK(a.worst == b.worst);
  CHECK(a.message == b.message);
}
