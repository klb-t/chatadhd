#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "loom/knowledge_candidate_graph.h"
#include "loom/util/sha256.h"

using namespace loom;
using loom::extract::validate_candidate_graph_bundle;
using loom::extract::validate_candidate_graph_vocabulary;

namespace {

Json vocabulary() {
  // Public author policy, never independent evaluation data. Runtime pack
  // integration has its own loader tests; this pure validator has no DB/HTTP.
  const auto path = std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path() /
                    "tools/structure/candidate_graph_vocabulary.json";
  std::ifstream stream(path);
  REQUIRE(stream.good());
  Json value;
  stream >> value;
  return value;
}

struct Draft {
  Json packet, bundle, support;
  int next = 0;

  explicit Draft(std::string source = "If the lamp glows, the room is bright.") {
    packet = Json{{"schema", "loom.source_packet/1"}, {"snapshot_id", "author_snapshot"},
      {"observations", Json::array({Json{{"id", "ob"}, {"unit", "unit"}, {"text", source},
        {"locator", Json{{"source", "author-source"}, {"member", "chapter"}}},
        {"attrs", Json{{"branch", "main"}}}}})}, {"entities", Json::array()}, {"claims", Json::array()}};
    support = Json::array({Json{{"observation", "ob"}, {"byte_start", 0}, {"byte_len", source.size()}, {"quote", source}}});
    bundle = Json{{"schema", "loom.candidate_graph/1"}, {"packet_id", "author_snapshot"},
      {"entity_drafts", Json::array()}, {"claim_drafts", Json::array()}, {"roots", Json::array()},
      {"coverage", Json::array()}, {"unknowns", Json::array()}};
  }

  void entity(const std::string& h, const std::string& kind, Json attrs = Json::object()) {
    bundle["entity_drafts"].push_back(Json{{"handle", h}, {"kind", kind}, {"label", h}, {"attrs", attrs}, {"support", support}});
  }
  void scope(const std::string& h, const std::string& kind = "assertion", const std::string& context = "asserted") {
    entity(h, "scope", Json{{"scope_type", kind}, {"assertion_context", context}});
  }
  std::string context(const std::string& s) const {
    for (const auto& e : bundle["entity_drafts"]) if (e["handle"] == s) return e["attrs"]["assertion_context"].get<std::string>();
    return "unknown";
  }
  void claim(const std::string& s, const std::string& p, const std::string& o,
             const std::string& scope_id, Json value = nullptr, const std::string& port = "", int ordinal = 0) {
    Json extra{{"polarity", "positive"}, {"assertion_context", context(scope_id)}};
    if (p == "operand") { extra["port"] = port; extra["ordinal"] = ordinal; }
    bundle["claim_drafts"].push_back(Json{{"handle", "@c" + std::to_string(next++)}, {"subject", s},
      {"predicate", p}, {"object", o}, {"value", value}, {"qualifiers", Json{{"scope", scope_id}, {"extra", extra}}},
      {"assessment", Json{{"basis", Json{{"support", support}}}, {"premises", Json{{"claims", Json::array()}}}}}});
  }
  void occurrence(const std::string& h, const std::string& kind, const std::string& s, Json attrs = Json::object()) {
    entity(h, kind, std::move(attrs)); claim(h, "in_scope", s, s);
  }
  void expression(const std::string& h, const std::string& op, const std::string& s) {
    occurrence(h, "expression_occurrence", s); claim(h, "operation_type", "", s, op);
  }
  void term(const std::string& h, const std::string& type, const std::string& symbol, const std::string& s) {
    occurrence(h, "term_occurrence", s, Json{{"term_type", type}, {"symbol", symbol}});
  }
  void operand(const std::string& h, const std::string& port, const std::string& target, const std::string& s, int ordinal = 0) {
    claim(h, "operand", target, s, nullptr, port, ordinal);
  }
  void atom(const std::string& h, const std::string& s) {
    expression(h, "predicate_application", s); term(h + "_predicate", "predicate", "glows", s);
    operand(h, "predicate", h + "_predicate", s);
  }
  void root(const std::string& h) {
    bundle["roots"] = Json::array({h});
    bundle["coverage"] = Json::array({Json{{"support", support}, {"status", "represented"},
      {"reason", "author-declared interpretation"}, {"drafts", Json::array({h})}}});
  }
  Json check(const Json& limits = Json::object()) const {
    return validate_candidate_graph_bundle(bundle, packet, vocabulary(), limits);
  }
  Json& find_claim(const std::string& subject, const std::string& predicate) {
    for (auto& c : bundle["claim_drafts"]) if (c["subject"] == subject && c["predicate"] == predicate) return c;
    throw std::logic_error("missing author claim");
  }
};

Draft conditional() {
  Draft d;
  d.scope("@s"); d.atom("@lamp", "@s"); d.atom("@bright", "@s");
  d.expression("@if", "conditional", "@s");
  d.operand("@if", "antecedent", "@lamp", "@s"); d.operand("@if", "consequent", "@bright", "@s");
  d.root("@if"); return d;
}

Draft quantified() {
  Draft d("Every lamp glows.");
  d.scope("@s"); d.scope("@qs", "quantifier"); d.claim("@qs", "scope_parent", "@s", "@qs");
  d.expression("@all", "quantifier", "@s"); d.claim("@all", "quantifier_kind", "", "@s", "forall");
  d.claim("@all", "introduces_scope", "@qs", "@s");
  d.occurrence("@binder", "binder", "@qs", Json{{"symbol", "x"}});
  d.atom("@body", "@qs"); d.term("@x", "variable", "x", "@qs");
  d.operand("@body", "argument", "@x", "@qs"); d.claim("@x", "bound_to", "@binder", "@qs");
  d.operand("@all", "binder", "@binder", "@s"); d.operand("@all", "body", "@body", "@s");
  d.root("@all"); return d;
}

std::string error_code(const Json& result) {
  return result["errors"].empty() ? "" : result["errors"][0]["code"].get<std::string>();
}

}  // namespace

TEST_CASE("candidate graph policy is explicit experimental data with bounded limits") {
  auto v = vocabulary();
  CHECK(validate_candidate_graph_vocabulary(v)["valid"] == true);
  v["status"] = "experimental_unpromoted"; v["limits"]["max_depth"] = 8;
  CHECK(validate_candidate_graph_vocabulary(v)["valid"] == true);
  v["operations"]["conditional"]["ports"]["antecedent"]["min"] = 0;
  CHECK(validate_candidate_graph_vocabulary(v)["valid"] == false);
  v = vocabulary(); v["status"] = "canonical";
  CHECK(validate_candidate_graph_vocabulary(v)["valid"] == false);
  v = vocabulary(); v["limits"]["max_depth"] = 33;
  CHECK(validate_candidate_graph_vocabulary(v)["valid"] == false);
  CHECK_NOTHROW(validate_candidate_graph_vocabulary(Json::array()));
}

TEST_CASE("candidate graph preserves partial drafts and documents native packet hash") {
  const auto d = conditional(); const auto r = d.check();
  INFO(r.dump()); REQUIRE(r["valid"] == true);
  CHECK(r["drafts"]["entities"] == d.bundle["entity_drafts"]);
  CHECK(r["drafts"]["claims"] == d.bundle["claim_drafts"]);
  CHECK(r["retained_input"]["source_packet"] == d.packet);
  CHECK(r["packet_hash"] == Sha256::hex(json::canonical(d.packet)));
  CHECK(r["hash_algorithm"] == "loom-json-canonical-sha256");
  CHECK(r["no_inference"] == true); CHECK(r["no_persistence"] == true);
  CHECK(r["coverage"]["representation_status"] == "complete_declared");
  CHECK(r["coverage"]["semantic_accuracy"].is_null());
  CHECK_FALSE(r["drafts"]["claims"][0]["assessment"].contains("status"));
  auto changed = d; changed.packet["source_metadata"] = Json{{"revision", 2}};
  CHECK(changed.check()["packet_hash"] != r["packet_hash"]);
}

TEST_CASE("candidate graph rejects malformed fields references and invented Assessment") {
  auto d = conditional(); d.find_claim("@if", "operand")["object"] = "missing";
  auto r = d.check(); CHECK(r["valid"] == false); CHECK(error_code(r) == "endpoint");
  CHECK(r["retained_input"]["bundle"] == d.bundle); CHECK(r["drafts"]["entities"].empty());
  d = conditional(); d.find_claim("@if", "operand")["qualifiers"]["extra"]["ordinal"] = 1;
  CHECK(error_code(d.check()) == "operand_cardinality");
  d = conditional(); d.find_claim("@if", "operand")["assessment"]["status"] = "candidate";
  CHECK(error_code(d.check()) == "shape");
  d = conditional(); d.bundle["entity_drafts"][0]["attrs"] = nullptr;
  CHECK_NOTHROW(r = d.check()); CHECK(r["valid"] == false);
  d = conditional(); d.bundle["coverage"][0]["reason"] = "\u00a0\u2003";
  CHECK(error_code(d.check()) == "text");
  d = conditional();
  for (auto& e : d.bundle["entity_drafts"]) if (e["kind"] == "term_occurrence") e["attrs"]["symbol"] = "\u00a0";
  CHECK(error_code(d.check()) == "text");
  CHECK_NOTHROW(validate_candidate_graph_bundle(Json(nullptr), Json::array(), vocabulary()));
}

TEST_CASE("candidate graph validates exact UTF8 byte support and reports located gaps") {
  Draft d("Żółta lampa."); d.scope("@s"); d.atom("@a", "@s"); d.root("@a");
  REQUIRE(d.check()["valid"] == true);
  d.bundle["entity_drafts"][0]["support"][0]["quote"] = "forged";
  CHECK(error_code(d.check()) == "quote_mismatch");
  d.bundle["entity_drafts"][0]["support"] = d.support;
  d.bundle["entity_drafts"][0]["support"][0]["byte_start"] = 1;
  d.bundle["entity_drafts"][0]["support"][0]["byte_len"] = 1;
  CHECK(error_code(d.check()) == "utf8_boundary");
  d.bundle["entity_drafts"][0]["support"] = d.support;
  d.bundle["coverage"][0]["support"][0] = Json{{"observation", "ob"}, {"byte_start", 0}, {"byte_len", 2}, {"quote", "Ż"}};
  auto r = d.check(); REQUIRE(r["valid"] == true);
  CHECK(r["coverage"]["representation_status"] == "partial");
  CHECK(r["coverage"]["uncovered_spans"][0]["quote"] == "ółta lampa.");
}

TEST_CASE("candidate graph quantifier ownership binding capture and contexts are checked") {
  auto d = quantified(); INFO(d.check().dump()); REQUIRE(d.check()["valid"] == true);
  d.find_claim("@x", "bound_to")["object"] = "@body";
  CHECK(error_code(d.check()) == "binding");
  d = quantified();
  for (auto& e : d.bundle["entity_drafts"]) if (e["handle"] == "@x") e["attrs"]["symbol"] = "y";
  CHECK(error_code(d.check()) == "binding_capture");
  d = quantified(); d.find_claim("@all", "introduces_scope")["object"] = "@s";
  CHECK(error_code(d.check()) == "quantifier_scope");
  d = quantified(); d.find_claim("@body", "operation_type")["qualifiers"]["extra"]["assertion_context"] = "quoted";
  CHECK(error_code(d.check()) == "context_mismatch");
  d = quantified(); d.find_claim("@x", "bound_to")["qualifiers"]["scope"] = "@s";
  CHECK(error_code(d.check()) == "qualifier_scope");
}

TEST_CASE("candidate graph existing context keeps full Assessment and typed namespaces") {
  auto d = conditional();
  d.packet["entities"] = Json::array({Json{{"id", "prior:c"}, {"kind", "component"}, {"label", "old lamp"}}});
  const Json assessment{{"basis", Json{{"support", Json::array()}, {"derivation", "retained-old-context"}}},
    {"premises", Json{{"claims", Json::array()}}}, {"evidence_class", "observed"}, {"origin", "archive"},
    {"confidence", 0.75}, {"status", "active"}, {"counter", Json::array({"old-counter"})}};
  d.packet["claims"] = Json::array({Json{{"id", "c"}, {"assessment", assessment}}});
  d.find_claim("@if", "operation_type")["assessment"]["premises"]["claims"] = Json::array({"c"});
  d.claim("@lamp_predicate", "denotes", "prior:c", "@s");
  auto r = d.check(); REQUIRE(r["valid"] == true);
  CHECK(r["retained_input"]["source_packet"]["claims"][0]["assessment"] == assessment);
  d.packet["claims"][0]["id"] = "prior:c";
  CHECK(error_code(d.check()) == "typed_namespace");
  d.packet["claims"][0]["id"] = "c"; d.packet["claims"][0]["assessment"]["evidence_class"] = "extrapolated";
  CHECK(error_code(d.check()) == "premise_ineligible");
  d.packet["claims"][0]["assessment"].erase("confidence");
  CHECK(error_code(d.check()) == "missing_assessment");
}

TEST_CASE("candidate graph supports located abstention without wildcard structure") {
  Draft d("The sentence has an unresolved reading.");
  d.bundle["coverage"] = Json::array({Json{{"support", d.support}, {"status", "ambiguous"},
    {"reason", "multiple readings retained"}, {"drafts", Json::array()}}});
  d.bundle["unknowns"] = Json::array({Json{{"support", d.support}, {"reason", "no selected interpretation"}}});
  auto r = d.check(); REQUIRE(r["valid"] == true);
  CHECK(r["coverage"]["representation_status"] == "unrepresented");
  CHECK(r["drafts"]["entities"].empty()); CHECK(r["coverage"]["located_unknowns"] == d.bundle["unknowns"]);
  d.bundle["coverage"][0]["status"] = "represented";
  CHECK(error_code(d.check()) == "coverage_reference");
}

TEST_CASE("candidate graph enforces packet bundle and traversal limits") {
  auto d = conditional();
  CHECK(error_code(d.check(Json{{"max_entities", 1}})) == "budget");
  CHECK(error_code(d.check(Json{{"max_source_bytes", 1}})) == "budget");
  CHECK(error_code(d.check(Json{{"max_bundle_bytes", 1}})) == "budget");
  CHECK(error_code(d.check(Json{{"max_depth", 33}})) == "limits");
  CHECK(error_code(d.check(Json{{"max_depth", true}})) == "limits");
  CHECK(error_code(d.check(Json{{"unknown", 1}})) == "limits");
  d.bundle["packet_id"] = "different";
  CHECK(error_code(d.check()) == "bundle_schema");
  Json deep = 1;
  for (int i = 0; i < 130; ++i) deep = Json::array({std::move(deep)});
  d = conditional(); d.packet["untrusted_metadata"] = std::move(deep);
  const auto deep_report = d.check();
  CHECK(error_code(deep_report) == "budget");
  CHECK(deep_report["retained_input"].is_null());
  CHECK(deep_report["retention"]["status"] == "not_copied");
  CHECK(deep_report["retention"]["input_ownership"] == "caller");
}

TEST_CASE("candidate graph memoizes shared DAG branches and rejects expression cycles") {
  Draft d("Repeated occurrences can share a subexpression."); d.scope("@s"); d.atom("@a", "@s");
  std::string previous = "@a";
  for (int i = 0; i < 24; ++i) {
    const auto h = "@and" + std::to_string(i); d.expression(h, "conjunction", "@s");
    d.operand(h, "member", previous, "@s", 0); d.operand(h, "member", previous, "@s", 1); previous = h;
  }
  d.root(previous); REQUIRE(d.check()["valid"] == true);
  CHECK(error_code(d.check(Json{{"max_depth", 16}})) == "depth");
  d.find_claim("@and0", "operand")["object"] = previous;
  CHECK(error_code(d.check()) == "syntax_cycle");
}

TEST_CASE("candidate graph preserves negated conditional versus conditional negation") {
  auto outer = conditional(); outer.expression("@not", "negation", "@s");
  outer.operand("@not", "body", "@if", "@s"); outer.root("@not");
  auto inner = conditional(); inner.expression("@not", "negation", "@s");
  inner.operand("@not", "body", "@bright", "@s");
  for (auto& c : inner.bundle["claim_drafts"]) if (c["subject"] == "@if" && c["predicate"] == "operand" && c["qualifiers"]["extra"]["port"] == "consequent") c["object"] = "@not";
  REQUIRE(outer.check()["valid"] == true); REQUIRE(inner.check()["valid"] == true);
  CHECK(outer.check()["drafts"] != inner.check()["drafts"]);
  CHECK(outer.check()["retained_input"]["bundle"]["roots"] != inner.check()["retained_input"]["bundle"]["roots"]);
}
