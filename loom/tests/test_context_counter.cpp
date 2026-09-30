#include <doctest/doctest.h>

#include <algorithm>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {
struct CounterFixture {
  fsutil::TempDir dir;
  std::unique_ptr<Runtime> rt;
  kb::KnowledgeStore* store;
  std::shared_ptr<const kb::Pack> pack;
  std::string run;
  Claim statement, counter, premise;

  CounterFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    rt = unwrap(Runtime::open(options));
    store = &rt->knowledge().store();
    pack = unwrap(rt->knowledge().pack());
    run = unwrap(store->begin_run(pack->hash(), Json::object())).id;
    auto make = [](std::string id, std::string value) {
      Claim c;
      c.id = std::move(id);
      c.subject = "e_distant_" + c.id;
      c.predicate = "states";
      c.value = std::move(value);
      c.assessment.evidence = EvidenceClass::Observed;
      c.assessment.origin = Origin::Archive;
      c.assessment.confidence = 1;
      Support support;
      support.observation = "ob_" + c.id;
      support.quote = c.value.get<std::string>();
      support.extractor = "synthetic-dev@1";
      c.assessment.support.push_back(support);
      return c;
    };
    statement = make("cl_statement", "The component always preserves the original bytes.");
    counter = make("cl_counter", "An observed conversion removed the original bytes.");
    premise = make("cl_counter_premise", "The conversion was the component's own operation.");
    statement.assessment.counter.claims = {counter.id};
    counter.assessment.premises.claims = {premise.id};
    LOOM_REQUIRE_OK(store->put_claims(run, {statement, counter, premise}));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  context::ContextRequest req() const {
    context::ContextRequest r;
    r.run = run;
    r.text = "Evaluate the proposed claim.";
    r.goal_type = "answer_question";
    r.relation_hops = 0;
    r.claim_targets = {statement.id};
    r.include_counter_evidence = true;
    r.budget_tokens = 10000;
    return r;
  }
};

const ContextItem* item(const ContextSet& set, const std::string& id) {
  auto found = std::find_if(set.items.begin(), set.items.end(), [&](const auto& i) { return i.ref == id; });
  return found == set.items.end() ? nullptr : &*found;
}
}

TEST_SUITE("context_counter") {
  TEST_CASE("explicit anchors and linked counter claims ignore geometric radius and retain premises") {
    CounterFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto r = f.req();
    auto set = unwrap(engine.select(r));
    REQUIRE(item(set, f.statement.id));
    REQUIRE(item(set, f.counter.id));
    REQUIRE(item(set, f.premise.id));
    CHECK(item(set, f.counter.id)->factors["counter_for"] == Json::array({f.statement.id}));
    CHECK(item(set, f.premise.id)->required_by == std::vector<std::string>{f.counter.id});
    CHECK(set.goal.params["claim_selection"][0]["status"] == "selected");
    CHECK(set.goal.params["counter_evidence"]["entries"][0]["claims"][0]["status"] == "selected");
    r.include_counter_evidence = false;
    auto without = unwrap(engine.select(r));
    CHECK(item(without, f.statement.id));
    CHECK(!item(without, f.counter.id));
  }

  TEST_CASE("missing counter link and missing anchor remain visible in trace and rendered context") {
    CounterFixture f;
    f.statement.assessment.counter.claims = {"cl_missing_counter"};
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.statement}));
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto r = f.req();
    r.claim_targets.push_back("cl_missing_anchor");
    auto set = unwrap(engine.select(r));
    CHECK(set.goal.params["counter_evidence"]["entries"][0]["claims"][0]["status"] == "missing");
    CHECK(set.goal.params["claim_selection"][1]["status"] == "missing");
    auto text = unwrap(engine.render(set));
    CHECK(text.find("INCOMPLETE") != std::string::npos);
    CHECK(text.find("cl_missing_counter") != std::string::npos);
    CHECK(text.find("cl_missing_anchor") != std::string::npos);
  }

  TEST_CASE("counter omission by item budget is not confused with absence") {
    CounterFixture f;
    f.counter.assessment.support[0].quote = std::string(50000, 'x');
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.counter}));
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto r = f.req();
    r.budget_tokens = 500;
    r.detail_resolution = Resolution::Raw;
    auto set = unwrap(engine.select(r));
    REQUIRE(item(set, f.statement.id));
    CHECK(!item(set, f.counter.id));
    CHECK(set.goal.params["counter_evidence"]["entries"][0]["claims"][0]["status"] == "over_budget");
    CHECK(unwrap(engine.render(set)).find("INCOMPLETE") != std::string::npos);
    CHECK(set.used_tokens <= r.budget_tokens);
  }

  TEST_CASE("request round trip rejects invalid anchor and counter option types") {
    auto r = unwrap(context::ContextRequest::from_json(Json{{"claim_targets", {"cl_one"}}, {"include_counter_evidence", true}}));
    CHECK(r.claim_targets == std::vector<std::string>{"cl_one"});
    CHECK(r.include_counter_evidence);
    CHECK(unwrap(context::ContextRequest::from_json(r.to_json())).to_json() == r.to_json());
    for (auto invalid : {Json(1), Json("true"), Json(nullptr), Json::array()}) {
      CHECK(!context::ContextRequest::from_json(Json{{"include_counter_evidence", invalid}}));
    }
    for (auto invalid : {Json(1), Json("cl_one"), Json::array({1}), Json::array({""})}) {
      CHECK(!context::ContextRequest::from_json(Json{{"claim_targets", invalid}}));
    }
  }
}
