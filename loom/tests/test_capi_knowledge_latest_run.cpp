#include <doctest/doctest.h>

#include "loom/knowledge.h"
#include "loom/knowledge_store.h"
#include "loom/loom.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct QueryFixture {
  fsutil::TempDir dir;
  std::unique_ptr<Runtime> writer;
  LoomContext* reader = nullptr;
  std::string pack_hash;

  QueryFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    writer = unwrap(Runtime::open(options));
    pack_hash = unwrap(writer->knowledge().pack())->hash();
  }

  ~QueryFixture() { if (reader) loom_shutdown(reader); }

  std::string run(int index, std::string_view status, std::string_view created) {
    auto& store = writer->knowledge().store();
    const auto id = unwrap(store.begin_run(pack_hash, Json{{"fixture", index}})).id;
    if (status != "running") LOOM_REQUIRE_OK(store.finish_run(id, status, Json::object()));
    auto lock = writer->db().lock();
    LOOM_REQUIRE_OK(writer->db().conn().run("UPDATE loom_kb_runs SET created = ? WHERE run_id = ?",
                                          created, id));
    return id;
  }

  Json query(Json request = Json{{"what", "stats"}}) {
    if (!reader) {
      writer.reset();
      const auto options = json::dump(Json{{"data_dir", dir.path().string()}, {"start_workers", false}});
      reader = loom_init_ex(options.c_str(), nullptr);
      REQUIRE(reader);
    }
    const char* response = loom_kb_query(reader, json::dump(request).c_str());
    REQUIRE(response);
    const auto result = json::parse_or(response, Json(nullptr));
    loom_free_string(response);
    return result;
  }
};
}  // namespace

TEST_SUITE("capi_knowledge_latest_run") {
  TEST_CASE("completed run remains default behind more than fifty unfinished runs after reopen") {
    QueryFixture f;
    const auto completed = f.run(0, "done", "2000-01-01T00:00:00Z");
    std::string unfinished;
    for (int index = 1; index <= 51; ++index)
      unfinished = f.run(index, "running", "2026-01-01T00:00:00Z");
    CHECK(f.query()["run"] == completed);
    CHECK(f.query(Json{{"what", "stats"}, {"run", unfinished}})["run"] == unfinished);
  }

  TEST_CASE("newest completed wins and no-completed store retains newest-run fallback") {
    SUBCASE("newest completed") {
      QueryFixture f;
      f.run(0, "done", "2000-01-01T00:00:00Z");
      const auto newest = f.run(1, "done", "2001-01-01T00:00:00Z");
      f.run(2, "failed", "2002-01-01T00:00:00Z");
      CHECK(f.query()["run"] == newest);
    }
    SUBCASE("no completed") {
      QueryFixture f;
      f.run(0, "failed", "2000-01-01T00:00:00Z");
      const auto newest = f.run(1, "running", "2001-01-01T00:00:00Z");
      CHECK(f.query()["run"] == newest);
    }
  }
}
