// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/semantic_worker.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  net::ScriptedTransport transport;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  SemanticLLM llm{cfg, secrets, transport, *analyzer};  // disabled: no semantic_model/api_key -> regex path
  GraphEngine graph{*db, bus, *analyzer};

  std::string add_pending_msg(const std::string& conv_id, const std::string& text) {
    NewMessage nm;
    nm.conv_id = conv_id;
    nm.text = text;
    nm.role = "user";
    return unwrap(db->create_msg(nm));
  }

  std::unique_ptr<SemanticWorker> worker(WorkerOptions opts = {}) {
    return std::make_unique<SemanticWorker>(*db, llm, graph, cfg, secrets, bus, transport, *analyzer, nullptr, opts);
  }
};

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}
}  // namespace

TEST_SUITE("semantic_worker") {
  TEST_CASE("status() reflects the pending count before any processing") {
    Env env;
    auto conv = unwrap(env.db->create_conv("c"));
    env.add_pending_msg(conv.id, "hello world this is pending");
    auto w = env.worker();
    auto s = w->status();
    CHECK(s.pending == 1);
    CHECK(!s.running);
    CHECK(!s.paused);
    CHECK(s.mode == "idle");
  }

  TEST_CASE("drain_once processes pending messages via the regex fallback") {
    Env env;
    auto conv = unwrap(env.db->create_conv("c"));
    env.add_pending_msg(conv.id, "message one is long enough");
    env.add_pending_msg(conv.id, "message two is long enough");
    int progress_events = 0;
    env.bus.on(events::kSemanticProgress, [&](std::string_view, const Json&) { ++progress_events; });

    auto w = env.worker();
    auto processed = unwrap(w->drain_once());
    CHECK(processed == 2);
    CHECK(progress_events == 1);
    CHECK(unwrap(env.db->count_pending_semantic()) == 0);
    auto s = w->status();
    CHECK(s.processed == 2);
    CHECK(s.mode == "regex");

    // Messages are flipped to 'done' with a regex analysis source recorded.
    auto msgs = unwrap(env.db->get_msgs(conv.id));
    for (const auto& m : msgs) {
      CHECK(m.semantic_status == "done");
    }
  }

  TEST_CASE("drain_once on an empty queue is a fast no-op") {
    Env env;
    auto w = env.worker();
    CHECK(unwrap(w->drain_once()) == 0);
  }

  TEST_CASE("start/stop: idempotent start, and stop() is prompt (no busy loop)") {
    Env env;
    WorkerOptions opts;
    opts.startup_delay = std::chrono::milliseconds(20);
    opts.idle_poll = std::chrono::seconds(30);  // would hang the test if stop() were not interruptible
    auto w = env.worker(opts);

    w->start();
    w->start();  // idempotent: must not spawn a second thread or crash
    CHECK(wait_until([&] { return w->status().running; }, std::chrono::seconds(2)));

    auto t0 = std::chrono::steady_clock::now();
    w->stop();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(!w->status().running);

    // stop() then start() again works (thread_ was properly joined/reset).
    w->start();
    CHECK(wait_until([&] { return w->status().running; }, std::chrono::seconds(2)));
    w->stop();
  }

  TEST_CASE("pause() stops draining; resume() continues it") {
    Env env;
    auto conv = unwrap(env.db->create_conv("c"));
    WorkerOptions opts;
    opts.startup_delay = std::chrono::milliseconds(5);
    opts.idle_poll = std::chrono::seconds(30);
    auto w = env.worker(opts);

    w->pause();
    w->start();
    CHECK(wait_until([&] { return w->status().paused; }, std::chrono::seconds(2)));

    env.add_pending_msg(conv.id, "queued while worker paused");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(w->status().pending == 1);  // untouched while paused

    w->resume();
    CHECK(wait_until([&] { return w->status().pending == 0; }, std::chrono::seconds(2)));
    CHECK(!w->status().paused);
    w->stop();
  }

  TEST_CASE("wake() interrupts a long idle wait promptly") {
    Env env;
    WorkerOptions opts;
    opts.startup_delay = std::chrono::milliseconds(5);
    opts.idle_poll = std::chrono::seconds(30);  // must not be waited out by the test
    auto w = env.worker(opts);

    w->start();
    // Let it reach idle (queue starts empty).
    CHECK(wait_until([&] { return w->status().mode == "idle"; }, std::chrono::seconds(2)));

    auto conv = unwrap(env.db->create_conv("c"));
    env.add_pending_msg(conv.id, "woken message goes here now");
    w->wake();

    CHECK(wait_until([&] { return w->status().processed >= 1; }, std::chrono::seconds(2)));
    w->stop();
  }

  TEST_CASE("import:done event wakes the worker") {
    Env env;
    WorkerOptions opts;
    opts.startup_delay = std::chrono::milliseconds(5);
    opts.idle_poll = std::chrono::seconds(30);
    auto w = env.worker(opts);
    w->start();
    CHECK(wait_until([&] { return w->status().mode == "idle"; }, std::chrono::seconds(2)));

    auto conv = unwrap(env.db->create_conv("c"));
    env.add_pending_msg(conv.id, "arrived via import event now");
    env.bus.emit(events::kImportDone, Json::object());

    CHECK(wait_until([&] { return w->status().processed >= 1; }, std::chrono::seconds(2)));
    w->stop();
  }

  TEST_CASE("status().to_json() has the documented keys and rate formatting") {
    WorkerStatus s;
    s.pending = 3;
    s.processed = 5;
    s.errors = 1;
    s.mode = "regex";
    s.rate = 2.5;
    s.running = true;
    Json j = s.to_json();
    CHECK(j["pending"] == 3);
    CHECK(j["processed"] == 5);
    CHECK(j["errors"] == 1);
    CHECK(j["mode"] == "regex");
    CHECK(j["rate"] == "2.5/s");
    CHECK(j["batch_id"].is_null());
    CHECK(j["running"] == true);
    CHECK(j["paused"] == false);
  }

  TEST_CASE("destructor stops a running worker cleanly") {
    Env env;
    WorkerOptions opts;
    opts.startup_delay = std::chrono::milliseconds(5);
    opts.idle_poll = std::chrono::seconds(30);
    {
      auto w = env.worker(opts);
      w->start();
      CHECK(wait_until([&] { return w->status().running; }, std::chrono::seconds(2)));
      // ~SemanticWorker() runs here and must join promptly.
    }
    CHECK(true);  // reaching here without hanging is the assertion
  }
}
