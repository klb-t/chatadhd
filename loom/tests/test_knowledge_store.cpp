// knowledge_store.h: CRUD and indexed queries per model type, premise rules
// (I3), deterministic output across data directories (I5), judgement replay
// (I4), lazy tables (I10).
#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/knowledge_store.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

Locator L(int n) {
  Locator l;
  l.source = "src_fixture";
  l.member = "conversations.json";
  l.byte_start = n * 100;
  l.byte_len = 80;
  return l;
}

Observation obs(const std::string& unit, int n, const std::string& text) {
  Observation o;
  o.unit = unit;
  o.text = text;
  o.locator = L(n);
  o.ordinal = n;
  o.lang = "pl";
  o.id = Observation::make_id(unit, o.locator, text);
  return o;
}

Entity ent(const std::string& kind, const std::string& key, const std::string& label) {
  Entity e;
  e.kind = kind;
  e.canonical_key = key;
  e.id = Entity::make_id(kind, key);
  e.label = label;
  e.aliases.push_back(Alias{key, label, "", "lexicon", 1, 1.0});
  return e;
}

Claim observed(const std::string& s, const std::string& p, const std::string& o, const Observation& ob,
               double conf = 0.8) {
  Claim c;
  c.subject = s;
  c.predicate = p;
  c.object = o;
  c.assessment.support.push_back(Support{ob.id, ob.locator, ob.text, "extract.pattern@1", 1.0});
  c.assessment.confidence = conf;
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

Claim derived_from(const std::string& s, const std::string& p, Json value, EvidenceClass ev,
                   std::vector<std::string> premises, std::string morphism = "") {
  Claim c;
  c.subject = s;
  c.predicate = p;
  c.value = std::move(value);
  c.assessment.evidence = ev;
  c.assessment.origin = Origin::System;
  c.assessment.confidence = 0.4;
  c.assessment.derivation = Derivation{"r.test", 1, morphism, 1};
  c.assessment.premises.claims = std::move(premises);
  if (ev == EvidenceClass::Inferred) {
    kb::ExpectedProperty e;
    e.expr = Json{{"op", "nonempty"}, {"args", Json::array({"$value"})}};
    e.rationale = "test";
    c.assessment.expected = e;
    c.assessment.check = CheckState::Pending;
  }
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  kb::KnowledgeStore ks;
  std::string run;
  Entity chat = ent("project", "chat adhd", "ChatADHD");
  Entity loom = ent("project", "loom", "Loom");
  Entity sqlite = ent("storage", "sqlite", "SQLite");
  Observation o1 = obs("un_1", 1, "ChatADHD trzyma dane w SQLite");
  Observation o2 = obs("un_1", 2, "Loom też używa SQLite");
  Claim c1 = observed(chat.id, "stores_in", sqlite.id, o1);
  Claim c2 = observed(loom.id, "stores_in", sqlite.id, o2, 0.9);

  explicit Fixture(const char* name = "ks.db") : db(open_db(td.path() / name)), ks(*db) {
    run = unwrap(ks.begin_run("packhash", Json{{"sources", Json::array({"a.zip"})}})).id;
  }
  void fill() {
    LOOM_REQUIRE_OK(ks.put_observations(run, {o2, o1}));
    LOOM_REQUIRE_OK(ks.put_entities(run, {chat, loom, sqlite}));
    LOOM_REQUIRE_OK(ks.put_claims(run, {c2, c1}));
    Instance in;
    in.paradigm = "software_app";
    in.subject = chat.id;
    in.id = Instance::make_id(in.paradigm, in.subject);
    in.slots.push_back(SlotValue{"store", 0, c1.id, Role::Resource, false});
    LOOM_REQUIRE_OK(ks.put_instances(run, {in}));
  }
};

}  // namespace

TEST_SUITE("knowledge_store") {
  TEST_CASE("reads on a fresh directory never create tables; the first write does") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "fresh.db");
    kb::KnowledgeStore ks(*db);
    CHECK(unwrap(ks.query_claims("kr_x", {})).empty());
    CHECK(unwrap(ks.list_runs()).empty());
    CHECK(!ks.has_schema());
    unwrap(ks.begin_run("h", Json::object()));
    CHECK(ks.has_schema());
  }

  TEST_CASE("runs are content-addressed and idempotent") {
    Fixture f;
    auto again = unwrap(f.ks.begin_run("packhash", Json{{"sources", Json::array({"a.zip"})}}));
    CHECK(again.id == f.run);
    CHECK(f.run.rfind("kr_", 0) == 0);
    LOOM_REQUIRE_OK(f.ks.finish_run(f.run, "done", Json{{"claims", 2}}));
    auto r = unwrap(f.ks.get_run(f.run));
    REQUIRE(r);
    CHECK(r->status == "done");
    CHECK(r->summary["claims"] == 2);
    CHECK(!f.ks.finish_run("kr_missing", "done", Json::object()));
  }

  TEST_CASE("CRUD and indexed queries") {
    Fixture f;
    f.fill();
    auto c = unwrap(f.ks.get_claim(f.run, f.c1.id));
    REQUIRE(c);
    CHECK(c->to_json() == f.c1.to_json());
    CHECK(!unwrap(f.ks.get_claim(f.run, "cl_missing")));
    CHECK(!unwrap(f.ks.get_claim("kr_other", f.c1.id)));  // runs are isolated

    kb::ClaimQuery bysubj;
    bysubj.subject = f.chat.id;
    CHECK(unwrap(f.ks.query_claims(f.run, bysubj)).size() == 1);
    kb::ClaimQuery byobj;
    byobj.object = f.sqlite.id;
    byobj.predicate = "stores_in";
    auto both = unwrap(f.ks.query_claims(f.run, byobj));
    REQUIRE(both.size() == 2);
    CHECK(both[0].id < both[1].id);  // deterministic order
    kb::ClaimQuery byev;
    byev.evidence = EvidenceClass::Inferred;
    CHECK(unwrap(f.ks.query_claims(f.run, byev)).empty());
    kb::ClaimQuery byobs;
    byobs.observation = f.o2.id;
    auto sup = unwrap(f.ks.query_claims(f.run, byobs));
    REQUIRE(sup.size() == 1);
    CHECK(sup[0].id == f.c2.id);

    kb::EntityQuery ek;
    ek.kind = "project";
    CHECK(unwrap(f.ks.query_entities(f.run, ek)).size() == 2);
    kb::EntityQuery ea;
    ea.alias_key = "sqlite";
    auto es = unwrap(f.ks.query_entities(f.run, ea));
    REQUIRE(es.size() == 1);
    CHECK(es[0].label == "SQLite");

    auto obs = unwrap(f.ks.observations_of_unit(f.run, "un_1"));
    REQUIRE(obs.size() == 2);
    CHECK(obs[0].ordinal == 1);

    kb::SlotQuery sr;
    sr.role = Role::Resource;
    auto slots = unwrap(f.ks.query_slots(f.run, sr));
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].value.claim == f.c1.id);
    CHECK(unwrap(f.ks.query_instances(f.run, "software_app")).size() == 1);
    CHECK(unwrap(f.ks.query_instances(f.run, "film")).empty());

    auto st = unwrap(f.ks.stats(f.run));
    CHECK(st["claims"] == 2);
    CHECK(st["aliases"] == 3);
    CHECK(st["slot_values"] == 1);
    LOOM_REQUIRE_OK(f.ks.clear_run(f.run));
    CHECK(unwrap(f.ks.stats(f.run))["claims"] == 0);
    CHECK(unwrap(f.ks.get_run(f.run)));  // the run row stays
  }

  TEST_CASE("every model object type round-trips through the store") {
    Fixture f;
    Principle p;
    p.id = "p.local_first";
    p.statement = {{"en", "Local-first"}};
    p.level = PrincipleLevel::Strategy;
    p.form = PrincipleForm::Default;
    LOOM_REQUIRE_OK(f.ks.put_principles(f.run, {p}));
    CHECK(unwrap(f.ks.get_principle(f.run, p.id))->to_json() == p.to_json());
    Operator op;
    op.id = "op.new_source";
    op.situation = {{"en", "new data source"}};
    op.solution = {{"en", "extend the provider registry"}};
    LOOM_REQUIRE_OK(f.ks.put_operators(f.run, {op}));
    CHECK(unwrap(f.ks.list_operators(f.run)).size() == 1);
    Decision d;
    d.id = f.c1.id;
    d.subject = f.chat.id;
    d.alternatives = {DecisionAlternative{"SQLite", f.sqlite.id, nullptr, {}, true}};
    LOOM_REQUIRE_OK(f.ks.put_decisions(f.run, {d}));
    CHECK(unwrap(f.ks.list_decisions(f.run, f.chat.id)).size() == 1);
    Fork fk;
    fk.kind = ForkKind::CodeLineage;
    fk.subject = f.chat.id;
    fk.base = "0.7.9";
    fk.sides = {ForkSide{"0.7.10", "", true, false, ""}, ForkSide{"0.8.3", "", false, false, ""}};
    fk.id = Fork::make_id(fk.kind, fk.subject, fk.base, fk.sides);
    LOOM_REQUIRE_OK(f.ks.put_forks(f.run, {fk}));
    CHECK(unwrap(f.ks.get_fork(f.run, fk.id))->to_json() == fk.to_json());
    Area a;
    a.subject = f.chat.id;
    a.statement = "wszystko jest danymi";
    a.id = Area::make_id(a.subject, f.o1.id, a.statement);
    a.gap = true;
    LOOM_REQUIRE_OK(f.ks.put_areas(f.run, {a}));
    CHECK(unwrap(f.ks.list_areas(f.run, f.chat.id))[0].gap);
    Prediction pn;
    pn.situation = "s";
    pn.solution = "e";
    pn.id = Prediction::make_id("op", pn.situation, "2026-02-08");
    LOOM_REQUIRE_OK(f.ks.put_predictions(f.run, {pn}));
    Model m;
    m.name = "reading A";
    m.id = Model::make_id(m.name);
    LOOM_REQUIRE_OK(f.ks.put_models(f.run, {m}));
    Product pd;
    pd.kind = "specification";
    pd.id = Product::make_id(pd.kind, "in_1", f.run);
    LOOM_REQUIRE_OK(f.ks.put_products(f.run, {pd}));
    Morphism mo;
    mo.id = "m.check";
    mo.from = MorphismEnd{"software_app", "test", "", "", "", std::nullopt};
    mo.to = MorphismEnd{"film", "continuity_check", "", "", "", std::nullopt};
    mo.expected = kb::ExpectedProperty{Json{{"op", "nonempty"}, {"args", Json::array({"$value"})}}, "r", {}, {}};
    LOOM_REQUIRE_OK(f.ks.put_morphisms(f.run, {mo}));
    CHECK(unwrap(f.ks.get_morphism(f.run, mo.id))->to_json() == mo.to_json());
    for (auto s : {std::pair{"0.7.10", StatusValue::Lost}, {"0.6.2", StatusValue::Implemented}, {"0.8.3", StatusValue::Restored}}) {
      StatusRecord r;
      r.entity = f.chat.id;
      r.version = s.first;
      r.status = s.second;
      r.id = StatusRecord::make_id(r.entity, r.branch, r.version, r.status, r.date);
      LOOM_REQUIRE_OK(f.ks.put_status_records(f.run, {r}));
    }
    auto h = unwrap(f.ks.status_history(f.run, f.chat.id));
    REQUIRE(h.size() == 3);
    CHECK(h[0].version == "0.6.2");
    CHECK(h[2].previous == StatusValue::Lost);
  }

  TEST_CASE("premise rules (I3): extrapolated never a premise, transfer never chains; batch is atomic") {
    Fixture f;
    f.fill();
    Claim x = derived_from(f.chat.id, "sync_via", "adapters", EvidenceClass::Extrapolated, {f.c1.id});
    LOOM_REQUIRE_OK(f.ks.put_claims(f.run, {x}));
    Claim bad = derived_from(f.chat.id, "uses", "x", EvidenceClass::Inferred, {x.id});
    auto r = f.ks.put_claims(f.run, {bad});
    REQUIRE(!r);
    CHECK(r.error().message.find("may never be a premise") != std::string::npos);
    // premise in the same batch
    Claim x2 = derived_from(f.loom.id, "sync_via", "y", EvidenceClass::Extrapolated, {});
    Claim bad2 = derived_from(f.loom.id, "uses", "y", EvidenceClass::Derived, {x2.id});
    CHECK(!f.ks.put_claims(f.run, {x2, bad2}));
    CHECK(!unwrap(f.ks.get_claim(f.run, x2.id)));  // nothing of the batch was written
    // transfer depth 1
    Claim t1 = derived_from(f.loom.id, "has_check", "continuity", EvidenceClass::Inferred, {f.c1.id}, "m.check");
    LOOM_REQUIRE_OK(f.ks.put_claims(f.run, {t1}));
    Claim t2 = derived_from(f.chat.id, "has_check", "mix", EvidenceClass::Inferred, {t1.id}, "m.mix");
    auto r2 = f.ks.put_claims(f.run, {t2});
    REQUIRE(!r2);
    CHECK(r2.error().message.find("never chain") != std::string::npos);
    // an ordinary inference may build on a transferred one
    Claim ok = derived_from(f.chat.id, "needs", "tests", EvidenceClass::Inferred, {t1.id});
    LOOM_REQUIRE_OK(f.ks.put_claims(f.run, {ok}));
    // invalid claims are refused
    Claim inv = f.c1;
    inv.assessment.support.clear();
    CHECK(!f.ks.put_claims(f.run, {inv}));
  }

  TEST_CASE("equal writes give byte-identical reads in two data directories (I5)") {
    Fixture a("a.db");
    Fixture b("b.db");
    a.fill();
    b.fill();
    CHECK(a.run == b.run);
    auto dump = [](Fixture& f) {
      Json out = Json::array();
      for (const auto& c : unwrap(f.ks.query_claims(f.run, {}))) out.push_back(c.to_json());
      for (const auto& e : unwrap(f.ks.query_entities(f.run, {}))) out.push_back(e.to_json());
      for (const auto& s : unwrap(f.ks.query_slots(f.run, {}))) out.push_back(s.to_json());
      return json::dump(out);
    };
    CHECK(dump(a) == dump(b));
  }

  TEST_CASE("judgements are append-only and replayed last: confirm, reject, edit, merge, split (I4)") {
    Fixture f;
    f.fill();
    auto add = [&](RefKind k, const std::string& target, Verdict v, Json payload, const char* when) {
      Judgement j;
      j.target_kind = k;
      j.target = target;
      j.verdict = v;
      j.payload = std::move(payload);
      j.created = when;
      return unwrap(f.ks.add_judgement(j));
    };
    auto j1 = add(RefKind::Claim, f.c2.id, Verdict::Confirm, Json::object(), "2026-09-26T10:00:00Z");
    auto j2 = add(RefKind::Claim, f.c1.id, Verdict::Edit, Json{{"object", Entity::make_id("storage", "leveldb")}},
                  "2026-09-26T10:01:00Z");
    auto j3 = add(RefKind::Entity, f.loom.id, Verdict::Merge, Json{{"into", f.chat.id}}, "2026-09-26T10:02:00Z");
    auto j4 = add(RefKind::Entity, f.sqlite.id, Verdict::Split, Json{{"aliases", Json::array({"sqlite"})}, {"key", "sqlite3"}},
                  "2026-09-26T10:03:00Z");
    auto j5 = add(RefKind::Claim, "cl_not_here", Verdict::Reject, Json::object(), "2026-09-26T10:04:00Z");
    CHECK(j1.seq == 1);
    CHECK(j5.seq == 5);
    CHECK(unwrap(f.ks.add_judgement(j1)).seq == 1);  // same id -> stored once
    CHECK(unwrap(f.ks.judgements()).size() == 5);
    CHECK(unwrap(f.ks.judgements(3)).size() == 2);
    CHECK(unwrap(f.ks.judgements(0, f.c1.id)).size() == 1);
    Judgement badj;
    badj.target_kind = RefKind::Observation;
    badj.target = f.o1.id;
    badj.verdict = Verdict::Edit;
    badj.payload = Json{{"text", "rewritten"}};
    CHECK(!f.ks.add_judgement(badj));  // observations are immutable (I1)

    auto rep = unwrap(f.ks.replay_judgements(f.run));
    CHECK(rep.applied == 4);
    CHECK(rep.skipped == 1);
    // confirm -> user evidence, confidence 1
    auto c2 = unwrap(f.ks.get_claim(f.run, f.c2.id));
    CHECK(c2->assessment.status == ClaimStatus::Superseded);  // then re-keyed by the merge
    // edit -> a new user claim; the old one rejected with a counter link; slot re-pointed.
    // The merge below re-keys Loom's claim onto the same (chat, stores_in, sqlite)
    // claim: it keeps the owner's rejection and gains Loom's support.
    auto old1 = unwrap(f.ks.get_claim(f.run, f.c1.id));
    CHECK(old1->assessment.status == ClaimStatus::Rejected);
    REQUIRE(old1->assessment.counter.claims.size() == 1);
    CHECK(old1->assessment.support.size() == 2);
    auto new1 = unwrap(f.ks.get_claim(f.run, old1->assessment.counter.claims[0]));
    REQUIRE(new1);
    CHECK(new1->assessment.evidence == EvidenceClass::User);
    CHECK(new1->assessment.origin == Origin::User);
    CHECK(new1->object == Entity::make_id("storage", "leveldb"));
    auto slots = unwrap(f.ks.query_slots(f.run, {}));
    CHECK(slots[0].value.claim == new1->id);
    // merge -> the loom entity is kept (rejected, merged_into), its claim superseded
    auto lm = unwrap(f.ks.get_entity(f.run, f.loom.id));
    CHECK(lm->status == ClaimStatus::Rejected);
    CHECK(lm->attrs["merged_into"] == f.chat.id);
    CHECK(c2->assessment.counter.claims == std::vector<std::string>{f.c1.id});
    kb::ClaimQuery q;
    q.subject = f.loom.id;
    q.status = ClaimStatus::Active;
    CHECK(unwrap(f.ks.query_claims(f.run, q)).empty());
    kb::EntityQuery ea;
    ea.alias_key = "loom";
    CHECK(unwrap(f.ks.query_entities(f.run, ea)).size() == 2);  // chat got loom's alias (loom kept its own row)
    // split -> a new user entity with the moved aliases
    auto sp = unwrap(f.ks.get_entity(f.run, Entity::make_id("storage", "sqlite3")));
    REQUIRE(sp);
    CHECK(sp->evidence == EvidenceClass::User);
    CHECK(sp->aliases.size() == 1);
    CHECK(unwrap(f.ks.get_entity(f.run, f.sqlite.id))->aliases.empty());
    // replay is incremental, so replaying again changes nothing
    std::string before;
    for (const auto& c : unwrap(f.ks.query_claims(f.run, {}))) before += json::dump(c.to_json());
    auto rep2 = unwrap(f.ks.replay_judgements(f.run));
    CHECK(rep2.applied == 0);
    std::string after;
    for (const auto& c : unwrap(f.ks.query_claims(f.run, {}))) after += json::dump(c.to_json());
    CHECK(before == after);
    // a rebuild (clear + same writes + replay) ends in the same state
    LOOM_REQUIRE_OK(f.ks.clear_run(f.run));
    f.fill();
    unwrap(f.ks.replay_judgements(f.run));
    std::string rebuilt;
    for (const auto& c : unwrap(f.ks.query_claims(f.run, {}))) rebuilt += json::dump(c.to_json());
    CHECK(rebuilt == after);
  }
}
