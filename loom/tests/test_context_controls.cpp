// R28: geometric graph scope and representation detail are independent inputs.
#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <set>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

Entity entity(const std::string& label) {
  Entity value;
  value.kind = "component";
  value.canonical_key = label;
  value.label = label;
  value.id = Entity::make_id(value.kind, value.canonical_key);
  value.origin = Origin::Archive;
  value.confidence = 1;
  return value;
}

Claim relation(const Entity& subject, const Entity& object, const std::string& quote,
               EvidenceClass evidence = EvidenceClass::Observed) {
  Claim value;
  value.subject = subject.id;
  value.object = object.id;
  value.predicate = "depends_on";
  value.qualifiers.valid_from = "2026-09-20";
  value.assessment.evidence = evidence;
  value.assessment.origin = Origin::Archive;
  value.assessment.confidence = 0.9;
  Support support;
  support.observation = "ob_scope_controls";
  support.quote = quote;
  support.extractor = "scope_controls_test";
  value.assessment.support = {support};
  if (evidence == EvidenceClass::Extrapolated) {
    Derivation derivation;
    derivation.op = "scope_control_test_proposal";
    value.assessment.derivation = derivation;
  }
  value.id = Claim::make_id(value.subject, value.predicate, value.object, value.value, value.qualifiers);
  return value;
}

struct Fixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  kb::KnowledgeStore* store;
  std::shared_ptr<const kb::Pack> pack;
  std::string run;
  Entity a = entity("scope alpha"), b = entity("scope beta"), c = entity("scope gamma"), d = entity("scope delta");
  Entity x = entity("filtered bridge"), y = entity("behind filtered bridge");
  Entity isolated = entity("isolated source"), target = entity("isolated target");
  const std::string long_quote = "SOURCE-BEGIN " + std::string(220, 'q') + " SOURCE-END";
  Claim ab = relation(a, b, long_quote);
  Claim ba = relation(b, a, "An incoming cyclic relation.");
  Claim bc = relation(b, c, "Second hop source.");
  Claim cd = relation(c, d, "Third hop source.");
  Claim ax = relation(a, x, "An unaccepted extrapolated bridge.", EvidenceClass::Extrapolated);
  Claim xy = relation(x, y, "An observation behind a filtered bridge.");
  Claim premise = relation(isolated, target, "PREMISE-BEGIN " + std::string(8000, 'p') + " PREMISE-END");
  Principle core, ancestor;

  Fixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
    store = &runtime->knowledge().store();
    pack = unwrap(runtime->knowledge().pack());
    run = unwrap(store->begin_run(pack->hash(), Json::object())).id;
    core.id = "p.scope_core";
    core.statement = {{"en", "Keep independently bound evidence with each conclusion."}};
    core.level = PrincipleLevel::Strategy;
    core.form = PrincipleForm::Invariant;
    core.validation = ValidationStatus::Confirmed;
    core.confidence = 1;
    ancestor.id = "p.scope_ancestor";
    ancestor.statement = {{"en", "Preserve source provenance."}};
    ancestor.level = PrincipleLevel::Value;  // not in answer_question's selected levels
    ancestor.form = PrincipleForm::Heuristic;
    ancestor.validation = ValidationStatus::Supported;
    ancestor.confidence = 1;
    core.derived_from = {ancestor.id};
    ab.assessment.premises.claims = {premise.id};
    premise.assessment.premises.principles = {ancestor.id};
    LOOM_REQUIRE_OK(store->put_entities(run, {a, b, c, d, x, y, isolated, target}));
    LOOM_REQUIRE_OK(store->put_claims(run, {ab, ba, bc, cd, ax, xy, premise}));
    LOOM_REQUIRE_OK(store->put_principles(run, {core, ancestor}));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }

  context::ContextRequest request() const {
    context::ContextRequest req;
    req.text = "How does this component depend on the other components?";
    req.targets = {a.id};
    req.run = run;
    req.goal_type = "answer_question";
    req.budget_tokens = 100000;
    return req;
  }
};

std::set<std::string> references(const ContextSet& set) {
  std::set<std::string> refs;
  for (const auto& item : set.items) refs.insert(item.ref);
  return refs;
}

const ContextItem& find_item(const ContextSet& set, const std::string& ref) {
  auto it = std::find_if(set.items.begin(), set.items.end(), [&](const auto& item) { return item.ref == ref; });
  REQUIRE(it != set.items.end());
  return *it;
}

}  // namespace

TEST_SUITE("context_controls") {
  TEST_CASE("scope and detail JSON defaults remain compatible and explicit controls round trip") {
    const auto defaults = unwrap(context::ContextRequest::from_json(Json::object()));
    CHECK(defaults.relation_hops == 1);
    CHECK(!defaults.detail_resolution);
    CHECK(!defaults.to_json().contains("relation_hops"));
    CHECK(!defaults.to_json().contains("detail_resolution"));
    for (const auto& name : {"label", "summary", "full", "raw"}) {
      auto req = unwrap(context::ContextRequest::from_json(Json{{"relation_hops", 0}, {"detail_resolution", name}}));
      CHECK(req.relation_hops == 0);
      REQUIRE(req.detail_resolution);
      CHECK(std::string(to_string(*req.detail_resolution)) == name);
      CHECK(unwrap(context::ContextRequest::from_json(req.to_json())).to_json() == req.to_json());
    }
    CHECK(!unwrap(context::ContextRequest::from_json(Json{{"detail_resolution", nullptr}})).detail_resolution);
    for (const auto& invalid : {Json(-1), Json(true), Json(1.5), Json("2"), Json(nullptr), Json(std::numeric_limits<std::uint64_t>::max())}) {
      auto result = context::ContextRequest::from_json(Json{{"relation_hops", invalid}});
      REQUIRE(!result);
      CHECK(result.error().code == Errc::InvalidArgument);
    }
    for (const auto& invalid : {Json("verbose"), Json(1), Json(true), Json::array()}) {
      CHECK(!context::ContextRequest::from_json(Json{{"detail_resolution", invalid}}));
    }
  }

  TEST_CASE("same graph scope at ample budget preserves references while representation changes") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.detail_resolution = Resolution::Label;
    const auto labels = unwrap(engine.select(req));
    req.detail_resolution = Resolution::Full;
    const auto full = unwrap(engine.select(req));
    req.detail_resolution = Resolution::Raw;
    const auto raw = unwrap(engine.select(req));
    CHECK(references(labels) == references(full));
    CHECK(references(labels) == references(raw));
    CHECK(find_item(labels, fixture.ab.id).text.find("SOURCE-BEGIN") == std::string::npos);
    CHECK(find_item(full, fixture.ab.id).text.find("SOURCE-BEGIN") != std::string::npos);
    CHECK(find_item(full, fixture.ab.id).text.find("SOURCE-END") == std::string::npos);
    CHECK(find_item(raw, fixture.ab.id).text.find("SOURCE-END") != std::string::npos);
    CHECK(find_item(raw, fixture.premise.id).text.find("PREMISE-END") != std::string::npos);
    CHECK(find_item(raw, fixture.premise.id).required_by == std::vector<std::string>{fixture.ab.id});
    CHECK(raw.used_tokens > labels.used_tokens);
    CHECK(raw.budget_tokens == labels.budget_tokens);
    for (const auto& item : raw.items) CHECK(item.resolution == Resolution::Raw);
    CHECK(labels.goal.id == raw.goal.id);
    CHECK(labels.id != raw.id);
  }

  TEST_CASE("same detail widens scope by graph hops in either direction without crossing filtered evidence") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.detail_resolution = Resolution::Summary;
    const auto one = unwrap(engine.select(req));
    req.relation_hops = 2;
    const auto two = unwrap(engine.select(req));
    req.relation_hops = 3;
    const auto three = unwrap(engine.select(req));
    CHECK(references(one).count(fixture.ba.id) == 1);  // incoming edge is included
    CHECK(references(one).count(fixture.bc.id) == 0);
    CHECK(references(two).count(fixture.bc.id) == 1);
    CHECK(references(two).count(fixture.cd.id) == 0);
    CHECK(references(three).count(fixture.cd.id) == 1);
    CHECK(references(three).count(fixture.ax.id) == 0);
    CHECK(references(three).count(fixture.xy.id) == 0);
    CHECK(find_item(one, fixture.ab.id).text == find_item(three, fixture.ab.id).text);
    CHECK(find_item(two, fixture.bc.id).factors["relation_hops"] == 2);
    CHECK(find_item(three, fixture.cd.id).factors["relation_hops"] == 3);
    CHECK(one.id != two.id);
    CHECK(one.budget_tokens == three.budget_tokens);
  }

  TEST_CASE("zero exploratory hops preserve stable context and required principle closure") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.relation_hops = 0;
    const auto selected = unwrap(engine.select(req));
    CHECK(references(selected).count(fixture.ab.id) == 0);
    CHECK(references(selected).count(fixture.core.id) == 1);
    CHECK(references(selected).count(fixture.ancestor.id) == 1);
    CHECK(find_item(selected, fixture.ancestor.id).required_by == std::vector<std::string>{fixture.core.id});
    CHECK(selected.goal.params["context_controls"]["relation_hops"] == 0);
    CHECK(selected.goal.params["context_controls"]["detail_resolution"].is_null());
  }

  TEST_CASE("finite cyclic graph terminates at an arbitrarily large requested radius") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.relation_hops = std::numeric_limits<int>::max();
    const auto selected = unwrap(engine.select(req));
    CHECK(references(selected).count(fixture.cd.id) == 1);
    CHECK(references(selected).count(fixture.xy.id) == 0);
    CHECK(references(selected).size() == selected.items.size());
    CHECK(unwrap(engine.select(req)).to_json() == selected.to_json());
  }

  TEST_CASE("detail cannot hide a premise omitted under an unchanged small budget") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.budget_tokens = 1000;
    req.detail_resolution = Resolution::Label;
    const auto labels = unwrap(engine.select(req));
    req.detail_resolution = Resolution::Raw;
    const auto raw = unwrap(engine.select(req));
    CHECK(references(labels).count(fixture.premise.id) == 1);
    CHECK(find_item(labels, fixture.ab.id).missing_premises.empty());
    CHECK(references(raw).count(fixture.premise.id) == 0);
    CHECK(find_item(raw, fixture.ab.id).missing_premises == std::vector<std::string>{fixture.premise.id});
    CHECK(unwrap(engine.render(raw)).find("[INCOMPLETE") != std::string::npos);
    CHECK(raw.used_tokens <= 1000);
    CHECK(labels.used_tokens <= 1000);
    CHECK(raw.budget_tokens == labels.budget_tokens);
  }

  TEST_CASE("default selection stays identical and explicit controls survive trace and model JSON") {
    Fixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    const auto before = unwrap(engine.select(req));
    req.relation_hops = 1;
    req.detail_resolution.reset();
    CHECK(before.to_json() == unwrap(engine.select(req)).to_json());
    CHECK(before.id == ContextSet::make_id(before.goal.id, before.budget_tokens, before.pack_hash));
    CHECK(!before.goal.params.contains("context_controls"));
    req.relation_hops = 2;
    req.detail_resolution = Resolution::Full;
    const auto selected = unwrap(engine.select(req));
    CHECK(unwrap(ContextSet::from_json(selected.to_json())).to_json() == selected.to_json());
    CHECK(engine.trace(selected)["goal"]["params"]["context_controls"]["detail_resolution"] == "full");
    CHECK(selected.id != before.id);
  }

  TEST_CASE("scope and detail stay offline despite configured semantic model and credentials") {
    Fixture fixture;
    auto transport = std::make_shared<net::ScriptedTransport>();
    transport->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "unexpected model call"));
    fixture.runtime->set_http_transport(transport);
    fixture.runtime->config().set("semantic_model", "test-model");
    fixture.runtime->secrets().set("api_key", "test-key");
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.goal_type.reset();
    req.text = "uncued-token";
    for (int hops : {0, 1, 3}) {
      req.relation_hops = hops;
      for (auto resolution : {Resolution::Label, Resolution::Summary, Resolution::Full, Resolution::Raw}) {
        req.detail_resolution = resolution;
        CHECK(unwrap(engine.build(req))["goal"]["params"]["external_goal_typing"]["status"] == "offline");
      }
    }
    req.relation_hops = -1;
    CHECK(!engine.select(req));
    req.relation_hops = 1;
    req.detail_resolution = static_cast<Resolution>(99);
    CHECK(!engine.select(req));
    CHECK(transport->requests().empty());
  }

  TEST_CASE("empty target never retrieves unrelated literal-valued claims") {
    Fixture fixture;
    auto literal = relation(fixture.isolated, fixture.target, "Unrelated literal-valued source.");
    literal.predicate = "has_value";
    literal.object.clear();
    literal.value = "unrelated literal value";
    literal.id = Claim::make_id(literal.subject, literal.predicate, literal.object, literal.value, literal.qualifiers);
    LOOM_REQUIRE_OK(fixture.store->put_claims(fixture.run, {literal}));
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    auto req = fixture.request();
    req.targets = {""};
    const auto selected = unwrap(engine.select(req));
    for (const auto& item : selected.items) CHECK(item.ref_kind != RefKind::Claim);
  }
}
