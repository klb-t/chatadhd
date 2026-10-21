// M1-K1a: the context kernel. The classes profile and rules, the selector profile (request, scope chain), the deterministic profile prefix and the
// receipt loom.context_receipt/1 that every chat request leaves with the user message. The golden bytes of the historical order are pinned in
// tests/test_chat_engine.cpp (tests/fixtures/golden/chat_request_legacy.json).
#include <doctest/doctest.h>

#include "chat/profile_prefix.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/guardian.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/onboarding_store.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json openai_reply(std::string content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 42}}}};
}

// A chat with memory, graph, history and rules about the person, like the order fixture of test_chat_engine.cpp.
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
  std::vector<std::string> history_ids;

  // golden_shape: the single earlier turn of the pinned request (tests/fixtures/golden/chat_request_legacy.json); else four turns.
  explicit Fixture(bool golden_shape = false) {
    secrets.set("api_key", "sk-test-key");
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
    cfg.set("system_prompt", "SYS-S0");
    cfg.set("auto_title", false);
    unwrap(mem.add_node("MEM-S1 core fact"));
    auto old_conv = unwrap(db->create_conv("Old conversation"));
    NewMessage nm; nm.conv_id = old_conv.id; nm.text = "we discussed the database and the API design"; nm.role = "assistant";
    auto old_mid = unwrap(db->create_msg(nm));
    auto topic = unwrap(db->get_or_create_node("tech", "topic"));
    unwrap(db->create_link(old_mid, topic, "tagged_with", 0.5));
    auto cur = unwrap(db->create_conv("Current"));
    conv_id = cur.id;
    for (int i = 1; i <= (golden_shape ? 1 : 4); ++i) {
      NewMessage h; h.conv_id = cur.id; h.text = golden_shape ? "HIST-S3 earlier turn" : "HIST-S3 turn " + std::to_string(i); h.role = i % 2 ? "user" : "assistant";
      history_ids.push_back(unwrap(db->create_msg(h)));
    }
  }
  ChatEngine engine() { return ChatEngine(cfg, secrets, *db, bus, transport, *analyzer, &mem, &gm); }
  // Overlay of the pack chat_context in the data directory (a user's own selector profiles, or none at all).
  // The presets of the pack plus the person's own selector profiles (an overlay replaces an array, so the presets are listed again).
  void add_selector_profiles(const std::vector<Json>& own) {
    const Json base = unwrap(RuntimeProfile::load("chat_context", td.path())).values().at("selector_profiles");
    Json all = base;
    for (const auto& p : own) all.push_back(p);
    overlay_chat_context(Json{{"selector_profiles", all}});
  }
  void overlay_chat_context(const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    Json o{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"}, {"overrides", overrides}};
    REQUIRE(fsutil::write_file(td.path() / "profiles" / "chat_context.pack", o.dump()));
  }
  Json send(const ChatOptions& base, const std::string& text = "tell me about the database and API again") {
    transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    auto e = engine();
    ChatOptions opts = base;
    opts.conv_id = conv_id;
    opts.stream = false;
    unwrap(e.send(text, opts));
    return unwrap(json::parse(transport.requests().back().body));
  }
  // The receipt kept with the user message of the last request.
  Json stored_trace(const std::string& text = "tell me about the database and API again") {
    for (const auto& m : unwrap(db->get_msgs(conv_id)))
      if (m.role == "user" && m.text == text && m.metadata.is_object() && m.metadata.contains("context_trace")) return m.metadata["context_trace"];
    return nullptr;
  }
  Json compile(const ChatOptions& base, Json& trace) {
    auto e = engine();
    ChatOptions opts = base;
    return unwrap(e.build_messages(conv_id, "the question", {}, opts, "", &trace));
  }
};

Json section(const Json& receipt, const std::string& cls) {
  for (const auto& s : receipt["sections"]) if (s["class"] == cls) return s;
  return nullptr;
}
std::vector<std::string> classes(const Json& receipt) {
  std::vector<std::string> out;
  for (const auto& s : receipt["sections"]) out.push_back(s["class"].get<std::string>());
  return out;
}
Json dropped(const Json& receipt, const std::string& reason) {
  for (const auto& d : receipt["dropped"]) if (d["reason"] == reason) return d;
  return nullptr;
}
std::vector<Json> prefix_of(const Json& body) {  // the messages before the memory: instruction, profile, rules
  std::vector<Json> out;
  for (const auto& m : body["messages"]) {
    if (m["content"].get<std::string>().find("User's memory/context") == 0) break;
    out.push_back(m);
  }
  return out;
}

// A user's own profile of the selector, as an overlay would add it.
Json own_profile(const std::string& id, Json breadth, const Json& budget = nullptr) {
  return Json{{"id", id}, {"label", Json{{"pl", id}, {"en", id}}}, {"why", Json{{"pl", "test"}, {"en", "test"}}}, {"order", "stability"},
              {"sources", Json{{"profile", true}, {"rules", true}, {"memory", true}, {"project", true}, {"history", true}, {"graph", true}}},
              {"breadth", std::move(breadth)}, {"depth", Json{{"hops", nullptr}}}, {"representation", nullptr}, {"flow", nullptr},
              {"budget_tokens", budget}, {"status", "preset"}, {"note", Json{{"pl", "test"}, {"en", "test"}}}};
}

void set_preference(Fixture& f, const std::string& key, const Json& value) {
  onboarding::OnboardingStore store(*f.db);
  auto read = store.read("tester");
  Json st = read ? *read : unwrap(store.open("tester"));
  unwrap(store.apply("tester", st.at("revision").get<std::int64_t>(), Json{{"target", "layers"}, {"op", "override"}, {"key", key}, {"value", value}}));
}

}  // namespace

TEST_SUITE("context_receipt") {
  TEST_CASE("M1-K1a (a): profile loom_legacy and an effective view without selector_profiles give the pinned bytes of today") {
    const Json pinned = [] {
      auto text = fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "golden" / "chat_request_legacy.json");
      REQUIRE(text);
      return unwrap(json::parse(*text));
    }();
    {  // loom_legacy: order legacy, no profile prefix; the rules stay with the instruction
      Fixture f(true);
      ChatOptions o; o.selector_profile = "loom_legacy"; o.guardian_rules = "RULES-R0 never use emoji";
      CHECK(f.send(o) == pinned["legacy"]);
    }
    {  // no selector_profiles at all: the settings as they are (preset order stability, empty profile = stable_first), and legacy one setting away
      Fixture f(true);
      f.overlay_chat_context(Json{{"selector_profiles", Json::array()}});
      ChatOptions o; o.guardian_rules = "RULES-R0 never use emoji";
      CHECK(f.send(o) == pinned["stability"]);
      CHECK(pinned["stability"] == pinned["stable_first"]);
      Fixture g(true);
      g.overlay_chat_context(Json{{"selector_profiles", Json::array()}});
      g.cfg.set("chat_context_order", "legacy");
      CHECK(g.send(o) == pinned["legacy"]);
      ChatOptions named; named.selector_profile = "loom"; named.guardian_rules = o.guardian_rules;
      Fixture h(true);
      h.overlay_chat_context(Json{{"selector_profiles", Json::array()}});
      h.transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
      auto e = h.engine();
      named.conv_id = h.conv_id; named.stream = false;
      CHECK_FALSE(e.send("x", named));  // a profile named by the request that does not exist is refused, not ignored
    }
    {  // profile loom with rules: the rules are their own class now, in the same place, the same bytes as stable_first
      Fixture f(true);
      ChatOptions o; o.selector_profile = "loom"; o.guardian_rules = "RULES-R0 never use emoji";
      CHECK(f.send(o) == pinned["stability"]);
    }
    {  // perturbing the order must show: the same request under stable_first is not the legacy one
      Fixture f(true);
      ChatOptions o; o.selector_profile = "loom_legacy"; o.context_order = "stable_first"; o.guardian_rules = "RULES-R0 never use emoji";
      CHECK(f.send(o) != pinned["legacy"]);
    }
  }

  TEST_CASE("M1-K1a (b): the prefix (instruction, profile, rules) is identical byte for byte between two turns while the profile is unchanged") {
    Fixture f;
    set_preference(f, "preference.style", "warm and short");
    set_preference(f, "preference.language", "pl");
    ChatOptions o; o.guardian_rules = "RULES-R0 never use emoji";
    const Json first = f.send(o, "first question about the database");
    const Json second = f.send(o, "second question about the API");
    const auto p1 = prefix_of(first), p2 = prefix_of(second);
    REQUIRE(p1.size() == 3);  // instruction, profile, rules
    CHECK(json::dump(Json(p1)) == json::dump(Json(p2)));
    const std::string profile = p1[1]["content"].get<std::string>();
    CHECK(profile.find("[Preferences]") != std::string::npos);
    CHECK(profile.find("- preference.language: pl") != std::string::npos);
    CHECK(profile.find("- preference.style: warm and short") != std::string::npos);
    CHECK(profile.find("preference.style") < profile.find("preference.language"));  // fixed order of the keys, not insertion order
    CHECK(p1[2]["content"] == "RULES-R0 never use emoji");
    // No time stamps, no counters: nothing but the header and the lines.
    CHECK(profile.find("20") == std::string::npos);
    // Changing the profile changes the prefix, and only the profile message.
    set_preference(f, "preference.style", "formal");
    const auto p3 = prefix_of(f.send(o, "third question"));
    REQUIRE(p3.size() == 3);
    CHECK(p3[0] == p1[0]);
    CHECK(p3[1] != p1[1]);
    CHECK(p3[2] == p1[2]);
  }

  TEST_CASE("M1-K1a: profile_detail_level chooses the region; presets and proposals are not the person's words; legacy sends no profile at all") {
    Fixture f;
    set_preference(f, "preference.style", "warm");
    set_preference(f, "preference.length", "long");
    set_preference(f, "preference.language", "pl");
    f.cfg.set("profile_detail_level", "minimal");
    std::string profile = prefix_of(f.send(ChatOptions{}))[1]["content"].get<std::string>();
    CHECK(profile.find("preference.style") != std::string::npos);
    CHECK(profile.find("preference.language") != std::string::npos);
    CHECK(profile.find("preference.length") == std::string::npos);  // minimal: style and language only
    f.cfg.set("profile_detail_level", "normal");
    profile = prefix_of(f.send(ChatOptions{}))[1]["content"].get<std::string>();
    CHECK(profile.find("preference.length: long") != std::string::npos);
    f.cfg.set("profile_detail_level", "no_such_level");  // an unknown level reads the default of the recipe, it never sends everything
    CHECK(prefix_of(f.send(ChatOptions{}))[1]["content"].get<std::string>() == profile);
    // legacy: the profile class is not in the order, nothing of it is sent
    ChatOptions legacy; legacy.context_order = "legacy";
    for (const auto& m : f.send(legacy)["messages"]) CHECK(m["content"].get<std::string>().find("preference.") == std::string::npos);
    // Without anything the person set themselves, there is no profile message (stability = stable_first).
    Fixture empty;
    const auto bare = prefix_of(empty.send(ChatOptions{}));
    CHECK(bare.size() == 1);
    // The pure renderer: same input, same bytes; an empty value says nothing.
    const Json recipe = unwrap(RuntimeProfile::load("chat_context", empty.td.path())).values().at("profile_prefix");
    const Json subgraphs = Json::parse(R"([{"id":"b","label":{"en":"B"},"order_for_prefix":2,"stability_rank":2},{"id":"a","label":{"en":"A"},"order_for_prefix":1,"stability_rank":1},{"id":"m","label":{"en":"M"},"order_for_prefix":null,"stability_rank":7}])");
    Json r2 = recipe;
    r2["subgraph_keys"] = Json::parse(R"({"a":["x.one"],"b":["x.two","x.three"],"m":["x.mem"]})");
    r2["regions"]["normal"]["subgraphs"] = nullptr;
    const std::vector<chat::prefix::Fact> facts{{"b", "x.three", "3"}, {"m", "x.mem", "no"}, {"a", "x.one", Json::parse("[\"1\",\"\",\"2\"]")}, {"b", "x.two", ""}};
    const auto one = chat::prefix::render(r2, subgraphs, "normal", facts), two = chat::prefix::render(r2, subgraphs, "normal", facts);
    CHECK(one.text == two.text);
    CHECK(one.text.find("[A]\n- x.one: 1, 2\n[B]\n- x.three: 3") != std::string::npos);  // by order_for_prefix; the null one is not in the prefix; empty skipped
    CHECK(one.text.find("x.mem") == std::string::npos);
    CHECK(one.text.find("x.two") == std::string::npos);
  }

  TEST_CASE("M1-K1a (e): the receipt has the sections in the order of the request, items with tokens, dropped with reasons and the sha256 of the request") {
    Fixture f;
    set_preference(f, "preference.style", "warm");
    ChatOptions o; o.guardian_rules = "RULES-R0 never use emoji";
    const Json body = f.send(o);
    const Json trace = f.stored_trace();
    REQUIRE(trace.is_object());
    CHECK(trace["kind"] == "compiled_messages");
    CHECK_FALSE(trace.contains("messages"));  // references, not contents: the slim trace holds no text of the request
    const Json r = trace["receipt"];
    CHECK(r["schema"] == "loom.context_receipt/1");
    CHECK(classes(r) == std::vector<std::string>{"system", "profile", "rules", "memory", "project", "history", "graph", "question"});
    CHECK(r["order"]["name"] == "stability");
    for (const char* cls : {"system", "profile", "rules", "memory", "history", "graph", "question"}) {
      const Json s = section(r, cls);
      REQUIRE(s.is_object());
      REQUIRE_FALSE(s["items"].empty());
      for (const auto& i : s["items"]) {
        CHECK(i["tokens_est"].get<long long>() > 0);
        CHECK_FALSE(i["ref"].get<std::string>().empty());
        CHECK_FALSE(i["source_kind"].get<std::string>().empty());
        CHECK_FALSE(i["why"].get<std::string>().empty());
      }
    }
    CHECK(section(r, "project")["items"].empty());
    CHECK(section(r, "profile")["items"][0]["source_kind"] == "profile_slot");
    CHECK(section(r, "profile")["items"][0]["ref"] == "preference.style");
    CHECK(section(r, "history")["items"].size() == 4);
    CHECK(section(r, "history")["items"][0]["source_kind"] == "message:L0");
    CHECK(r["sha256"] == Sha256::hex(json::dump(body["messages"])));
    CHECK(r["sha256"] == trace["messages_sha256"]);
    CHECK(r["profile"]["id"].is_null());
    CHECK(r["profile"]["source"] == "settings");
    CHECK(r["guard"]["asked"] == false);
    CHECK(r["guard"]["baseline"].is_null());
    CHECK(r["truncated"] == false);
    // References, not contents: none of the texts is in the stored trace.
    const std::string stored = json::dump(trace);
    for (const char* text : {"SYS-S0", "MEM-S1", "HIST-S3", "RULES-R0", "tell me about", "warm"}) CHECK(stored.find(text) == std::string::npos);
    // The tokens of the sections add up.
    long long sum = 0;
    for (const auto& s : r["sections"]) sum += s["tokens_est"].get<long long>();
    CHECK(r["tokens_est"] == sum);
  }

  TEST_CASE("M1-K1a (e): dropped carries the reasons (thread_filter, representation_level, history_window, excluded, budget, source_off) with counts") {
    Fixture f;
    f.add_selector_profiles({own_profile("window2", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", 2}}),
                             own_profile("tight", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", nullptr}}, 12)});
    // thread filter and a lossy level, as the guardian hands them to the selector
    ChatOptions o;
    o.history_skip = {f.history_ids[0]};
    o.history_skip_reason[f.history_ids[0]] = "thread_filter";
    o.history_text[f.history_ids[1]] = "short";
    o.history_level[f.history_ids[1]] = "L2";
    Json trace = Json::object();
    f.compile(o, trace);
    Json r = trace["receipt"];
    CHECK(dropped(r, "thread_filter")["count"] == 1);
    CHECK(dropped(r, "thread_filter")["refs"][0] == f.history_ids[0]);
    CHECK(dropped(r, "representation_level")["count"] == 1);
    CHECK(section(r, "history")["items"].size() == 3);
    bool lossy = false;
    const Json history_section = section(r, "history");  // bound to a name: no range-for over a temporary
    for (const auto& i : history_section["items"]) lossy = lossy || i["source_kind"] == "message:L2";
    CHECK(lossy);
    // a window of two messages
    ChatOptions w; w.selector_profile = "window2";
    trace = Json::object();
    f.compile(w, trace);
    r = trace["receipt"];
    CHECK(r["profile"]["id"] == "window2");
    CHECK(r["profile"]["source"] == "request");
    CHECK(dropped(r, "history_window")["count"] == 2);
    CHECK(section(r, "history")["items"].size() == 2);
    CHECK(section(r, "history")["items"][0]["ref"] == f.history_ids[2]);
    // a budget: the graph goes first, then the oldest messages; the last one stays
    ChatOptions b; b.selector_profile = "tight";
    trace = Json::object();
    f.compile(b, trace);
    r = trace["receipt"];
    CHECK(dropped(r, "budget").is_object());
    CHECK(section(r, "history")["items"].size() >= 1);
    CHECK(section(r, "graph")["items"].empty());
    CHECK(r["profile"]["applied"]["budget"]["limit"] == 12);
    // an excluded message
    MsgPatch ex; ex.status = std::string(msg_status::kExcluded);
    REQUIRE(f.db->update_msg(f.history_ids[3], ex));
    trace = Json::object();
    f.compile(ChatOptions{}, trace);
    CHECK(dropped(trace["receipt"], "excluded")["refs"][0] == f.history_ids[3]);
    // a profile that does not read a source (compat): only the instruction, the conversation and the question reach the model
    ChatOptions c; c.selector_profile = "compat"; c.guardian_rules = "RULES-R0 never use emoji";
    trace = Json::object();
    const Json compiled = f.compile(c, trace);
    r = trace["receipt"];
    CHECK(dropped(r, "source_off")["ref"].is_string());
    for (const auto& m : compiled) {
      const std::string text = m["content"].get<std::string>();
      CHECK(text.find("MEM-S1") == std::string::npos);
      CHECK(text.find("RULES-R0") == std::string::npos);
      CHECK(text.find("Relevant context") == std::string::npos);
    }
    CHECK(section(r, "memory")["items"].empty());
    CHECK(r["profile"]["id"] == "compat");
  }

  TEST_CASE("M1-K1a: context_receipt_max_bytes cuts the references (truncated), always keeps everything, never stores nothing") {
    Fixture f;
    f.cfg.set("context_receipt_max_bytes", 400);
    f.send(ChatOptions{});
    Json r = f.stored_trace()["receipt"];
    CHECK(r["truncated"] == true);
    CHECK(r["truncated_reason"] == "context_receipt_max_bytes");
    CHECK(r["bytes_whole"].get<long long>() > 400);
    CHECK(classes(r).size() == 8);  // the sections stay; their items are summarised
    CHECK(section(r, "history")["items"].empty());
    CHECK(section(r, "history")["items_omitted"] == 4);
    CHECK(section(r, "history")["tokens_est"].get<long long>() > 0);
    Fixture g;
    g.cfg.set("context_receipt_max_bytes", 400);
    g.cfg.set("context_receipt_store", "always");
    g.send(ChatOptions{});
    CHECK(g.stored_trace()["receipt"]["truncated"] == false);
    CHECK(section(g.stored_trace()["receipt"], "history")["items"].size() == 4);
    Fixture n;
    n.cfg.set("context_receipt_store", "never");
    n.send(ChatOptions{});
    CHECK(n.stored_trace().is_null());  // the old behaviour: nothing is kept without a flag asking for it
    Fixture t;
    ChatOptions off; off.trace_context = false;
    t.send(off);
    CHECK(t.stored_trace().is_null());  // the request itself opted out
  }

  TEST_CASE("M1-K1a: the whole trace of a flagged request keeps today's fields and gets the receipt") {
    Fixture f;
    ChatOptions o; o.trace_context = true;
    f.send(o);
    const Json trace = f.stored_trace();
    CHECK(trace.contains("messages"));
    CHECK(trace.contains("history_message_ids"));
    CHECK(trace["receipt"]["sha256"] == trace["messages_sha256"]);
  }

  TEST_CASE("M1-K1a: the profile is chosen by the request, the turn value or the scope chain; breadth and depth modifiers read the same way") {
    Fixture f;
    f.add_selector_profiles({own_profile("window2", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", 2}})});
    {  // a value of this turn only: read here, put there by M1-K1b
      ChatOptions o; o.turn_values = Json{{"context.profile", "window2"}};
      Json trace = Json::object();
      f.compile(o, trace);
      CHECK(trace["receipt"]["profile"]["source"] == "turn");
      CHECK(section(trace["receipt"], "history")["items"].size() == 2);
      trace = Json::object();
      f.compile(ChatOptions{}, trace);  // the next request starts empty: the profile is gone
      CHECK(trace["receipt"]["profile"]["id"].is_null());
      CHECK(section(trace["receipt"], "history")["items"].size() == 4);
    }
    {  // "szerzej": the window of the profile widens (x2), "węziej" narrows it (/2); the receipt names the steps
      ChatOptions o; o.selector_profile = "window2"; o.turn_values = Json{{"context.breadth_delta", "wider"}};
      Json trace = Json::object();
      f.compile(o, trace);
      CHECK(section(trace["receipt"], "history")["items"].size() == 4);
      CHECK(trace["receipt"]["modifiers"]["breadth_delta"][0] == "wider");
      CHECK(trace["receipt"]["modifiers"]["scope"] == "turn");
      o.turn_values = Json{{"context.breadth_delta", "narrower"}};
      trace = Json::object();
      f.compile(o, trace);
      CHECK(section(trace["receipt"], "history")["items"].size() == 1);
    }
    {  // "płycej" on depth 1 switches the graph memory off
      ChatOptions o; o.context_depth = 1; o.turn_values = Json{{"context.depth_delta", "shallower"}};
      Json trace = Json::object();
      f.compile(o, trace);
      CHECK(section(trace["receipt"], "graph")["items"].empty());
      CHECK(trace["receipt"]["modifiers"]["depth_delta"][0] == "shallower");
    }
    {  // an id from the scope chain that names no profile is ignored, and the receipt says so
      ChatOptions o; o.turn_values = Json{{"context.profile", "no_such_profile"}};
      Json trace = Json::object();
      f.compile(o, trace);
      CHECK(trace["receipt"]["profile"]["id"].is_null());
      CHECK(trace["receipt"]["profile"]["ignored"].is_string());
    }
    {  // the setting selector_profile of the person is the base of the scope chain
      f.cfg.set("selector_profile", "window2");
      Json trace = Json::object();
      f.compile(ChatOptions{}, trace);
      CHECK(trace["receipt"]["profile"]["id"] == "window2");
      CHECK(trace["receipt"]["profile"]["source"] == "scope_chain");
    }
  }

  TEST_CASE("M1-K1a: a rule of the conversation on context.profile chooses the profile; the profile names the representation the guardian selects") {
    Fixture f;
    f.add_selector_profiles({own_profile("window2", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", 2}})});
    ConvPatch patch;
    patch.metadata = Json{{"settings", Json{{"context.profile", "window2"}}}};
    REQUIRE(f.db->update_conv(f.conv_id, patch));
    Json trace = Json::object();
    f.compile(ChatOptions{}, trace);
    CHECK(trace["receipt"]["profile"]["id"] == "window2");
    CHECK(trace["receipt"]["profile"]["source"] == "scope_chain");
    CHECK(section(trace["receipt"], "history")["items"].size() == 2);
    // the request wins over the chain
    ChatOptions o; o.selector_profile = "loom";
    trace = Json::object();
    f.compile(o, trace);
    CHECK(trace["receipt"]["profile"]["id"] == "loom");
    CHECK(trace["receipt"]["profile"]["source"] == "request");
    // the guardian takes the representation of the profile instead of asking the chain; none named = the chain decides (raw here)
    CHECK(unwrap(guardian::select(*f.db, f.cfg, f.conv_id, "lean")).preset == "lean");
    CHECK(unwrap(guardian::select(*f.db, f.cfg, f.conv_id)).preset == "raw");
    CHECK(unwrap(guardian::select(*f.db, f.cfg, f.conv_id, "no_such_preset")).preset == "raw");
    f.cfg.set("chat_representation", "lean");
    CHECK(unwrap(guardian::select(*f.db, f.cfg, f.conv_id)).preset == "lean");
    CHECK(unwrap(guardian::select(*f.db, f.cfg, f.conv_id, "raw")).preset == "raw");
  }
}
