// Ordinary chat transport goes through the shared usage guard (audit CH-005).
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct GuardEnv {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  net::ScriptedTransport transport;
  GuardEnv() {
    secrets.set("api_key", "sk-test-key");
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
    cfg.set("auto_title", false);
  }
  ChatEngine engine() { return ChatEngine(cfg, secrets, *db, bus, transport, *analyzer); }
  void script(int n) {
    for (int i = 0; i < n; ++i)
      transport.expect("POST", "https://api.test/chat/completions",
          net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message",
              Json{{"role", "assistant"}, {"content", "ok"}}}}})}, {"usage", Json{{"completion_tokens", 3}}}}));
  }
  Json inspect() {
    auto policy = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", unwrap(effective_usage_policy_options(cfg))));
    return unwrap(policy->inspect("chat.send|https://api.test|test/model"));
  }
};
ChatOptions plain() { ChatOptions o; o.stream = false; return o; }
}  // namespace

TEST_SUITE("chat_usage_guard") {
  TEST_CASE("first send with an empty ledger is authorized and measured") {
    GuardEnv env;
    env.script(1);
    auto engine = env.engine();
    auto r = engine.send("hello", plain());
    REQUIRE(r);
    CHECK_FALSE(r->usage_pending.has_value());
    CHECK(env.transport.requests().size() == 1);
    const auto dump = env.inspect().dump();
    CHECK(dump.find("input_bytes") != std::string::npos);
    CHECK(dump.find("input_bytes") != std::string::npos);
  }

  TEST_CASE("x10 growth pauses, confirm sends once, reject cancels without sending") {
    GuardEnv env;
    env.script(4);
    auto engine = env.engine();
    for (int i = 0; i < 3; ++i) REQUIRE(engine.send("hi " + std::to_string(i), plain()));
    REQUIRE(env.transport.requests().size() == 3);
    const std::string big(60000, 'x');

    // Preview is read-only and shows the confirmation requirement.
    const auto before = env.inspect();
    auto preview = unwrap(engine.preview_usage(big, plain()));
    CHECK(preview.value("status", "") == "requires_confirmation");
    CHECK(env.inspect() == before);
    CHECK(env.transport.requests().size() == 3);

    auto paused = engine.send(big, plain());
    REQUIRE(paused);
    REQUIRE(paused->usage_pending.has_value());
    const Json pending = *paused->usage_pending;
    CHECK(pending["status"] == "requires_confirmation");
    CHECK(pending["sent"] == false);
    CHECK(env.transport.requests().size() == 3);  // nothing sent
    CHECK(paused->assistant_message_id.empty());

    // Reject: cancelled, still nothing sent.
    ChatOptions reject = plain();
    reject.usage_confirmation = Json{{"approved", false}, {"receipt_id", pending["receipt_id"]},
        {"ref", "test-reject"}, {"operation_id", pending["operation_id"]}};
    auto rejected = engine.send(big, reject);
    REQUIRE(rejected);
    REQUIRE(rejected->usage_pending.has_value());
    CHECK((*rejected->usage_pending)["status"] == "denied");
    CHECK(env.transport.requests().size() == 3);

    // A fresh pause then approve executes exactly once.
    auto again = engine.send(big, plain());
    REQUIRE(again);
    REQUIRE(again->usage_pending.has_value());
    const Json p2 = *again->usage_pending;
    ChatOptions approve = plain();
    approve.usage_confirmation = Json{{"approved", true}, {"receipt_id", p2["receipt_id"]},
        {"ref", "test-approve"}, {"operation_id", p2["operation_id"]}};
    auto sent = engine.send(big, approve);
    REQUIRE(sent);
    CHECK_FALSE(sent->usage_pending.has_value());
    CHECK(sent->text == "ok");
    CHECK(env.transport.requests().size() == 4);
    CHECK(env.inspect().dump().find("test-approve") != std::string::npos);
  }

  TEST_CASE("usage_confirmation parses from request JSON and rejects malformed values") {
    CHECK(ChatOptions::from_json(Json{{"usage_confirmation", Json{{"approved", true}}}}));
    CHECK_FALSE(ChatOptions::from_json(Json{{"usage_confirmation", "yes"}}));
  }
}
