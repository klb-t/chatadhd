// M1-K1b: the turn modifiers and the receipt operations. The sentence "szerzej" (and its siblings) is a command for the action context.modify - it
// changes the NEXT request only, through the turn carrier, and writes no rule; "na stałe w tej rozmowie" is a rule (rules.set at the conversation
// level); "co wysłaliśmy" is context.receipt. A change that would make the next request ten times the size of its kind is asked of the person by the
// shared usage guard; the guard values (baseline, ratio, asked) are in the receipt.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json openai_reply(std::string content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 42}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("model_registry_autorefresh", "off");
    rt->config().set("auto_title", false);
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  std::filesystem::path overlay_file(const std::string& domain) { return td.path() / "profiles" / (domain + ".pack"); }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(overlay_file(domain)) << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
  // The presets of chat_context plus a profile of our own whose history window is `window` messages (the base of the x10 tests).
  void own_window_profile(const std::string& id, int window) {
    Json all = unwrap(RuntimeProfile::load("chat_context", td.path())).values().at("selector_profiles");
    all.push_back(Json{{"id", id}, {"label", Json{{"pl", id}, {"en", id}}}, {"why", Json{{"pl", "test"}, {"en", "test"}}}, {"order", "stability"},
                       {"sources", Json{{"profile", true}, {"rules", true}, {"memory", true}, {"project", true}, {"history", true}, {"graph", true}}},
                       {"breadth", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", window}}}, {"depth", Json{{"hops", nullptr}}},
                       {"representation", nullptr}, {"flow", nullptr}, {"budget_tokens", nullptr}, {"status", "preset"}, {"note", Json{{"pl", "test"}, {"en", "test"}}}});
    overlay("chat_context", Json{{"selector_profiles", all}});
  }
  std::string action_of(const Json& routed) { return routed.value("command", Json::object()).value("action", ""); }
};

}  // namespace

TEST_SUITE("context_modify") {
  TEST_CASE("M1-K1b (d): 'szerzej' as a sentence is context.modify, nothing is written to profiles/action_rules.pack; 'opisz to szerzej' stays a message") {
    Fixture f;
    f.add("user", "We should ship on Friday.");
    for (const char* text : {"szerzej", "Szerzej.", "wider", "weź szerszy kontekst", "więcej kontekstu", "głębiej o jeden", "płycej", "węziej", "shallower"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(f.action_of(r) == "context.modify");
    }
    CHECK(f.route("szerzej").at("command").at("args").at("step") == "wider");
    CHECK(f.route("głębiej o jeden").at("command").at("args").at("step") == "deeper");
    CHECK(f.route("profil selektora lean").at("command").at("args").at("profile") == "lean");
    CHECK(f.route("selector profile lean").at("command").at("action") == "context.modify");
    // a word inside a message is not a command
    for (const char* text : {"opisz to szerzej", "Czy możesz to wytłumaczyć głębiej, proszę?", "describe it wider"}) {
      INFO(text);
      CHECK(f.action_of(f.route(text)) != "context.modify");
    }
    // "co wysłaliśmy" is the record of the last request
    CHECK(f.action_of(f.route("co wysłaliśmy")) == "context.receipt");
    CHECK(f.action_of(f.route("what did we send to the model")) == "context.receipt");
    // permanently in this conversation is a rule, not a change for the next turn
    const Json permanent = f.route("szerzej na stałe w tej rozmowie");
    REQUIRE(permanent.at("kind") == "command");
    CHECK(f.action_of(permanent) == "rules.set");
    CHECK(permanent.at("command").at("args").at("key") == "context.breadth_delta");
    CHECK(permanent.at("command").at("args").at("value") == "wider");
    CHECK(permanent.at("command").at("args").at("scope") == "conversation");
    CHECK(f.action_of(f.route("wider permanently in this conversation")) == "rules.set");

    // said in the chat: the carrier holds the step for this conversation, no rule exists
    const auto out = f.input("szerzej");
    CHECK(out.handled);
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id) == Json{{"context.breadth_delta", Json::array({"wider"})}});
    CHECK_FALSE(std::filesystem::exists(f.overlay_file("action_rules")));
    // twice = applied twice; a profile replaces; "reset" drops it
    f.input("szerzej");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).at("context.breadth_delta") == Json::array({"wider", "wider"}));
    f.input("profil selektora lean");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).at("context.profile") == "lean");
    CHECK_FALSE(std::filesystem::exists(f.overlay_file("action_rules")));
    // the plain message after the modifier is a message, and it takes the modifier with it
  }

  TEST_CASE("M1-K1b (g): context.receipt returns the receipt of a request (also asked about the answer), and for an old record the reason, not an invented receipt") {
    Fixture f;
    const std::string old_user = f.add("user", "a question from before the records existed");
    f.add("assistant", "an old answer");
    // a message saved before the records existed: no receipt, and the answer says why
    const Json missing = unwrap(f.rt->actions().invoke_port("actions", "context.receipt", Json{{"msg_id", old_user}, {"context", Json{{"conv_id", f.conv_id}}}}));
    INFO(missing.dump());
    CHECK(missing.dump().find("before_m1") != std::string::npos);
    CHECK(missing.dump().find("loom.context_receipt/1") == std::string::npos);
    // a real request leaves the receipt with the user message; asked about the answer, the record of the request that made it comes back
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    const auto sent = unwrap(f.rt->chat().send("what is new?", opts));
    const Json got = unwrap(f.rt->actions().invoke_port("actions", "context.receipt", Json{{"msg_id", sent.assistant_message_id}, {"context", Json{{"conv_id", f.conv_id}}}}));
    INFO(got.dump());
    CHECK(got.dump().find("loom.context_receipt/1") != std::string::npos);
    CHECK(got.dump().find(sent.user_message_id) != std::string::npos);
    CHECK(got.dump().find("before_m1") == std::string::npos);
    // said in the chat: the last request of the conversation
    const auto said = f.input("co wysłaliśmy");
    REQUIRE(said.handled);
    CHECK(f.text_of(said).find("tokenów") != std::string::npos);
  }

  TEST_CASE("M1-K1b (f): a modifier that would make the next request ten times the usual one is asked through the shared usage guard; the guard values are real") {
    Fixture f;
    for (int i = 0; i < 120; ++i) f.add(i % 2 ? "assistant" : "user", "message number " + std::to_string(i) + " about the plan, long enough to count as a real piece of the conversation history");
    f.own_window_profile("window2", 2);
    f.rt->config().set("selector_profile", "window2");
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    // no ledger yet: nothing was measured, so there is no baseline - said as unknown, never as zero
    const Json none = unwrap(f.rt->chat().context_guard(f.conv_id, "", opts));
    CHECK(none["unknown_reason"] == "no_usage_ledger_yet");
    CHECK(none["baseline"].is_null());
    CHECK(none["ratio"].is_null());
    CHECK(none["asked"] == false);
    const std::int64_t base_bytes = none["bytes"].get<std::int64_t>();
    REQUIRE(base_bytes > 0);
    // the baseline of the kind is what the requests of this kind measured (here a declared one: the size of the request as it is)
    const std::string key = usage_baseline_key(f.rt->config(), "chat.send", "https://api.test", "test/model");
    f.rt->config().set("loom_usage_policy", Json{{"baseline_min_samples", 0}, {"initial_baselines", Json{{key, Json{{"input_bytes", base_bytes}}}}}});
    unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", unwrap(effective_usage_policy_options(f.rt->config()))));
    const Json same = unwrap(f.rt->chat().context_guard(f.conv_id, "", opts));
    CHECK(same["baseline"] == base_bytes);
    CHECK(same["ratio"].get<double>() == doctest::Approx(1.0));
    CHECK(same["asked"] == false);
    CHECK(same["growth_factor"] == 10.0);
    CHECK(same["baseline_key"] == key);
    // "szerzej" doubles the window of the profile: the first steps are below ten times and are not asked
    int applied = 0;
    bool asked = false;
    Json held_meta;
    for (int i = 0; i < 12 && !asked; ++i) {
      const Json before = f.rt->chat().turn_carrier().peek(f.conv_id);
      const auto out = f.input("szerzej");
      REQUIRE(out.handled);
      const Json after = f.rt->chat().turn_carrier().peek(f.conv_id);
      if (after == before) {
        asked = true;
        held_meta = f.meta(out);
      } else {
        ++applied;
      }
    }
    CHECK(applied >= 1);   // the small steps went through without a question
    REQUIRE(asked);        // and a step that crosses ten times was held
    INFO(held_meta.dump());
    // the question is the guard's own: a run waiting for the answer, with the receipt of the ledger
    const Json run = unwrap(f.rt->actions().invoke_port("actions", "context.modify", Json{{"step", "wider"}, {"context", Json{{"conv_id", f.conv_id}}}}));
    INFO(run.dump());
    REQUIRE(run.value("status", "") == "awaiting_usage");
    const Json pending = run.at("pending");
    CHECK(pending.at("kind") == "usage");
    const std::string receipt = pending.at("receipt_id");
    CHECK(receipt.rfind("usage_", 0) == 0);
    const Json waiting_before = f.rt->chat().turn_carrier().peek(f.conv_id);
    // declined: nothing is changed
    const Json declined = unwrap(f.rt->actions().invoke_port("actions", "confirm", Json{{"run_id", run.at("id")}, {"approved", false}, {"receipt_id", receipt}, {"ref", "test"}}));
    CHECK(declined.value("status", "") == "rejected");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id) == waiting_before);
    // confirmed: the step is applied, and the carrier is the only place it lives
    const Json run2 = unwrap(f.rt->actions().invoke_port("actions", "context.modify", Json{{"step", "wider"}, {"context", Json{{"conv_id", f.conv_id}}}}));
    REQUIRE(run2.value("status", "") == "awaiting_usage");
    const Json done = unwrap(f.rt->actions().invoke_port("actions", "confirm", Json{{"run_id", run2.at("id")}, {"approved", true}, {"receipt_id", run2.at("pending").at("receipt_id")}, {"ref", "test"}}));
    INFO(done.dump());
    CHECK(done.value("status", "") == "done");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).at("context.breadth_delta").size() == waiting_before.at("context.breadth_delta").size() + 1);
    // the receipt of the request that follows carries the guard: baseline, ratio and that the guard asked (the person confirmed it above)
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    opts.stream = false;
    unwrap(f.rt->chat().send("and now?", opts));
    Json trace;
    for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv_id)))
      if (m.role == "user" && m.text == "and now?") trace = m.metadata.at("context_trace");
    const Json guard = trace.at("receipt").at("guard");
    INFO(guard.dump());
    CHECK(guard.at("baseline") == base_bytes);
    CHECK(guard.at("ratio").get<double>() > 10.0);
    CHECK(guard.at("asked") == true);
    CHECK_FALSE(guard.contains("unknown_reason"));
  }
}
