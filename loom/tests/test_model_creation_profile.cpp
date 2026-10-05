#include <array>
#include <limits>

#include "loom/model.h"
#include "loom/model_profile.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {
Json principle_input() {
  return Json{{"id", "p.synthetic"}, {"statement", Json{{"en", "Synthetic creation"}}},
              {"level", "strategy"}, {"form", "heuristic"}, {"origin", "archive"}};
}
Json operator_input() {
  return Json{{"id", "op.synthetic"}, {"situation", Json{{"en", "Synthetic situation"}}},
              {"solution", Json{{"en", "Synthetic solution"}}}, {"origin", "archive"}};
}
Json morphism_input() {
  return Json{{"id", "m.synthetic"}, {"use", "anchoring"}, {"origin", "model_knowledge"},
              {"from", Json{{"paradigm", "synthetic"}, {"kind", "component"}}},
              {"to", Json{{"role", "part"}}}};
}
Json slot_input() { return Json{{"name", "label"}, {"type", "text"}}; }
Json relation_input() { return Json{{"rel", "has_part"}, {"target", "component"}}; }
Json domain_input() { return Json{{"id", "component"}, {"role", "part"}}; }
Json project_input() {
  return Json{{"id", "synthetic"}, {"title", Json{{"en", "Synthetic project"}}}, {"origin", "archive"},
              {"domain_kinds", Json::array({domain_input()})}};
}
template <class T>
Json decoded(const Json& record) { return unwrap(T::from_json(record)).to_json(); }
}

TEST_SUITE("model.creation_profile") {
  TEST_CASE("builtin creation priors preserve every historic decoder output") {
    struct Example { std::string_view kind; Json input; Json (*decoder)(const Json&); };
    const std::array<Example, 7> cases{{
        {"principle", principle_input(), decoded<Principle>}, {"operator", operator_input(), decoded<Operator>},
        {"morphism", morphism_input(), decoded<Morphism>}, {"slot", slot_input(), decoded<SlotSpec>},
        {"domain_relation", relation_input(), decoded<DomainRelation>}, {"domain_kind", domain_input(), decoded<DomainKind>},
        {"project_kind", project_input(), decoded<ProjectKind>},
    }};
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    for (const auto& example : cases) {
      CAPTURE(example.kind);
      const auto original = example.input;
      const auto legacy = example.decoder(example.input);
      const auto implicit = unwrap(normalize_creation(example.kind, example.input));
      const auto explicit_profile = unwrap(normalize_creation(example.kind, example.input, builtin));
      CHECK(example.decoder(implicit.record) == legacy);
      CHECK(implicit.record == explicit_profile.record);
      CHECK(implicit.applied_defaults == explicit_profile.applied_defaults);
      CHECK(implicit.profile_hash == builtin.hash());
      CHECK(example.input == original);
      CHECK_FALSE(implicit.applied_defaults.empty());
    }
    CHECK(creation_capabilities().at("records").size() == 7);
  }

  TEST_CASE("local creation policy applies to nested kinds slots and relations") {
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    const auto custom = unwrap(builtin.with_overrides(Json{{"creation_defaults", Json{
      {"principle", Json{{"confidence", 0.2}, {"validation_status", "supported"}}},
      {"morphism", Json{{"mode", "value"}}},
      {"slot", Json{{"card", "list"}, {"required", true}, {"weight", 4.0}}},
      {"domain_relation", Json{{"card", "one"}}},
      {"domain_kind", Json{{"card", "one"}, {"required", true}, {"weight", 3.0}}},
      {"project_kind", Json{{"subject_kind", "application"}}}
    }}}));
    const auto principle = unwrap(normalize_creation("principle", principle_input(), custom));
    CHECK(principle.record.at("confidence") == 0.2);
    CHECK(principle.record.at("validation_status") == "supported");
    CHECK(principle.applied_defaults.at("/confidence") == 0.2);
    CHECK(principle.profile_hash == custom.hash());
    CHECK(principle.profile_hash != builtin.hash());
    CHECK(unwrap(normalize_creation("morphism", morphism_input(), custom)).record.at("mode") == "value");
    auto project = project_input();
    project["domain_kinds"][0]["slots"] = Json::array({slot_input()});
    project["domain_kinds"][0]["relations"] = Json::array({relation_input()});
    const auto result = unwrap(normalize_creation("project_kind", project, custom));
    CHECK(result.record.at("subject_kind") == "application");
    const auto& domain = result.record.at("domain_kinds").at(0);
    CHECK(domain.at("card") == "one");
    CHECK(domain.at("required") == true);
    CHECK(domain.at("weight") == 3.0);
    CHECK(domain.at("slots").at(0).at("card") == "list");
    CHECK(domain.at("slots").at(0).at("weight") == 4.0);
    CHECK(domain.at("relations").at(0).at("card") == "one");
    CHECK(result.applied_defaults.at("/domain_kinds/0/slots/0/weight") == 4.0);
    CHECK(result.applied_defaults.at("/domain_kinds/0/relations/0/card") == "one");
  }

  TEST_CASE("explicit invocation overrides preserve producer evidence provenance and confidence") {
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    const auto custom = unwrap(builtin.with_overrides(Json{{"creation_defaults", Json{
      {"principle", Json{{"confidence", 0.2}, {"validation_status", "rejected"}}}}}}));
    auto input = principle_input();
    input["confidence"] = 0.91;
    input["validation_status"] = "confirmed";
    input["origin"] = "user";
    input["sources"] = Json::array({Json{{"doc", "synthetic-source"}, {"section", "synthetic-location"}}});
    input["evidence_for"] = Json::array({"synthetic-observation"});
    input["checks"] = Json::array({"synthetic-check"});
    input["producer_provenance"] = Json{{"origin", "recorded"}, {"text", "{{profile_cannot_evaluate_this}}"}};
    const auto result = unwrap(normalize_creation("principle", input, custom));
    CHECK(result.record == input);
    CHECK(result.applied_defaults.empty());
    CHECK(result.record.at("origin") == "user");
    CHECK(result.record.at("confidence") == 0.91);
    CHECK(result.record.at("producer_provenance") == input.at("producer_provenance"));
    CHECK(result.profile_hash == custom.hash());
    const auto prior = unwrap(normalize_creation("principle", principle_input(), custom));
    CHECK(prior.applied_defaults.contains("/confidence"));
    CHECK_FALSE(prior.applied_defaults.contains("/origin"));
    CHECK_FALSE(prior.record.contains("measured_confidence"));
  }

  TEST_CASE("new creation requires honest explicit producer origin and wire identity") {
    auto input = principle_input();
    input.erase("origin");
    CHECK_FALSE(normalize_creation("principle", input));
    CHECK(unwrap(Principle::from_json(input)).origin == Origin::Archive);  // immutable historical read
    for (const Json& value : Json::array({nullptr, "", "invented", 17})) {
      input["origin"] = value;
      CHECK_FALSE(normalize_creation("principle", input));
    }
    input = principle_input();
    input.erase("id");
    CHECK_FALSE(normalize_creation("principle", input));
    input = principle_input();
    input.erase("level");
    CHECK_FALSE(normalize_creation("principle", input));
    CHECK_FALSE(normalize_creation("claim", principle_input()));
    CHECK_FALSE(normalize_creation("principle", Json::array()));
    auto morphism = morphism_input();
    morphism["bidirectional"] = true;
    CHECK_FALSE(normalize_creation("morphism", morphism));
  }

  TEST_CASE("malformed explicit policy values and schema spoofing fail closed") {
    auto input = principle_input();
    for (const Json& value : Json::array({nullptr, "0.5", -0.1, 1.1})) {
      input["confidence"] = value;
      CHECK_FALSE(normalize_creation("principle", input));
    }
    input["confidence"] = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(normalize_creation("principle", input));
    auto slot = slot_input();
    slot["weight"] = 0;
    CHECK_FALSE(normalize_creation("slot", slot));
    slot["weight"] = -1;
    CHECK_FALSE(normalize_creation("slot", slot));
    slot["weight"] = std::numeric_limits<double>::denorm_min();
    CHECK(normalize_creation("slot", slot));
    slot["card"] = "invented";
    CHECK_FALSE(normalize_creation("slot", slot));
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    CHECK_FALSE(builtin.with_overrides(Json{{"creation_defaults", Json{{"principle", Json{{"origin", "user"}}}}}}));
    CHECK_FALSE(builtin.with_overrides(Json{{"creation_defaults", Json{{"principle", Json{{"sources", Json::array()}}}}}}));
    CHECK_FALSE(normalize_creation("principle", principle_input(), unwrap(RuntimeProfile::builtin("memory"))));
    const Json definition{{"schema", "loom.runtime_profile/1"}, {"domain", "model"}, {"revision", 1},
      {"defaults", Json{{"creation_defaults", Json{{"principle", Json{{"confidence", "wrong"}, {"origin", "user"}}}}}}},
      {"value_schema", Json{{"type", "object"}}}};
    const auto permissive = unwrap(RuntimeProfile::from_definition(definition));
    CHECK_FALSE(normalize_creation("principle", principle_input(), permissive));
  }

  TEST_CASE("active creation policy never changes historical missing-field reads") {
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    const auto custom = unwrap(builtin.with_overrides(Json{{"creation_defaults", Json{
      {"principle", Json{{"confidence", 0.8}, {"validation_status", "retired"}}},
      {"slot", Json{{"weight", 7.0}, {"card", "list"}}},
      {"project_kind", Json{{"subject_kind", "application"}}}
    }}}));
    auto historic = principle_input();
    historic.erase("origin");
    const auto old = unwrap(Principle::from_json(historic));
    CHECK(old.confidence == 0.5);
    CHECK(old.validation == ValidationStatus::Candidate);
    CHECK(old.origin == Origin::Archive);
    CHECK(unwrap(normalize_creation("principle", principle_input(), custom)).record.at("confidence") == 0.8);
    CHECK(unwrap(Principle::from_json(historic)).to_json() == old.to_json());
    CHECK(unwrap(SlotSpec::from_json(slot_input())).weight == 1.0);
    CHECK(unwrap(SlotSpec::from_json(slot_input())).card == Cardinality::One);
    CHECK(unwrap(ProjectKind::from_json(project_input())).subject_kind == "project");
  }
}
