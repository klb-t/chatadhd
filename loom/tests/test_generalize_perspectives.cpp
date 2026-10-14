// Public, synthetic mechanism cases: several readings of the same subject
// must not silently collapse to the highest-scoring project kind.
#include <doctest/doctest.h>

#include <algorithm>
#include <set>

#include "loom/generalize.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::generalize;
using loom::test::unwrap;

namespace {

std::shared_ptr<const kb::Pack> pack() {
  static auto p = unwrap(kb::Pack::load_builtin());
  return p;
}

Match perspective(const std::string& subject, const std::string& kind, double score) {
  Match m;
  m.instance.subject = subject;
  m.instance.subject_label = subject;
  m.instance.paradigm = kind;
  m.instance.id = model::Instance::make_id(kind, subject);
  m.instance.score = m.score = score;
  return m;
}

void fill(Match& m, const std::string& slot, model::Role role, const std::string& predicate,
          const std::string& value, model::EvidenceClass evidence = model::EvidenceClass::Observed) {
  model::Claim c;
  c.subject = m.instance.subject;
  c.predicate = predicate;
  c.value = value;
  c.assessment.evidence = evidence;
  c.assessment.confidence = 0.8;
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  m.instance.slots.push_back({slot, 0, c.id, role, false});
  m.claims.push_back(std::move(c));
}

std::vector<Match> readings() {
  auto film = perspective("e_source", "film", 0.4);
  fill(film, "scene", model::Role::Part, "has_scene", "scene");
  fill(film, "character", model::Role::Actor, "has_character", "character");
  fill(film, "continuity_check", model::Role::Check, "has_check", "continuity");
  auto music = perspective("e_target", "music", 0.4);
  fill(music, "section", model::Role::Part, "has_section", "section");
  fill(music, "performer", model::Role::Actor, "has_performer", "performer");
  return {film, music};
}

Json claims_json(const std::vector<model::Claim>& claims) {
  Json out = Json::array();
  for (const auto& c : claims) out.push_back(c.to_json());
  return out;
}

}  // namespace

TEST_SUITE("generalize_perspectives") {
  TEST_CASE("a higher scoring unrelated reading cannot hide a valid transfer") {
    auto ms = readings();
    const auto before = unwrap(transfer(*pack(), ms));
    REQUIRE(before.size() == 1);
    ms.push_back(perspective("e_source", "research", 0.99));
    ms.push_back(perspective("e_target", "software_app", 0.99));
    const auto after = unwrap(transfer(*pack(), ms));
    CHECK(claims_json(after) == claims_json(before));
    std::reverse(ms.begin(), ms.end());
    CHECK(claims_json(unwrap(transfer(*pack(), ms))) == claims_json(before));
  }

  TEST_CASE("analogy retains independent perspective pairs with their identities") {
    auto ms = readings();
    auto alternative = perspective("e_source", "software_app", 0.99);
    fill(alternative, "module", model::Role::Part, "has_component", "component");
    fill(alternative, "developer", model::Role::Actor, "has_role", "developer");
    ms.push_back(alternative);
    auto analogies = unwrap(ParadigmMatcher(pack()).analogies(ms));
    REQUIRE(analogies.size() == 2);
    std::set<std::string> from;
    for (const auto& c : analogies) {
      CHECK(c.subject != c.object);
      from.insert(json::get_string(c.qualifiers.extra, "from_instance"));
      CHECK(c.qualifiers.extra["to_instance"] == ms[1].instance.id);
    }
    CHECK(from == std::set<std::string>{ms[0].instance.id, ms[2].instance.id});
    std::reverse(ms.begin(), ms.end());
    CHECK(claims_json(unwrap(ParadigmMatcher(pack()).analogies(ms))) == claims_json(analogies));
  }

  TEST_CASE("transfer premises include the actual role evidence from both projects") {
    auto ms = readings();
    auto result = unwrap(transfer(*pack(), ms));
    REQUIRE(result.size() == 1);
    std::set<std::string> expected;
    for (const auto& m : ms) for (const auto& c : m.claims) expected.insert(c.id);
    const auto& premises = result[0].assessment.premises.claims;
    CHECK(std::set<std::string>(premises.begin(), premises.end()) == expected);
    CHECK(result[0].assessment.evidence == model::EvidenceClass::Inferred);
    CHECK(result[0].assessment.support.empty());
    CHECK(result[0].assessment.check == model::CheckState::Pending);
  }

  TEST_CASE("inferred roles alone cannot justify an observed-grounded analogy") {
    auto a = perspective("e_a", "film", 0.8);
    auto b = perspective("e_b", "music", 0.8);
    fill(a, "scene", model::Role::Part, "has_scene", "maybe", model::EvidenceClass::Inferred);
    fill(b, "section", model::Role::Part, "has_section", "maybe", model::EvidenceClass::Inferred);
    CHECK(unwrap(ParadigmMatcher(pack()).analogies({a, b})).empty());
  }

  TEST_CASE("reverse transfer expected property refers to its actual destination") {
    auto ms = readings();
    // Music now has the check, film does not.
    ms[0].claims.pop_back();
    ms[0].instance.slots.pop_back();
    fill(ms[1], "mix_check", model::Role::Check, "has_check", "mix check");
    auto result = unwrap(transfer(*pack(), ms));
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].assessment.expected);
    Evidence ev;
    fill(ms[0], "continuity_check", model::Role::Check, "has_check", "continuity");
    auto check = ms[0].claims.back();
    model::Entity e;
    e.id = "e_continuity";
    e.kind = "continuity_check";
    e.canonical_key = e.label = "continuity";
    check.object = e.id;
    check.value = Json();
    ev.entities.push_back(e);
    ev.claims.push_back(check);
    CHECK(unwrap(evaluate_property(*result[0].assessment.expected, result[0], ev)) == model::CheckState::Holds);
  }
}
