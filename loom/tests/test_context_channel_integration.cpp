// Independent integration checks of injected candidate instruments. All data
// and instrument outputs are synthetic; these tests do not measure relevance.
#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using namespace loom::context;
using loom::test::unwrap;

namespace {

class InjectedCandidateChannel final : public CandidateChannel {
 public:
  std::vector<RetrievalHit> hits;
  bool throws = false;
  int calls = 0;
  std::size_t received_corpus_count = 0;

  RetrievalBatch retrieve(std::string_view, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest&) override {
    ++calls;
    received_corpus_count = corpus.size();
    if (throws) throw std::runtime_error("private instrument response must not enter diagnostics");
    RetrievalBatch batch;
    batch.method = "synthetic_nonlexical_instrument";
    batch.corpus_count = corpus.size();
    batch.hits = hits;  // deliberately does not enforce request thresholds/limit
    batch.scores = hits;
    batch.scored_count = hits.size();
    batch.eligible_count = hits.size();
    return batch;
  }
};

struct ChannelFixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  kb::KnowledgeStore* store;
  std::string run;
  std::vector<Claim> claims;

  ChannelFixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
    pack = unwrap(runtime->knowledge().pack());
    store = &runtime->knowledge().store();
    run = unwrap(store->begin_run(pack->hash(), Json{{"fixture", "independent_channel_integration"}})).id;
    std::vector<Entity> entities;
    for (const auto* suffix : {"a", "b", "c", "d", "e"}) {
      Entity entity;
      entity.kind = "component";
      entity.canonical_key = std::string("separate subject ") + suffix;
      entity.id = Entity::make_id(entity.kind, entity.canonical_key);
      entity.label = entity.canonical_key;
      entities.push_back(entity);
      Claim claim;
      claim.id = std::string("cl_channel_") + suffix;
      claim.subject = entity.id;
      claim.predicate = "records";
      claim.value = "neutral material";
      claim.assessment.evidence = EvidenceClass::Observed;
      claim.assessment.origin = Origin::Archive;
      claim.assessment.confidence = 1;
      Support support;
      support.observation = std::string("ob_channel_") + suffix;
      support.extractor = "independent_channel_fixture";
      support.quote = std::string(suffix) == "a" ? "An orchid in the source." : "A recorded statement.";
      claim.assessment.support = {support};
      claims.push_back(std::move(claim));
    }
    LOOM_REQUIRE_OK(store->put_entities(run, entities));
    LOOM_REQUIRE_OK(store->put_claims(run, claims));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  ContextRequest request() const {
    ContextRequest request;
    request.text = "unrelatedquery";  // absent from every label/value/quote
    request.run = run;
    request.goal_type = "answer_question";
    request.relation_hops = 0;
    request.budget_tokens = 10000;
    request.candidate_channels = {CandidateChannelRequest{"custom", 50, 0}};
    return request;
  }
};

std::vector<std::string> selected_refs(const ContextSet& set) {
  std::vector<std::string> refs;
  for (const auto& item : set.items) refs.push_back(item.ref);
  return refs;
}

const Json& retrieval_trace(const ContextSet& set) {
  return set.goal.params.at("candidate_retrieval");
}

}  // namespace

TEST_SUITE("context_channel_integration") {
  TEST_CASE("channel scores above one remain measurements and nonlexical ranks three through five stay ordered") {
    ChannelFixture fixture;
    ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto channel = std::make_shared<InjectedCandidateChannel>();
    // Reverse identifier order catches accidental ties or a lexical relevance
    // floor below rank 2. Each subject is distinct so diversity adds no penalty.
    channel->hits = {{"cl_channel_a", 10}, {"cl_channel_b", 20}, {"cl_channel_c", 30},
                     {"cl_channel_d", 40}, {"cl_channel_e", 50}};
    engine.set_candidate_channel("custom", channel);
    const auto selected = unwrap(engine.select(fixture.request()));
    const std::vector<std::string> expected{"cl_channel_e", "cl_channel_d", "cl_channel_c", "cl_channel_b", "cl_channel_a"};
    CHECK(selected_refs(selected) == expected);
    REQUIRE(selected.items.size() == 5);
    for (std::size_t index = 0; index < selected.items.size(); ++index) {
      const auto& factors = selected.items[index].factors["candidate_channels"]["custom"];
      CHECK(factors["rank"] == index + 1);
      CHECK(factors["score"] == 50 - static_cast<int>(index) * 10);
      if (index > 0) CHECK(selected.items[index - 1].score > selected.items[index].score);
    }
    CHECK(channel->calls == 1);
    CHECK(channel->received_corpus_count == 5);
    CHECK(retrieval_trace(selected)["channels"][0]["invalid_hits"].empty());
  }

  TEST_CASE("engine validates bad instrument hits and independently applies exclusive score and result limits") {
    ChannelFixture fixture;
    ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto channel = std::make_shared<InjectedCandidateChannel>();
    channel->hits = {{"cl_channel_a", 2}, {"cl_channel_b", 2.5}, {"cl_channel_c", 3},
                     {"cl_channel_c", 3}, {"cl_not_in_corpus", 100},
                     {"cl_channel_d", std::numeric_limits<double>::quiet_NaN()}};
    engine.set_candidate_channel("custom", channel);
    auto request = fixture.request();
    request.candidate_channels = {CandidateChannelRequest{"custom", 1, 2}};
    const auto selected = unwrap(engine.select(request));
    CHECK(selected_refs(selected) == std::vector<std::string>{"cl_channel_c"});
    const auto& trace = retrieval_trace(selected)["channels"][0];
    REQUIRE(trace["accepted_hits"].size() == 1);
    CHECK(trace["accepted_hits"][0]["score"] == 3);
    std::set<std::string> reasons;
    for (const auto& invalid : trace["invalid_hits"]) reasons.insert(invalid["reason"].get<std::string>());
    CHECK(reasons.count("nonfinite_score") == 1);
    CHECK(reasons.count("invalid_or_duplicate_corpus_hit") == 1);
    CHECK(reasons.count("outside_requested_threshold_or_limit") == 1);
  }

  TEST_CASE("lexical instrument registered under another name does not erase its own shadow gap") {
    ChannelFixture fixture;
    ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    engine.set_candidate_channel("arbitrary_alias", make_lexical_candidate_channel(fixture.pack));
    auto request = fixture.request();
    request.text = "orchid";
    request.candidate_channels = {CandidateChannelRequest{"arbitrary_alias", 50, 0}};
    request.lexical_shadow = true;
    const auto selected = unwrap(engine.select(request));
    CHECK(selected_refs(selected) == std::vector<std::string>{"cl_channel_a"});
    const auto& trace = retrieval_trace(selected);
    CHECK(trace["channels"][0]["method"] == "lexical_token_overlap");
    const auto& gaps = trace["lexical_shadow"]["diagnostic_gap"];
    REQUIRE(gaps.size() == 1);
    CHECK(gaps[0]["ref"] == "cl_channel_a");
    CHECK(gaps[0]["verification"] == "undecided");
    CHECK(trace["lexical_shadow"]["selection_effect"] == "none");
  }

  TEST_CASE("missing capability and instrument exception remain separate statuses without fabricated scores") {
    ChannelFixture fixture;
    ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto channel = std::make_shared<InjectedCandidateChannel>();
    channel->throws = true;
    engine.set_candidate_channel("broken", channel);
    auto request = fixture.request();
    request.candidate_channels = {CandidateChannelRequest{"missing", 50, 0}, CandidateChannelRequest{"broken", 50, 0}};
    const auto selected = unwrap(engine.select(request));
    CHECK(selected.items.empty());
    const auto& traces = retrieval_trace(selected)["channels"];
    REQUIRE(traces.size() == 2);
    CHECK(traces[0]["status"] == "unavailable");
    CHECK(traces[1]["status"] == "error");
    for (const auto& trace : traces) {
      CHECK(trace["scores"].empty());
      CHECK(trace["accepted_hits"].empty());
    }
    CHECK(selected.to_json().dump().find("private instrument response") == std::string::npos);
    CHECK(channel->calls == 1);
  }

  TEST_CASE("candidate corpus cap stays explicit and excludes out-of-scan instrument references") {
    ChannelFixture fixture;
    ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto channel = std::make_shared<InjectedCandidateChannel>();
    channel->hits = {{"cl_channel_e", 100}, {"cl_channel_b", 20}, {"cl_channel_a", 10}};
    engine.set_candidate_channel("custom", channel);
    auto request = fixture.request();
    request.candidate_scan_limit = 2;
    const auto selected = unwrap(engine.select(request));
    CHECK(selected_refs(selected) == std::vector<std::string>{"cl_channel_b", "cl_channel_a"});
    const auto& trace = retrieval_trace(selected);
    CHECK(trace["scan_limit"] == 2);
    CHECK(trace["scanned_claims"] == 2);
    CHECK(trace["possibly_truncated"] == true);
    CHECK(channel->received_corpus_count == 2);
    REQUIRE(trace["channels"][0]["invalid_hits"].size() == 1);
    CHECK(trace["channels"][0]["invalid_hits"][0]["ref"] == "cl_channel_e");
  }
}
