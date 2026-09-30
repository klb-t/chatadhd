// context_engine.h: goal typing (deterministic PL/EN cue classifier + an
// optional, explicitly budgeted native LLM fallback), the ContextSet builder (score,
// diversity, dependency closure, three-band token budget) and the renderer.
#include <doctest/doctest.h>

#include <algorithm>

#include "loom/context_engine.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
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

  TEST_CASE("type_goal: model classification needs separate native authorization and explicit limits") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    scripted->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "should not be called"));
    f.rt->set_http_transport(scripted);
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd random text matching no cue at all";
    auto goal = unwrap(engine.type_goal(req));
    CHECK(goal.params["classifier"] == "cue");
    CHECK(scripted->requests().empty());  // default typing is always offline

    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    auto offline = unwrap(engine.type_goal(req));
    CHECK(offline.params["classifier"] == "cue");
    CHECK(scripted->requests().empty());
    scripted->expect(
        "POST", "https://openrouter.ai/api/v1",
        net::ScriptedTransport::Reply::json(
            200, Json{{"choices", Json::array({Json{{"message", Json{{"content", "{\"goal_type\": \"verify_claim\", \"confidence\": 0.9}"}}}}})}}));
    context::GoalTypingBudget budget{1, 10000, 32, 2000, 4096};
    auto goal2 = unwrap(engine.type_goal_with_model(req, budget));
    CHECK(goal2.type == "verify_claim");
    CHECK(goal2.params["classifier"] == "llm");
    CHECK(goal2.params["confidence_basis"] == "provider_self_report");
    CHECK(goal2.params["reported_confidence"] == 0.9);
    CHECK(goal2.params["calibration_status"] == "unavailable");
    CHECK(goal2.params["external_goal_typing"]["status"] == "accepted");
    CHECK(goal2.params["external_goal_typing"]["requests"] == 1);
    CHECK(goal2.params["external_goal_typing"]["response"]["status"] == 200);
    const auto& response = goal2.params["external_goal_typing"]["response"];
    CHECK(response["complete"] == true);
    auto raw = unwrap(f.rt->blobs().read(response["blob_hash"].get<std::string>()));
    CHECK(raw.find("verify_claim") != std::string::npos);
    auto source = unwrap(f.rt->provenance().get_source(response["source_id"].get<std::string>()));
    REQUIRE(source.has_value());
    CHECK(source->blob_hash == response["blob_hash"].get<std::string>());
    REQUIRE(scripted->requests().size() == 1);
    const auto sent = scripted->requests().front();
    CHECK(sent.timeout_ms == 2000);
    CHECK(unwrap(json::parse(sent.body))["max_tokens"] == 32);
    CHECK(goal2.params.dump().find("sk-test") == std::string::npos);
  }

  TEST_CASE("native goal typing: exhausted and invalid budgets never call the transport") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    f.rt->set_http_transport(scripted);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd";
    auto disabled = unwrap(engine.type_goal_with_model(req, {}));
    CHECK(disabled.params["classifier"] == "cue");
    CHECK(disabled.params["external_goal_typing"]["status"] == "budget_disabled");
    for (const auto& invalid : std::vector<context::GoalTypingBudget>{
             {2, 10000, 32, 2000, 4096}, {-1, 10000, 32, 2000, 4096}, {1, 0, 32, 2000, 4096},
             {1, 10000, 0, 2000, 4096}, {1, 10000, 32, 0, 4096}, {1, 256001, 32, 2000, 4096},
             {1, 10000, 4097, 2000, 4096}, {1, 10000, 32, 60001, 4096},
             {1, 10000, 32, 2000, 0}, {1, 10000, 32, 2000, 256001}}) {
      auto result = engine.type_goal_with_model(req, invalid);
      REQUIRE(!result);
      CHECK(result.error().code == Errc::InvalidArgument);
    }
    auto input_limited = unwrap(engine.type_goal_with_model(req, {1, 1, 32, 2000, 4096}));
    CHECK(input_limited.params["classifier"] == "cue");
    CHECK(input_limited.params["external_goal_typing"]["status"] == "input_limit");
    req.goal_type = "implement_part";
    CHECK(unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, 4096})).params["classifier"] == "forced");
    req.goal_type.reset();
    f.rt->config().set("semantic_model", "");
    const auto unavailable = unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, 4096}));
    CHECK(unavailable.params["external_goal_typing"]["status"] == "unavailable");
    CHECK(scripted->requests().empty());
  }

  TEST_CASE("native goal typing: failed instrument keeps cue result and records the first response") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    const std::string echoed = "rate limit (credential echoed: sk-test)";
    scripted->set_fallback(net::ScriptedTransport::Reply::text(429, echoed));
    f.rt->set_http_transport(scripted);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd";
    const auto baseline = unwrap(engine.type_goal(req));
    const auto result = unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, 4096}));
    CHECK(result.type == baseline.type);
    CHECK(result.confidence == baseline.confidence);
    CHECK(result.params["classifier"] == "cue");
    CHECK(result.params["external_goal_typing"]["status"] == "failed");
    const auto& response = result.params["external_goal_typing"]["response"];
    CHECK(unwrap(f.rt->blobs().read(response["blob_hash"].get<std::string>())) == echoed);
    CHECK(result.params.dump().find("sk-test") == std::string::npos);
    CHECK(scripted->requests().size() == 1);
  }

  TEST_CASE("native goal typing: response overflow preserves a partial source and never promotes it") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    const std::string payload = "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\",\\\"confidence\\\":1}\"}}]}";
    scripted->set_fallback(net::ScriptedTransport::Reply::text(200, payload));
    f.rt->set_http_transport(scripted);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd";
    const auto baseline = unwrap(engine.type_goal(req));
    const auto result = unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, 8}));
    CHECK(result.type == baseline.type);
    CHECK(result.params["classifier"] == "cue");
    CHECK(result.params["external_goal_typing"]["status"] == "response_limit");
    const auto& response = result.params["external_goal_typing"]["response"];
    CHECK(response["complete"] == false);
    CHECK(response["bytes"] == 8);
    CHECK(response["status"] == 200);
    CHECK(unwrap(f.rt->blobs().read(response["blob_hash"].get<std::string>())) == payload.substr(0, 8));
    CHECK(scripted->requests().size() == 1);
  }

  TEST_CASE("native goal typing: invalid first responses remain raw evidence without retry") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    f.rt->set_http_transport(scripted);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "asdkjhasd";
    const auto baseline = unwrap(engine.type_goal(req));
    std::size_t calls = 0;
    for (const auto& body : std::vector<std::string>{
             "not JSON", "{\"choices\":[]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"unknown_type\\\",\\\"confidence\\\":1}\"}}]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\"}\"}}]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\",\\\"confidence\\\":\\\"high\\\"}\"}}]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\",\\\"confidence\\\":true}\"}}]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\",\\\"confidence\\\":-0.1}\"}}]}",
             "{\"choices\":[{\"message\":{\"content\":\"{\\\"goal_type\\\":\\\"verify_claim\\\",\\\"confidence\\\":1.1}\"}}]}"}) {
      scripted->set_fallback(net::ScriptedTransport::Reply::text(200, body));
      const auto result = unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, body.size()}));
      CHECK(result.type == baseline.type);
      CHECK(result.params["classifier"] == "cue");
      CHECK(result.confidence == baseline.confidence);
      CHECK(result.params["confidence_basis"] == "heuristic_cue_margin");
      CHECK(!result.params.contains("reported_confidence"));
      CHECK(result.params["external_goal_typing"]["status"] == "failed");
      CHECK(!result.params["external_goal_typing"].contains("reported_confidence"));
      const auto& response = result.params["external_goal_typing"]["response"];
      CHECK(response["status"] == 200);
      CHECK(response["complete"] == true);  // byte boundary is inclusive, malformed != partial
      CHECK(unwrap(f.rt->blobs().read(response["blob_hash"].get<std::string>())) == body);
      CHECK(scripted->requests().size() == ++calls);
    }
  }

  TEST_CASE("native goal typing: persistence failure preserves spent attempt and never asks caller to retry implicitly") {
    for (bool break_blob_storage : {false, true}) {
      Fixture f;
      auto scripted = std::make_shared<net::ScriptedTransport>();
      const auto reply = net::ScriptedTransport::Reply::json(
          200, Json{{"choices", Json::array({Json{{"message", Json{{"content", "{\"goal_type\":\"verify_claim\",\"confidence\":0.9}"}}}}})}});
      scripted->set_fallback(reply);
      f.rt->set_http_transport(scripted);
      f.rt->config().set("semantic_model", "test-model");
      f.rt->secrets().set("api_key", "sk-test");
      if (break_blob_storage) {
        const auto root = f.rt->blobs().root();
        std::filesystem::remove_all(root);
        LOOM_REQUIRE_OK(fsutil::write_file(root, "not a directory"));
      } else {
        LOOM_REQUIRE_OK(f.rt->db().conn().run("DROP TABLE loom_sources"));
      }
      context::ContextEngine engine(*f.rt, *f.ks, f.pack);
      context::ContextRequest req;
      req.text = "asdkjhasd";
      const auto baseline = unwrap(engine.type_goal(req));
      const auto result = unwrap(engine.type_goal_with_model(req, {1, 10000, 32, 2000, 4096}));
      CHECK(result.type == baseline.type);
      CHECK(result.params["classifier"] == "cue");
      CHECK(result.params["confidence_basis"] == "heuristic_cue_margin");
      const auto& attempt = result.params["external_goal_typing"];
      CHECK(attempt["status"] == "storage_failure");
      CHECK(attempt["requests"] == 1);
      CHECK(attempt["retry_authorized"] == false);
      CHECK(attempt["response"]["source_status"] == "unavailable");
      if (!break_blob_storage) {
        const auto raw = unwrap(f.rt->blobs().read(attempt["response"]["blob_hash"].get<std::string>()));
        CHECK(raw == reply.chunks.front());
      }
      CHECK(scripted->requests().size() == 1);
    }
  }

  TEST_CASE("preview: configured models and credentials never authorize a transport request") {
    Fixture f;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    scripted->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "preview must be offline"));
    f.rt->set_http_transport(scripted);
    f.rt->config().set("semantic_model", "test-model");
    f.rt->secrets().set("api_key", "sk-test");
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    auto req = unwrap(context::ContextRequest::from_json(
        Json{{"text", "asdkjhasd"}, {"targets", Json::array({f.feat.id})}, {"run", f.run},
             {"llm", "auto"}, {"max_requests", 1}}));  // preview JSON cannot authorize the native instrument
    auto selected = unwrap(engine.select(req));
    CHECK(selected.goal.params["classifier"] == "cue");
    CHECK(selected.goal.params["external_goal_typing"]["status"] == "offline");
    CHECK(scripted->requests().empty());
    auto built = unwrap(engine.build(req));
    CHECK(built["goal"]["params"]["classifier"] == "cue");
    CHECK(built["goal"]["params"]["external_goal_typing"]["status"] == "offline");
    CHECK(scripted->requests().empty());
  }

  TEST_CASE("select: dependency closure reaches long claim and principle chains") {
    Fixture f;
    std::vector<Claim> chain;
    for (int i = 0; i < 6; ++i) {
      chain.push_back(claim(i == 0 ? f.feat.id : "isolated_" + std::to_string(i),
                            "depends_on", "hidden_" + std::to_string(i),
                            EvidenceClass::Observed, 0.9, "2025-05-10"));
    }
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) chain[i].assessment.premises.claims = {chain[i + 1].id};

    std::vector<Principle> principles;
    for (int i = 0; i < 5; ++i) {
      auto p = project_principle("p.hidden_chain_" + std::to_string(i));
      p.level = PrincipleLevel::Value;  // not gathered by implement_part's strategy/epistemic levels
      principles.push_back(std::move(p));
    }
    for (std::size_t i = 0; i + 1 < principles.size(); ++i) principles[i].derived_from = {principles[i + 1].id};
    chain.back().assessment.premises.principles = {principles.front().id};
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, chain));
    LOOM_REQUIRE_OK(f.ks->put_principles(f.run, principles));
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.text = "implement dependency chain";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    req.goal_type = "implement_part";
    req.budget_tokens = 10000;
    auto set = unwrap(engine.select(req));
    auto included = [&](const std::string& ref) {
      return std::find_if(set.items.begin(), set.items.end(), [&](const auto& item) { return item.ref == ref; });
    };
    for (const auto& c : chain) {
      INFO("claim ", c.id);
      CHECK(included(c.id) != set.items.end());
    }
    for (const auto& p : principles) {
      INFO("principle ", p.id);
      CHECK(included(p.id) != set.items.end());
    }
    for (const auto& item : set.items) CHECK(item.missing_premises.empty());
    CHECK(set.used_tokens <= set.budget_tokens);
  }

  TEST_CASE("select: cyclic dependencies terminate and unresolved references stay visible") {
    Fixture f;
    auto root = claim(f.feat.id, "has_cycle", "cycle_root", EvidenceClass::Observed, 0.9, "2025-05-10");
    auto child = claim("isolated_cycle", "requires", "cycle_child", EvidenceClass::Observed, 0.9, "2025-05-10");
    root.assessment.premises.claims = {child.id};
    child.assessment.premises.claims = {root.id, "cl_missing_cycle_premise"};
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {root, child}));
    context::ContextEngine engine(*f.rt, *f.ks, f.pack);
    context::ContextRequest req;
    req.goal_type = "implement_part";
    req.targets = {f.feat.id};
    req.project = f.proj.id;
    req.budget_tokens = 10000;
    auto selected = unwrap(engine.select(req));
    int roots = 0, children = 0;
    for (const auto& item : selected.items) {
      if (item.ref == root.id) {
        ++roots;
        CHECK(item.missing_premises.empty());
      }
      if (item.ref == child.id) {
        ++children;
        CHECK(item.missing_premises == std::vector<std::string>{"cl_missing_cycle_premise"});
        CHECK(item.required_by == std::vector<std::string>{root.id});
      }
    }
    CHECK(roots == 1);
    CHECK(children == 1);
    CHECK(unwrap(engine.render(selected)).find("cl_missing_cycle_premise") != std::string::npos);
  }

  TEST_CASE("select: three bands, evidence markers, dependency closure, budget respected") {
    Fixture f;
    // A distinct goal claim keeps this a three-band test. The decision and its
    // underlying c_impl claim deliberately share one reference and must not be
    // counted twice merely to populate an additional band.
    const auto direct_goal = claim(f.feat.id, "has_detail", "goal_specific_detail", EvidenceClass::Observed, 0.9, "2025-05-10");
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {direct_goal}));
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
    CHECK(std::count_if(set.items.begin(), set.items.end(), [&](const auto& it) { return it.ref == f.c_impl.id; }) == 1);
    CHECK(std::count_if(set.items.begin(), set.items.end(), [&](const auto& it) {
      return it.ref == direct_goal.id && it.band == ContextBand::Goal;
    }) == 1);
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
    // Keep this a closure-budget test after shared-reference deduplication and
    // forward carry made the old short, independently selected premise always
    // fit alongside its conclusion. A non-core Value principle is outside
    // implement_part's Strategy/Epistemic policy, but remains mandatory through
    // c_impl's premise reference. Its larger text exercises both budget paths.
    f.p_project.level = PrincipleLevel::Value;
    f.p_project.statement = {{"en", "Required supporting rationale: " + std::string(720, 'p')}};
    LOOM_REQUIRE_OK(f.ks->put_principles(f.run, {f.p_project}));
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
