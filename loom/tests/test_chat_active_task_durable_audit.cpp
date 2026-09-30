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

// Independently authored synthetic acceptance-boundary counterexamples. These
// exercise Runtime/EventLog with a scripted transport, not an evaluation corpus,
// user exports, credentials, or a live provider.
struct DurableAuditFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  DurableAuditFixture() {
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
    conv = unwrap(rt->db().create_conv("Independent durable acceptance audit")).id;
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
};

}  // namespace

TEST_SUITE("chat_active_task_durable_audit") {
  TEST_CASE("malformed authoritative event cannot disappear into a fresh task head") {
    DurableAuditFixture f;
    unwrap(f.rt->chat().send("Accept the first product.", f.options(f.spec)));
    const auto before_rows = f.rows();
    const auto before_calls = f.transport->requests().size();
    unwrap(EventLog(f.rt->db()).append("chat.active_task.accepted.v1", f.conv,
      Json{{"schema", "malformed.synthetic.event"}}));
    const auto evidence = f.events();
    auto sent = f.rt->chat().send("Reject this successor without erasing the corrupt evidence.",
      f.options(f.successor()));
    CHECK_FALSE(sent);
    CHECK(f.rows() == before_rows);
    CHECK(f.transport->requests().size() == before_calls);
    REQUIRE(f.events().size() == evidence.size());
    CHECK(f.events().back().payload == evidence.back().payload);
  }

  TEST_CASE("acceptance ancestry after the five hundredth event governs replay") {
    DurableAuditFixture f;
    unwrap(f.rt->chat().send("Accept the first product.", f.options(f.spec)));
    const auto accepted = f.events("chat.active_task.accepted.v1");
    REQUIRE(accepted.size() == 1);
    // Repeated identical observations are redundant, not conflicting products.
    // Fill the first reader page without a costly second implementation of send.
    {
      auto lock = f.rt->db().lock();
      sql::Txn txn(f.rt->db().conn());
      LOOM_REQUIRE_OK(txn.begin_status());
      for (int i = 0; i < 505; ++i) {
        unwrap(EventLog(f.rt->db()).append(accepted[0].type, f.conv, accepted[0].payload,
          accepted[0].input_hash, accepted[0].output_hash));
      }
      LOOM_REQUIRE_OK(txn.commit());
    }
    const Json next = f.successor();
    unwrap(f.rt->chat().send("Accept the successor beyond page one.", f.options(next)));
    REQUIRE(f.events("chat.active_task.accepted.v1").size() == 507);
    const auto before_rows = f.rows();
    const auto before_calls = f.transport->requests().size();
    auto stale = f.rt->chat().send("The old root is no longer the current product.", f.options(f.spec));
    CHECK_FALSE(stale);
    CHECK(f.rows() == before_rows);
    CHECK(f.transport->requests().size() == before_calls);
    unwrap(f.rt->chat().send("An exact latest-product replay remains supported.", f.options(next)));
    CHECK(f.transport->requests().size() == before_calls + 1);
  }

  TEST_CASE("failed baseline or acceptance insert rolls back legacy import and current message together") {
    for (const std::string rejected_type : {"chat.active_task.baseline.v1", "chat.active_task.accepted.v1"}) {
      DurableAuditFixture f;
      const auto legacy = f.add_retained(f.preview(f.spec));
      REQUIRE(f.events().empty());
      const auto before_rows = f.rows();
      const auto source_before = unwrap(f.rt->db().get_msg(f.source.id))->to_json();
      {
        auto lock = f.rt->db().lock();
        LOOM_REQUIRE_OK(f.rt->db().conn().exec(
          "CREATE TRIGGER synthetic_reject_task_baseline BEFORE INSERT ON loom_events "
          "WHEN NEW.type = '" + rejected_type + "' "
          "BEGIN SELECT RAISE(ABORT, 'synthetic acceptance write failure'); END"));
      }
      auto failed = f.rt->chat().send("This attempt must roll back.", f.options(f.successor()));
      CHECK_FALSE(failed);
      CHECK(f.rows() == before_rows);
      CHECK(f.events().empty());
      CHECK(f.transport->requests().empty());
      CHECK(unwrap(f.rt->db().get_msg(legacy.id))->to_json() == legacy.to_json());
      CHECK(unwrap(f.rt->db().get_msg(f.source.id))->to_json() == source_before);
      {
        auto lock = f.rt->db().lock();
        LOOM_REQUIRE_OK(f.rt->db().conn().exec("DROP TRIGGER synthetic_reject_task_baseline"));
      }
      unwrap(f.rt->chat().send("Retry after the storage fault is removed.", f.options(f.successor())));
      CHECK(f.events("chat.active_task.baseline.v1").size() == 1);
      CHECK(f.events("chat.active_task.legacy_observed.v1").size() == 1);
      CHECK(f.events("chat.active_task.accepted.v1").size() == 1);
      CHECK(unwrap(f.rt->db().get_msg(legacy.id))->to_json() == legacy.to_json());
    }
  }

  TEST_CASE("post-baseline metadata injection cannot change durable authority") {
    DurableAuditFixture f;
    Json competing = f.spec;
    competing["product_ref"]["id"] = "injected.competing.root";
    const auto injected_snapshot = f.preview(competing);
    REQUIRE(f.events().empty());
    unwrap(f.rt->chat().send("Establish durable authority.", f.options(f.spec)));
    const auto injected = f.add_retained(injected_snapshot, "Synthetic later metadata injection.");
    const auto malformed = f.add_retained(Json{{"schema", "malformed.synthetic.metadata"}});
    unwrap(f.rt->chat().send("The real successor still extends the durable product.", f.options(f.successor())));
    CHECK(f.events("chat.active_task.baseline.v1").size() == 1);
    CHECK(f.events("chat.active_task.legacy_observed.v1").empty());
    CHECK(f.events("chat.active_task.accepted.v1").size() == 2);
    CHECK(unwrap(f.rt->db().get_msg(injected.id))->to_json() == injected.to_json());
    CHECK(unwrap(f.rt->db().get_msg(malformed.id))->to_json() == malformed.to_json());
  }

  TEST_CASE("same product cannot acquire changed source role or attachments under the same text hash") {
    for (bool change_role : {false, true}) {
      DurableAuditFixture f;
      unwrap(f.rt->chat().send("Accept the source snapshot.", f.options(f.spec)));
      const auto evidence = f.events("chat.active_task.accepted.v1");
      REQUIRE(evidence.size() == 1);
      MsgPatch patch;
      if (change_role) patch.role = "assistant";
      else patch.attachments = Json::array({"synthetic-attachment-reference.txt"});
      LOOM_REQUIRE_OK(f.rt->db().update_msg(f.source.id, patch));
      REQUIRE(unwrap(f.rt->db().get_msg(f.source.id))->text == f.source.text);
      const auto before_rows = f.rows();
      const auto before_calls = f.transport->requests().size();
      auto replay = f.rt->chat().send("This identity already names the original source snapshot.", f.options(f.spec));
      CHECK_FALSE(replay);
      CHECK(f.rows() == before_rows);
      CHECK(f.transport->requests().size() == before_calls);
      const auto after = f.events("chat.active_task.accepted.v1");
      REQUIRE(after.size() == 1);
      CHECK(after[0].payload == evidence[0].payload);
    }
  }

  TEST_CASE("explicit replacement binding preserves moved ancestor without selecting its new conversation") {
    DurableAuditFixture f;
    unwrap(f.rt->chat().send("Accept the original source snapshot.", f.options(f.spec)));
    const Json original_bindings = f.bindings;
    const auto other = unwrap(f.rt->db().create_conv("Synthetic other conversation")).id;
    MsgPatch move;
    move.conv_id = other;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(f.source.id, move));
    NewMessage replacement;
    replacement.conv_id = f.conv;
    replacement.role = "user";
    replacement.text = "Current source replacement: preserve references and identify uncertainty.";
    const auto replacement_id = unwrap(f.rt->db().create_msg(replacement));
    f.bindings["source.1"] = Json{{"message_id", replacement_id}, {"text_sha256", Sha256::hex(replacement.text)}};
    Json next = f.successor();
    next["source_refs"][0]["locator"] = Json{{"source", "synthetic.native.replacement"}};
    next["source_refs"][0]["quote"] = replacement.text;
    auto options = f.options(next);
    options.trace_context = true;
    const auto accepted = unwrap(f.rt->chat().send("Use the explicit current replacement.", options));
    const auto retained = unwrap(f.rt->db().get_msg(accepted.user_message_id))->metadata.at("active_task");
    REQUIRE(retained["source_messages"].size() == 1);
    CHECK(retained["source_messages"][0]["message_id"] == replacement_id);
    CHECK(retained["source_messages"][0]["text"] == replacement.text);
    REQUIRE(retained["inherited_source_messages"].size() == 1);
    CHECK(retained["inherited_source_messages"][0]["source_message"]["message_id"] == f.source.id);
    CHECK(retained["inherited_source_messages"][0]["source_message"]["text"] == f.source.text);
    for (const auto& id : accepted.context_trace["history_message_ids"]) CHECK(id != f.source.id);
    for (const auto& id : accepted.context_trace["replaced_history_message_ids"]) CHECK(id != f.source.id);
    CHECK(unwrap(f.rt->db().get_msg(f.source.id))->conv_id == other);

    // Historical evidence can survive relocation; a current explicit binding
    // cannot select a row outside the request's declared conversation.
    f.bindings = original_bindings;
    const auto before_rows = f.rows();
    const auto before_calls = f.transport->requests().size();
    auto outside = f.rt->chat().send("Do not select the moved row as current evidence.", f.options(next));
    CHECK_FALSE(outside);
    CHECK(f.rows() == before_rows);
    CHECK(f.transport->requests().size() == before_calls);
    CHECK(f.events("chat.active_task.accepted.v1").size() == 2);
  }
}
