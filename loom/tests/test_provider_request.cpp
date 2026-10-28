// RI-2: the request the kernel puts on the wire for the provider, as the request inspector shows it (loom.provider_request/1,
// chat_engine.h provider_request_preview / last_provider_request). What the scripted transport received is the reference: the record
// equals it byte for byte where nothing is masked, a secret (the key of the route, a value of the secrets store, a pattern of the
// chat_secrets recipe) never appears in it, and the preview builds the same request the send then makes, sending nothing.
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr const char* kKey = "sk-or-v1-RI2routekey0123456789abcdef";
constexpr const char* kStoreValue = "ri2-store-value-QWERTY-12345";

Json openai_reply(std::string content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  net::ScriptedTransport transport;
  MemoryEngine mem{td.path() / "memory.json"};
  GraphMemorySelector gm{*db, cfg, *analyzer};
  std::string conv_id;
  Fixture() {
    secrets.set("api_key", kKey);
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
    cfg.set("system_prompt", "SYS-RI2 be brief");
    cfg.set("auto_title", false);
    unwrap(mem.add_node("MEM-RI2 the person likes tea"));
    conv_id = unwrap(db->create_conv("RI2")).id;
    for (int i = 1; i <= 2; ++i) {
      NewMessage h; h.conv_id = conv_id; h.text = "HIST-RI2 turn " + std::to_string(i); h.role = i % 2 ? "user" : "assistant";
      unwrap(db->create_msg(h));
    }
  }
  ChatEngine engine() { return ChatEngine(cfg, secrets, *db, bus, transport, *analyzer, &mem, &gm); }
  ChatOptions opts() const {
    ChatOptions o;
    o.conv_id = conv_id;
    o.stream = false;
    return o;
  }
};

}  // namespace

TEST_SUITE("provider_request") {
  TEST_CASE("RI-2: the record of the last request equals what the transport received, the key never in it") {
    Fixture f;
    auto e = f.engine();
    CHECK(e.last_provider_request(f.conv_id).value("kind", "") == "none");
    f.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(e.send("MSG-RI2 what is the weather", f.opts()));
    REQUIRE(f.transport.requests().size() == 1);
    const net::HttpRequest wire = f.transport.requests().back();
    const Json view = e.last_provider_request(f.conv_id);
    CHECK(view.value("schema", "") == "loom.provider_request/1");
    CHECK(view.value("kind", "") == "sent");
    CHECK(view.value("dispatched", false));
    CHECK(view.value("model", "") == "test/model");
    CHECK(view.value("url", "") == wire.url);
    // Nothing secret in the body: the body is the wire bytes parsed back, byte for byte.
    CHECK(json::dump(view.at("body")) == wire.body);
    CHECK(view.value("body_sha256", "") == Sha256::hex(wire.body));
    CHECK(view.value("wire_bytes", 0) == wire.body.size());
    // The key went out in a header; the record says a header was masked and never holds the value.
    bool key_on_wire = false;
    for (const auto& [name, value] : wire.headers) key_on_wire = key_on_wire || value.find(kKey) != std::string::npos;
    CHECK(key_on_wire);
    const std::string dumped = view.dump();
    CHECK(dumped.find(kKey) == std::string::npos);
    CHECK(dumped.find("[redacted:header]") != std::string::npos);
    // The parts in order: the instruction first, the message last.
    const Json parts = view.at("parts");
    REQUIRE(parts.size() >= 3);
    CHECK(parts.front().value("part", "") == "system_prompt");
    CHECK(parts.back().value("part", "") == "current_message");
    bool memory = false, history = false;
    for (const auto& p : parts) {
      memory = memory || p.value("part", "") == "memory";
      history = history || p.value("part", "") == "history_user";
    }
    CHECK(memory);
    CHECK(history);
    CHECK(view.at("parameters").contains("model"));
  }

  TEST_CASE("RI-2: a secret typed into a setting or the memory goes out but the record masks it by rule and place") {
    Fixture f;
    f.secrets.set("github_token", kStoreValue);
    f.cfg.set("system_prompt", std::string("SYS-RI2 token is ") + kStoreValue);
    unwrap(f.mem.add_node("password: hunter22xyz"));
    auto e = f.engine();
    f.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(e.send("MSG-RI2 hello", f.opts()));
    const std::string wire = f.transport.requests().back().body;
    REQUIRE(wire.find(kStoreValue) != std::string::npos);  // the person's own setting goes out as it is (the inspector only shows)
    const Json view = e.last_provider_request(f.conv_id);
    const std::string dumped = view.dump();
    CHECK(dumped.find(kStoreValue) == std::string::npos);
    CHECK(dumped.find("hunter22xyz") == std::string::npos);
    CHECK(dumped.find(kKey) == std::string::npos);
    CHECK(view.value("redacted", false));
    bool store = false, pattern = false;
    for (const auto& r : view.at("redactions")) {
      CHECK(r.dump().find(kStoreValue) == std::string::npos);
      if (r.value("rule", "") == "secrets_store" && r.value("key", "") == "github_token") store = r.value("where", "").rfind("body/messages/0", 0) == 0;
      if (r.value("rule", "") == "password_assignment") pattern = true;
    }
    CHECK(store);
    CHECK(pattern);
    // Minus the masks the record is the wire: put the store value back where its mark stands and compare the instruction.
    const Json sent = unwrap(json::parse(wire));
    std::string sys = view.at("body").at("messages").at(0).at("content").get<std::string>();
    const std::string mark = "[redacted:secrets_store:github_token]";
    REQUIRE(sys.find(mark) != std::string::npos);
    sys.replace(sys.find(mark), mark.size(), kStoreValue);
    CHECK(sys == sent.at("messages").at(0).at("content").get<std::string>());
    CHECK(view.at("body").at("messages").size() == sent.at("messages").size());
  }

  TEST_CASE("RI-2b: request_inspector_show_setting_values shows what only looks like a secret, the store and the route credential stay masked") {
    Fixture f;
    f.secrets.set("github_token", kStoreValue);
    f.cfg.set("system_prompt", std::string("SYS-RI2 token is ") + kStoreValue + " and password: hunter22xyz");
    f.cfg.set("request_inspector_show_setting_values", true);
    auto e = f.engine();
    f.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(e.send("MSG-RI2 hello", f.opts()));
    const std::string dumped = e.last_provider_request(f.conv_id).dump();
    CHECK(dumped.find("hunter22xyz") != std::string::npos);        // typed in settings, shown on request
    CHECK(dumped.find(kStoreValue) == std::string::npos);          // the secrets store is masked whatever the setting says
    CHECK(dumped.find("[redacted:secrets_store:github_token]") != std::string::npos);
    CHECK(dumped.find(kKey) == std::string::npos);                 // the route credential too
    CHECK(dumped.find("[redacted:header]") != std::string::npos);
    // The preview obeys the same setting.
    const std::string pv = unwrap(e.provider_request_preview("MSG-RI2 next", f.opts())).dump();
    CHECK(pv.find(kStoreValue) == std::string::npos);
    CHECK(pv.find(kKey) == std::string::npos);
    f.cfg.set("request_inspector_show_setting_values", false);
    f.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(e.send("MSG-RI2 again", f.opts()));
    CHECK(e.last_provider_request(f.conv_id).dump().find("hunter22xyz") == std::string::npos);
  }

  TEST_CASE("RI-2: the preview builds the request the send then makes, stores and sends nothing, needs no key") {
    Fixture f;
    auto e = f.engine();
    const size_t msgs_before = unwrap(f.db->get_msgs(f.conv_id)).size();
    const Json preview = unwrap(e.provider_request_preview("MSG-RI2 next one", f.opts()));
    CHECK(preview.value("kind", "") == "preview");
    CHECK_FALSE(preview.value("dispatched", true));
    CHECK(f.transport.requests().empty());
    CHECK(unwrap(f.db->get_msgs(f.conv_id)).size() == msgs_before);
    CHECK(preview.dump().find(kKey) == std::string::npos);
    CHECK(e.last_provider_request(f.conv_id).value("kind", "") == "none");
    f.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(e.send("MSG-RI2 next one", f.opts()));
    CHECK(json::dump(preview.at("body")) == f.transport.requests().back().body);
    // With no key stored the preview still builds (a look needs no credential).
    f.secrets.erase("api_key");
    auto e2 = f.engine();
    CHECK(e2.provider_request_preview("MSG-RI2 again", f.opts()));
  }
}
