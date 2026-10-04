// W3 scoped regressions. Build with the source-local verification runner.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/selector.h"
#include "loom/util/sha256.h"
#include "context/context_execution.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct SelectorFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::string run;
  SelectorFixture(bool knowledge = true) {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = transport;
    runtime = unwrap(Runtime::open(options));
    runtime->config().set("base_url", "https://selector.test");
    runtime->config().set("default_model", "fixture/chat");
    runtime->config().set("semantic_model", "fixture/typing");
    runtime->config().set("system_prompt", "Stable system prefix.");
    runtime->config().set("semantic_analysis", false);
    runtime->secrets().set("api_key", "synthetic-credential");
    if (knowledge) {
      auto pack = unwrap(runtime->knowledge().pack());
      auto& store = runtime->knowledge().store();
      run = unwrap(store.begin_run(pack->hash(), Json::object())).id;
      model::Principle principle;
      principle.id = "pr.selector_fixture";
      principle.statement = {{"en", "Always preserve recorded source bytes."}};
      principle.form = model::PrincipleForm::Invariant;
      principle.level = model::PrincipleLevel::Strategy;
      principle.validation = model::ValidationStatus::Confirmed;
      principle.confidence = 1;
      LOOM_REQUIRE_OK(store.put_principles(run, {principle}));
      LOOM_REQUIRE_OK(store.finish_run(run, "done", Json::object()));
    }
  }
  void chat_reply() {
    transport->expect("POST", "https://selector.test/chat/completions", net::ScriptedTransport::Reply::json(
        200, Json{{"choices", Json::array({Json{{"message", {{"content", "fixture response"}}}}})},
                  {"usage", {{"prompt_tokens", 10}, {"completion_tokens", 2}}}}));
  }
};
Json execution_options() {
  return Json{{"unified", {{"enabled", true}}},
              {"goal_typing", {{"enabled", true}, {"calls_authorized", true},
                                {"max_requests", 2}, {"max_input_bytes", 300000},
                                {"max_output_tokens", 5000}, {"timeout_ms", 70000},
                                {"max_response_bytes", 300000}}}};
}
bool has_text(const Json& messages, std::string_view text) {
  for (const auto& message : messages)
    if (message["content"].is_string() && message["content"].get<std::string>().find(text) != std::string::npos) return true;
  return false;
}
}

TEST_SUITE("w3_chat_selector") {
  TEST_CASE("configured execution leaves build_messages preview offline") {
    SelectorFixture fixture;
    fixture.runtime->config().set("context_execution", execution_options());
    unwrap(fixture.runtime->memory().add_node("Stable personal source."));
    Json trace;
    auto messages = unwrap(fixture.runtime->chat().build_messages("", "asdkjhasd", {}, {}, "", &trace));
    CHECK(fixture.transport->requests().empty());
    CHECK(has_text(messages, "Stable personal source."));
    CHECK(trace["unified_context"]["used_tokens"].get<int>() <= trace["unified_context"]["budget_tokens"].get<int>());
    CHECK(messages[0]["content"] == "Stable system prefix.");
    CHECK(context::current_context_execution_scope() == nullptr);
  }

  TEST_CASE("modern legacy-only selector works before first knowledge snapshot") {
    SelectorFixture fixture(false);
    fixture.runtime->config().set("context_execution", Json{{"unified", {{"enabled", true}}}});
    unwrap(fixture.runtime->memory().add_node("Saved source before KB exists."));
    fixture.chat_reply();
    ChatOptions options;
    options.stream = false;
    auto result = unwrap(fixture.runtime->chat().send("current query", options));
    REQUIRE(result.context_trace.is_object());
    auto body = unwrap(json::parse(fixture.transport->requests().back().body));
    CHECK(result.context_trace["messages"] == body["messages"]);
    CHECK(result.context_trace["messages_sha256"] == Sha256::hex(json::dump(body["messages"])));
    CHECK(has_text(body["messages"], "Saved source before KB exists."));
    CHECK(fixture.transport->requests().size() == 1);
    auto user = unwrap(fixture.runtime->db().get_msg(result.user_message_id));
    REQUIRE(user);
    CHECK(user->metadata["context_trace"] == result.context_trace);
    CHECK(context::current_context_execution_scope() == nullptr);
  }

  TEST_CASE("unified chat sends source once and respects independent memory switch") {
    SelectorFixture fixture;
    fixture.runtime->config().set("context_execution", Json{{"unified", {{"enabled", true}}}});
    unwrap(fixture.runtime->memory().add_node("Unique stable memory."));
    Json enabled_trace, disabled_trace;
    auto enabled = unwrap(fixture.runtime->chat().build_messages("", "current", {}, {}, "", &enabled_trace));
    ChatOptions options;
    options.include_memory = false;
    auto disabled = unwrap(fixture.runtime->chat().build_messages("", "current", {}, options, "", &disabled_trace));
    CHECK(has_text(enabled, "Unique stable memory."));
    CHECK_FALSE(has_text(disabled, "Unique stable memory."));
    int occurrences = 0;
    for (const auto& message : enabled)
      if (message["content"].is_string() && message["content"].get<std::string>().find("Unique stable memory.") != std::string::npos) ++occurrences;
    CHECK(occurrences == 1);
    CHECK(enabled_trace["unified_context"]["knowledge_result"] == disabled_trace["unified_context"]["knowledge_result"]);
  }

  TEST_CASE("explicit missing knowledge snapshot stays an error in modern mode") {
    SelectorFixture fixture;
    fixture.runtime->config().set("context_execution", Json{{"unified", {{"enabled", true}}}});
    ChatOptions options;
    options.knowledge_context = context::ContextRequest{};
    options.knowledge_context->run = "absent-run";
    auto result = fixture.runtime->chat().build_messages("", "query", {}, options);
    CHECK_FALSE(result);
    CHECK(fixture.transport->requests().empty());
  }

  TEST_CASE("explicit configured snapshot and goal failures are not hidden by legacy fallback") {
    SelectorFixture fixture;
    for (const auto& request : {Json{{"run", "absent-run"}}, Json{{"goal_type", "absent-goal"}}}) {
      fixture.runtime->config().set("context_execution", Json{{"unified", {{"enabled", true}}}, {"request", request}});
      auto result = fixture.runtime->chat().build_messages("", "query", {}, {});
      CHECK_FALSE(result);
      CHECK(fixture.transport->requests().empty());
    }
  }

  TEST_CASE("model goal typing is connected to send and guarded by W2") {
    SelectorFixture fixture;
    fixture.runtime->config().set("context_execution", execution_options());
#if __has_include("loom/usage_policy.h")
    fixture.transport->expect("POST", "https://selector.test/chat/completions", net::ScriptedTransport::Reply::json(
        200, Json{{"choices", Json::array({Json{{"message", {{"content", "{\"goal_type\":\"verify_claim\",\"confidence\":0.8}"}}}}})},
                  {"usage", {{"prompt_tokens", 20}, {"completion_tokens", 12}, {"cost", 0.001}}}}));
#endif
    fixture.chat_reply();
    ChatOptions options;
    options.stream = false;
    auto result = unwrap(fixture.runtime->chat().send("asdkjhasd", options));
    const auto& goal = result.context_trace["knowledge_context"]["goal"];
    REQUIRE(goal.is_object());
#if __has_include("loom/usage_policy.h")
    CHECK(goal["type"] == "verify_claim");
    CHECK(goal["params"]["classifier"] == "llm");
    CHECK(goal["params"]["external_goal_typing"]["response"]["source_status"] == "recorded");
    CHECK(fixture.transport->requests().size() == 2);
    CHECK(unwrap(json::parse(fixture.transport->requests()[0].body))["max_tokens"] == 5000);
#else
    CHECK(goal["params"]["classifier"] == "cue");
    CHECK(fixture.transport->requests().size() == 1);
#endif
    CHECK(json::dump(result.context_trace).find("synthetic-credential") == std::string::npos);
    CHECK(context::current_context_execution_scope() == nullptr);
  }

  TEST_CASE("execution scope isolation survives nesting and exceptions") {
    CHECK(context::current_context_execution_scope() == nullptr);
    {
      context::ContextExecutionScope outer(Json{{"name", "outer"}});
      try {
        context::ContextExecutionScope inner(Json{{"name", "inner"}});
        CHECK(context::current_context_execution_scope()->options()["name"] == "inner");
        throw std::runtime_error("fixture");
      } catch (const std::runtime_error&) {}
      CHECK(context::current_context_execution_scope()->options()["name"] == "outer");
    }
    CHECK(context::current_context_execution_scope() == nullptr);
  }

  TEST_CASE("active task rejects memory changes beyond historical memory cap") {
    SelectorFixture fixture;
    auto& runtime = *fixture.runtime;
    runtime.config().set("context_execution", Json{{"unified", {{"enabled", true}, {"memory_max_chars", 0}, {"budget_tokens", 10000}}}});
    unwrap(runtime.memory().add_node(std::string(17000, 'a')));
    const auto tail = unwrap(runtime.memory().add_node("Original tail memory."));
    const auto conv = unwrap(runtime.db().create_conv("task fixture")).id;
    NewMessage source;
    source.conv_id = conv;
    source.role = "user";
    source.text = "Write the result.";
    const auto mid = unwrap(runtime.db().create_msg(source));
    Json spec{{"schema", "loom.active_task_spec/1"}, {"product_ref", {{"kind", "product"}, {"id", "fixture-product"}}},
        {"goal_id", "fixture-goal"}, {"knowledge_run", nullptr},
        {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "fixture"}}},
        {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-10-04T12:00:00Z"},
        {"representation", "derived_product"}, {"materializer", {{"id", "fixture"}, {"version", "1"}}},
        {"history_event_ids", Json::array({"event.fixture"})},
        {"source_refs", Json::array({Json{{"event_id", "event.fixture"},
          {"locator", {{"source", "synthetic"}, {"json_pointer", "/messages/0"}}},
          {"known_at", "2026-10-04T11:00:00Z"}, {"quote", source.text}}})},
        {"statements", Json::array({Json{{"id", "goal"}, {"kind", "goal"}, {"status", "active"}, {"text", source.text},
          {"source_event_ids", Json::array({"event.fixture"})}, {"claim_ids", Json::array()},
          {"conditions", Json::array()}, {"supersedes", Json::array()}}})},
        {"compiled_instruction", {{"text", "fixture"}, {"source_map", Json::array({Json{
          {"span", {{"byte_start", 0}, {"byte_len", 7}}}, {"statement_ids", Json::array({"goal"})}}})}}}};
    auto options = unwrap(ChatOptions::from_json(Json{{"conv_id", conv}, {"stream", false}, {"include_graph_memory", false},
        {"active_task_spec", spec}, {"active_task_bindings", {{"event.fixture", {{"message_id", mid}, {"text_sha256", Sha256::hex(source.text)}}}}}}));
    runtime.chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      LOOM_TRY(runtime.memory().update_node(tail, Json{{"content", "Changed tail memory."}}));
      return Json{{"prompt", ""}, {"context_set", {{"items", Json::array()}}}};
    });
    auto result = runtime.chat().send("execute", options);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(result.error().message.find("memory consumed during context preparation changed") != std::string::npos);
    CHECK(fixture.transport->requests().empty());
  }
}
