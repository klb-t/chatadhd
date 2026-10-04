#include <doctest/doctest.h>

#include "loom/util/sha256.h"
#include "packet/packet.h"
using namespace loom;
namespace {
Json base() {
  Json p = {{"schema", "loom.graph_packet/1"},
            {"definitions", Json::array()},
            {"entities", Json::array()},
            {"claims", Json::array()},
            {"sources", Json::array()},
            {"task", Json::object()},
            {"provenance", Json{{"definitions", Json::object()},
                                {"entities", Json::object()},
                                {"claims", Json::object()},
                                {"sources", Json::object()}}},
            {"history", Json::array()}};
  p["packet_id"] = packet::digest(p);
  return p;
}
Json invoke(Json r) {
  auto result = packet::execute(r);
  REQUIRE(result);
  return *result;
}
}  // namespace
TEST_CASE("Packet strict parse preserves representation and rejects ambiguous bytes") {
  CHECK_THROWS(packet::parse_strict("{\"x\":1,\"x\":2}"));
  CHECK_THROWS(packet::parse_strict("{\"x\":{\"a\":1,\"a\":2}}"));
  CHECK_THROWS(packet::parse_strict("{\"x\":18446744073709551616}"));
  CHECK_THROWS(packet::parse_strict("{\"x\":-9223372036854775809}"));
  CHECK_THROWS(packet::parse_strict("\xef\xbb\xbf{}"));
  CHECK_THROWS(packet::parse_strict(std::string("\xff", 1)));
  CHECK(packet::parse_strict("{\"a\":18446744073709551615}")["a"] == UINT64_MAX);
}
TEST_CASE("Packet configured limits and valid calendar timestamps") {
  auto p = base();
  CHECK_NOTHROW(packet::validate(p));
  CHECK_THROWS(packet::validate(p, Json{{"max_nodes", 1}}));
  CHECK_NOTHROW(packet::validate(p, Json{{"max_depth", nullptr}, {"max_nodes", nullptr}}));
  CHECK_NOTHROW(packet::timestamp("2024-02-29T12:00:00+02:00"));
  CHECK_THROWS(packet::timestamp("2025-02-29T12:00:00Z"));
  CHECK_THROWS(packet::timestamp("2026-10-04T25:00:00Z"));
}
TEST_CASE("Packet invalid reply retains exact capture even with invalid base") {
  auto p = base();
  p["packet_id"] = "forged";
  auto r = invoke(Json{{"operation", "compile_reply"},
                       {"packet", p},
                       {"raw", " \ninvalid first response"},
                       {"host", Json::object()}});
  REQUIRE(r.contains("error"));
  REQUIRE(r["error"].contains("raw_capture"));
  CHECK(r["error"]["raw_capture"] == packet::capture(" \ninvalid first response"));
}
TEST_CASE("Packet duplicate diff edits and stale base fail without changing packet") {
  auto p = base();
  Json o = {{"kind", "user"},
            {"actor", "test"},
            {"model", nullptr},
            {"recipe_sha256", nullptr},
            {"response_sha256", nullptr}};
  auto d = packet::empty_diff(p, "proposal", o, nullptr);
  Json def = {{"id", "d1"},  {"kind", "synthetic-kind"}, {"description", ""}, {"examples", Json::array()},
              {"origin", o}, {"attrs", Json::object()}};
  d["definitions"]["add"] = Json::array({def, def});
  CHECK(invoke(Json{{"operation", "preview"}, {"packet", p}, {"diff", d}}).contains("error"));
  d["definitions"]["add"] = Json::array({def});
  auto before = p;
  auto preview = packet::preview(p, d);
  CHECK(p == before);
  CHECK(preview["canonical_store_written"] == false);
  auto candidate = preview["candidate_packet"];
  CHECK_NOTHROW(packet::validate(candidate));
  d["base_packet_sha256"] = "stale";
  CHECK(invoke(Json{{"operation", "preview"}, {"packet", p}, {"diff", d}}).contains("error"));
}
