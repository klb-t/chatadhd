#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

// Independently authored synthetic development cases. Each revision is a
// complete explicit projection, with sources local to that revision. No model,
// archive, or sealed evaluation corpus supplies or grades these instructions.
struct RevisionFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;

  RevisionFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://revision-coverage.test");
    rt->config().set("default_model", "test/revisions");
    rt->config().set("system_prompt", "Test system instructions.");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "synthetic-revision-key");
    conv = unwrap(rt->db().create_conv("Independent revision coverage cases")).id;
  }

  Message source(std::string role, std::string text) {
    NewMessage message;
    message.conv_id = conv;
    message.role = std::move(role);
    message.text = std::move(text);
    message.metadata = Json{{"fixture_origin", "revision-coverage-development"}};
    return *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
  }

  ChatOptions product(std::string id, int version, const Json& previous,
                      const std::vector<Message>& sources, std::string instruction,
                      std::string task_id = "operator-report") const {
    Json events = Json::array();
    Json refs = Json::array();
    Json bindings = Json::object();
    for (std::size_t i = 0; i < sources.size(); ++i) {
      const std::string event = id + ".source." + std::to_string(i);
      events.push_back(event);
      refs.push_back(Json{{"event_id", event},
                          {"locator", {{"source", "synthetic.revision.messages"},
                                       {"json_pointer", "/" + sources[i].id}}},
                          {"known_at", "2026-09-30T19:00:00Z"},
                          {"quote", sources[i].text}});
      bindings[event] = Json{{"message_id", sources[i].id},
                             {"text_sha256", Sha256::hex(sources[i].text)}};
    }
    Json statement{{"id", "goal"}, {"kind", "goal"}, {"status", "active"},
                   {"text", instruction}, {"source_event_ids", events},
                   {"claim_ids", Json::array()}, {"conditions", Json::array()},
                   {"supersedes", Json::array()}};
    Json spec{{"schema", "loom.active_task_spec/1"},
              {"product_ref", {{"kind", "product"}, {"id", id}}},
              {"goal_id", "shared-goal-reference"}, {"knowledge_run", nullptr},
              {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"},
                         {"task_id", task_id}}},
              {"version", version}, {"previous_product_ref", previous},
              {"known_at", "2026-09-30T20:00:00Z"},
              {"representation", "derived_product"},
              {"materializer", {{"id", "independent-revision-fixture"}, {"version", "1"}}},
              {"history_event_ids", events}, {"source_refs", refs},
              {"statements", Json::array({statement})},
              {"compiled_instruction", {{"text", instruction},
                 {"source_map", Json::array({Json{
                   {"span", {{"byte_start", 0}, {"byte_len", instruction.size()}}},
                   {"statement_ids", Json::array({"goal"})}}})}}}};
    return unwrap(ChatOptions::from_json(Json{{"conv_id", conv}, {"stream", false},
        {"include_memory", false}, {"include_graph_memory", false},
        {"active_task_spec", spec}, {"active_task_bindings", bindings}}));
  }

  static Json ref(const ChatOptions& options) {
    return (*options.active_task_spec)["product_ref"];
  }

  void reply(int status = 200) {
    transport->expect("POST", "https://revision-coverage.test/chat/completions",
      net::ScriptedTransport::Reply::json(status, Json{{"choices", Json::array({
        Json{{"message", {{"content", "Fixture response without instruction markers."}}}}})}}));
  }

  ChatResult send(std::string_view text, const ChatOptions& options) {
    reply();
    return unwrap(rt->chat().send(text, options));
  }

  Json payload(std::size_t index) const {
    const auto requests = transport->requests();
    REQUIRE(index < requests.size());
    return unwrap(json::parse(requests[index].body))["messages"];
  }

  Json retained(const ChatResult& result) const {
    const auto message = unwrap(rt->db().get_msg(result.user_message_id));
    REQUIRE(message);
    REQUIRE(message->metadata.contains("active_task"));
    return message->metadata["active_task"];
  }

  void unchanged(const std::vector<Message>& sources) const {
    for (const auto& original : sources) {
      const auto current = unwrap(rt->db().get_msg(original.id));
      REQUIRE(current);
      CHECK(current->to_json() == original.to_json());
    }
  }
};

bool has(const Json& messages, std::string_view marker) {
  for (const auto& message : messages) {
    if (message["content"].is_string() &&
        message["content"].get<std::string>().find(marker) != std::string::npos) return true;
  }
  return false;
}

int occurrences(const Json& messages, std::string_view text) {
  int count = 0;
  for (const auto& message : messages) {
    if (message["role"] == "user" && message["content"] == text) ++count;
  }
  return count;
}

std::set<std::string> replaced(const ChatResult& result) {
  const auto& ids = result.context_trace["replaced_history_message_ids"];
  REQUIRE(ids.is_array());
  return ids.get<std::set<std::string>>();
}

}  // namespace

TEST_SUITE("chat_active_task_revisions") {
  TEST_CASE("new-only revision bindings retain coverage across three accepted revisions") {
    RevisionFixture f;
    const auto unrelated = f.source("user", "UNRELATED_BACKGROUND: preserve the sample catalog.");
    const auto initial = f.source("user", "ORIGINAL_SOURCE: prepare a report in an obsolete style.");
    const auto rejected = f.source("assistant", "REJECTED_ANCESTOR_PLAN: omit the audit trail.");
    const auto v1 = f.product("report.v1", 1, nullptr, {initial, rejected},
                              "FIRST_ACTIVE_INSTRUCTION: preserve the audit trail.");
    const auto r1 = f.send("First acceptance follow-up.", v1);
    const auto retained1 = f.retained(r1);

    const auto revision2 = f.source("user", "SECOND_SOURCE: keep every audit ID and use neutral prose.");
    const auto v2 = f.product("report.v2", 2, RevisionFixture::ref(v1), {revision2},
                              "SECOND_ACTIVE_INSTRUCTION: use neutral prose and keep every audit ID.");
    const auto r2 = f.send("Second acceptance follow-up.", v2);
    const auto retained2 = f.retained(r2);
    const auto request2 = f.payload(1);
    CHECK_FALSE(has(request2, "REJECTED_ANCESTOR_PLAN"));
    CHECK_FALSE(has(request2, "ORIGINAL_SOURCE"));
    CHECK_FALSE(has(request2, "SECOND_SOURCE"));
    CHECK_FALSE(has(request2, "FIRST_ACTIVE_INSTRUCTION"));
    CHECK(has(request2, "SECOND_ACTIVE_INSTRUCTION"));
    CHECK(has(request2, "UNRELATED_BACKGROUND"));
    CHECK(occurrences(request2, "First acceptance follow-up.") == 1);
    CHECK(occurrences(request2, "Second acceptance follow-up.") == 1);
    CHECK(request2.back()["content"] == "Second acceptance follow-up.");
    CHECK(replaced(r2) == std::set<std::string>{initial.id, rejected.id, revision2.id});

    const auto revision3 = f.source("user", "THIRD_SOURCE: retain identifiers including Żółć-17 in the appendix.");
    const auto v3 = f.product("report.v3", 3, RevisionFixture::ref(v2), {revision3},
                              "THIRD_ACTIVE_INSTRUCTION: preserve Żółć-17 and all audit IDs in the appendix.");
    const auto r3 = f.send("Third acceptance follow-up.", v3);
    const auto request3 = f.payload(2);
    for (const auto& marker : {"REJECTED_ANCESTOR_PLAN", "ORIGINAL_SOURCE", "SECOND_SOURCE",
                               "THIRD_SOURCE", "FIRST_ACTIVE_INSTRUCTION", "SECOND_ACTIVE_INSTRUCTION"}) {
      CHECK_FALSE(has(request3, marker));
    }
    CHECK(has(request3, "THIRD_ACTIVE_INSTRUCTION"));
    CHECK(has(request3, "UNRELATED_BACKGROUND"));
    CHECK(occurrences(request3, "First acceptance follow-up.") == 1);
    CHECK(occurrences(request3, "Second acceptance follow-up.") == 1);
    CHECK(occurrences(request3, "Third acceptance follow-up.") == 1);
    CHECK(request3.back()["content"] == "Third acceptance follow-up.");
    CHECK(replaced(r3) == std::set<std::string>{initial.id, rejected.id, revision2.id, revision3.id});
    CHECK(r3.context_trace["messages"] == request3);
    CHECK(f.retained(r1) == retained1);
    CHECK(f.retained(r2) == retained2);
    CHECK(f.retained(r3)["supplied_spec"] == *v3.active_task_spec);
    f.unchanged({unrelated, initial, rejected, revision2, revision3});
  }

  TEST_CASE("explicit append preserves ancestor sources and later replay can replace them again") {
    RevisionFixture f;
    const auto original = f.source("assistant", "ANCESTOR_APPEND_SOURCE: a rejected proposal.");
    const auto v1 = f.product("append.v1", 1, nullptr, {original}, "Compile the accepted report.");
    f.send("Accept first version.", v1);

    const auto correction = f.source("user", "LATEST_APPEND_SOURCE: produce the corrected report.");
    auto v2 = f.product("append.v2", 2, RevisionFixture::ref(v1), {correction},
                        "CURRENT_APPEND_INSTRUCTION: produce the corrected report.");
    v2.active_task_history = "append";
    const auto appended = f.send("Accept second version with source history.", v2);
    CHECK(has(f.payload(1), "ANCESTOR_APPEND_SOURCE"));
    CHECK(has(f.payload(1), "LATEST_APPEND_SOURCE"));
    CHECK(has(f.payload(1), "CURRENT_APPEND_INSTRUCTION"));
    CHECK(replaced(appended).empty());

    // Identical latest-product replay changes request composition, not product
    // identity. A source-identical current follow-up must still be sent once.
    v2.active_task_history = "replace_refinement";
    const auto replayed = f.send(correction.text, v2);
    const auto request = f.payload(2);
    CHECK_FALSE(has(request, "ANCESTOR_APPEND_SOURCE"));
    CHECK(occurrences(request, correction.text) == 1);
    CHECK(request.back()["content"] == correction.text);
    CHECK(has(request, "CURRENT_APPEND_INSTRUCTION"));
    CHECK(replaced(replayed) == std::set<std::string>{original.id, correction.id});
    CHECK(f.retained(replayed)["supplied_spec"] == *v2.active_task_spec);
    f.unchanged({original, correction});
  }

  TEST_CASE("coverage is isolated by task scope even when goal references match") {
    RevisionFixture f;
    const auto other = f.source("user", "OTHER_TASK_SOURCE: keep this independent task in history.");
    const auto task_a = f.product("task-a.v1", 1, nullptr, {other},
                                  "Prepare task A.", "task-a");
    f.send("Accept task A.", task_a);

    const auto current = f.source("user", "TASK_B_SOURCE: replace only this task refinement.");
    const auto task_b = f.product("task-b.v1", 1, nullptr, {current},
                                  "Prepare task B.", "task-b");
    const auto r_b1 = f.send("Accept task B.", task_b);
    CHECK(has(f.payload(1), "OTHER_TASK_SOURCE"));
    CHECK_FALSE(has(f.payload(1), "TASK_B_SOURCE"));
    CHECK(replaced(r_b1) == std::set<std::string>{current.id});

    const auto correction = f.source("user", "TASK_B_CORRECTION: use the updated B instructions.");
    const auto task_b2 = f.product("task-b.v2", 2, RevisionFixture::ref(task_b), {correction},
                                   "Prepare revised task B.", "task-b");
    const auto r_b2 = f.send("Accept revised task B.", task_b2);
    const auto request = f.payload(2);
    CHECK(has(request, "OTHER_TASK_SOURCE"));
    CHECK_FALSE(has(request, "TASK_B_SOURCE"));
    CHECK_FALSE(has(request, "TASK_B_CORRECTION"));
    CHECK(replaced(r_b2) == std::set<std::string>{current.id, correction.id});
    CHECK(occurrences(request, "Accept task A.") == 1);
    CHECK(occurrences(request, "Accept revised task B.") == 1);
    f.unchanged({other, current, correction});
  }

  TEST_CASE("accepted lineage coverage survives provider failure and trace opt-out") {
    RevisionFixture f;
    const auto original = f.source("assistant", "FAILED_PROVIDER_ANCESTOR: a rejected original plan.");
    auto v1 = f.product("failed.v1", 1, nullptr, {original}, "Use the accepted first instruction.");
    v1.trace_context = false;
    f.reply(503);
    const auto failed = f.rt->chat().send("First request fails remotely.", v1);
    REQUIRE_FALSE(failed);
    REQUIRE(f.transport->requests().size() == 1);
    const auto before = unwrap(f.rt->db().get_msgs(f.conv));
    REQUIRE(before.size() == 2);
    CHECK(before.back().metadata.contains("active_task"));
    CHECK_FALSE(before.back().metadata.contains("context_trace"));

    const auto correction = f.source("user", "AFTER_FAILURE_SOURCE: accepted second instruction.");
    const auto v2 = f.product("failed.v2", 2, RevisionFixture::ref(v1), {correction},
                              "AFTER_FAILURE_ACTIVE: execute the second instruction.");
    const auto r2 = f.send("Try the revised request.", v2);
    const auto request = f.payload(1);
    CHECK_FALSE(has(request, "FAILED_PROVIDER_ANCESTOR"));
    CHECK_FALSE(has(request, "AFTER_FAILURE_SOURCE"));
    CHECK(has(request, "AFTER_FAILURE_ACTIVE"));
    CHECK(occurrences(request, "First request fails remotely.") == 1);
    CHECK(occurrences(request, "Try the revised request.") == 1);
    CHECK(replaced(r2) == std::set<std::string>{original.id, correction.id});
    f.unchanged({original, correction});
  }

  TEST_CASE("unseen inherited source edits fail before another request or accepting message") {
    RevisionFixture f;
    const auto original = f.source("user", "ORIGINAL_INHERITED_BYTES: preserve the first instruction.");
    const auto v1 = f.product("edited.v1", 1, nullptr, {original}, "Use the first instruction.");
    const auto first = f.send("Accept the original source bytes.", v1);
    const auto retained1 = f.retained(first);

    MsgPatch patch;
    patch.text = "UNSEEN_CHANGED_BYTES: do not silently hide this later source edit.";
    LOOM_REQUIRE_OK(f.rt->db().update_msg(original.id, patch));
    const auto correction = f.source("user", "SECOND_EDIT_SOURCE: use the revised full instruction.");
    const auto v2 = f.product("edited.v2", 2, RevisionFixture::ref(v1), {correction},
                              "Use the revised full instruction.");
    const auto count = unwrap(f.rt->db().get_msgs(f.conv)).size();
    const auto rejected = f.rt->chat().send("Must not accept unbound source edits.", v2);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.transport->requests().size() == 1);
    CHECK(unwrap(f.rt->db().get_msgs(f.conv)).size() == count);
    CHECK(f.retained(first) == retained1);
    const auto still_edited = unwrap(f.rt->db().get_msg(original.id));
    REQUIRE(still_edited);
    CHECK(still_edited->text == *patch.text);
  }

  TEST_CASE("intentional current rebinding accepts changed bytes without rewriting earlier provenance") {
    RevisionFixture f;
    const auto original = f.source("user", "BEFORE_EXPLICIT_REBIND: initial source text.");
    const auto v1 = f.product("rebound.v1", 1, nullptr, {original}, "Prepare the first report.");
    const auto first = f.send("Accept the initial source.", v1);
    const auto retained1 = f.retained(first);

    MsgPatch patch;
    patch.text = "AFTER_EXPLICIT_REBIND: corrected source text with known bytes.";
    LOOM_REQUIRE_OK(f.rt->db().update_msg(original.id, patch));
    const auto edited = unwrap(f.rt->db().get_msg(original.id));
    REQUIRE(edited);
    const auto correction = f.source("user", "REBIND_CORRECTION: retain the corrected text.");
    const auto v2 = f.product("rebound.v2", 2, RevisionFixture::ref(v1), {*edited, correction},
                              "EXPLICIT_REBIND_ACTIVE: prepare the corrected report.");
    const auto second = f.send("Accept intentionally rebound current source bytes.", v2);
    const auto request = f.payload(1);
    CHECK_FALSE(has(request, "BEFORE_EXPLICIT_REBIND"));
    CHECK_FALSE(has(request, "AFTER_EXPLICIT_REBIND"));
    CHECK_FALSE(has(request, "REBIND_CORRECTION"));
    CHECK(has(request, "EXPLICIT_REBIND_ACTIVE"));
    CHECK(replaced(second) == std::set<std::string>{original.id, correction.id});
    CHECK(f.retained(first) == retained1);
    CHECK(f.retained(first)["source_messages"][0]["text"] == original.text);
    CHECK(f.retained(second)["source_messages"][0]["text"] == edited->text);
    f.unchanged({*edited, correction});
  }

  TEST_CASE("source visibility controls do not reject unseen ancestor edits when no bytes are hidden") {
    for (const bool include_history : {false, true}) {
      CAPTURE(include_history);
      RevisionFixture f;
      const auto original = f.source("user", "CONTROL_SOURCE_BEFORE_EDIT: a prior source.");
      const auto v1 = f.product("control.v1", 1, nullptr, {original}, "Accept the first instruction.");
      f.send("Accept the first version.", v1);
      MsgPatch patch;
      patch.text = "CONTROL_SOURCE_AFTER_EDIT: preserve an unsummarized source edit.";
      LOOM_REQUIRE_OK(f.rt->db().update_msg(original.id, patch));

      const auto correction = f.source("user", "CONTROL_CURRENT_SOURCE: the latest chosen instruction.");
      auto v2 = f.product("control.v2", 2, RevisionFixture::ref(v1), {correction},
                          "CONTROL_CURRENT_ACTIVE: execute the latest chosen instruction.");
      v2.include_history = include_history;
      if (include_history) v2.active_task_history = "append";
      const auto second = f.send("Use the explicit history controls.", v2);
      const auto request = f.payload(1);
      CHECK(has(request, "CONTROL_SOURCE_AFTER_EDIT") == include_history);
      CHECK(has(request, "CONTROL_CURRENT_SOURCE") == include_history);
      CHECK(has(request, "CONTROL_CURRENT_ACTIVE"));
      CHECK(replaced(second).empty());
      CHECK(occurrences(request, "Use the explicit history controls.") == 1);
    }
  }

  TEST_CASE("inactive ancestor sources keep provenance without blocking current refinement") {
    RevisionFixture f;
    const auto original = f.source("user", "INACTIVE_ANCESTOR: prior source later excluded from history.");
    const auto v1 = f.product("inactive.v1", 1, nullptr, {original}, "Prepare the first report.");
    const auto first = f.send("Accept first version before exclusion.", v1);
    const auto retained1 = f.retained(first);
    MsgPatch patch;
    patch.status = "excluded";
    LOOM_REQUIRE_OK(f.rt->db().update_msg(original.id, patch));

    const auto correction = f.source("user", "ACTIVE_SUCCESSOR_SOURCE: prepare the final report.");
    const auto v2 = f.product("inactive.v2", 2, RevisionFixture::ref(v1), {correction},
                              "ACTIVE_SUCCESSOR_INSTRUCTION: prepare the final report.");
    const auto second = f.send("Accept version after exclusion.", v2);
    const auto request = f.payload(1);
    CHECK_FALSE(has(request, "INACTIVE_ANCESTOR"));
    CHECK_FALSE(has(request, "ACTIVE_SUCCESSOR_SOURCE"));
    CHECK(has(request, "ACTIVE_SUCCESSOR_INSTRUCTION"));
    CHECK(replaced(second) == std::set<std::string>{correction.id});
    CHECK(f.retained(first) == retained1);
    const auto stored = unwrap(f.rt->db().get_msg(original.id));
    REQUIRE(stored);
    CHECK(stored->text == original.text);
    CHECK(stored->status == "excluded");
  }

  TEST_CASE("new active source versions require explicit binding before replacing inherited history") {
    RevisionFixture f;
    const auto original = f.source("user", "VERSIONED_ORIGINAL: the source before native editing.");
    const auto v1 = f.product("versioned.v1", 1, nullptr, {original}, "Prepare the initial report.");
    const auto first = f.send("Accept the original native version.", v1);
    const auto retained1 = f.retained(first);

    const auto edited_id = unwrap(f.rt->db().edit_msg(original.id,
        "VERSIONED_ACTIVE_EDIT: a new native sibling containing a correction."));
    REQUIRE(edited_id);
    const auto edited = unwrap(f.rt->db().get_msg(*edited_id));
    REQUIRE(edited);
    REQUIRE(edited->id != original.id);
    CHECK(edited->version_group_id == original.version_group_id);
    CHECK(edited->status == "active");
    const auto old_row = unwrap(f.rt->db().get_msg(original.id));
    REQUIRE(old_row);
    CHECK(old_row->status == "version");
    CHECK(old_row->text == original.text);

    const auto correction = f.source("user", "VERSIONED_NEW_SOURCE: compile the full corrected task.");
    const auto incomplete = f.product("versioned.v2", 2, RevisionFixture::ref(v1), {correction},
                                      "Use the corrected full task.");
    const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    const auto rejected = f.rt->chat().send("Do not accept the unbound active sibling.", incomplete);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.transport->requests().size() == 1);
    CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);

    const auto rebound = f.product("versioned.v2", 2, RevisionFixture::ref(v1), {*edited, correction},
                                   "VERSIONED_REBOUND_ACTIVE: use the corrected full task.");
    const auto second = f.send("Accept explicitly bound active sibling.", rebound);
    const auto request2 = f.payload(1);
    CHECK_FALSE(has(request2, "VERSIONED_ORIGINAL"));
    CHECK_FALSE(has(request2, "VERSIONED_ACTIVE_EDIT"));
    CHECK_FALSE(has(request2, "VERSIONED_NEW_SOURCE"));
    CHECK(has(request2, "VERSIONED_REBOUND_ACTIVE"));
    CHECK(replaced(second) == std::set<std::string>{edited->id, correction.id});
    CHECK(f.retained(first) == retained1);

    // Once explicitly accepted, the sibling belongs to known ancestry; a later
    // complete projection can again provide only its new source bindings.
    const auto next_source = f.source("user", "VERSIONED_THIRD_SOURCE: preserve the accepted edit.");
    const auto v3 = f.product("versioned.v3", 3, RevisionFixture::ref(rebound), {next_source},
                              "VERSIONED_THIRD_ACTIVE: preserve the accepted source edit.");
    const auto third = f.send("Accept the next revision after rebinding.", v3);
    const auto request3 = f.payload(2);
    CHECK_FALSE(has(request3, "VERSIONED_ORIGINAL"));
    CHECK_FALSE(has(request3, "VERSIONED_ACTIVE_EDIT"));
    CHECK_FALSE(has(request3, "VERSIONED_NEW_SOURCE"));
    CHECK_FALSE(has(request3, "VERSIONED_THIRD_SOURCE"));
    CHECK(has(request3, "VERSIONED_THIRD_ACTIVE"));
    CHECK(replaced(third) == std::set<std::string>{edited->id, correction.id, next_source.id});
    CHECK(occurrences(request3, "Accept the next revision after rebinding.") == 1);
    CHECK(f.retained(first) == retained1);
    f.unchanged({*old_row, *edited, correction, next_source});
  }

  TEST_CASE("invalid UTF-8 native source fails even with a matching binding and no optional quote") {
    RevisionFixture f;
    const auto original = f.source("user", "VALID_SOURCE_BEFORE_UTF8_MUTATION");
    auto options = f.product("invalid-utf8.v1", 1, nullptr, {original},
                             "Execute the valid explicit instruction.");
    std::string invalid_text = "INVALID_NATIVE_UTF8:";
    invalid_text.append("\xC3\x28", 2);
    MsgPatch patch;
    patch.text = invalid_text;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(original.id, patch));
    (*options.active_task_spec)["source_refs"][0].erase("quote");
    options.active_task_bindings["invalid-utf8.v1.source.0"]["text_sha256"] = Sha256::hex(invalid_text);

    const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    const auto rejected = f.rt->chat().send("Reject invalid native bytes before persistence.", options);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);
    const auto stored = unwrap(f.rt->db().get_msg(original.id));
    REQUIRE(stored);
    CHECK(stored->text == invalid_text);
  }
}
