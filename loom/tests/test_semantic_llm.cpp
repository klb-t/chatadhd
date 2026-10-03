// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>
#include <chrono>
#include <functional>
#include <future>
#include <thread>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  net::ScriptedTransport transport;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());

  Env() {
    cfg.set("base_url", "https://api.test");
    cfg.set("semantic_model", "cheap/model");
    secrets.set("api_key", "sk-test");
  }

  SemanticLLM llm() { return SemanticLLM(cfg, secrets, transport, *analyzer); }
};

struct CallbackTransport : net::HttpTransport {
  std::function<Result<net::HttpResponse>(const net::HttpRequest&)> reply;
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink*, const CancelToken*) override {
    return reply(request);
  }
  std::string name() const override { return "callback-test"; }
};

net::HttpResponse success_response() {
  Json content{{"entities", Json::array()}, {"topics", Json::array()}, {"relations", Json::array()}};
  Json body{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}};
  return net::HttpResponse{200, {}, json::dump(body)};
}
}  // namespace

TEST_SUITE("semantic_llm") {
  TEST_CASE("enabled() requires semantic_analysis, semantic_model and api_key") {
    Env env;
    auto llm = env.llm();
    CHECK(llm.enabled());

    Config cfg2(env.td.path() / "c2.json");
    Secrets sec2(env.td.path() / "s2.json");
    SemanticLLM disabled(cfg2, sec2, env.transport, *env.analyzer);
    CHECK(!disabled.enabled());  // no semantic_model, no api_key

    cfg2.set("semantic_model", "m");
    SemanticLLM stillNoKey(cfg2, sec2, env.transport, *env.analyzer);
    CHECK(!stillNoKey.enabled());

    sec2.set("api_key", "k");
    SemanticLLM ok(cfg2, sec2, env.transport, *env.analyzer);
    CHECK(ok.enabled());

    cfg2.set("semantic_analysis", false);
    SemanticLLM offByConfig(cfg2, sec2, env.transport, *env.analyzer);
    CHECK(!offByConfig.enabled());
  }

  TEST_CASE("parse_response_json strips ``` fences") {
    auto a = SemanticLLM::parse_response_json("```json\n{\"x\":1}\n```");
    REQUIRE(a.has_value());
    CHECK((*a)["x"] == 1);

    auto b = SemanticLLM::parse_response_json("  {\"y\":2}  ");
    REQUIRE(b.has_value());
    CHECK((*b)["y"] == 2);

    auto c = SemanticLLM::parse_response_json("not json");
    CHECK(!c.has_value());

    // starts with ``` but no newline: Python leaves it unchanged (still
    // invalid JSON), so parsing fails.
    auto d = SemanticLLM::parse_response_json("```nofence");
    CHECK(!d.has_value());
  }

  TEST_CASE("analyse(): short text or disabled -> regex-only unified dict") {
    Env env;
    auto llm = env.llm();
    Json a = llm.analyse("hi");  // < 20 code points
    CHECK(a["source"] == "regex");
    CHECK(a["entities"].is_array());
    CHECK(a["topics"].is_array());
    CHECK(a["relations"].is_array());
    CHECK(env.transport.requests().empty());  // no HTTP call for short text
  }

  TEST_CASE("analyse(): calls the LLM with the documented request shape") {
    Env env;
    Json analysis_content{{"entities", Json::array({Json{{"name", "Acme"}, {"kind", "org"}, {"relevance", 0.9}}})},
                          {"topics", Json::array({Json{{"label", "tech"}, {"confidence", 0.8}}})},
                          {"relations", Json::array()},
                          {"summary", "s"},
                          {"sentiment", "neutral"}};
    Json message{{"content", json::dump(analysis_content)}};
    Json choice{{"message", message}};
    Json llm_response{{"choices", Json::array({choice})}};
    env.transport.expect("POST", "https://api.test/chat/completions",
                         net::ScriptedTransport::Reply::json(200, llm_response));
    auto llm = env.llm();
    Json a = llm.analyse("This message is definitely long enough to trigger the LLM path.");
    CHECK(a["source"] == "llm");
    REQUIRE(a["entities"].is_array());
    bool found = false;
    for (const auto& e : a["entities"]) {
      if (e["name"] == "Acme") found = true;
    }
    CHECK(found);

    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].url == "https://api.test/chat/completions");
    CHECK(net::header_value(reqs[0].headers, "Authorization") == "Bearer sk-test");
    CHECK(net::header_value(reqs[0].headers, "X-Title") == "ChatADHD-Semantic");
    Json body = unwrap(json::parse(reqs[0].body));
    CHECK(body["model"] == "cheap/model");
    CHECK(body["temperature"] == 0.1);
    CHECK(body["max_tokens"] == 800);
    std::string content = body["messages"][0]["content"].get<std::string>();
    CHECK(content.find("This message is definitely long enough") != std::string::npos);
    CHECK(content.rfind(std::string(kAnalysisPrompt), 0) == 0);
  }

  TEST_CASE("analyse(): non-200 falls back to regex and counts a failure") {
    Env env;
    env.transport.set_fallback(net::ScriptedTransport::Reply::json(500, Json{{"error", "nope"}}));
    auto llm = env.llm();
    Json a = llm.analyse("This is long enough for the LLM path to be attempted here.");
    CHECK(a["source"] == "regex");
    CHECK(llm.consecutive_failures() == 1);
    CHECK(!llm.disabled_by_errors());
  }

  TEST_CASE("analyse(): disables itself after 5 consecutive failures") {
    Env env;
    env.transport.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "down"));
    auto llm = env.llm();
    for (int i = 0; i < 5; ++i) {
      llm.analyse("This text is long enough to attempt the LLM call every single time.");
    }
    CHECK(llm.consecutive_failures() == 5);
    CHECK(llm.disabled_by_errors());
    CHECK(!llm.enabled());

    // Once disabled, no further HTTP calls are attempted.
    std::size_t before = env.transport.requests().size();
    llm.analyse("Still long enough text but the LLM should not be called anymore.");
    CHECK(env.transport.requests().size() == before);

    llm.reset_failures();
    CHECK(!llm.disabled_by_errors());
    CHECK(llm.enabled());
  }

  TEST_CASE("relevant configuration changes recover without replay; other settings preserve the latch") {
    Env env;
    env.transport.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "down"));
    auto llm = env.llm();
    const std::string text = "This text is long enough to attempt semantic analysis.";
    auto trip = [&] {
      for (int i = 0; i < 5; ++i) llm.analyse(text);
      CHECK(!llm.enabled());
      CHECK(llm.consecutive_failures() == 5);
    };
    trip();
    env.cfg.set("theme", "amoled");
    env.cfg.set("semantic_model", "cheap/model");
    env.cfg.set("base_url", "https://api.test/");  // Same effective endpoint.
    env.secrets.set("api_key", "sk-test");
    CHECK(!llm.enabled());
    llm.analyse(text);
    CHECK(env.transport.requests().size() == 5);

    std::vector<std::function<void()>> changes{
        [&] { env.cfg.set("semantic_model", "corrected/model"); },
        [&] { env.cfg.set("base_url", "https://corrected.test"); },
        [&] { env.secrets.set("api_key", "corrected-test-key"); },
        [&] { env.cfg.set("semantic_analysis", false); }};
    for (const auto& change : changes) {
      auto before = env.transport.requests().size();
      change();
      CHECK(llm.enabled() == json::truthy(env.cfg.get("semantic_analysis")));
      CHECK(!llm.disabled_by_errors());
      CHECK(llm.consecutive_failures() == 0);
      CHECK(env.transport.requests().size() == before);
      if (llm.enabled()) trip();
    }
    env.cfg.set("semantic_analysis", true);
    env.transport.set_fallback(net::ScriptedTransport::Reply::text(200, success_response().body));
    CHECK(llm.analyse(text)["source"] == "llm");
    auto request = env.transport.requests().back();
    CHECK(request.url == "https://corrected.test/chat/completions");
    CHECK(unwrap(json::parse(request.body))["model"] == "corrected/model");
    CHECK(net::header_value(request.headers, "Authorization") == "Bearer corrected-test-key");
  }

  TEST_CASE("a current successful request resets the consecutive failure count") {
    Env env;
    auto llm = env.llm();
    const std::string text = "This text is long enough to attempt semantic analysis.";
    for (int i = 0; i < 4; ++i) llm.analyse(text);  // Default unscripted failure.
    CHECK(llm.consecutive_failures() == 4);
    env.transport.expect("POST", "", net::ScriptedTransport::Reply::text(200, success_response().body));
    CHECK(llm.analyse(text)["source"] == "llm");
    CHECK(llm.consecutive_failures() == 0);
    llm.analyse(text);
    CHECK(llm.consecutive_failures() == 1);
  }

  TEST_CASE("old in-flight results cannot change new configuration failure state") {
    bool old_success = false;
    SUBCASE("late failure after config fix without an enabled poll") {}
    SUBCASE("late success cannot clear failures of corrected settings") { old_success = true; }
    Env env;
    CallbackTransport transport;
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future().share();
    transport.reply = [&](const net::HttpRequest& request) -> Result<net::HttpResponse> {
      auto body = json::parse(request.body);
      if (body && (*body)["model"] == "cheap/model") {
        entered.set_value();
        released.wait();
        if (old_success) return success_response();
      }
      return make_error(Errc::Network, "test failure");
    };
    SemanticLLM llm(env.cfg, env.secrets, transport, *env.analyzer);
    const std::string text = "This text is long enough to attempt semantic analysis.";
    Json result;
    std::thread worker([&] { result = llm.analyse(text); });
    bool reached_http = started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    CHECK(reached_http);
    if (reached_http) {
      env.cfg.set("semantic_model", "corrected/model");
      if (old_success) {
        for (int i = 0; i < 4; ++i) llm.analyse(text);
      }
    }
    release.set_value();
    worker.join();
    CHECK(result["source"] == (old_success ? "llm" : "regex"));
    CHECK(llm.consecutive_failures() == (old_success ? 4 : 0));
    CHECK(llm.enabled());
  }

  TEST_CASE("manual reset and observed config round trip invalidate in-flight failures") {
    bool round_trip = false;
    SUBCASE("manual reset") {}
    SUBCASE("config changed and returned to its previous value") { round_trip = true; }
    Env env;
    CallbackTransport transport;
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future().share();
    transport.reply = [&](const net::HttpRequest&) -> Result<net::HttpResponse> {
      entered.set_value();
      released.wait();
      return make_error(Errc::Network, "test failure");
    };
    SemanticLLM llm(env.cfg, env.secrets, transport, *env.analyzer);
    std::thread worker([&] { llm.analyse("This text is long enough to attempt semantic analysis."); });
    bool reached_http = started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    CHECK(reached_http);
    if (reached_http) {
      if (round_trip) {
        env.cfg.set("semantic_model", "corrected/model");
        CHECK(llm.enabled());
        env.cfg.set("semantic_model", "cheap/model");
        CHECK(llm.enabled());
      } else {
        llm.reset_failures();
      }
    }
    release.set_value();
    worker.join();
    CHECK(llm.consecutive_failures() == 0);
    CHECK(llm.enabled());
  }

  TEST_CASE("merge(): regex entities missing from the LLM result are appended at 0.8x confidence") {
    Analysis regex;
    ExtractedEntity e;
    e.text = "Bob";
    e.entity_type = "person";
    e.confidence = 1.0;
    regex.entities.push_back(e);
    ExtractedEntity e2;
    e2.text = "acme";  // will collide case-insensitively with the LLM's "Acme"
    e2.entity_type = "org";
    e2.confidence = 1.0;
    regex.entities.push_back(e2);

    Json llm_result{{"entities", Json::array({Json{{"name", "Acme"}, {"kind", "org"}, {"relevance", 0.9}}})}};
    Json merged = SemanticLLM::merge(llm_result, regex);
    CHECK(merged["source"] == "llm");
    REQUIRE(merged["entities"].size() == 2);  // Acme (from LLM) + Bob (regex-only)
    bool bob_found = false;
    for (const auto& e3 : merged["entities"]) {
      if (e3["name"] == "Bob") {
        bob_found = true;
        CHECK(e3["relevance"] == doctest::Approx(0.8));
      }
    }
    CHECK(bob_found);
  }
}
