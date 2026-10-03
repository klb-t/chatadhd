#include <doctest/doctest.h>

#include <functional>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/knowledge_semantic.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

// Offline transport builds a response from the actual request packet, so the
// test does not duplicate request hashing or precompute implementation IDs.
class PacketTransport final : public net::HttpTransport {
 public:
  std::vector<net::HttpRequest> requests;
  std::function<Json(const Json&)> respond;
  std::string finish_reason = "stop";
  Json wrapper_extra;
  std::function<void()> after_reply;
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* sink,
                                const CancelToken*) override {
    requests.push_back(request);
    const auto body = unwrap(json::parse(request.body));
    const auto packet = unwrap(json::parse(body["messages"][1]["content"].get<std::string>()));
    const Json result = respond(packet);
    Json wire{{"choices", Json::array({Json{{"finish_reason", finish_reason},
                       {"message", Json{{"content", json::dump(result)}}}}})}};
    if (!wrapper_extra.is_null()) wire["extra"] = wrapper_extra;
    const std::string bytes = json::dump(wire);
    if (after_reply) after_reply();
    if (sink && sink->on_data && !sink->on_data(bytes)) return Error(Errc::Cancelled, "test sink stopped");
    net::HttpResponse response;
    response.status = 200;
    if (!sink) response.body = bytes;
    return response;
  }
  std::string name() const override { return "offline-packet"; }
};

Json support_for(const Json& input) {
  const auto& o = input["source_packet"]["observations"][0];
  const std::string text = o["text"].get<std::string>();
  return Json::array({Json{{"observation", o["id"]}, {"byte_start", 0}, {"byte_len", text.size()}, {"quote", text}}});
}

Json empty_bundle(const Json& input) {
  const auto support = support_for(input);
  return Json{{"schema", "loom.candidate_graph/1"}, {"packet_id", input["source_packet"]["snapshot_id"]},
              {"entity_drafts", Json::array()}, {"claim_drafts", Json::array()}, {"roots", Json::array()},
              {"coverage", Json::array({Json{{"support", support}, {"status", "unsupported"},
                  {"reason", "author test deliberately abstains"}, {"drafts", Json::array()}}})},
              {"unknowns", Json::array({Json{{"support", support}, {"reason", "meaning not resolved"}}})}};
}

Json application_bundle(const Json& input) {
  Json out = empty_bundle(input);
  const Json support = support_for(input);
  auto entity = [&](std::string handle, std::string kind, Json attrs) {
    out["entity_drafts"].push_back(Json{{"handle", handle}, {"kind", kind}, {"label", handle},
                                      {"attrs", attrs}, {"support", support}});
  };
  entity("@scope", "scope", Json{{"scope_type", "assertion"}, {"assertion_context", "asserted"}});
  entity("@apply", "expression_occurrence", Json::object());
  entity("@pred", "term_occurrence", Json{{"term_type", "predicate"}, {"symbol", "moves"}});
  entity("@bird", "term_occurrence", Json{{"term_type", "constant"}, {"symbol", "bird"}});
  auto claim = [&](std::string subject, std::string predicate, std::string object, Json value,
                   std::string port = "") {
    Json extra{{"polarity", "positive"}, {"assertion_context", "asserted"}};
    if (!port.empty()) { extra["port"] = port; extra["ordinal"] = 0; }
    out["claim_drafts"].push_back(Json{{"handle", "@c" + std::to_string(out["claim_drafts"].size())},
      {"subject", subject}, {"predicate", predicate}, {"object", object}, {"value", value},
      {"qualifiers", Json{{"scope", "@scope"}, {"extra", extra}}},
      {"assessment", Json{{"basis", Json{{"support", support}}}, {"premises", Json{{"claims", Json::array()}}}}}});
  };
  for (const char* node : {"@apply", "@pred", "@bird"}) claim(node, "in_scope", "@scope", nullptr);
  claim("@apply", "operation_type", "", "predicate_application");
  claim("@apply", "operand", "@pred", nullptr, "predicate");
  claim("@apply", "operand", "@bird", nullptr, "argument");
  out["roots"] = Json::array({"@apply"});
  out["coverage"] = Json::array({Json{{"support", support}, {"status", "represented"},
    {"reason", "author-supplied application"}, {"drafts", Json::array({"@apply"})}}});
  out["unknowns"] = Json::array();
  return out;
}

Json envelope(const Json& input, Json bundles) {
  return Json{{"schema_version", 2}, {"packet_hash", input["packet_hash"]}, {"bundles", bundles}};
}

struct GraphFixture {
  fsutil::TempDir dir;
  std::shared_ptr<PacketTransport> http = std::make_shared<PacketTransport>();
  std::unique_ptr<Runtime> rt;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<kb::Normalizer> normalizer;
  knowledge::KnowledgeConfig config;
  std::string run;
  std::vector<model::Observation> observations;
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;
  std::optional<Json> resume;
  Json checkpoint;
  bool stopped = false;

  GraphFixture() {
    RuntimeOptions opts;
    opts.data_dir = dir.path().string(); opts.start_workers = false; opts.http = http;
    rt = unwrap(Runtime::open(opts));
    rt->config().set("semantic_model", "test/semantic");
    rt->config().set("base_url", "https://offline.invalid/v1");
    rt->secrets().set("api_key", "offline-test-key");
    pack = unwrap(rt->knowledge().pack());
    normalizer = std::make_unique<kb::Normalizer>(*pack);
    config.llm = "auto";
    run = unwrap(rt->knowledge().store().begin_run(pack->hash(), Json{{"test", "graph-mode"}})).id;
    model::Observation o;
    o.id = "ob_bird"; o.unit = "un_bird"; o.text = "Żółty bird moves."; o.lang = "mixed";
    o.speaker = "user"; o.artifact_type = "chat"; o.ordinal = 4;
    o.locator.source = "sha256:" + Sha256::hex("author graph test");
    o.locator.member = "own-example.json"; o.locator.json_pointer = "/messages/4";
    o.attrs = Json{{"branch", "main"}, {"seq", 4}, {"source_metadata", Json{{"preserve", true}}}};
    observations.push_back(o);
    http->respond = [](const Json& input) { return envelope(input, Json::array({application_bundle(input)})); };
  }

  void add_prior() {
    model::Entity e;
    e.id = "e_bird"; e.kind = "concept"; e.label = "bird"; e.canonical_key = "bird";
    e.attrs = Json{{"observations", Json::array({observations[0].id})}, {"retained", true}};
    e.confidence = 0.42;
    entities.push_back(e);
    model::Claim c;
    c.id = "cl_prior"; c.subject = e.id; c.predicate = "source_statement"; c.value = observations[0].text;
    c.assessment.origin = model::Origin::Archive; c.assessment.evidence = model::EvidenceClass::Observed;
    c.assessment.confidence = 0.37;
    c.assessment.premises.assumptions = {"explicit author assumption remains visible"};
    model::Support support;
    support.observation = observations[0].id; support.quote = observations[0].text;
    support.locator = observations[0].locator; support.extractor = "author@1";
    c.assessment.support = {support};
    claims.push_back(c);
  }

  Result<Json> call(Json semantic = Json::object()) {
    semantic["representation"] = "occurrence_graph_v1";
    knowledge::StageContext ctx{*rt, rt->knowledge().store(), pack, *normalizer, config, run, "extract",
      Json{{"semantic", semantic}}, Json::object(), config.prior_filter(), {}, [this] { return stopped; },
      [this](const Json& state) -> Status { checkpoint = state; return {}; }, resume};
    return extract::propose_semantics(ctx, observations, entities, claims);
  }

  std::int64_t count(std::string_view table) {
    auto lock = rt->db().lock();
    return unwrap(rt->db().conn().query_int("SELECT COUNT(*) FROM " + std::string(table))).value_or(0);
  }
  Json candidate() {
    auto lock = rt->db().lock();
    return unwrap(json::parse(unwrap(rt->db().conn().query_text("SELECT payload FROM loom_kb_candidates LIMIT 1")).value()));
  }
};

}  // namespace

TEST_SUITE("knowledge_semantic_graph") {
  TEST_CASE("graph mode accepts source occurrences with no extracted entities and only queues candidates") {
    GraphFixture f;
    auto stats = unwrap(f.call());
    CHECK(stats["status"] == "completed");
    CHECK(stats["representation"] == "occurrence_graph_v1");
    CHECK(stats["accepted_bundles"] == 1);
    CHECK(stats["entity_drafts"] == 4);
    CHECK(stats["claim_drafts"] == 6);
    CHECK(stats["abstentions"] == 0);
    CHECK(f.count("loom_kb_candidates") == 1);
    CHECK(f.count("loom_kb_claims") == 0);
    CHECK(f.count("loom_kb_entities") == 0);
    const auto candidate = f.candidate();
    CHECK(candidate["representation"] == "occurrence_graph_v1");
    CHECK(candidate["proposal"]["validation"]["valid"] == true);
    CHECK(json::canonical(candidate["source_packet"]["observations"][0]) == json::canonical(f.observations[0].to_json()));
    CHECK(candidate["packet_hash"] == Sha256::hex(json::canonical(candidate["source_packet"])));
    REQUIRE(f.http->requests.size() == 1);
    const auto request = unwrap(json::parse(f.http->requests[0].body));
    CHECK(request["model"] == "test/semantic");
    CHECK(request["max_tokens"] == 1600);
    CHECK(stats["response_outcomes"][0]["finish_reason"] == "stop");
  }

  TEST_CASE("local full assessments survive and unrelated branch context is not supplied") {
    GraphFixture f;
    f.add_prior();
    f.entities[0].attrs["observations"].push_back("ob_prior_context");
    f.claims[0].assessment.premises.claims.push_back("cl_prior_context");
    auto outside = f.entities[0]; outside.id = "e_outside";
    outside.attrs["observations"] = Json::array({"ob_other_branch"}); f.entities.push_back(outside);
    auto claim = f.claims[0]; claim.id = "cl_outside"; claim.subject = outside.id;
    claim.assessment.support[0].observation = "ob_other_branch"; f.claims.push_back(claim);
    unwrap(f.call());
    const auto packet = f.candidate()["source_packet"];
    REQUIRE(packet["entities"].size() == 1);
    REQUIRE(packet["claims"].size() == 1);
    CHECK(json::canonical(packet["entities"][0]) == json::canonical(f.entities[0].to_json()));
    CHECK(json::canonical(packet["claims"][0]) == json::canonical(f.claims[0].to_json()));
    CHECK(json::dump(packet).find("cl_outside") == std::string::npos);
    CHECK(packet["metadata"]["context_selection"]["external_known_references"]["entity_observations"] == Json::array({"ob_prior_context"}));
    CHECK(packet["metadata"]["context_selection"]["external_known_references"]["claim_premises"] == Json::array({"cl_prior_context"}));
    CHECK(packet["metadata"]["context_selection"]["external_references_are_observation_evidence"] == false);
    CHECK(f.claims[0].assessment.confidence == 0.37);
  }

  TEST_CASE("packet mismatch and forged UTF-8 support never cache or persist") {
    for (const bool wrong_packet : {false, true}) {
      GraphFixture f;
      f.http->respond = [=](const Json& input) {
        auto bundle = application_bundle(input);
        if (!wrong_packet) bundle["entity_drafts"][0]["support"][0]["quote"] = "Invented";
        auto response = envelope(input, Json::array({bundle}));
        if (wrong_packet) response["packet_hash"] = "different-source";
        return response;
      };
      const auto stats = unwrap(f.call());
      CHECK(stats["accepted_bundles"] == 0);
      CHECK(f.count("loom_kb_candidates") == 0);
      CHECK(f.count("loom_kb_llm_cache") == 0);
      CHECK(stats[wrong_packet ? "failed" : "rejected"] == 1);
    }
  }

  TEST_CASE("duplicate bundles cache replay and resume do not inflate graph counts") {
    GraphFixture f;
    f.http->respond = [](const Json& input) {
      auto bundle = application_bundle(input);
      return envelope(input, Json::array({bundle, bundle}));
    };
    const auto first = unwrap(f.call());
    f.resume = unwrap(json::parse(json::canonical(f.checkpoint)));
    const auto second = unwrap(f.call());
    CHECK(second["cache_hits"] == 1);
    CHECK(second["requests"] == 0);
    CHECK(second["requests_spent"] == 1);
    for (const char* field : {"accepted", "accepted_bundles", "entity_drafts", "claim_drafts", "abstentions", "output"})
      CHECK(second[field] == first[field]);
    CHECK(second["accepted_bundles"] == 1);
    CHECK(f.count("loom_kb_candidates") == 1);
    CHECK(f.http->requests.size() == 1);
  }

  TEST_CASE("unknown-only bundles retain coverage and empty envelopes report abstention") {
    for (const bool empty : {false, true}) {
      GraphFixture f;
      f.http->respond = [=](const Json& input) { return envelope(input, empty ? Json::array() : Json::array({empty_bundle(input)})); };
      const auto stats = unwrap(f.call());
      CHECK(stats["abstentions"] == 1);
      CHECK(stats["entity_drafts"] == 0);
      CHECK(stats["claim_drafts"] == 0);
      CHECK(stats["accepted_bundles"] == (empty ? 0 : 1));
      if (!empty) {
        CHECK(f.candidate()["proposal"]["bundle"]["coverage"][0]["status"] == "unsupported");
        CHECK(f.candidate()["graph_summary"]["operation_nodes"] == 0);
      }
      f.resume = f.checkpoint;
      CHECK(unwrap(f.call())["abstentions"] == 1);
    }
  }

  TEST_CASE("reported truncation rejects even a complete JSON prefix and does not retry") {
    GraphFixture f;
    f.http->finish_reason = "length";
    const auto stats = unwrap(f.call());
    CHECK(stats["failed"] == 1);
    CHECK(stats["response_outcomes"][0]["truncated"] == true);
    CHECK(stats["rejections"][0]["reason"] == "provider_output_truncated");
    CHECK(f.count("loom_kb_candidates") == 0);
    CHECK(f.count("loom_kb_llm_cache") == 0);
    f.resume = f.checkpoint;
    const auto resumed = unwrap(f.call());
    CHECK(resumed["requests"] == 0);
    CHECK(resumed["retry_required"] == true);
    CHECK(f.http->requests.size() == 1);
  }

  TEST_CASE("deep model JSON and outer response JSON are rejected before recursive parsing helpers") {
    for (const bool wrapper : {false, true}) {
      GraphFixture f;
      Json nested = nullptr;
      for (int depth = 0; depth < 130; ++depth) nested = Json::array({nested});
      if (wrapper) f.http->wrapper_extra = nested;
      else f.http->respond = [nested](const Json& input) {
        auto bundle = application_bundle(input);
        bundle["entity_drafts"][0]["attrs"]["untrusted"] = nested;
        return envelope(input, Json::array({bundle}));
      };
      const auto stats = unwrap(f.call());
      CHECK(stats["failed"] == 1);
      CHECK(stats["rejections"][0]["reason"] == "response_json_nesting_limit");
      CHECK(f.count("loom_kb_candidates") == 0);
      CHECK(f.count("loom_kb_llm_cache") == 0);
    }
    GraphFixture quoted;
    quoted.observations[0].text = std::string(150, '[') + " quoted \" braces { and escape \\";
    CHECK(unwrap(quoted.call())["accepted_bundles"] == 1);
  }

  TEST_CASE("full assessment over budget is omitted without truncation or a request") {
    GraphFixture f;
    f.add_prior();
    f.claims[0].assessment.premises.assumptions.push_back(std::string(17000, 'x'));
    const auto stats = unwrap(f.call());
    CHECK(stats["requests"] == 0);
    CHECK(stats["omitted_observations"] == 1);
    CHECK(stats["skipped"][0]["reason"] == "request_exceeds_chunk_budget");
    CHECK(f.http->requests.empty());
    CHECK(f.claims[0].assessment.premises.assumptions.back().size() == 17000);
  }

  TEST_CASE("reduced packet policy is checked before any paid attempt") {
    GraphFixture f;
    f.add_prior();
    std::map<std::string, Json> documents;
    for (const auto& path : f.pack->files()) documents[path] = f.pack->file(path);
    documents["policy/candidate_graph.json"]["limits"]["max_packet_bytes"] = 1024;
    f.pack = unwrap(kb::Pack::from_documents(std::move(documents)));
    f.normalizer = std::make_unique<kb::Normalizer>(*f.pack);
    const auto stats = unwrap(f.call());
    CHECK(stats["requests"] == 0);
    CHECK(stats["requests_spent"] == 0);
    CHECK(stats["omitted_observations"] == 1);
    CHECK(stats["skipped"][0]["reason"] == "source_packet_validation");
    CHECK(f.http->requests.empty());
  }

  TEST_CASE("graph requests preserve cancellation checkpoint and hard request caps") {
    GraphFixture f;
    CHECK(unwrap(f.call(Json{{"max_requests", 0}}))["requests"] == 0);
    CHECK(!f.call(Json{{"max_requests", 9}}));
    f.http->after_reply = [&] { f.stopped = true; };
    const auto paused = f.call();
    REQUIRE(!paused);
    CHECK(paused.error().code == Errc::Paused);
    CHECK(f.checkpoint["semantic"]["requests_spent"] == 1);
    CHECK(f.count("loom_kb_candidates") == 0);
    f.stopped = false; f.http->after_reply = {}; f.resume = f.checkpoint;
    CHECK(unwrap(f.call())["retry_required"] == true);
    CHECK(f.http->requests.size() == 1);
  }

  TEST_CASE("representation is separate from integer budgets and vocabulary changes identity") {
    GraphFixture f;
    const Json graph{{"semantic", Json{{"representation", "occurrence_graph_v1"}}}};
    const auto relation = extract::semantic_fingerprint(*f.rt, Json::object(), "auto", f.pack->policy("candidate_graph"));
    const auto identity = extract::semantic_fingerprint(*f.rt, graph, "auto", f.pack->policy("candidate_graph"));
    CHECK(relation["representation"] == "relation_v1");
    CHECK(identity["schema_version"] == 2);
    CHECK_FALSE(identity["limits"].contains("representation"));
    CHECK(relation["prompt_hash"] != identity["prompt_hash"]);
    auto changed = f.pack->policy("candidate_graph"); changed["test_revision"] = 1;
    CHECK(extract::semantic_fingerprint(*f.rt, graph, "auto", changed) != identity);
    knowledge::StageContext ctx{*f.rt, f.rt->knowledge().store(), f.pack, *f.normalizer, f.config, f.run, "extract",
      Json{{"semantic", Json{{"representation", 7}}}}, Json::object(), f.config.prior_filter(), {}, {}, {}, {}};
    CHECK_FALSE(extract::propose_semantics(ctx, f.observations, f.entities, f.claims));
    CHECK(f.http->requests.empty());
  }
}
