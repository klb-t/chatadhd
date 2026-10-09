#include <doctest/doctest.h>

#include "context/ctx_common.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"
#include "loom/loom.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using test::unwrap;

namespace {
struct ContextRunFixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  ContextRunFixture() { reopen(); }
  void reopen() {
    runtime.reset();
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
  }
  std::string run(int index, const Json& summary, std::string_view created,
                  std::string_view status = "done") {
    auto& store = runtime->knowledge().store();
    const auto id = unwrap(store.begin_run("safe-context-fixture", Json{{"index", index}})).id;
    LOOM_REQUIRE_OK(store.finish_run(id, status, summary));
    LOOM_REQUIRE_OK(runtime->db().conn().run("UPDATE loom_kb_runs SET created = ? WHERE run_id = ?", created, id));
    return id;
  }
  Json query(const Json& request) {
    const auto options = Json{{"data_dir", directory.path().string()}, {"start_workers", false}}.dump();
    auto* context = loom_init_ex(options.c_str(), nullptr);
    REQUIRE(context != nullptr);
    auto* response = loom_kb_query(context, request.dump().c_str());
    REQUIRE(response != nullptr);
    auto decoded = json::parse(response);
    loom_free_string(response);
    loom_shutdown(context);
    REQUIRE(decoded.has_value());
    return *decoded;
  }
};
}  // namespace

TEST_SUITE("knowledge_context_runs") {
  TEST_CASE("explicitly ineligible completed runs do not displace legacy context before or after reopen") {
    ContextRunFixture fixture;
    const auto legacy = fixture.run(0, Json{{"legacy", true}}, "2000-01-01T00:00:00Z");
    std::string excluded;
    // More than the historical list default. Filtering must precede the result
    // limit, not inspect a fixed-size window of the most recent receipts.
    for (int index = 1; index <= 73; ++index)
      excluded = fixture.run(index, Json{{"implicit_context_eligible", false}}, "2026-01-01T00:00:00Z");
    auto& store = fixture.runtime->knowledge().store();
    auto selected = unwrap(store.list_context_runs(1, "done"));
    REQUIRE(selected.size() == 1);
    CHECK(selected[0].id == legacy);
    CHECK(unwrap(ctx::resolve_run(store, "")) == legacy);
    CHECK(unwrap(ctx::resolve_run(store, excluded)) == excluded);
    CHECK(unwrap(store.list_runs(100, "done")).size() == 74);
    CHECK(unwrap(store.get_run(excluded))->summary.at("implicit_context_eligible") == false);
    fixture.reopen();
    auto& reopened = fixture.runtime->knowledge().store();
    REQUIRE(unwrap(reopened.list_context_runs(1, "done")).size() == 1);
    CHECK(unwrap(reopened.list_context_runs(1, "done"))[0].id == legacy);
    CHECK(fixture.query(Json{{"what", "stats"}}).at("run") == legacy);
    CHECK(fixture.query(Json{{"what", "stats"}, {"run", excluded}}).at("run") == excluded);
    const auto opted_in = fixture.run(74, Json{{"implicit_context_eligible", true}}, "2026-02-01T00:00:00Z");
    CHECK(unwrap(reopened.list_context_runs(1, "done"))[0].id == opted_in);
    CHECK(fixture.query(Json{{"what", "stats"}}).at("run") == opted_in);
  }

  TEST_CASE("eligibility flags are strict booleans and malformed persisted flags never mean missing") {
    ContextRunFixture fixture;
    const auto legacy = fixture.run(0, Json::object(), "2000-01-01T00:00:00Z");
    const auto newer = fixture.run(1, Json{{"implicit_context_eligible", false}}, "2026-01-01T00:00:00Z");
    auto& store = fixture.runtime->knowledge().store();
    for (const auto& invalid : {Json(1), Json(0), Json("true"), Json(nullptr), Json::array(), Json::object()}) {
      const auto outcome = store.finish_run(newer, "done", Json{{"implicit_context_eligible", invalid}});
      REQUIRE_FALSE(outcome.has_value());
      CHECK(outcome.error().code == Errc::InvalidArgument);
      CHECK(unwrap(store.get_run(newer))->summary.at("implicit_context_eligible") == false);
      // Persisted historical/corrupt input is also checked; no truthiness or
      // integer/string coercion may promote it into an implicit context.
      LOOM_REQUIRE_OK(fixture.runtime->db().conn().run("UPDATE loom_kb_runs SET summary = ? WHERE run_id = ?",
          Json{{"implicit_context_eligible", invalid}}.dump(), newer));
      const auto selected = unwrap(store.list_context_runs(1, "done"));
      REQUIRE(selected.size() == 1);
      CHECK(selected[0].id == legacy);
      LOOM_REQUIRE_OK(store.finish_run(newer, "done", Json{{"implicit_context_eligible", false}}));
    }
    LOOM_REQUIRE_OK(fixture.runtime->db().conn().run("UPDATE loom_kb_runs SET summary = ? WHERE run_id = ?",
        "{invalid-json", newer));
    CHECK(unwrap(store.list_context_runs(1, "done"))[0].id == legacy);
    fixture.reopen();
    CHECK(unwrap(fixture.runtime->knowledge().store().list_context_runs(1, "done"))[0].id == legacy);
  }

  TEST_CASE("no eligible completed run preserves only the existing eligible unfinished fallback") {
    ContextRunFixture fixture;
    const auto excluded = fixture.run(0, Json{{"implicit_context_eligible", false}}, "2026-01-01T00:00:00Z");
    auto& store = fixture.runtime->knowledge().store();
    CHECK(unwrap(store.list_context_runs(1)).empty());
    CHECK_FALSE(ctx::resolve_run(store, "").has_value());
    const auto none = fixture.query(Json{{"what", "stats"}});
    CHECK(none.at("error").at("code") == "not_found");
    CHECK(fixture.query(Json{{"what", "stats"}, {"run", excluded}}).at("run") == excluded);
    const auto unfinished = fixture.run(1, Json::object(), "2000-01-01T00:00:00Z", "running");
    CHECK(fixture.query(Json{{"what", "stats"}}).at("run") == unfinished);
    CHECK_FALSE(ctx::resolve_run(store, "").has_value()); // context engine still requires done
  }
}
