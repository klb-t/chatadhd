// N2 synthetic mechanism controls: exact output parity against the existing
// uncached vector adapter. No answer-quality or timing assertion.
#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <future>
#include <iostream>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::context;
using loom::test::unwrap;

namespace {

std::shared_ptr<CandidateChannel> uncached(const std::shared_ptr<const kb::Pack>& pack) {
  return make_vector_candidate_channel(std::shared_ptr<resolve::VectorSpace>(resolve::make_tfidf_space(pack)));
}

CandidateChannelRequest spec(int limit = 50, double minimum = 0) { return {"tfidf", limit, minimum}; }

const std::vector<RetrievalDocument> documents{
    {"query", "encryption protects the original record"}, {"query:", "orchid marker records a checkpoint"},
    {"unrepresentable", "..."}, {"unrelated", "sailing boat"}};

class StatefulSpace final : public resolve::VectorSpace {
 public:
  int fit_calls = 0, vector_calls = 0;
  bool fail_fit = false;
  std::string method() const override { return "synthetic-stateful-space"; }
  std::vector<std::string> modalities() const override { return {"text"}; }
  Status fit(const std::vector<resolve::EmbedInput>&) override {
    ++fit_calls;
    if (fail_fit) return Error(Errc::Unavailable, "synthetic fit failure");
    return {};
  }
  Result<std::vector<resolve::SparseVec>> vectors(const std::vector<resolve::EmbedInput>& input) override {
    ++vector_calls;
    std::vector<resolve::SparseVec> out;
    for (std::size_t i = 0; i < input.size(); ++i) {
      out.push_back({{fit_calls % 2 == 0 && i == 0 ? "second" : "first", 1}});
    }
    return out;
  }
};

class Mutator final : public CandidateChannel {
 public:
  std::function<void()> mutation;
  int calls = 0;
  RetrievalBatch retrieve(std::string_view, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest&) override {
    if (++calls == 1) mutation();
    RetrievalBatch out;
    out.method = "synthetic_mutator";
    out.corpus_count = corpus.size();
    return out;
  }
};

struct Fixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  kb::KnowledgeStore* store = nullptr;
  std::string run;
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;

  Fixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
    store = &runtime->knowledge().store();
    pack = unwrap(runtime->knowledge().pack());
    run = unwrap(store->begin_run(pack->hash(), Json{{"fixture", "n2.tfidf.reuse"}})).id;
    for (int i = 0; i < 3; ++i) {
      model::Entity e;
      e.id = "e.n2." + std::to_string(i);
      e.kind = "component";
      e.canonical_key = e.id;
      e.label = "neutral component " + std::to_string(i);
      entities.push_back(e);
      model::Claim c;
      c.id = "cl.n2." + std::to_string(i);
      c.subject = e.id;
      c.predicate = "records";
      c.value = "original record";
      c.qualifiers.valid_from = "2026-09-30";
      c.assessment.evidence = model::EvidenceClass::Observed;
      c.assessment.origin = model::Origin::Archive;
      c.assessment.confidence = 0.9;
      model::Support support;
      support.observation = "ob.n2." + std::to_string(i);
      support.extractor = "n2.synthetic@1";
      support.quote = i == 0 ? "Orchid preserves the original bytes." : i == 1 ? "Encryption of the record." : "A sailing boat.";
      c.assessment.support = {support};
      claims.push_back(c);
    }
    LOOM_REQUIRE_OK(store->put_entities(run, entities));
    LOOM_REQUIRE_OK(store->put_claims(run, claims));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  ContextRequest request() const {
    ContextRequest req;
    req.run = run;
    req.text = "orchid";
    req.goal_type = "answer_question";
    req.relation_hops = 0;
    req.detail_resolution = model::Resolution::Raw;
    req.budget_tokens = 100000;
    req.candidate_channels = {spec(), {"not_installed", 50, 0}};
    req.lexical_shadow = true;
    return req;
  }

  Json check(ContextEngine& engine, const ContextRequest& req) {
    const auto actual = unwrap(engine.build(req));
    ContextEngine reference(*runtime, *store, pack);
    reference.set_candidate_channel("tfidf", uncached(pack));
    const auto expected = unwrap(reference.build(req));
    CHECK(actual == expected);
    return actual;
  }

  void corrupt(const std::string& table, const std::string& id) {
    auto lock = runtime->db().lock();
    auto statement = unwrap(runtime->db().conn().prepare("UPDATE " + table + " SET body = ? WHERE run_id = ? AND id = ?"));
    statement.bind_all("{malformed synthetic body", run, id);
    LOOM_REQUIRE_OK(statement.run());
  }
};

const Json& retrieval(const Json& build) { return build.at("context_set").at("goal").at("params").at("candidate_retrieval"); }

}  // namespace

TEST_SUITE("context_tfidf_reuse") {
  TEST_CASE("warm query thresholds limits and unrepresentable states exactly match uncached vectors") {
    const auto pack = unwrap(kb::Pack::load_builtin());
    auto cached = make_tfidf_candidate_channel(pack);
    auto reference = uncached(pack);
    for (const auto* query : {"szyfrowanie", "orchid", "unknownword", "...", "", "checkpoint", "szyfrowanie"}) {
      for (const auto& request : {spec(), spec(1), spec(2, 0.5), spec(50, -1), spec(50, 1)}) {
        INFO(query);
        const auto expected = reference->retrieve(query, documents, request).to_json();
        CHECK(cached->retrieve(query, documents, request).to_json() == expected);
        CHECK(cached->retrieve(query, documents, request).to_json() == expected);
      }
    }
    CHECK(make_tfidf_candidate_channel(nullptr)->retrieve("query", documents, spec()).to_json() ==
          make_vector_candidate_channel(nullptr)->retrieve("query", documents, spec()).to_json());
  }

  TEST_CASE("ordered exact corpus key notices text id order insertion deletion and return to an old corpus") {
    const auto pack = unwrap(kb::Pack::load_builtin());
    auto cached = make_tfidf_candidate_channel(pack);
    auto reference = uncached(pack);
    std::vector<std::vector<RetrievalDocument>> variants{documents};
    auto changed = documents;
    changed[0].text = "orchid";
    variants.push_back(changed);
    changed[0].ref = "renamed";
    variants.push_back(changed);
    std::reverse(changed.begin(), changed.end());
    variants.push_back(changed);
    changed.push_back({"new", "orchid encryption"});
    variants.push_back(changed);
    changed.erase(changed.begin());
    variants.push_back(changed);
    variants.push_back({});
    variants.push_back(documents);
    for (const auto& corpus : variants) {
      for (const auto* query : {"orchid", "szyfrowanie"}) {
        const auto expected = reference->retrieve(query, corpus, spec()).to_json();
        CHECK(cached->retrieve(query, corpus, spec()).to_json() == expected);
        CHECK(cached->retrieve(query, corpus, spec()).to_json() == expected);
      }
    }
    CHECK(cached->retrieve("orchid", {{"same", "one"}, {"same", "two"}}, spec()).status == "error");
    CHECK(cached->retrieve("orchid", documents, spec()).to_json() == reference->retrieve("orchid", documents, spec()).to_json());
  }

  TEST_CASE("injected vector spaces keep every fit call state transition and failure") {
    auto space = std::make_shared<StatefulSpace>();
    auto channel = make_vector_candidate_channel(space);
    const std::vector<RetrievalDocument> corpus{{"one", "same bytes"}};
    const auto first = channel->retrieve("query", corpus, spec());
    const auto second = channel->retrieve("query", corpus, spec());
    REQUIRE(first.scores.size() == 1);
    REQUIRE(second.scores.size() == 1);
    CHECK(first.scores[0].score == 1);
    CHECK(second.scores[0].score == 0);
    CHECK(space->fit_calls == 2);
    CHECK(space->vector_calls == 2);
    space->fail_fit = true;
    CHECK(channel->retrieve("query", corpus, spec()).status == "error");
    CHECK(space->fit_calls == 3);
    CHECK(space->vector_calls == 2);
    space->fail_fit = false;
    CHECK(channel->retrieve("query", corpus, spec()).to_json() == second.to_json());
    CHECK(space->fit_calls == 4);
  }

  TEST_CASE("fresh store labels support metadata corpus caps and runs remain visible after warming") {
    Fixture f;
    ContextEngine engine(*f.runtime, *f.store, f.pack);
    auto req = f.request();
    const auto original = f.check(engine, req);
    f.entities[1].label = "orchid label now changed";
    LOOM_REQUIRE_OK(f.store->put_entities(f.run, {f.entities[1]}));
    CHECK(f.check(engine, req) != original);
    f.claims[0].assessment.support[0].quote = "A completely different source quotation.";
    f.claims[1].assessment.confidence = 0.4;
    f.claims[1].assessment.counter.claims = {f.claims[2].id};
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.claims[0], f.claims[1]}));
    req.include_counter_evidence = true;
    const auto changed = f.check(engine, req);
    CHECK(changed != original);
    req.candidate_scan_limit = 1;
    const auto capped = f.check(engine, req);
    CHECK(retrieval(capped)["possibly_truncated"] == true);
    req.candidate_scan_limit = 10000;
    const auto other_run = unwrap(f.store->begin_run(f.pack->hash(), Json{{"fixture", "n2.other"}})).id;
    LOOM_REQUIRE_OK(f.store->put_entities(other_run, f.entities));
    auto other = f.claims[0];
    other.assessment.support[0].quote = "Orchid returned in a different knowledge run.";
    LOOM_REQUIRE_OK(f.store->put_claims(other_run, {other}));
    LOOM_REQUIRE_OK(f.store->finish_run(other_run, "done", Json::object()));
    req.run = other_run;
    f.check(engine, req);
    req.run = f.run;
    CHECK(f.check(engine, req) == changed);
    LOOM_REQUIRE_OK(f.store->clear_run(f.run));
    const auto empty = f.check(engine, req);
    CHECK(retrieval(empty)["admitted_claims"] == 0);
  }

  TEST_CASE("external connection updates and corpus or label errors cannot become stale successful results") {
    Fixture f;
    ContextEngine engine(*f.runtime, *f.store, f.pack);
    const auto req = f.request();
    const auto original = f.check(engine, req);
    auto external = unwrap(sql::Connection::open(f.directory.path() / "chatadhd.db"));
    auto changed = f.claims[0];
    changed.assessment.support[0].quote = "The orchid has left the original record, replaced by encryption.";
    LOOM_REQUIRE_OK(external.run("UPDATE loom_kb_claims SET body = ? WHERE run_id = ? AND id = ?",
        json::dump(changed.to_json()), f.run, changed.id));
    CHECK(f.check(engine, req) != original);
    f.corrupt("loom_kb_entities", f.entities[0].id);
    const auto label_error = f.check(engine, req);
    CHECK(retrieval(label_error)["entity_label_query_errors"] == 1);
    LOOM_REQUIRE_OK(f.store->put_entities(f.run, {f.entities[0]}));
    f.corrupt("loom_kb_claims", f.claims[0].id);
    const auto corpus_error = f.check(engine, req);
    CHECK(retrieval(corpus_error)["corpus_status"] == "query_error");
    CHECK(retrieval(corpus_error)["channels"][0]["status"] == "error");
    CHECK(retrieval(corpus_error)["channels"][0]["scores"].empty());
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, f.claims));
    CHECK(f.check(engine, req) == original);
  }

  TEST_CASE("explicit instrument replacement after warming overrides the builtin and its cache") {
    Fixture f;
    ContextEngine engine(*f.runtime, *f.store, f.pack);
    const auto req = f.request();
    const auto original = f.check(engine, req);
    engine.set_candidate_channel("tfidf", nullptr);
    const auto disabled = unwrap(engine.build(req));
    CHECK(retrieval(disabled)["channels"][0]["status"] == "unavailable");
    CHECK(retrieval(disabled)["channels"][0]["scores"].empty());
    auto space = std::make_shared<StatefulSpace>();
    engine.set_candidate_channel("tfidf", make_vector_candidate_channel(space));
    unwrap(engine.build(req));
    unwrap(engine.build(req));
    CHECK(space->fit_calls == 2);
    CHECK(space->vector_calls == 2);
    engine.set_candidate_channel("tfidf", make_tfidf_candidate_channel(f.pack));
    CHECK(f.check(engine, req) == original);
  }

  TEST_CASE("mutations between plan theses retain fresh sources and query failure diagnostics") {
    Fixture f;
    auto req = f.request();
    req.candidate_channels.push_back({"mutator", 50, 0});
    req.plan = Json{{"id", "explicit-mutation-control"}, {"source_ref", {{"kind", "synthetic-declaration"}}},
        {"theses", Json::array({Json{{"id", "before"}, {"text", "orchid"}, {"require_counter_evidence", false}},
                               Json{{"id", "after"}, {"text", "newmarker"}, {"require_counter_evidence", false}}})}};
    for (bool corrupt : {false, true}) {
      const auto build = [&](bool reuse) {
        LOOM_REQUIRE_OK(f.store->put_claims(f.run, f.claims));
        ContextEngine engine(*f.runtime, *f.store, f.pack);
        if (!reuse) engine.set_candidate_channel("tfidf", uncached(f.pack));
        auto mutator = std::make_shared<Mutator>();
        mutator->mutation = [&] {
          if (corrupt) f.corrupt("loom_kb_claims", f.claims[0].id);
          else {
            auto changed = f.claims[0];
            changed.assessment.support[0].quote = "newmarker retains new source bytes";
            LOOM_REQUIRE_OK(f.store->put_claims(f.run, {changed}));
          }
        };
        engine.set_candidate_channel("mutator", mutator);
        auto result = unwrap(engine.build(req));
        CHECK(mutator->calls == (corrupt ? 1 : 2));
        return result;
      };
      const auto actual = build(true);
      CHECK(actual == build(false));
      const auto& theses = actual["context_set"]["goal"]["params"]["plan_trace"]["theses"];
      CHECK(theses[1]["selection_params"]["candidate_retrieval"]["corpus_status"] == (corrupt ? "query_error" : "ok"));
      if (!corrupt) CHECK(actual["prompt"].get<std::string>().find("newmarker retains new source bytes") != std::string::npos);
    }
  }

  TEST_CASE("one actual shared builtin serializes concurrent variable corpus fits and scoring") {
    const auto pack = unwrap(kb::Pack::load_builtin());
    auto shared = make_tfidf_candidate_channel(pack);
    std::vector<std::vector<RetrievalDocument>> corpora{documents, {{"different", "orchid"}}, {{"one", "encryption"}, {"two", "orchid"}}};
    std::vector<Json> expected;
    for (const auto& corpus : corpora) expected.push_back(uncached(pack)->retrieve("orchid", corpus, spec()).to_json());
    std::vector<std::future<std::vector<Json>>> workers;
    for (std::size_t worker = 0; worker < 4; ++worker) {
      workers.push_back(std::async(std::launch::async, [&, worker] {
        std::vector<Json> results;
        for (std::size_t i = 0; i < 6; ++i) results.push_back(shared->retrieve("orchid", corpora[(i + worker) % corpora.size()], spec()).to_json());
        return results;
      }));
    }
    for (std::size_t worker = 0; worker < workers.size(); ++worker) {
      const auto results = workers[worker].get();
      for (std::size_t i = 0; i < results.size(); ++i) CHECK(results[i] == expected[(i + worker) % corpora.size()]);
    }
  }

  TEST_CASE("logical retained payload counts are reported separately from allocator and process RSS") {
    const auto pack = unwrap(kb::Pack::load_builtin());
    Json sizes = Json::array();
    for (int size : {64, 256, 1024}) {
      std::vector<resolve::EmbedInput> inputs;
      std::size_t id_bytes = 0, text_bytes = 0, vector_entries = 0, vector_key_bytes = 0;
      for (int i = 0; i < size; ++i) {
        auto padded = [](int number, int width) {
          const auto text = std::to_string(number);
          return std::string(static_cast<std::size_t>(width) - text.size(), '0') + text;
        };
        const auto id = "cl.n2." + padded(i, 5);
        const auto topic = "topic" + padded(i % 32, 2);
        const auto text = "component " + std::to_string(i) + " records " + topic + "\n" + topic +
            " preserves the original record and its exact source provenance. "
            "A durable checkpoint permits inspection after interruption. "
            "The component marker is number " + std::to_string(i) + "; retain all evidence bytes.";
        id_bytes += id.size();
        text_bytes += text.size();
        inputs.push_back({id, "text", text, "", ""});
      }
      auto space = resolve::make_tfidf_space(pack);
      LOOM_REQUIRE_OK(space->fit(inputs));
      const auto vectors = unwrap(space->vectors(inputs));
      for (const auto& vector : vectors) for (const auto& [key, value] : vector) {
        (void)value;
        ++vector_entries;
        vector_key_bytes += key.size();
      }
      sizes.push_back(Json{{"claims", size}, {"copied_document_id_bytes", id_bytes}, {"copied_document_text_bytes", text_bytes},
          {"normalized_vector_entries", vector_entries}, {"normalized_vector_key_bytes", vector_key_bytes},
          {"vector_numeric_bytes", vector_entries * sizeof(double)}, {"state_bytes", static_cast<std::size_t>(size) * sizeof(int)}});
      CHECK(vectors.size() == static_cast<std::size_t>(size));
      CHECK(vector_entries > 0);
    }
    std::cout << "N2_RETAINED_PAYLOAD " << json::dump(Json{{"schema", "loom.n2.logical_cache_payload/1"}, {"sizes", sizes},
        {"excludes", "container nodes, string capacity, allocator overhead, query id, temporary cold-fit copies and result JSON"}}) << '\n';
  }
}
