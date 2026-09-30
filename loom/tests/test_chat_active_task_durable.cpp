#include <doctest/doctest.h>

#include <barrier>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr std::string_view kAcceptedEvent = "chat.active_task.accepted.v1";
constexpr std::string_view kBaselineEvent = "chat.active_task.acceptance_baseline.v1";
constexpr auto kWait = std::chrono::seconds(5);

// Avoid timing sleeps. A test controls the release point, and every worker has
// a bounded observation path so a lock-order regression reports a timeout.
struct Gate {
  std::promise<void> promise;
  std::shared_future<void> future = promise.get_future().share();
  void open() { promise.set_value(); }
  bool wait() const { return future.wait_for(kWait) == std::future_status::ready; }
};

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

std::vector<EventRecord> all_events(EventLog& log, std::string_view type,
                                    std::string_view subject) {
  std::vector<EventRecord> out;
  std::int64_t after = 0;
  while (true) {
    EventQuery query;
    query.after_seq = after;
    query.type = std::string(type);
    query.subject_id = std::string(subject);
    query.limit = 500;
    auto page = unwrap(log.query(query));
    if (page.empty()) break;
    after = page.back().seq;
    out.insert(out.end(), page.begin(), page.end());
    if (page.size() < 500) break;
  }
  return out;
}

struct DurableFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Message source;
  Json spec;
  Json bindings;
  bool provider_key = false;

  explicit DurableFixture(bool with_provider_key = false) : provider_key(with_provider_key) {
    open();
    conv = unwrap(rt->chat().new_conv("Durable active task")).id;
    NewMessage message;
    message.conv_id = conv;
    message.role = "user";
    message.text = "DURABLE_SOURCE: retain the exact source bytes and audit identifier Z-17.";
    message.metadata = Json{{"source_marker", "must survive"}};
    source = *unwrap(rt->db().get_msg(unwrap(rt->db().create_msg(message))));
    bindings = Json{{"event.0", {{"message_id", source.id},
                                  {"text_sha256", Sha256::hex(source.text)}}}};
    spec = make_spec(conv, "durable-product-v1", 1, nullptr,
                     "VERSION_ONE: prepare the durable report.");
  }

  static Json make_spec(std::string_view conversation, std::string product_id,
                        int version, Json previous, std::string instruction,
                        std::string task_id = "durable-report") {
    return Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", std::move(product_id)}}},
      {"goal_id", "durable-goal"}, {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conversation}, {"branch_id", "native:active"},
                 {"task_id", std::move(task_id)}}},
      {"version", version}, {"previous_product_ref", std::move(previous)},
      {"known_at", "2026-10-01T00:00:00Z"}, {"representation", "derived_product"},
      {"materializer", {{"id", "durable-native-test"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"event.0"})},
      {"source_refs", Json::array({Json{
        {"event_id", "event.0"},
        {"locator", {{"source", "synthetic.native.messages"}, {"json_pointer", "/messages/0"}}},
        {"known_at", "2026-09-30T23:59:00Z"},
        {"quote", "DURABLE_SOURCE"}}})},
      {"statements", Json::array({Json{
        {"id", "goal"}, {"kind", "goal"}, {"status", "active"},
        {"text", instruction}, {"source_event_ids", Json::array({"event.0"})},
        {"claim_ids", Json::array()}, {"conditions", Json::array()},
        {"supersedes", Json::array()}}})},
      {"compiled_instruction", {{"text", instruction}, {"source_map", Json::array({Json{
        {"span", {{"byte_start", 0}, {"byte_len", instruction.size()}}},
        {"statement_ids", Json::array({"goal"})}}})}}}
    };
  }

  void open() {
    RuntimeOptions options;
    options.data_dir = dir.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://durable-active-task.test");
    rt->config().set("default_model", "test/durable");
    rt->config().set("system_prompt", "Durable test system prompt.");
    rt->config().set("semantic_analysis", false);
    rt->config().set("auto_title", false);
    if (provider_key) rt->secrets().set("api_key", "synthetic-durable-key");
    transport->set_fallback(net::ScriptedTransport::Reply::json(
        503, Json{{"error", "synthetic provider rejection"}}));
  }

  void reopen() {
    rt.reset();
    open();
  }

  ChatOptions options(const Json& product) const {
    return unwrap(ChatOptions::from_json(Json{
      {"conv_id", conv}, {"stream", false}, {"trace_context", false},
      {"include_memory", false}, {"include_graph_memory", false},
      {"active_task_spec", product}, {"active_task_bindings", bindings}}));
  }

  Json successor(std::string id = "durable-product-v2") const {
    Json next = spec;
    next["product_ref"]["id"] = std::move(id);
    next["version"] = 2;
    next["previous_product_ref"] = spec["product_ref"];
    next["statements"][0]["text"] = "VERSION_TWO: preserve the durable audit appendix.";
    return next;
  }

  Result<ChatResult> send(std::string_view text, const Json& product) {
    return rt->chat().send(text, options(product));
  }

  std::vector<EventRecord> accepted() {
    return all_events(rt->event_log(), kAcceptedEvent, conv);
  }

  std::vector<EventRecord> baselines() {
    return all_events(rt->event_log(), kBaselineEvent, conv);
  }

  Message message_with_text(std::string_view text, std::string_view conversation = {}) {
    auto messages = unwrap(rt->db().get_msgs(conversation.empty() ? conv : conversation, true));
    std::optional<Message> found;
    for (const auto& message : messages) {
      if (message.text == text) {
        REQUIRE_FALSE(found.has_value());
        found = message;
      }
    }
    REQUIRE(found.has_value());
    return *found;
  }

  std::size_t row_count(std::string_view conversation = {}) {
    return unwrap(rt->db().get_msgs(conversation.empty() ? conv : conversation, true)).size();
  }
};

void check_acceptance_payload(const EventRecord& event, const Json& expected_spec) {
  const auto& payload = event.payload;
  CHECK(event.type == kAcceptedEvent);
  CHECK(payload["schema"] == "loom.chat_active_task_acceptance/1");
  CHECK(payload["scope"] == expected_spec["scope"]);
  CHECK(payload["goal_id"] == expected_spec["goal_id"]);
  CHECK(payload["product_ref"] == expected_spec["product_ref"]);
  CHECK(payload["version"] == expected_spec["version"]);
  CHECK(payload["previous_product_ref"] == expected_spec["previous_product_ref"]);
  REQUIRE(payload["accepted_snapshot"].is_object());
  CHECK(payload["accepted_snapshot"]["supplied_spec"] == expected_spec);
  CHECK(payload["snapshot_sha256"] ==
        Sha256::hex(json::canonical(payload["accepted_snapshot"])));
}

}  // namespace

TEST_SUITE("chat_active_task_durable") {
  TEST_CASE("acceptance survives metadata relocation or accepting-row move and runtime reopen") {
    for (const std::string mutation : {"metadata_relocation", "accepting_row_move"}) {
      CAPTURE(mutation);
      DurableFixture f(mutation == "accepting_row_move");
      const auto original_source = f.source.to_json();
      const auto first = f.send("Accept durable version one.", f.spec);
      REQUIRE_FALSE(first);
      CHECK(first.error().code == (f.provider_key ? Errc::Http : Errc::Auth));
      REQUIRE(f.accepted().size() == 1);
      check_acceptance_payload(f.accepted().front(), f.spec);
      const auto accepted_row = f.message_with_text("Accept durable version one.");

      std::string moved_to;
      if (mutation == "metadata_relocation") {
        MsgPatch patch;
        patch.metadata = Json{{"preserved_original_metadata", accepted_row.metadata},
                              {"client_annotation", "synthetic relayout"}};
        LOOM_REQUIRE_OK(f.rt->db().update_msg(accepted_row.id, patch));
      } else {
        moved_to = unwrap(f.rt->db().create_conv("Relocated accepting row")).id;
        MsgPatch patch;
        patch.conv_id = moved_to;
        LOOM_REQUIRE_OK(f.rt->db().update_msg(accepted_row.id, patch));
      }

      f.reopen();
      Json competing = f.spec;
      competing["product_ref"]["id"] = "competing-version-one";
      const auto before_competing = f.row_count();
      const auto requests_before_competing = f.transport->requests().size();
      const auto rejected_root = f.send("A competing root must not persist.", competing);
      REQUIRE_FALSE(rejected_root);
      CHECK(rejected_root.error().code == Errc::InvalidArgument);
      CHECK(f.row_count() == before_competing);
      CHECK(f.transport->requests().size() == requests_before_competing);

      const Json v2 = f.successor();
      const auto accepted_v2 = f.send("Accept exact durable successor.", v2);
      REQUIRE_FALSE(accepted_v2);
      CHECK(accepted_v2.error().code == (f.provider_key ? Errc::Http : Errc::Auth));
      REQUIRE(f.accepted().size() == 2);
      check_acceptance_payload(f.accepted().back(), v2);

      const auto rows_after_v2 = f.row_count();
      const auto stale = f.send("Do not replay stale version one.", f.spec);
      REQUIRE_FALSE(stale);
      CHECK(stale.error().code == Errc::InvalidArgument);
      CHECK(f.row_count() == rows_after_v2);

      const auto replay = f.send("Replay the exact latest product.", v2);
      REQUIRE_FALSE(replay);
      CHECK(replay.error().code == (f.provider_key ? Errc::Http : Errc::Auth));
      REQUIRE(f.accepted().size() == 3);
      check_acceptance_payload(f.accepted().back(), v2);
      CHECK(f.message_with_text("Replay the exact latest product.").metadata["active_task"]
            ["supplied_spec"] == v2);

      const auto retained_source = unwrap(f.rt->db().get_msg(f.source.id));
      REQUIRE(retained_source);
      CHECK(retained_source->to_json() == original_source);
      if (!moved_to.empty()) {
        CHECK(f.message_with_text("Accept durable version one.", moved_to).id == accepted_row.id);
      } else {
        const auto relocated = unwrap(f.rt->db().get_msg(accepted_row.id));
        REQUIRE(relocated);
        CHECK(relocated->metadata["preserved_original_metadata"] == accepted_row.metadata);
      }
    }
  }

  TEST_CASE("exact replay may change history composition without poisoning durable ancestry") {
    DurableFixture f;
    const auto first = f.send("Accept version one with replacement history.", f.spec);
    REQUIRE_FALSE(first);
    CHECK(first.error().code == Errc::Auth);
    REQUIRE(f.accepted().size() == 1);
    CHECK(f.accepted().front().payload["accepted_snapshot"]["history_mode"] ==
          "replace_refinement");

    auto append_replay = f.options(f.spec);
    append_replay.active_task_history = "append";
    const auto replay = f.rt->chat().send("Replay exact version one with appended history.",
                                          append_replay);
    REQUIRE_FALSE(replay);
    CHECK(replay.error().code == Errc::Auth);
    auto replayed_events = f.accepted();
    REQUIRE(replayed_events.size() == 2);
    check_acceptance_payload(replayed_events.back(), f.spec);
    CHECK(replayed_events.back().payload["accepted_snapshot"]["history_mode"] == "append");

    f.reopen();
    const Json v2 = f.successor("history-mode-successor-v2");
    const auto successor = f.send("Accept successor after mixed-mode replay.", v2);
    REQUIRE_FALSE(successor);
    CHECK(successor.error().code == Errc::Auth);
    const auto final_events = f.accepted();
    REQUIRE(final_events.size() == 3);
    check_acceptance_payload(final_events.back(), v2);
    REQUIRE(f.baselines().size() == 1);
  }

  TEST_CASE("durable source snapshot preserves supported null attachments") {
    DurableFixture f;
    MsgPatch imported_shape;
    imported_shape.attachments = Json(nullptr);
    LOOM_REQUIRE_OK(f.rt->db().update_msg(f.source.id, imported_shape));
    const auto stored_source = unwrap(f.rt->db().get_msg(f.source.id));
    REQUIRE(stored_source);
    REQUIRE(stored_source->attachments.is_null());

    const auto accepted = f.send("Accept imported source with null attachments.", f.spec);
    REQUIRE_FALSE(accepted);
    CHECK(accepted.error().code == Errc::Auth);
    const auto events = f.accepted();
    REQUIRE(events.size() == 1);
    check_acceptance_payload(events.front(), f.spec);
    const auto& sources = events.front().payload["accepted_snapshot"]["source_messages"];
    REQUIRE(sources.size() == 1);
    CHECK(sources[0]["message_id"] == f.source.id);
    CHECK(sources[0]["attachments"].is_null());
    CHECK(unwrap(f.rt->db().get_msg(f.source.id))->attachments.is_null());
  }

  TEST_CASE("preview is read-only and send atomically bootstraps visible legacy retention") {
    DurableFixture f;
    Json trace;
    const auto preview = f.rt->chat().build_messages(
        f.conv, "Preview legacy version one.", {}, f.options(f.spec), "", &trace);
    REQUIRE(preview);
    REQUIRE(trace["active_task"].is_object());
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());

    NewMessage legacy;
    legacy.conv_id = f.conv;
    legacy.role = "user";
    legacy.text = "Visible legacy accepted row.";
    legacy.metadata = Json{{"active_task", trace["active_task"]}};
    const auto legacy_id = unwrap(f.rt->db().create_msg(legacy));
    const Json v2 = f.successor("bootstrapped-v2");
    Json successor_trace;
    const auto successor_preview = f.rt->chat().build_messages(
        f.conv, "Preview successor only.", {}, f.options(v2), "", &successor_trace);
    REQUIRE(successor_preview);
    CHECK(successor_trace["active_task"]["supplied_spec"] == v2);
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());

    const auto accepted = f.send("Bootstrap and accept version two.", v2);
    REQUIRE_FALSE(accepted);
    CHECK(accepted.error().code == Errc::Auth);
    const auto events = f.accepted();
    REQUIRE(events.size() == 2);
    CHECK(events[0].payload["acceptance"] == "legacy_retention_observed");
    CHECK(events[0].payload["originating_message_id"] == legacy_id);
    check_acceptance_payload(events[0], f.spec);
    CHECK(events[1].payload["acceptance"] == "explicit_caller_supplied");
    check_acceptance_payload(events[1], v2);
    REQUIRE(f.baselines().size() == 1);
    CHECK(f.message_with_text("Visible legacy accepted row.").metadata["active_task"] ==
          trace["active_task"]);
  }

  TEST_CASE("legacy active-task metadata on an assistant row fails closed without effects") {
    DurableFixture f(true);
    Json trace;
    const auto preview = f.rt->chat().build_messages(
        f.conv, "Build a valid snapshot without accepting it.", {}, f.options(f.spec), "", &trace);
    REQUIRE(preview);
    REQUIRE(trace["active_task"].is_object());
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());

    NewMessage misplaced;
    misplaced.conv_id = f.conv;
    misplaced.role = "assistant";
    misplaced.text = "Assistant row must not establish caller acceptance.";
    misplaced.metadata = Json{{"active_task", trace["active_task"]},
                              {"unrelated", "preserve exactly"}};
    const auto misplaced_id = unwrap(f.rt->db().create_msg(misplaced));
    const auto before = unwrap(f.rt->db().get_msg(misplaced_id));
    REQUIRE(before);
    const auto before_rows = f.row_count();
    int callbacks = 0;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { ++callbacks; };

    const auto rejected = f.rt->chat().send("Do not import assistant metadata.",
                                            f.options(f.spec), cb);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.row_count() == before_rows);
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());
    CHECK(callbacks == 0);
    CHECK(f.transport->requests().empty());
    const auto after = unwrap(f.rt->db().get_msg(misplaced_id));
    REQUIRE(after);
    CHECK(after->to_json() == before->to_json());
  }

  TEST_CASE("moving an inherited source row does not erase its durable snapshot") {
    DurableFixture f;
    REQUIRE_FALSE(f.send("Accept source before its owner moves the row.", f.spec));
    const auto original_source = f.source.to_json();
    const auto archive_conv = unwrap(f.rt->db().create_conv("New owner of old source row")).id;
    MsgPatch move;
    move.conv_id = archive_conv;
    LOOM_REQUIRE_OK(f.rt->db().update_msg(f.source.id, move));
    f.reopen();

    NewMessage correction;
    correction.conv_id = f.conv;
    correction.role = "user";
    correction.text = "CURRENT_SOURCE: add a new requirement without erasing the inherited bytes.";
    const auto current = *unwrap(f.rt->db().get_msg(unwrap(f.rt->db().create_msg(correction))));
    Json v2 = f.successor("moved-inherited-source-v2");
    v2["history_event_ids"] = Json::array({"event.1"});
    v2["source_refs"] = Json::array({Json{
      {"event_id", "event.1"},
      {"locator", {{"source", "synthetic.native.messages"}, {"json_pointer", "/messages/current"}}},
      {"known_at", "2026-09-30T23:59:30Z"}, {"quote", "CURRENT_SOURCE"}}});
    v2["statements"][0]["source_event_ids"] = Json::array({"event.1"});
    Json current_binding{{"event.1", {{"message_id", current.id},
                                       {"text_sha256", Sha256::hex(current.text)}}}};
    auto options = unwrap(ChatOptions::from_json(Json{
      {"conv_id", f.conv}, {"stream", false}, {"trace_context", false},
      {"include_memory", false}, {"include_graph_memory", false},
      {"active_task_spec", v2}, {"active_task_bindings", current_binding}}));
    const auto accepted = f.rt->chat().send("Accept new-only successor after source move.", options);
    REQUIRE_FALSE(accepted);
    CHECK(accepted.error().code == Errc::Auth);
    REQUIRE(f.accepted().size() == 2);
    check_acceptance_payload(f.accepted().back(), v2);
    const auto retained = f.message_with_text("Accept new-only successor after source move.");
    const auto& inherited = retained.metadata["active_task"]["inherited_source_messages"];
    REQUIRE(inherited.size() == 1);
    CHECK(inherited[0]["source_message"]["text"] == f.source.text);
    const auto moved = unwrap(f.rt->db().get_msg(f.source.id));
    REQUIRE(moved);
    Json expected_moved = original_source;
    expected_moved["conv_id"] = archive_conv;
    CHECK(moved->to_json() == expected_moved);
  }

  TEST_CASE("durable authority is isolated by full scope and conversation") {
    DurableFixture f;
    REQUIRE_FALSE(f.send("Accept task in first scope.", f.spec));

    Json same_conversation = f.spec;
    same_conversation["scope"]["task_id"] = "independent-task-in-same-conversation";
    same_conversation["product_ref"]["id"] = "same-conversation-other-task-v1";
    same_conversation["statements"][0]["text"] = "SAME_CONVERSATION_OTHER_TASK: independent root.";
    const auto same_conv_result = f.send("Accept another task in the same conversation.",
                                         same_conversation);
    REQUIRE_FALSE(same_conv_result);
    CHECK(same_conv_result.error().code == Errc::Auth);
    REQUIRE(f.accepted().size() == 2);
    check_acceptance_payload(f.accepted().back(), same_conversation);

    const auto other_conv = unwrap(f.rt->db().create_conv("Independent durable scope")).id;
    NewMessage other_source;
    other_source.conv_id = other_conv;
    other_source.role = "user";
    other_source.text = f.source.text;
    const auto other_source_id = unwrap(f.rt->db().create_msg(other_source));
    Json other_spec = DurableFixture::make_spec(other_conv, "other-scope-v1", 1, nullptr,
                                                "OTHER_SCOPE: independent version one.");
    Json other_bindings{{"event.0", {{"message_id", other_source_id},
                                      {"text_sha256", Sha256::hex(other_source.text)}}}};
    auto other_options = unwrap(ChatOptions::from_json(Json{
      {"conv_id", other_conv}, {"stream", false}, {"trace_context", false},
      {"include_memory", false}, {"include_graph_memory", false},
      {"active_task_spec", other_spec}, {"active_task_bindings", other_bindings}}));
    const auto other = f.rt->chat().send("Accept independent scope.", other_options);
    REQUIRE_FALSE(other);
    CHECK(other.error().code == Errc::Auth);
    REQUIRE(all_events(f.rt->event_log(), kAcceptedEvent, f.conv).size() == 2);
    const auto other_events = all_events(f.rt->event_log(), kAcceptedEvent, other_conv);
    REQUIRE(other_events.size() == 1);
    check_acceptance_payload(other_events.front(), other_spec);
  }

  TEST_CASE("malformed authoritative tail beyond the first 500-event page fails closed") {
    DurableFixture f;
    REQUIRE_FALSE(f.send("Accept durable head before paging.", f.spec));
    auto events = f.accepted();
    REQUIRE(events.size() == 1);
    Json valid_payload = events.front().payload;
    for (int i = 0; i < 499; ++i) {
      valid_payload["originating_message_id"] = "synthetic-replay-" + std::to_string(i);
      LOOM_REQUIRE_OK(f.rt->event_log().append(kAcceptedEvent, f.conv, valid_payload));
    }
    Json malformed = valid_payload;
    malformed["originating_message_id"] = "malformed-page-tail";
    malformed["snapshot_sha256"] = "not-the-canonical-snapshot-digest";
    LOOM_REQUIRE_OK(f.rt->event_log().append(kAcceptedEvent, f.conv, malformed));

    EventQuery first_page;
    first_page.type = std::string(kAcceptedEvent);
    first_page.subject_id = f.conv;
    first_page.limit = 500;
    const auto page_one = unwrap(f.rt->event_log().query(first_page));
    REQUIRE(page_one.size() == 500);
    EventQuery second_page = first_page;
    second_page.after_seq = page_one.back().seq;
    const auto page_two = unwrap(f.rt->event_log().query(second_page));
    REQUIRE(page_two.size() == 1);
    CHECK(page_two.front().payload["originating_message_id"] == "malformed-page-tail");

    const auto before_rows = f.row_count();
    const auto before_requests = f.transport->requests().size();
    const auto rejected = f.send("Malformed authority must fail closed.", f.successor());
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.row_count() == before_rows);
    CHECK(f.transport->requests().size() == before_requests);
  }

  TEST_CASE("two ChatEngine instances sharing one Database accept exactly one distinct successor") {
    auto f = std::make_shared<DurableFixture>();
    REQUIRE_FALSE(f->send("Accept shared-database predecessor.", f->spec));
    ChatEngine second(f->rt->config(), f->rt->secrets(), f->rt->db(), f->rt->bus(),
                      f->rt->http(), f->rt->analyzer(), &f->rt->memory(), &f->rt->graph_memory());
    const Json a_spec = f->successor("shared-db-successor-a");
    const Json b_spec = f->successor("shared-db-successor-b");
    const auto a_options = f->options(a_spec);
    const auto b_options = f->options(b_spec);
    auto release = std::make_shared<Gate>();
    auto a_ready = std::make_shared<Gate>();
    auto b_ready = std::make_shared<Gate>();
    BoundedWorker<Result<ChatResult>> a([f, release, a_ready, a_options] {
      a_ready->open();
      if (!release->wait()) return Result<ChatResult>(Error(Errc::Timeout, "A release gate"));
      return f->rt->chat().send("Shared DB candidate A.", a_options);
    });
    BoundedWorker<Result<ChatResult>> b([f, &second, release, b_ready, b_options] {
      b_ready->open();
      if (!release->wait()) return Result<ChatResult>(Error(Errc::Timeout, "B release gate"));
      return second.send("Shared DB candidate B.", b_options);
    });
    const bool ready = a_ready->wait() && b_ready->wait();
    release->open();
    REQUIRE(ready);
    REQUIRE(a.ready());
    REQUIRE(b.ready());
    const auto a_result = a.get();
    const auto b_result = b.get();
    REQUIRE_FALSE(a_result);
    REQUIRE_FALSE(b_result);
    CHECK(((a_result.error().code == Errc::Auth && b_result.error().code == Errc::InvalidArgument) ||
           (b_result.error().code == Errc::Auth && a_result.error().code == Errc::InvalidArgument)));
    CHECK(f->accepted().size() == 2);
    int candidate_rows = 0;
    for (const auto& row : unwrap(f->rt->db().get_msgs(f->conv, true))) {
      if (row.text == "Shared DB candidate A." || row.text == "Shared DB candidate B.") ++candidate_rows;
    }
    CHECK(candidate_rows == 1);
    CHECK(f->transport->requests().empty());
  }

  TEST_CASE("two Runtime connections to one file accept exactly one distinct successor") {
    auto f = std::make_shared<DurableFixture>();
    REQUIRE_FALSE(f->send("Accept cross-connection predecessor.", f->spec));
    RuntimeOptions second_options;
    second_options.data_dir = f->dir.path().string();
    second_options.start_workers = false;
    auto second_transport = std::make_shared<net::ScriptedTransport>();
    second_options.http = second_transport;
    auto second = std::shared_ptr<Runtime>(unwrap(Runtime::open(second_options)).release());
    const Json a_spec = f->successor("cross-connection-successor-a");
    const Json b_spec = f->successor("cross-connection-successor-b");
    auto a_options = f->options(a_spec);
    auto b_options = f->options(b_spec);
    a_options.knowledge_context = context::ContextRequest{};
    b_options.knowledge_context = context::ContextRequest{};
    auto tentative_read_barrier = std::make_shared<std::barrier<>>(2);
    f->rt->chat().set_knowledge_context_builder(
        [tentative_read_barrier](const context::ContextRequest&) -> Result<Json> {
          tentative_read_barrier->arrive_and_wait();
          return Json{{"prompt", "Cross-connection context A."}, {"context_set", Json::object()}};
        });
    second->chat().set_knowledge_context_builder(
        [tentative_read_barrier](const context::ContextRequest&) -> Result<Json> {
          tentative_read_barrier->arrive_and_wait();
          return Json{{"prompt", "Cross-connection context B."}, {"context_set", Json::object()}};
        });
    auto release = std::make_shared<Gate>();
    auto a_ready = std::make_shared<Gate>();
    auto b_ready = std::make_shared<Gate>();
    BoundedWorker<Result<ChatResult>> a([f, release, a_ready, a_options] {
      a_ready->open();
      if (!release->wait()) return Result<ChatResult>(Error(Errc::Timeout, "connection A release gate"));
      return f->rt->chat().send("Cross-connection candidate A.", a_options);
    });
    BoundedWorker<Result<ChatResult>> b([second, release, b_ready, b_options] {
      b_ready->open();
      if (!release->wait()) return Result<ChatResult>(Error(Errc::Timeout, "connection B release gate"));
      return second->chat().send("Cross-connection candidate B.", b_options);
    });
    const bool ready = a_ready->wait() && b_ready->wait();
    release->open();
    REQUIRE(ready);
    REQUIRE(a.ready());
    REQUIRE(b.ready());
    const auto a_result = a.get();
    const auto b_result = b.get();
    REQUIRE_FALSE(a_result);
    REQUIRE_FALSE(b_result);
    CHECK(((a_result.error().code == Errc::Auth && b_result.error().code == Errc::InvalidArgument) ||
           (b_result.error().code == Errc::Auth && a_result.error().code == Errc::InvalidArgument)));
    CHECK(f->accepted().size() == 2);
    int candidate_rows = 0;
    for (const auto& row : unwrap(f->rt->db().get_msgs(f->conv, true))) {
      if (row.text == "Cross-connection candidate A." || row.text == "Cross-connection candidate B.") ++candidate_rows;
    }
    CHECK(candidate_rows == 1);
    CHECK(f->transport->requests().empty());
    CHECK(second_transport->requests().empty());
  }

  TEST_CASE("post-capture native-history mutation rejects and rolls back the whole acceptance") {
    DurableFixture f(true);
    NewMessage ordinary;
    ordinary.conv_id = f.conv;
    ordinary.role = "user";
    ordinary.text = "ORDINARY_HISTORY_BEFORE_CAPTURE: keep this non-task row stable.";
    const auto ordinary_id = unwrap(f.rt->db().create_msg(ordinary));
    // ensure_active_task_baseline() runs after the outbound array and its
    // native-history projection have been captured. This trigger gives a
    // deterministic mutation in precisely that gap, without sleeps or a
    // production-only hook. The enclosing acceptance transaction must then
    // fail its final comparison and roll back the trigger write and marker.
    LOOM_REQUIRE_OK(f.rt->db().conn().exec(
        "CREATE TEMP TRIGGER mutate_history_after_capture "
        "AFTER INSERT ON main.loom_events "
        "WHEN NEW.type = 'chat.active_task.acceptance_baseline.v1' "
        "BEGIN UPDATE messages SET text = 'ORDINARY_HISTORY_MUTATED_AFTER_CAPTURE' "
        "WHERE id = '" + ordinary_id + "'; END"));
    const auto before_rows = f.row_count();
    int callbacks = 0;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { ++callbacks; };
    const auto rejected = f.rt->chat().send("Reject a stale compiled history.",
                                            f.options(f.spec), cb);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.row_count() == before_rows);
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());
    CHECK(callbacks == 0);
    CHECK(f.transport->requests().empty());
    const auto ordinary_after = unwrap(f.rt->db().get_msg(ordinary_id));
    REQUIRE(ordinary_after);
    CHECK(ordinary_after->text == ordinary.text);
  }

  TEST_CASE("event-storage failure rolls back user row and baseline before callbacks or provider") {
    DurableFixture f(true);
    LOOM_REQUIRE_OK(f.rt->db().conn().exec(
        "CREATE TEMP TRIGGER reject_active_task_event "
        "BEFORE INSERT ON main.loom_events "
        "WHEN NEW.type = 'chat.active_task.accepted.v1' "
        "BEGIN SELECT RAISE(ABORT, 'synthetic acceptance event failure'); END"));
    const auto before_rows = f.row_count();
    int callbacks = 0;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { ++callbacks; };
    const auto failed = f.rt->chat().send("Atomic event failure.", f.options(f.spec), cb);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == Errc::Conflict);
    CHECK(f.row_count() == before_rows);
    CHECK(f.accepted().empty());
    CHECK(f.baselines().empty());
    CHECK(callbacks == 0);
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().get_msg(f.source.id))->to_json() == f.source.to_json());
  }

  TEST_CASE("active-task send inside an enclosing transaction has no early effects") {
    DurableFixture f(true);
    auto db_lock = f.rt->db().lock();
    sql::Txn outer(f.rt->db().conn());
    LOOM_REQUIRE_OK(outer.begin_status());
    const auto before_rows = f.row_count();
    const auto before_events = f.accepted().size();
    int callback_calls = 0;
    ChatCallbacks callbacks;
    callbacks.on_start = [&](std::string_view, std::string_view) { ++callback_calls; };
    const auto rejected = f.rt->chat().send("Reject nested transaction acceptance.",
                                            f.options(f.spec), callbacks);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(f.row_count() == before_rows);
    CHECK(f.accepted().size() == before_events);
    CHECK(callback_calls == 0);
    CHECK(f.transport->requests().empty());
    outer.rollback();
  }
}
