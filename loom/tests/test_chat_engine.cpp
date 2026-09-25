// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_memory.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  net::ScriptedTransport transport;

  Env() {
    secrets.set("api_key", "sk-test-key");
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
  }

  ChatEngine engine() { return ChatEngine(cfg, secrets, *db, bus, transport, *analyzer); }
};

Json openai_reply(std::string content, std::string reasoning = "") {
  Json message{{"role", "assistant"}, {"content", content}};
  if (!reasoning.empty()) message["reasoning"] = reasoning;
  return Json{{"choices", Json::array({Json{{"message", message}}})}, {"usage", Json{{"total_tokens", 42}}}};
}

}  // namespace

TEST_SUITE("chat_engine") {
  TEST_CASE("build_content: no attachments returns the plain string") {
    Env env;
    auto engine = env.engine();
    Json c = engine.build_content("hello", {});
    CHECK(c == Json("hello"));
  }

  TEST_CASE("build_content: image attachment becomes a data URL") {
    Env env;
    auto path = env.td.path() / "pic.png";
    { std::ofstream f(path, std::ios::binary); f << "PNGDATA"; }
    auto engine = env.engine();
    Json c = engine.build_content("look", {path.string()});
    REQUIRE(c.is_array());
    REQUIRE(c.size() == 2);
    CHECK(c[0]["type"] == "text");
    CHECK(c[0]["text"] == "look");
    CHECK(c[1]["type"] == "image_url");
    std::string url = c[1]["image_url"]["url"].get<std::string>();
    CHECK(url.rfind("data:image/png;base64,", 0) == 0);
  }

  TEST_CASE("build_content: jpg maps to image/jpeg; text file is inlined with FILE markers") {
    Env env;
    auto jpg = env.td.path() / "a.jpg";
    { std::ofstream f(jpg, std::ios::binary); f << "JPEGDATA"; }
    auto txt = env.td.path() / "notes.txt";
    { std::ofstream f(txt); f << "hello world"; }
    auto engine = env.engine();
    Json c = engine.build_content("x", {jpg.string(), txt.string()});
    REQUIRE(c.is_array());
    REQUIRE(c.size() == 3);
    CHECK(c[1]["image_url"]["url"].get<std::string>().rfind("data:image/jpeg;base64,", 0) == 0);
    std::string ft = c[2]["text"].get<std::string>();
    CHECK(ft.find("--- FILE: notes.txt ---") != std::string::npos);
    CHECK(ft.find("hello world") != std::string::npos);
    CHECK(ft.find("--- END FILE ---") != std::string::npos);
  }

  TEST_CASE("build_content: missing attachment is skipped, not fatal") {
    Env env;
    auto engine = env.engine();
    Json c = engine.build_content("x", {"/nonexistent/path.png"});
    REQUIRE(c.is_array());
    CHECK(c.size() == 1);  // just the text part
  }

  TEST_CASE("configure_reasoning: non-thinking models get no reasoning block") {
    Json payload = Json::object();
    ChatEngine::configure_reasoning(payload, "openai/gpt-4o-mini", std::nullopt);
    CHECK(!payload.contains("reasoning"));
  }

  TEST_CASE("configure_reasoning: legacy thinking model uses effort") {
    Json payload = Json::object();
    ChatEngine::configure_reasoning(payload, "deepseek/deepseek-r1", std::string("high"));
    REQUIRE(payload.contains("reasoning"));
    CHECK(payload["reasoning"]["enabled"] == true);
    CHECK(payload["reasoning"]["effort"] == "high");
  }

  TEST_CASE("configure_reasoning: new Claude model maps effort to max_tokens budget") {
    Json payload = Json::object();
    ChatEngine::configure_reasoning(payload, "anthropic/claude-sonnet-4-5", std::string("low"));
    CHECK(payload["reasoning"]["max_tokens"] == 5000);

    payload = Json::object();
    ChatEngine::configure_reasoning(payload, "anthropic/claude-sonnet-4-5", std::string("adaptive"));
    CHECK(!payload["reasoning"].contains("max_tokens"));

    payload = Json::object();
    ChatEngine::configure_reasoning(payload, "anthropic/claude-opus-4-5", std::string("max"));
    CHECK(payload["verbosity"] == "max");
    CHECK(!payload["reasoning"].contains("max_tokens"));
  }

  TEST_CASE("send: non-streaming success persists both messages, auto-titles, records usage") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("Hi there!", "thinking...")));
    auto engine = env.engine();
    ChatOptions opts;
    opts.stream = false;
    auto r = engine.send("Hello, how are you?", opts);
    REQUIRE(r);
    CHECK(r->text == "Hi there!");
    REQUIRE(r->reasoning.has_value());
    CHECK(*r->reasoning == "thinking...");
    CHECK(r->usage["total_tokens"] == 42);
    CHECK(!r->cancelled);
    REQUIRE(r->new_title.has_value());
    CHECK(*r->new_title == "Hello, how are you?");  // < 30 chars, no ellipsis

    auto msgs = unwrap(env.db->get_msgs(r->conv_id));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "assistant");
    CHECK(msgs[1].metadata["reasoning"] == "thinking...");

    // Request shape: Authorization header, model, no duplicate current message.
    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(net::header_value(reqs[0].headers, "Authorization") == "Bearer sk-test-key");
    Json body = unwrap(json::parse(reqs[0].body));
    CHECK(body["model"] == "test/model");
    CHECK(body["stream"] == false);
    REQUIRE(body["messages"].is_array());
    CHECK(body["messages"].back()["role"] == "user");
    CHECK(body["messages"].back()["content"] == "Hello, how are you?");
  }

  TEST_CASE("send: second turn excludes the just-persisted message from history (no duplication)") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("first reply")));
    auto engine = env.engine();
    auto r1 = unwrap(engine.send("first message"));

    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("second reply")));
    ChatOptions opts2;
    opts2.conv_id = r1.conv_id;
    auto r2 = unwrap(engine.send("second message", opts2));

    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 2);
    Json body2 = unwrap(json::parse(reqs[1].body));
    int occurrences = 0;
    for (const auto& m : body2["messages"]) {
      if (m["role"] == "user" && m["content"] == "second message") ++occurrences;
    }
    CHECK(occurrences == 1);  // history (excluded) + current turn must not double it
    CHECK(r2.conv_id == r1.conv_id);
  }

  TEST_CASE("send: streaming delivers deltas and reasoning via callbacks") {
    Env env;
    env.transport.expect(
        "POST", "https://api.test/chat/completions",
        net::ScriptedTransport::Reply::sse({
            R"({"choices":[{"delta":{"reasoning":"th-"}}]})",
            R"({"choices":[{"delta":{"content":"Hel"}}]})",
            R"({"choices":[{"delta":{"content":"lo"}}]})",
            R"({"choices":[{"delta":{}}],"usage":{"total_tokens":7}})",
            "[DONE]",
        }));
    auto engine = env.engine();
    std::string chunks, reasoning;
    bool started = false;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view) { started = true; };
    cb.on_chunk = [&](std::string_view t) { chunks += t; };
    cb.on_reasoning = [&](std::string_view t) { reasoning += t; };
    auto r = unwrap(engine.send("stream please", {}, cb));
    CHECK(started);
    CHECK(chunks == "Hello");
    CHECK(reasoning == "th-");
    CHECK(r.text == "Hello");
    CHECK(r.usage["total_tokens"] == 7);
  }

  TEST_CASE("send: no API key still persists the user message, then fails with Auth") {
    Env env;
    env.secrets.erase("api_key");
    auto engine = env.engine();
    auto r = engine.send("hi");
    REQUIRE(!r);
    CHECK(r.error().code == Errc::Auth);
    auto convs = unwrap(env.db->list_convs());
    REQUIRE(convs.size() == 1);
    auto msgs = unwrap(env.db->get_msgs(convs[0].id));
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].role == "user");
  }

  TEST_CASE("send: non-200 status maps to Errc::Http with a body snippet") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    auto engine = env.engine();
    auto r = engine.send("hi");
    REQUIRE(!r);
    CHECK(r.error().code == Errc::Http);
    CHECK(r.error().message.find("500") != std::string::npos);
  }

  TEST_CASE("send: cancellation persists partial (empty) text with metadata.cancelled") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("would have replied")));
    auto engine = env.engine();
    CancelToken tok;
    tok.cancel();
    auto r = unwrap(engine.send("hi", {}, {}, &tok));
    CHECK(r.cancelled);
    CHECK(r.text.empty());
    auto msgs = unwrap(env.db->get_msgs(r.conv_id));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[1].metadata["cancelled"] == true);
  }

  TEST_CASE("send: web_search adds the web plugin; deep_research adds max_results + options") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    auto engine = env.engine();
    ChatOptions opts;
    opts.deep_research = true;
    unwrap(engine.send("search this", opts));
    Json body = unwrap(json::parse(env.transport.requests().back().body));
    REQUIRE(body["plugins"].is_array());
    CHECK(body["plugins"][0]["id"] == "web");
    CHECK(body["plugins"][0]["max_results"] == 10);
    CHECK(body["web_search_options"]["search_context_size"] == "high");
  }

  TEST_CASE("send: context_depth 0 disables graph memory even when a selector is present") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    Config cfg2(env.td.path() / "cfg2.json");
    GraphMemorySelector gm(*env.db, env.cfg, *env.analyzer);
    ChatEngine engine(env.cfg, env.secrets, *env.db, env.bus, env.transport, *env.analyzer, nullptr, &gm);
    ChatOptions opts;
    opts.context_depth = 0;
    auto r = unwrap(engine.send("no graph please", opts));
    CHECK(!r.text.empty());
  }
}
