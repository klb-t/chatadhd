// R28 structural selection tests; synthetic fixtures, no model-quality claim.
#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <set>

#include "loom/context_plan.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

Entity plan_entity(const std::string& label) {
  Entity e;
  e.kind = "component";
  e.canonical_key = label;
  e.label = label;
  e.id = Entity::make_id(e.kind, label);
  e.origin = Origin::Archive;
  e.confidence = 1;
  return e;
}

Claim plan_claim(const Entity& a, const Entity& b, const std::string& quote) {
  Claim c;
  c.subject = a.id;
  c.object = b.id;
  c.predicate = "depends_on";
  c.qualifiers.valid_from = "2026-09-30";
  c.assessment.evidence = EvidenceClass::Observed;
  c.assessment.origin = Origin::Archive;
  c.assessment.confidence = 0.9;
  Support s;
  s.observation = "ob_plan_fixture";
  s.extractor = "plan_fixture";
  s.quote = quote;
  c.assessment.support = {s};
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

struct PlanFixture {
  fsutil::TempDir dir;
  std::unique_ptr<Runtime> rt;
  kb::KnowledgeStore* store;
  std::shared_ptr<const kb::Pack> pack;
  std::string run;
  Entity a = plan_entity("plan alpha"), b = plan_entity("plan beta"), c = plan_entity("plan gamma");
  Entity x = plan_entity("remote counter"), y = plan_entity("remote endpoint"), z = plan_entity("remote premise");
  Claim ab = plan_claim(a, b, "RAW-ALPHA " + std::string(300, 'a') + " END-ALPHA");
  Claim bc = plan_claim(b, c, "Second relation.");
  Claim counter = plan_claim(x, y, "COUNTER-BYTES");
  Claim premise = plan_claim(y, z, "PREMISE-BYTES " + std::string(7000, 'p'));

  PlanFixture() {
    RuntimeOptions opts;
    opts.data_dir = dir.path().string();
    opts.start_workers = false;
    rt = unwrap(Runtime::open(opts));
    store = &rt->knowledge().store();
    pack = unwrap(rt->knowledge().pack());
    run = unwrap(store->begin_run(pack->hash(), Json{{"fixture", "context_plan"}})).id;
    ab.assessment.counter.claims = {counter.id};
    ab.assessment.premises.claims = {premise.id};
    LOOM_REQUIRE_OK(store->put_entities(run, {a, b, c, x, y, z}));
    LOOM_REQUIRE_OK(store->put_claims(run, {ab, bc, counter, premise}));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  context::ContextRequest request(const Json& theses) const {
    context::ContextRequest req;
    req.run = run;
    req.text = "Explain the component relationships";
    req.goal_type = "answer_question";
    req.budget_tokens = 100000;
    req.targets = {a.id};
    req.plan = Json{{"id", "product-plan"}, {"source_ref", Json{{"kind", "product"}, {"id", "caller-active-spec"}}}, {"theses", theses}};
    return req;
  }
};

std::vector<const ContextItem*> matching(const ContextSet& set, const std::string& ref) {
  std::vector<const ContextItem*> result;
  for (const auto& item : set.items) if (item.ref == ref) result.push_back(&item);
  return result;
}

const Json& thesis_trace(const ContextSet& set, std::size_t i = 0) {
  return set.goal.params.at("plan_trace").at("theses").at(i);
}

bool has_gap(const Json& trace, const std::string& reason) {
  return std::any_of(trace["gaps"].begin(), trace["gaps"].end(), [&](const auto& value) { return value.value("reason", "") == reason; });
}

Json thesis(const std::string& id) {
  return Json{{"id", id}, {"text", "Explain this relationship"}};
}

}  // namespace

TEST_SUITE("context_plan") {
  TEST_CASE("strict plan schema rejects silent typos and ambiguous thesis identity") {
    Json valid{{"id", "plan"}, {"theses", Json::array({thesis("one")})}};
    LOOM_REQUIRE_OK(context::validate_context_plan(valid));
    LOOM_REQUIRE_OK(context::validate_context_plan(nullptr));
    for (const auto& wrong : {Json::array(), Json(true), Json("plan"), Json::object()}) {
      CHECK(!context::validate_context_plan(wrong));
    }
    for (const auto& field : {"scope", "budget_tokens", "active_task_spec"}) {
      auto bad = valid;
      bad["theses"][0][field] = 1;
      CHECK(!context::validate_context_plan(bad));
    }
    auto bad = valid;
    bad["theses"].push_back(thesis("one"));
    CHECK(!context::validate_context_plan(bad));
    bad = valid;
    bad["theses"] = Json::array();
    CHECK(!context::validate_context_plan(bad));
    for (const auto& field : {"id", "text"}) {
      bad = valid;
      bad["theses"][0][field] = " \n";
      CHECK(!context::validate_context_plan(bad));
    }
    for (const auto& field : {"targets", "claims"}) {
      for (const auto& wrong : {Json("one"), Json::array({"one", "one"}), Json::array({1}), Json::array({""})}) {
        bad = valid;
        bad["theses"][0][field] = wrong;
        CHECK(!context::validate_context_plan(bad));
      }
    }
  }

  TEST_CASE("schema validates independent controls weights and counter policy") {
    const Json valid{{"id", "plan"}, {"theses", Json::array({thesis("one")})}};
    const std::vector<std::pair<std::string, Json>> invalid{
      {"relation_hops", -1}, {"relation_hops", true}, {"relation_hops", 1.5},
      {"relation_hops", std::numeric_limits<std::uint64_t>::max()},
      {"detail_resolution", "medium"}, {"detail_resolution", 1},
      {"require_counter_evidence", 1}, {"require_counter_evidence", nullptr},
      {"budget_weight", 0}, {"budget_weight", -1}, {"budget_weight", "2"},
      {"budget_weight", true}, {"budget_weight", std::numeric_limits<double>::infinity()},
      {"budget_weight", std::numeric_limits<double>::quiet_NaN()}};
    for (const auto& [key, value] : invalid) {
      auto bad = valid;
      bad["theses"][0][key] = value;
      CHECK(!context::validate_context_plan(bad));
    }
    for (const auto& resolution : {"label", "summary", "full", "raw"}) {
      auto good = valid;
      good["theses"][0]["detail_resolution"] = resolution;
      good["theses"][0]["relation_hops"] = 0;
      good["theses"][0]["require_counter_evidence"] = false;
      good["theses"][0]["budget_weight"] = 1e300;
      LOOM_REQUIRE_OK(context::validate_context_plan(good));
    }
  }

  TEST_CASE("plan and opaque source provenance round trip through actual request parsing") {
    PlanFixture f;
    auto req = f.request(Json::array({thesis("one")}));
    const auto parsed = unwrap(context::ContextRequest::from_json(req.to_json()));
    CHECK(parsed.plan == req.plan);
    auto invalid = req.to_json();
    invalid["plan"]["theses"][0]["unknown"] = 1;
    CHECK(!context::ContextRequest::from_json(invalid));
    CHECK(!context::ContextRequest{}.to_json().contains("plan"));
  }

  TEST_CASE("per thesis radius and detail retain separate representations") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto narrow = thesis("narrow");
    narrow["relation_hops"] = 1;
    narrow["detail_resolution"] = "label";
    auto broad = thesis("broad");
    broad["relation_hops"] = 2;
    broad["detail_resolution"] = "raw";
    const auto selected = unwrap(engine.select(f.request(Json::array({narrow, broad}))));
    const auto alpha = matching(selected, f.ab.id);
    REQUIRE(alpha.size() == 2);
    CHECK(alpha[0]->resolution != alpha[1]->resolution);
    for (const auto* item : alpha) {
      REQUIRE(item->factors["thesis_ids"].size() == 1);
      if (item->resolution == Resolution::Raw) {
        CHECK(item->factors["thesis_ids"][0] == "broad");
        CHECK(item->text.find("END-ALPHA") != std::string::npos);
      } else CHECK(item->text.find("RAW-ALPHA") == std::string::npos);
    }
    const auto second = matching(selected, f.bc.id);
    REQUIRE(second.size() == 1);
    CHECK(second[0]->factors["thesis_ids"] == Json::array({"broad"}));
    CHECK(thesis_trace(selected, 0)["relation_hops"] == 1);
    CHECK(thesis_trace(selected, 1)["detail_resolution"] == "raw");
    LOOM_REQUIRE_OK(selected.validate());
  }

  TEST_CASE("identical representations merge membership without losing per thesis diagnostics") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto req = f.request(Json::array({thesis("one"), thesis("two")}));
    req.detail_resolution = Resolution::Label;
    const auto selected = unwrap(engine.select(req));
    const auto alpha = matching(selected, f.ab.id);
    REQUIRE(alpha.size() == 1);
    CHECK(alpha[0]->factors["thesis_ids"] == Json::array({"one", "two"}));
    CHECK(alpha[0]->factors["thesis_selection"].size() == 2);
    CHECK(thesis_trace(selected, 1)["added_tokens_after_deduplication"] == 0);
    CHECK(thesis_trace(selected, 1)["selected_tokens_before_deduplication"].get<int>() > 0);
    CHECK(selected.used_tokens <= selected.budget_tokens);
  }

  TEST_CASE("explicit claims and counter links are independent of zero exploratory radius") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto t = thesis("explicit");
    t["claims"] = Json::array({f.ab.id});
    t["relation_hops"] = 0;
    auto req = f.request(Json::array({t}));
    const auto selected = unwrap(engine.select(req));
    CHECK(matching(selected, f.ab.id).size() == 1);
    CHECK(matching(selected, f.counter.id).size() == 1);
    CHECK(matching(selected, f.premise.id).size() == 1);
    CHECK(matching(selected, f.bc.id).empty());
    CHECK(thesis_trace(selected)["direct_material_candidates"] == 1);
    CHECK(thesis_trace(selected).contains("counter_evidence"));
    req.plan["theses"][0]["require_counter_evidence"] = false;
    const auto disabled = unwrap(engine.select(req));
    CHECK(matching(disabled, f.counter.id).empty());
    CHECK(matching(disabled, f.ab.id).size() == 1);
  }

  TEST_CASE("missing explicit support and counter links stay visible as gaps") {
    PlanFixture f;
    f.ab.assessment.counter.claims.push_back("cl_missing_counter");
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.ab}));
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto t = thesis("missing");
    t["relation_hops"] = 0;
    t["claims"] = Json::array({"cl_not_present"});
    auto req = f.request(Json::array({t}));
    const auto absent = unwrap(engine.select(req));
    CHECK(has_gap(thesis_trace(absent), "no_direct_support_candidates"));
    CHECK(has_gap(thesis_trace(absent), "explicit_claim_missing"));
    req.plan["theses"][0]["claims"] = Json::array({f.ab.id});
    const auto counters = unwrap(engine.select(req));
    CHECK(thesis_trace(counters)["counter_evidence"].dump().find("cl_missing_counter") != std::string::npos);
    CHECK(has_gap(thesis_trace(counters), "counter_evidence_not_selected"));
  }

  TEST_CASE("zero allocation never expands to the legacy 4000 token default") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto req = f.request(Json::array({thesis("zero"), thesis("last")}));
    req.budget_tokens = 1;
    const auto selected = unwrap(engine.select(req));
    CHECK(thesis_trace(selected, 0)["allocated_budget_tokens"] == 0);
    CHECK(thesis_trace(selected, 0)["selection_status"] == "not_evaluated");
    CHECK(thesis_trace(selected, 0)["selected_refs"].empty());
    CHECK(has_gap(thesis_trace(selected, 0), "zero_budget_allocation"));
    CHECK(thesis_trace(selected, 1)["allocated_budget_tokens"] == 1);
    CHECK(selected.used_tokens <= 1);
    CHECK(selected.budget_tokens == 1);
  }

  TEST_CASE("explicit claim gaps distinguish evidence filtering from budget exclusion") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto t = thesis("explicit");
    t["claims"] = Json::array({f.ab.id});
    t["relation_hops"] = 0;
    t["require_counter_evidence"] = false;
    auto req = f.request(Json::array({t}));
    f.ab.assessment.evidence = EvidenceClass::Extrapolated;
    Derivation derivation;
    derivation.op = "fixture_hypothesis";
    f.ab.assessment.derivation = derivation;
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.ab}));
    const auto filtered = unwrap(engine.select(req));
    CHECK(matching(filtered, f.ab.id).empty());
    CHECK(has_gap(thesis_trace(filtered), "explicit_claim_filtered"));
    f.ab.assessment.evidence = EvidenceClass::Observed;
    f.ab.assessment.derivation.reset();
    f.ab.assessment.support[0].quote = std::string(50000, 'q');
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.ab}));
    req.detail_resolution = Resolution::Raw;
    req.budget_tokens = 1000;
    const auto budgeted = unwrap(engine.select(req));
    CHECK(matching(budgeted, f.ab.id).empty());
    CHECK(has_gap(thesis_trace(budgeted), "explicit_claim_over_budget"));
    CHECK(budgeted.used_tokens <= req.budget_tokens);
  }

  TEST_CASE("weights and forward unused capacity have explicit exact accounting") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto first = thesis("empty");
    first["targets"] = Json::array();
    first["relation_hops"] = 0;
    first["budget_weight"] = 1;
    auto second = thesis("actual");
    second["budget_weight"] = 3;
    auto req = f.request(Json::array({first, second}));
    req.budget_tokens = 10000;
    const auto selected = unwrap(engine.select(req));
    CHECK(thesis_trace(selected, 0)["base_budget_tokens"] == 2500);
    CHECK(thesis_trace(selected, 0)["added_tokens_after_deduplication"] == 0);
    CHECK(thesis_trace(selected, 1)["base_budget_tokens"] == 7500);
    CHECK(thesis_trace(selected, 1)["carried_budget_tokens"] == 2500);
    CHECK(thesis_trace(selected, 1)["allocated_budget_tokens"] == 10000);
    CHECK(selected.used_tokens <= 10000);
  }

  TEST_CASE("dependency omission remains incomplete in the owning thesis") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto t = thesis("incomplete");
    t["detail_resolution"] = "raw";
    auto req = f.request(Json::array({t}));
    req.budget_tokens = 1000;
    const auto selected = unwrap(engine.select(req));
    const auto alpha = matching(selected, f.ab.id);
    REQUIRE(alpha.size() == 1);
    CHECK(alpha[0]->missing_premises == std::vector<std::string>{f.premise.id});
    CHECK(has_gap(thesis_trace(selected), "missing_premise"));
    CHECK(unwrap(engine.render(selected)).find("INCOMPLETE") != std::string::npos);
  }

  TEST_CASE("inherited controls and an explicit empty claims list remain distinct") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto inherit = thesis("inherit");
    inherit["detail_resolution"] = nullptr;
    auto clear = thesis("clear");
    clear["claims"] = Json::array();
    auto req = f.request(Json::array({inherit, clear}));
    req.relation_hops = 0;
    req.detail_resolution = Resolution::Label;
    req.claim_targets = {f.ab.id};
    const auto selected = unwrap(engine.select(req));
    CHECK(thesis_trace(selected, 0)["claims"] == Json::array({f.ab.id}));
    CHECK(thesis_trace(selected, 0)["relation_hops"] == 0);
    CHECK(thesis_trace(selected, 0)["detail_resolution"] == "label");
    CHECK(thesis_trace(selected, 1)["selected_refs"].empty());
    CHECK(has_gap(thesis_trace(selected, 1), "no_direct_support_candidates"));
  }

  TEST_CASE("an underlying claim selected as a decision is counted as explicit material") {
    PlanFixture f;
    Decision decision;
    decision.id = f.ab.id;
    decision.subject = f.a.id;
    decision.alternatives = {DecisionAlternative{"selected alternative", "", "selected value", {}, true}};
    LOOM_REQUIRE_OK(f.store->put_decisions(f.run, {decision}));
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto t = thesis("decision");
    t["claims"] = Json::array({f.ab.id});
    const auto selected = unwrap(engine.select(f.request(Json::array({t}))));
    REQUIRE(matching(selected, f.ab.id).size() == 1);
    CHECK(matching(selected, f.ab.id)[0]->ref_kind == RefKind::Decision);
    CHECK(thesis_trace(selected)["direct_material_candidates"] == 1);
    CHECK(!has_gap(thesis_trace(selected), "explicit_claim_not_selected"));
    CHECK(!has_gap(thesis_trace(selected), "no_direct_support_candidates"));
  }

  TEST_CASE("candidate capability failures and corpus limits are rendered as thesis gaps") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto req = f.request(Json::array({thesis("diagnostics")}));
    req.candidate_channels = {{"uninstalled_instrument", 10, 0}};
    req.candidate_scan_limit = 1;
    const auto selected = unwrap(engine.select(req));
    CHECK(has_gap(thesis_trace(selected), "candidate_channel_unavailable"));
    CHECK(has_gap(thesis_trace(selected), "candidate_corpus_may_be_truncated"));
    const auto rendered = unwrap(engine.render(selected));
    CHECK(rendered.find("candidate_channel_unavailable") != std::string::npos);
    CHECK(rendered.find("candidate_corpus_may_be_truncated") != std::string::npos);
  }

  TEST_CASE("identity binds caller plan resolved run and exact selected bytes and is deterministic") {
    PlanFixture f;
    context::ContextEngine engine(*f.rt, *f.store, f.pack);
    auto req = f.request(Json::array({thesis("one")}));
    req.detail_resolution = Resolution::Raw;
    const auto original = unwrap(engine.select(req));
    CHECK(unwrap(engine.select(req)).to_json() == original.to_json());
    CHECK(unwrap(ContextSet::from_json(original.to_json())).to_json() == original.to_json());
    req.plan["source_ref"]["version"] = 2;
    CHECK(unwrap(engine.select(req)).id != original.id);
    req.plan["source_ref"].erase("version");
    f.ab.assessment.support[0].quote += " NEW-BYTES";
    LOOM_REQUIRE_OK(f.store->put_claims(f.run, {f.ab}));
    CHECK(unwrap(engine.select(req)).id != original.id);
    req.run.clear();
    CHECK(unwrap(engine.select(req)).goal.params["plan_trace"]["run"] == f.run);
    auto http = std::make_shared<net::ScriptedTransport>();
    http->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "unexpected model call"));
    f.rt->set_http_transport(http);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "test-key");
    req.goal_type.reset();
    CHECK(engine.build(req).has_value());
    CHECK(http->requests().empty());
  }
}
