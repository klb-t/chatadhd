#include <doctest/doctest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/net/http.h"
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

struct ConcurrentTaskFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;

  ConcurrentTaskFixture() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://active-task-concurrency.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "System instructions.");
    rt->config().set("semantic_analysis", false);
    rt->config().set("auto_title", false);
    rt->secrets().set("api_key", "synthetic-concurrency-test-key");
    conv = unwrap(rt->chat().new_conv("Concurrent active tasks")).id;
    NewMessage message;
    message.conv_id = conv;
    message.role = "user";
    message.text = "SOURCE: prepare a concise report and preserve all audit identifiers.";
    source = *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
    bindings = Json{{"event.0", {{"message_id", source.id}, {"text_sha256", Sha256::hex(source.text)}}}};
    spec = Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", "concurrent-v1"}}},
      {"goal_id", "concurrent-report"}, {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "report"}}},
      {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-09-30T20:00:00Z"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "synthetic-concurrency-test"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"event.0"})},
      {"source_refs", Json::array({Json{
        {"event_id", "event.0"},
        {"locator", {{"source", "synthetic.native.messages"}, {"json_pointer", "/messages/0"}}},
        {"known_at", "2026-09-30T19:00:00Z"}, {"quote", source.text}}})},
      {"statements", Json::array({Json{
        {"id", "goal"}, {"kind", "goal"}, {"status", "active"}, {"text", "VERSION_ONE: prepare the concise report."},
        {"source_event_ids", Json::array({"event.0"})}, {"claim_ids", Json::array()},
        {"conditions", Json::array()}, {"supersedes", Json::array()}}})},
      {"compiled_instruction", {{"text", "x"}, {"source_map", Json::array({Json{
        {"span", {{"byte_start", 0}, {"byte_len", 1}}}, {"statement_ids", Json::array({"goal"})}}})}}}};
    transport->set_fallback(net::ScriptedTransport::Reply::json(200,
      Json{{"choices", Json::array({Json{{"message", {{"content", "Synthetic reply."}}}}})}}));
  }

  ChatOptions options(const Json& product) const {
    return unwrap(ChatOptions::from_json(Json{
      {"conv_id", conv}, {"stream", false}, {"include_memory", false}, {"include_graph_memory", false},
      {"active_task_spec", product}, {"active_task_bindings", bindings}}));
  }

  Json successor(std::string id, std::string text) const {
    Json next = spec;
    next["previous_product_ref"] = spec["product_ref"];
    next["product_ref"]["id"] = std::move(id);
    next["version"] = 2;
    next["statements"][0]["text"] = std::move(text);
    return next;
  }
};

bool contains_text(const Json& messages, std::string_view text) {
  for (const auto& message : messages) {
    if (message["content"].is_string() && message["content"].get<std::string>().find(text) != std::string::npos) return true;
  }
  return false;
}

Json saved_task(ConcurrentTaskFixture& fixture, const ChatResult& result) {
  const auto message = unwrap(fixture.rt->db().get_msg(result.user_message_id));
  REQUIRE(message);
  return message->metadata.at("active_task");
}

}  // namespace

TEST_SUITE("chat_active_task_concurrency") {
  TEST_CASE("competing distinct successors accept exactly one version two product") {
    auto f = std::make_shared<ConcurrentTaskFixture>();
    const auto first = unwrap(f->rt->chat().send("Accept version one.", f->options(f->spec)));
    const auto a_spec = f->successor("successor-a", "VERSION_TWO_A: preserve all audit identifiers.");
    const auto b_spec = f->successor("successor-b", "VERSION_TWO_B: preserve all audit identifiers.");
    const auto a_options = f->options(a_spec);
    const auto b_options = f->options(b_spec);
    auto start = std::make_shared<Gate>();
    auto a_ready = std::make_shared<Gate>();
    auto b_ready = std::make_shared<Gate>();
    BoundedWorker<Result<ChatResult>> a([f, start, a_ready, a_options] {
      a_ready->open();
      if (!start->wait()) return Result<ChatResult>(Error(Errc::Timeout, "competitor A start gate"));
      return f->rt->chat().send("Candidate A.", a_options);
    });
    BoundedWorker<Result<ChatResult>> b([f, start, b_ready, b_options] {
      b_ready->open();
      if (!start->wait()) return Result<ChatResult>(Error(Errc::Timeout, "competitor B start gate"));
      return f->rt->chat().send("Candidate B.", b_options);
    });
    const bool both_waiting = a_ready->wait() && b_ready->wait();
    start->open();
    REQUIRE(both_waiting);
    REQUIRE(a.ready());
    REQUIRE(b.ready());
    auto a_result = a.get();
    auto b_result = b.get();
    REQUIRE(static_cast<bool>(a_result) != static_cast<bool>(b_result));
    const auto& winner = a_result ? *a_result : *b_result;
    const auto& loser = a_result ? b_result : a_result;
    CHECK(loser.error().code == Errc::InvalidArgument);
    CHECK(saved_task(*f, winner)["supplied_spec"] == (a_result ? a_spec : b_spec));
    CHECK(saved_task(*f, first)["supplied_spec"] == f->spec);
    CHECK(f->transport->requests().size() == 2);
    const auto messages = unwrap(f->rt->db().get_msgs(f->conv));
    CHECK(messages.size() == 5);  // one source and two accepted user/assistant pairs
    int candidate_messages = 0;
    int retained_version_two = 0;
    for (const auto& message : messages) {
      if (message.text == "Candidate A." || message.text == "Candidate B.") ++candidate_messages;
      if (message.metadata.contains("active_task") &&
          message.metadata["active_task"]["supplied_spec"]["version"] == 2) ++retained_version_two;
    }
    CHECK(candidate_messages == 1);
    CHECK(retained_version_two == 1);
  }

  TEST_CASE("successor can finish while accepted predecessor is paused in on_start") {
    auto f = std::make_shared<ConcurrentTaskFixture>();
    auto entered = std::make_shared<Gate>();
    auto resume = std::make_shared<Gate>();
    auto callback_released = std::make_shared<bool>(false);
    const auto first_options = f->options(f->spec);
    ChatCallbacks cb;
    cb.on_start = [entered, resume, callback_released](std::string_view, std::string_view) {
      entered->open();
      *callback_released = resume->wait();
    };
    BoundedWorker<Result<ChatResult>> first([f, first_options, cb] {
      return f->rt->chat().send("Accepted predecessor follow-up.", first_options, cb);
    });
    const bool first_accepted = entered->wait();
    if (!first_accepted) resume->open();
    REQUIRE(first_accepted);
    const auto next = f->successor("callback-successor", "VERSION_TWO: retain the full audit appendix.");
    const auto next_options = f->options(next);
    BoundedWorker<Result<ChatResult>> second([f, next_options] {
      return f->rt->chat().send("Concurrent successor follow-up.", next_options);
    });
    const bool successor_completed = second.ready();
    resume->open();
    REQUIRE(successor_completed);
    REQUIRE(first.ready());
    auto first_result = unwrap(first.get());
    auto second_result = unwrap(second.get());
    CHECK(*callback_released);
    CHECK(saved_task(*f, first_result)["supplied_spec"] == f->spec);
    CHECK(saved_task(*f, second_result)["supplied_spec"] == next);
    const auto requests = f->transport->requests();
    REQUIRE(requests.size() == 2);
    const auto second_messages = unwrap(json::parse(requests[0].body))["messages"];
    const auto first_messages = unwrap(json::parse(requests[1].body))["messages"];
    CHECK(first_result.context_trace["messages"] == first_messages);
    CHECK(second_result.context_trace["messages"] == second_messages);
    CHECK(contains_text(first_messages, "VERSION_ONE"));
    CHECK_FALSE(contains_text(first_messages, "VERSION_TWO"));
    CHECK_FALSE(contains_text(first_messages, "Concurrent successor follow-up."));
    CHECK(contains_text(second_messages, "VERSION_TWO"));
    CHECK(contains_text(second_messages, "Accepted predecessor follow-up."));
    CHECK(first_messages.back()["content"] == "Accepted predecessor follow-up.");
    CHECK(second_messages.back()["content"] == "Concurrent successor follow-up.");
  }

  TEST_CASE("on_start permits another thread to edit sources under database then engine locks") {
    auto f = std::make_shared<ConcurrentTaskFixture>();
    auto mutation = std::make_shared<std::promise<Status>>();
    auto mutation_result = mutation->get_future();
    const auto options = f->options(f->spec);
    ChatCallbacks cb;
    cb.on_start = [f, mutation](std::string_view, std::string_view) {
      BoundedWorker<Status> writer([f] {
        auto db_lock = f->rt->db().lock();
        const auto current = f->rt->chat().current_conv();
        if (!current || current->id != f->conv) return Status(Error(Errc::Internal, "current conversation changed"));
        MsgPatch patch;
        patch.text = "SOURCE_CHANGED_BY_CONCURRENT_CALLBACK";
        return f->rt->db().update_msg(f->source.id, patch);
      });
      if (!writer.ready()) {
        mutation->set_value(Error(Errc::Timeout, "callback retained a database or engine lock"));
        return;
      }
      mutation->set_value(writer.get());
    };
    BoundedWorker<Result<ChatResult>> sender([f, options, cb] {
      return f->rt->chat().send("Send the accepted source snapshot.", options, cb);
    });
    REQUIRE(sender.ready());
    auto result = unwrap(sender.get());
    REQUIRE(mutation_result.wait_for(kWait) == std::future_status::ready);
    LOOM_REQUIRE_OK(mutation_result.get());
    CHECK(unwrap(f->rt->db().get_msg(f->source.id))->text == "SOURCE_CHANGED_BY_CONCURRENT_CALLBACK");
    const auto retained = saved_task(*f, result);
    CHECK(retained["source_messages"][0]["text"] == f->source.text);
    CHECK(retained["source_messages"][0]["text_sha256"] == Sha256::hex(f->source.text));
    const auto requests = f->transport->requests();
    REQUIRE(requests.size() == 1);
    const auto messages = unwrap(json::parse(requests[0].body))["messages"];
    CHECK(result.context_trace["messages"] == messages);
    CHECK_FALSE(contains_text(messages, "SOURCE_CHANGED_BY_CONCURRENT_CALLBACK"));
  }

  TEST_CASE("task context builder permits concurrent engine inspection while it owns the database") {
    auto f = std::make_shared<ConcurrentTaskFixture>();
    auto entered = std::make_shared<Gate>();
    auto resume = std::make_shared<Gate>();
    f->rt->chat().set_knowledge_context_builder([entered, resume](const context::ContextRequest&) -> Result<Json> {
      entered->open();
      if (!resume->wait()) return Error(Errc::Timeout, "engine inspection did not complete during compilation");
      return Json{{"prompt", "Concurrent inspection completed."}, {"context_set", Json::object()}};
    });
    auto options = f->options(f->spec);
    options.knowledge_context = context::ContextRequest{};
    BoundedWorker<Result<Json>> compiler([f, options] {
      return f->rt->chat().build_messages(f->conv, "Preview only.", {}, options);
    });
    const bool compiling = entered->wait();
    if (!compiling) resume->open();
    REQUIRE(compiling);
    // build_messages owns the database here. Access through mu_ from another
    // thread must remain possible: the builder is called after copying it and
    // releasing mu_. This is a deterministic lock-scope check, not a proof of
    // every possible contention schedule or of cross-process coordination.
    BoundedWorker<bool> inspector([f] {
      const auto current = f->rt->chat().current_conv();
      f->rt->chat().set_knowledge_context_builder([](const context::ContextRequest&) -> Result<Json> {
        return Json{{"prompt", "Replacement builder."}, {"context_set", Json::object()}};
      });
      return current && current->id == f->conv;
    });
    const bool inspected = inspector.ready();
    resume->open();
    REQUIRE(inspected);
    CHECK(inspector.get());
    REQUIRE(compiler.ready());
    const auto preview = unwrap(compiler.get());
    CHECK(contains_text(preview, "Concurrent inspection completed."));
    CHECK_FALSE(contains_text(preview, "Replacement builder."));
    CHECK(f->transport->requests().empty());
    CHECK(unwrap(f->rt->db().get_msgs(f->conv)).size() == 1);
  }
}
