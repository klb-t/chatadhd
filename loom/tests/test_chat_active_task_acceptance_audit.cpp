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

// Independently authored synthetic journal-fold counterexamples. These
// exercise Runtime/EventLog with a scripted transport, not an evaluation corpus,
// user exports, credentials, or a live provider.
struct AcceptanceAuditFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  AcceptanceAuditFixture() {
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

TEST_SUITE("chat_active_task_acceptance_audit") {
  TEST_CASE("a fresh acceptance origin cannot replay an older head after its successor") {
    AcceptanceAuditFixture f;
    unwrap(f.rt->chat().send("Accept version one.", f.options(f.spec)));
    const auto root = f.events("chat.active_task.accepted.v1").at(0).payload.at("accepted_snapshot");
    const auto next = f.successor();
    unwrap(f.rt->chat().send("Accept version two.", f.options(next)));
    // A new origin is a new acceptance, not an idempotent copy of an old event.
    f.append_snapshot(root, "synthetic.stale.root.origin");
    f.reject_without_effects(next);
  }

  TEST_CASE("a complete final ancestry cannot excuse successor-before-root event order") {
    AcceptanceAuditFixture f;
    const auto root = f.preview(f.spec);
    f.add_retained(root);
    const auto next = f.successor();
    const auto successor = f.preview(next);
    // Assemble valid, individually hashed events in an impossible acceptance
    // order. The extra retained row is only a projection after this marker.
    unwrap(EventLog(f.rt->db()).append("chat.active_task.baseline.v1", f.conv,
      Json{{"schema", "loom.chat_active_task_baseline/1"}, {"legacy_records", 0},
           {"evidence_boundary", "visible_top_level_metadata_all_statuses_at_upgrade"}}));
    f.append_snapshot(successor, "synthetic.reverse.successor.origin");
    f.append_snapshot(root, "synthetic.reverse.root.origin");
    f.reject_without_effects(next);
  }

  TEST_CASE("independent full scopes may interleave ordered revisions and latest replays") {
    AcceptanceAuditFixture f;
    Json other = f.spec;
    other["product_ref"]["id"] = "audit.other.product.1";
    other["scope"]["task_id"] = "audit.other.task";
    other["goal_id"] = "audit.other.goal";
    Json other_next = other;
    other_next["version"] = 2;
    other_next["previous_product_ref"] = other["product_ref"];
    other_next["product_ref"]["id"] = "audit.other.product.2";
    other_next["statements"][0]["text"] = "Expose unresolved claims in the other task.";
    const auto next = f.successor();
    for (const auto& product : {f.spec, other, next, other_next, next}) {
      unwrap(f.rt->chat().send("Accept an ordered scope-local step.", f.options(product)));
    }
    CHECK(f.transport->requests().size() == 5);
    CHECK(f.events("chat.active_task.accepted.v1").size() == 5);
    CHECK(f.events("chat.active_task.baseline.v1").size() == 1);
  }

  TEST_CASE("assistant and system projections cannot bootstrap legacy caller acceptance") {
    for (const std::string role : {"assistant", "system"}) {
      CAPTURE(role);
      AcceptanceAuditFixture f;
      const auto snapshot = f.preview(f.spec);
      const auto retained = f.add_retained(snapshot);
      MsgPatch patch;
      patch.role = role;
      LOOM_REQUIRE_OK(f.rt->db().update_msg(retained.id, patch));
      const auto original = unwrap(f.rt->db().get_msg(retained.id))->to_json();
      f.reject_without_effects(f.successor());
      CHECK(f.events().empty());
      CHECK(unwrap(f.rt->db().get_msg(retained.id))->to_json() == original);
    }
  }

  TEST_CASE("rehashed inherited evidence must still agree with the recorded ancestor") {
    AcceptanceAuditFixture f;
    unwrap(f.rt->chat().send("Accept the original source.", f.options(f.spec)));
    NewMessage refinement;
    refinement.conv_id = f.conv;
    refinement.role = "user";
    refinement.text = "A new source asks to expose unresolved claims.";
    const auto refinement_id = unwrap(f.rt->db().create_msg(refinement));
    f.bindings["source.1"] = Json{{"message_id", refinement_id},
                                 {"text_sha256", Sha256::hex(refinement.text)}};
    auto next = f.successor();
    next["source_refs"][0]["quote"] = refinement.text;
    unwrap(f.rt->chat().send("Accept the distinct refinement source.", f.options(next)));
    auto corrupted = f.events("chat.active_task.accepted.v1").back().payload.at("accepted_snapshot");
    REQUIRE(corrupted["inherited_source_messages"].size() == 1);
    auto& inherited = corrupted["inherited_source_messages"][0]["source_message"];
    inherited["text"] = "Invented inherited bytes absent from the accepted ancestor.";
    inherited["text_sha256"] = Sha256::hex(inherited["text"].get<std::string>());
    // Both the inherited text digest and the current-format envelope digest
    // are internally valid; only lineage reconstruction exposes the forgery.
    f.append_snapshot(corrupted, "synthetic.corrupted.inheritance.origin");
    f.reject_without_effects(next);
  }
}
