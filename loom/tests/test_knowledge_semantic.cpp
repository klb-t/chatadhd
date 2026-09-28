#include <doctest/doctest.h>

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

struct Fixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<kb::Normalizer> norm;
  knowledge::KnowledgeConfig cfg;
  std::string run_id;
  std::vector<model::Observation> observations;
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;
  bool stopped = false;
  std::optional<Json> resume;
  Json saved_checkpoint;

  Fixture() {
    RuntimeOptions opts;
    opts.data_dir = dir.path().string();
    opts.start_workers = false;
    opts.http = http;  // unmatched requests fail; never a live transport
    rt = unwrap(Runtime::open(opts));
    rt->config().set("semantic_model", "cheap/semantic-model");
    rt->config().set("default_model", "expensive/conversation-model");
    rt->config().set("base_url", "https://semantic.test/v1/");
    rt->secrets().set("api_key", "test-only-key");
    pack = unwrap(rt->knowledge().pack());
    norm = std::make_unique<kb::Normalizer>(*pack);
    cfg.llm = "auto";
    run_id = unwrap(rt->knowledge().store().begin_run(pack->hash(), Json{{"test", "semantic"}})).id;
    add("ob_1", "Parser preserves source bytes.", 1, "main");
    model::Claim c;
    c.subject = entities[0].id;
    c.predicate = "source_statement";
    c.value = observations[0].text;
    c.assessment.origin = model::Origin::Archive;
    c.assessment.evidence = model::EvidenceClass::Observed;
    c.assessment.confidence = 0.8;
    model::Support s;
    s.observation = observations[0].id;
    s.quote = observations[0].text;
    s.locator = observations[0].locator;
    s.extractor = "test@1";
    c.assessment.support = {s};
    c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
    claims.push_back(c);
    unwrap(rt->knowledge().store().put_observations(run_id, observations));
    unwrap(rt->knowledge().store().put_entities(run_id, entities));
    unwrap(rt->knowledge().store().put_claims(run_id, claims));
  }

  void add(std::string id, std::string text, int ordinal, std::string branch) {
    model::Observation o;
    o.id = std::move(id);
    o.unit = "un_conversation";
    o.text = std::move(text);
    o.ordinal = ordinal;
    o.locator.source = "sha256:" + Sha256::hex("source fixture bytes");
    o.locator.member = "conversation.json";
    o.locator.json_pointer = "/messages/" + std::to_string(ordinal);
    o.attrs = Json{{"branch", branch}, {"seq", ordinal}, {"node", "node_" + std::to_string(ordinal)}};
    observations.push_back(o);
    model::Entity e;
    e.kind = "component";
    e.label = "Parser " + std::to_string(ordinal);
    e.canonical_key = "parser_" + std::to_string(ordinal);
    e.id = model::Entity::make_id(e.kind, e.canonical_key);
    e.attrs = Json{{"observations", Json::array({o.id})}, {"units", Json::array({o.unit})}};
    entities.push_back(e);
  }

  Json proposal(std::size_t index = 0) const {
    const auto& o = observations[index];
    return Json{{"kind", "structure"}, {"unknowns", Json::array()},
                {"claim", Json{{"subject", entities[index].id}, {"predicate", "states_requirement"},
                               {"object", ""}, {"value", o.text},
                               {"qualifiers", Json{{"extra", Json{{"polarity", "positive"}, {"assertion_context", "asserted"},
                                                                    {"topic", "parser"}}}}},
                               {"assessment", Json{{"basis", Json{{"support", Json::array({Json{{"observation", o.id},
                                                                                                {"quote", o.text}, {"byte_start", 0},
                                                                                                {"byte_len", o.text.size()}}})}}},
                                                    {"premises", Json{{"claims", Json::array()}}}}}}}};
  }

  void reply(const Json& proposals) {
    Json content{{"schema_version", 1}, {"proposals", proposals}};
    http->expect("POST", "https://semantic.test/v1/chat/completions",
                 net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}}));
  }

  Result<Json> call(Json params = Json::object()) {
    knowledge::StageContext ctx{*rt, rt->knowledge().store(), pack, *norm, cfg, run_id, "extract", std::move(params),
                                Json::object(), cfg.prior_filter(), {}, [this] { return stopped; },
                                [this](const Json& j) -> Status { saved_checkpoint = j; return {}; }, resume};
    return extract::propose_semantics(ctx, observations, entities, claims);
  }

  std::int64_t count(std::string_view table) {
    auto lock = rt->db().lock();
    return unwrap(rt->db().conn().query_int("SELECT COUNT(*) FROM " + std::string(table))).value_or(0);
  }

  Json candidate() {
    auto lock = rt->db().lock();
    return unwrap(json::parse(unwrap(rt->db().conn().query_text("SELECT payload FROM loom_kb_candidates ORDER BY id LIMIT 1")).value()));
  }
};

class StopTransport final : public net::HttpTransport {
 public:
  StopTransport(std::shared_ptr<net::ScriptedTransport> inner, bool& stop, bool before)
      : inner_(std::move(inner)), stop_(stop), before_(before) {}
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* sink,
                                  const CancelToken* cancel) override {
    if (before_) stop_ = true;
    auto result = inner_->send(request, sink, cancel);
    stop_ = true;
    return result;
  }
  std::string name() const override { return "scripted-stop"; }
 private:
  std::shared_ptr<net::ScriptedTransport> inner_;
  bool& stop_;
  bool before_;
};

}  // namespace

TEST_SUITE("knowledge_semantic") {
  TEST_CASE("explicit model call produces only grounded candidates and cache replay is stable") {
    Fixture f;
    f.reply(Json::array({f.proposal()}));
    const Json before = f.claims[0].to_json();
    auto first = unwrap(f.call());
    CHECK(first["status"] == "completed");
    CHECK(first["accepted"] == 1);
    CHECK(first["requests"] == 1);
    REQUIRE(f.http->requests().size() == 1);
    const auto request = unwrap(json::parse(f.http->requests()[0].body));
    CHECK(request["model"] == "cheap/semantic-model");
    CHECK(request["max_tokens"] == 1600);
    CHECK(f.count("loom_kb_candidates") == 1);
    CHECK(f.count("loom_kb_claims") == 1);
    auto candidate = f.candidate();
    CHECK(candidate["run_id"] == f.run_id);
    CHECK(candidate["group"]["branch"] == "main");
    CHECK(candidate["proposal"]["claim"]["assessment"]["basis"]["support"][0]["quote"] == f.observations[0].text);
    CHECK(candidate["proposal"]["claim"]["assessment"]["basis"]["support"][0]["locator"]["source"] == f.observations[0].locator.source);
    CHECK(unwrap(f.rt->knowledge().store().get_claim(f.run_id, f.claims[0].id))->to_json() == before);
    auto second = unwrap(f.call());
    CHECK(second["requests"] == 0);
    CHECK(second["cache_hits"] == 1);
    CHECK(second["output"] == first["output"]);
    CHECK(second["candidate_ids"] == first["candidate_ids"]);
    CHECK(f.http->requests().size() == 1);
    CHECK(f.count("loom_kb_candidates") == 1);
    // The identical prompt reuses the cache across runs, but candidates keep
    // their own run identity and cannot overwrite the first run's proposals.
    f.run_id = unwrap(f.rt->knowledge().store().begin_run(f.pack->hash(), Json{{"test", "another run"}})).id;
    auto other = unwrap(f.call());
    CHECK(other["cache_hits"] == 1);
    CHECK(other["candidate_ids"] != first["candidate_ids"]);
    CHECK(f.count("loom_kb_candidates") == 2);
  }

  TEST_CASE("off, missing configuration and disabled semantics never request a provider") {
    Fixture f;
    f.cfg.llm = "off";
    CHECK(unwrap(f.call())["status"] == "off");
    f.cfg.llm = "auto";
    f.rt->config().set("semantic_model", "");
    CHECK(unwrap(f.call())["reason"] == "semantic_model_missing");
    f.rt->config().set("semantic_model", "cheap/semantic-model");
    f.rt->secrets().erase("api_key");
    const auto missing = extract::semantic_fingerprint(*f.rt, Json::object(), "auto");
    CHECK(unwrap(f.call())["reason"] == "api_key_missing");
    f.rt->secrets().set("api_key", "another-test-key");
    CHECK(extract::semantic_fingerprint(*f.rt, Json::object(), "auto") != missing);
    f.rt->config().set("semantic_analysis", false);
    CHECK(unwrap(f.call())["reason"] == "semantic_analysis_disabled");
    CHECK(f.http->requests().empty());
    CHECK(f.count("loom_kb_candidates") == 0);
  }

  TEST_CASE("forged quotes, missing polarity and dangling premises are rejected without cache") {
    Fixture f;
    auto forged = f.proposal();
    forged["claim"]["assessment"]["basis"]["support"][0]["quote"] = "Invented source bytes.";
    auto polarity = f.proposal();
    polarity["claim"]["qualifiers"]["extra"].erase("polarity");
    auto dangling = f.proposal();
    dangling["claim"]["assessment"]["premises"]["claims"] = Json::array({"cl_missing"});
    auto subject = f.proposal();
    subject["claim"]["subject"] = "e_invented";
    f.reply(Json::array({forged, polarity, dangling, subject}));
    auto stats = unwrap(f.call());
    CHECK(stats["rejected"] == 4);
    CHECK(stats["accepted"] == 0);
    CHECK(stats["status"] == "partial");
    CHECK(f.count("loom_kb_candidates") == 0);
    CHECK(f.count("loom_kb_llm_cache") == 0);
    CHECK(f.count("loom_kb_claims") == 1);
  }

  TEST_CASE("branch and chunk boundaries prohibit references to other source chunks") {
    Fixture f;
    f.add("ob_2", "Film lighting has a different scope.", 2, "side");
    auto outside = f.proposal();
    outside["claim"]["assessment"]["basis"]["support"][0]["observation"] = "ob_2";
    f.reply(Json::array({outside}));
    f.reply(Json::array({f.proposal(1)}));
    auto stats = unwrap(f.call());
    CHECK(stats["chunks"] == 2);
    CHECK(stats["rejected"] == 1);
    CHECK(stats["accepted"] == 1);
    REQUIRE(f.http->requests().size() == 2);
    for (const auto& request : f.http->requests()) {
      const auto body = unwrap(json::parse(request.body));
      const auto input = unwrap(json::parse(body["messages"][1]["content"].get<std::string>()));
      CHECK(input["observations"].size() == 1);
      CHECK(input["entities"].size() == 1);
    }
    CHECK(f.candidate()["group"]["branch"] == "side");
  }

  TEST_CASE("HTTP and schema failure preserve canonical claims and are never cached") {
    Fixture f;
    f.http->expect("POST", "https://semantic.test", net::ScriptedTransport::Reply::fail(Errc::Network, "scripted outage"));
    auto failed = unwrap(f.call());
    CHECK(failed["status"] == "failed");
    CHECK(failed["failed"] == 1);
    CHECK(f.count("loom_kb_claims") == 1);
    CHECK(f.count("loom_kb_candidates") == 0);
    CHECK(f.count("loom_kb_llm_cache") == 0);
    f.http->expect("POST", "https://semantic.test", net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array()}}));
    CHECK(unwrap(f.call())["status"] == "failed");
    CHECK(f.count("loom_kb_llm_cache") == 0);
    f.reply(Json::array({f.proposal()}));
    CHECK(unwrap(f.call())["accepted"] == 1);
    CHECK(f.http->requests().size() == 3);
  }

  TEST_CASE("budgets bound requests, outputs, bytes and stable cache replay coverage") {
    Fixture f;
    f.add("ob_2", "Parser keeps the prior statement.", 2, "main");
    Json params{{"semantic", Json{{"max_requests", 1}, {"max_observations", 1}, {"max_output_tokens", 123}}}};
    f.reply(Json::array({f.proposal()}));
    auto stats = unwrap(f.call(params));
    CHECK(stats["status"] == "budget_exhausted");
    CHECK(stats["requests"] == 1);
    CHECK(stats["chunks"] == 2);
    CHECK(unwrap(json::parse(f.http->requests()[0].body))["max_tokens"] == 123);
    auto again = unwrap(f.call(params));
    CHECK(again["output"] == stats["output"]);
    CHECK(again["requests"] == 0);
    CHECK(f.http->requests().size() == 1);
    CHECK(unwrap(f.call(Json{{"semantic", Json{{"max_requests", 0}}}}))["requests"] == 0);
    CHECK(unwrap(f.call(Json{{"semantic", Json{{"max_chunk_bytes", 1}}}}))["requests"] == 0);
    CHECK(!f.call(Json{{"semantic", Json{{"max_requests", 999}}}}));
    CHECK(!f.call(Json{{"semantic", Json{{"typo", 1}}}}));
    CHECK(f.http->requests().size() == 1);
  }

  TEST_CASE("runtime identity mismatch prevents resumed run from using another model") {
    Fixture f;
    Json expected = extract::semantic_fingerprint(*f.rt, Json::object(), "auto");
    CHECK(json::dump(expected).find("test-only-key") == std::string::npos);
    f.rt->config().set("semantic_model", "other/model");
    auto result = f.call(Json{{"_semantic_identity", expected}});
    REQUIRE(!result);
    CHECK(result.error().code == Errc::Conflict);
    CHECK(f.http->requests().empty());
  }

  TEST_CASE("bounded sampling includes late source chunks and reports omitted locations") {
    Fixture f;
    f.add("ob_2", "Earlier unrelated music topic.", 2, "main");
    f.add("ob_3", "Another unrelated topic.", 3, "main");
    f.add("ob_4", "Late project needs an explicit interface.", 4, "main");
    f.reply(Json::array({f.proposal(0)}));
    f.reply(Json::array({f.proposal(3)}));
    auto stats = unwrap(f.call(Json{{"semantic", Json{{"max_requests", 2}, {"max_observations", 1}}}}));
    CHECK(stats["accepted"] == 2);
    CHECK(stats["omitted_observations"] == 2);
    REQUIRE(stats["selected_chunks"].size() == 2);
    CHECK(stats["selected_chunks"][1]["observations"][0]["id"] == "ob_4");
    CHECK(stats["skipped"][0]["observations"][0]["locator"]["member"] == "conversation.json");
  }

  TEST_CASE("UTF-8 support bytes are verified and response/proposal caps fail closed") {
    Fixture f;
    f.observations[0].text = "Żółć remains quoted exactly.";
    unwrap(f.rt->knowledge().store().put_observations(f.run_id, f.observations));
    auto valid = f.proposal();
    f.reply(Json::array({valid}));
    CHECK(unwrap(f.call())["accepted"] == 1);
    auto split = valid;
    split["claim"]["assessment"]["basis"]["support"][0]["byte_start"] = 1;
    f.reply(Json::array({split}));
    // Different decode budget avoids the first validated cache entry.
    CHECK(unwrap(f.call(Json{{"semantic", Json{{"max_output_tokens", 1500}}}}))["rejected"] == 1);
    f.reply(Json::array({valid, valid}));
    CHECK(unwrap(f.call(Json{{"semantic", Json{{"max_proposals", 1}}}}))["failed"] == 1);
    f.reply(Json::array({valid}));
    CHECK(unwrap(f.call(Json{{"semantic", Json{{"max_response_bytes", 1}, {"max_output_tokens", 1400}}}}))["failed"] == 1);
    CHECK(f.count("loom_kb_claims") == 1);
  }

  TEST_CASE("late source selection follows sequence, not unknown-branch node UUID order") {
    Fixture f;
    f.add("ob_late", "Late project has its own source scope.", 99, "");
    f.add("ob_early", "Early unrelated topic.", 2, "");
    const std::vector<std::string> nodes{"node_a", "node_b", "node_c"};
    for (std::size_t i = 0; i < f.observations.size(); ++i) {
      f.observations[i].attrs.erase("branch");
      f.observations[i].attrs["node"] = nodes[i];
    }
    f.reply(Json::array({f.proposal(0)}));
    f.reply(Json::array({f.proposal(1)}));
    auto result = unwrap(f.call(Json{{"semantic", Json{{"max_requests", 2}}}}));
    CHECK(result["accepted"] == 2);
    REQUIRE(result["selected_chunks"].size() == 2);
    CHECK(result["selected_chunks"][1]["observations"][0]["id"] == "ob_late");
  }

  TEST_CASE("partial response retries keep a stable candidate and first provenance") {
    Fixture f;
    const auto valid = f.proposal();
    auto invalid = valid;
    invalid["claim"]["subject"] = "e_missing";
    f.reply(Json::array({valid, invalid}));
    auto first = unwrap(f.call());
    REQUIRE(first["accepted"] == 1);
    CHECK(f.count("loom_kb_llm_cache") == 0);
    const auto provenance = f.candidate()["provenance"];
    f.reply(Json::array({valid}));
    auto second = unwrap(f.call());  // a fresh explicit invocation, no resume
    CHECK(second["accepted"] == 1);
    CHECK(second["candidate_ids"] == first["candidate_ids"]);
    CHECK(f.count("loom_kb_candidates") == 1);
    CHECK(f.candidate()["provenance"] == provenance);
    CHECK(f.count("loom_kb_llm_cache") == 1);
    // A stricter response cap must not reuse the previously large cached reply.
    f.reply(Json::array({valid}));
    auto capped = unwrap(f.call(Json{{"semantic", Json{{"max_response_bytes", 1}}}}));
    CHECK(capped["failed"] == 1);
    CHECK(capped["cache_hits"] == 0);
  }

  TEST_CASE("pause during the last HTTP request cannot persist or spend twice on resume") {
    for (const bool before : {false, true}) {
      Fixture f;
      f.reply(Json::array({f.proposal()}));
      f.rt->set_http_transport(std::make_shared<StopTransport>(f.http, f.stopped, before));
      auto paused = f.call();
      REQUIRE(!paused);
      CHECK(paused.error().code == Errc::Paused);
      CHECK(f.count("loom_kb_candidates") == 0);
      CHECK(f.count("loom_kb_llm_cache") == 0);
      REQUIRE(f.saved_checkpoint.contains("semantic"));
      CHECK(f.saved_checkpoint["semantic"]["requests_spent"] == 1);
      CHECK(json::dump(f.saved_checkpoint).find("test-only-key") == std::string::npos);
      f.resume = f.saved_checkpoint;
      f.stopped = false;
      f.rt->set_http_transport(f.http);
      auto resumed = unwrap(f.call());
      CHECK(resumed["requests"] == 0);
      CHECK(resumed["requests_spent"] == 1);
      CHECK(resumed["retry_required"] == true);
      CHECK(resumed["rejections"][0]["reason"] == "previous_attempt_uncached");
      CHECK(f.http->requests().size() == 1);
      CHECK(f.count("loom_kb_claims") == 1);
    }
  }

  TEST_CASE("resume reuses successful response cache within the persisted attempt budget") {
    Fixture f;
    f.reply(Json::array({f.proposal()}));
    auto first = unwrap(f.call());
    f.resume = f.saved_checkpoint;
    auto resumed = unwrap(f.call());
    CHECK(resumed["output"] == first["output"]);
    CHECK(resumed["requests"] == 0);
    CHECK(resumed["requests_spent"] == 1);
    CHECK(resumed["accepted"] == 1);
    CHECK(resumed["cache_hits"] == 1);
    CHECK(f.http->requests().size() == 1);
    f.resume = Json{{"semantic", Json{{"identity", Json::object()}}}};
    auto bad = f.call();
    REQUIRE(!bad);
    CHECK(bad.error().code == Errc::Conflict);
  }
}
