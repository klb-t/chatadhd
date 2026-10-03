#include <doctest/doctest.h>

#include <chrono>
#include <future>
#include <thread>
#include <string>
#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <sqlite3.h>
#endif
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

constexpr auto kWait = std::chrono::seconds(5);

// std::async futures can block on destruction precisely when a locking
// regression is under test. A timed-out worker instead keeps its shared-owned
// fixture alive; it cannot access any destroyed stack objects or test asserts.
// Normal successful paths join every worker.
template <class T>
class BoundedWorker {
 public:
  template <class Function>
  explicit BoundedWorker(Function function) {
    std::packaged_task<T()> task(std::move(function));
    future_ = task.get_future();
    thread_ = std::thread(std::move(task));
  }
  ~BoundedWorker() {
    if (thread_.joinable()) thread_.detach();
  }
  BoundedWorker(const BoundedWorker&) = delete;
  BoundedWorker& operator=(const BoundedWorker&) = delete;
  bool ready() { return future_.wait_for(kWait) == std::future_status::ready; }
  T get() {
    thread_.join();
    return future_.get();
  }

 private:
  std::future<T> future_;
  std::thread thread_;
};

struct Gate {
  std::promise<void> promise;
  std::shared_future<void> future = promise.get_future().share();
  void open() { promise.set_value(); }
  bool wait() const { return future.wait_for(kWait) == std::future_status::ready; }
};


// Synthetic transaction/restart counterexamples through Runtime/EventLog.
// Fixture shape follows the independent audit fixture; no evaluation corpus,
// user exports, real credentials, or live provider is used.
struct DurableFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  DurableFixture() {
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
    LOOM_REQUIRE_OK(rt->config().save());
    LOOM_REQUIRE_OK(rt->secrets().save());
    transport->set_fallback(net::ScriptedTransport::Reply::json(200,
      Json{{"choices", Json::array({Json{{"message", {{"content", "Synthetic reply."}}}}})}}));
    conv = unwrap(rt->db().create_conv("Durable acceptance transaction tests")).id;
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

  void reopen() {
    rt.reset();
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
  }

  std::unique_ptr<Runtime> second() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    return unwrap(Runtime::open(options));
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

TEST_SUITE("chat_active_task_durability") {
  TEST_CASE("accepted authority survives metadata relocation and originating row moves after reopen") {
    for (bool move_row : {false, true}) {
      DurableFixture f;
      const auto first = unwrap(f.rt->chat().send("Accept durable root.", f.options(f.spec)));
      const auto accepted = f.events("chat.active_task.accepted.v1");
      REQUIRE(accepted.size() == 1);
      const auto original = accepted[0].payload;
      auto message = *unwrap(f.rt->db().get_msg(first.user_message_id));
      MsgPatch patch;
      if (move_row) {
        patch.conv_id = unwrap(f.rt->db().create_conv("Moved accepting row")).id;
      } else {
        patch.metadata = Json{{"archived_task", message.metadata["active_task"]}, {"owner_note", "preserve"}};
      }
      LOOM_REQUIRE_OK(f.rt->db().update_msg(first.user_message_id, patch));
      f.reopen();
      Json competing = f.spec;
      competing["product_ref"]["id"] = "competing.root";
      const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
      CHECK_FALSE(f.rt->chat().send("Must not reset task.", f.options(competing)));
      CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);
      CHECK(f.transport->requests().size() == 1);
      CHECK(f.events("chat.active_task.accepted.v1")[0].payload == original);
      CHECK(f.events("chat.active_task.accepted.v1")[0].subject_id == f.conv);
      auto retained = *unwrap(f.rt->db().get_msg(first.user_message_id));
      if (!move_row) CHECK(retained.metadata == *patch.metadata);
      else CHECK(retained.conv_id == *patch.conv_id);
      const auto second = unwrap(f.rt->chat().send("Explicit successor.", f.options(f.successor())));
      CHECK_FALSE(second.user_message_id.empty());
      CHECK(f.events("chat.active_task.accepted.v1").size() == 2);
    }
  }

  TEST_CASE("authentication failure retains committed acceptance independently of tracing") {
    DurableFixture f;
    f.rt->secrets().set("api_key", "");
    auto result = f.rt->chat().send("Accept before auth outcome.", f.options(f.spec));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Auth);
    CHECK(f.transport->requests().empty());
    REQUIRE(f.events("chat.active_task.accepted.v1").size() == 1);
    auto events = f.events("chat.active_task.accepted.v1");
    auto message = unwrap(f.rt->db().get_msg(events[0].payload["originating_message_id"].get<std::string>()));
    REQUIRE(message);
    CHECK_FALSE(message->metadata.contains("context_trace"));
    f.reopen();
    Json competing = f.spec;
    competing["product_ref"]["id"] = "competing.after.auth.failure";
    CHECK_FALSE(f.rt->chat().send("Do not reset.", f.options(competing)));
    CHECK(f.events("chat.active_task.accepted.v1").size() == 1);
  }

  TEST_CASE("externally owned transaction rejects before row events callbacks or provider") {
    DurableFixture f;
    const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    bool started = false;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { started = true; };
    sql::Txn outer(f.rt->db().conn());
    LOOM_REQUIRE_OK(outer.begin_status());
    auto preview = f.rt->chat().build_messages(f.conv, "Preview only.", {}, f.options(f.spec));
    REQUIRE(preview);
    auto result = f.rt->chat().send("Cannot commit for caller.", f.options(f.spec), cb);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(result.error().message.find("externally owned transaction") != std::string::npos);
    CHECK_FALSE(started);
    CHECK(f.transport->requests().empty());
    CHECK(f.events().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);
    CHECK(f.rt->db().conn().in_transaction());
    outer.rollback();
  }

  TEST_CASE("acceptance append failure rolls back current row and first baseline atomically") {
    DurableFixture f;
    LOOM_REQUIRE_OK(f.rt->db().conn().exec(
      "CREATE TRIGGER reject_acceptance BEFORE INSERT ON loom_events "
      "WHEN NEW.type='chat.active_task.accepted.v1' BEGIN SELECT RAISE(ABORT, 'test acceptance abort'); END"));
    const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    bool started = false;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { started = true; };
    CHECK_FALSE(f.rt->chat().send("Atomic attempt.", f.options(f.spec), cb));
    CHECK_FALSE(started);
    CHECK(f.transport->requests().empty());
    CHECK(f.events().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);
    CHECK_FALSE(f.rt->db().conn().in_transaction());
    LOOM_REQUIRE_OK(f.rt->db().conn().exec("DROP TRIGGER reject_acceptance"));
    CHECK(f.rt->chat().send("Retry after failure.", f.options(f.spec)));
    CHECK(f.events("chat.active_task.accepted.v1").size() == 1);
  }

  TEST_CASE("separate engine callback observes commit despite metadata relocation and allows successor") {
    DurableFixture f;
    auto other = f.second();
    bool nested_accepted = false;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view mid) {
      CHECK_FALSE(f.rt->db().conn().in_transaction());
      auto row = *unwrap(other->db().get_msg(mid));
      MsgPatch patch;
      patch.metadata = Json{{"moved_projection", row.metadata["active_task"]}};
      LOOM_REQUIRE_OK(other->db().update_msg(mid, patch));
      Json competing = f.spec;
      competing["product_ref"]["id"] = "nested.competing.root";
      CHECK_FALSE(other->chat().send("No competing root.", f.options(competing)));
      nested_accepted = static_cast<bool>(other->chat().send("Nested successor.", f.options(f.successor())));
    };
    auto first = unwrap(f.rt->chat().send("Outer accepted root.", f.options(f.spec), cb));
    CHECK(nested_accepted);
    CHECK(f.transport->requests().size() == 2);
    CHECK(f.events("chat.active_task.accepted.v1").size() == 2);
    const auto metadata = unwrap(f.rt->db().get_msg(first.user_message_id))->metadata;
    CHECK(metadata.contains("moved_projection"));
    CHECK(metadata.contains("active_task"));
    CHECK_FALSE(f.rt->chat().send("Old root remains stale.", f.options(f.spec)));
  }

  TEST_CASE("two independent connections cannot accept competing successors prepared at the same head") {
    auto f = std::make_shared<DurableFixture>();
    unwrap(f->rt->chat().send("Root.", f->options(f->spec)));
    auto second_unique = f->second();
    auto other = std::shared_ptr<Runtime>(std::move(second_unique));
    auto first_ready = std::make_shared<Gate>();
    auto second_ready = std::make_shared<Gate>();
    auto release = std::make_shared<Gate>();
    auto builder = [release](const std::shared_ptr<Gate>& ready, Database& db) {
      return [release, ready, &db](const context::ContextRequest&) -> Result<Json> {
        if (db.conn().in_transaction()) return Error(Errc::Internal, "builder ran inside SQL transaction");
        ready->open();
        if (!release->wait()) return Error(Errc::Timeout, "test release timed out");
        return Json{{"prompt", "frozen derived context"}, {"context_set", Json::object()}};
      };
    };
    f->rt->chat().set_knowledge_context_builder(builder(first_ready, f->rt->db()));
    other->chat().set_knowledge_context_builder(builder(second_ready, other->db()));
    auto left = f->options(f->successor());
    auto right = left;
    (*right.active_task_spec)["product_ref"]["id"] = "different.successor";
    left.knowledge_context = context::ContextRequest{};
    right.knowledge_context = context::ContextRequest{};
    BoundedWorker<Result<ChatResult>> a([f, left] { return f->rt->chat().send("Left attempt.", left); });
    BoundedWorker<Result<ChatResult>> b([f, other, right] { return other->chat().send("Right attempt.", right); });
    const bool both_prepared = first_ready->wait() && second_ready->wait();
    release->open();
    REQUIRE(both_prepared);
    REQUIRE(a.ready());
    REQUIRE(b.ready());
    auto ar = a.get();
    auto br = b.get();
    CHECK(static_cast<bool>(ar) != static_cast<bool>(br));
    CHECK(f->transport->requests().size() == 2); // root and one successor
    CHECK(f->events("chat.active_task.accepted.v1").size() == 2);
    auto messages = unwrap(f->rt->db().get_msgs(f->conv, true));
    std::size_t attempts = 0;
    for (const auto& message : messages) if (message.text == "Left attempt." || message.text == "Right attempt.") ++attempts;
    CHECK(attempts == 1);
  }

  TEST_CASE("history changed by a separate connection during builder rejects compiled request before acceptance") {
    DurableFixture f;
    auto other = f.second();
    NewMessage history;
    history.conv_id = f.conv;
    history.role = "assistant";
    history.text = "Old history.";
    const auto history_id = unwrap(f.rt->db().create_msg(history));
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      CHECK_FALSE(f.rt->db().conn().in_transaction());
      MsgPatch patch;
      patch.text = "New history.";
      auto changed = other->db().update_msg(history_id, patch);
      if (!changed) return changed.error();
      return Json{{"prompt", "derived snapshot"}, {"context_set", Json::object()}};
    });
    auto opts = f.options(f.spec);
    opts.knowledge_context = context::ContextRequest{};
    const auto count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    CHECK_FALSE(f.rt->chat().send("Stale compilation.", opts));
    CHECK(f.events().empty());
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == count);
  }

#if !defined(_WIN32)
  TEST_CASE("process exit before acceptance commit leaves neither row nor events; exit after commit retains both") {
    for (bool after_commit : {false, true}) {
      DurableFixture f;
      const auto initial_count = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
      f.rt.reset(); // fork never inherits an open SQLite connection
      const auto child = fork();
      REQUIRE(child >= 0);
      if (child == 0) {
        f.reopen();
        if (!after_commit) {
          sqlite3_create_function(f.rt->db().conn().handle(), "acceptance_test_exit", 0,
            SQLITE_UTF8, nullptr, [](sqlite3_context*, int, sqlite3_value**) { _exit(73); }, nullptr, nullptr);
          auto trigger = f.rt->db().conn().exec(
            "CREATE TEMP TRIGGER exit_before_acceptance BEFORE INSERT ON loom_events "
            "WHEN NEW.type='chat.active_task.accepted.v1' BEGIN SELECT acceptance_test_exit(); END");
          if (!trigger) _exit(90);
        }
        ChatCallbacks cb;
        if (after_commit) cb.on_start = [](std::string_view, std::string_view) { _exit(74); };
        auto unused = f.rt->chat().send("Interrupted current turn.", f.options(f.spec), cb);
        (void)unused;
        _exit(91);
      }
      int status = 0;
      bool reaped = false;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(child, &status, WNOHANG) == child) { reaped = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (!reaped) { kill(child, SIGKILL); waitpid(child, &status, 0); }
      REQUIRE(reaped);
      REQUIRE(WIFEXITED(status));
      REQUIRE(WEXITSTATUS(status) == (after_commit ? 74 : 73));
      f.reopen();
      CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == initial_count + (after_commit ? 1 : 0));
      CHECK(f.events("chat.active_task.accepted.v1").size() == (after_commit ? 1 : 0));
      CHECK(f.events("chat.active_task.baseline.v1").size() == (after_commit ? 1 : 0));
      if (after_commit) {
        Json competing = f.spec;
        competing["product_ref"]["id"] = "root.after.process.exit";
        CHECK_FALSE(f.rt->chat().send("Cannot reset after crash.", f.options(competing)));
      }
      CHECK(f.transport->requests().empty());
    }
  }
#endif
}
