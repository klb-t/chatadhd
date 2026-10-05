#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/runtime_profile.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {
StatusRecord record(int version, StatusValue status) {
  StatusRecord out;
  out.id = "synthetic-status-" + std::to_string(version);
  out.entity = "synthetic-component";
  out.version = "1.0." + std::to_string(version);
  out.status = status;
  return out;
}
Json records_json(const std::vector<StatusRecord>& records) {
  Json out = Json::array();
  for (const auto& row : records) out.push_back(row.to_json());
  return out;
}
}

TEST_SUITE("model.profile") {
  TEST_CASE("builtin authority ranks preserve exact old values and noexcept compatibility") {
    static_assert(noexcept(authority_rank(Origin::User)));
    const auto profile = unwrap(RuntimeProfile::builtin("model"));
    const std::array<int, count<Origin>()> historical{4, 4, 5, 3, 1, 2};
    for (const auto origin : all<Origin>()) {
      const auto expected = historical[static_cast<std::size_t>(origin)];
      CHECK(authority_rank(origin) == expected);
      CHECK(unwrap(authority_rank(origin, profile)) == expected);
    }
    CHECK(authority_rank(static_cast<Origin>(999)) == 0);
    CHECK(unwrap(authority_rank(static_cast<Origin>(999), profile)) == 0);
  }

  TEST_CASE("injected authority policy changes rank without rewriting provenance or premise rules") {
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    const auto profile = unwrap(builtin.with_overrides(Json{{"authority_ranks", Json{{"model_knowledge", 999}, {"user", -100}}},
                                                          {"unknown_origin_rank", -7}}));
    Entity entity;
    entity.id = "synthetic-entity";
    entity.kind = "concept";
    entity.canonical_key = "synthetic";
    entity.origin = Origin::ModelKnowledge;
    entity.evidence = EvidenceClass::Extrapolated;
    const auto before = entity.to_json();
    CHECK(unwrap(authority_rank(entity.origin, profile)) == 999);
    CHECK(unwrap(authority_rank(Origin::User, profile)) == -100);
    CHECK(unwrap(authority_rank(static_cast<Origin>(999), profile)) == -7);
    CHECK(entity.to_json() == before);
    CHECK(entity.origin == Origin::ModelKnowledge);
    CHECK(entity.evidence == EvidenceClass::Extrapolated);
    CHECK_FALSE(may_be_premise(entity.evidence));
    CHECK(authority_rank(Origin::ModelKnowledge) == 1);
  }

  TEST_CASE("builtin status history preserves ordering previous values and historical oscillation flags") {
    const auto profile = unwrap(RuntimeProfile::builtin("model"));
    const std::vector<StatusRecord> input{record(5, StatusValue::Restored), record(1, StatusValue::Implemented),
                                         record(3, StatusValue::Restored), record(2, StatusValue::Lost), record(4, StatusValue::Lost)};
    const auto legacy = order_status_history(input);
    const auto checked = unwrap(order_status_history(input, profile));
    CHECK(json::dump(records_json(checked)) == json::dump(records_json(legacy)));
    REQUIRE(legacy.size() == 5);
    CHECK_FALSE(legacy[0].previous);
    CHECK(legacy[1].previous == StatusValue::Implemented);
    CHECK(legacy[2].previous == StatusValue::Lost);
    CHECK(legacy[3].previous == StatusValue::Restored);
    CHECK(legacy[4].previous == StatusValue::Lost);
    CHECK_FALSE(legacy[0].oscillation);
    CHECK_FALSE(legacy[1].oscillation);
    CHECK_FALSE(legacy[2].oscillation);
    CHECK(legacy[3].oscillation);
    CHECK(legacy[4].oscillation);
    CHECK(records_json(order_status_history(legacy)) == records_json(legacy));
  }

  TEST_CASE("status classes are recipe data and recorded statuses are preserved") {
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    const auto profile = unwrap(builtin.with_overrides(Json{{"status_history", Json{
      {"present", Json::array({"planned"})}, {"lost", Json::array({"abandoned"})}}}}));
    const std::vector<StatusRecord> input{record(1, StatusValue::Planned), record(2, StatusValue::Abandoned),
                                         record(3, StatusValue::Planned), record(4, StatusValue::Abandoned), record(5, StatusValue::Planned)};
    const auto original = order_status_history(input);
    const auto changed = unwrap(order_status_history(input, profile));
    REQUIRE(changed.size() == input.size());
    for (std::size_t i = 0; i < changed.size(); ++i) {
      CHECK_FALSE(original[i].oscillation);
      CHECK(changed[i].status == input[i].status);
      CHECK(changed[i].id == input[i].id);
      CHECK(changed[i].oscillation == (i >= 3));
    }
    const auto cleared = unwrap(profile.with_overrides(Json{{"status_history", Json{{"present", Json::array()},
                                                                                    {"lost", Json::array()}}}}));
    for (const auto& row : unwrap(order_status_history(input, cleared))) CHECK_FALSE(row.oscillation);
  }

  TEST_CASE("anchoring rationale is editable while identities confidence and role bindings remain exact") {
    const auto profile = unwrap(RuntimeProfile::builtin("model"));
    const auto pack = unwrap(kb::Pack::load_builtin());
    const auto before = unwrap(anchoring_morphisms(*pack));
    const auto builtin = unwrap(anchoring_morphisms(*pack, profile));
    REQUIRE_FALSE(before.empty());
    REQUIRE(builtin.size() == before.size());
    const auto custom = unwrap(profile.with_overrides(Json{{"anchoring_rationale", "{{paradigm}}: {{kind}} -> {{role}}"}}));
    const auto after = unwrap(anchoring_morphisms(*pack, custom));
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
      CHECK(before[i].to_json() == builtin[i].to_json());
      CHECK(before[i].rationale == "domain kind '" + before[i].from.kind + "' plays the universal role '" +
                                    std::string(to_string(*before[i].to.role)) + "'");
      CHECK(after[i].rationale == before[i].from.paradigm + ": " + before[i].from.kind + " -> " +
                                   std::string(to_string(*before[i].to.role)));
      CHECK(after[i].id == "m.anchor." + before[i].from.paradigm + "." + before[i].from.kind);
      CHECK(after[i].confidence == 1.0);
      auto old_fields = before[i].to_json(), new_fields = after[i].to_json();
      old_fields.erase("rationale");
      new_fields.erase("rationale");
      CHECK(new_fields == old_fields);
    }
    const auto invalid = unwrap(profile.with_overrides(Json{{"anchoring_rationale", "{{missing}}"}}));
    CHECK_FALSE(anchoring_morphisms(*pack, invalid));
  }

  TEST_CASE("all checked model APIs reject foreign or permissive incompatible profiles") {
    const auto pack = unwrap(kb::Pack::load_builtin());
    const auto foreign = unwrap(RuntimeProfile::builtin("memory"));
    CHECK_FALSE(authority_rank(Origin::User, foreign));
    CHECK_FALSE(order_status_history({}, foreign));
    CHECK_FALSE(anchoring_morphisms(*pack, foreign));
    const Json definition{{"schema", "loom.runtime_profile/1"}, {"domain", "model"}, {"revision", 1},
                          {"defaults", Json{{"authority_ranks", Json{{"user", "wrong"}}}}},
                          {"value_schema", Json{{"type", "object"}}}};
    const auto permissive = unwrap(RuntimeProfile::from_definition(definition));
    CHECK_FALSE(authority_rank(Origin::User, permissive));
    CHECK_FALSE(order_status_history({}, permissive));
    CHECK_FALSE(anchoring_morphisms(*pack, permissive));
    const auto builtin = unwrap(RuntimeProfile::builtin("model"));
    CHECK_FALSE(builtin.with_overrides(Json{{"authority_ranks", Json{{"user", std::numeric_limits<std::uint64_t>::max()}}}}));
    CHECK_FALSE(builtin.with_overrides(Json{{"status_history", Json{{"present", Json::array({"imaginary"})}}}}));
  }
}
