// model.h: closed sets, JSON round trips of every model type, invariant
// checks (assessment, claim, operator, judgement, context set), status
// oscillation, content-derived ids.
#include <doctest/doctest.h>

#include "loom/model.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

template <class T>
void round_trip(const T& v) {
  Json j = v.to_json();
  auto back = T::from_json(j);
  INFO(json::dump(j));
  if (!back) FAIL(back.error().to_string());
  CHECK(json::dump(back->to_json()) == json::dump(j));
}

Locator loc() {
  Locator l;
  l.source = "sha256:" + std::string(64, 'a');
  l.member = "conversations.json";
  l.json_pointer = "/3/mapping/x";
  l.byte_start = 10;
  l.byte_len = 42;
  l.line = 7;
  return l;
}

kb::ExpectedProperty ep() {
  kb::ExpectedProperty e;
  e.expr = Json{{"op", "in_class"}, {"args", Json::array({"storage.embedded"})}};
  e.rationale = "local-first";
  e.confirm_if = {"a stores_in claim"};
  return e;
}

Claim observed_claim() {
  Claim c;
  c.subject = Entity::make_id("project", "chatadhd");
  c.predicate = "stores_in";
  c.object = Entity::make_id("storage", "sqlite");
  c.qualifiers.version = "0.7.9";
  c.qualifiers.lang = "pl";
  Support s;
  s.observation = "ob_1";
  s.locator = loc();
  s.quote = "trzyma dane w SQLite";
  s.extractor = "extract.pattern@1";
  s.quality = 0.9;
  c.assessment.support.push_back(s);
  c.assessment.confidence = 0.8;
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

}  // namespace

TEST_SUITE("model") {
  TEST_CASE("closed sets: names round-trip, unknown names are rejected with the set listed") {
    CHECK(count<Role>() == 14);
    for (auto r : all<Role>()) CHECK(from_string<Role>(to_string(r)) == r);
    for (auto o : all<Origin>()) CHECK(from_string<Origin>(to_string(o)) == o);
    for (auto e : all<EvidenceClass>()) {
      CHECK(from_string<EvidenceClass>(to_string(e)) == e);
      CHECK(kb::evidence_from_string(to_string(e)) == e);  // one vocabulary with kb
    }
    for (auto s : all<StatusValue>()) CHECK(from_string<StatusValue>(to_string(s)) == s);
    CHECK(to_string(StatusValue::Lost) == "lost");
    CHECK(to_string(PrincipleForm::ConflictResolution) == "conflict_resolution");
    CHECK(to_string(Origin::ModelKnowledge) == "model_knowledge");
    CHECK(!from_string<Role>("module"));  // a domain kind, not a universal role
    auto r = parse<Origin>("gossip", "claim/origin");
    REQUIRE(!r);
    CHECK(r.error().message.find("unknown origin 'gossip'") != std::string::npos);
    CHECK(r.error().message.find("model_knowledge") != std::string::npos);
  }

  TEST_CASE("closed-set semantics: premises, authority order") {
    CHECK(may_be_premise(EvidenceClass::Observed));
    CHECK(may_be_premise(EvidenceClass::Inferred));
    CHECK(!may_be_premise(EvidenceClass::Extrapolated));
    CHECK(!may_be_premise(EvidenceClass::Absent));
    CHECK(authority_rank(Origin::User) > authority_rank(Origin::Archive));
    CHECK(authority_rank(Origin::Archive) == authority_rank(Origin::Repo));
    CHECK(authority_rank(Origin::Repo) > authority_rank(Origin::ExternalAuthority));
    CHECK(authority_rank(Origin::ExternalAuthority) > authority_rank(Origin::System));
    CHECK(authority_rank(Origin::System) > authority_rank(Origin::ModelKnowledge));
    CHECK(is_producible(EvidenceClass::Extrapolated));
    CHECK(!is_producible(EvidenceClass::Observed));
  }

  TEST_CASE("layer A/B round trips") {
    Unit u;
    u.source = "src_1";
    u.kind = "conversation";
    u.locator = loc();
    u.id = Unit::make_id(u.source, u.locator);
    u.title = "Loom";
    u.lang = "pl";
    round_trip(u);
    Observation o;
    o.unit = u.id;
    o.kind = ObservationKind::ListItem;
    o.text = "wszystko jest danymi";
    o.locator = loc();
    o.id = Observation::make_id(o.unit, o.locator, o.text);
    o.attrs = Json{{"depth", 1}};
    round_trip(o);
    Entity e;
    e.kind = "project";
    e.canonical_key = "chat adhd";
    e.id = Entity::make_id(e.kind, e.canonical_key);
    e.label = "ChatADHD";
    e.labels = {{"en", "ChatADHD"}, {"pl", "ChatADHD"}};
    e.aliases.push_back(Alias{"czat adhd", "czat ADHD", "pl", "lexicon", 3, 0.9});
    round_trip(e);
    Claim c = observed_claim();
    LOOM_REQUIRE_OK(c.validate());
    round_trip(c);
    // an inferred claim with every question answered
    Claim i;
    i.subject = c.subject;
    i.predicate = "stores_in";
    i.object = c.object;
    i.assessment.evidence = EvidenceClass::Inferred;
    i.assessment.origin = Origin::ModelKnowledge;
    i.assessment.confidence = 0.55;
    i.assessment.derivation = Derivation{"r.storage_local_first", 1, "", 1};
    i.assessment.premises.claims = {c.id};
    i.assessment.premises.principles = {"p.local_first"};
    i.assessment.counter.claims = {"cl_x"};
    i.assessment.status = ClaimStatus::Contested;
    i.assessment.consequences.predictions = {"pn_1"};
    i.assessment.open.questions = {"which sync?"};
    i.assessment.expected = ep();
    i.assessment.check = CheckState::Pending;
    i.assessment.alternatives.push_back(Alternative{"", Json("LevelDB"), 0.1});
    i.id = Claim::make_id(i.subject, i.predicate, i.object, i.value, i.qualifiers);
    LOOM_REQUIRE_OK(i.validate());
    round_trip(i);
    Model m;
    m.name = "reading A";
    m.id = Model::make_id(m.name);
    m.claims = {c.id};
    round_trip(m);
  }

  TEST_CASE("assessment and claim invariants (I2, I3)") {
    Claim c = observed_claim();
    c.assessment.support.clear();
    CHECK(!c.validate());  // observed without support
    c = observed_claim();
    c.value = "SQLite";
    CHECK(!c.validate());  // object and value
    c = observed_claim();
    c.assessment.evidence = EvidenceClass::Inferred;
    c.assessment.derivation = Derivation{"r.x", 1, "", 0};
    CHECK(!c.validate());  // inferred without expected property
    c.assessment.expected = ep();
    CHECK(!c.validate());  // ... and without a check state
    c.assessment.check = CheckState::Pending;
    LOOM_REQUIRE_OK(c.validate());
    c.assessment.confidence = 1.5;
    CHECK(!c.validate());
    Claim a;
    a.subject = "e_1";
    a.predicate = "stores_in";
    a.assessment.evidence = EvidenceClass::Absent;
    a.assessment.open.slots = {"in_1/storage"};
    a.assessment.open.fill_query = Json{{"terms", Json::array({"storage", "baza danych"})}};
    LOOM_REQUIRE_OK(a.validate());
    a.value = "x";
    CHECK(!a.validate());
    // unknown closed-set names are rejected on read
    Json j = observed_claim().to_json();
    j["assessment"]["origin"] = "hearsay";
    auto bad = Claim::from_json(j);
    REQUIRE(!bad);
    CHECK(bad.error().message.find("unknown origin 'hearsay'") != std::string::npos);
    j = observed_claim().to_json();
    j["assessment"]["evidence_class"] = "conflicting";  // conflict is a state, not a class (§2.3)
    CHECK(!Claim::from_json(j));
    j = observed_claim().to_json();
    j["assessment"]["expected_property"] = Json{{"expr", Json{{"op", "looks_right"}}}};
    CHECK(!Claim::from_json(j));
  }

  TEST_CASE("layer C round trips and operator/morphism rules") {
    Principle p;
    p.id = "p.kod_ne_dane";
    p.statement = {{"en", "Code is not data"}, {"pl", "Kod ≠ dane"}};
    p.phrasings = {"KOD ≠ DANE"};
    p.level = PrincipleLevel::Strategy;
    p.form = PrincipleForm::Invariant;
    p.scope.project_kinds = {"software_app"};
    p.protects = {"p.value.minimal_arbitrariness"};
    p.predicts.push_back(SituationSolution{"new data source", "extend the provider registry"});
    p.sources.push_back(Reference{"MEGA_MASTER_2026-09-16.md", "§2.B", "2026-09-16", "", "", ""});
    round_trip(p);
    CHECK(!p.is_preference());
    p.owner = "user";
    p.scope.products = {"codebase"};
    CHECK(p.is_preference());
    Json pj = p.to_json();
    pj["form"] = "dogma";
    CHECK(!Principle::from_json(pj));

    Operator rule;
    rule.id = "r.x";
    rule.produces = EvidenceClass::Inferred;
    rule.stratum = 1;
    rule.when = Json{{"op", "const"}, {"args", Json::array({true})}};
    rule.value = Json{{"op", "const"}, {"args", Json::array({"x"})}};
    rule.target = Json{{"paradigm", "software_app"}, {"slot", "store"}};
    rule.expected = ep();
    LOOM_REQUIRE_OK(rule.validate());
    round_trip(rule);
    rule.stratum = 2;
    CHECK(!rule.validate());
    Operator design;
    design.id = "op.new_source";
    design.situation = {{"en", "a new data source appears"}};
    design.solution = {{"en", "extend the provider/capability registry"}};
    design.principles = {"p.capability_not_enum"};
    LOOM_REQUIRE_OK(design.validate());
    round_trip(design);
    design.solution.clear();
    CHECK(!design.validate());

    Morphism m;
    m.id = "m.check";
    m.use = MorphismUse::Transfer;
    m.from.paradigm = "software_app";
    m.from.kind = "test";
    m.to.paradigm = "film";
    m.to.kind = "continuity_check";
    m.expected = ep();
    round_trip(m);
    Json mj = m.to_json();
    mj["to"] = Json{{"role", "check"}};
    CHECK(!Morphism::from_json(mj));  // a transfer does not end on a role
    mj = m.to_json();
    mj["expected_property"] = nullptr;
    CHECK(!Morphism::from_json(mj));  // a transfer states its expected property
  }

  TEST_CASE("paradigm views: domain kinds map to exactly one role; anchoring checks relations") {
    Json pk = Json::parse(R"({"id":"toy","title":{"en":"Toy"},"domain_kinds":[
      {"id":"module","role":"part","entity_kind":"component","relation":"has_component"},
      {"id":"test","role":"check","relations":[{"rel":"verifies","target":"module"}]}]})");
    auto k = unwrap(ProjectKind::from_json(pk));
    CHECK(k.domain_kind("test")->role == Role::Check);
    CHECK(k.domain_kind("module")->value_type == "entity:component");
    round_trip(k);
    pk["domain_kinds"][1]["role"] = "tests";
    CHECK(!ProjectKind::from_json(pk));
    pk["domain_kinds"][1].erase("role");
    CHECK(!ProjectKind::from_json(pk));  // the role is required
    pk["domain_kinds"][1]["role"] = "check";
    pk["domain_kinds"][1]["id"] = "module";
    CHECK(!ProjectKind::from_json(pk));  // duplicate kind

    AnchoringModel am = unwrap(AnchoringModel::from_json(Json::parse(R"({
      "role_relations":[{"id":"verifies","from":["check"],"to":["part","output","artifact"]}],
      "relation_map":{"verifies":"verifies"}})")));
    CHECK(am.check("verifies", Role::Check, Role::Part).empty());
    CHECK(!am.check("verifies", Role::Part, Role::Check).empty());
    CHECK(!am.check("eats", Role::Check, Role::Part).empty());
    round_trip(am);

    ArtifactType at = unwrap(ArtifactType::from_json(Json::parse(R"({"id":"brainstorm","title":{"en":"Brainstorm"},
      "medium":"text","structure":[{"name":"areas","type":"record[]","card":"many"}]})")));
    round_trip(at);
    Facet f = unwrap(Facet::from_json(Json::parse(R"({"id":"multiplatform","title":{"en":"Multi-platform"},
      "applies_to":["software_app"],"domain_kinds":[{"id":"platform","role":"resource"}]})")));
    round_trip(f);
  }

  TEST_CASE("layer D/E round trips") {
    GoalType gt = unwrap(GoalType::from_json(Json::parse(R"({"id":"implement_part","roles":["part","interface","constraint"],
      "principle_levels":["strategy"],"evidence":["observed","derived","user","inferred"],
      "resolutions":{"part":"full","*":"summary"},"budget":{"stable":0.2,"project":0.3,"goal":0.5}})")));
    round_trip(gt);
    Json bad = gt.to_json();
    bad["budget"]["goal"] = 0.9;
    CHECK(!GoalType::from_json(bad));
    bad = gt.to_json();
    bad["resolutions"]["module"] = "full";
    CHECK(!GoalType::from_json(bad));

    ContextSet cs;
    cs.goal.type = "implement_part";
    cs.goal.text = "dodaj moduł importu";
    cs.goal.id = Goal::make_id(cs.goal.type, cs.goal.text, cs.goal.targets);
    cs.budget_tokens = 1000;
    cs.id = ContextSet::make_id(cs.goal.id, cs.budget_tokens, "h");
    ContextItem a{RefKind::Principle, "p.kod_ne_dane", ContextBand::Stable, Resolution::Label, 0.9, Json::object(), 12,
                  "core invariant", {}, ""};
    ContextItem b{RefKind::Claim, "cl_1", ContextBand::Goal, Resolution::Full, 0.7, Json::object(), 100,
                  "the module to implement", {}, ""};
    cs.items = {a, b};
    cs.used_tokens = 112;
    LOOM_REQUIRE_OK(cs.validate());
    round_trip(cs);
    std::swap(cs.items[0], cs.items[1]);
    CHECK(!cs.validate());  // bands out of order

    Decision d;
    d.id = "cl_dec";
    d.subject = "e_storage";
    d.alternatives.push_back(DecisionAlternative{"SQLite", "", Json("sqlite"),
                                                 {ValueImpact{"p.value.autonomy", ValueEffect::Positive, "local"}}, true});
    d.alternatives.push_back(DecisionAlternative{"server DB", "", Json("pg"), {}, false});
    d.principles = {"p.local_first"};
    round_trip(d);
    CHECK(d.chosen()->label == "SQLite");
    Json dj = d.to_json();
    dj["alternatives"][1]["chosen"] = true;
    CHECK(!Decision::from_json(dj));

    Fork f;
    f.kind = ForkKind::CodeLineage;
    f.subject = "e_chatadhd";
    f.base = "0.7.9";
    f.sides = {ForkSide{"0.7.10", "main", true, false, ""}, ForkSide{"0.8.3", "attachments", false, false, "2026-03-17"}};
    f.id = Fork::make_id(f.kind, f.subject, f.base, f.sides);
    round_trip(f);
    Prediction pn;
    pn.situation = "new media type";
    pn.solution = "provider abstraction";
    pn.op = "op.new_source";
    pn.cut = "2026-02-08";
    pn.id = Prediction::make_id(pn.op, pn.situation, pn.cut);
    round_trip(pn);
    Product pd;
    pd.kind = "specification";
    pd.id = Product::make_id(pd.kind, "in_1", "kr_1");
    pd.checks = {ProductCheck{"chk.no_emoji", "p.pref.ascii_icons", false, "3 emoji"}};
    CHECK(!pd.passed());
    round_trip(pd);
  }

  TEST_CASE("judgements: verdicts per target kind; sources are immutable") {
    Judgement j;
    j.target_kind = RefKind::Entity;
    j.target = "e_loom_weaving";
    j.verdict = Verdict::Split;
    j.payload = Json{{"aliases", Json::array({"loom band"})}, {"key", "loom weaving"}};
    j.created = "2026-09-26T10:00:00Z";
    j.id = Judgement::make_id(j.created, j.target, j.verdict, j.payload);
    LOOM_REQUIRE_OK(j.validate());
    round_trip(j);
    j.target_kind = RefKind::Claim;
    CHECK(!j.validate());  // only entities split
    j.verdict = Verdict::Edit;
    j.payload = Json{{"value", "0.9.0"}};
    LOOM_REQUIRE_OK(j.validate());
    j.target_kind = RefKind::Observation;
    CHECK(!j.validate());
    j.target_kind = RefKind::Entity;
    j.verdict = Verdict::Merge;
    j.payload = Json{{"into", j.target}};
    CHECK(!j.validate());
  }

  TEST_CASE("status history: per branch and version, oscillation recorded (lost -> restored -> lost)") {
    auto rec = [](std::string v, StatusValue s, std::string date, std::string branch = "") {
      StatusRecord r;
      r.entity = "e_api_editor";
      r.branch = branch;
      r.version = v;
      r.status = s;
      r.date = date;
      r.id = StatusRecord::make_id(r.entity, r.branch, r.version, r.status, r.date);
      return r;
    };
    // The API request editor of the historical report: full in 0.6.2, a stub
    // in 0.7.10, raw JSON again in 0.8.3 (other branch), gone in 0.9.0.
    std::vector<StatusRecord> in = {rec("0.9.0", StatusValue::Lost, "2026-04-17"),
                                    rec("0.4.6", StatusValue::Partial, "2026-01-30"),
                                    rec("0.7.10", StatusValue::Lost, "2026-03-05"),
                                    rec("0.6.2", StatusValue::Implemented, "2026-02-08"),
                                    rec("0.8.3", StatusValue::Restored, "2026-03-17"),
                                    rec("0.8.4", StatusValue::Lost, "2026-03-21"),
                                    rec("0.8.5", StatusValue::Restored, "2026-03-25"),
                                    rec("0.8.3", StatusValue::Partial, "2026-03-17", "attachments")};
    auto out = order_status_history(in);
    REQUIRE(out.size() == 8);
    std::vector<std::string> main_line;
    for (const auto& r : out) {
      if (r.branch.empty()) main_line.push_back(r.version + ":" + std::string(to_string(r.status)) + (r.oscillation ? "!" : ""));
    }
    CHECK(main_line == std::vector<std::string>{"0.4.6:partial", "0.6.2:implemented", "0.7.10:lost", "0.8.3:restored",
                                                "0.8.4:lost!", "0.8.5:restored!", "0.9.0:lost!"});
    CHECK(out[0].branch == "");
    CHECK(out[1].previous == StatusValue::Partial);
    CHECK(out.back().branch == "attachments");
    CHECK(!out.back().previous);
    round_trip(out[3]);
    CHECK(order_status_history(out)[3].to_json() == out[3].to_json());  // idempotent
  }

  TEST_CASE("content-derived ids are stable and discriminating (I5)") {
    Claim a = observed_claim();
    Claim b = observed_claim();
    CHECK(a.id == b.id);
    CHECK(a.id.rfind("cl_", 0) == 0);
    CHECK(a.id.size() == 3 + kb::kStableIdHex);
    b.qualifiers.branch = "0.8.x";
    CHECK(Claim::make_id(b.subject, b.predicate, b.object, b.value, b.qualifiers) != a.id);
    CHECK(Claim::make_id("s", "p", "", Json("x"), {}) != Claim::make_id("s", "p", "x", nullptr, {}));
    CHECK(Entity::make_id("project", "loom") != Entity::make_id("concept", "loom"));
    CHECK(Instance::make_id("film", "e_1").rfind("in_", 0) == 0);
  }
}
