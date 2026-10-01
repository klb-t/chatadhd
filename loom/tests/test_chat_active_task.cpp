#include <doctest/doctest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

// Hand-authored development counterexamples. No evaluation corpus or model is
// involved: these tests check the actual caller-to-transport contract only.
struct ActiveTaskFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  std::vector<Message> sources;
  Json spec;
  Json bindings = Json::object();
  std::string unrelated;

  ActiveTaskFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://active-task.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "System instructions.");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "active-task-test-key");
    conv = unwrap(rt->db().create_conv("Active task counterexamples")).id;
    unrelated = add_message("user", "KEEP_UNRELATED_CONTEXT: preserve the dataset.").id;
    sources.push_back(add_message("user", "SOURCE_0: Prepare the report. OBSOLETE_STYLE: be cheerful."));
    sources.push_back(add_message("assistant", "REJECTED_PLAN: discard the audit identifiers."));
    sources.push_back(add_message("user", "SOURCE_2: Be concise, except preserve audit details. The title is still wrong."));

    Json refs = Json::array();
    Json history = Json::array();
    for (std::size_t i = 0; i < sources.size(); ++i) {
      const auto event = "event." + std::to_string(i);
      history.push_back(event);
      refs.push_back(Json{{"event_id", event}, {"locator", {{"source", "synthetic.native.messages"},
                          {"json_pointer", "/messages/" + std::to_string(i)}}},
                          {"known_at", "2026-09-30T19:00:00Z"}, {"quote", sources[i].text}});
      bindings[event] = Json{{"message_id", sources[i].id}, {"text_sha256", Sha256::hex(sources[i].text)}};
    }
    spec = Json{{"schema", "loom.active_task_spec/1"},
                {"product_ref", {{"kind", "product"}, {"id", "active-report-v1"}}},
                {"goal_id", "report-goal"}, {"knowledge_run", nullptr},
                {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "report"}}},
                {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-09-30T20:00:00Z"},
                {"representation", "derived_product"},
                {"materializer", {{"id", "manual-runtime-counterexample"}, {"version", "1"}}},
                {"history_event_ids", history}, {"source_refs", refs},
                {"statements", Json::array({
                  clause("old-style", "style", "superseded", "OBSOLETE_STYLE: be cheerful.", "event.0"),
                  clause("rejected", "alternative", "rejected", "REJECTED_PLAN: discard the audit identifiers.", "event.1"),
                  clause("goal", "goal", "active", "Prepare the operator report.", "event.0"),
                  clause("style", "style", "active", "Keep the main text concise.", "event.2", {}, {"old-style"}),
                  clause("audit", "exception", "active", "Retain every audit identifier, including Żółć-17.", "event.2",
                         {"Only the audit appendix may exceed one page."}),
                  clause("title", "open_issue", "contested", "The title needs correction; the replacement is unresolved.", "event.2"),
                  clause("private", "executor_context", "active", "PRIVATE_BACKGROUND: the operator is anxious.", "event.2")})},
                // A structurally valid map does not make this text authoritative.
                {"compiled_instruction", {{"text", "FORGED_COMPILED_TEXT"}, {"source_map", Json::array({
                  Json{{"span", {{"byte_start", 0}, {"byte_len", 20}}}, {"statement_ids", Json::array({"goal"})}}})}}}};
  }

  static Json clause(std::string id, std::string kind, std::string status, std::string text,
                     std::string event, std::vector<std::string> conditions = {},
                     std::vector<std::string> supersedes = {}) {
    return Json{{"id", id}, {"kind", kind}, {"status", status}, {"text", text},
                {"source_event_ids", Json::array({event})}, {"claim_ids", Json::array()},
                {"conditions", conditions}, {"supersedes", supersedes}};
  }

  Message add_message(std::string role, std::string text, std::string cid = "") {
    NewMessage message;
    message.conv_id = cid.empty() ? conv : cid;
    message.role = std::move(role);
    message.text = std::move(text);
    message.metadata = Json{{"original_import", "keep me byte-for-byte"}};
    return *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
  }

  Json option_json() const {
    return Json{{"conv_id", conv}, {"stream", false}, {"include_memory", false},
                {"include_graph_memory", false}, {"active_task_spec", spec},
                {"active_task_bindings", bindings}};
  }

  ChatOptions options() const { return unwrap(ChatOptions::from_json(option_json())); }

  void reply(int status = 200) {
    transport->expect("POST", "https://active-task.test/chat/completions",
                     net::ScriptedTransport::Reply::json(status, Json{{"choices", Json::array({
                       Json{{"message", {{"content", "A test reply."}}}}})}}));
  }

  Json sent_messages(std::size_t index = 0) const {
    const auto requests = transport->requests();
    REQUIRE(requests.size() > index);
    return unwrap(json::parse(requests[index].body))["messages"];
  }

  Json saved_active_task(const ChatResult& result) const {
    auto message = unwrap(rt->db().get_msg(result.user_message_id));
    REQUIRE(message);
    REQUIRE(message->metadata.contains("active_task"));
    return message->metadata["active_task"];
  }
};

bool contains_text(const Json& messages, std::string_view text) {
  for (const auto& message : messages) {
    if (message["content"].is_string() && message["content"].get<std::string>().find(text) != std::string::npos) return true;
  }
  return false;
}

int exact_user_count(const Json& messages, std::string_view text) {
  int count = 0;
  for (const auto& message : messages) {
    if (message["role"] == "user" && message["content"] == text) ++count;
  }
  return count;
}

}  // namespace

TEST_SUITE("chat_active_task") {
  TEST_CASE("actual transport receives current clauses and scoped exceptions without obsolete attempts") {
    ActiveTaskFixture f;
    f.reply();
    auto result = unwrap(f.rt->chat().send("Produce the report now.", f.options()));
    const auto messages = f.sent_messages();
    CHECK(contains_text(messages, "Prepare the operator report."));
    CHECK(contains_text(messages, "Keep the main text concise."));
    CHECK(contains_text(messages, "Retain every audit identifier, including Żółć-17."));
    CHECK(contains_text(messages, "Only the audit appendix may exceed one page."));
    CHECK(contains_text(messages, "KEEP_UNRELATED_CONTEXT"));
    CHECK_FALSE(contains_text(messages, "REJECTED_PLAN"));
    CHECK_FALSE(contains_text(messages, "OBSOLETE_STYLE"));
    CHECK_FALSE(contains_text(messages, "FORGED_COMPILED_TEXT"));
    CHECK_FALSE(contains_text(messages, "SOURCE_0"));
    CHECK_FALSE(contains_text(messages, "SOURCE_2"));
    CHECK(exact_user_count(messages, "Produce the report now.") == 1);
    CHECK(messages.back()["content"] == "Produce the report now.");
    CHECK(result.context_trace["messages"] == messages);
    CHECK(result.context_trace["messages_sha256"] == Sha256::hex(json::dump(messages)));
    const auto saved = f.saved_active_task(result);
    CHECK(saved["supplied_spec"] == f.spec);
    CHECK(saved["bindings"] == f.bindings);
    CHECK(saved["compiled_spec"]["compiled_instruction"]["text"] != "FORGED_COMPILED_TEXT");
    for (const auto& source : f.sources) CHECK(unwrap(f.rt->db().get_msg(source.id))->to_json() == source.to_json());
  }

  TEST_CASE("unresolved correction and executor-only context remain explicitly qualified") {
    ActiveTaskFixture f;
    f.reply();
    auto result = unwrap(f.rt->chat().send("Continue.", f.options()));
    const auto saved = f.saved_active_task(result);
    const auto text = saved["compiled_spec"]["compiled_instruction"]["text"].get<std::string>();
    CHECK(text.find("The title needs correction; the replacement is unresolved.") != std::string::npos);
    CHECK(text.find("contested") != std::string::npos);
    CHECK(text.find("do not resolve without clarification") != std::string::npos);
    CHECK(text.find("executor only") != std::string::npos);
    CHECK(text.find("not product content") != std::string::npos);
    CHECK(contains_text(f.sent_messages(), text));
    CHECK(saved["compiled_spec"]["statements"][5]["status"] == "contested");
    CHECK(saved["compiled_spec"]["statements"][6]["kind"] == "executor_context");
  }

  TEST_CASE("rebuilt UTF-8 byte map names active clauses and retains source event relationships") {
    ActiveTaskFixture f;
    f.reply();
    auto result = unwrap(f.rt->chat().send("Continue.", f.options()));
    const auto compiled = f.saved_active_task(result)["compiled_spec"];
    const auto text = compiled["compiled_instruction"]["text"].get<std::string>();
    auto mapped = Json::array();
    for (const auto& row : compiled["compiled_instruction"]["source_map"]) {
      const auto start = row["span"]["byte_start"].get<std::size_t>();
      const auto length = row["span"]["byte_len"].get<std::size_t>();
      REQUIRE(start <= text.size());
      REQUIRE(length <= text.size() - start);
      for (const auto& id : row["statement_ids"]) {
        mapped.push_back(id);
        bool found = false;
        for (const auto& statement : compiled["statements"]) {
          if (statement["id"] != id) continue;
          found = true;
          CHECK(statement["status"] != "rejected");
          CHECK(statement["status"] != "superseded");
          CHECK(text.substr(start, length).find(statement["text"].get<std::string>()) != std::string::npos);
          CHECK_FALSE(statement["source_event_ids"].empty());
        }
        CHECK(found);
      }
    }
    CHECK(mapped == Json::array({"goal", "style", "audit", "title", "private"}));
    CHECK(compiled["source_refs"] == f.spec["source_refs"]);
  }

  TEST_CASE("history policy is independent and never rewrites source rows") {
    ActiveTaskFixture f;
    auto input = f.option_json();
    input["active_task_history"] = "append";
    f.reply();
    auto appended = unwrap(f.rt->chat().send("Append mode.", unwrap(ChatOptions::from_json(input))));
    CHECK(contains_text(f.sent_messages(), "REJECTED_PLAN"));
    CHECK(contains_text(f.sent_messages(), "Prepare the operator report."));
    input["include_history"] = false;
    f.reply();
    auto isolated = unwrap(f.rt->chat().send("No history mode.", unwrap(ChatOptions::from_json(input))));
    CHECK_FALSE(contains_text(f.sent_messages(1), "REJECTED_PLAN"));
    CHECK_FALSE(contains_text(f.sent_messages(1), "KEEP_UNRELATED_CONTEXT"));
    CHECK(contains_text(f.sent_messages(1), "Prepare the operator report."));
    CHECK(exact_user_count(f.sent_messages(1), "No history mode.") == 1);
    for (const auto& source : f.sources) CHECK(unwrap(f.rt->db().get_msg(source.id))->to_json() == source.to_json());
    CHECK(f.saved_active_task(appended)["compiled_spec"] == f.saved_active_task(isolated)["compiled_spec"]);
  }

  TEST_CASE("current message identical to a covered source is still sent exactly once") {
    ActiveTaskFixture f;
    f.reply();
    unwrap(f.rt->chat().send(f.sources[0].text, f.options()));
    CHECK(exact_user_count(f.sent_messages(), f.sources[0].text) == 1);
    CHECK(f.sent_messages().back()["content"] == f.sources[0].text);
  }

  TEST_CASE("preview compiles the same messages offline without accepting or persisting a task") {
    ActiveTaskFixture f;
    Json trace;
    const auto before = unwrap(f.rt->db().get_msgs(f.conv)).size();
    auto preview = unwrap(f.rt->chat().build_messages(f.conv, "Continue.", {}, f.options(), "", &trace));
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv)).size() == before);
    CHECK(trace["messages"] == preview);
    f.reply();
    auto sent = unwrap(f.rt->chat().send("Continue.", f.options()));
    CHECK(f.sent_messages() == preview);
    CHECK(sent.context_trace["messages"] == preview);
  }

  TEST_CASE("trace opt-out retains the accepted task and historical versions after provider failure") {
    ActiveTaskFixture f;
    auto options = f.options();
    options.trace_context = false;
    f.reply(503);
    auto failed = f.rt->chat().send("Continue despite provider failure.", options);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == Errc::Http);
    REQUIRE(f.transport->requests().size() == 1);
    auto messages = unwrap(f.rt->db().get_msgs(f.conv));
    REQUIRE_FALSE(messages.empty());
    const auto& metadata = messages.back().metadata;
    CHECK_FALSE(metadata.contains("context_trace"));
    REQUIRE(metadata.contains("active_task"));
    CHECK(metadata["active_task"]["supplied_spec"] == f.spec);
    CHECK(contains_text(f.sent_messages(), "Prepare the operator report."));
  }

  TEST_CASE("bare summaries and malformed source maps do not become executable instructions") {
    ActiveTaskFixture f;
    std::vector<Json> invalid;
    invalid.push_back(Json{{"text", "Trust this arbitrary summary."}});
    invalid.push_back(f.spec);
    invalid.back()["compiled_instruction"]["source_map"][0]["statement_ids"] = Json::array({"rejected"});
    invalid.push_back(f.spec);
    invalid.back()["compiled_instruction"]["text"] = "Żx";
    invalid.back()["compiled_instruction"]["source_map"][0]["span"] = {{"byte_start", 1}, {"byte_len", 1}};
    for (const auto& spec : invalid) {
      auto input = f.option_json();
      input["active_task_spec"] = spec;
      auto parsed = ChatOptions::from_json(input);
      if (parsed) CHECK_FALSE(f.rt->chat().send("Must not send.", *parsed));
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("source bindings fail closed for missing extra duplicate stale and foreign messages") {
    ActiveTaskFixture f;
    const auto foreign_conv = unwrap(f.rt->db().create_conv("Unrelated conversation")).id;
    const auto foreign = f.add_message("user", f.sources[0].text, foreign_conv);
    std::vector<Json> invalid;
    invalid.push_back(f.bindings);
    invalid.back().erase("event.0");
    invalid.push_back(f.bindings);
    invalid.back()["invented.event"] = f.bindings["event.0"];
    invalid.push_back(f.bindings);
    invalid.back()["event.0"]["message_id"] = "missing-native-message";
    invalid.push_back(f.bindings);
    invalid.back()["event.0"]["text_sha256"] = std::string(64, '0');
    invalid.push_back(f.bindings);
    invalid.back()["event.1"] = f.bindings["event.0"];
    invalid.push_back(f.bindings);
    invalid.back()["event.0"]["message_id"] = foreign.id;
    for (const auto& bindings : invalid) {
      auto input = f.option_json();
      input["active_task_bindings"] = bindings;
      auto parsed = ChatOptions::from_json(input);
      if (parsed) CHECK_FALSE(f.rt->chat().send("Must not send.", *parsed));
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("edited or inactive source messages invalidate previously prepared options") {
    for (bool deactivate : {false, true}) {
      ActiveTaskFixture f;
      auto options = f.options();
      MsgPatch patch;
      if (deactivate) patch.status = "deleted";
      else patch.text = "A later edit must not be attributed to the earlier accepted bytes.";
      LOOM_REQUIRE_OK(f.rt->db().update_msg(f.sources[0].id, patch));
      CHECK_FALSE(f.rt->chat().send("Must not send.", options));
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("scope binding rejects foreign conversations and unsupported branch claims") {
    ActiveTaskFixture f;
    for (const auto& key : {"conversation_id", "branch_id"}) {
      auto input = f.option_json();
      input["active_task_spec"]["scope"][key] = "unsupported-source-scope";
      auto parsed = ChatOptions::from_json(input);
      if (parsed) CHECK_FALSE(f.rt->chat().send("Must not send.", *parsed));
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("successor revision uses its latest text while both accepted source specifications survive") {
    ActiveTaskFixture f;
    f.reply();
    auto first = unwrap(f.rt->chat().send("Produce version one.", f.options()));
    const auto previous = f.spec;
    f.spec["version"] = 2;
    f.spec["previous_product_ref"] = f.spec["product_ref"];
    f.spec["product_ref"]["id"] = "active-report-v2";
    f.spec["statements"][3]["text"] = "LATEST_STYLE: use a neutral technical tone.";
    auto input = f.option_json();
    input["include_history"] = false;
    f.reply();
    auto second = unwrap(f.rt->chat().send("Produce version two.", unwrap(ChatOptions::from_json(input))));
    CHECK(contains_text(f.sent_messages(1), "LATEST_STYLE"));
    CHECK_FALSE(contains_text(f.sent_messages(1), "Keep the main text concise."));
    CHECK(f.saved_active_task(first)["supplied_spec"] == previous);
    CHECK(f.saved_active_task(second)["supplied_spec"] == f.spec);
    CHECK(f.saved_active_task(second)["compiled_spec"]["previous_product_ref"] == previous["product_ref"]);
  }

  TEST_CASE("reused product identity cannot silently mutate an accepted instruction") {
    ActiveTaskFixture f;
    f.reply();
    unwrap(f.rt->chat().send("Accept first version.", f.options()));
    f.spec["statements"][3]["text"] = "IMPOSTOR_STYLE: quietly change the accepted version.";
    CHECK_FALSE(f.rt->chat().send("Reject identity collision.", f.options()));
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("accepted request uses the same source snapshot even if on_start edits a source") {
    ActiveTaskFixture f;
    auto opts = f.options();
    ChatCallbacks callbacks;
    callbacks.on_start = [&](std::string_view, std::string_view) {
      MsgPatch patch;
      patch.text = "CHANGED_AFTER_ACCEPTANCE";
      LOOM_REQUIRE_OK(f.rt->db().update_msg(f.sources[0].id, patch));
    };
    f.reply();
    auto result = unwrap(f.rt->chat().send("Use the accepted snapshot.", opts, callbacks));
    CHECK_FALSE(contains_text(f.sent_messages(), "CHANGED_AFTER_ACCEPTANCE"));
    CHECK(result.context_trace["messages"] == f.sent_messages());
    CHECK(f.saved_active_task(result)["source_messages"][0]["text"] == f.sources[0].text);
    CHECK(unwrap(f.rt->db().get_msg(f.sources[0].id))->text == "CHANGED_AFTER_ACCEPTANCE");
  }

  TEST_CASE("independent knowledge context is retained and is not represented as task-filtered") {
    ActiveTaskFixture f;
    std::optional<context::ContextRequest> observed_request;
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest& request) -> Result<Json> {
      observed_request = request;
      return Json{{"prompt", "INDEPENDENT_CONTEXT: REJECTED_PLAN is mentioned here as evidence."},
                  {"context_set", Json::object()}, {"request", request.to_json()}};
    });
    auto input = f.option_json();
    input["knowledge_context"] = Json{{"text", "INDEPENDENT_RETRIEVAL_QUERY"},
                                      {"targets", Json::array({"entity.explicit"})},
                                      {"project", "entity.project"}, {"budget_tokens", 1800},
                                      {"relation_hops", 3}, {"detail_resolution", "full"}};
    auto opts = unwrap(ChatOptions::from_json(input));
    f.reply();
    auto result = unwrap(f.rt->chat().send("Use the chosen context.", opts));
    REQUIRE(observed_request);
    CHECK(observed_request->text == "INDEPENDENT_RETRIEVAL_QUERY");
    CHECK(observed_request->targets == std::vector<std::string>{"entity.explicit"});
    CHECK(observed_request->project == "entity.project");
    CHECK(observed_request->budget_tokens == 1800);
    CHECK(observed_request->relation_hops == 3);
    REQUIRE(observed_request->detail_resolution);
    CHECK(std::string(model::to_string(*observed_request->detail_resolution)) == "full");
    const auto messages = f.sent_messages();
    int rejected_mentions = 0;
    for (const auto& message : messages) {
      if (!message["content"].is_string() ||
          message["content"].get<std::string>().find("REJECTED_PLAN") == std::string::npos) continue;
      ++rejected_mentions;
      CHECK(message["role"] == "system");
      CHECK(message["content"].get<std::string>().find("INDEPENDENT_CONTEXT") != std::string::npos);
    }
    CHECK(rejected_mentions == 1);
    CHECK(contains_text(messages, "Prepare the operator report."));
    CHECK(exact_user_count(messages, "Use the chosen context.") == 1);
    CHECK(result.context_trace["messages"] == messages);
    CHECK(result.context_trace["knowledge_context_request"] == observed_request->to_json());
    CHECK(result.context_trace["active_task"]["compiled_spec"]["product_ref"] == f.spec["product_ref"]);
    CHECK(result.context_trace["replaced_history_message_ids"].size() == 3);
  }

  TEST_CASE("malformed metadata projection does not erase durable accepted authority") {
    ActiveTaskFixture f;
    f.reply();
    auto first = unwrap(f.rt->chat().send("Accept version one.", f.options()));
    auto message = unwrap(f.rt->db().get_msg(first.user_message_id));
    MsgPatch patch;
    auto metadata = message->metadata;
    metadata["active_task"]["supplied_spec"].erase("goal_id");
    patch.metadata = metadata;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(first.user_message_id, patch));
    const auto count = unwrap(f.rt->db().get_msgs(f.conv)).size();
    f.reply();
    auto replay = unwrap(f.rt->chat().send("Replay from durable lineage.", f.options()));
    CHECK(f.transport->requests().size() == 2);
    CHECK(unwrap(f.rt->db().get_msgs(f.conv)).size() == count + 2);
    CHECK(f.saved_active_task(replay)["supplied_spec"] == f.spec);
    CHECK(unwrap(f.rt->db().get_msg(first.user_message_id))->metadata == metadata);
  }

  TEST_CASE("revision cannot refer to a missing predecessor or skip its version") {
    for (bool missing : {false, true}) {
      ActiveTaskFixture f;
      f.reply();
      unwrap(f.rt->chat().send("Accept first version.", f.options()));
      f.spec["previous_product_ref"] = f.spec["product_ref"];
      f.spec["product_ref"]["id"] = "bad-successor";
      f.spec["version"] = missing ? 2 : 3;
      if (missing) f.spec["previous_product_ref"]["id"] = "never-accepted-product";
      CHECK_FALSE(f.rt->chat().send("Reject broken lineage.", f.options()));
      CHECK(f.transport->requests().size() == 1);
    }
  }

  TEST_CASE("structural mutations reject unknown sources cyclic supersession and future provenance") {
    ActiveTaskFixture f;
    std::vector<Json> invalid;
    invalid.push_back(f.spec);
    invalid.back()["statements"][2]["source_event_ids"] = Json::array({"invented-source"});
    invalid.push_back(f.spec);
    invalid.back()["statements"][0]["supersedes"] = Json::array({"rejected"});
    invalid.back()["statements"][1]["supersedes"] = Json::array({"old-style"});
    invalid.push_back(f.spec);
    invalid.back()["source_refs"][0]["known_at"] = "2026-10-01T00:00:00Z";
    invalid.push_back(f.spec);
    invalid.back()["known_at"] = "2026-02-30T00:00:00Z";
    invalid.push_back(f.spec);
    invalid.back()["compiled_instruction"]["source_map"][0]["span"]["byte_len"] =
        std::numeric_limits<std::uint64_t>::max();
    for (const auto& spec : invalid) {
      auto input = f.option_json();
      input["active_task_spec"] = spec;
      CHECK_FALSE(ChatOptions::from_json(input));
    }
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("metadata-only maximal revision cannot replace durable accepted version") {
    ActiveTaskFixture f;
    f.reply();
    auto first = unwrap(f.rt->chat().send("Accept first.", f.options()));
    auto message = unwrap(f.rt->db().get_msg(first.user_message_id));
    auto metadata = message->metadata;
    metadata["active_task"]["supplied_spec"]["version"] = std::numeric_limits<std::uint64_t>::max();
    MsgPatch patch;
    patch.metadata = metadata;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(first.user_message_id, patch));
    f.spec["previous_product_ref"] = f.spec["product_ref"];
    f.spec["product_ref"]["id"] = "overflow-successor";
    f.spec["version"] = 2;
    f.reply();
    auto successor = unwrap(f.rt->chat().send("Extend the durable revision.", f.options()));
    CHECK(f.transport->requests().size() == 2);
    CHECK(f.saved_active_task(successor)["supplied_spec"] == f.spec);
    CHECK(unwrap(f.rt->db().get_msg(first.user_message_id))->metadata == metadata);
  }
}
