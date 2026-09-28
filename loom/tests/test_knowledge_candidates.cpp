#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/knowledge_store.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("knowledge_candidates") {
  TEST_CASE("candidate reads isolate runs and kinds and expose pagination without promotion") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "db.sqlite");
    kb::KnowledgeStore store(*db);
    const auto run = unwrap(store.begin_run("pack", Json{{"source", "a"}})).id;
    const auto other = unwrap(store.begin_run("pack", Json{{"source", "b"}})).id;
    {
      auto lock = db->lock();
      for (const auto& id : {"one", "two", "other", "policy"}) {
        const auto owner = std::string(id) == "other" ? other : run;
        const auto kind = std::string(id) == "policy" ? "policy" : "semantic_structure";
        unwrap(db->conn().run("INSERT INTO loom_kb_candidates(id,kind,payload) VALUES(?,?,?)",
                             id, kind, json::dump(Json{{"run_id", owner}, {"draft_claim", Json{{"subject", "e_a"}}}})));
      }
    }
    auto first = unwrap(store.query_candidates(run, "semantic_structure", 1));
    CHECK(first["total"] == 2);
    REQUIRE(first["items"].size() == 1);
    CHECK(first["items"][0]["id"] == "one");
    CHECK(first["has_more"] == true);
    CHECK(first["items"][0]["status"] == "candidate");
    auto second = unwrap(store.query_candidates(run, "semantic_structure", 1, 1));
    CHECK(second["items"][0]["id"] == "two");
    CHECK(second["has_more"] == false);
    CHECK(unwrap(store.query_candidates(run))["total"] == 3);
    CHECK(unwrap(store.query_candidates(other))["total"] == 1);
    CHECK(unwrap(store.query_candidates(run, "", 1, 999))["items"].empty());
    CHECK(unwrap(store.stats(run))["claims"] == 0);
    CHECK(!store.query_candidates(run, "", 0));
    CHECK(!store.query_candidates(run, "", 1001));
    CHECK(!store.query_candidates(run, "", 1, -1));
  }
}
