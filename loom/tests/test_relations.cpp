#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/relations.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("relations") {
  TEST_CASE("built-ins seeded with inverse/symmetry; unknown types auto-registered") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "r.db");
    RelationRegistry reg(*db);
    LOOM_REQUIRE_OK(reg.seed_builtin());
    const char* required[] = {"mentions", "depends_on", "references", "reply_to", "part_of", "generated_by",
                              "tagged_with", "derived_from", "contradicts", "implements", "related", "related_to",
                              "created_by", "supersedes", "superseded_by", "alternative_to", "forked_from",
                              "decided_in", "answers", "asks"};
    for (const char* n : required) {
      INFO(n);
      CHECK(unwrap(reg.get(n)).has_value());
    }
    CHECK(unwrap(reg.list()).size() == builtin_relation_types().size());
    CHECK(reg.inverse_of("supersedes") == "superseded_by");
    CHECK(reg.inverse_of("superseded_by") == "supersedes");
    CHECK(reg.is_symmetric("contradicts"));
    CHECK(!reg.is_symmetric("depends_on"));
    CHECK(unwrap(reg.get("depends_on"))->transitive);
    CHECK(unwrap(reg.list("temporal")).size() == 3);

    auto custom = unwrap(reg.ensure("inspired_by"));
    CHECK(custom.category == "custom");
    CHECK(unwrap(reg.get("inspired_by")).has_value());
    CHECK(reg.inverse_of("unknown_rel").empty());

    // User edits survive re-seeding.
    RelationType t = *unwrap(reg.get("mentions"));
    t.description = "edited";
    LOOM_REQUIRE_OK(reg.upsert(t));
    LOOM_REQUIRE_OK(reg.seed_builtin());
    CHECK(unwrap(reg.get("mentions"))->description == "edited");

    LOOM_REQUIRE_OK(reg.load(Json::array({Json{{"name", "blocks"}, {"inverse", "blocked_by"}, {"category", "semantic"}}})));
    CHECK(reg.inverse_of("blocks") == "blocked_by");
    CHECK(!reg.load(Json::object()));
    CHECK(!RelationType::from_json(Json::object()));

    // Persisted: a fresh registry sees the same data.
    RelationRegistry again(*db);
    CHECK(unwrap(again.get("blocks")).has_value());
  }
}
