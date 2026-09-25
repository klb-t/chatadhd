// OWNER: wave 2 net/chat/worker.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/providers.h"
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

  Env() {
    cfg.set("base_url", "https://api.test");
    secrets.set("api_key", "sk-test");
  }

  std::filesystem::path models_path() const { return td.path() / "models.json"; }
};

void write_file(const std::filesystem::path& p, std::string_view content) {
  std::ofstream f(p, std::ios::binary);
  f << content;
}
}  // namespace

TEST_SUITE("providers.models") {
  TEST_CASE("no file -> empty list, no crash") {
    Env env;
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    CHECK(reg.size() == 0);
    CHECK(reg.all().empty());
  }

  TEST_CASE("canonical list-of-dicts format loads as-is") {
    Env env;
    write_file(env.models_path(), R"([{"id":"a/b","name":"B","context_length":1000}])");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    REQUIRE(reg.size() == 1);
    CHECK(reg.name("a/b") == "B");
    CHECK(reg.get_context_length("a/b") == 1000);
  }

  TEST_CASE("v0.06.x plain id-string list is upgraded") {
    Env env;
    write_file(env.models_path(), R"(["openai/gpt-4"])");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    REQUIRE(reg.size() == 1);
    CHECK(reg.all()[0]["id"] == "openai/gpt-4");
    CHECK(reg.all()[0]["name"] == "gpt-4");
    CHECK(reg.all()[0]["context_length"] == 0);
    // Re-saved canonically.
    auto text = unwrap(fsutil::read_file(env.models_path()));
    auto reparsed = unwrap(json::parse(text));
    CHECK(reparsed[0]["name"] == "gpt-4");
  }

  TEST_CASE("raw API cache dict format ({\"data\":[...]}) is normalised") {
    Env env;
    write_file(env.models_path(), R"({"data":[{"id":"x/y","name":"Y"}]})");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    REQUIRE(reg.size() == 1);
    CHECK(reg.all()[0]["id"] == "x/y");
  }

  TEST_CASE("corrupt JSON removes the file and starts empty") {
    Env env;
    write_file(env.models_path(), "not json {{{");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    CHECK(reg.size() == 0);
    CHECK(!std::filesystem::exists(env.models_path()));
  }

  TEST_CASE("name() falls back to the segment after the last slash") {
    Env env;
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    CHECK(reg.name("anthropic/claude-x") == "claude-x");
    CHECK(reg.name("noSlash") == "noSlash");
  }

  TEST_CASE("grouped() buckets by provider in first-appearance order") {
    Env env;
    write_file(env.models_path(),
              R"([{"id":"b/1"},{"id":"a/1"},{"id":"b/2"},{"id":"noSlash"}])");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    Json g = reg.grouped();
    auto keys = g.items();
    std::vector<std::string> order;
    for (auto& [k, v] : g.items()) order.push_back(k);
    REQUIRE(order.size() == 3);
    CHECK(order[0] == "b");
    CHECK(order[1] == "a");
    CHECK(order[2] == "other");
    CHECK(g["b"].size() == 2);
  }

  TEST_CASE("estimate_cost handles numeric and string prices") {
    Env env;
    write_file(env.models_path(),
              R"([{"id":"num","pricing":{"prompt":1,"completion":2}},
                   {"id":"str","pricing":{"prompt":"0.000001","completion":"0.000002"}}])");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    CHECK(reg.estimate_cost("num", 1000000, 1000000) == doctest::Approx(3.0));
    CHECK(reg.estimate_cost("str", 1000000, 1000000) == doctest::Approx(0.000003));
    CHECK(reg.estimate_cost("unknown", 100, 100) == 0.0);
  }

  TEST_CASE("update_from_api fetches, normalises and persists") {
    Env env;
    Json api_response{{"data", Json::array({Json{{"id", "z/1"}, {"name", "Z1"}, {"context_length", 8000},
                                                  {"pricing", Json{{"prompt", "0.1"}}}, {"description", "desc"}}})}};
    env.transport.expect("GET", "https://api.test/models", net::ScriptedTransport::Reply::json(200, api_response));
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    REQUIRE(reg.update_from_api());
    REQUIRE(reg.size() == 1);
    CHECK(reg.get_description("z/1") == "desc");
    auto reqs = env.transport.requests();
    CHECK(net::header_value(reqs[0].headers, "Authorization") == "Bearer sk-test");
    CHECK(std::filesystem::exists(env.models_path()));
  }

  TEST_CASE("update_from_api without a key is Unavailable") {
    Env env;
    env.secrets.erase("api_key");
    ModelRegistry reg(env.models_path(), env.cfg, env.secrets, env.transport);
    auto st = reg.update_from_api();
    REQUIRE(!st);
    CHECK(st.error().code == Errc::Unavailable);
  }
}

TEST_SUITE("providers.registry") {
  TEST_CASE("constraints_satisfied: scalars, arrays, max_/min_, booleans") {
    Json offered{{"formats", Json::array({"wav", "mp3"})}, {"max_file_mb", 25}, {"streaming", true}};
    CHECK(constraints_satisfied(offered, Json::object()));
    CHECK(constraints_satisfied(offered, Json{{"formats", "wav"}}));
    CHECK(!constraints_satisfied(offered, Json{{"formats", "flac"}}));
    CHECK(constraints_satisfied(offered, Json{{"formats", Json::array({"wav", "mp3"})}}));
    CHECK(!constraints_satisfied(offered, Json{{"formats", Json::array({"wav", "flac"})}}));
    CHECK(constraints_satisfied(offered, Json{{"max_file_mb", 10}}));
    CHECK(!constraints_satisfied(offered, Json{{"max_file_mb", 30}}));
    CHECK(constraints_satisfied(offered, Json{{"streaming", true}}));
    CHECK(constraints_satisfied(offered, Json{{"streaming", false}}));
    CHECK(!constraints_satisfied(offered, Json{{"missing_key", 1}}));
  }

  TEST_CASE("load_builtin seeds openrouter, anthropic and friends") {
    Env env;
    ProviderRegistry reg(env.cfg, env.secrets);
    REQUIRE(reg.load_builtin());
    auto all = reg.all();
    CHECK(all.size() == 6);
    auto orm = reg.get("openrouter");
    REQUIRE(orm.has_value());
    CHECK(orm->base_url == "https://api.test");  // picked up from config
    CHECK(reg.can("llm", "chat.completions"));   // api_key is set
    CHECK(reg.can("llm", "chat.stream"));
    CHECK(!reg.can("llm", "nonexistent-capability"));

    // Anthropic batch key not set -> not available even though the
    // manifest exists.
    CHECK(!reg.can("batch", "messages.batches"));
    env.secrets.set("anthropic_batch_key", "sk-ant");
    CHECK(reg.can("batch", "messages.batches"));

    Json status = reg.status();
    REQUIRE(status["providers"].is_array());
    CHECK(status["providers"].size() == 6);
    for (const auto& p : status["providers"]) {
      CHECK(!p.contains("auth_secret"));  // never leaks the secret key name via status()
    }
  }

  TEST_CASE("load() upserts by id and validates shape") {
    Env env;
    ProviderRegistry reg(env.cfg, env.secrets);
    Json manifest = Json::array({Json{{"id", "custom"},
                                      {"auth_secret", ""},
                                      {"capabilities", Json::array({Json{{"resource", "llm"}, {"name", "chat.completions"}}})}}});
    REQUIRE(reg.load(manifest));
    CHECK(reg.can("llm", "chat.completions"));  // no auth_secret required -> always available

    Json bad = Json::array({Json{{"id", "x"}, {"capabilities", Json::array({Json{{"resource", "llm"}}})}}});
    CHECK(!reg.load(bad));  // capability missing "name"
  }
}
