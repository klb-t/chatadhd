// Native C ABI integration for W2 retrieval requests. Synthetic repository DEV
// only: these verify request plumbing and evidence retention, not semantic quality.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>

#include "capi/context.h"
#include "loom/knowledge.h"
#include "loom/loom.h"
#include "loom/net/http.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

Json take_json(const char* bytes) {
  REQUIRE(bytes);
  const Json value = json::parse_or(bytes, Json(nullptr));
  loom_free_string(bytes);
  return value;
}

Entity native_entity(const std::string& label) {
  Entity entity;
  entity.kind = "component";
  entity.canonical_key = label;
  entity.label = label;
  entity.id = Entity::make_id(entity.kind, entity.canonical_key);
  entity.origin = Origin::Archive;
  entity.confidence = 1;
  return entity;
}

Claim native_claim(const Entity& from, const Entity& to, const std::string& quote) {
  Claim claim;
  claim.subject = from.id;
  claim.object = to.id;
  claim.predicate = "depends_on";
  claim.qualifiers.valid_from = "2026-09-30";
  claim.assessment.evidence = EvidenceClass::Observed;
  claim.assessment.origin = Origin::Archive;
  claim.assessment.confidence = 0.9;
  Support support;
  support.observation = "ob_native_consumer_dev";
  support.quote = quote;
  support.extractor = "synthetic_native_consumer_fixture";
  claim.assessment.support = {support};
  claim.id = Claim::make_id(claim.subject, claim.predicate, claim.object, claim.value, claim.qualifiers);
  return claim;
}

struct NativeFixture {
  fsutil::TempDir directory;
  std::unique_ptr<LoomContext, decltype(&loom_shutdown)> context{nullptr, &loom_shutdown};
  Entity a = native_entity("native alpha"), b = native_entity("native beta"), c = native_entity("native gamma");
  Entity x = native_entity("separate native counter"), y = native_entity("separate native endpoint");
  Claim ab = native_claim(a, b, "NATIVE-SOURCE-BEGIN " + std::string(300, 'q') + " NATIVE-SOURCE-END");
  Claim bc = native_claim(b, c, "NATIVE-SECOND-HOP");
  Claim counter = native_claim(x, y, "NATIVE-COUNTER-SOURCE");
  std::string run;

  NativeFixture() {
    const auto options = json::dump(Json{{"data_dir", directory.path().string()}, {"start_workers", false}});
    context.reset(loom_init_ex(options.c_str(), nullptr));
    REQUIRE(context);
    auto& store = context->rt->knowledge().store();
    const auto pack = unwrap(context->rt->knowledge().pack());
    run = unwrap(store.begin_run(pack->hash(), Json{{"fixture", "native_consumer_synthetic_dev"}})).id;
    ab.assessment.counter.claims = {counter.id};
    LOOM_REQUIRE_OK(store.put_entities(run, {a, b, c, x, y}));
    LOOM_REQUIRE_OK(store.put_claims(run, {ab, bc, counter}));
    LOOM_REQUIRE_OK(store.finish_run(run, "done", Json::object()));
  }

  Json request() const {
    return Json{{"text", "How do the components depend on one another?"}, {"run", run},
        {"goal_type", "answer_question"}, {"budget_tokens", 10000}, {"relation_hops", 0}};
  }

  Json build(const Json& request) const {
    return take_json(loom_context_build(context.get(), json::dump(request).c_str()));
  }
};

std::set<std::string> item_refs(const Json& response) {
  std::set<std::string> refs;
  for (const auto& item : response.at("context_set").at("items")) refs.insert(item.at("ref").get<std::string>());
  return refs;
}

}  // namespace

TEST_SUITE("context_native_consumer") {
  TEST_CASE("C ABI explicit claims and recorded counters cross zero exploratory radius") {
    NativeFixture fixture;
    auto request = fixture.request();
    request["claim_targets"] = Json::array({fixture.ab.id});
    request["include_counter_evidence"] = true;
    request["detail_resolution"] = "raw";
    const auto enabled = fixture.build(request);
    REQUIRE_FALSE(enabled.contains("error"));
    CHECK(item_refs(enabled) == std::set<std::string>{fixture.ab.id, fixture.counter.id});
    CHECK(enabled["text"].get<std::string>().find("NATIVE-SOURCE-END") != std::string::npos);
    CHECK(enabled["text"].get<std::string>().find("NATIVE-COUNTER-SOURCE") != std::string::npos);
    CHECK(enabled["context_set"]["goal"]["params"].contains("counter_evidence"));
    request["include_counter_evidence"] = false;
    const auto disabled = fixture.build(request);
    REQUIRE_FALSE(disabled.contains("error"));
    CHECK(item_refs(disabled) == std::set<std::string>{fixture.ab.id});
    CHECK(enabled["context_set"]["id"] != disabled["context_set"]["id"]);
  }

  TEST_CASE("C ABI plan preserves per-thesis scope detail source provenance and shared item budget") {
    NativeFixture fixture;
    auto request = fixture.request();
    const Json source{{"kind", "active_task_spec"}, {"id", "caller-owned-spec"}, {"version", 7}};
    request["plan"] = Json{{"id", "native-plan"}, {"source_ref", source}, {"theses", Json::array({
        Json{{"id", "overview"}, {"text", "Explain the immediate relation."}, {"targets", Json::array({fixture.a.id})},
            {"relation_hops", 1}, {"detail_resolution", "label"}, {"require_counter_evidence", false}},
        Json{{"id", "evidence"}, {"text", "Show full source evidence across the wider scope."},
            {"targets", Json::array({fixture.a.id})}, {"relation_hops", 2}, {"detail_resolution", "raw"},
            {"require_counter_evidence", false}}})}};
    const auto response = fixture.build(request);
    REQUIRE_FALSE(response.contains("error"));
    const auto& set = response.at("context_set");
    const auto& trace = set.at("goal").at("params").at("plan_trace");
    CHECK(trace["plan"]["source_ref"] == source);
    CHECK(trace["run"] == fixture.run);
    REQUIRE(trace["theses"].size() == 2);
    CHECK(trace["theses"][0]["relation_hops"] == 1);
    CHECK(trace["theses"][0]["detail_resolution"] == "label");
    CHECK(trace["theses"][1]["relation_hops"] == 2);
    CHECK(trace["theses"][1]["detail_resolution"] == "raw");
    std::set<std::string> ab_resolutions;
    for (const auto& item : set.at("items")) {
      if (item["ref"] == fixture.ab.id) ab_resolutions.insert(item["resolution"].get<std::string>());
    }
    CHECK(ab_resolutions == std::set<std::string>{"label", "raw"});
    CHECK(item_refs(response).count(fixture.bc.id) == 1);
    CHECK(item_refs(response).count(fixture.counter.id) == 0);
    CHECK(set["used_tokens"].get<int>() <= request["budget_tokens"].get<int>());
    CHECK(fixture.build(request) == response);
  }

  TEST_CASE("C ABI malformed new fields return invalid_argument rather than silent defaults") {
    NativeFixture fixture;
    for (const auto& fields : {
        Json{{"claim_targets", "not-an-array"}}, Json{{"claim_targets", Json::array({42})}},
        Json{{"include_counter_evidence", 1}}, Json{{"candidate_channels", "tfidf"}},
        Json{{"candidate_channels", Json::array({Json{{"id", "tfidf"}, {"limit", 0}}})}},
        Json{{"candidate_scan_limit", true}}, Json{{"lexical_shadow", 1}}, Json{{"plan", Json::object()}},
        Json{{"plan", Json{{"id", "plan"}, {"theses", Json::array({Json{{"id", "one"}, {"text", "Question"},
            {"relation_hpos", 1}}})}}}}}) {
      auto request = fixture.request();
      request.update(fields);
      const auto response = fixture.build(request);
      REQUIRE(response.contains("error"));
      CHECK(response["error"]["code"] == "invalid_argument");
    }
    CHECK(take_json(loom_context_build(nullptr, "{}"))["error"]["code"] == "invalid_argument");
  }

  TEST_CASE("C ABI candidate channel retrieves disconnected material without model calls") {
    NativeFixture fixture;
    auto transport = std::make_shared<net::ScriptedTransport>();
    transport->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "unexpected model request"));
    fixture.context->rt->set_http_transport(transport);
    fixture.context->rt->config().set("semantic_model", "unused-test-model");
    fixture.context->rt->secrets().set("api_key", "synthetic-test-key");
    auto request = fixture.request();
    request["text"] = "NATIVE-COUNTER-SOURCE";
    request["detail_resolution"] = "raw";
    const auto graph_only = fixture.build(request);
    REQUIRE_FALSE(graph_only.contains("error"));
    CHECK(item_refs(graph_only).empty());
    request["candidate_channels"] = Json::array({Json{{"id", "tfidf"}, {"limit", 20}, {"min_score", 0.0}}});
    request["candidate_scan_limit"] = 100;
    request["lexical_shadow"] = true;
    const auto retrieved = fixture.build(request);
    REQUIRE_FALSE(retrieved.contains("error"));
    CHECK(item_refs(retrieved).count(fixture.counter.id) == 1);
    CHECK(retrieved["text"].get<std::string>().find("NATIVE-COUNTER-SOURCE") != std::string::npos);
    CHECK(retrieved["context_set"]["used_tokens"].get<int>() <= request["budget_tokens"].get<int>());
    CHECK(fixture.build(request) == retrieved);
    CHECK(transport->requests().empty());
  }

  TEST_CASE("C ABI uninstalled channel reports unavailable with no fabricated scores or fallback") {
    NativeFixture fixture;
    auto request = fixture.request();
    request["text"] = "NATIVE-COUNTER-SOURCE";
    request["candidate_channels"] = Json::array({Json{{"id", "caller-model-not-installed"}, {"limit", 20}}});
    request["lexical_shadow"] = true;
    const auto response = fixture.build(request);
    REQUIRE_FALSE(response.contains("error"));
    CHECK(item_refs(response).empty());
    const auto& retrieval = response.at("context_set").at("goal").at("params").at("candidate_retrieval");
    REQUIRE(retrieval.at("channels").size() == 1);
    const auto& channel = retrieval.at("channels").at(0);
    CHECK(channel.at("id") == "caller-model-not-installed");
    CHECK(channel.at("status") == "unavailable");
    CHECK(channel.at("scores").empty());
    CHECK(channel.at("hits").empty());
    CHECK(channel.at("scored_count") == 0);
    CHECK(retrieval.at("lexical_shadow").contains("diagnostic_gap"));
  }

  TEST_CASE("C ABI missing plan support is present in trace and rendered diagnostics") {
    NativeFixture fixture;
    auto request = fixture.request();
    request["plan"] = Json{{"id", "missing-native-plan"}, {"theses", Json::array({
        Json{{"id", "missing"}, {"text", "Explain a missing source."},
            {"claims", Json::array({"cl_absent_native"})}, {"require_counter_evidence", true}}})}};
    const auto response = fixture.build(request);
    REQUIRE_FALSE(response.contains("error"));
    CHECK(item_refs(response).empty());
    const auto trace = response["context_set"]["goal"]["params"]["plan_trace"];
    REQUIRE(trace.contains("theses"));
    CHECK(trace["theses"][0]["gaps"].dump().find("cl_absent_native") != std::string::npos);
    CHECK(response["text"].get<std::string>().find("INCOMPLETE") != std::string::npos);
  }
}
