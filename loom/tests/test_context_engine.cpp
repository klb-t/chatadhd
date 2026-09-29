// context_engine.h: goal typing (deterministic PL/EN cue classifier + an
// optional, config-gated LLM fallback), the ContextSet builder (score,
// diversity, dependency closure, three-band token budget) and the renderer.
#include <doctest/doctest.h>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

std::unique_ptr<Runtime> open_rt(const std::filesystem::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

Entity project_entity(const std::string& key, const std::string& label) {
  Entity e;
  e.kind = "project";
  e.canonical_key = key;
  e.id = Entity::make_id(e.kind, key);
  e.label = label;
  e.origin = Origin::Archive;
  e.confidence = 1.0;
  return e;
}

Entity feature_entity(const std::string& parent, const std::string& key, const std::string& label) {
  Entity e;
  e.kind = "component";
  e.canonical_key = key;
  e.id = Entity::make_id(e.kind, key);
  e.label = label;
  e.parent = parent;
  e.origin = Origin::Archive;
  e.confidence = 0.9;
  return e;
}

Claim claim(const std::string& s, const std::string& p, const std::string& o, EvidenceClass ev, double conf,
           std::string date, std::vector<std::string> premise_principles = {}) {
  Claim c;
  c.subject = s;
  c.predicate = p;
  c.object = o;
  c.assessment.evidence = ev;
  c.assessment.origin = Origin::Archive;
  c.assessment.confidence = conf;
  c.qualifiers.valid_from = date;
  c.assessment.premises.principles = std::move(premise_principles);
  Support sup;
  sup.observation = "ob_1";
  sup.quote = "some supporting quote";
  sup.extractor = "test@1";
  c.assessment.support = {sup};
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

Principle invariant_principle(const std::string& id) {
  Principle p;
  p.id = id;
  p.statement = {{"en", "Code describes universal operations; data describes variety."}, {"pl", "Kod opisuje uniwersalne operacje; dane opisują odmienność."}};
  p.level = PrincipleLevel::Strategy;
  p.form = PrincipleForm::Invariant;
  p.validation = ValidationStatus::Confirmed;
  p.confidence = 0.95;
  return p;
}

Principle preference_principle(const std::string& id) {
  Principle p;
  p.id = id;
  p.statement = {{"en", "Never use emoji in UI code."}};
  p.level = PrincipleLevel::Strategy;
  p.form = PrincipleForm::Default;
  p.owner = "user";
  p.scope.products = {"code"};
  p.validation = ValidationStatus::Supported;
  p.confidence = 0.9;
  return p;
}

Principle project_principle(const std::string& id) {
  Principle p;
  p.id = id;
  p.statement = {{"en", "Decide fast on what is known now, correct later."}};
  p.level = PrincipleLevel::Strategy;
  p.form = PrincipleForm::Heuristic;
  p.validation = ValidationStatus::Supported;
  p.confidence = 0.7;
  p.sources.push_back(Reference{"chat", "", "2025-08-10", "", "", ""});
  return p;
}

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  std::shared_ptr<const kb::Pack> pack;
  kb::KnowledgeStore* ks = nullptr;
  std::string run;

  Entity proj = project_entity("noteflow", "NoteFlow");
  Entity feat = feature_entity(proj.id, "checklist", "checklist note type");
  Principle p_invariant = invariant_principle("p.kod_ne_dane");
  Principle p_pref = preference_principle("p.pref.ascii_icons");
  Principle p_project = project_principle("pr.decide_fast_correct_later");
  Claim c_impl = claim(proj.id, "implements", feat.id, EvidenceClass::Observed, 0.9, "2025-05-10", {p_project.id});
  Decision decision;

  Fixture() {
    rt = open_rt(td.path());
    pack = unwrap(rt->knowledge().pack());
    ks = &rt->knowledge().store();
    run = unwrap(ks->begin_run(pack->hash(), Json::object())).id;

    LOOM_REQUIRE_OK(ks->put_entities(run, {proj, feat}));
    LOOM_REQUIRE_OK(ks->put_principles(run, {p_invariant, p_pref, p_project}));
    LOOM_REQUIRE_OK(ks->put_claims(run, {c_impl}));

    decision.id = c_impl.id;  // a Decision IS its `decides` claim id
    decision.subject = proj.id;
    decision.date = "2025-05-10";
    decision.alternatives = {DecisionAlternative{"new class", "", nullptr, {}, false},
                             DecisionAlternative{"data-shaped variant", "", nullptr, {}, true}};
    decision.principles = {p_project.id};
    LOOM_REQUIRE_OK(ks->put_decisions(run, {decision}));

    StatusRecord sr1, sr2, sr3;
    sr1.entity = feat.id;
    sr1.version = "0.5.0";
    sr1.status = StatusValue::Implemented;
    sr1.date = "2025-05-10";
    sr1.id = StatusRecord::make_id(sr1.entity, sr1.branch, sr1.version, sr1.status, sr1.date);
    sr2.entity = feat.id;
    sr2.version = "0.9.0";
    sr2.status = StatusValue::Lost;
    sr2.date = "2025-10-10";
    sr2.id = StatusRecord::make_id(sr2.entity, sr2.branch, sr2.version, sr2.status, sr2.date);
    sr3.entity = feat.id;
    sr3.version = "1.0.0";
    sr3.status = StatusValue::Restored;
    sr3.date = "2025-12-01";
    sr3.id = StatusRecord::make_id(sr3.entity, sr3.branch, sr3.version, sr3.status, sr3.date);
    LOOM_REQUIRE_OK(ks->put_status_records(run, {sr1, sr2, sr3}));

    LOOM_REQUIRE_OK(ks->finish_run(run, "done", Json::object()));
  }
};

}  // namespace

TEST_SUITE("context_engine") {
  TEST_CASE("type_goal: forcing an unknown type is an error; a known forced type wins with confidence 1") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.goal_type = "not_a_real_goal_type";
    auto bad = engine.type_goal(req);
    CHECK(!bad);
    CHECK(bad.error().code == Errc::NotFound);

    req.goal_type = "implement_part";
    auto goal = unwrap(engine.type_goal(req));
    CHECK(goal.type == "implement_part");
    CHECK(goal.confidence == 1.0);
  }

  TEST_CASE("type_goal: bilingual cue classifier picks a sensible type without forcing") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);

    context::ContextRequest en;
    en.text = "please implement the checklist feature and fix the bug";
    auto g1 = unwrap(engine.type_goal(en));
    CHECK(g1.type == "implement_part");
    CHECK(g1.confidence > 0.3);

    context::ContextRequest pl;
    pl.text = "zaimplementuj checklisty i napraw ten błąd";
    auto g2 = unwrap(engine.type_goal(pl));
    CHECK(g2.type == "implement_part");

    context::ContextRequest verify;
    verify.text = "czy to prawda że checklisty zostały przywrócone? sprawdź to";
    auto g3 = unwrap(engine.type_goal(verify));
    CHECK(g3.type == "verify_claim");

    // No cues at all -> low-confidence fallback, never an error.
    context::ContextRequest empty;
    auto g4 = unwrap(engine.type_goal(empty));
    CHECK(g4.confidence < 0.5);
  }

  TEST_CASE("type_goal: an LLM classifier is used only when configured, and never touches the network otherwise") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    scripted->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "should not be called"));
    f.rt->set_http_transport(scripted);
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd random text matching no cue at all";
    auto goal = unwrap(engine.type_goal(req));
    CHECK(goal.params["classifier"] == "cue");
    CHECK(scripted->requests().empty());  // no api_key/semantic_model configured: never called

    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    scripted->expect(
        "POST", "https://openrouter.ai/api/v1",
        net::ScriptedTransport::Reply::json(
            200, Json{{"choices", Json::array({Json{{"message", Json{{"content", "{\"goal_type\": \"verify_claim\", \"confidence\": 0.9}"}}}}})}}));
    auto goal2 = unwrap(engine.type_goal(req));
    CHECK(goal2.type == "verify_claim");
    CHECK(goal2.params["classifier"] == "llm");
    CHECK(scripted->requests().size() == 1);
  }

  TEST_CASE("select: three bands, evidence markers, dependency closure, budget respected") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "zaimplementuj checklisty w NoteFlow";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    req.goal_type = "implement_part";
    req.budget_tokens = 2000;

    auto set = unwrap(engine.select(req));
    CHECK(set.goal.type == "implement_part");
    CHECK(set.used_tokens <= set.budget_tokens);
    CHECK(set.pack_hash == f.pack->hash());
    REQUIRE(!set.items.empty());

    bool have_stable_invariant = false, have_stable_pref = false, have_project_band = false, have_goal_band = false;
    bool have_dependency_pull = false;
    for (const auto& it : set.items) {
      if (it.ref == f.p_invariant.id) have_stable_invariant = true;
      if (it.ref == f.p_pref.id) have_stable_pref = true;
      if (it.band == ContextBand::Project) have_project_band = true;
      if (it.band == ContextBand::Goal) have_goal_band = true;
      if (it.ref == f.p_project.id && !it.required_by.empty()) have_dependency_pull = true;
      CHECK(!it.why.empty());
      CHECK(it.tokens > 0);
    }
    CHECK(have_stable_invariant);
    CHECK(have_stable_pref);
    CHECK(have_project_band);
    CHECK(have_goal_band);
    // pr.decide_fast_correct_later is a premise of c_impl and of the decision;
    // it should be pulled in even though it is not independently a top scorer.
    CHECK(have_dependency_pull);

    // Band order, then score desc, then ref (header contract).
    for (std::size_t i = 1; i < set.items.size(); ++i) {
      CHECK(set.items[i - 1].band <= set.items[i].band);
    }

    // Determinism (I5): selecting twice from the same run gives byte-identical JSON.
    auto set2 = unwrap(engine.select(req));
    CHECK(set.to_json().dump() == set2.to_json().dump());
  }

  TEST_CASE("select: a tiny budget drops items and records why") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "zaimplementuj checklisty";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    req.goal_type = "implement_part";
    req.budget_tokens = 5;  // far too small for everything gathered

    auto set = unwrap(engine.select(req));
    CHECK(set.used_tokens <= 5);
    CHECK(!set.dropped.empty());
    for (const auto& d : set.dropped) CHECK(!d.why.empty());
  }

  TEST_CASE("select: a premise the budget cannot hold is flagged, never silently dropped") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    int incomplete_seen = 0, complete_with_premise = 0;
    for (int budget = 4; budget <= 700; budget += 2) {
      context::ContextRequest req;
      req.text = "zaimplementuj checklisty w NoteFlow";
      req.targets = {f.feat.id};
      req.project = f.proj.id;
      req.goal_type = "implement_part";
      req.budget_tokens = budget;
      auto set = unwrap(engine.select(req));
      const model::ContextItem* impl = nullptr;
      bool premise_present = false;
      for (const auto& it : set.items) {
        if (it.ref == f.c_impl.id) impl = &it;
        if (it.ref == f.p_project.id) premise_present = true;
      }
      if (!impl) continue;
      if (premise_present) {
        CHECK(impl->missing_premises.empty());
        ++complete_with_premise;
      } else {
        INFO("budget=", budget);
        REQUIRE(impl->missing_premises.size() == 1);
        CHECK(impl->missing_premises[0] == f.p_project.id);
        auto text = unwrap(engine.render(set));
        CHECK(text.find("[INCOMPLETE") != std::string::npos);
        auto trace = engine.trace(set);
        CHECK(trace.dump().find("missing_premises") != std::string::npos);
        ++incomplete_seen;
      }
      // JSON round trip keeps the flag.
      auto rt_item = unwrap(model::ContextItem::from_json(impl->to_json()));
      CHECK(rt_item.missing_premises == impl->missing_premises);
    }
    CHECK(complete_with_premise > 0);
    // The sweep must actually exercise the incomplete path, otherwise this test proves nothing.
    CHECK(incomplete_seen > 0);
  }

  TEST_CASE("render + trace: sections-as-data match the rendered text, with evidence markers") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "zaimplementuj checklisty w NoteFlow";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    req.goal_type = "implement_part";
    req.budget_tokens = 2000;
    auto set = unwrap(engine.select(req));

    auto text = unwrap(engine.render(set));
    CHECK(!text.empty());
    CHECK(text.find("NoteFlow") != std::string::npos);

    Json tr = engine.trace(set);
    REQUIRE(tr["sections"].size() == 3);
    std::size_t total_items = 0;
    for (const auto& sec : tr["sections"]) total_items += sec["items"].size();
    CHECK(total_items == set.items.size());
    CHECK(tr["budget_tokens"] == set.budget_tokens);
    CHECK(tr["used_tokens"] == set.used_tokens);
  }

  TEST_CASE("build(): the integration hook returns goal, context_set and prompt together") {
    Fixture f;
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "implement the checklist feature";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    auto out = unwrap(engine.build(req));
    CHECK(out.contains("goal"));
    CHECK(out.contains("context_set"));
    CHECK(json::get_string(out, "prompt").size() > 0);
  }

  TEST_CASE("select: 'run' defaults to the latest done run; no finished run is not_found") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    auto pack = unwrap(rt->knowledge().pack());
    context::ContextEngine engine(*rt, rt->knowledge().store(), pack);
    context::ContextRequest req;
    req.text = "hello";
    CHECK(!engine.select(req));
  }

  TEST_CASE("estimate_tokens: code points / 4, at least 1 for non-empty text") {
    CHECK(context::ContextEngine::estimate_tokens("") == 0);
    CHECK(context::ContextEngine::estimate_tokens("abc") == 1);
    CHECK(context::ContextEngine::estimate_tokens(std::string(400, 'x')) == 100);
  }
}
