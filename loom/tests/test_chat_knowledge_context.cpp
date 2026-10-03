#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/graph_engine.h"
#include "loom/knowledge.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct ChatContextFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string run;

  ChatContextFixture() {
    RuntimeOptions opts;
    opts.data_dir = dir.path().string();
    opts.start_workers = false;
    opts.http = transport;
    rt = unwrap(Runtime::open(opts));
    rt->config().set("base_url", "https://chat.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "system instruction");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "fixture-only-not-a-real-key");
    auto pack = unwrap(rt->knowledge().pack());
    auto& store = rt->knowledge().store();
    run = unwrap(store.begin_run(pack->hash(), Json::object())).id;
    model::Principle principle;
    principle.id = "pr.chat_context_fixture";
    principle.statement = {{"en", "Preserve the original observations and explain every derived transformation."}};
    principle.form = model::PrincipleForm::Invariant;
    principle.level = model::PrincipleLevel::Strategy;
    principle.validation = model::ValidationStatus::Confirmed;
    principle.confidence = 0.95;
    LOOM_REQUIRE_OK(store.put_principles(run, {principle}));
    LOOM_REQUIRE_OK(store.finish_run(run, "done", Json::object()));
  }

  ChatOptions options() const {
    return unwrap(ChatOptions::from_json(Json{{"knowledge_context", {{"run", run}, {"lang", "en"}}},
                                              {"include_graph_memory", false}, {"stream", false}}));
  }

  void reply(int status = 200) {
    transport->expect("POST", "https://chat.test/chat/completions", net::ScriptedTransport::Reply::json(
      status, Json{{"choices", Json::array({Json{{"message", {{"content", "test reply"}}}}})}}));
  }
};

bool contains_text(const Json& messages, std::string_view text) {
  for (const auto& message : messages) {
    if (message["content"].is_string() && message["content"].get<std::string>().find(text) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_SUITE("chat_knowledge_context") {
  TEST_CASE("default recipe does not call the knowledge builder or add a trace") {
    ChatContextFixture f;
    int calls = 0;
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      ++calls;
      return Error(Errc::Internal, "must not be called");
    });
    f.reply();
    auto result = unwrap(f.rt->chat().send("hello"));
    CHECK(calls == 0);
    CHECK(result.context_trace.is_null());
    CHECK_FALSE(result.to_json().contains("context_trace"));
    auto body = unwrap(json::parse(f.transport->requests().back().body));
    CHECK(body["messages"] == Json::array({Json{{"role", "system"}, {"content", "system instruction"}},
                                           Json{{"role", "user"}, {"content", "hello"}}}));
  }

  TEST_CASE("runtime preview is offline and pins a default latest run in its trace") {
    ChatContextFixture f;
    f.rt->config().set("semantic_model", "test/semantic");
    f.rt->config().set("semantic_analysis", true);
    ChatOptions options;
    options.knowledge_context = context::ContextRequest{};
    options.include_graph_memory = false;
    Json trace;
    auto messages = unwrap(f.rt->chat().build_messages("", "What applies?", {}, options, "", &trace));
    CHECK(f.transport->requests().empty());
    CHECK(trace["knowledge_context_request"]["text"] == "What applies?");
    CHECK(trace["knowledge_context_request"]["run"] == f.run);
    CHECK(trace["messages"] == messages);
    CHECK(trace["messages_sha256"] == Sha256::hex(json::dump(messages)));
    CHECK(contains_text(messages, "Preserve the original observations"));
    CHECK(contains_text(messages, trace["knowledge_context"]["prompt"].get<std::string>()));
  }

  TEST_CASE("actual request and retained trace share one compilation and exact message text") {
    ChatContextFixture f;
    auto options = f.options();
    f.reply();
    auto result = unwrap(f.rt->chat().send("What applies?", options));
    auto requests = f.transport->requests();
    REQUIRE(requests.size() == 1);
    auto body = unwrap(json::parse(requests[0].body));
    CHECK(result.context_trace["messages"] == body["messages"]);
    CHECK(result.to_json()["context_trace"] == result.context_trace);
    auto user = unwrap(f.rt->db().get_msg(result.user_message_id));
    REQUIRE(user);
    CHECK(user->text == "What applies?");
    CHECK(user->metadata["context_trace"] == result.context_trace);
    CHECK(json::dump(result.context_trace).find("fixture-only-not-a-real-key") == std::string::npos);
    auto prompt = result.context_trace["knowledge_context"]["prompt"];
    int knowledge_count = 0, current_count = 0;
    for (const auto& m : body["messages"]) {
      if (m["role"] == "system" && m["content"] == prompt) ++knowledge_count;
      if (m["role"] == "user" && m["content"] == "What applies?") ++current_count;
    }
    CHECK(knowledge_count == 1);
    CHECK(current_count == 1);
  }

  TEST_CASE("independent history and memory controls change the request without rewriting sources") {
    ChatContextFixture f;
    auto conv = unwrap(f.rt->db().create_conv("history"));
    NewMessage original;
    original.conv_id = conv.id;
    original.role = "user";
    original.text = "Original history, including rejected draft.";
    auto original_id = unwrap(f.rt->db().create_msg(original));
    auto before = unwrap(f.rt->db().get_msg(original_id))->to_json();
    unwrap(f.rt->memory().add_node("Saved personal context"));
    auto options = f.options();
    Json enabled_trace, disabled_trace;
    auto enabled = unwrap(f.rt->chat().build_messages(conv.id, "current", {}, options, "", &enabled_trace));
    options.include_history = false;
    auto no_history = unwrap(f.rt->chat().build_messages(conv.id, "current", {}, options));
    CHECK_FALSE(contains_text(no_history, original.text));
    CHECK(contains_text(no_history, "Saved personal context"));
    options.include_history = true;
    options.include_memory = false;
    auto no_memory = unwrap(f.rt->chat().build_messages(conv.id, "current", {}, options));
    CHECK(contains_text(no_memory, original.text));
    CHECK_FALSE(contains_text(no_memory, "Saved personal context"));
    options.include_history = false;
    auto disabled = unwrap(f.rt->chat().build_messages(conv.id, "current", {}, options, "", &disabled_trace));
    CHECK(contains_text(enabled, original.text));
    CHECK(contains_text(enabled, "Saved personal context"));
    CHECK_FALSE(contains_text(disabled, original.text));
    CHECK_FALSE(contains_text(disabled, "Saved personal context"));
    CHECK(enabled_trace["knowledge_context"] == disabled_trace["knowledge_context"]);
    CHECK(enabled_trace["history_message_ids"] == Json::array({original_id}));
    CHECK(disabled_trace["history_message_ids"].empty());
    CHECK(unwrap(f.rt->db().get_msg(original_id))->to_json() == before);
  }

  TEST_CASE("knowledge item budget affects transmitted context independently of history") {
    ChatContextFixture f;
    auto options = f.options();
    Json full, tight;
    unwrap(f.rt->chat().build_messages("", "query", {}, options, "", &full));
    options.knowledge_context->budget_tokens = 1;
    unwrap(f.rt->chat().build_messages("", "query", {}, options, "", &tight));
    CHECK(full["knowledge_context"]["context_set"]["used_tokens"].get<int>() > 1);
    CHECK(tight["knowledge_context"]["context_set"]["used_tokens"].get<int>() <= 1);
    CHECK(full["knowledge_context"]["prompt"] != tight["knowledge_context"]["prompt"]);
    CHECK(tight["messages"].back()["content"] == "query");
  }

  TEST_CASE("semantic reindex retains compiled context and unrelated source metadata") {
    ChatContextFixture f;
    f.reply();
    auto result = unwrap(f.rt->chat().send("Explain the project context", f.options()));
    auto original = unwrap(f.rt->db().get_msg(result.user_message_id));
    REQUIRE(original);
    auto metadata = original->metadata;
    metadata["export_provenance"] = Json{{"source", "synthetic source fixture"}};
    MsgPatch patch;
    patch.metadata = metadata;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(result.user_message_id, patch));
    CHECK(f.rt->graph().reindex_conversation(result.conv_id) >= 1);
    auto reindexed = unwrap(f.rt->db().get_msg(result.user_message_id));
    REQUIRE(reindexed);
    CHECK(reindexed->metadata["context_trace"] == result.context_trace);
    CHECK(reindexed->metadata["export_provenance"] == metadata["export_provenance"]);
    CHECK(reindexed->metadata.contains("semantic"));
    CHECK(reindexed->text == original->text);
  }

  TEST_CASE("semantic reindex preserves legacy non-object metadata while enriching it") {
    ChatContextFixture f;
    auto conv = unwrap(f.rt->db().create_conv("legacy metadata"));
    for (const auto& metadata : {Json("legacy source string"), Json::array({1, "two"}), Json(42), Json(nullptr)}) {
      NewMessage message;
      message.conv_id = conv.id;
      message.text = "Project notes for metadata regression";
      message.role = "user";
      auto id = unwrap(f.rt->db().create_msg(message));
      MsgPatch patch;
      patch.metadata = metadata;
      LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
      CHECK(f.rt->graph().reindex_conversation(conv.id) >= 1);
      auto enriched = unwrap(f.rt->db().get_msg(id));
      REQUIRE(enriched);
      CHECK(enriched->metadata["loom_preserved_metadata"] == metadata);
      CHECK(enriched->metadata.contains("semantic"));
      CHECK(enriched->semantic_status == "done");
    }
  }

  TEST_CASE("explicit trace opt-out leaves knowledge selection active without a retained copy") {
    ChatContextFixture f;
    auto options = f.options();
    options.trace_context = false;
    f.reply();
    auto result = unwrap(f.rt->chat().send("What applies?", options));
    CHECK(result.context_trace.is_null());
    auto user = unwrap(f.rt->db().get_msg(result.user_message_id));
    REQUIRE(user);
    CHECK_FALSE(user->metadata.contains("context_trace"));
    auto body = unwrap(json::parse(f.transport->requests()[0].body));
    CHECK(contains_text(body["messages"], "Preserve the original observations"));
  }

  TEST_CASE("malformed nested options are rejected instead of silently falling back") {
    for (const auto& invalid : {Json("bad"), Json{{"budegt_tokens", 50}}, Json{{"budget_tokens", "50"}},
                               Json{{"budget_tokens", 0}}, Json{{"budget_tokens", -1}},
                               Json{{"budget_tokens", 1.5}}, Json{{"budget_tokens", 999999999999LL}},
                               Json{{"targets", Json::array({"valid", 4})}}, Json{{"goal_type", false}},
                               Json{{"relation_hops", -1}}, Json{{"relation_hops", 1.5}},
                               Json{{"relation_hops", 999999999999LL}}, Json{{"detail_resolution", "unknown"}},
                               Json{{"text", 42}}, Json{{"run", false}}}) {
      auto options = ChatOptions::from_json(Json{{"knowledge_context", invalid}});
      CHECK_FALSE(options);
      if (!options) CHECK(options.error().code == Errc::InvalidArgument);
    }
    CHECK_FALSE(ChatOptions::from_json(Json{{"include_history", "false"}}));
    auto defaults = unwrap(ChatOptions::from_json(Json{{"knowledge_context", nullptr}}));
    CHECK_FALSE(defaults.knowledge_context);
    CHECK_FALSE(defaults.stream.has_value());
  }

  TEST_CASE("chat parser retains independent relation scope and detail controls") {
    auto options = unwrap(ChatOptions::from_json(Json{{"knowledge_context", {
      {"relation_hops", 3}, {"detail_resolution", "full"}, {"budget_tokens", 1800}}}}));
    REQUIRE(options.knowledge_context);
    CHECK(options.knowledge_context->to_json()["relation_hops"] == 3);
    CHECK(options.knowledge_context->to_json()["detail_resolution"] == "full");
    CHECK(options.knowledge_context->to_json()["budget_tokens"] == 1800);
    auto zero = ChatOptions::from_json(Json{{"knowledge_context", {{"relation_hops", 0}, {"detail_resolution", nullptr}}}});
    REQUIRE(zero);
  }

  TEST_CASE("latest finished run remains reachable behind more than fifty unfinished runs") {
    ChatContextFixture f;
    auto& store = f.rt->knowledge().store();
    auto pack = unwrap(f.rt->knowledge().pack());
    // A fixed old timestamp avoids relying on clock granularity for ordering.
    {
      auto lock = f.rt->db().lock();
      LOOM_REQUIRE_OK(f.rt->db().conn().run("UPDATE loom_kb_runs SET created = ? WHERE run_id = ?",
                                           "2000-01-01T00:00:00Z", f.run));
    }
    for (int i = 0; i < 51; ++i) unwrap(store.begin_run(pack->hash(), Json{{"unfinished", i}}));
    auto first_page = unwrap(store.list_runs(50));
    REQUIRE(first_page.size() == 50);
    for (const auto& run : first_page) CHECK(run.id != f.run);
    auto done = unwrap(store.list_runs(1, "done"));
    REQUIRE(done.size() == 1);
    CHECK(done[0].id == f.run);
    CHECK(unwrap(store.list_runs(1, "done' OR 1=1 --")).empty());
    ChatOptions options;
    options.knowledge_context = context::ContextRequest{};
    options.include_graph_memory = false;
    Json trace;
    unwrap(f.rt->chat().build_messages("", "query", {}, options, "", &trace));
    CHECK(trace["knowledge_context_request"]["run"] == f.run);
    CHECK(contains_text(trace["messages"], "Preserve the original observations"));
  }

  TEST_CASE("unknown goal or run fails before the chat provider call") {
    ChatContextFixture f;
    auto options = f.options();
    options.knowledge_context->goal_type = "unknown-goal";
    auto bad_goal = f.rt->chat().send("query", options);
    REQUIRE_FALSE(bad_goal);
    CHECK(bad_goal.error().code == Errc::NotFound);
    options.knowledge_context->goal_type.reset();
    options.knowledge_context->run = "missing-run";
    auto bad_run = f.rt->chat().send("query", options);
    REQUIRE_FALSE(bad_run);
    CHECK(bad_run.error().code == Errc::NotFound);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("missing capability and malformed builder result do not fall back to legacy chat") {
    ChatContextFixture f;
    auto options = f.options();
    f.rt->chat().set_knowledge_context_builder({});
    auto missing = f.rt->chat().send("query", options);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == Errc::NotImplemented);
    f.rt->chat().set_knowledge_context_builder([](const context::ContextRequest&) -> Result<Json> {
      return Json{{"prompt", 42}};
    });
    auto malformed = f.rt->chat().send("query", options);
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().code == Errc::Internal);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("compilation survives a provider failure without claiming provider receipt") {
    ChatContextFixture f;
    auto options = f.options();
    options.conv_id = unwrap(f.rt->db().create_conv("failure")).id;
    f.reply(503);
    auto result = f.rt->chat().send("query", options);
    REQUIRE_FALSE(result);
    auto messages = unwrap(f.rt->db().get_msgs(*options.conv_id));
    REQUIRE(messages.size() == 1);
    auto trace = messages[0].metadata["context_trace"];
    CHECK(trace["kind"] == "compiled_messages");
    CHECK_FALSE(trace.contains("request_provenance"));
    CHECK(trace["messages"] == unwrap(json::parse(f.transport->requests()[0].body))["messages"]);
  }

  TEST_CASE("stream setting uses config by default and explicit request overrides it") {
    ChatContextFixture f;
    f.rt->config().set("stream", false);
    ChatCallbacks cb;
    cb.on_chunk = [](std::string_view) {};
    f.reply();
    unwrap(f.rt->chat().send("config default", {}, cb));
    CHECK_FALSE(unwrap(json::parse(f.transport->requests().back().body))["stream"].get<bool>());
    ChatOptions options;
    options.stream = true;
    f.transport->expect("POST", "https://chat.test/chat/completions", net::ScriptedTransport::Reply::sse(
      {"{\"choices\":[{\"delta\":{\"content\":\"streamed\"}}]}", "[DONE]"}));
    CHECK(unwrap(f.rt->chat().send("explicit stream", options, cb)).text == "streamed");
    CHECK(unwrap(json::parse(f.transport->requests().back().body))["stream"].get<bool>());
    f.rt->config().set("stream", true);
    options.stream = false;
    f.reply();
    unwrap(f.rt->chat().send("explicit nonstream", options, cb));
    CHECK_FALSE(unwrap(json::parse(f.transport->requests().back().body))["stream"].get<bool>());
  }
}
