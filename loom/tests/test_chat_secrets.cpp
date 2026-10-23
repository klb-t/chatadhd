// R67: the setting secrets_in_chat has a reader. The check runs before the sentence is routed, stored or sent; the patterns, the actions and the words
// are data (runtime/chat_secrets.pack). Synthetic keys only. The gate is a pure function of (recipe, mode, sources, store values, confirmation), so the
// possibility space is covered by a generated sweep: every pattern's own prefix + generated tails hit, ordinary words never do, in every mode.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../src/chat/secret_filter.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace sec = loom::chat::secrets;

namespace {

Json recipe() { return unwrap(RuntimeProfile::builtin("chat_secrets")).values(); }
const std::string kKey = "sk-or-v1-" + std::string("a1B2c3D4e5F6g7H8i9J0k1L2");  // synthetic

Json model_reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fx {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fx() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
    rt->secrets().set("api_key", "sk-test-key-synthetic-0001");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    transport->set_fallback(net::ScriptedTransport::Reply::json(200, model_reply("ok")));
  }
};

std::size_t convs(Fx& f) { return unwrap(f.rt->db().list_convs(100)).size(); }

}  // namespace

TEST_SUITE("chat_secrets") {
  TEST_CASE("a hit names the rule and the place, never the value; the three actions follow the setting") {
    const Json r = recipe();
    const std::vector<sec::Source> src{{"text", "", "mój klucz to " + kKey + " proszę"}};
    const Json ask = unwrap(sec::gate(r, "ask", src, Json::object(), false));
    REQUIRE(ask.is_object());
    CHECK(ask["status"] == "needs_confirmation");
    CHECK(ask["text"]["pl"] == "W wiadomości jest sekret — wysłać mimo to?");
    CHECK(ask["hits"][0]["rule"] == "openrouter_key");
    CHECK(ask["hits"][0]["where"] == "text");
    CHECK(ask.dump().find(kKey) == std::string::npos);
    CHECK(ask.dump().find("a1B2c3D4") == std::string::npos);
    CHECK(unwrap(sec::gate(r, "ask", src, Json::object(), true)).is_null());      // confirmed hold goes on
    const Json never = unwrap(sec::gate(r, "never", src, Json::object(), true));  // a refusal is not overridden by a confirmation
    CHECK(never["status"] == "refused");
    CHECK(never.dump().find(kKey) == std::string::npos);
    CHECK(unwrap(sec::gate(r, "allow", src, Json::object(), false)).is_null());
    // a value of the setting the recipe does not know is read as ask (nothing leaves unseen)
    CHECK(unwrap(sec::gate(r, "perhaps", src, Json::object(), false))["status"] == "needs_confirmation");
  }

  TEST_CASE("ordinary text, short words and a short store value do not hit; a store value does, by key") {
    const Json r = recipe();
    CHECK(unwrap(sec::gate(r, "ask", {{"text", "", "Jak ustawić hasło do routera? Napisz krótko."}}, Json::object(), false)).is_null());
    const Json store{{"api_key", "sk-test-key-synthetic-0001"}, {"short", "abc"}, {"number", 5}};
    CHECK(unwrap(sec::gate(r, "ask", {{"text", "", "abc and 5"}}, store, false)).is_null());
    const Json hit = unwrap(sec::gate(r, "ask", {{"text", "", "to jest sk-test-key-synthetic-0001 koniec"}}, store, false));
    REQUIRE(hit.is_object());
    // the value also looks like a key (openai_key), so both rules name it; the store rule names the key it equals, never the value
    Json by_store = Json(nullptr);
    for (const auto& h : hit["hits"]) if (h["rule"] == "secrets_store") by_store = h;
    REQUIRE(by_store.is_object());
    CHECK(by_store["key"] == "api_key");
    CHECK(hit.dump().find("synthetic-0001") == std::string::npos);
  }

  TEST_CASE("an attached text file is checked and the hit names the file") {
    const Json hit = unwrap(sec::gate(recipe(), "ask", {{"text", "", "zobacz plik"}, {"file", "notatki.txt", "linia\npassword = hunter2hunter2\n"}}, Json::object(), false));
    REQUIRE(hit.is_object());
    CHECK(hit["hits"].size() == 1);
    CHECK(hit["hits"][0]["where"] == "file");
    CHECK(hit["hits"][0]["file"] == "notatki.txt");
    CHECK(hit["hits"][0]["rule"] == "password_assignment");
  }

  TEST_CASE("the recipe is data: an overlay pattern is read, a broken pattern is said, not skipped") {
    Json r = recipe();
    r["patterns"].push_back(Json{{"id", "my_badge"}, {"regex", "BADGE-[0-9]{6}"}, {"flags", ""}, {"label", Json{{"pl", "numer odznaki"}, {"en", "badge number"}}}});
    const Json hit = unwrap(sec::gate(r, "ask", {{"text", "", "BADGE-123456"}}, Json::object(), false));
    CHECK(hit["hits"][0]["rule"] == "my_badge");
    r["patterns"].push_back(Json{{"id", "broken"}, {"regex", "(["}, {"flags", ""}, {"label", Json::object()}});
    auto bad = sec::gate(r, "ask", {{"text", "", "x"}}, Json::object(), false);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("broken") != std::string::npos);
  }

  TEST_CASE("generated sweep: every prefix with generated tails hits in every mode that is not pass, ordinary words never do") {
    const Json r = recipe();
    const std::vector<std::pair<std::string, std::string>> shapes{
        {"sk-ant-", "A"}, {"sk-or-v1-", "A"}, {"sk-", "A"}, {"ghp_", "A"}, {"github_pat_", "A"}, {"AKIA", "A"}, {"xoxb-", "A"}, {"AIza", "A"}};
    const std::vector<std::pair<std::string, std::size_t>> lens{{"sk-ant-", 24}, {"sk-or-v1-", 24}, {"sk-", 24}, {"ghp_", 36}, {"github_pat_", 48}, {"AKIA", 16}, {"xoxb-", 16}, {"AIza", 35}};
    const std::string alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uint32_t seed = 12345;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (const auto& [prefix, len] : lens) {
      for (int round = 0; round < 25; ++round) {
        std::string tail;
        const bool upper_only = prefix == "AKIA";
        for (std::size_t i = 0; i < len; ++i) tail += upper_only ? alpha[26 + next() % 36] : alpha[next() % alpha.size()];
        const std::string text = "x " + prefix + tail + " y";
        for (const char* mode : {"ask", "never"}) {
          const Json h = unwrap(sec::gate(r, mode, {{"text", "", text}}, Json::object(), false));
          INFO(text);
          REQUIRE(h.is_object());
          CHECK(h.dump().find(tail) == std::string::npos);
        }
        CHECK(unwrap(sec::gate(r, "allow", {{"text", "", text}}, Json::object(), false)).is_null());
      }
    }
    for (int round = 0; round < 200; ++round) {
      std::string words;
      for (int w = 0; w < 6; ++w) {
        std::string word;
        for (std::size_t i = 0, n = 2 + next() % 9; i < n; ++i) word += alpha[next() % 26];
        words += word + " ";
      }
      INFO(words);
      CHECK(unwrap(sec::gate(r, "ask", {{"text", "", words}}, Json::object(), false)).is_null());
    }
  }

  TEST_CASE("end to end: a held message is neither stored nor sent; confirmed it goes with the bytes of an ordinary message") {
    Fx f;
    f.rt->config().set("secrets_in_chat", "ask");
    const std::string said = "Mój klucz: " + kKey + ", co o nim sądzisz?";
    auto held = unwrap(f.rt->chat().send(said, ChatOptions{}));
    REQUIRE(held.secret_pending);
    CHECK((*held.secret_pending)["status"] == "needs_confirmation");
    CHECK(f.transport->requests().empty());
    CHECK(convs(f) == 0);
    ChatOptions go;
    go.secret_confirmation = Json{{"approved", true}};
    auto sent = unwrap(f.rt->chat().send(said, go));
    CHECK_FALSE(sent.secret_pending);
    CHECK(f.transport->requests().size() == 1);
    CHECK(convs(f) == 1);
    // the same sentence, the setting allow: no question, same path
    Fx g;
    g.rt->config().set("secrets_in_chat", "allow");
    CHECK_FALSE(unwrap(g.rt->chat().send(said, ChatOptions{})).secret_pending);
    CHECK(g.transport->requests().size() == 1);
    // never: refused for good, even with a confirmation, and nothing is stored
    Fx h;
    h.rt->config().set("secrets_in_chat", "never");
    auto refused = unwrap(h.rt->chat().send(said, go));
    REQUIRE(refused.secret_pending);
    CHECK((*refused.secret_pending)["status"] == "refused");
    CHECK(h.transport->requests().empty());
    CHECK(convs(h) == 0);
  }

  TEST_CASE("the preset of the setting is ask, and a message without a secret is untouched") {
    Fx f;
    CHECK(f.rt->config().get("secrets_in_chat") == "ask");
    CHECK_FALSE(unwrap(f.rt->chat().send("Cześć, jak się masz?", ChatOptions{})).secret_pending);
    CHECK(f.transport->requests().size() == 1);
  }
}
