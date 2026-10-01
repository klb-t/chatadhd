// Define only for the baseline standalone red/green probe; ordinary CMake
// builds use the repository's existing doctest main.
#ifdef LOOM_ACCEPTANCE_ABA_STANDALONE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif
#include <doctest/doctest.h>

#include <sqlite3.h>
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

// Deterministic native-history ABA scheduling through SQLite public tracing
// and a separately opened Database. No sleeps, real data or live provider.
struct AbaFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  AbaFixture() {
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

TEST_SUITE("chat_active_task_history_aba") {
  TEST_CASE("history that changes A to B during capture and returns to A cannot be accepted as unchanged") {
    AbaFixture f;
    auto second = unwrap(Database::open(f.rt->paths().db));
    NewMessage ordinary;
    ordinary.conv_id = f.conv;
    ordinary.role = "assistant";
    ordinary.text = "SYNTHETIC_HISTORY_STATE_A";
    const auto ordinary_id = unwrap(f.rt->db().create_msg(ordinary));
    const auto rows_before = f.rows();
    const auto original_row = unwrap(f.rt->db().get_msg(ordinary_id))->to_json();
    struct Schedule {
      Database& writer;
      std::string message_id;
      bool armed = false;
      bool restored = false;
      std::string error;
    } schedule{*second, ordinary_id, false, false, {}};
    // PROFILE fires after every row of this read has been consumed. The
    // returned vector therefore contains B, while the second connection puts
    // A back before build_messages returns and final validation begins.
    const int installed = sqlite3_trace_v2(f.rt->db().conn().handle(), SQLITE_TRACE_PROFILE,
      [](unsigned, void* context, void* statement, void*) -> int {
        auto& state = *static_cast<Schedule*>(context);
        const char* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
        if (!state.armed || !sql || std::string_view(sql) !=
            "SELECT * FROM messages WHERE conv_id = ? AND status = 'active' ORDER BY created, rowid") return 0;
        state.armed = false;
        MsgPatch patch;
        patch.text = "SYNTHETIC_HISTORY_STATE_A";
        auto changed = state.writer.update_msg(state.message_id, patch);
        if (changed) state.restored = true;
        else state.error = changed.error().to_string();
        return 0;
      }, &schedule);
    REQUIRE(installed == SQLITE_OK);
    struct TraceGuard {
      sqlite3* db;
      ~TraceGuard() { sqlite3_trace_v2(db, 0, nullptr, nullptr); }
    } trace_guard{f.rt->db().conn().handle()};
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      MsgPatch patch;
      patch.text = "SYNTHETIC_HISTORY_STATE_B";
      auto changed = second->update_msg(ordinary_id, patch);
      if (!changed) return changed.error();
      schedule.armed = true;
      return Json{{"prompt", "Independent synthetic context."}, {"context_set", Json::object()}};
    });
    auto options = f.options(f.spec);
    options.knowledge_context = context::ContextRequest{};
    int callbacks = 0;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { ++callbacks; };
    const auto result = f.rt->chat().send("Reject historical ABA.", options, cb);
    INFO(schedule.error);
    REQUIRE(schedule.restored);
    CHECK(unwrap(f.rt->db().get_msg(ordinary_id))->to_json() == original_row);
    const auto requests = f.transport->requests();
    if (!requests.empty()) {
      const auto body = unwrap(json::parse(requests.front().body));
      bool stale_b = false;
      for (const auto& message : body["messages"])
        stale_b = stale_b || message.value("content", Json()) == "SYNTHETIC_HISTORY_STATE_B";
      // In a vulnerable baseline, show that B actually reached the compiled
      // provider payload while the final native row is back at A.
      CHECK(stale_b);
    }
    CHECK_FALSE(result);
    if (!result) CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(f.rows() == rows_before);
    CHECK(callbacks == 0);
    CHECK(requests.empty());
    CHECK(f.events().empty());
  }
}
