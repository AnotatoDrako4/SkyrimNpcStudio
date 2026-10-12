#include <catch2/catch_test_macros.hpp>

#include "sns/core/error.hpp"
#include "sns/ipc/framing.hpp"

using sns::ipc::encode_frame;
using sns::ipc::FrameDecoder;

TEST_CASE("Frames round-trip, including split and coalesced reads", "[ipc][framing]") {
  const std::string a = R"({"jsonrpc":"2.0","id":1,"result":"ñ"})";
  const std::string b = R"({"jsonrpc":"2.0","method":"catalog.progress","params":{}})";
  const auto wire = encode_frame(a) + encode_frame(b);

  FrameDecoder decoder;
  // Feed one byte at a time: every partial state must be handled.
  std::vector<std::string> out;
  for (char c : wire) {
    decoder.feed(std::string_view(&c, 1));
    while (auto m = decoder.next()) out.push_back(*m);
  }
  REQUIRE(out.size() == 2);
  CHECK(out[0] == a);
  CHECK(out[1] == b);
  CHECK(decoder.buffered() == 0);
}

TEST_CASE("Extra headers are ignored and header names are case-insensitive", "[ipc][framing]") {
  FrameDecoder decoder;
  decoder.feed("content-length: 2\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n{}");
  CHECK(decoder.next() == std::optional<std::string>("{}"));
}

TEST_CASE("Malformed or hostile frames are rejected", "[ipc][framing][security]") {
  SECTION("missing length") {
    FrameDecoder d;
    d.feed("Content-Type: x\r\n\r\n{}");
    CHECK_THROWS_AS(d.next(), sns::Error);
  }
  SECTION("non-numeric length") {
    FrameDecoder d;
    d.feed("Content-Length: 12abc\r\n\r\n");
    CHECK_THROWS_AS(d.next(), sns::Error);
  }
  SECTION("oversized message") {
    FrameDecoder d;
    d.feed("Content-Length: 999999999999\r\n\r\n");
    CHECK_THROWS_AS(d.next(), sns::Error);
  }
  SECTION("endless header") {
    FrameDecoder d;
    d.feed(std::string(FrameDecoder::kMaxHeaderBytes + 10, 'a'));
    CHECK_THROWS_AS(d.next(), sns::Error);
  }
}
