#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
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

// Independent lifecycle counterexamples. These use only native messages and a
// scripted local transport; no extraction fixture or model output is involved.
struct ExceptionalFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  std::string source_id;
  std::string source_text = "Prepare the cancellation audit with exact provenance.";
  Json spec;
  Json bindings;

  ExceptionalFixture() {
    RuntimeOptions setup;
    setup.data_dir = dir.path().string();
    setup.start_workers = false;
    setup.http = transport;
    rt = unwrap(Runtime::open(setup));
    rt->config().set("base_url", "https://exceptional.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "exceptional-test-key");
    conv = unwrap(rt->db().create_conv("Synthetic exceptional paths")).id;

    NewMessage source;
    source.conv_id = conv;
    source.role = "user";
    source.text = source_text;
    source_id = unwrap(rt->db().create_msg(source));

    bindings = Json{{"source.1", {{"message_id", source_id}, {"text_sha256", Sha256::hex(source_text)}}}};
    spec = Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", "exceptional.product.1"}}},
      {"goal_id", "exceptional.goal"},
      {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "exceptional.task"}}},
      {"version", 1},
      {"previous_product_ref", nullptr},
      {"known_at", "2026-10-01T01:00:00Z"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "synthetic.exceptional"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"source.1"})},
      {"source_refs", Json::array({Json{
        {"event_id", "source.1"},
        {"locator", {{"source", "synthetic.native"}}},
        {"known_at", "2026-10-01T00:59:00Z"},
        {"quote", source_text}}})},
      {"statements", Json::array({Json{
        {"id", "goal"},
        {"kind", "goal"},
        {"status", "active"},
        {"text", source_text},
        {"source_event_ids", Json::array({"source.1"})},
        {"claim_ids", Json::array()},
        {"conditions", Json::array()},
        {"supersedes", Json::array()}}})},
      {"compiled_instruction", {
        {"text", "untrusted caller rendering"},
        {"source_map", Json::array({Json{
          {"span", {{"byte_start", 0}, {"byte_len", 10}}},
          {"statement_ids", Json::array({"goal"})}}})}}}
    };
  }

  ChatOptions options(bool stream) const {
    return unwrap(ChatOptions::from_json(Json{
      {"conv_id", conv},
      {"stream", stream},
      {"include_memory", false},
      {"include_graph_memory", false},
      {"trace_context", false},
      {"active_task_spec", spec},
      {"active_task_bindings", bindings}}));
  }

  void buffered_reply(std::string text = "Synthetic replay response.") {
    transport->expect("POST", "https://exceptional.test/chat/completions",
      net::ScriptedTransport::Reply::json(200, Json{
        {"choices", Json::array({Json{{"message", {{"content", std::move(text)}}}}})}}));
  }

  void stream_reply(std::vector<std::string> events) {
    transport->expect("POST", "https://exceptional.test/chat/completions",
                      net::ScriptedTransport::Reply::sse(events));
  }

  void single_chunk_stream_reply(const std::vector<std::string>& events) {
    auto reply = net::ScriptedTransport::Reply::sse(events);
    std::string coalesced;
    for (const auto& chunk : reply.chunks) coalesced += chunk;
    reply.chunks = {std::move(coalesced)};
    transport->expect("POST", "https://exceptional.test/chat/completions", std::move(reply));
  }

  void unterminated_stream_reply(std::string event) {
    net::ScriptedTransport::Reply reply;
    reply.status = 200;
    reply.headers = {{"Content-Type", "text/event-stream"}};
    reply.chunks = {"data: " + std::move(event)};
    transport->expect("POST", "https://exceptional.test/chat/completions", std::move(reply));
  }

  Message message(std::string_view id) const {
    auto found = unwrap(rt->db().get_msg(id));
    REQUIRE(found);
    return *found;
  }

};

}  // namespace

TEST_SUITE("chat_active_task_exceptional") {
  TEST_CASE("pre-cancelled acceptance remains durable and persists a cancelled assistant row") {
    ExceptionalFixture f;
    f.buffered_reply("This response must not be consumed.");
    CancelToken cancel;
    cancel.cancel();

    auto cancelled = unwrap(f.rt->chat().send("Accept, then cancel.", f.options(false), {}, &cancel));
    CHECK(cancelled.cancelled);
    CHECK(cancelled.text.empty());
    CHECK(f.transport->requests().size() == 1);
    CHECK(f.message(cancelled.user_message_id).metadata["active_task"]["supplied_spec"] == f.spec);
    const auto cancelled_assistant = f.message(cancelled.assistant_message_id);
    CHECK(cancelled_assistant.text.empty());
    CHECK(cancelled_assistant.metadata["cancelled"] == true);

    f.buffered_reply();
    auto replay = unwrap(f.rt->chat().send("Replay the accepted task.", f.options(false)));
    CHECK_FALSE(replay.cancelled);
    CHECK(f.message(replay.user_message_id).metadata["active_task"]["supplied_spec"] == f.spec);
  }

  TEST_CASE("mid-stream cancellation retains partial output and accepted task authority") {
    ExceptionalFixture f;
    // Both deltas deliberately occupy one raw transport chunk. Cancellation
    // from the first callback must still suppress the already-buffered second
    // event, rather than waiting for the next network read.
    f.single_chunk_stream_reply({
      R"({"choices":[{"delta":{"content":"kept-part"}}]})",
      R"({"choices":[{"delta":{"content":"discarded-part"}}]})",
      "[DONE]",
    });
    CancelToken cancel;
    int chunks = 0;
    ChatCallbacks callbacks;
    callbacks.on_chunk = [&](std::string_view) {
      ++chunks;
      cancel.cancel();
    };

    auto result = unwrap(f.rt->chat().send("Cancel after one chunk.", f.options(true), callbacks, &cancel));
    CHECK(result.cancelled);
    CHECK(result.text == "kept-part");
    CHECK(chunks == 1);
    CHECK(f.message(result.user_message_id).metadata["active_task"]["supplied_spec"] == f.spec);
    const auto assistant = f.message(result.assistant_message_id);
    CHECK(assistant.text == "kept-part");
    CHECK(assistant.metadata["cancelled"] == true);

    f.buffered_reply();
    auto replay = unwrap(f.rt->chat().send("Continue after cancellation.", f.options(false)));
    CHECK_FALSE(replay.cancelled);
  }

  TEST_CASE("stream callback exceptions preserve acceptance and release in-flight authority") {
    for (const std::string callback : {"content", "reasoning"}) {
      CAPTURE(callback);
      ExceptionalFixture f;
      f.stream_reply(callback == "content"
        ? std::vector<std::string>{R"({"choices":[{"delta":{"content":"partial"}}]})", "[DONE]"}
        : std::vector<std::string>{R"({"choices":[{"delta":{"reasoning":"private"}}]})", "[DONE]"});

      std::string user_id;
      ChatCallbacks callbacks;
      callbacks.on_start = [&](std::string_view, std::string_view id) { user_id = id; };
      callbacks.on_chunk = [&](std::string_view) {
        if (callback == "content") throw std::runtime_error("synthetic content callback exception");
      };
      callbacks.on_reasoning = [&](std::string_view) {
        if (callback == "reasoning") throw std::runtime_error("synthetic reasoning callback exception");
      };

      bool threw = false;
      try {
        (void)f.rt->chat().send("Throw during stream callback.", f.options(true), callbacks);
      } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()) == "synthetic " + callback + " callback exception");
      }
      CHECK(threw);
      REQUIRE_FALSE(user_id.empty());
      CHECK(f.message(user_id).metadata["active_task"]["supplied_spec"] == f.spec);

      // Exact replay proves both that the committed acceptance survived the
      // exception and that the process-local in-flight guard was released.
      f.buffered_reply();
      auto replay = unwrap(f.rt->chat().send("Replay after callback exception.", f.options(false)));
      CHECK(f.message(replay.user_message_id).metadata["active_task"]["supplied_spec"] == f.spec);
      CHECK(f.message(replay.assistant_message_id).text == "Synthetic replay response.");
    }
  }

  TEST_CASE("cancellation from an unterminated final SSE event marks the result") {
    ExceptionalFixture f;
    f.unterminated_stream_reply(R"({"choices":[{"delta":{"content":"final-part"}}]})");
    CancelToken cancel;
    ChatCallbacks callbacks;
    callbacks.on_chunk = [&](std::string_view) { cancel.cancel(); };

    auto result = unwrap(f.rt->chat().send("Cancel from parser finish.", f.options(true), callbacks, &cancel));
    CHECK(result.cancelled);
    CHECK(result.text == "final-part");
    const auto assistant = f.message(result.assistant_message_id);
    CHECK(assistant.text == "final-part");
    CHECK(assistant.metadata["cancelled"] == true);
    CHECK(f.message(result.user_message_id).metadata["active_task"]["supplied_spec"] == f.spec);
  }
}
