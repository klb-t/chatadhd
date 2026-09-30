// Retrieval diagnostics are mechanism evidence, not semantic-recall estimates.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>

#include "loom/context_engine.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

Entity diagnostic_entity(const std::string& name) {
  Entity entity;
  entity.kind = "component";
  entity.canonical_key = name;
  entity.label = name;
  entity.id = Entity::make_id(entity.kind, name);
  return entity;
}

Claim diagnostic_claim(const std::string& id, const std::string& subject) {
  Claim claim;
  claim.id = id;
  claim.subject = subject;
  claim.predicate = "has_evidence";
  claim.value = "a traceable value";
  claim.assessment.evidence = EvidenceClass::Observed;
  claim.assessment.origin = Origin::Archive;
  claim.assessment.confidence = 0.9;
  Support support;
  support.observation = "ob_diagnostic_fixture";
  support.quote = "Recorded source material.";
  support.extractor = "diagnostic_fixture";
  claim.assessment.support = {support};
  return claim;
}

struct DiagnosticFixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  kb::KnowledgeStore* store;
  std::shared_ptr<const kb::Pack> pack;
  std::string run;
  Entity anchor = diagnostic_entity("diagnostic anchor");

  DiagnosticFixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
    store = &runtime->knowledge().store();
    pack = unwrap(runtime->knowledge().pack());
    run = unwrap(store->begin_run(pack->hash(), Json::object())).id;
    LOOM_REQUIRE_OK(store->put_entities(run, {anchor}));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  context::ContextRequest request() const {
    context::ContextRequest request;
    request.run = run;
    request.targets = {anchor.id};
    request.project = anchor.id;
    request.goal_type = "answer_question";
    request.budget_tokens = 10000;
    return request;
  }

  void corrupt_claim(const std::string& id) {
    auto lock = runtime->db().lock();
    auto statement = unwrap(runtime->db().conn().prepare("UPDATE loom_kb_claims SET body = ? WHERE run_id = ? AND id = ?"));
    statement.bind(1, "{corrupt diagnostic body");
    statement.bind(2, run);
    statement.bind(3, id);
    LOOM_REQUIRE_OK(statement.run());
  }
};

const Json& diagnostics(const ContextSet& set) {
  return set.goal.params.at("retrieval_diagnostics");
}

const ContextItem& diagnostic_item(const ContextSet& set, const std::string& ref) {
  auto found = std::find_if(set.items.begin(), set.items.end(), [&](const auto& item) { return item.ref == ref; });
  REQUIRE(found != set.items.end());
  return *found;
}

}  // namespace

TEST_SUITE("context_diagnostics") {
  TEST_CASE("bounded query diagnostics distinguish empty retrieval from errors and round trip") {
    DiagnosticFixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto selected = unwrap(engine.select(fixture.request()));
    const auto& diagnostic = diagnostics(selected);
    CHECK(diagnostic["status"] == "bounded");
    CHECK(diagnostic["exhaustive"] == false);
    CHECK(diagnostic["query_errors"] == 0);
    CHECK(diagnostic["cap_hits"] == 0);
    int graph_queries = 0;
    for (const auto& query : diagnostic["queries"]) {
      if (query["operation"] != "graph") continue;
      ++graph_queries;
      CHECK(query["status"] == "below_cap");
      CHECK(query["returned"] == 0);
      CHECK(query["limit"] == 10000);
    }
    CHECK(graph_queries == 2);
    CHECK(unwrap(ContextSet::from_json(selected.to_json())).to_json() == selected.to_json());
    CHECK(engine.trace(selected)["goal"]["params"]["retrieval_diagnostics"] == diagnostic);
  }

  TEST_CASE("goal band consumes unused earlier capacity without rounding above the item budget") {
    DiagnosticFixture fixture;
    auto claim = diagnostic_claim("cl_budget_diagnostic", fixture.anchor.id);
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim}));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto request = fixture.request();
    auto roomy = unwrap(engine.select(request));
    REQUIRE(roomy.items.size() == 1);
    request.budget_tokens = roomy.items.front().tokens;
    const auto exact = unwrap(engine.select(request));
    REQUIRE(exact.items.size() == 1);
    CHECK(exact.items.front().ref == claim.id);
    CHECK(exact.used_tokens == request.budget_tokens);
    for (int budget = 1; budget <= 80; ++budget) {
      request.budget_tokens = budget;
      const auto selected = unwrap(engine.select(request));
      CHECK(selected.used_tokens <= budget);
      const auto& capacities = diagnostics(selected)["budget"]["initial_band_tokens"];
      CHECK(capacities[0].get<int>() + capacities[1].get<int>() + capacities[2].get<int>() == budget);
      CHECK(diagnostics(selected)["budget"]["includes_prompt_overhead"] == false);
    }
  }

  TEST_CASE("decision and graph views share one item and preserve both premise lists") {
    DiagnosticFixture fixture;
    auto claim = diagnostic_claim("cl_shared_decision", fixture.anchor.id);
    claim.assessment.premises.principles = {"p.underlying_claim"};
    Decision decision;
    decision.id = claim.id;
    decision.subject = fixture.anchor.id;
    decision.principles = {"p.decision_rationale"};
    decision.alternatives = {DecisionAlternative{"retained alternative", "", "selected value", {}, true}};
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim}));
    LOOM_REQUIRE_OK(fixture.store->put_decisions(fixture.run, {decision}));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto request = fixture.request();
    request.detail_resolution = Resolution::Full;
    const auto selected = unwrap(engine.select(request));
    CHECK(std::count_if(selected.items.begin(), selected.items.end(), [&](const auto& item) { return item.ref == claim.id; }) == 1);
    const auto& item = diagnostic_item(selected, claim.id);
    CHECK(item.ref_kind == RefKind::Decision);
    const std::set<std::string> expected_missing{"p.decision_rationale", "p.underlying_claim"};
    CHECK(std::set<std::string>(item.missing_premises.begin(), item.missing_premises.end()) == expected_missing);
    CHECK(item.text.find("retained alternative") != std::string::npos);
    CHECK(item.text.find("Recorded source material.") != std::string::npos);
    CHECK(item.factors["candidate_views"].size() == 2);
    CHECK(diagnostics(selected)["duplicate_candidates_merged"] == 1);
    CHECK(unwrap(engine.render(selected)).find("[INCOMPLETE") != std::string::npos);
  }

  TEST_CASE("independently rounded band shares cannot create an extra spendable token") {
    DiagnosticFixture fixture;
    std::vector<Principle> principles;
    for (int index = 0; index < 10; ++index) {
      Principle principle;
      principle.id = "p.tiny_" + std::to_string(index);
      principle.statement = {{"en", "unit"}};
      principle.form = PrincipleForm::Invariant;
      principle.level = PrincipleLevel::Strategy;
      principles.push_back(std::move(principle));
    }
    LOOM_REQUIRE_OK(fixture.store->put_principles(fixture.run, principles));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto request = fixture.request();
    request.detail_resolution = Resolution::Label;
    // answer_question's 0.15/0.35/0.5 shares independently round to 1+2+3.
    // Ten one-token candidates can expose the resulting six-token overspend.
    request.budget_tokens = 5;
    const auto selected = unwrap(engine.select(request));
    CHECK(selected.used_tokens == 5);
    CHECK(selected.items.size() == 5);
    CHECK(selected.dropped.size() == 5);
  }

  TEST_CASE("failed graph query is not reported as an empty successful query") {
    DiagnosticFixture fixture;
    auto claim = diagnostic_claim("cl_corrupt_neighbour", fixture.anchor.id);
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim}));
    fixture.corrupt_claim(claim.id);
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    const auto selected = unwrap(engine.select(fixture.request()));
    CHECK(diagnostics(selected)["status"] == "incomplete");
    CHECK(diagnostics(selected)["query_errors"] == 1);
    bool found_error = false;
    for (const auto& query : diagnostics(selected)["queries"]) {
      if (query["operation"] != "graph" || query["selector"]["direction"] != "outgoing") continue;
      found_error = true;
      CHECK(query["status"] == "error");
      CHECK(query["returned"].is_null());
      CHECK(query["error_code"] == "database");
    }
    CHECK(found_error);
    CHECK(unwrap(engine.render(selected)).find("retrieval encountered") != std::string::npos);
  }

  TEST_CASE("unreadable and absent premises retain separate reasons") {
    DiagnosticFixture fixture;
    auto claim = diagnostic_claim("cl_needs_premises", fixture.anchor.id);
    auto premise = diagnostic_claim("cl_unreadable_premise", "e_outside_scope");
    claim.assessment.premises.claims = {premise.id, "cl_absent_premise"};
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim, premise}));
    fixture.corrupt_claim(premise.id);
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    const auto selected = unwrap(engine.select(fixture.request()));
    CHECK(diagnostic_item(selected, claim.id).missing_premises.size() == 2);
    std::map<std::string, std::string> reasons;
    for (const auto& missing : diagnostics(selected)["missing_premises"]) {
      reasons[missing["ref"].get<std::string>()] = missing["reason"].get<std::string>();
    }
    CHECK(reasons[premise.id] == "query_error");
    CHECK(reasons["cl_absent_premise"] == "missing_record");
  }

  TEST_CASE("a reached adjacency cap stays explicit even when every fetched claim is filtered") {
    DiagnosticFixture fixture;
    std::vector<Claim> claims;
    for (int index = 0; index < 10001; ++index) {
      auto claim = diagnostic_claim("cl_capped_" + std::to_string(index), fixture.anchor.id);
      claim.assessment.evidence = EvidenceClass::Extrapolated;
      Derivation derivation;
      derivation.op = "diagnostic_proposal";
      claim.assessment.derivation = derivation;
      claims.push_back(std::move(claim));
    }
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, claims));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    const auto selected = unwrap(engine.select(fixture.request()));
    CHECK(selected.items.empty());
    CHECK(diagnostics(selected)["cap_hits"] == 1);
    CHECK(diagnostics(selected)["status"] == "incomplete");
    CHECK(diagnostics(selected)["excluded"].size() == 10000);
    bool capped = false;
    for (const auto& query : diagnostics(selected)["queries"]) {
      if (query["status"] != "cap_reached") continue;
      capped = true;
      CHECK(query["returned"] == 10000);
    }
    CHECK(capped);
  }

  TEST_CASE("zero-hop selection executes no adjacency queries and budget omissions keep factors") {
    DiagnosticFixture fixture;
    auto claim = diagnostic_claim("cl_omitted_by_budget", fixture.anchor.id);
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim}));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto request = fixture.request();
    request.relation_hops = 0;
    const auto zero = unwrap(engine.select(request));
    for (const auto& query : diagnostics(zero)["queries"]) CHECK(query["operation"] != "graph");
    request.relation_hops = 1;
    request.budget_tokens = 1;
    const auto tight = unwrap(engine.select(request));
    REQUIRE(tight.dropped.size() == 1);
    CHECK(tight.dropped.front().factors["exclusion_reason"] == "budget");
    CHECK(tight.dropped.front().factors.contains("relevance"));
    CHECK(tight.dropped.front().factors["discovery_reason"].get<std::string>().find("one hop") != std::string::npos);
  }

  TEST_CASE("a deferred project premise accepted after budget carry is not still reported dropped") {
    DiagnosticFixture fixture;
    Principle premise;
    premise.id = "p.recovered_after_carry";
    premise.level = PrincipleLevel::Epistemic;
    premise.form = PrincipleForm::Heuristic;
    premise.statement = {{"en", "Preserve this supporting principle: " + std::string(600, 'p')}};
    auto claim = diagnostic_claim("cl_needs_deferred_project_premise", fixture.anchor.id);
    claim.assessment.premises.principles = {premise.id};
    LOOM_REQUIRE_OK(fixture.store->put_principles(fixture.run, {premise}));
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {claim}));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto request = fixture.request();
    request.detail_resolution = Resolution::Full;
    const auto roomy = unwrap(engine.select(request));
    const auto premise_tokens = diagnostic_item(roomy, premise.id).tokens;
    const auto claim_tokens = diagnostic_item(roomy, claim.id).tokens;
    REQUIRE(premise_tokens > claim_tokens + 2);
    // P cannot fit either initial half, then project carry lets C + P fit
    // exactly. C pulls P back through dependency closure.
    request.budget_tokens = premise_tokens + claim_tokens;
    const auto selected = unwrap(engine.select(request));
    CHECK(selected.used_tokens == request.budget_tokens);
    CHECK(diagnostic_item(selected, claim.id).missing_premises.empty());
    CHECK(diagnostic_item(selected, premise.id).required_by == std::vector<std::string>{claim.id});
    CHECK(std::none_of(selected.dropped.begin(), selected.dropped.end(), [&](const auto& item) { return item.ref == premise.id; }));
    const auto& recovered = diagnostics(selected).at("recovered_budget_drops");
    REQUIRE(recovered.is_array());
    CHECK(std::any_of(recovered.begin(), recovered.end(), [&](const auto& item) { return item.value("ref", "") == premise.id; }));
  }
}
