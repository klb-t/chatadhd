// generalize.h: unit tests per mechanism on small hand-built evidence —
// Expected-Property predicates and re-check, paradigm matching (anchors,
// facets, absent slots with fill queries, conflicts kept), analogies,
// stratified rules (derived / inferred / extrapolated), transfer through a
// morphism (depth 1, model_knowledge below the owner's sources), principle
// discovery + typing with the prior filter, operator mining, predictions
// and their holdout evaluation, competing models, and the stage end to end
// through KnowledgeEngine + KnowledgeStore (determinism).
#include <doctest/doctest.h>

#include "loom/generalize.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"
#include "loom/runtime.h"
#include "test_generalize_fixture.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::generalize;
using loom::test::unwrap;
using model::CheckState;
using model::Claim;
using model::EvidenceClass;

namespace {

std::shared_ptr<const kb::Pack> the_pack() {
  static auto p = unwrap(kb::Pack::load_builtin());
  return p;
}

struct Builder {
  Evidence ev;
  int n = 0;

  std::string obs(const std::string& unit, const std::string& text, const std::string& date, const std::string& speaker = "user") {
    model::Observation o;
    o.unit = unit;
    o.text = text;
    o.date = date;
    o.speaker = speaker;
    o.ordinal = n++;
    o.kind = model::ObservationKind::Utterance;
    o.artifact_type = "conversation";
    o.locator.source = "sha256:test";
    o.locator.json_pointer = "/" + std::to_string(o.ordinal);
    o.id = model::Observation::make_id(unit, o.locator, text);
    ev.observations.push_back(o);
    return o.id;
  }
  std::string entity(const std::string& kind, const std::string& key, const std::string& label = "") {
    model::Entity e;
    e.kind = kind;
    e.canonical_key = key;
    e.label = label.empty() ? key : label;
    e.id = model::Entity::make_id(kind, key);
    for (const auto& x : ev.entities) {
      if (x.id == e.id) return e.id;
    }
    ev.entities.push_back(e);
    return e.id;
  }
  Claim& claim(const std::string& s, const std::string& p, const std::string& object, const Json& value, const std::string& ob,
               double conf = 0.8) {
    Claim c;
    c.subject = s;
    c.predicate = p;
    c.object = object;
    c.value = value;
    c.assessment.support.push_back(model::Support{ob, {}, "", "test@1", 1.0});
    c.assessment.confidence = conf;
    c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
    ev.claims.push_back(c);
    return ev.claims.back();
  }
  // subject -rel-> new entity of `kind`
  void link(const std::string& s, const std::string& rel, const std::string& kind, const std::string& key, const std::string& ob) {
    claim(s, rel, entity(kind, key), Json(), ob);
  }
};

// A film and a music project that fill the same roles, the film with a
// continuity check, the music project without a mix check.
Builder film_and_music() {
  Builder b;
  std::string o1 = b.obs("u1", "film: scena 1 i scena 2, bohater Ada", "2026-01-01");
  std::string o2 = b.obs("u2", "utwor: zwrotka, refren, wokal", "2026-01-02");
  std::string film = b.entity("project", "reel", "Reel");
  std::string music = b.entity("project", "ep", "EP");
  b.claim(film, "instance_of", "", "film", o1);
  b.claim(music, "instance_of", "", "music", o2);
  b.claim(film, "mentioned_in", "", "u1", o1);
  b.claim(music, "mentioned_in", "", "u2", o2);
  b.link(film, "has_scene", "scene", "scene 1", o1);
  b.link(film, "has_scene", "scene", "scene 2", o1);
  b.link(film, "has_character", "character", "ada", o1);
  b.link(film, "has_check", "continuity_check", "continuity pass", o1);
  b.link(music, "has_section", "section", "verse", o2);
  b.link(music, "has_section", "section", "chorus", o2);
  b.link(music, "has_performer", "performer", "vocal", o2);
  return b;
}

const Match* find_match(const std::vector<Match>& ms, const std::string& paradigm) {
  for (const auto& m : ms) {
    if (m.instance.paradigm == paradigm) return &m;
  }
  return nullptr;
}

}  // namespace

TEST_SUITE("generalize") {
  TEST_CASE("expected property: bound references re-check against new evidence (pending -> holds / violated)") {
    Builder b;
    std::string o = b.obs("u", "x", "2026-01-01");
    std::string p = b.entity("project", "p");
    Claim target;
    target.subject = p;
    target.predicate = "stores_in";
    target.value = "SQLite";
    target.id = "cl_target";
    kb::ExpectedProperty ep;
    ep.expr = Json{{"op", "nonempty"}, {"args", Json::array({Json{{"ref", "claims"}, {"subject", p}, {"predicate", "has_test"}}})}};
    CHECK(unwrap(evaluate_property(ep, target, b.ev)) == CheckState::Pending);
    b.link(p, "has_test", "test", "unit tests", o);
    CHECK(unwrap(evaluate_property(ep, target, b.ev)) == CheckState::Holds);

    kb::ExpectedProperty cls;
    cls.expr = Json{{"op", "in_class"}, {"args", Json::array({"storage.embedded", Json{{"members", Json::array({"sqlite", "leveldb"})}}})}};
    CHECK(unwrap(evaluate_property(cls, target, b.ev)) == CheckState::Holds);
    target.value = "Postgres";
    CHECK(unwrap(evaluate_property(cls, target, b.ev)) == CheckState::Violated);

    kb::ExpectedProperty ver;
    ver.expr = Json{{"op", "version_gte"}, {"args", Json::array({"$value", Json{{"members", Json::array({"0.7.10"})}}})}};
    target.value = "0.9.0";
    CHECK(unwrap(evaluate_property(ver, target, b.ev)) == CheckState::Holds);
    target.value = "0.7.9";
    CHECK(unwrap(evaluate_property(ver, target, b.ev)) == CheckState::Violated);

    // not_contradicted_by: an observed single-valued statement with another value refutes.
    kb::ExpectedProperty nc;
    nc.expr = Json{{"op", "not_contradicted_by"}, {"args", Json::array({"written_in"})}};
    target.value = "C++";
    CHECK(unwrap(evaluate_property(nc, target, b.ev)) == CheckState::Pending);
    b.claim(p, "written_in", "", "Rust", o);
    CHECK(unwrap(evaluate_property(nc, target, b.ev)) == CheckState::Violated);
    // Unknown predicates stay pending (capability honesty).
    kb::ExpectedProperty unk;
    unk.expr = Json{{"op", "available_on"}, {"args", Json::array({"android"})}};
    CHECK(unwrap(evaluate_property(unk, target, b.ev)) == CheckState::Pending);
  }

  TEST_CASE("matching: anchored instances, facet, absent slots with fill queries, conflicts kept") {
    auto b = film_and_music();
    std::string o = b.ev.observations.back().id;
    std::string music = model::Entity::make_id("project", "ep");
    // Two equally supported keys for a card-one slot: a conflict, both kept.
    b.link(music, "in_key", "musical_key", "c minor", o);
    b.link(music, "in_key", "musical_key", "d minor", o);
    ParadigmMatcher mt(the_pack());
    auto ms = unwrap(mt.match_projects(b.ev));
    auto* film = find_match(ms, "film");
    auto* mu = find_match(ms, "music");
    REQUIRE(film);
    REQUIRE(mu);
    CHECK(film->instance.subject == model::Entity::make_id("project", "reel"));
    CHECK(film->score > 0.4);
    int scenes = 0, conflicts = 0, absent = 0;
    for (const auto& sv : film->instance.slots) scenes += sv.slot == "scene";
    CHECK(scenes == 2);
    for (const auto& sv : mu->instance.slots) conflicts += sv.slot == "key" && sv.conflict;
    CHECK(conflicts == 2);
    for (const auto& c : mu->claims) {
      if (!c.is_absent()) continue;
      ++absent;
      CHECK(c.validate());
      CHECK(!c.assessment.open.slots.empty());
      CHECK(c.assessment.open.fill_query.is_object());
    }
    CHECK(absent > 5);  // e.g. mix_check, tempo, score ... never a silent gap
    CHECK(mu->instance.coverage["absent"].get<int>() == absent);
    // Deterministic.
    auto ms2 = unwrap(mt.match_projects(b.ev));
    REQUIRE(ms2.size() == ms.size());
    for (std::size_t i = 0; i < ms.size(); ++i) CHECK(ms[i].to_json() == ms2[i].to_json());
  }

  TEST_CASE("analogy + transfer: depth 1 through a morphism, model_knowledge below the owner's sources") {
    auto b = film_and_music();
    ParadigmMatcher mt(the_pack());
    auto ms = unwrap(mt.match_projects(b.ev));
    auto an = unwrap(mt.analogies(ms));
    REQUIRE(an.size() == 1);
    CHECK(an[0].predicate == "analogous_to");
    CHECK(an[0].assessment.evidence == EvidenceClass::Inferred);
    CHECK(an[0].validate());
    auto tr = unwrap(transfer(*the_pack(), ms));
    const Claim* mix = nullptr;
    for (const auto& c : tr) {
      if (c.subject == model::Entity::make_id("project", "ep") && json::get_string(c.qualifiers.extra, "slot") == "mix_check") mix = &c;
    }
    REQUIRE(mix);
    CHECK(mix->validate());
    CHECK(mix->assessment.evidence == EvidenceClass::Inferred);
    CHECK(mix->assessment.origin == model::Origin::ModelKnowledge);
    CHECK(mix->assessment.derivation->morphism == "m.check.film_music");
    CHECK(mix->assessment.derivation->depth == 1);
    CHECK(mix->assessment.confidence < 0.55);
    CHECK(mix->assessment.check == CheckState::Pending);
    REQUIRE(mix->assessment.premises.claims.size() == 1);
    // The premise is the film's OBSERVED continuity check, never a transfer.
    bool premise_observed = false;
    for (const auto& c : b.ev.claims) premise_observed |= c.id == mix->assessment.premises.claims[0] && c.assessment.evidence == EvidenceClass::Observed;
    CHECK(premise_observed);
    // Nothing transfers into a slot the owner's sources already fill.
    for (const auto& c : tr) CHECK(json::get_string(c.qualifiers.extra, "slot") != "scene");
    // The transferred expectation is re-checkable: once a mix check is observed, it holds.
    std::string o = b.ev.observations.back().id;
    b.link(model::Entity::make_id("project", "ep"), "has_check", "mix_check", "mono check", o);
    CHECK(unwrap(evaluate_property(*mix->assessment.expected, *mix, b.ev)) == CheckState::Holds);
  }

  TEST_CASE("rules: stratum 0 derived, stratum 2 extrapolated (capped, never premises)") {
    Builder b;
    std::string o = b.obs("u", "wersja 0.9.0 i 0.7.10, moduł sync, moduł core", "2026-03-01");
    std::string p = b.entity("project", "app");
    b.claim(p, "instance_of", "", "software_app", o);
    b.link(p, "has_component", "component", "sync", o);
    b.link(p, "has_component", "component", "core", o);
    b.claim(p, "has_version", "", "0.7.10", o);
    b.claim(p, "has_version", "", "0.9.0", o);
    b.claim(p, "has_stage", b.entity("stage", "render"), Json(), o);
    b.claim(p, "has_stage", b.entity("stage", "compose"), Json(), o);
    ParadigmMatcher mt(the_pack());
    auto ms = unwrap(mt.match_projects(b.ev));
    auto* sw = find_match(ms, "software_app");
    REQUIRE(sw);
    CHECK(std::find(sw->instance.facets.begin(), sw->instance.facets.end(), "staged_transformation") != sw->instance.facets.end());
    auto inf = unwrap(infer(*the_pack(), b.ev, ms));
    const Claim* cur = nullptr;
    for (const auto& c : inf) {
      CHECK(c.validate());
      if (c.assessment.derivation && c.assessment.derivation->op == "r.current_version") cur = &c;
    }
    REQUIRE(cur);
    CHECK(cur->value == "0.9.0");
    CHECK(cur->assessment.evidence == EvidenceClass::Derived);
    auto ex = unwrap(extrapolate(*the_pack(), b.ev, ms));
    bool checkpoint = false;
    double cap = 0.5;
    for (const auto& c : ex) {
      CHECK(c.assessment.evidence == EvidenceClass::Extrapolated);
      CHECK(c.assessment.confidence <= cap + 1e-9);
      checkpoint |= c.assessment.derivation->op == "x.pipeline_resumable_stages";
    }
    CHECK(checkpoint);
    // The store refuses an extrapolation as a premise (I3).
    fsutil::TempDir td;
    auto db = test::open_db(td.path() / "k.db");
    kb::KnowledgeStore ks(*db);
    auto run = unwrap(ks.begin_run("h", Json::object()));
    LOOM_REQUIRE_OK(ks.put_observations(run.id, b.ev.observations));
    LOOM_REQUIRE_OK(ks.put_claims(run.id, b.ev.claims));
    LOOM_REQUIRE_OK(ks.put_claims(run.id, ex));
    Claim bad = inf.front();
    bad.assessment.premises.claims = {ex.front().id};
    bad.qualifiers.extra["probe"] = true;
    bad.id = Claim::make_id(bad.subject, bad.predicate, bad.object, bad.value, bad.qualifiers);
    CHECK(!ks.put_claims(run.id, {bad}));
  }

  TEST_CASE("principles: clustered phrasings, typing, prior filter (seed found / seed_only / invisible)") {
    Builder b;
    b.obs("u1", "nigdy nie kasuję porzuconej gałęzi, zostawiam ją z opisem czemu odpadła", "2025-01-10");
    b.obs("u2", "znowu: nie kasuję porzuconej gałęzi, zostawiam opis czemu odpadła", "2025-06-10");
    b.obs("u3", "kiedy dwie wartości się kłócą, wygrywa ta opcja, którą łatwiej cofnąć", "2025-03-01");
    b.obs("u4", "ok, a pogoda jutro?", "2025-03-02");
    b.obs("u4", "Zawsze trzymaj alternatywy — never delete them", "2025-03-03", "assistant");
    auto pk = the_pack();
    auto rep = unwrap(discover_principles(*pk, b.ev, model::PriorFilter::none()));
    CHECK(rep.seed_status.empty());
    const model::Principle* branch = nullptr;
    const model::Principle* conflict = nullptr;
    for (const auto& p : rep.principles) {
      for (const auto& ph : p.phrasings) {
        if (ph.find("gałęzi") != std::string::npos) branch = &p;
        if (ph.find("kłócą") != std::string::npos) conflict = &p;
        CHECK(ph.find("never delete them") == std::string::npos);  // the assistant is not the owner
      }
    }
    REQUIRE(branch);
    CHECK(branch->phrasings.size() == 2);
    CHECK(branch->validation == model::ValidationStatus::Supported);  // 2 units, 2 dates
    CHECK(branch->owner == "user");
    CHECK(branch->id.rfind("p_", 0) == 0);
    CHECK(branch->form == model::PrincipleForm::Invariant);
    CHECK(branch->level == model::PrincipleLevel::Epistemic);
    REQUIRE(conflict);
    CHECK(conflict->form == model::PrincipleForm::ConflictResolution);
    CHECK(conflict->validation == model::ValidationStatus::Candidate);
    // Seeds: visible only when their earliest source is before the cut.
    auto early = unwrap(discover_principles(*pk, b.ev, model::PriorFilter::as_of_date("2026-01-01")));
    CHECK(early.seed_status.empty());
    auto late = unwrap(discover_principles(*pk, b.ev, model::PriorFilter::as_of_date("2026-12-31")));
    CHECK(late.seed_status.size() == unwrap(model::principles(*pk)).size());
    // Deterministic.
    CHECK(unwrap(discover_principles(*pk, b.ev, model::PriorFilter::none())).to_json() == rep.to_json());
  }

  TEST_CASE("operators: recurring decisions merge; predictions at T hold / stay unattributed after T; models") {
    Builder b;
    std::string p1 = b.entity("project", "notes");
    std::string p2 = b.entity("project", "reels");
    auto decide = [&](const std::string& subj, const std::string& unit, const std::string& before, const std::string& text,
                      std::vector<std::string> alts, const std::string& chosen, const std::string& date) {
      b.obs(unit, before, date);
      std::string o = b.obs(unit, text, date);
      auto& c = b.claim(subj, "decides", "", chosen, o);
      model::Decision d;
      d.id = c.id;
      d.subject = subj;
      d.date = date;
      for (const auto& a : alts) d.alternatives.push_back(model::DecisionAlternative{a, "", Json(), {}, a == chosen});
      b.ev.decisions.push_back(d);
      return d.id;
    };
    auto d1 = decide(p1, "a", "rachunek za chmure bo auto-upload bez pytania", "dodaje bramke: pokazuje estymowany koszt i czekam na potwierdzenie",
                     {"keep uploading", "gate uploads behind a cost estimate"}, "gate uploads behind a cost estimate", "2025-10-10");
    auto d2 = decide(p1, "b", "czy sqlite czy realm na notatki", "SQLite dla danych strukturalnych",
                     {"SQLite", "Realm"}, "SQLite", "2025-01-10");
    auto d3 = decide(p2, "c", "generowanie klipow bedzie drogie", "klipy dostaja bramke: estymowany koszt i jawne potwierdzenie",
                     {"render freely", "gate clips behind a cost estimate"}, "gate clips behind a cost estimate", "2026-06-10");
    auto d4 = decide(p2, "d", "font do napisow", "biore Inter, bo czytelny", {"Inter", "Roboto"}, "Inter", "2026-06-20");
    auto pk = the_pack();
    // Full corpus: the two cost gates recur -> one supported operator.
    auto ops = unwrap(mine_operators(*pk, b.ev, model::PriorFilter::none()));
    const model::Operator* gate = nullptr;
    for (const auto& o : ops) {
      CHECK(o.validate());
      for (const auto& e : o.examples) {
        if (e.claim == d1) gate = &o;
      }
    }
    REQUIRE(gate);
    CHECK(gate->examples.size() == 2);
    CHECK(gate->success == 2);
    CHECK(gate->validation == model::ValidationStatus::Supported);
    // Holdout: mine <= T, predict, evaluate after T.
    Evidence pre, post = b.ev;
    pre.entities = b.ev.entities;
    for (const auto& o : b.ev.observations) {
      if (o.date <= "2026-05-01") pre.observations.push_back(o);
    }
    for (const auto& c : b.ev.claims) pre.claims.push_back(c);
    for (const auto& d : b.ev.decisions) {
      if (d.date <= "2026-05-01") pre.decisions.push_back(d);
    }
    auto pre_ops = unwrap(mine_operators(*pk, pre, model::PriorFilter::none()));
    CHECK(pre_ops.size() == 2);
    auto preds = unwrap(predict(pre_ops, pre, "2026-05-01"));
    CHECK(preds.size() == 2);
    for (const auto& p : preds) CHECK(p.outcome == CheckState::Pending);
    preds = unwrap(evaluate_predictions(preds, post));
    int holds = 0;
    for (const auto& p : preds) {
      if (p.outcome != CheckState::Holds) continue;
      ++holds;
      CHECK(p.evaluated_against == std::vector<std::string>{d3});
      CHECK(p.cut == "2026-05-01");
    }
    CHECK(holds == 1);
    for (const auto& p : preds) {
      for (const auto& e : p.evaluated_against) CHECK(e != d4);  // an unrelated decision is not attributed
    }
    (void)d2;
    // Competing models: kept side by side and scored.
    auto rep = unwrap(discover_principles(*pk, b.ev, model::PriorFilter::none()));
    auto models = unwrap(build_models(b.ev, rep.principles));
    for (const auto& m : models) {
      CHECK(m.explanatory >= 0.0);
      CHECK(m.explanatory <= 1.0);
    }
  }

  TEST_CASE("stage: knowledge.generalize through the engine writes the store deterministically (synthetic_dev)") {
    auto pk = the_pack();
    auto fx = test::gfix::load(*pk);
    std::string outputs[2];
    for (int i = 0; i < 2; ++i) {
      fsutil::TempDir td;
      RuntimeOptions o;
      o.data_dir = td.path().string();
      o.start_workers = false;
      auto rt = unwrap(Runtime::open(o));
      auto& ke = rt->knowledge();
      auto noop = [](const char* name) {
        return [name](knowledge::StageContext& c) -> Result<Json> {
          (void)c;
          return Json{{"output", std::string(name)}, {"stats", Json::object()}};
        };
      };
      ke.set_stage("catalog", noop("catalog"));
      ke.set_stage("extract", [&fx](knowledge::StageContext& c) -> Result<Json> {
        LOOM_TRY(c.store.put_entities(c.run, fx.ev.entities));
        LOOM_TRY(c.store.put_observations(c.run, fx.ev.observations));
        LOOM_TRY(c.store.put_claims(c.run, fx.ev.claims));
        LOOM_TRY(c.store.put_decisions(c.run, fx.ev.decisions));
        return Json{{"output", "fixture"}, {"stats", Json::object()}};
      });
      ke.set_stage("resolve", noop("resolve"));
      ke.set_stage("assess", noop("assess"));
      ke.set_stage("materialize", noop("materialize"));
      knowledge::KnowledgeConfig cfg;
      cfg.prior_cut = "2026-05-01";
      auto r = unwrap(ke.run(cfg));
      INFO(r.error);
      REQUIRE(r.status == "done");
      const knowledge::StageRun* g = nullptr;
      for (const auto& s : r.stages) {
        if (s.stage == "generalize") g = &s;
      }
      REQUIRE(g);
      outputs[i] = g->output_hash;
      CHECK(g->stats["instances"].get<int>() >= 5);
      CHECK(g->stats["principles"].get<int>() > 0);
      CHECK(g->stats["operators"].get<int>() > 0);
      CHECK(g->stats["predictions_holds"].get<int>() >= 3);
      auto& ks = ke.store();
      auto preds = unwrap(ks.list_predictions(r.run));
      CHECK(preds.size() == static_cast<std::size_t>(g->stats["predictions"].get<int>()));
      auto inst = unwrap(ks.query_instances(r.run, "music"));
      CHECK(inst.size() == 1);
      // Every stored claim of the stage validates; nothing inferred is observed.
      kb::ClaimQuery q;
      q.evidence = EvidenceClass::Inferred;
      for (const auto& c : unwrap(ks.query_claims(r.run, q))) {
        CHECK(c.assessment.expected.has_value());
        CHECK(c.assessment.support.empty());
      }
    }
    CHECK(outputs[0] == outputs[1]);
  }
}
