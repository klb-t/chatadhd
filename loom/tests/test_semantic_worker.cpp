// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include <chrono>
#include <limits>
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
  TEST_CASE("CH-011 invalid graph preflight cannot dispatch or complete a pending message") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("preflight"));
    const auto mid = env.add_pending_msg(conv.id, "Synthetic contact preflight@example.invalid for analysis");
    env.cfg.set("semantic_model", "synthetic/model");
    env.secrets.set("api_key", "synthetic-not-a-credential");
    LOOM_REQUIRE_OK(fsutil::ensure_dir(env.td.path() / "profiles"));
    const auto path = env.td.path() / "profiles/graph_ingest.pack";
    auto overlay = [](const Json& overrides) {
      return Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_ingest"}, {"overrides", overrides}};
    };
    LOOM_REQUIRE_OK(fsutil::write_file(path, overlay(Json{{"min_input_codepoints", "invalid"}}).dump()));
    auto worker = env.worker();
    const auto failed = worker->drain_once();
    REQUIRE_FALSE(failed);
    CHECK(worker->status().processed == 0);
    CHECK(worker->status().errors == 1);
    CHECK(unwrap(env.db->get_msg(mid))->semantic_status == "pending");
    CHECK(env.transport.requests().empty());
    CHECK(unwrap(env.db->list_nodes()).empty());
    LOOM_REQUIRE_OK(fsutil::write_file(path, overlay(Json::object()).dump()));
    // Controlled transport returns a real error; SemanticLLM's established
    // successful regex fallback still completes, rather than becoming failed.
    env.transport.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Timeout, "synthetic timeout"));
    CHECK(unwrap(worker->drain_once()) == 1);
    CHECK(env.transport.requests().size() == 1);
    CHECK(unwrap(env.db->get_msg(mid))->semantic_status == "done");
    CHECK(unwrap(env.db->find_node("preflight@example.invalid")).has_value());
    const auto state = worker->status();
    REQUIRE(state.failed.has_value()); REQUIRE(state.executing.has_value());
    CHECK(*state.failed == 0); CHECK(*state.executing == 0); CHECK(state.counts_known);
  }

  TEST_CASE("CH-011 storage failures roll back graph and completion; repair needs explicit requeue after reopen") {
    for (const std::string target : {"nodes", "links", "done"}) {
      CAPTURE(target);
      Env env;
      const auto path = env.db->path();
      const auto conv = unwrap(env.db->create_conv("failed storage"));
      const auto mid = env.add_pending_msg(conv.id, "Synthetic storage storage@example.invalid result");
      {
        auto lock = env.db->lock();
        const std::string trigger = target == "done"
          ? "CREATE TRIGGER reject_analysis BEFORE UPDATE OF semantic_status ON messages WHEN NEW.semantic_status='done' BEGIN SELECT RAISE(ABORT, 'synthetic done failure'); END"
          : "CREATE TRIGGER reject_analysis BEFORE INSERT ON " + target + " BEGIN SELECT RAISE(ABORT, 'synthetic graph failure'); END";
        LOOM_REQUIRE_OK(env.db->conn().exec(trigger));
      }
      int events = 0;
      env.bus.on(events::kGraphChanged, [&](std::string_view, const Json&) { ++events; });
      auto worker = env.worker();
      CHECK(unwrap(worker->drain_once()) == 0);
      CHECK(worker->status().processed == 0);
      CHECK(worker->status().errors == 1);
      REQUIRE(worker->status().failed.has_value());
      CHECK(*worker->status().failed == 1);
      const auto message = *unwrap(env.db->get_msg(mid));
      CHECK(message.semantic_status == "failed");
      const auto history = message.metadata.at("loom_semantic_attempts");
      REQUIRE(history.size() == 1);
      CHECK(history[0]["state"] == "failed");
      CHECK(history[0]["error"]["code"] == "conflict");
      CHECK(history[0]["result"]["source"] == "regex");
      CHECK_FALSE(message.metadata.contains("semantic_source"));
      CHECK(unwrap(env.db->list_nodes()).empty());
      CHECK(unwrap(env.db->get_links()).empty());
      CHECK(events == 0);
      CHECK(env.transport.requests().empty());
      worker.reset(); env.db.reset();
      auto reopened = loom::test::open_db(path);
      GraphEngine graph(*reopened, env.bus, *env.analyzer);
      SemanticWorker resumed(*reopened, env.llm, graph, env.cfg, env.secrets, env.bus, env.transport, *env.analyzer);
      CHECK(unwrap(reopened->get_msg(mid))->metadata.at("loom_semantic_attempts") == history);
      CHECK(*resumed.status().failed == 1);
      {
        auto lock = reopened->lock();
        LOOM_REQUIRE_OK(reopened->conn().exec("DROP TRIGGER reject_analysis"));
      }
      resumed.resume();
      CHECK(unwrap(resumed.drain_once()) == 0); // unpause/repair alone is not retry
      MsgPatch requeue; requeue.semantic_status = "pending";
      LOOM_REQUIRE_OK(reopened->update_msg(mid, requeue));
      CHECK(unwrap(resumed.drain_once()) == 1);
      CHECK(unwrap(resumed.drain_once()) == 0);
      const auto complete = *unwrap(reopened->get_msg(mid));
      CHECK(complete.semantic_status == "done");
      REQUIRE(complete.metadata.at("loom_semantic_attempts").size() == 2);
      CHECK(complete.metadata.at("loom_semantic_attempts")[0] == history[0]);
      CHECK(complete.metadata.at("loom_semantic_attempts")[1]["state"] == "done");
      CHECK(unwrap(reopened->find_node("storage@example.invalid")).has_value());
      CHECK(events == 1);
    }
  }

  TEST_CASE("CH-011 unwritable failure bookkeeping retains durable executing and never dispatches after reopen") {
    Env env;
    const auto path = env.db->path();
    const auto conv = unwrap(env.db->create_conv("interrupted"));
    const auto mid = env.add_pending_msg(conv.id, "Synthetic incomplete held@example.invalid analysis");
    env.cfg.set("semantic_model", "synthetic/model");
    env.secrets.set("api_key", "synthetic-not-a-credential");
    env.transport.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Timeout, "synthetic offline timeout"));
    {
      auto lock = env.db->lock();
      LOOM_REQUIRE_OK(env.db->conn().exec(
        "CREATE TRIGGER reject_graph BEFORE INSERT ON links BEGIN SELECT RAISE(ABORT, 'synthetic link failure'); END;"
        "CREATE TRIGGER reject_failure BEFORE UPDATE OF semantic_status ON messages WHEN NEW.semantic_status='failed' "
        "BEGIN SELECT RAISE(ABORT, 'synthetic failure bookkeeping'); END"));
    }
    WorkerOptions options; options.llm_rate_limit = 0;
    auto worker = env.worker(options);
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(env.transport.requests().size() == 1);
    CHECK(worker->status().processed == 0);
    CHECK(worker->status().errors == 1);
    CHECK(*worker->status().executing == 1);
    const auto before = *unwrap(env.db->get_msg(mid));
    CHECK(before.semantic_status == "executing");
    CHECK(before.metadata.at("loom_semantic_attempts").back()["state"] == "executing");
    CHECK(unwrap(env.db->list_nodes()).empty());
    worker.reset(); env.db.reset();
    auto reopened = loom::test::open_db(path);
    GraphEngine graph(*reopened, env.bus, *env.analyzer);
    SemanticWorker resumed(*reopened, env.llm, graph, env.cfg, env.secrets, env.bus, env.transport, *env.analyzer);
    CHECK(unwrap(reopened->get_msg(mid))->metadata == before.metadata);
    CHECK(unwrap(resumed.drain_once()) == 0);
    CHECK(*resumed.status().executing == 1);
    CHECK(env.transport.requests().size() == 1);
  }

  TEST_CASE("CH-011 successful empty analysis remains done; unknown stored states are not complete") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("empty"));
    const auto mid = env.add_pending_msg(conv.id, "Synthetic plain greeting without extracted entities");
    auto worker = env.worker();
    CHECK(unwrap(worker->drain_once()) == 1);
    CHECK(unwrap(env.db->get_msg(mid))->semantic_status == "done");
    CHECK(unwrap(env.db->list_nodes()).empty());
    CHECK(worker->status().errors == 0);
    CHECK(worker->status().counts_known);
    MsgPatch unknown; unknown.semantic_status = "future_custom_state";
    LOOM_REQUIRE_OK(env.db->update_msg(mid, unknown));
    CHECK_FALSE(worker->status().counts_known);
    CHECK_FALSE(worker->status().to_json()["counts_known"].get<bool>());
    {
      auto lock = env.db->lock();
      LOOM_REQUIRE_OK(env.db->conn().exec("ALTER TABLE messages RENAME TO temporarily_unavailable_messages"));
    }
    const auto unavailable = worker->status().to_json();
    CHECK_FALSE(unavailable["counts_known"].get<bool>());
    CHECK(unavailable["failed"].is_null()); CHECK(unavailable["executing"].is_null());
    {
      auto lock = env.db->lock();
      LOOM_REQUIRE_OK(env.db->conn().exec("ALTER TABLE temporarily_unavailable_messages RENAME TO messages"));
    }
  }

  TEST_CASE("CH-011 transport observes durable claim and cannot change pinned graph recipe or overwrite newer source") {
    for (const std::string mutation : {"profile", "text", "excluded", "requeue", "new_attempt"}) {
      CAPTURE(mutation);
      Env env;
      const auto conv = unwrap(env.db->create_conv("controlled transport"));
      const std::string source = "Synthetic original original@example.invalid analysis";
      const auto mid = env.add_pending_msg(conv.id, source);
      env.cfg.set("semantic_model", "synthetic/model");
      env.cfg.set("base_url", "https://offline.invalid");
      env.secrets.set("api_key", "synthetic-not-a-credential");
      const auto pinned = unwrap(RuntimeProfile::builtin("graph_ingest"));
      std::optional<SemanticAttempt> newer;
      Json latest_metadata;
      struct ControlledTransport final : net::HttpTransport {
        std::function<void()> during;
        int requests = 0;
        Result<net::HttpResponse> send(const net::HttpRequest&, const net::StreamSink*, const CancelToken*) override {
          ++requests; during();
          const Json analysis{{"entities", Json::array()}, {"topics", Json::array()}, {"relations", Json::array()},
                              {"summary", "synthetic result"}, {"sentiment", "neutral"}};
          const Json response{{"choices", Json::array({Json{{"message", Json{{"content", analysis.dump()}}}}})}};
          return net::HttpResponse{200, {}, response.dump()};
        }
        std::string name() const override { return "controlled semantic fixture"; }
      } transport;
      transport.during = [&] {
        const auto claimed = *unwrap(env.db->get_msg(mid));
        REQUIRE(claimed.semantic_status == "executing");
        REQUIRE(claimed.metadata.at("loom_semantic_attempts").size() == 1);
        if (mutation == "profile") {
          LOOM_REQUIRE_OK(fsutil::ensure_dir(env.td.path() / "profiles"));
          LOOM_REQUIRE_OK(fsutil::write_file(env.td.path() / "profiles/graph_ingest.pack",
            Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_ingest"},
              {"overrides", {{"entities", {{"min_relevance", 2.0}}}}}}.dump()));
        } else {
          MsgPatch patch;
          if (mutation == "text") patch.text = "Synthetic newer edit newer@example.invalid analysis";
          else if (mutation == "excluded") patch.status = msg_status::kExcluded;
          else patch.semantic_status = "pending";
          LOOM_REQUIRE_OK(env.db->update_msg(mid, patch));
          if (mutation == "new_attempt") {
            newer = unwrap(env.graph.begin_analysis(mid, source, conv.id, pinned));
          }
          latest_metadata = unwrap(env.db->get_msg(mid))->metadata;
        }
      };
      SemanticLLM llm(env.cfg, env.secrets, transport, *env.analyzer);
      WorkerOptions options; options.llm_rate_limit = 0;
      SemanticWorker worker(*env.db, llm, env.graph, env.cfg, env.secrets, env.bus, transport, *env.analyzer, nullptr, options);
      CHECK(unwrap(worker.drain_once()) == (mutation == "profile" ? 1 : 0));
      CHECK(transport.requests == 1);
      const auto after = *unwrap(env.db->get_msg(mid));
      if (mutation == "profile") {
        CHECK(after.semantic_status == "done");
        CHECK(after.metadata.at("loom_semantic_attempts").back()["graph_profile"]["hash"] == pinned.hash());
        CHECK(unwrap(env.db->find_node("original@example.invalid")).has_value());
        CHECK(unwrap(RuntimeProfile::load("graph_ingest", env.td.path())).hash() != pinned.hash());
      } else {
        CHECK(after.metadata == latest_metadata);
        CHECK(after.semantic_status == (mutation == "requeue" ? "pending" : "executing"));
        CHECK(unwrap(env.db->list_nodes()).empty());
        CHECK(worker.status().errors == 1);
        if (mutation == "text") CHECK(after.text != source);
        if (mutation == "excluded") CHECK(after.status == msg_status::kExcluded);
        if (newer) CHECK(after.metadata.at("loom_semantic_attempts").back()["id"] == newer->id);
      }
    }
  }
  TEST_CASE("worker options are a preset; larger queues and zero waits are configurable") {
    auto builtin = unwrap(RuntimeProfile::builtin("worker"));
    auto profile = unwrap(builtin.with_overrides(Json{{"drain_batch", 750}, {"batch_max_messages", 20001},
      {"idle_poll_ms", 0}, {"startup_delay_ms", 0}, {"llm_rate_limit", 0.0},
      {"batch_endpoint", "https://offline.invalid/batches"}}));
    auto opts = unwrap(WorkerOptions::from_profile(profile));
    CHECK(opts.drain_batch == 750);
    CHECK(opts.batch_max_messages == 20001);
    CHECK(opts.idle_poll.count() == 0);
    CHECK(opts.startup_delay.count() == 0);
    CHECK(opts.llm_rate_limit == 0.0);
    CHECK(opts.batch_endpoint == "https://offline.invalid/batches");
    CHECK_FALSE(WorkerOptions::from_profile(unwrap(RuntimeProfile::builtin("media"))));
    CHECK_FALSE(builtin.with_overrides(Json{{"idle_poll_ms", std::numeric_limits<std::uint64_t>::max()}}));
    CHECK(builtin.values().at("failure_recovery") == "explicit_requeue");
    CHECK_FALSE(builtin.with_overrides(Json{{"failure_recovery", "automatic_retry"}}));
    Json forged_definition = builtin.definition();
    forged_definition["value_schema"] = Json{{"type", "object"}};
    forged_definition["defaults"]["drain_batch"] = "invalid";
    CHECK_FALSE(WorkerOptions::from_profile(unwrap(RuntimeProfile::from_definition(forged_definition))));
  }

  TEST_CASE("worker loads overlay while explicit legacy options win") {
    Env env;
    auto conv = unwrap(env.db->create_conv("profile"));
    for (int i = 0; i < 3; ++i) env.add_pending_msg(conv.id, "pending message for profile check");
    LOOM_REQUIRE_OK(fsutil::ensure_dir(env.td.path() / "profiles"));
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "worker"},
                 {"overrides", {{"drain_batch", 1}}}};
    LOOM_REQUIRE_OK(fsutil::write_file(env.td.path() / "profiles/worker.pack", overlay.dump()));
    SemanticWorker worker(*env.db, env.llm, env.graph, env.cfg, env.secrets,
                          env.bus, env.transport, *env.analyzer);
    CHECK(unwrap(worker.drain_once()) == 1);
    CHECK(unwrap(env.db->count_pending_semantic()) == 2);
    CHECK(unwrap(worker.runtime_profile())["is_builtin"] == false);
    WorkerOptions opts;
    opts.drain_batch = 2;
    auto explicit_worker = env.worker(opts);
    CHECK(unwrap(explicit_worker->drain_once()) == 2);
    CHECK(unwrap(explicit_worker->runtime_profile())["values"]["drain_batch"] == 2);
    CHECK(env.transport.requests().empty());
  }

  TEST_CASE("batch profile settings shape the offline submit request") {
    Env env;
    auto conv = unwrap(env.db->create_conv("batch"));
    auto msg_id = env.add_pending_msg(conv.id, "abcdefghijklmnopqrstuvwxyz");
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    env.secrets.set("local_batch_key", "fake-batch-key");
    env.transport.expect("POST", "https://offline.invalid/batches",
      net::ScriptedTransport::Reply::json(201, Json{{"id", "offline-batch"}}));
    Json overrides{{"batch_threshold", 0}, {"batch_endpoint", "https://offline.invalid/batches"},
      {"batch_secret_key", "local_batch_key"}, {"batch_output_tokens", 123}, {"batch_input_chars", 5},
      {"strip_model_provider", false}, {"submit_timeout_ms", 9}, {"headers", {{"X-Test", "yes"}}}};
    SemanticWorker worker(*env.db, env.llm, env.graph, env.cfg, env.secrets,
                          env.bus, env.transport, *env.analyzer, nullptr, {}, overrides);
    CHECK(unwrap(worker.drain_once()) == 0);
    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].timeout_ms == 9);
    CHECK(net::header_value(reqs[0].headers, "x-api-key") == "fake-batch-key");
    CHECK(net::header_value(reqs[0].headers, "X-Test") == "yes");
    auto body = unwrap(json::parse(reqs[0].body));
    REQUIRE(body["requests"].size() == 1);
    const auto& item = body["requests"][0];
    CHECK(item["custom_id"] == msg_id);
    CHECK(item["params"]["model"] == "vendor/model");
    CHECK(item["params"]["max_tokens"] == 123);
    const std::string content = item["params"]["messages"][0]["content"].get<std::string>();
    CHECK(content.ends_with("abcde"));
    CHECK(content.find("abcdefghijklmnopqrstuvwxyz") == std::string::npos);
    CHECK(worker.status().batch_id == "offline-batch");
    CHECK(unwrap(env.db->count_pending_semantic()) == 1);
  }

  TEST_CASE("invalid worker overlay prevents processing and transport") {
    Env env;
    auto conv = unwrap(env.db->create_conv("invalid-profile"));
    env.add_pending_msg(conv.id, "message stays pending for invalid overlay");
    LOOM_REQUIRE_OK(fsutil::ensure_dir(env.td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(env.td.path() / "profiles/worker.pack", "invalid"));
    auto worker = env.worker();
    CHECK_FALSE(worker->runtime_profile());
    CHECK_FALSE(worker->drain_once());
    CHECK(unwrap(env.db->count_pending_semantic()) == 1);
    CHECK(env.transport.requests().empty());
  }

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
