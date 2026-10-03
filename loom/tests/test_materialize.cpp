// materialize.h: self-description, dossier, backlog, extrapolated spec,
// preference checks over real files, and the knowledge.materialize stage.
#include <doctest/doctest.h>

#include "loom/knowledge.h"
#include "loom/materialize.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;
namespace fs = std::filesystem;

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
  return e;
}

Entity child_entity(const std::string& parent, const std::string& key, const std::string& label) {
  Entity e;
  e.kind = "component";
  e.canonical_key = key;
  e.id = Entity::make_id(e.kind, key);
  e.label = label;
  e.parent = parent;
  return e;
}

Claim make_claim(const std::string& s, const std::string& p, Json value, EvidenceClass ev, double conf = 0.8) {
  Claim c;
  c.subject = s;
  c.predicate = p;
  c.value = std::move(value);
  c.assessment.evidence = ev;
  c.assessment.confidence = conf;
  if (ev == EvidenceClass::Observed) {
    Support sup;
    sup.observation = "ob_1";
    sup.quote = "some supporting quote";
    sup.extractor = "test@1";
    c.assessment.support = {sup};
  }
  if (ev == EvidenceClass::Inferred) {
    c.assessment.derivation = Derivation{"r.test", 1, "", 1};
    kb::ExpectedProperty exp;
    exp.expr = Json{{"op", "nonempty"}, {"args", Json::array({"$value"})}};
    exp.rationale = "test";
    c.assessment.expected = exp;
    c.assessment.check = CheckState::Violated;
  }
  if (ev == EvidenceClass::Absent) c.assessment.open.fill_query = Json{{"terms", Json::array({"x"})}};
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  std::shared_ptr<const kb::Pack> pack;
  kb::KnowledgeStore* ks = nullptr;
  std::string run;

  Entity proj = project_entity("noteflow", "NoteFlow");
  Entity feat = child_entity(proj.id, "checklist", "checklist note type");

  Fixture() {
    rt = open_rt(td.path());
    pack = unwrap(rt->knowledge().pack());
    ks = &rt->knowledge().store();
    run = unwrap(ks->begin_run(pack->hash(), Json::object())).id;
    LOOM_REQUIRE_OK(ks->put_entities(run, {proj, feat}));
  }
};

}  // namespace

TEST_SUITE("materialize") {
  TEST_CASE("self_description: projects, components with status history, decisions, principles, operators") {
    Fixture f;
    StatusRecord sr;
    sr.entity = f.feat.id;
    sr.version = "0.9.0";
    sr.status = StatusValue::Lost;
    sr.date = "2025-10-10";
    sr.id = StatusRecord::make_id(sr.entity, sr.branch, sr.version, sr.status, sr.date);
    StatusRecord sr2;
    sr2.entity = f.feat.id;
    sr2.version = "1.0.0";
    sr2.status = StatusValue::Restored;
    sr2.date = "2025-12-01";
    sr2.id = StatusRecord::make_id(sr2.entity, sr2.branch, sr2.version, sr2.status, sr2.date);
    StatusRecord sr3;
    sr3.entity = f.feat.id;
    sr3.version = "1.2.0";
    sr3.status = StatusValue::Lost;
    sr3.date = "2026-07-01";
    sr3.id = StatusRecord::make_id(sr3.entity, sr3.branch, sr3.version, sr3.status, sr3.date);
    LOOM_REQUIRE_OK(f.ks->put_status_records(f.run, {sr, sr2, sr3}));

    Claim decClaim;
    decClaim.subject = f.proj.id;
    decClaim.predicate = "decides";
    decClaim.value = "C++ core";
    decClaim.assessment.confidence = 0.8;
    decClaim.assessment.support = {Support{"ob_1", {}, "some quote", "test@1", 1.0}};
    decClaim.id = Claim::make_id(decClaim.subject, decClaim.predicate, decClaim.object, decClaim.value, decClaim.qualifiers);
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {decClaim}));
    Decision dec;
    dec.id = decClaim.id;
    dec.subject = f.proj.id;
    dec.date = "2025-08-10";
    dec.alternatives = {DecisionAlternative{"C++ core", "", nullptr, {}, true}};
    LOOM_REQUIRE_OK(f.ks->put_decisions(f.run, {dec}));

    Principle seed;
    seed.id = "p.kod_ne_dane";
    seed.statement = {{"en", "Data, not code branches."}};
    seed.level = PrincipleLevel::Strategy;
    seed.form = PrincipleForm::Invariant;
    seed.validation = ValidationStatus::Candidate;  // no observed occurrence yet -> "seed"
    Principle discovered;
    discovered.id = "pr.decide_fast_correct_later";
    discovered.statement = {{"en", "Decide fast, correct later."}};
    discovered.level = PrincipleLevel::Strategy;
    discovered.form = PrincipleForm::Heuristic;
    discovered.validation = ValidationStatus::Supported;
    discovered.sources.push_back(Reference{"chat", "", "2025-08-10", "", "", ""});
    LOOM_REQUIRE_OK(f.ks->put_principles(f.run, {seed, discovered}));

    Operator op;
    op.id = "op.new_source_new_adapter";
    op.situation = {{"en", "a new data source appears"}};
    op.solution = {{"en", "extend the provider/capability registry"}};
    op.confidence = 0.7;
    LOOM_REQUIRE_OK(f.ks->put_operators(f.run, {op}));

    materialize::Materializer m(*f.rt, *f.ks, f.pack);
    auto r = unwrap(m.self_description(f.run));
    CHECK(r.kind == "self_description");
    CHECK(r.markdown.find("NoteFlow") != std::string::npos);
    CHECK(r.markdown.find("checklist note type") != std::string::npos);
    CHECK(r.markdown.find("oscillates") != std::string::npos);  // lost -> restored -> lost again
    CHECK(r.markdown.find("seed") != std::string::npos);
    CHECK(r.markdown.find("discovered") != std::string::npos);
    CHECK(r.markdown.find("extend the provider/capability registry") != std::string::npos);
    CHECK(r.data["input_hash"].get<std::string>().size() == 64);
    CHECK(!r.product.id.empty());

    // Determinism: rendering twice from the same run is byte-identical.
    auto r2 = unwrap(m.self_description(f.run));
    CHECK(r.markdown == r2.markdown);
    CHECK(r.data["input_hash"] == r2.data["input_hash"]);
  }

  TEST_CASE("dossier: slot table with evidence markers, a conflict and a transferred (by-analogy) slot") {
    Fixture f;
    Claim observed_claim = make_claim(f.proj.id, "language", "C++", EvidenceClass::Observed, 0.9);
    Claim absent_claim = make_claim(f.proj.id, "ci_pipeline", Json(nullptr), EvidenceClass::Absent, 0.0);
    Claim transferred = make_claim(f.proj.id, "has_tests", true, EvidenceClass::Inferred, 0.5);
    transferred.assessment.derivation = Derivation{"m.transfer.x", 1, "m.software_to_film", 1};
    transferred.assessment.check = CheckState::Pending;
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {observed_claim, absent_claim, transferred}));

    Instance inst;
    inst.paradigm = "software_app";
    inst.subject = f.proj.id;
    inst.subject_label = f.proj.label;
    inst.id = Instance::make_id(inst.paradigm, inst.subject);
    inst.slots = {SlotValue{"language", 0, observed_claim.id, Role::Constraint, false},
                 SlotValue{"ci_pipeline", 0, absent_claim.id, Role::Check, true},
                 SlotValue{"has_tests", 0, transferred.id, Role::Check, false}};
    LOOM_REQUIRE_OK(f.ks->put_instances(f.run, {inst}));

    materialize::Materializer m(*f.rt, *f.ks, f.pack);
    auto r = unwrap(m.dossier(f.run, inst.id));
    CHECK(r.markdown.find("language") != std::string::npos);
    CHECK(r.markdown.find("C++") != std::string::npos);
    CHECK(r.markdown.find("[CONFLICT]") != std::string::npos);
    CHECK(r.data["conflicts"].size() == 1);
    CHECK(r.data["analogies"].size() == 1);
    CHECK(r.data["slots"].size() == 3);

    CHECK(!m.dossier(f.run, "in_missing"));
  }

  TEST_CASE("backlog: contested claims, violated expected properties, absent slots, lost features") {
    Fixture f;
    Claim contested = make_claim(f.proj.id, "core_language", "C++", EvidenceClass::Observed, 0.6);
    contested.assessment.status = ClaimStatus::Contested;
    Claim violated = make_claim(f.proj.id, "has_ci", true, EvidenceClass::Inferred, 0.5);
    Claim violates = make_claim(f.proj.id, "violates", "p.no_telemetry", EvidenceClass::Observed, 0.9);
    violates.predicate = "violates";
    Claim absent = make_claim(f.proj.id, "license", Json(nullptr), EvidenceClass::Absent, 0.0);
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {contested, violated, violates, absent}));

    Instance inst;
    inst.paradigm = "software_app";
    inst.subject = f.proj.id;
    inst.subject_label = f.proj.label;
    inst.id = Instance::make_id(inst.paradigm, inst.subject);
    inst.slots = {SlotValue{"license", 0, absent.id, Role::Constraint, false}};
    LOOM_REQUIRE_OK(f.ks->put_instances(f.run, {inst}));

    StatusRecord lost;
    lost.entity = f.feat.id;
    lost.version = "1.2.0";
    lost.status = StatusValue::Lost;
    lost.date = "2026-07-01";
    lost.id = StatusRecord::make_id(lost.entity, lost.branch, lost.version, lost.status, lost.date);
    LOOM_REQUIRE_OK(f.ks->put_status_records(f.run, {lost}));

    materialize::Materializer m(*f.rt, *f.ks, f.pack);
    auto r = unwrap(m.backlog(f.run));
    CHECK(r.data["contested"].size() == 1);
    CHECK(r.data["violated_expected_properties"].size() == 1);
    CHECK(r.data["violations"].size() == 1);
    CHECK(r.data["absent_required_slots"].size() == 1);
    CHECK(r.data["lost_features"].size() == 1);
    CHECK(r.markdown.find("checklist note type") != std::string::npos);
  }

  TEST_CASE("extrapolated_spec: only Extrapolated-evidence slots, under Proposals, never as fact") {
    Fixture f;
    Claim extrapolated = make_claim(f.proj.id, "next_platform", "watchOS", EvidenceClass::Extrapolated, 0.4);
    extrapolated.assessment.derivation = Derivation{"x.multiplatform_extension", 1, "", 2};
    Claim observed = make_claim(f.proj.id, "language", "C++", EvidenceClass::Observed, 0.9);
    LOOM_REQUIRE_OK(f.ks->put_claims(f.run, {extrapolated, observed}));

    Instance inst;
    inst.paradigm = "software_app";
    inst.subject = f.proj.id;
    inst.subject_label = f.proj.label;
    inst.id = Instance::make_id(inst.paradigm, inst.subject);
    inst.slots = {SlotValue{"next_platform", 0, extrapolated.id, Role::Constraint, false},
                 SlotValue{"language", 0, observed.id, Role::Constraint, false}};
    LOOM_REQUIRE_OK(f.ks->put_instances(f.run, {inst}));

    materialize::Materializer m(*f.rt, *f.ks, f.pack);
    auto r = unwrap(m.extrapolated_spec(f.run, inst.id));
    CHECK(r.markdown.find("Proposals") != std::string::npos);
    CHECK(r.markdown.find("watchOS") != std::string::npos);
    CHECK(r.markdown.find("C++") == std::string::npos);  // observed, not a proposal: must not leak in here
    CHECK(r.data["proposals"].size() == 1);
  }

  TEST_CASE("check_preferences: real detectors over real files (regex_line, regex_block, string_array_literal)") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path() / "d");
    auto pack = unwrap(rt->knowledge().pack());
    kb::KnowledgeStore ks(rt->db());

    fs::path bad_py = td.path() / "bad.py";
    LOOM_REQUIRE_OK(fsutil::write_file(bad_py, "def f():\n    try:\n        g()\n    except:\n        pass\n"));
    fs::path clean_py = td.path() / "clean.py";
    LOOM_REQUIRE_OK(fsutil::write_file(clean_py, "def f():\n    try:\n        g()\n    except Exception:\n        log.debug('x', exc_info=True)\n"));
    fs::path literal_py = td.path() / "literal.py";
    std::string arr = "TOPICS = [";
    for (int i = 0; i < 25; ++i) arr += "\"topic" + std::to_string(i) + "\", ";
    arr += "]\n";
    LOOM_REQUIRE_OK(fsutil::write_file(literal_py, arr));

    materialize::Materializer m(*rt, ks, pack);
    model::Product product;
    auto checks = unwrap(m.check_preferences(product, {bad_py.string(), clean_py.string(), literal_py.string()}));
    REQUIRE(!checks.empty());
    bool found_except = false, found_literal = false;
    for (const auto& c : checks) {
      if (c.check == "chk.silent_except") {
        found_except = true;
        CHECK(!c.passed);
        CHECK(c.detail.find("bad.py") != std::string::npos);
      }
      if (c.check == "chk.literal_policy_table") {
        found_literal = true;
        CHECK(!c.passed);
        CHECK(c.detail.find("literal.py") != std::string::npos);
      }
      if (c.check == "chk.telemetry_call") CHECK(c.passed);  // no telemetry calls anywhere in the fixture files
    }
    CHECK(found_except);
    CHECK(found_literal);
  }

  TEST_CASE("knowledge.materialize stage: end to end through KnowledgeEngine, artifacts + products stored") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    Entity proj = project_entity("noteflow", "NoteFlow");

    for (auto s : {"catalog", "extract", "resolve", "assess"}) {
      rt->knowledge().set_stage(s, [](knowledge::StageContext&) -> Result<Json> {
        return Json{{"output", "x"}, {"stats", Json::object()}};
      });
    }
    rt->knowledge().set_stage("generalize", [proj](knowledge::StageContext& c) -> Result<Json> {
      LOOM_TRY(c.store.put_entities(c.run, {proj}));
      Principle p;
      p.id = "p.kod_ne_dane";
      p.statement = {{"en", "Data, not code branches."}};
      p.level = PrincipleLevel::Strategy;
      p.form = PrincipleForm::Invariant;
      LOOM_TRY(c.store.put_principles(c.run, {p}));
      Instance inst;
      inst.paradigm = "software_app";
      inst.subject = proj.id;
      inst.subject_label = proj.label;
      inst.id = Instance::make_id(inst.paradigm, inst.subject);
      LOOM_TRY(c.store.put_instances(c.run, {inst}));
      return Json{{"output", "gen-hash"}, {"stats", Json::object()}};
    });

    auto result = unwrap(rt->knowledge().run(knowledge::KnowledgeConfig{}));
    REQUIRE(result.status == "done");
    auto stage_names = std::vector<std::string>{};
    for (auto& s : result.stages) stage_names.push_back(s.stage);
    CHECK(std::find(stage_names.begin(), stage_names.end(), "materialize") != stage_names.end());

    auto products = unwrap(rt->knowledge().store().list_products(result.run));
    CHECK(products.size() >= 2);  // self_description + backlog (+ dossier/extrapolated_spec for the instance)

    auto artifacts = unwrap(rt->provenance().list_artifacts(100));
    bool have_self = false;
    for (const auto& a : artifacts) {
      if (a.kind == "knowledge.self_description") {
        have_self = true;
        auto content = unwrap(rt->blobs().read(a.blob_hash));
        CHECK(content.find("NoteFlow") != std::string::npos);
      }
    }
    CHECK(have_self);

    // Re-running with the same (cached) inputs is a stage-level cache hit
    // and does not duplicate products.
    auto result2 = unwrap(rt->knowledge().run(knowledge::KnowledgeConfig{}));
    CHECK(result2.run == result.run);
    for (const auto& s : result2.stages) CHECK(s.cache_hit);
  }
}
