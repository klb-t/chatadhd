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
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
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

  TEST_CASE("golden: worker batch submit, poll and results request bytes are pinned") {
    Env env;
    auto conv = unwrap(env.db->create_conv("golden batch"));
    auto msg_id = env.add_pending_msg(conv.id, "golden batch message");
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    env.secrets.set("anthropic_batch_key", "fake-batch-key");
    const std::string batches = "https://api.anthropic.com/v1/messages/batches";
    env.transport.expect("POST", batches, net::ScriptedTransport::Reply::json(200, Json{{"id", "golden-batch"}}));
    env.transport.expect("GET", batches + "/golden-batch",
        net::ScriptedTransport::Reply::json(200, Json{{"processing_status", "ended"},
            {"request_counts", Json{{"succeeded", 0}, {"errored", 0}}}}));
    env.transport.expect("GET", batches + "/golden-batch/results", net::ScriptedTransport::Reply::text(200, ""));
    SemanticWorker worker(*env.db, env.llm, env.graph, env.cfg, env.secrets, env.bus, env.transport, *env.analyzer,
                          nullptr, {}, Json{{"batch_threshold", 0}});
    CHECK(unwrap(worker.drain_once()) == 0);   // submit
    worker.start();                            // the loop polls, then ingests the ended batch
    REQUIRE(wait_until([&] { return env.transport.requests().size() >= 3; }, std::chrono::seconds(5)));
    worker.stop();
    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 3);
    const Json key_headers = Json::array({Json::array({"x-api-key", "fake-batch-key"}),
                                          Json::array({"anthropic-version", "2023-06-01"})});
    Json submit_headers = key_headers;
    submit_headers.push_back(Json::array({"content-type", "application/json"}));
    const std::string content = std::string(kAnalysisPrompt) + "golden batch message";
    CHECK(loom::test::request_json(reqs[0]) ==
          Json{{"method", "POST"}, {"url", batches}, {"headers", submit_headers},
               {"body", "{\"requests\":[{\"custom_id\":\"" + msg_id + "\",\"params\":{\"model\":\"model\","
                        "\"max_tokens\":800,\"messages\":[{\"role\":\"user\",\"content\":" + json::dump(Json(content)) + "}]}}]}"},
               {"timeout_ms", 120000}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[1]) ==
          Json{{"method", "GET"}, {"url", batches + "/golden-batch"}, {"headers", key_headers},
               {"body", ""}, {"timeout_ms", 30000}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[2]) ==
          Json{{"method", "GET"}, {"url", batches + "/golden-batch/results"}, {"headers", key_headers},
               {"body", ""}, {"timeout_ms", 300000}, {"stream", true}});
  }

  // CH-005: the automatic batch submission goes through the shared usage guard.
  namespace {
  Json batch_overrides() {
    return Json{{"batch_threshold", 0}, {"batch_endpoint", "https://offline.invalid/batches"},
                {"batch_secret_key", "local_batch_key"}};
  }
  void prepare_batch_env(Env& env) {
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    env.secrets.set("local_batch_key", "fake-batch-key");
    for (int i = 0; i < 6; ++i)
      env.transport.expect("POST", "https://offline.invalid/batches",
          net::ScriptedTransport::Reply::json(201, Json{{"id", "offline-batch"}}));
  }
  std::unique_ptr<SemanticWorker> batch_worker(Env& env) {
    return std::make_unique<SemanticWorker>(*env.db, env.llm, env.graph, env.cfg, env.secrets, env.bus,
                                            env.transport, *env.analyzer, nullptr, std::optional<WorkerOptions>{}, batch_overrides());
  }
  void build_batch_baseline(Env& env, const std::string& conv) {
    env.add_pending_msg(conv, "small baseline message");
    for (int i = 0; i < 3; ++i) CHECK(unwrap(batch_worker(env)->drain_once()) == 0);
    REQUIRE(env.transport.requests().size() == 3);
    for (int i = 0; i < 60; ++i) env.add_pending_msg(conv, "growth message " + std::to_string(i) + std::string(200, 'g'));
  }
  }  // namespace

  TEST_CASE("CH-005 first batch submit with an empty ledger is authorized") {
    Env env;
    prepare_batch_env(env);
    env.add_pending_msg(unwrap(env.db->create_conv("first")).id, "the only message in the ledger");
    auto worker = batch_worker(env);
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(env.transport.requests().size() == 1);
    CHECK(worker->status().batch_id == "offline-batch");
    CHECK_FALSE(worker->status().usage_pending.has_value());
  }

  TEST_CASE("CH-005 x10 batch growth awaits confirmation, keeps messages pending, then submits on approval") {
    Env env;
    prepare_batch_env(env);
    const auto conv = unwrap(env.db->create_conv("growth")).id;
    build_batch_baseline(env, conv);
    auto worker = batch_worker(env);
    CHECK(unwrap(worker->drain_once()) == 0);
    auto status = worker->status();
    CHECK(status.mode == "awaiting_usage_confirmation");
    REQUIRE(status.usage_pending.has_value());
    CHECK(status.to_json()["usage_pending"]["status"] == "requires_confirmation");
    CHECK(env.transport.requests().size() == 3);  // nothing sent
    CHECK(unwrap(env.db->count_pending_semantic()) == 61);
    CHECK(unwrap(worker->drain_once()) == 0);     // no retry storm
    CHECK(env.transport.requests().size() == 3);
    const auto receipt = (*status.usage_pending)["receipt_id"].get<std::string>();
    CHECK_FALSE(worker->confirm_usage(true, "wrong-receipt", "t"));
    REQUIRE(worker->confirm_usage(true, receipt, "test-approve"));
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(env.transport.requests().size() == 4);
    status = worker->status();
    CHECK(status.batch_id == "offline-batch");
    CHECK_FALSE(status.usage_pending.has_value());
    CHECK(unwrap(env.db->count_pending_semantic()) == 61);
  }

  TEST_CASE("CH-005 rejected batch usage pauses the worker and sends nothing") {
    Env env;
    prepare_batch_env(env);
    const auto conv = unwrap(env.db->create_conv("reject")).id;
    build_batch_baseline(env, conv);
    auto worker = batch_worker(env);
    CHECK(unwrap(worker->drain_once()) == 0);
    const auto receipt = (*worker->status().usage_pending)["receipt_id"].get<std::string>();
    REQUIRE(worker->confirm_usage(false, receipt, "test-reject"));
    CHECK(worker->status().paused);
    CHECK_FALSE(worker->status().usage_pending.has_value());
    CHECK(env.transport.requests().size() == 3);
    CHECK(unwrap(env.db->count_pending_semantic()) == 61);
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

  // ── T2 / WP-C: retry modes, bulk requeue, queue jam ───────────────────
  namespace {
  void set_done_rejection(Env& env, bool on) {
    auto lock = env.db->lock();
    if (on) LOOM_REQUIRE_OK(env.db->conn().exec("CREATE TRIGGER reject_done BEFORE UPDATE OF semantic_status ON messages "
        "WHEN NEW.semantic_status='done' BEGIN SELECT RAISE(ABORT, 'synthetic done failure'); END"));
    else LOOM_REQUIRE_OK(env.db->conn().exec("DROP TRIGGER reject_done"));
  }
  std::unique_ptr<SemanticWorker> retry_worker(Env& env, Json overrides) {
    return std::make_unique<SemanticWorker>(*env.db, env.llm, env.graph, env.cfg, env.secrets, env.bus,
                                            env.transport, *env.analyzer, nullptr, std::optional<WorkerOptions>{}, overrides);
  }
  const std::string kText = "Synthetic retry contact retry@example.invalid analysis";
  // An executing claim whose owner worker no longer exists (restarted process).
  void stale_claim(Env& env, const std::string& mid, const std::string& conv_id) {
    auto profile = unwrap(RuntimeProfile::builtin("graph_ingest"));
    unwrap(env.graph.begin_analysis(mid, kText, conv_id, profile, Json::object(), false, std::nullopt, "w_dead_worker"));
  }
  }  // namespace

  TEST_CASE("T2 explicit_requeue (default preset) never resends a failed item on its own") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("explicit"));
    const auto mid = env.add_pending_msg(conv.id, kText);
    set_done_rejection(env, true);
    auto worker = env.worker();
    CHECK(unwrap(worker->drain_once()) == 0);
    set_done_rejection(env, false);
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(*worker->status().failed == 1);
    CHECK(worker->status().retry_mode == "explicit_requeue");
    CHECK(unwrap(env.db->get_msg(mid))->metadata.at("loom_semantic_attempts").size() == 1);
  }

  TEST_CASE("T2 automatic mode requeues failed items with no ceiling; history is kept; spacing is a preset") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("automatic"));
    const auto mid = env.add_pending_msg(conv.id, kText);
    set_done_rejection(env, true);
    auto worker = retry_worker(env, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 0}});
    // Many failing rounds: every one is retried, nothing stops the loop.
    for (int i = 0; i < 6; ++i) CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(unwrap(env.db->get_msg(mid))->metadata.at("loom_semantic_attempts").size() == 6);
    set_done_rejection(env, false);
    CHECK(unwrap(worker->drain_once()) == 1);
    const auto done = *unwrap(env.db->get_msg(mid));
    CHECK(done.semantic_status == "done");
    const auto& attempts = done.metadata.at("loom_semantic_attempts");
    REQUIRE(attempts.size() == 7);
    CHECK(attempts[0]["state"] == "failed");
    CHECK(attempts[0]["requeued"]["reason"] == "automatic");
    CHECK(attempts[6]["state"] == "done");
    CHECK(attempts[6]["provenance"]["retry_mode"] == "automatic");
    CHECK(env.transport.requests().empty());  // regex path: nothing reaches a provider, no guard needed

    // A long spacing preset holds the resend back (no ceiling, just spacing).
    Env slow;
    const auto conv2 = unwrap(slow.db->create_conv("spacing"));
    slow.add_pending_msg(conv2.id, kText);
    set_done_rejection(slow, true);
    auto held = retry_worker(slow, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 3600000}});
    CHECK(unwrap(held->drain_once()) == 0);
    CHECK(unwrap(held->drain_once()) == 0);
    CHECK(*held->status().failed == 1);
  }

  TEST_CASE("T2 automatic resend of an LLM item passes the usage guard (x10 growth asks the owner)") {
    Env env;
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    Json content{{"entities", Json::array()}, {"topics", Json::array()}, {"relations", Json::array()}};
    env.transport.set_fallback(net::ScriptedTransport::Reply::json(200,
        Json{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}}));
    const auto conv = unwrap(env.db->create_conv("guarded")).id;
    set_done_rejection(env, true);
    auto worker = retry_worker(env, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 0}, {"llm_rate_limit", 0}});
    env.add_pending_msg(conv, "small baseline message number 0");
    for (int i = 0; i < 4; ++i) CHECK(unwrap(worker->drain_once()) == 0);  // 3 admitted resends build the baseline
    CHECK_FALSE(worker->status().usage_pending.has_value());
    for (int i = 0; i < 60; ++i) env.add_pending_msg(conv, "growth message " + std::to_string(i) + std::string(200, 'g'));
    for (int i = 0; i < 3 && !worker->status().usage_pending; ++i) CHECK(unwrap(worker->drain_once()) == 0);
    auto status = worker->status();
    REQUIRE(status.usage_pending.has_value());
    CHECK((*status.usage_pending)["kind"] == "retry");
    CHECK(status.usage_pending->at("status") == "requires_confirmation");
    const auto failed_before = *status.failed;
    const auto requests_before = env.transport.requests().size();
    CHECK(unwrap(worker->drain_once()) == 0);                 // the rest of the queue still drains; nothing is resent
    status = worker->status();                                // the failed set may have grown: the receipt follows it
    REQUIRE(status.usage_pending.has_value());
    const auto receipt = (*status.usage_pending)["receipt_id"].get<std::string>();
    const auto failed_now = *status.failed;
    const auto requests_now = env.transport.requests().size();
    CHECK(unwrap(worker->drain_once()) == 0);                 // unchanged set: no re-ask storm, nothing resent
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK((*worker->status().usage_pending)["receipt_id"] == receipt);
    CHECK(*worker->status().failed == failed_now);
    CHECK(env.transport.requests().size() == requests_now);
    CHECK(failed_now >= failed_before);
    (void)requests_before;
    set_done_rejection(env, false);
    CHECK_FALSE(worker->confirm_usage(true, "wrong-receipt", "t"));
    auto approved = worker->confirm_usage(true, receipt, "test-approve");
    const std::string why = approved ? std::string() : approved.error().message;
    INFO(why);
    REQUIRE(approved);
    CHECK(unwrap(worker->drain_once()) > 0);                  // confirmed: requeued and re-analysed
    CHECK_FALSE(worker->status().usage_pending.has_value());
  }

  TEST_CASE("golden: automatic resend guard request identity is pinned") {
    Env env;
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    Json content{{"entities", Json::array()}, {"topics", Json::array()}, {"relations", Json::array()}};
    env.transport.set_fallback(net::ScriptedTransport::Reply::json(200,
        Json{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}}));
    const auto conv = unwrap(env.db->create_conv("golden")).id;
    set_done_rejection(env, true);
    auto worker = retry_worker(env, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 0}, {"llm_rate_limit", 0}});
    const auto mid = env.add_pending_msg(conv, "golden resend message number 0");
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(unwrap(worker->drain_once()) == 0);
    auto policy = unwrap(UsagePolicy::open(env.td.path() / "usage-policy.sqlite",
                                           unwrap(effective_usage_policy_options(env.cfg))));
    const std::string key = "semantic.retry|https://openrouter.ai/api/v1/chat/completions|vendor/model";
    auto cohort = unwrap(policy->inspect(key));
    REQUIRE(cohort["operations"].size() == 1);
    // The guard binds method, URL, header list, body, timeout and stream flag.
    const Json identity{{"method", "POST"}, {"url", "https://openrouter.ai/api/v1/chat/completions"},
                        {"headers", Json::array()},
                        {"body", json::dump(Json{{"schema", "loom.semantic_retry/1"}, {"ids", Json::array({mid})}})},
                        {"timeout_ms", net::builtin_http_timeout_ms()}, {"stream", false}};
    const auto& estimate = cohort["operations"][0]["estimate"];
    CHECK(estimate["request_identity_sha256"] == Sha256::hex(json::canonical(identity)));
    CHECK(estimate["request_body_sha256"] == Sha256::hex(identity["body"].get<std::string>()));
  }

  TEST_CASE("T2 declining an automatic resend answers that operation only: setting kept, items noted usage_declined, queue keeps draining") {
    Env env;
    env.cfg.set("semantic_model", "vendor/model");
    env.secrets.set("api_key", "fake-model-key");
    Json content{{"entities", Json::array()}, {"topics", Json::array()}, {"relations", Json::array()}};
    env.transport.set_fallback(net::ScriptedTransport::Reply::json(200,
        Json{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}}));
    const auto conv = unwrap(env.db->create_conv("declined")).id;
    set_done_rejection(env, true);
    auto worker = retry_worker(env, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 0}, {"llm_rate_limit", 0}});
    env.add_pending_msg(conv, "small baseline message number 0");
    for (int i = 0; i < 4; ++i) CHECK(unwrap(worker->drain_once()) == 0);
    for (int i = 0; i < 60; ++i) env.add_pending_msg(conv, "growth message " + std::to_string(i) + std::string(200, 'g'));
    for (int i = 0; i < 3 && !worker->status().usage_pending; ++i) CHECK(unwrap(worker->drain_once()) == 0);
    auto status = worker->status();
    REQUIRE(status.usage_pending.has_value());
    const std::int64_t declined = status.usage_pending->at("messages").get<std::int64_t>();  // the declined operation
    REQUIRE(worker->confirm_usage(false, (*status.usage_pending)["receipt_id"].get<std::string>(), "test-reject"));
    CHECK(worker->status().retry_mode == "automatic");  // a "no" never changes the user's setting
    CHECK_FALSE(worker->status().usage_pending.has_value());
    CHECK_FALSE(worker->status().paused);
    set_done_rejection(env, false);
    const auto fresh = env.add_pending_msg(conv, "Synthetic fresh row fresh@example.invalid analysis");
    const auto requests_before = env.transport.requests().size();
    CHECK(unwrap(worker->drain_once()) >= 1);          // everything else keeps being processed
    CHECK(unwrap(env.db->get_msg(fresh))->semantic_status == "done");
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(*worker->status().failed == declined);        // only the declined items stay failed, not resent
    CHECK_FALSE(worker->status().usage_pending.has_value());
    CHECK(env.transport.requests().size() > requests_before);  // other items did reach analysis
    int noted = 0;
    for (const auto& m : unwrap(env.db->get_msgs(conv))) {
      if (m.semantic_status != "failed") continue;
      if (m.metadata.at("loom_semantic_attempts").back().contains("usage_declined")) ++noted;
    }
    CHECK(noted == declined);
    // Explicit requeue is the user's call and works for declined items.
    CHECK(unwrap(worker->requeue())["requeued"] == declined);
  }

  TEST_CASE("T2 bulk requeue: filters, counts of known numbers, affected ids, history preserved") {
    Env env;
    const auto conv_a = unwrap(env.db->create_conv("a")).id;
    const auto conv_b = unwrap(env.db->create_conv("b")).id;
    const auto fa = env.add_pending_msg(conv_a, kText);
    const auto fb = env.add_pending_msg(conv_b, kText);
    const auto st = env.add_pending_msg(conv_a, kText);
    set_done_rejection(env, true);
    auto worker = env.worker();
    // st is claimed by a dead worker instead of being analysed
    stale_claim(env, st, conv_a);
    CHECK(unwrap(worker->drain_once()) == 0);  // fa, fb fail
    set_done_rejection(env, false);
    auto status = worker->status();
    CHECK(*status.failed == 2);
    CHECK(*status.executing == 1);
    CHECK(status.counts_known);
    // Dry run changes nothing.
    auto dry = unwrap(worker->requeue(Json{{"dry_run", true}}));
    CHECK(dry["matched"] == 3);
    CHECK(dry["requeued"] == 0);
    CHECK(unwrap(env.db->get_msg(fa))->semantic_status == "failed");
    // Narrow filter: one conversation, only failed.
    auto one = unwrap(worker->requeue(Json{{"conv_id", conv_a}, {"stalled", false}}));
    CHECK(one["requeued"] == 1);
    CHECK(one["failed_requeued"] == 1);
    CHECK(one["stalled_requeued"] == 0);
    CHECK(one["ids"] == Json::array({fa}));
    // error_class that nothing has: matches nothing.
    CHECK(unwrap(worker->requeue(Json{{"error_class", "timeout"}}))["matched"] == 0);
    // Everything else (failed fb + stalled st).
    auto rest = unwrap(worker->requeue());
    CHECK(rest["requeued"] == 2);
    CHECK(rest["failed_requeued"] == 1);
    CHECK(rest["stalled_requeued"] == 1);
    CHECK(unwrap(worker->requeue())["matched"] == 0);
    CHECK_FALSE(worker->requeue(Json{{"nonsense", 1}}));
    // History of the earlier attempts stays, annotated.
    const auto stalled_row = *unwrap(env.db->get_msg(st));
    CHECK(stalled_row.semantic_status == "pending");
    REQUIRE(stalled_row.metadata.at("loom_semantic_attempts").size() == 1);
    CHECK(stalled_row.metadata.at("loom_semantic_attempts")[0]["state"] == "stalled");
    CHECK(stalled_row.metadata.at("loom_semantic_attempts")[0]["requeued"]["reason"] == "bulk_requeue");
    CHECK(unwrap(worker->drain_once()) == 3);
    for (const auto& id : {fa, fb, st}) CHECK(unwrap(env.db->get_msg(id))->semantic_status == "done");
    CHECK(unwrap(env.db->get_msg(fa))->metadata.at("loom_semantic_attempts").size() == 2);
    CHECK(unwrap(env.db->get_msg(fa))->metadata.at("loom_semantic_attempts")[0]["state"] == "failed");
  }

  TEST_CASE("T2 jam: a stalled executing claim is reported, never silently retried, never blocks the queue") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("jam")).id;
    const auto dead = env.add_pending_msg(conv, kText);
    stale_claim(env, dead, conv);
    const auto live = env.add_pending_msg(conv, "Synthetic live contact live@example.invalid analysis");
    auto worker = env.worker();
    auto status = worker->status();
    CHECK(*status.executing == 1);
    REQUIRE(status.stalled.has_value());
    CHECK(*status.stalled == 1);
    CHECK(status.to_json()["stalled"] == 1);
    CHECK(unwrap(worker->drain_once()) == 1);  // the other item is processed
    CHECK(unwrap(env.db->get_msg(live))->semantic_status == "done");
    CHECK(unwrap(env.db->get_msg(dead))->semantic_status == "executing");  // not silently retried
    CHECK(unwrap(worker->drain_once()) == 0);
    CHECK(*worker->status().stalled == 1);
    // A fresh claim from the synchronous graph path (owner cannot be probed) is not stalled until its lease expires.
    auto profile = unwrap(RuntimeProfile::builtin("graph_ingest"));
    const auto fresh = env.add_pending_msg(conv, "Synthetic fresh claim fresh@example.invalid analysis");
    unwrap(env.graph.begin_analysis(fresh, "Synthetic fresh claim fresh@example.invalid analysis", conv, profile));
    CHECK(*worker->status().executing == 2);
    CHECK(*worker->status().stalled == 1);
    auto expired = retry_worker(env, Json{{"executing_lease_ms", 0}});
    CHECK(*expired->status().stalled == 2);
    // Explicit requeue picks the stalled one up.
    auto requeued = unwrap(worker->requeue());
    CHECK(requeued["stalled_requeued"] == 1);
    CHECK(requeued["ids"] == Json::array({dead}));
    CHECK(unwrap(worker->drain_once()) == 1);
    CHECK(unwrap(env.db->get_msg(dead))->semantic_status == "done");
  }

  TEST_CASE("T2 jam: under automatic mode a stalled claim is requeued like a failed one") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("jam auto")).id;
    const auto dead = env.add_pending_msg(conv, kText);
    stale_claim(env, dead, conv);
    auto worker = retry_worker(env, Json{{"retry_mode", "automatic"}, {"retry_spacing_ms", 0}});
    CHECK(unwrap(worker->drain_once()) == 1);
    const auto row = *unwrap(env.db->get_msg(dead));
    CHECK(row.semantic_status == "done");
    CHECK(row.metadata.at("loom_semantic_attempts")[0]["state"] == "stalled");
    CHECK(row.metadata.at("loom_semantic_attempts")[0]["requeued"]["reason"] == "automatic");
  }

  TEST_CASE("T2 jam: a pending row with unreadable attempt history no longer stops the whole drain") {
    Env env;
    const auto conv = unwrap(env.db->create_conv("jam corrupt")).id;
    const auto bad = env.add_pending_msg(conv, "Synthetic oldest row with broken history bad@example.invalid");
    MsgPatch broken; broken.metadata = Json{{"loom_semantic_attempts", "unrecognized historical data"}, {"keep", 17}};
    LOOM_REQUIRE_OK(env.db->update_msg(bad, broken));
    const auto good = env.add_pending_msg(conv, "Synthetic later row good@example.invalid analysis");
    auto worker = env.worker();
    CHECK(unwrap(worker->drain_once()) == 1);                 // before: the oldest row aborted every drain with Conflict
    CHECK(unwrap(env.db->get_msg(good))->semantic_status == "done");
    const auto kept = *unwrap(env.db->get_msg(bad));
    CHECK(kept.semantic_status == "pending");                 // data retained, visible, not hidden as done
    CHECK(kept.metadata.at("keep") == 17);
    CHECK(worker->status().quarantined == 1);
    CHECK(unwrap(worker->drain_once()) == 0);                 // and it does not spin
  }
}
