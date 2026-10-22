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

// M1-K1c1: twelve earlier conversations tagged "tech" (the graph answers a question about the database and API), a short history, a narrow graph width.
void seed_graph(Fixture& f) {
  auto topic = unwrap(f.rt->db().get_or_create_node("tech", "topic"));
  for (int k = 0; k < 12; ++k) {
    auto old_conv = unwrap(f.rt->db().create_conv("Old conversation " + std::to_string(k)));
    NewMessage nm; nm.conv_id = old_conv.id; nm.text = "we discussed the database and the API design, variant " + std::to_string(k) + " of the server code"; nm.role = "assistant";
    unwrap(f.rt->db().create_link(unwrap(f.rt->db().create_msg(nm)), topic, "tagged_with", 0.5));
  }
  for (int i = 0; i < 6; ++i) f.add(i % 2 ? "assistant" : "user", "message number " + std::to_string(i) + " about the database and API of the plan");
  f.rt->config().set("graph_memory_max_nodes", 2);
}
bool said_any(const std::string& text, std::initializer_list<const char*> parts) {
  for (const char* p : parts) if (text.find(p) != std::string::npos) return true;
  return false;
}
// A real send; the guard values the receipt of that request carries.
Json send_and_guard(Fixture& f, const ChatOptions& opts, const std::string& text) {
  f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
  unwrap(f.rt->chat().send(text, opts));
  Json trace = nullptr;
  for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv_id)))
    if (m.role == "user" && m.text == text && m.metadata.is_object() && m.metadata.contains("context_trace")) trace = m.metadata["context_trace"];
  REQUIRE(trace.is_object());
  return trace["receipt"].value("guard", Json(nullptr));
}

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
    INFO(f.text_of(said));
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
    CHECK(done.value("status", "") == "completed");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).at("context.breadth_delta").size() == waiting_before.at("context.breadth_delta").size() + 1);
    // the receipt of the request that follows carries the guard: baseline, ratio and that the guard asked (the person confirmed it above)
    opts.stream = false;
    const Json waiting_after = f.rt->chat().turn_carrier().peek(f.conv_id);
    // the request itself is still the guard's to admit: a request ten times the usual one is held again at send time (the question about a
    // modifier is early notice, the guard of the send is the one that binds), nothing is sent, and the values of the turn wait for the re-send
    const auto held = unwrap(f.rt->chat().send("and now?", opts));
    REQUIRE(held.usage_pending.has_value());
    CHECK((*held.usage_pending)["status"] == "requires_confirmation");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id) == waiting_after);
    ChatOptions again = opts;
    again.usage_confirmation = Json{{"approved", true}, {"receipt_id", (*held.usage_pending)["receipt_id"]}, {"ref", "test"}, {"operation_id", (*held.usage_pending)["operation_id"]}};
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    unwrap(f.rt->chat().send("and now?", again));
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).empty());  // spent by the request that went out
    Json trace = nullptr;
    for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv_id)))
      if (m.role == "user" && m.text == "and now?" && m.metadata.is_object() && m.metadata.contains("context_trace")) trace = m.metadata["context_trace"];
    REQUIRE(trace.is_object());
    REQUIRE(trace.contains("receipt"));
    const Json guard = trace["receipt"].value("guard", Json(nullptr));
    REQUIRE(guard.is_object());
    INFO(guard.dump());
    CHECK(guard["baseline"] == base_bytes);
    REQUIRE(guard["ratio"].is_number());
    CHECK(guard["ratio"].get<double>() > 10.0);
    CHECK(guard["asked"] == true);
    CHECK(guard["confirmed"] == true);
    CHECK_FALSE(guard.contains("unknown_reason"));
  }

  // M1-K1c1: the owner saw two numbers for ONE widened request - the estimate after "Szerzej" (about 1.1x) and the guard of the receipt after the send
  // (1.8x). Measured here (graph of 12 earlier conversations, widened = the graph width x2, 6 sends so that the base is a measured one):
  //   estimate with text "" (what context.modify compiled until K1c1): 1394 bytes = 0.87x of the base 1593.5 (measured, 6 samples) - the same as the
  //                                                           request without "wider": the graph is chosen by the text of the question, an empty
  //                                                           text selects nothing, so the widening had nothing to widen;
  //   the same request with the text of the send:              1960 bytes = 1.23x of the same base;
  //   guard in the receipt of the send (text + 1 byte):        1961 bytes = 1.23x of the same base (baseline, samples and source equal).
  // The base is the same at both moments; it moves only by the send itself (one more sample), and until 5 requests were measured it is a starting floor.
  TEST_CASE("M1-K1c1 (a): the estimate compiled with the text of the send says what the receipt says; without a text it is another request; the base is named and moves only by a send") {
    Fixture f;
    seed_graph(f);
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    const auto send = [&](const std::string& text) { return send_and_guard(f, opts, text); };
    // the first request has no base, the next ones are measured against a starting floor, from the fifth sample on against what the requests of the kind measured: named in the guard
    for (int i = 0; i < 5; ++i) {
      const Json g = send("an earlier question number " + std::to_string(i) + " about the database");
      INFO(g.dump());
      if (i == 0) { CHECK(g["baseline"].is_null()); continue; }   // nothing was measured yet: no base, said as unknown
      CHECK(g["baseline_source"] == "floor");
      CHECK(g["baseline_samples"] == i);   // the samples the request was compared with: the ones before it
    }
    const std::string text = "and the database API server code again?";
    const Json wider = Json{{"context.breadth_delta", "wider"}};
    const Json plain_empty = unwrap(f.rt->chat().context_guard(f.conv_id, "", opts));
    const Json wider_empty = unwrap(f.rt->chat().context_guard(f.conv_id, "", opts, wider));
    const Json plain_text = unwrap(f.rt->chat().context_guard(f.conv_id, text, opts));
    const Json est = unwrap(f.rt->chat().context_guard(f.conv_id, text, opts, wider));
    INFO(wider_empty.dump());
    INFO(est.dump());
    // without a text nothing selects the graph, so widening it changes nothing (this is why the estimate of a change made before the text is typed
    // cannot be the estimate of the send); with the text it does
    CHECK(wider_empty["bytes"] == plain_empty["bytes"]);
    CHECK(est["bytes"].get<std::int64_t>() > plain_text["bytes"].get<std::int64_t>());
    CHECK(est["bytes"].get<std::int64_t>() > wider_empty["bytes"].get<std::int64_t>());
    // one base for all of them at this moment
    for (const Json* g : {&plain_empty, &wider_empty, &plain_text, &est}) {
      CHECK((*g)["baseline"] == est["baseline"]);
      CHECK((*g)["baseline_samples"] == est["baseline_samples"]);
      CHECK((*g)["baseline_source"] == "measured");
      CHECK((*g)["ratio"].get<double>() == doctest::Approx((*g)["bytes"].get<double>() / (*g)["baseline"].get<double>()));   // the ratio is always bytes over the base it names
    }
    // the send: the receipt says what the estimate with the same text said
    f.rt->chat().turn_carrier().add(f.conv_id, "context.breadth_delta", "wider", true);
    const Json sent = send(text);
    INFO(sent.dump());
    CHECK(sent["bytes"] == est["bytes"]);
    CHECK(sent["baseline"] == est["baseline"]);
    CHECK(sent["baseline_samples"] == est["baseline_samples"]);
    CHECK(sent["baseline_source"] == est["baseline_source"]);
    CHECK(sent["ratio"].get<double>() == doctest::Approx(est["ratio"].get<double>()));
    CHECK(sent["asked"] == est["asked"]);
    // a send in between moves the base by exactly that send - and says so (one sample more), the numbers stay bytes over the base they name
    const Json after = unwrap(f.rt->chat().context_guard(f.conv_id, text, opts));
    CHECK(after["baseline_samples"].get<int>() == est["baseline_samples"].get<int>() + 1);
    CHECK(after["baseline"] != est["baseline"]);
    CHECK(after["ratio"].get<double>() == doctest::Approx(after["bytes"].get<double>() / after["baseline"].get<double>()));
  }

  // M1-K1c1 (b): the action itself - what the person sees after "Szerzej".
  TEST_CASE("M1-K1c1 (b): context.modify compiles the estimate with the pending text and options and says what it was compiled for; the send then says the same") {
    Fixture f;
    seed_graph(f);
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    for (int i = 0; i < 5; ++i) send_and_guard(f, opts, "an earlier question number " + std::to_string(i) + " about the database");
    const std::string text = "and the database API server code again?";
    const Json ctx{{"conv_id", f.conv_id}};
    // the sends of this test do not stream (the scripted transport answers whole), so the estimate says so too: the option of the send is counted
    // (a streaming send has "stream":true, one byte fewer) - the options a composer would pass with the text
    const auto modify = [&](Json args) {
      args["context"] = ctx;
      if (!args.contains("pending_options")) args["pending_options"] = Json{{"stream", false}};
      else args["pending_options"]["stream"] = false;
      return unwrap(f.rt->actions().invoke_port("actions", "context.modify", args));
    };
    const auto data_of = [](const Json& run) { return run.at("result").at("data"); };
    const auto text_of = [](const Json& run) { return run.at("result").at("parts").at(0).at("text").get<std::string>(); };
    // no text: the estimate is for an empty message, said in the data and in the sentence; widening changes nothing there
    const Json plain_empty = unwrap(f.rt->chat().context_guard(f.conv_id, "", opts));
    const Json empty = modify(Json{{"step", "wider"}});
    REQUIRE(empty.value("status", "") == "completed");
    CHECK(data_of(empty).at("guard").at("estimate_for") == "empty_message");
    CHECK(data_of(empty).at("guard").at("pending_text_bytes") == 0);
    CHECK(data_of(empty).at("guard").at("pending_options") == true);
    CHECK(data_of(empty).at("guard").at("bytes") == plain_empty.at("bytes"));
    INFO(text_of(empty));
    CHECK(said_any(text_of(empty), {"pustej wiadomości", "an empty message"}));   // in the language of the call
    CHECK(said_any(text_of(empty), {"średnia z 5 ostatnich zapytań", "the average of the last 5 requests"}));
    unwrap(f.rt->actions().invoke_port("actions", "context.modify", Json{{"reset", true}, {"context", ctx}}));
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).empty());
    // with the typed text: the estimate is the request of the send
    const Json typed = modify(Json{{"step", "wider"}, {"pending_text", text}});
    REQUIRE(typed.value("status", "") == "completed");
    const Json tg = data_of(typed).at("guard");
    INFO(tg.dump());
    CHECK(tg.at("estimate_for") == "pending_text");
    CHECK(tg.at("pending_text_bytes") == text.size());
    CHECK(tg.at("bytes").get<std::int64_t>() > data_of(empty).at("guard").at("bytes").get<std::int64_t>());
    INFO(text_of(typed));
    CHECK(said_any(text_of(typed), {"wpisanym przez Ciebie tekstem", "the text you typed"}));
    CHECK(said_any(text_of(typed), {"średnia z 5 ostatnich zapytań", "the average of the last 5 requests"}));
    const Json sent = send_and_guard(f, opts, text);
    CHECK(sent["bytes"] == tg["bytes"]);
    CHECK(sent["baseline"] == tg["baseline"]);
    CHECK(sent["baseline_samples"] == tg["baseline_samples"]);
    CHECK(sent["baseline_source"] == tg["baseline_source"]);
    CHECK(sent["ratio"].get<double>() == doctest::Approx(tg["ratio"].get<double>()));
    // the options of the send are counted: another model is another request of another kind (its own base, which does not exist yet: unknown, never zero)
    const Json other = modify(Json{{"step", "wider"}, {"pending_text", text}, {"pending_options", Json{{"model", "test/other-model"}}}});
    REQUIRE(other.value("status", "") == "completed");
    CHECK(data_of(other).at("guard").at("pending_options") == true);
    CHECK(data_of(other).at("guard").at("model") == "test/other-model");
    CHECK(data_of(other).at("guard").at("ratio").is_null());
    // options that are not options are refused with the reason, nothing waits for the next message
    unwrap(f.rt->actions().invoke_port("actions", "context.modify", Json{{"reset", true}, {"context", ctx}}));
    const Json bad = modify(Json{{"step", "wider"}, {"pending_options", Json{{"web_search", "yes"}}}});
    CHECK(bad.value("status", "") != "completed");
    CHECK(f.rt->chat().turn_carrier().peek(f.conv_id).empty());
  }
}
