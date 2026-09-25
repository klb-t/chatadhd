// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include "loom/net/sse.h"

using namespace loom;

namespace {
std::vector<net::SseEvent> parse_all(std::string_view whole, std::size_t chunk_size) {
  net::SseParser p;
  std::vector<net::SseEvent> out;
  auto cb = [&](const net::SseEvent& e) { out.push_back(e); };
  for (std::size_t i = 0; i < whole.size(); i += chunk_size) {
    p.feed(whole.substr(i, chunk_size), cb);
  }
  p.finish(cb);
  return out;
}
}  // namespace

TEST_SUITE("net.sse") {
  TEST_CASE("basic data-only events, one shot") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    p.feed("data: {\"a\":1}\n\ndata: [DONE]\n\n", [&](const net::SseEvent& e) { out.push_back(e); });
    REQUIRE(out.size() == 2);
    CHECK(out[0].data == "{\"a\":1}");
    CHECK(!out[0].is_done());
    CHECK(out[1].is_done());
  }

  TEST_CASE("multi-line data is joined with \\n") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    p.feed("data: line1\ndata: line2\n\n", [&](const net::SseEvent& e) { out.push_back(e); });
    REQUIRE(out.size() == 1);
    CHECK(out[0].data == "line1\nline2");
  }

  TEST_CASE("comments and unknown fields are ignored") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    p.feed(": OPENROUTER PROCESSING\nfoo: bar\ndata: x\n\n", [&](const net::SseEvent& e) { out.push_back(e); });
    REQUIRE(out.size() == 1);
    CHECK(out[0].data == "x");
  }

  TEST_CASE("event and id fields recorded; retry parsed") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    p.feed("event: ping\nid: 42\nretry: 5000\ndata: hi\n\n", [&](const net::SseEvent& e) { out.push_back(e); });
    REQUIRE(out.size() == 1);
    CHECK(out[0].event == "ping");
    CHECK(out[0].id == "42");
    REQUIRE(out[0].retry.has_value());
    CHECK(*out[0].retry == 5000);
  }

  TEST_CASE("no event without a blank line and without data") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    p.feed("event: ping\nid: 42\n\n", [&](const net::SseEvent& e) { out.push_back(e); });
    CHECK(out.empty());  // no "data" line seen -> nothing dispatched
  }

  TEST_CASE("finish() dispatches a pending event without the final blank line") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    auto cb = [&](const net::SseEvent& e) { out.push_back(e); };
    p.feed("data: partial", cb);
    CHECK(out.empty());
    p.finish(cb);
    REQUIRE(out.size() == 1);
    CHECK(out[0].data == "partial");
  }

  TEST_CASE("[DONE] trims surrounding spaces") {
    net::SseEvent e;
    e.data = "  [DONE]  ";
    CHECK(e.is_done());
    e.data = "[DONE]x";
    CHECK(!e.is_done());
  }

  TEST_CASE("CRLF, bare CR and LF line endings all work") {
    auto out = parse_all("data: a\r\n\r\ndata: b\n\ndata: c\r\r", 4096);
    REQUIRE(out.size() == 3);
    CHECK(out[0].data == "a");
    CHECK(out[1].data == "b");
    CHECK(out[2].data == "c");
  }

  TEST_CASE("chunk boundaries split mid-line, mid-CRLF and mid-field are handled") {
    std::string whole = "data: {\"choices\":[{\"delta\":{\"content\":\"hi\"}}]}\r\n\r\ndata: [DONE]\r\n\r\n";
    for (std::size_t sz : {1, 2, 3, 5, 7, 11, 4096}) {
      auto out = parse_all(whole, sz);
      REQUIRE(out.size() == 2);
      CHECK(out[0].data == "{\"choices\":[{\"delta\":{\"content\":\"hi\"}}]}");
      CHECK(out[1].is_done());
    }
  }

  TEST_CASE("reset clears buffered state") {
    net::SseParser p;
    std::vector<net::SseEvent> out;
    auto cb = [&](const net::SseEvent& e) { out.push_back(e); };
    p.feed("data: x", cb);
    p.reset();
    p.finish(cb);
    CHECK(out.empty());
  }
}
