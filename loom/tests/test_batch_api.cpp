// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "loom/batch_api.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
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
  net::ScriptedTransport transport;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());

  Env() {
    cfg.set("semantic_model", "cheap/model");
    secrets.set("api_key", "sk-test");
  }

  std::string add_msg(const std::string& conv_id, const std::string& text) {
    NewMessage nm;
    nm.conv_id = conv_id;
    nm.text = text;
    nm.role = "user";
    return unwrap(db->create_msg(nm));
  }

  SemanticBatchAPI api() { return SemanticBatchAPI(cfg, secrets, *db, transport, *analyzer); }
};

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}
}  // namespace

TEST_SUITE("batch_api") {
  TEST_CASE("parse_analysis strips fences and stamps source=llm") {
    auto a = SemanticBatchAPI::parse_analysis("```json\n{\"entities\":[]}\n```");
    REQUIRE(a.has_value());
    CHECK((*a)["entities"].is_array());
    CHECK((*a)["source"] == "llm");
    CHECK(!SemanticBatchAPI::parse_analysis("not json").has_value());
  }

  TEST_CASE("batch_regex analyses pending messages without a 'relations' key (Python quirk)") {
    Env env;
    auto conv = unwrap(env.db->create_conv("c"));
    env.add_msg(conv.id, "this is a long enough message for the regex batch");
    env.add_msg(conv.id, "another sufficiently long message here too");
    auto api = env.api();
    auto n = unwrap(api.batch_regex(100));
    CHECK(n == 2);
    CHECK(unwrap(env.db->count_pending_semantic()) == 0);
    auto msgs = unwrap(env.db->get_msgs(conv.id));
    for (const auto& m : msgs) {
      CHECK(m.semantic_status == "done");
      CHECK(m.metadata["semantic_source"] == "regex");
    }
  }

  TEST_CASE("batch_regex marks blank-text messages as skipped") {
    Env env;
    auto conv = unwrap(env.db->create_conv("c"));
    NewMessage nm;
    nm.conv_id = conv.id;
    nm.text = std::string(25, ' ');  // passes the length>=20 filter, blank after strip()
    nm.role = "user";
    std::string mid = unwrap(env.db->create_msg(nm));
    auto api = env.api();
    auto n = unwrap(api.batch_regex(100));
    CHECK(n == 0);  // Python's count isn't incremented on the skip branch either
    auto m = unwrap(env.db->get_msg(mid));
    REQUIRE(m.has_value());
    CHECK(m->semantic_status == "done");
  }

  TEST_CASE("submit_anthropic_batch: concurrent path (non-Anthropic base_url)") {
    Env env;
    env.cfg.set("base_url", "https://api.test");
    auto conv = unwrap(env.db->create_conv("c"));
    std::string m1 = env.add_msg(conv.id, "concurrent batch message one text");
    std::string m2 = env.add_msg(conv.id, "concurrent batch message two text");

    Json llm_message{{"content", "{\"entities\":[],\"topics\":[]}"}};
    Json llm_choice{{"message", llm_message}};
    Json llm_reply{{"choices", Json::array({llm_choice})}};
    env.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, llm_reply));
    env.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, llm_reply));

    auto api = env.api();
    auto id = unwrap(api.submit_anthropic_batch({m1, m2}));
    REQUIRE(id.has_value());
    CHECK(id->rfind("concurrent_", 0) == 0);

    REQUIRE(wait_until([&] { return json::get_string(api.check_batch(*id), "status") == "completed"; }));
    auto applied = unwrap(api.collect_results(*id));
    CHECK(applied == 2);

    auto msg1 = unwrap(env.db->get_msg(m1));
    REQUIRE(msg1.has_value());
    CHECK(msg1->semantic_status == "done");
  }

  TEST_CASE("submit_anthropic_batch: native Anthropic path submits and records the batch") {
    Env env;
    env.cfg.set("base_url", "https://api.anthropic.com");
    auto conv = unwrap(env.db->create_conv("c"));
    std::string m1 = env.add_msg(conv.id, "native anthropic batch message text");

    env.transport.expect("POST", "https://api.anthropic.com/v1/messages/batches",
                         net::ScriptedTransport::Reply::json(200, Json{{"id", "batch_abc123"}}));

    auto api = env.api();
    auto id = unwrap(api.submit_anthropic_batch({m1}));
    REQUIRE(id.has_value());
    CHECK(*id == "batch_abc123");

    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(net::header_value(reqs[0].headers, "x-api-key") == "sk-test");
    CHECK(net::header_value(reqs[0].headers, "anthropic-version") == "2023-06-01");
    Json body = unwrap(json::parse(reqs[0].body));
    REQUIRE(body["requests"].is_array());
    CHECK(body["requests"][0]["custom_id"] == m1);
    CHECK(body["requests"][0]["params"]["model"] == "cheap/model");

    Json batches = api.active_batches();
    CHECK(batches["batch_abc123"]["type"] == "anthropic");
  }

  TEST_CASE("check_batch and collect_results poll and stream the native Anthropic API") {
    Env env;
    env.cfg.set("base_url", "https://api.anthropic.com");
    auto conv = unwrap(env.db->create_conv("c"));
    std::string m1 = env.add_msg(conv.id, "native anthropic batch message text");
    env.transport.expect("POST", "https://api.anthropic.com/v1/messages/batches",
                         net::ScriptedTransport::Reply::json(200, Json{{"id", "batch_xyz"}}));
    auto api = env.api();
    auto id = unwrap(api.submit_anthropic_batch({m1}));
    REQUIRE(id.has_value());

    Json request_counts{{"succeeded", 1}, {"errored", 0}, {"processing", 0}};
    Json poll_reply{{"processing_status", "ended"},
                    {"request_counts", request_counts},
                    {"results_url", "https://api.anthropic.com/v1/messages/batches/batch_xyz/results"}};
    env.transport.expect("GET", "https://api.anthropic.com/v1/messages/batches/batch_xyz",
                         net::ScriptedTransport::Reply::json(200, poll_reply));
    Json status = api.check_batch(*id);
    CHECK(status["status"] == "ended");
    CHECK(status["succeeded"] == 1);

    // collect_results() re-polls status itself (matches Python's
    // _check_anthropic_batch call inside collect_results), so a second poll
    // reply is needed even though check_batch() already consumed the first.
    env.transport.expect("GET", "https://api.anthropic.com/v1/messages/batches/batch_xyz",
                         net::ScriptedTransport::Reply::json(200, poll_reply));
    std::string jsonl =
        std::string("{\"custom_id\":\"") + m1 +
        "\",\"result\":{\"type\":\"succeeded\",\"message\":{\"content\":[{\"text\":\"{\\\"entities\\\":[]}\"}]}}}\n";
    env.transport.expect("GET", "https://api.anthropic.com/v1/messages/batches/batch_xyz/results",
                         net::ScriptedTransport::Reply::text(200, jsonl));
    auto applied = unwrap(api.collect_results(*id));
    CHECK(applied == 1);
    auto m = unwrap(env.db->get_msg(m1));
    REQUIRE(m.has_value());
    CHECK(m->semantic_status == "done");
  }

  TEST_CASE("submit_anthropic_batch fails gracefully without a model or key") {
    Env env;
    env.cfg.set("semantic_model", "");
    auto api = env.api();
    auto id = unwrap(api.submit_anthropic_batch({"m_doesnotexist"}));
    CHECK(!id.has_value());
  }

  TEST_CASE("check_batch on an unknown id") {
    Env env;
    auto api = env.api();
    Json s = api.check_batch("nope");
    CHECK(s["status"] == "unknown");
  }
}
