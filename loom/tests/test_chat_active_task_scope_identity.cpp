#ifdef LOOM_ACCEPTANCE_SCOPE_STANDALONE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif
#include <doctest/doctest.h>

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

// Public Runtime/EventLog scope identity counterexamples. Raw caller JSON
// remains preserved while the three named scope values define its identity.
struct ScopeFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  ScopeFixture() {
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

TEST_SUITE("chat_active_task_scope_identity") {
  TEST_CASE("reordering scope object keys cannot establish another root for the same named scope") {
    ScopeFixture f;
    unwrap(f.rt->chat().send("Establish original named scope.", f.options(f.spec)));
    Json competing = f.spec;
    competing["product_ref"]["id"] = "reordered.competing.root";
    competing["scope"] = Json{{"task_id", f.spec["scope"]["task_id"]},
      {"branch_id", f.spec["scope"]["branch_id"]}, {"conversation_id", f.conv}};
    REQUIRE(competing["scope"] != f.spec["scope"]); // ordered_json preserves wire order
    for (const auto* key : {"conversation_id", "branch_id", "task_id"})
      REQUIRE(competing["scope"][key] == f.spec["scope"][key]);
    f.reject_without_effects(competing);
  }

  TEST_CASE("a legal successor can reorder scope keys while preserving supplied JSON exactly") {
    ScopeFixture f;
    unwrap(f.rt->chat().send("Establish original named scope.", f.options(f.spec)));
    auto next = f.successor();
    next["scope"] = Json{{"task_id", f.spec["scope"]["task_id"]},
      {"conversation_id", f.conv}, {"branch_id", f.spec["scope"]["branch_id"]}};
    auto options = f.options(next);
    const auto accepted = unwrap(f.rt->chat().send("Extend the same named scope.", options));
    REQUIRE(f.events("chat.active_task.accepted.v1").size() == 2);
    CHECK(f.events("chat.active_task.accepted.v1").back().payload["accepted_snapshot"]["supplied_spec"] == next);
    CHECK(unwrap(f.rt->db().get_msg(accepted.user_message_id))->metadata["active_task"]["supplied_spec"] == next);
    CHECK(f.transport->requests().size() == 2);
    // A latest replay in its retained representation still works, and the old
    // product remains stale regardless of the scope object's member order.
    unwrap(f.rt->chat().send("Replay exact latest representation.", options));
    f.reject_without_effects(f.spec);
  }
}
