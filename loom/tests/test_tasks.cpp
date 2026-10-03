#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "loom/tasks.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("tasks") {
  TEST_CASE("submit, run_sync, result hashes, events") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "t.db");
    EventLog log(*db);
    EventBus bus;
    std::vector<std::string> statuses;
    bus.on(events::kTaskChanged, [&](std::string_view, const Json& d) { statuses.push_back(d["status"]); });
    TaskEngine eng(*db, log, &bus);
    eng.register_handler("sum", [](TaskContext& ctx) -> Status {
      std::int64_t s = 0;
      for (const auto& v : ctx.params()["values"]) s += v.get<std::int64_t>();
      ctx.progress(1, 1, "summed");
      ctx.set_result(Json{{"sum", s}});
      return {};
    });
    std::string id = unwrap(eng.submit("sum", Json{{"values", Json::array({1, 2, 3})}}));
    CHECK(id.rfind("t_", 0) == 0);
    auto rec = unwrap(eng.run_sync(id));
    CHECK(rec.status == "done");
    CHECK(rec.attempts == 1);
    CHECK((*rec.result)["sum"] == 6);
    CHECK(rec.input_hash.size() == 64);
    CHECK(rec.output_hash.size() == 64);
    CHECK(statuses == std::vector<std::string>{"pending", "running", "done"});
    auto evs = unwrap(log.query());
    REQUIRE(evs.size() == 3);
    CHECK(evs[2].type == "task:done");
    CHECK(evs[2].output_hash == rec.output_hash);

    // Idempotent submit returns the existing task for identical params.
    SubmitOptions dd;
    dd.dedupe = true;
    CHECK(unwrap(eng.submit("sum", Json{{"values", Json::array({1, 2, 3})}}, dd)) == id);
    CHECK(unwrap(eng.submit("sum", Json{{"values", Json::array({1, 2})}}, dd)) != id);
    CHECK(unwrap(eng.list()).size() == 2);
    TaskFilter f;
    f.status = "done";
    CHECK(unwrap(eng.list(f)).size() == 1);
    CHECK(!eng.run_sync("t_missing"));
  }

  TEST_CASE("retries until max_attempts, then failed; resume resets") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "t.db");
    EventLog log(*db);
    TaskEngine eng(*db, log);
    int calls = 0;
    eng.register_handler("flaky", [&](TaskContext& ctx) -> Status {
      calls++;
      if (ctx.params()["succeed_on"].get<int>() == calls) return {};
      return Error(Errc::Network, "try again");
    });
    SubmitOptions o;
    o.max_attempts = 3;
    auto ok = unwrap(eng.run_sync(unwrap(eng.submit("flaky", Json{{"succeed_on", 2}}, o))));
    CHECK(ok.status == "done");
    CHECK(ok.attempts == 2);
    calls = 0;
    std::string id = unwrap(eng.submit("flaky", Json{{"succeed_on", 99}}, o));
    auto failed = unwrap(eng.run_sync(id));
    CHECK(failed.status == "failed");
    CHECK(failed.attempts == 3);
    CHECK(failed.error.find("try again") != std::string::npos);
    LOOM_REQUIRE_OK(eng.resume(id));
    CHECK(unwrap(eng.get(id))->status == "pending");
    CHECK(unwrap(eng.get(id))->attempts == 0);
  }

  TEST_CASE("crash-resume from checkpoint") {
    fsutil::TempDir td;
    auto path = td.path() / "t.db";
    std::vector<int> processed;
    std::string id;
    {
      // Process 1: submits, claims and processes steps 0..3, checkpointing
      // after each, then dies (the row stays 'running' exactly as a killed
      // process leaves it).
      auto db = open_db(path);
      EventLog log(*db);
      TaskEngine eng(*db, log);
      id = unwrap(eng.submit("count", Json{{"n", 10}}));
      LOOM_REQUIRE_OK(db->conn().run("UPDATE loom_tasks SET status='running', attempts=1 WHERE id=?", id));
      for (int i = 0; i < 4; ++i) {
        processed.push_back(i);
        LOOM_REQUIRE_OK(
            db->conn().run("UPDATE loom_tasks SET checkpoint=? WHERE id=?", json::py_dumps(Json{{"next", i + 1}}), id));
      }
      CHECK(unwrap(eng.get(id))->status == "running");
    }
    {
      // Process 2: recovers and resumes from checkpoint {"next": 4}.
      auto db = open_db(path);
      EventLog log(*db);
      TaskEngine eng(*db, log);
      eng.register_handler("count", [&](TaskContext& ctx) -> Status {
        int start = ctx.checkpoint() ? (*ctx.checkpoint())["next"].get<int>() : 0;
        int n = ctx.params()["n"].get<int>();
        for (int i = start; i < n; ++i) {
          processed.push_back(i);
          LOOM_TRY(ctx.save_checkpoint(Json{{"next", i + 1}}));
        }
        ctx.set_result(Json{{"done", n}});
        return {};
      });
      CHECK(unwrap(eng.recover_interrupted()) == 1);
      auto pending = unwrap(eng.get(id));
      CHECK(pending->status == "pending");
      CHECK(pending->attempts == 0);  // interrupted run not counted
      CHECK((*pending->checkpoint)["next"] == 4);
      auto done = unwrap(eng.run_sync(id));
      CHECK(done.status == "done");
      CHECK((*done.checkpoint)["next"] == 10);
    }
    CHECK(processed == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
  }

  TEST_CASE("cancel / pause / resume and handler exceptions") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "t.db");
    EventLog log(*db);
    TaskEngine eng(*db, log);
    eng.register_handler("boom", [](TaskContext&) -> Status { throw std::runtime_error("kaboom"); });
    SubmitOptions once;
    once.max_attempts = 1;
    auto r = unwrap(eng.run_sync(unwrap(eng.submit("boom", Json::object(), once))));
    CHECK(r.status == "failed");
    CHECK(r.error.find("kaboom") != std::string::npos);

    std::string p = unwrap(eng.submit("nohandler"));
    LOOM_REQUIRE_OK(eng.pause(p));
    CHECK(unwrap(eng.get(p))->status == "paused");
    LOOM_REQUIRE_OK(eng.resume(p));
    CHECK(unwrap(eng.get(p))->status == "pending");
    CHECK(!eng.run_sync(p));  // no handler registered
    LOOM_REQUIRE_OK(eng.cancel(p));
    CHECK(unwrap(eng.get(p))->status == "cancelled");
    LOOM_REQUIRE_OK(eng.cancel(p));  // idempotent
    CHECK(!eng.resume(p));
    CHECK(!eng.cancel("t_missing"));
  }

  TEST_CASE("worker pool runs tasks; running tasks honour cancel and shutdown") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "t.db");
    EventLog log(*db);
    EventBus bus;
    TaskEngine eng(*db, log, &bus);
    std::atomic<int> done{0};
    eng.register_handler("quick", [&](TaskContext& ctx) -> Status {
      ctx.set_result(ctx.params());
      done++;
      return {};
    });
    std::atomic<bool> long_started{false};
    eng.register_handler("long", [&](TaskContext& ctx) -> Status {
      long_started = true;
      for (int i = 0; i < 2000; ++i) {
        if (ctx.cancelled()) return Error(Errc::Cancelled, "stopped");
        if (ctx.pause_requested()) return Error(Errc::Paused, "yield");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return {};
    });
    eng.start(3);
    for (int i = 0; i < 20; ++i) unwrap(eng.submit("quick", Json{{"i", i}}));
    std::string lid = unwrap(eng.submit("long"));
    for (int i = 0; i < 500 && (done.load() < 20 || !long_started.load()); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(done.load() == 20);
    REQUIRE(long_started.load());
    LOOM_REQUIRE_OK(eng.cancel(lid));
    for (int i = 0; i < 300 && unwrap(eng.get(lid))->status == "running"; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(unwrap(eng.get(lid))->status == "cancelled");

    // Shutdown while a long task runs: it yields back to pending.
    long_started = false;
    std::string lid2 = unwrap(eng.submit("long"));
    for (int i = 0; i < 300 && !long_started.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(long_started.load());
    eng.stop();
    CHECK(unwrap(eng.get(lid2))->status == "pending");
    CHECK(!eng.running());
    TaskFilter f;
    f.kind = "quick";
    f.status = "done";
    CHECK(unwrap(eng.list(f)).size() == 20);
  }
}
