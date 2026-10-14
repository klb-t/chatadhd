// P11 5 (R55): the catalogue grows from conversations, always as proposals and never silently. A command no action understands
// becomes a new action (a); a chain repeated N times becomes one composite (b); a user-defined action used on several classes
// is offered a group (c). Accepting writes only user overlays, every proposal leaves provenance in the graph, every definition can
// be taken back, and the setting `emergent_actions: off` switches it all off.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_overlay.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"
#include "test_receipts.h"

using namespace loom;
using loom::test::unwrap;
using loom::actions::OverlayStore;
using loom::actions::actions_define;
using loom::actions::actions_remove;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id, first, second;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests; fetching the model list for a price is tested in test_estimate_registry
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    first = add("user", "We should ship on Friday.");
    second = add("assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(const std::string& content, int n = 1) {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json body(std::size_t i) { return unwrap(json::parse(transport->requests().at(i).body)); }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  Message message(const std::string& id) { return *unwrap(rt->db().get_msg(id)); }
  Json proposal_of(const chat_route::Outcome& o) { return message(o.assistant_message_id).metadata.at("action_proposal"); }
  Json overlay(const std::string& domain) {
    const auto path = td.path() / "profiles" / (domain + ".pack");
    if (!std::filesystem::exists(path)) return Json::object();
    std::ifstream in(path);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return unwrap(json::parse(bytes)).at("overrides");
  }
  Json node(const std::string& id) { return unwrap(rt->db().get_node(id)).value().metadata; }
  std::set<std::string> derived_from(const std::string& node_id) {
    std::set<std::string> out;
    for (const auto& l : unwrap(rt->db().get_links(node_id, "derived_from")))
      if (l.src == node_id) out.insert(l.dst);
    return out;
  }
};

std::string hash_of(const char* domain) { return unwrap(RuntimeProfile::builtin(domain)).hash(); }

}  // namespace

TEST_SUITE("emergent_catalogue") {
  TEST_CASE("P14 3c: with the model method a sentence the model calls a message is still offered as a new action when it is shaped like a command") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    const std::string message_verdict = "{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.95}";
    // not shaped like a command: still a message (the model said so, the words agree)
    for (const char* text : {"Jak działa ta rozmowa?", "Napisz wiersz o tej rozmowie", "Dziękuję"}) {
      f.script(message_verdict);
      CHECK(f.route(text).at("kind") == "message");
    }
    // shaped like a command about an element, the model says message: the heuristic's offer stands
    f.script(message_verdict);
    const Json routed = f.route("Rozpisz tę wiadomość na kroki");
    CHECK(routed.at("method") == "model");
    REQUIRE(routed.at("kind") == "command");
    CHECK(routed.at("command").at("source") == "emergent");
    CHECK(routed.at("command").at("action") == "actions.define");
    CHECK(routed.at("command").at("emergent").at("kind") == "define_action");
    CHECK(routed.at("issues").empty());
    // in the chat: the proposal of the definition is the answer; nothing but the model's routing call was sent
    const std::size_t calls = f.transport->requests().size();
    f.script(message_verdict);
    const auto asked = f.input("Rozpisz tę wiadomość na kroki");
    REQUIRE(asked.handled);
    CHECK(f.proposal_of(asked).at("kind") == "define_action");
    CHECK(f.transport->requests().size() == calls + 1);
    // the setting is the user's: off, and the model's verdict is the end of it
    f.rt->config().set("emergent_actions", "off");
    f.script(message_verdict);
    CHECK(f.route("Rozpisz tę wiadomość na kroki").at("kind") == "message");
  }

  TEST_CASE("P17 1: an ordinary sentence in a conversation that has messages is never offered as a new action; real commands still are") {
    Fixture f;
    // statements, noun phrases and labels that mention a message or the conversation: none of them is an order (PL and EN)
    const std::vector<std::string> ordinary = {
        "First message of run 1",
        "Last message from run 2",
        "Second message of the test",
        "Message number 3 works",
        "The last message was long",
        "This message is fine",
        "Outline of this conversation is below",
        "List of messages in this conversation",
        "Summary of the last message",
        "Plan for this conversation",
        "Draft of the message is saved",
        "Review for this conversation is pending",
        "Edit of the last message was saved",
        "Note: this message is final",
        "You outline this message badly",
        "Run 3 message count",
        "Pierwsza wiadomość z przebiegu 1",
        "Ostatnia wiadomość z przebiegu 2",
        "Druga wiadomość testowa",
        "Wiadomość numer 3 działa",
        "Ta wiadomość jest za długa",
        "Rozpiska tej wiadomości jest niska",
        "Rozpiska tej rozmowy jest poniżej",
        "Uwaga: ta wiadomość jest ostateczna",
        "Poprawka tej wiadomości jest gotowa",
        "Uporządkowanie tej rozmowy jest w toku",
        "Pierwsza wiadomosc z przebiegu 1",
        "Zmiana tej wiadomosci jest zapisana",
    };
    for (const auto& text : ordinary) {
      const Json routed = f.route(text);
      INFO(text);
      CHECK(routed.at("kind") == "message");
      CHECK_FALSE(f.input(text).handled);
    }
    // the unknown-command offers stay: a verb of the language at the lead, the element as its object, no question, no statement
    for (const auto& text : std::vector<std::string>{"Rozpisz tę wiadomość na kroki", "Uporządkuj tę wiadomość", "Outline this conversation",
                                                       "Please outline this message", "Can you outline this message", "Tidy this message",
                                                       "Fix this message", "Mógłbyś rozpisać tę wiadomość", "Przeanalizuj tę wiadomość",
                                                       "Proszę rozpisać ostatnią wiadomość"}) {
      const Json routed = f.route(text);
      INFO(text);
      REQUIRE(routed.at("kind") == "command");
      CHECK(routed.at("command").at("source") == "emergent");
    }
    // a question is never an order, with or without the mark
    for (const auto& text : std::vector<std::string>{"Can you outline this message?", "Czy możesz rozpisać tę wiadomość?"})
      CHECK(f.route(text).at("kind") == "message");
    // a verb the list does not know is not guessed; the user adds it in the overlay without replacing the list
    CHECK(f.route("Frobnicate this message").at("kind") == "message");
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "intent_routing.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "intent_routing"},
                {"overrides", Json{{"emergent", Json{{"command_shape", Json{{"verbs_extra", Json{{"en", Json::array({"frobnicate"})}}}}}}}}}}
               .dump();
    CHECK(f.route("Frobnicate this message").at("kind") == "command");
    CHECK(f.route("Outline this conversation").at("kind") == "command");  // the shipped list stays
  }

  TEST_CASE("P14 3c: a failed model route falls back to the local method, the definition included, and says so") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    const Json routed = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(routed.at("kind") == "command");
    CHECK(routed.at("command").at("source") == "emergent");
    CHECK(routed.at("issues").at(0).at("code") == "model_route_failed");
  }

  TEST_CASE("a: a command no action understands is offered as a new action; accepting writes the overlays and runs it; undo takes it back") {
    Fixture f;
    const std::string shipped_actions = hash_of("actions"), shipped_routing = hash_of("intent_routing"), shipped_entry = hash_of("graph_entry");
    // not a command: a question, no element, a short thanks, an explicit chat feature
    for (const char* text : {"Jak działa ta rozmowa?", "Rozpisz to", "Dziękuję", "Co sądzisz o tej rozmowie z szefem?", "I think we should discuss this conversation",
                             "Write a poem about this conversation", "Napisz wiersz o tej rozmowie", "Wyjaśnij mi dokładnie, jak działa ta rozmowa"})
      CHECK(f.route(text).at("kind") == "message");
    const Json routed = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(routed.at("kind") == "command");
    CHECK(routed.at("command").at("source") == "emergent");
    CHECK(routed.at("command").at("action") == "actions.define");
    CHECK(routed.at("command").at("emergent").at("kind") == "define_action");

    const auto proposal = f.input("Rozpisz tę wiadomość na kroki");
    REQUIRE(proposal.handled);
    const Json p = f.proposal_of(proposal);
    CHECK(p.at("kind") == "define_action");
    CHECK(p.at("status") == "pending");
    CHECK(p.at("definition").at("id") == "rozpisz");
    CHECK(p.at("definition").at("args").at("instruction") == "Rozpisz tę wiadomość na kroki");
    CHECK(p.at("definition").at("args").at("requires") == Json::array({"has_text"}));
    CHECK(p.at("definition").at("args").at("phrases") == Json{{"pl", Json::array({"rozpisz"})}});
    CHECK(p.at("definition").at("overlay") == "profiles/actions.pack");
    CHECK(p.at("needs_confirmation").size() == 1);  // the preset rule: every definition is confirmed
    const std::string text = f.message(proposal.assistant_message_id).text;
    CHECK(text.find("Nie znam polecenia „Rozpisz”") != std::string::npos);
    CHECK(text.find("profiles/actions.pack") != std::string::npos);
    // nothing written, nothing sent
    CHECK(f.overlay("actions").empty());
    CHECK(f.transport->requests().empty());
    // provenance: a node with derived_from links to the messages it emerged from
    const std::string node = p.at("definition").at("node");
    REQUIRE_FALSE(node.empty());
    CHECK(f.node(node).at("status") == "proposed");
    const std::set<std::string> from = f.derived_from(node);
    CHECK(from.count(proposal.user_message_id) == 1);  // the command itself
    CHECK(from.count(f.second) == 1);                  // the message it was about
    CHECK(unwrap(f.rt->db().get_node(node))->kind == "proposal");

    // the yes: defined, then run on the element the words were about
    f.script("1. Check the tests. 2. Ship.");
    const auto done = f.input("tak");
    REQUIRE(done.handled);
    CHECK(f.overlay("actions").at("actions").at("rozpisz").at("kind") == "composite");
    const Json entry = f.overlay("actions").at("actions").at("rozpisz");
    CHECK(entry.at("steps").at(0).at("with").at("prompt") == "Rozpisz tę wiadomość na kroki");
    CHECK(entry.at("requires") == Json::array({"has_text"}));
    CHECK(entry.at("origin").at("kind") == "emergent");
    CHECK(entry.at("origin").at("node") == node);
    CHECK(entry.at("origin").at("conv_id") == f.conv_id);
    CHECK(f.overlay("intent_routing").at("heuristic").at("phrases").at("rozpisz") == Json{{"pl", Json::array({"rozpisz"})}});
    CHECK(f.node(node).at("status") == "accepted");
    CHECK(f.message(done.assistant_message_id).text.find("1. Check the tests") != std::string::npos);
    REQUIRE(f.transport->requests().size() == 1);
    const Json sent = f.body(0);
    CHECK(sent.at("messages").back() == Json{{"role", "user"}, {"content", "Rozpisz tę wiadomość na kroki"}});
    CHECK(sent.at("messages").at(sent.at("messages").size() - 2) == Json{{"role", "user"}, {"content", "Agreed, Friday works if the tests pass."}});
    // the shipped data is untouched
    CHECK(hash_of("actions") == shipped_actions);
    CHECK(hash_of("intent_routing") == shipped_routing);
    CHECK(hash_of("graph_entry") == shipped_entry);

    // from now on it is an ordinary action of the lexicon
    const Json again = f.route("Rozpisz ostatnią wiadomość");
    CHECK(again.at("command").at("source") == "lexicon");
    CHECK(again.at("command").at("action") == "rozpisz");
    CHECK(again.at("command").at("feasible") == true);

    // undo: out of the overlay, with its phrases; the node remembers
    const auto undo = f.input("usuń akcję rozpisz");
    REQUIRE(undo.handled);
    CHECK(f.proposal_of(undo).at("action") == "actions.remove");
    f.input("tak");
    CHECK(f.overlay("actions").empty());
    CHECK(f.overlay("intent_routing").empty());
    CHECK(f.node(node).at("status") == "undone");
    CHECK(f.route("Rozpisz ostatnią wiadomość").at("command").at("source") == "emergent");  // unknown again
  }

  TEST_CASE("a: declining leaves nothing; off switches it off; the name is taken from the user's word and stays unique") {
    Fixture f;
    const auto proposal = f.input("Uporządkuj tę wiadomość");
    REQUIRE(proposal.handled);
    const std::string node = f.proposal_of(proposal).at("definition").at("node");
    f.input("nie");
    CHECK(f.overlay("actions").empty());
    CHECK(f.node(node).at("status") == "rejected");
    CHECK(f.message(proposal.assistant_message_id).metadata.at("action_proposal").at("status") == "rejected");
    // a name that is taken gets a number: the word "summarise" is a shipped action, so the user's own word must differ
    const Json en = f.route("Outline this conversation");
    CHECK(en.at("command").at("emergent").at("id") == "outline");
    CHECK(en.at("command").at("emergent").at("args").at("phrases") == Json{{"en", Json::array({"outline"})}});
    // off: the catalogue never grows from conversations
    f.rt->config().set("emergent_actions", "off");
    CHECK(f.route("Outline this conversation").at("kind") == "message");
    CHECK_FALSE(f.input("Uporządkuj tę wiadomość").handled);
    // an unknown preset is off as well, not a guess
    f.rt->config().set("emergent_actions", "whatever");
    CHECK(f.route("Outline this conversation").at("kind") == "message");
    f.rt->config().set("emergent_actions", "propose");
    CHECK(f.route("Outline this conversation").at("kind") == "command");
  }

  TEST_CASE("b: the same chain, each run on the result of the one before, N times in a row, is offered as one composite") {
    Fixture f;
    f.rt->config().set("intent_routing_run", "always");
    f.rt->config().set("action_result_target", "conversation_message");
    for (int round = 0; round < 3; ++round) {
      f.script("A short summary " + std::to_string(round));
      const auto summary = f.input("Summarise the last message");
      REQUIRE(summary.handled);
      CHECK(summary.chunk.at("type") == "action_result");
      CHECK_FALSE(summary.result.at("routed").contains("follow_up"));
      f.script("Eine kurze Zusammenfassung " + std::to_string(round));
      const auto translated = f.input("Translate the last message into German");
      REQUIRE(translated.handled);
      if (round < 2) {
        CHECK_FALSE(translated.result.at("routed").contains("follow_up"));  // not yet: N is 3
      } else {
        REQUIRE(translated.result.at("routed").contains("follow_up"));
      }
    }
    const auto msgs = unwrap(f.rt->db().get_msgs(f.conv_id));
    const Message offer = msgs.back();
    REQUIRE(offer.metadata.contains("action_proposal"));
    const Json p = offer.metadata.at("action_proposal");
    CHECK(p.at("kind") == "define_sequence");
    CHECK(p.at("definition").at("id") == "summarise_then_translate");
    CHECK(p.at("definition").at("args").at("steps") == Json::array({Json{{"action", "summarise"}}, Json{{"action", "translate"}}}));
    CHECK(offer.text.find("3 times") != std::string::npos);
    CHECK(f.overlay("actions").empty());  // offered, not made
    const std::string node = p.at("definition").at("node");
    CHECK(f.derived_from(node).size() >= 3);  // the commands that repeat

    // accepted: one action; running it is one run with both model calls, each result feeding the next
    const auto yes = f.input("yes");
    REQUIRE(yes.handled);
    const Json entry = f.overlay("actions").at("actions").at("summarise_then_translate");
    CHECK(entry.at("steps") == Json::array({Json{{"action", "summarise"}}, Json{{"action", "translate"}}}));
    CHECK(entry.at("requires") == Json::array({"has_text"}));
    CHECK(entry.at("origin").at("node") == node);
    CHECK(f.node(node).at("status") == "accepted");
    const std::size_t before = f.transport->requests().size();
    f.script("Short.");
    f.script("Kurz.");
    const Json run = unwrap(f.rt->actions().run(Json{{"action", "summarise_then_translate"}, {"element", f.first}, {"args", Json{{"target_language", "German"}}}, {"ref", "test"}}));
    INFO(run.value("error", Json()).dump());
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == before + 2);
    CHECK(f.body(before + 1).at("messages").at(1) == Json{{"role", "user"}, {"content", "Short."}});
    CHECK(f.body(before + 1).at("messages").back().at("content") == "Translate the text above into German.");
    // not offered twice: the same chain again, a fourth time, asks nothing more
    f.script("Another summary.");
    f.input("Summarise the last message");
    f.script("Noch eine.");
    const auto fourth = f.input("Translate the last message into German");
    CHECK_FALSE(fourth.result.at("routed").contains("follow_up"));
    // undo
    f.input("remove action summarise_then_translate");
    f.input("yes");
    CHECK(f.overlay("actions").empty());
    CHECK(f.node(node).at("status") == "undone");
  }

  TEST_CASE("b: a chain that is not a pipeline, or is not repeated enough, or is switched off, offers nothing") {
    Fixture f;
    f.rt->config().set("intent_routing_run", "always");
    // two summaries of the same element are not a chain
    for (int i = 0; i < 4; ++i) {
      f.script("S");
      const auto out = f.input("Summarise this conversation");
      CHECK_FALSE(out.result.at("routed").contains("follow_up"));
    }
    // the count is data: with N = 2 the second round is enough
    f.rt->config().set("action_result_target", "conversation_message");
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "intent_routing.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "intent_routing"}, {"overrides", Json{{"emergent", Json{{"sequences", Json{{"repeat", 2}}}}}}}}.dump();
    bool offered = false;
    for (int round = 0; round < 2; ++round) {
      f.script("S");
      f.input("Summarise the last message");
      f.script("T");
      const auto t = f.input("Translate the last message into German");
      offered = t.result.at("routed").contains("follow_up");
    }
    CHECK(offered);
  }

  TEST_CASE("c: a user-defined action used on several classes that share traits is offered a group; accepting writes the overlay of the graph entry") {
    Fixture f;
    f.rt->config().set("intent_routing_run", "always");
    unwrap(loom::test::invoke_confirmed(*f.rt, "actions.define", Json{{"id", "rozpisz"}, {"instruction", "Rozpisz na kroki"}, {"phrases", Json{{"pl", Json::array({"rozpisz"})}}}, {"lang", "pl"}, {"confirmed", true}}));
    const std::string shipped_entry = hash_of("graph_entry");
    f.script("Kroki 1");
    const auto on_message = f.input("Rozpisz tę wiadomość");
    REQUIRE(on_message.handled);
    CHECK_FALSE(on_message.result.at("routed").contains("follow_up"));  // one class so far
    f.script("Kroki 2");
    const auto on_conversation = f.input("Rozpisz tę rozmowę");
    REQUIRE(on_conversation.result.at("routed").contains("follow_up"));
    const Json p = unwrap(f.rt->db().get_msg(on_conversation.result.at("routed").at("follow_up").at("message_id").get<std::string>()))->metadata.at("action_proposal");
    CHECK(p.at("kind") == "define_group");
    CHECK(p.at("definition").at("id") == "rozpisz_targets");
    const Json traits = p.at("definition").at("args").at("traits");
    CHECK(std::find(traits.begin(), traits.end(), "has_text") != traits.end());
    CHECK(p.at("definition").at("args").at("label").at("pl") == "Na czym działa „rozpisz”");
    CHECK(f.overlay("graph_entry").empty());  // offered, not made
    CHECK(hash_of("graph_entry") == shipped_entry);
    const std::string node = p.at("definition").at("node");
    f.input("tak");
    const Json group = f.overlay("graph_entry").at("ontology").at("groups").at("rozpisz_targets");
    CHECK(group.at("traits") == traits);
    CHECK(group.at("element_match") == "all");
    CHECK(group.at("origin").at("kind") == "emergent");
    CHECK(f.node(node).at("status") == "accepted");
    // the group is a name that rules can use; the shipped data does not have it
    const auto effective = unwrap(RuntimeProfile::load("graph_entry", f.td.path()));
    CHECK(effective.values().at("ontology").at("groups").contains("rozpisz_targets"));
    CHECK_FALSE(unwrap(RuntimeProfile::builtin("graph_entry")).values().at("ontology").at("groups").contains("rozpisz_targets"));
    CHECK(hash_of("graph_entry") == shipped_entry);
    // not offered again; and undone on request
    f.script("Kroki 3");
    CHECK_FALSE(f.input("Rozpisz ostatnią wiadomość").result.at("routed").contains("follow_up"));
    f.input("usuń grupę rozpisz_targets");
    f.input("tak");
    CHECK(f.overlay("graph_entry").empty());
    CHECK(f.node(node).at("status") == "undone");
    // shipped actions and groups cannot be removed through these doors
    CHECK(unwrap(loom::test::invoke_confirmed(*f.rt, "actions.remove", Json{{"id", "summarise"}, {"confirmed", true}})).at("status") == "failed");
    CHECK(unwrap(loom::test::invoke_confirmed(*f.rt, "groups.remove", Json{{"id", "rich_content"}, {"confirmed", true}})).at("status") == "failed");
    CHECK(unwrap(loom::test::invoke_confirmed(*f.rt, "actions.define", Json{{"id", "summarise"}, {"instruction", "x"}, {"confirmed", true}})).at("status") == "failed");
  }

  TEST_CASE("the writers through the API: dry run, confirmation, names, an action another one of yours uses") {
    Fixture f;
    const Json waiting = unwrap(f.rt->actions().invoke("actions.define", Json{{"id", "stepper"}, {"instruction", "Do it in steps"}}));
    CHECK(waiting.at("status") == "awaiting_confirmation");  // gated like any command
    CHECK(f.overlay("actions").empty());
    CHECK(unwrap(actions_define(f.td.path(), Json{{"id", "stepper"}, {"instruction", "Do it in steps"}, {"dry_run", true}})).at("written") == false);
    CHECK(f.overlay("actions").empty());
    CHECK_FALSE(actions_define(f.td.path(), Json{{"id", "Bad Name"}, {"instruction", "x"}}));
    CHECK_FALSE(actions_define(f.td.path(), Json{{"id", "both"}, {"instruction", "x"}, {"steps", Json::array({Json{{"action", "summarise"}}})}}));
    CHECK_FALSE(actions_define(f.td.path(), Json{{"id", "chain"}, {"steps", Json::array({Json{{"action", "describe_image"}}})}}));  // describe_image is only declared
    unwrap(actions_define(f.td.path(), Json{{"id", "stepper"}, {"instruction", "Do it in steps"}}));
    unwrap(actions_define(f.td.path(), Json{{"id", "chained"}, {"steps", Json::array({Json{{"action", "stepper"}}, Json{{"action", "translate"}}})}}));
    const auto in_use = actions_remove(f.td.path(), Json{{"id", "stepper"}});
    REQUIRE_FALSE(in_use);
    CHECK(in_use.error().message.find("chained") != std::string::npos);
    unwrap(actions_remove(f.td.path(), Json{{"id", "chained"}}));
    unwrap(actions_remove(f.td.path(), Json{{"id", "stepper"}}));
    CHECK(f.overlay("actions").empty());
    // an entry written by hand is not replaced from a conversation
    std::filesystem::create_directories(f.td.path() / "profiles");
    Json by_hand = unwrap(actions_define(f.td.path(), Json{{"id", "tmp"}, {"instruction", "x"}, {"dry_run", true}})).at("entry");
    by_hand["id"] = "mine";
    by_hand.erase("origin");
    REQUIRE(OverlayStore(f.td.path()).put("actions", {"actions", "mine"}, by_hand));
    CHECK_FALSE(actions_define(f.td.path(), Json{{"id", "mine"}, {"instruction", "y"}}));
  }
}
