#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <future>

#include "loom/db.h"
#include "loom/relations.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("relations") {
  TEST_CASE("transaction and nested rollback never leave uncommitted relation cache entries") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "rollback.db");
    RelationRegistry reg(*db);
    CHECK(unwrap(reg.list()).empty()); // warm the committed empty cache
    {
      auto lock = db->lock();
      sql::Txn outer(db->conn());
      LOOM_REQUIRE_OK(outer.begin_status());
      LOOM_REQUIRE_OK(reg.seed_builtin());
      CHECK(unwrap(reg.get("mentions")).has_value());
      CHECK_FALSE(unwrap(reg.list()).empty());
      // rollback by destruction, including seed_builtin's nested savepoint
    }
    CHECK_FALSE(unwrap(reg.get("mentions")).has_value());
    CHECK(unwrap(reg.list()).empty());
    RelationType original;
    original.name = "synthetic_relation"; original.description = "committed";
    LOOM_REQUIRE_OK(reg.upsert(original));
    CHECK(unwrap(reg.get(original.name))->description == "committed");
    {
      auto lock = db->lock();
      sql::Txn outer(db->conn());
      LOOM_REQUIRE_OK(outer.begin_status());
      auto changed = original; changed.description = "outer";
      LOOM_REQUIRE_OK(reg.upsert(changed));
      CHECK(unwrap(reg.get(original.name))->description == "outer");
      {
        sql::Txn nested(db->conn());
        LOOM_REQUIRE_OK(nested.begin_status());
        changed.description = "nested";
        LOOM_REQUIRE_OK(reg.upsert(changed));
        CHECK(unwrap(reg.get(original.name))->description == "nested");
        CHECK(unwrap(reg.ensure("synthetic_new")).name == "synthetic_new");
      }
      CHECK(unwrap(reg.get(original.name))->description == "outer");
      CHECK_FALSE(unwrap(reg.get("synthetic_new")).has_value());
      const auto rows = unwrap(reg.list());
      CHECK(std::none_of(rows.begin(), rows.end(), [](const RelationType& row) { return row.name == "synthetic_new"; }));
      unwrap(reg.ensure("synthetic_new"));
      CHECK(unwrap(db->conn().query_int("SELECT COUNT(*) FROM loom_relation_types WHERE name='synthetic_new'")).value() == 1);
    }
    CHECK(unwrap(reg.get(original.name))->description == "committed");
    CHECK_FALSE(unwrap(reg.get("synthetic_new")).has_value());
    unwrap(reg.ensure("synthetic_new"));
    {
      auto lock = db->lock();
      CHECK(unwrap(db->conn().query_int("SELECT COUNT(*) FROM loom_relation_types WHERE name='synthetic_new'")).value() == 1);
    }
    RelationRegistry reopened(*db);
    CHECK(unwrap(reopened.get("synthetic_new")).has_value());
    CHECK(unwrap(reopened.get(original.name))->description == "committed");
  }

  TEST_CASE("failed SQL with partial trigger effects invalidates a warm relation cache") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "partial.db");
    RelationRegistry reg(*db);
    RelationType original;
    original.name = "sentinel"; original.description = "before";
    LOOM_REQUIRE_OK(reg.upsert(original));
    CHECK(unwrap(reg.get("sentinel"))->description == "before");
    {
      auto lock = db->lock();
      LOOM_REQUIRE_OK(db->conn().exec(
        "CREATE TRIGGER registry_partial BEFORE INSERT ON loom_relation_types WHEN NEW.name='broken' "
        "BEGIN UPDATE loom_relation_types SET description='partial effect' WHERE name='sentinel'; "
        "SELECT RAISE(FAIL, 'synthetic registry failure'); END"));
    }
    RelationType broken; broken.name = "broken";
    auto result = reg.upsert(broken);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Conflict);
    CHECK(unwrap(reg.get("sentinel"))->description == "partial effect");
    const auto rows = unwrap(reg.list());
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].description == "partial effect");
  }

  TEST_CASE("warm relation reads take the database lock before consulting their cache") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "ordering.db");
    RelationRegistry reg(*db);
    unwrap(reg.ensure("synthetic_lock"));
    unwrap(reg.list());
    for (bool list : {false, true}) {
      CAPTURE(list);
      std::promise<void> started;
      auto began = started.get_future();
      std::future<bool> read;
      {
        auto lock = db->lock();
        read = std::async(std::launch::async, [&] {
          started.set_value();
          if (list) return !unwrap(reg.list()).empty();
          return unwrap(reg.get("synthetic_lock")).has_value();
        });
        began.wait();
        // Release Database before joining even when this CHECK fails. This
        // detects the old warm-cache bypass without constructing a deadlock.
        CHECK(read.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
      }
      CHECK(read.get());
    }
  }

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
