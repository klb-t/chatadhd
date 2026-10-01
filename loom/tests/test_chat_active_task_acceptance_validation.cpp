#include <doctest/doctest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

// Synthetic complete-representation and journal-format checks through public
// Runtime/EventLog. Fixture shape follows the independent audit suite.
struct ValidationFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  ValidationFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://durable-audit.test");
    rt->config().set("default_model", "synthetic/model");
    rt->config().set("semantic_analysis", false);
    rt->config().set("auto_title", false);
    rt->secrets().set("api_key", "synthetic-durable-audit-key");
    transport->set_fallback(net::ScriptedTransport::Reply::json(200,
      Json{{"choices", Json::array({Json{{"message", {{"content", "Synthetic reply."}}}}})}}));
    conv = unwrap(rt->db().create_conv("Independent acceptance fold audit")).id;
    NewMessage message;
    message.conv_id = conv;
    message.role = "user";
    message.text = "Preserve the report's source references.";
    source = *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
    bindings = Json{{"source.1", {{"message_id", source.id}, {"text_sha256", Sha256::hex(source.text)}}}};
    spec = Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", "audit.product.1"}}},
      {"goal_id", "audit.goal"}, {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "audit.task"}}},
      {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-10-01T00:00:00Z"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "synthetic.independent.durability"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"source.1"})},
      {"source_refs", Json::array({Json{{"event_id", "source.1"},
        {"locator", {{"source", "synthetic.native"}}},
        {"known_at", "2026-10-01T00:00:00Z"}, {"quote", source.text}}})},
      {"statements", Json::array({Json{{"id", "goal"}, {"kind", "goal"}, {"status", "active"},
        {"text", source.text}, {"source_event_ids", Json::array({"source.1"})},
        {"claim_ids", Json::array()}, {"conditions", Json::array()}, {"supersedes", Json::array()}}})},
      {"compiled_instruction", {{"text", "x"}, {"source_map", Json::array({Json{
        {"span", {{"byte_start", 0}, {"byte_len", 1}}}, {"statement_ids", Json::array({"goal"})}}})}}}
    };
  }

  ChatOptions options(const Json& product) const {
    return unwrap(ChatOptions::from_json(Json{{"conv_id", conv}, {"stream", false},
      {"include_memory", false}, {"include_graph_memory", false}, {"trace_context", false},
      {"active_task_spec", product}, {"active_task_bindings", bindings}}));
  }

  Json successor() const {
    Json next = spec;
    next["version"] = 2;
    next["previous_product_ref"] = spec["product_ref"];
    next["product_ref"]["id"] = "audit.product.2";
    next["statements"][0]["text"] = "Preserve references and expose unresolved claims.";
    return next;
  }

  Json preview(const Json& product) {
    Json trace;
    unwrap(rt->chat().build_messages(conv, "Read-only preview.", {}, options(product), "", &trace));
    return trace.at("active_task");
  }

  Message add_retained(const Json& snapshot, const std::string& text = "Synthetic legacy acceptance.") {
    NewMessage message;
    message.conv_id = conv;
    message.role = "user";
    message.text = text;
    message.metadata = Json{{"active_task", snapshot}, {"unrelated", "preserve legacy evidence"}};
    return *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
  }

  std::vector<EventRecord> events(std::string type = "chat.active_task.*") {
    EventQuery query;
    query.type = std::move(type);
    query.subject_id = conv;
    query.limit = 10000;  // Audit the complete fixture independently of reader paging.
    return unwrap(EventLog(rt->db()).query(query));
  }

  std::size_t rows() { return unwrap(rt->db().get_msgs(conv, true)).size(); }

  Json journal() {
    Json result = Json::array();
    for (const auto& event : events()) result.push_back(event.to_json());
    return result;
  }

  void reject_without_effects(const Json& product) {
    const auto before_rows = rows();
    const auto before_calls = transport->requests().size();
    const auto before_events = journal();
    int callbacks = 0;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { ++callbacks; };
    auto result = rt->chat().send("Reject malformed synthetic authority.", options(product), cb);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(rows() == before_rows);
    CHECK(transport->requests().size() == before_calls);
    CHECK(callbacks == 0);
    CHECK(journal() == before_events);
  }

  void append_snapshot(const Json& snapshot, std::string_view origin) {
    unwrap(EventLog(rt->db()).append("chat.active_task.accepted.v1", conv,
      Json{{"schema", "loom.chat_active_task_acceptance/1"},
           {"acceptance", "explicit_caller_supplied"},
           {"originating_message_id", origin}, {"accepted_snapshot", snapshot},
           {"snapshot_sha256", Sha256::hex(json::dump(snapshot))}}));
  }
};

}  // namespace

TEST_SUITE("chat_active_task_acceptance_validation") {
  TEST_CASE("rehashed incomplete or ill-typed snapshots fail before any accepting effect") {
    const std::vector<std::pair<std::string, std::function<void(Json&)>>> mutations{
      {"missing coverage", [](Json& j) { j.erase("history_coverage_message_ids"); }},
      {"invented coverage", [](Json& j) { j["history_coverage_message_ids"].push_back("not-covered"); }},
      {"history mode", [](Json& j) { j["history_mode"] = "implicit_reset"; }},
      {"false owner acceptance", [](Json& j) { j["acceptance"] = "owner_confirmed"; }},
      {"compiler", [](Json& j) { j["compiler"]["version"] = "unknown"; }},
      {"verification", [](Json& j) { j["binding_verification"] = "unchecked"; }},
      {"source selection", [](Json& j) { j["source_selection"] = "all_conversations"; }},
      {"source role", [](Json& j) { j["source_messages"][0]["role"] = 1; }},
      {"source status", [](Json& j) { j["source_messages"][0]["status"] = "excluded"; }},
      {"source timestamp", [](Json& j) { j["source_messages"][0]["created"] = false; }},
      {"missing attachment references", [](Json& j) { j["source_messages"][0].erase("attachments"); }},
      {"source version group", [](Json& j) { j["source_messages"][0]["version_group_id"] = Json::array(); }},
      {"source version", [](Json& j) { j["source_messages"][0]["version_num"] = true; }},
      {"overflow source version", [](Json& j) { j["source_messages"][0]["version_num"] = std::numeric_limits<std::uint64_t>::max(); }},
      {"unknown snapshot field", [](Json& j) { j["future_unvalidated_field"] = true; }},
      {"unknown source field", [](Json& j) { j["source_messages"][0]["future_unvalidated_field"] = true; }}
    };
    for (const auto& [name, mutate] : mutations) {
      CAPTURE(name);
      ValidationFixture f;
      unwrap(f.rt->chat().send("Accept valid root.", f.options(f.spec)));
      auto corrupt = f.events("chat.active_task.accepted.v1").front().payload["accepted_snapshot"];
      mutate(corrupt);
      f.append_snapshot(corrupt, "new.corrupt.origin"); // freshly recomputed envelope hash
      f.reject_without_effects(f.successor());
    }
  }

  TEST_CASE("first acceptance of a successor cannot invent structurally valid inherited evidence") {
    ValidationFixture f;
    unwrap(f.rt->chat().send("Accept root.", f.options(f.spec)));
    NewMessage source;
    source.conv_id = f.conv;
    source.role = "user";
    source.text = "A distinct current refinement.";
    const auto mid = unwrap(f.rt->db().create_msg(source));
    f.bindings["source.1"] = Json{{"message_id", mid}, {"text_sha256", Sha256::hex(source.text)}};
    auto next = f.successor();
    next["source_refs"][0]["quote"] = source.text;
    auto corrupted = f.preview(next);
    REQUIRE(corrupted["inherited_source_messages"].size() == 1);
    // This is the first v2 event, so equal-product replay checks cannot expose
    // the corruption. Only comparison with the actual ancestor can do so.
    auto& inherited = corrupted["inherited_source_messages"][0]["source_message"];
    inherited["role"] = "invented-but-structurally-valid-role";
    f.append_snapshot(corrupted, "first.corrupted.successor.origin");
    f.reject_without_effects(next);
  }

  TEST_CASE("legacy recovery is an unordered set and preserves duplicate observations") {
    ValidationFixture f;
    const auto root = f.preview(f.spec);
    f.add_retained(root);
    const auto next = f.successor();
    const auto second = f.preview(next);
    auto legacy = [&](const Json& snapshot, std::string_view mid) {
      unwrap(EventLog(f.rt->db()).append("chat.active_task.legacy_observed.v1", f.conv,
        Json{{"schema", "loom.chat_active_task_acceptance/1"}, {"acceptance", "legacy_retention_observed"},
          {"originating_message_id", mid}, {"accepted_snapshot", snapshot},
          {"snapshot_sha256", Sha256::hex(json::dump(snapshot))}}));
    };
    legacy(second, "observed.version.two");
    legacy(root, "observed.version.one");
    legacy(root, "observed.duplicate.version.one");
    unwrap(EventLog(f.rt->db()).append("chat.active_task.baseline.v1", f.conv,
      Json{{"schema", "loom.chat_active_task_baseline/1"}, {"legacy_records", 3},
        {"evidence_boundary", "visible_top_level_metadata_all_statuses_at_upgrade"}}));
    Json third = next;
    third["version"] = 3;
    third["previous_product_ref"] = next["product_ref"];
    third["product_ref"]["id"] = "audit.product.3";
    unwrap(f.rt->chat().send("Extend recovered version two.", f.options(third)));
    CHECK(f.events("chat.active_task.legacy_observed.v1").size() == 3);
    CHECK(f.events("chat.active_task.accepted.v1").size() == 1);
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("null source attachments and mixed history-mode replays keep the existing wire contract") {
    ValidationFixture f;
    MsgPatch patch;
    patch.attachments = Json(nullptr);
    LOOM_REQUIRE_OK(f.rt->db().update_msg(f.source.id, patch));
    auto options = f.options(f.spec);
    unwrap(f.rt->chat().send("Accept replacement history.", options));
    options.active_task_history = "append";
    unwrap(f.rt->chat().send("Replay with appended history.", options));
    unwrap(f.rt->chat().send("Explicit successor.", f.options(f.successor())));
    const auto events = f.events("chat.active_task.accepted.v1");
    REQUIRE(events.size() == 3);
    for (const auto& event : events) {
      CHECK(event.payload.size() == 5);
      const auto& snapshot = event.payload["accepted_snapshot"];
      CHECK(snapshot["source_messages"][0]["attachments"].is_null());
      CHECK(event.payload["snapshot_sha256"] == Sha256::hex(json::dump(snapshot)));
    }
    CHECK(events[0].payload["accepted_snapshot"]["history_mode"] == "replace_refinement");
    CHECK(events[1].payload["accepted_snapshot"]["history_mode"] == "append");
    REQUIRE(f.events("chat.active_task.baseline.v1").size() == 1);
    CHECK(f.events("chat.active_task.baseline.v1")[0].payload.size() == 3);
  }

  TEST_CASE("incompatible external journal envelopes and markers remain intact and require explicit migration") {
    for (bool external_marker : {false, true}) {
      ValidationFixture f;
      const auto snapshot = f.preview(f.spec);
      Json payload;
      if (external_marker) {
        payload = Json{{"schema", "loom.chat_active_task_acceptance_baseline/1"}, {"completed", true},
          {"legacy_rows_observed", 0}, {"legacy_acceptances_imported", 0},
          {"legacy_originating_message_ids", Json::array()}, {"last_legacy_acceptance_seq", nullptr},
          {"recovery_boundary", "upgrade-time observation of retained top-level metadata; absent or moved legacy rows are not recovered"}};
      } else {
        payload = Json{{"schema", "loom.chat_active_task_acceptance/1"},
          {"acceptance", "explicit_caller_supplied"}, {"originating_message_id", "external.origin"},
          {"scope", f.spec["scope"]}, {"goal_id", f.spec["goal_id"]}, {"product_ref", f.spec["product_ref"]},
          {"version", f.spec["version"]}, {"previous_product_ref", f.spec["previous_product_ref"]},
          {"accepted_snapshot", snapshot}, {"snapshot_sha256", Sha256::hex(json::canonical(snapshot))}};
      }
      unwrap(EventLog(f.rt->db()).append(external_marker ? "chat.active_task.acceptance_baseline.v1" :
        "chat.active_task.accepted.v1", f.conv, payload));
      f.reject_without_effects(f.spec);
      const auto result = f.rt->chat().build_messages(f.conv, "Read only.", {}, f.options(f.spec));
      REQUIRE_FALSE(result);
      CHECK(result.error().message.find("explicit migration") != std::string::npos);
      CHECK(f.events().back().payload == payload);
    }
  }

  TEST_CASE("same-schema envelope and baseline drift reject without silently extending the format") {
    for (bool corrupt_baseline : {false, true}) {
      ValidationFixture f;
      if (corrupt_baseline) {
        unwrap(EventLog(f.rt->db()).append("chat.active_task.baseline.v1", f.conv,
          Json{{"schema", "loom.chat_active_task_baseline/1"}, {"legacy_records", 0},
            {"evidence_boundary", "visible_top_level_metadata_all_statuses_at_upgrade"}, {"unknown", true}}));
      } else {
        unwrap(f.rt->chat().send("Accept root.", f.options(f.spec)));
        auto payload = f.events("chat.active_task.accepted.v1").front().payload;
        payload["unknown"] = true;
        unwrap(EventLog(f.rt->db()).append("chat.active_task.accepted.v1", f.conv, payload));
      }
      f.reject_without_effects(f.spec);
    }
  }
}
