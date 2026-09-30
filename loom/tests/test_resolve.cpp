// resolve.h: entity resolution (PL + EN keys, camel/acronym splits, kind
// agreement, split guard), claim remapping, calibration, conflicts, diff and
// code lineage on synthetic revisions.
#include <doctest/doctest.h>

#include "loom/kb.h"
#include "loom/resolve.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

std::shared_ptr<const kb::Pack> pack() {
  static auto p = unwrap(kb::Pack::load_builtin());
  return p;
}

model::Observation obs(const std::string& id, const std::string& text) {
  model::Observation o;
  o.id = id;
  o.unit = "un_1";
  o.text = text;
  return o;
}

model::Entity ent(const std::string& kind, const std::string& label, const std::vector<std::string>& observations,
                  const std::string& method = "mined") {
  kb::Normalizer n(*pack());
  model::Entity e;
  e.kind = kind;
  e.label = label;
  e.canonical_key = n.phrase_key(label);
  e.id = model::Entity::make_id(kind, e.canonical_key);
  model::Alias a;
  a.key = n.phrase_key(label, false);
  a.surface = label;
  a.method = method;
  a.count = 1;
  e.aliases.push_back(a);
  Json o = Json::array();
  for (const auto& x : observations) o.push_back(x);
  e.attrs = Json{{"observations", o}};
  return e;
}

model::Claim observed(const std::string& subject, const std::string& pred, const Json& value, const std::string& obs_id,
                      const std::string& extractor = "extract.status_cues@1", double q = 1.0) {
  model::Claim c;
  c.subject = subject;
  c.predicate = pred;
  c.value = value;
  model::Support s;
  s.observation = obs_id;
  s.locator.source = "sha256:x";
  s.locator.json_pointer = "/" + obs_id;
  s.extractor = extractor;
  s.quality = q;
  c.assessment.support.push_back(s);
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

const model::Entity* find(const resolve::ResolveResult& r, const std::string& label) {
  for (const auto& e : r.entities) {
    if (e.label == label) return &e;
  }
  return nullptr;
}

}  // namespace

TEST_SUITE("resolve") {
  TEST_CASE("czat ADHD == chat adhd == ChatADHD (glossary, camel split); same_as claims with reasons") {
    std::vector<model::Observation> os{obs("o1", "Pracuję nad czat ADHD na Kivy."), obs("o2", "The chat adhd app on Android."),
                                       obs("o3", "ChatADHD uses OpenRouter.")};
    std::vector<model::Entity> es{ent("project", "czat ADHD", {"o1"}), ent("project", "chat adhd", {"o2"}),
                                  ent("concept", "ChatADHD", {"o3"})};
    resolve::Resolver r(pack());
    auto res = unwrap(r.resolve(es, os));
    REQUIRE(res.entities.size() == 1);
    CHECK(res.entities[0].kind == "project");  // the specific kind wins over "concept"
    CHECK(res.entities[0].aliases.size() >= 2);
    CHECK(res.same_as.size() == 1);  // PL and EN keys are one key already; the camel name is merged
    for (const auto& c : res.same_as) {
      CHECK(c.predicate == "same_as");
      CHECK(c.assessment.evidence == model::EvidenceClass::Derived);
      CHECK(c.assessment.origin == model::Origin::System);
      CHECK(c.validate());
    }
    bool split_reason = false;
    for (const auto& d : res.decisions) {
      for (const auto& x : d.reasons) split_reason = split_reason || x == "camel_acronym_split";
    }
    CHECK(split_reason);
  }

  TEST_CASE("split guard: loom the weaving word vs Loom the kernel stay apart") {
    std::vector<model::Observation> os{
        obs("k1", "Loom kernel exposes a C ABI and a runtime for ChatADHD."),
        obs("k2", "The Loom SDK runtime stores the graph in SQLite via the C++ core."),
        obs("w1", "Kupiłam mamie krosno, loom do tkania makramy z bawełny."),
        obs("w2", "Stołowe krosno tkackie, loom, prezent dla mamy na urodziny.")};
    std::vector<model::Entity> es{ent("project", "Loom", {"k1", "k2"}), ent("concept", "loom", {"w1", "w2"})};
    resolve::Resolver r(pack());
    auto res = unwrap(r.resolve(es, os));
    CHECK(res.entities.size() == 2);
    CHECK(find(res, "Loom") != nullptr);
    CHECK(find(res, "loom") != nullptr);
    bool guarded = false;
    for (const auto& d : res.decisions) guarded = guarded || d.blocked_by == "split_guard";
    CHECK(guarded);
  }

  TEST_CASE("kinds that disagree are not merged; versions never merge with names") {
    std::vector<model::Observation> os{obs("a", "Python is the language"), obs("b", "python the branch")};
    std::vector<model::Entity> es{ent("language", "Python", {"a"}), ent("platform", "Python", {"b"}),
                                  ent("version", "0.7.9", {"a"})};
    resolve::Resolver r(pack());
    auto res = unwrap(r.resolve(es, os));
    CHECK(res.entities.size() == 3);
  }

  TEST_CASE("apply_remap re-keys claims; the old claims stay, superseded") {
    std::vector<model::Observation> os{obs("o1", "czat ADHD"), obs("o3", "ChatADHD")};
    std::vector<model::Entity> es{ent("project", "czat ADHD", {"o1"}), ent("concept", "ChatADHD", {"o3"})};
    resolve::Resolver r(pack());
    auto res = unwrap(r.resolve(es, os));
    auto c1 = observed(es[1].id, "has_version", "0.7.9", "o3");
    auto out = r.apply_remap({c1}, res);
    REQUIRE(out.size() == 2);
    int superseded = 0;
    for (const auto& c : out) {
      if (c.assessment.status == model::ClaimStatus::Superseded) {
        ++superseded;
        CHECK(c.id == c1.id);
      } else {
        CHECK(c.subject == res.remap.at(es[1].id));
      }
    }
    CHECK(superseded == 1);
  }

  TEST_CASE("calibrate: unit-deduplicated noisy-OR of policy reliabilities, isotonic per class") {
    auto a = observed("e_1", "has_status", "lost", "o1", "extract.status_cues@1", 1.0);
    auto b = a;
    // a second support from the same unit counts once; from another unit it adds
    b.assessment.support.push_back(b.assessment.support.front());
    b.assessment.support.back().observation = "o2";
    auto c = b;
    c.assessment.support.back().locator.json_pointer = "/other";
    std::vector<model::Claim> v{a, b, c};
    LOOM_REQUIRE_OK(resolve::calibrate(*pack(), v));
    CHECK(v[0].assessment.confidence == doctest::Approx(0.6));   // extractor.item Beta(6,4)
    CHECK(v[1].assessment.confidence == doctest::Approx(0.6));   // same unit: counted once
    CHECK(v[2].assessment.confidence == doctest::Approx(0.84));  // 1 - 0.4 * 0.4
  }

  TEST_CASE("calibrate: owner authority preserves supplied confidence and source assessment") {
    for (double confidence : {0.0, 0.35, 1.0}) {
      CAPTURE(confidence);
      auto observed_claim = observed("e_1", "has_status", "implemented", "o1");
      auto owner_claim = observed("e_1", "has_status", "planned", "o2");
      owner_claim.assessment.evidence = model::EvidenceClass::User;
      owner_claim.assessment.origin = model::Origin::User;
      owner_claim.assessment.confidence = confidence;
      const auto original_owner = owner_claim.to_json();
      std::vector<model::Claim> claims{observed_claim, owner_claim};

      LOOM_REQUIRE_OK(resolve::calibrate(*pack(), claims));
      CHECK(claims[0].assessment.confidence == doctest::Approx(0.6));
      CHECK(claims[1].assessment.confidence == confidence);
      // Confidence, support, source origin and all other assessment data survive.
      CHECK(claims[1].to_json() == original_owner);

      auto conflicts = unwrap(resolve::detect_conflicts(claims));
      REQUIRE(conflicts.size() == 1);
      CHECK(conflicts[0].resolution == "user");
      CHECK(conflicts[0].winner == owner_claim.id);
      CHECK(claims[1].assessment.confidence == confidence);
      CHECK(claims[0].assessment.status == model::ClaimStatus::Contested);
      CHECK(claims[1].assessment.status == model::ClaimStatus::Contested);
    }
  }

  TEST_CASE("detect_conflicts: incompatible values stay, contested, with a candidate resolution") {
    auto a = observed("e_f", "has_status", "implemented", "o1");
    a.qualifiers.version = "0.9.0";
    a.qualifiers.valid_from = "2025-10-10";
    a.id = model::Claim::make_id(a.subject, a.predicate, a.object, a.value, a.qualifiers);
    auto b = observed("e_f", "has_status", "lost", "o2");
    b.qualifiers = a.qualifiers;
    b.id = model::Claim::make_id(b.subject, b.predicate, b.object, b.value, b.qualifiers);
    auto other = observed("e_g", "has_status", "lost", "o3");
    std::vector<model::Claim> v{a, b, other};
    auto cs = unwrap(resolve::detect_conflicts(v));
    REQUIRE(cs.size() == 1);
    CHECK(cs[0].claims.size() == 2);
    CHECK(cs[0].resolution == "open");
    CHECK(v[0].assessment.status == model::ClaimStatus::Contested);
    CHECK(v[1].assessment.status == model::ClaimStatus::Contested);
    CHECK(v[2].assessment.status == model::ClaimStatus::Active);
    CHECK(v[0].assessment.counter.claims == std::vector<std::string>{b.id});
    // the owner's statement is the candidate winner
    v[1].assessment.evidence = model::EvidenceClass::User;
    v[0].assessment.status = v[1].assessment.status = model::ClaimStatus::Active;
    auto cs2 = unwrap(resolve::detect_conflicts(v));
    REQUIRE(cs2.size() == 1);
    CHECK(cs2[0].resolution == "user");
    CHECK(cs2[0].winner == b.id);
  }

  TEST_CASE("changed_lines (Myers) and code_lineage on synthetic revisions") {
    CHECK(resolve::changed_lines("a\nb\nc\n", "a\nb\nc\n") == 0);
    CHECK(resolve::changed_lines("a\nb\nc", "a\nx\nc") == 2);
    CHECK(resolve::changed_lines("a\nb", "a\nb\nc") == 1);
    resolve::Revision r1{"r1", "0.1", "2025-01-01T00:00:00Z", {{"core.py", "a\nb\nc\n"}, {"ui.py", "x\n"}}};
    resolve::Revision r2{"r2", "0.2", "2025-02-01T00:00:00Z", {{"core.py", "a\nb\nc\nd\n"}, {"ui.py", "x\ny\n"}}};
    resolve::Revision r3{"r3", "0.3", "2025-03-01T00:00:00Z", {{"core.py", "a\nB\nc\nd\ne\n"}, {"ui.py", "x\ny\nz\n"}}};
    // a fork of 0.2: core.py unchanged, ui.py edited
    resolve::Revision snap{"snapshot:fork", "fork", "", {{"core.py", "a\nb\nc\nd\n"}, {"ui.py", "x\ny\nq\n"}, {"new.py", "n\n"}}};
    auto lr = unwrap(resolve::code_lineage(snap, {r3, r1, r2}));
    CHECK(lr.base == "r2");
    CHECK(lr.per_file["core.py"]["revision"] == "r2");
    CHECK(lr.per_file["core.py"]["changed_lines"] == 0);
    // identical trees tie -> the earlier date, then the id
    resolve::Revision twin = r2;
    twin.id = "r2b";
    auto lr2 = unwrap(resolve::code_lineage(snap, {twin, r2}));
    CHECK(lr2.base == "r2");
    auto js = unwrap(resolve::Revision::from_json(r1.to_json()));
    CHECK(js.files.at("core.py").rfind("sha256:", 0) == 0);  // contents are hashed in JSON
    CHECK(!resolve::code_lineage(snap, {}));
  }
}
