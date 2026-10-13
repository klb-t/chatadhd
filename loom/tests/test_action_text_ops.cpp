// P11 3: elaborate, critique, combine, rephrase and explain_simply are composites as pure data (steps, prompts in Polish and
// English, parameters in runtime/actions.pack; phrases in runtime/intent_routing.pack). They run through use_model on the
// existing adapters; the bytes sent are pinned. combine takes several elements as one input.
#include <doctest/doctest.h>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

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
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    first = add("user", "We should ship on Friday.");
    second = add("assistant", "Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void chat(const std::string& content) {
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json body(std::size_t i) { return unwrap(json::parse(transport->requests().at(i).body)); }
  Json run(const std::string& action, const Json& element, const Json& args = Json::object(), const Json& context = Json::object()) {
    return unwrap(rt->actions().run(Json{{"action", action}, {"element", element}, {"args", args}, {"context", context}, {"ref", "test"}}));
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
};

std::string content_of(const Json& body, int i) { return body.at("messages").at(static_cast<std::size_t>(i)).at("content"); }
bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

}  // namespace

TEST_SUITE("action_text_ops") {
  TEST_CASE("elaborate: the focus is a parameter; the bytes sent are pinned in English and Polish") {
    Fixture f;
    f.chat("Friday, because the release train leaves then; the tests gate it.");
    const Json run = f.run("elaborate", f.first, Json{{"focus", "the reasons"}});
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 1);
    const Json body = f.body(0);
    CHECK(body.at("temperature") == 0.6);
    REQUIRE(body.at("messages").size() == 3);
    CHECK(contains(content_of(body, 0), "You develop texts"));
    CHECK(content_of(body, 1) == "We should ship on Friday.");
    CHECK(content_of(body, 2) == "Elaborate on the text above. Dwell on: the reasons.");
    f.chat("Rozwinięcie.");
    f.run("elaborate", f.first, Json::object(), Json{{"lang", "pl"}});
    CHECK(contains(content_of(f.body(1), 0), "Rozwijasz teksty"));
    CHECK(content_of(f.body(1), 2) == "Rozwiń powyższy tekst. Skup się na: tym, co najbardziej wymaga rozwinięcia.");
    const auto expresses = unwrap(f.rt->db().get_links(run.at("result").at("produced").at("items").at(0).at("form").get<std::string>(), "expresses"));
    CHECK(expresses.at(0).metadata.at("content_op") == "expand");
  }

  TEST_CASE("critique, rephrase and explain_simply: one model call each, their own system prompt and parameter") {
    Fixture f;
    f.chat("Strengths: clear. Weaknesses: no owner.");
    REQUIRE(f.run("critique", f.second, Json{{"criteria", "the plan"}}).at("status") == "completed");
    CHECK(contains(content_of(f.body(0), 0), "fair, rigorous critic"));
    CHECK(content_of(f.body(0), 2) == "Critique the text above. Judge it by: the plan.");
    CHECK(f.body(0).at("temperature") == 0.3);

    f.chat("We ought to deliver on Friday.");
    REQUIRE(f.run("rephrase", f.first, Json{{"tone", "formal"}, {"level", "legal"}}).at("status") == "completed");
    CHECK(contains(content_of(f.body(1), 0), "You rewrite texts"));
    CHECK(content_of(f.body(1), 2) == "Rewrite the text above. Tone: formal. Level: legal.");

    f.chat("We will send it out on Friday. That is the plan.");
    REQUIRE(f.run("explain_simply", f.first, Json{{"audience", "a ten-year-old"}}).at("status") == "completed");
    CHECK(contains(content_of(f.body(2), 0), "explain things simply"));
    CHECK(content_of(f.body(2), 2) == "Explain the text above simply, for this audience: a ten-year-old.");

    // Polish prompts with their defaults
    f.chat("x");
    f.run("explain_simply", f.first, Json::object(), Json{{"lang", "pl"}});
    CHECK(content_of(f.body(3), 2) == "Wyjaśnij prosto powyższy tekst dla tego odbiorcy: ktoś, kto dopiero poznaje temat.");
    f.chat("y");
    f.run("rephrase", f.first, Json::object(), Json{{"lang", "pl"}});
    CHECK(content_of(f.body(4), 2) == "Przeredaguj powyższy tekst. Ton: prosty i neutralny. Poziom: język codzienny.");
    f.chat("z");
    f.run("critique", f.first, Json::object(), Json{{"lang", "pl"}});
    CHECK(content_of(f.body(5), 2) == "Oceń krytycznie powyższy tekst. Kryteria: mocne strony, słabe strony, wiarygodność i jasność.");
  }

  TEST_CASE("combine takes several elements as one input: their contents one after another, the result derived from each") {
    Fixture f;
    f.chat("Ship on Friday if the tests pass.");
    const Json run = f.run("combine", Json::array({f.first, f.second}), Json{{"shape", "a one-line summary"}});
    REQUIRE(run.at("status") == "completed");
    const Json body = f.body(0);
    REQUIRE(body.at("messages").size() == 4);
    CHECK(contains(content_of(body, 0), "combine several texts"));
    CHECK(content_of(body, 1) == "We should ship on Friday.");
    CHECK(content_of(body, 2) == "Friday works if the tests pass.");
    CHECK(content_of(body, 3) == "Combine the texts above into one: a one-line summary.");
    const Json produced = run.at("result").at("produced");
    const std::string form = produced.at("items").at(0).at("form");
    const auto derived = unwrap(f.rt->db().get_links(form, "derived_from"));
    std::set<std::string> from;
    for (const auto& l : derived) from.insert(l.dst);
    CHECK(from == std::set<std::string>{f.first, f.second});
    CHECK(produced.at("sources").size() == 2);
    // one id is not a group; none is an error that says so
    f.chat("single");
    CHECK(f.run("combine", Json::array({f.first})).at("status") == "completed");
    CHECK(f.body(1).at("messages").size() == 3);
    CHECK_FALSE(f.rt->actions().run(Json{{"action", "combine"}, {"element", Json::array()}}));
    // a group of unlike elements is fitted by what all of them are: a conversation and a message are both text
    f.chat("mixed");
    CHECK(f.run("combine", Json::array({f.conv_id, f.first})).at("status") == "completed");
    // an element that does not exist is said, nothing is sent
    const std::size_t sent = f.transport->requests().size();
    CHECK_FALSE(f.rt->actions().run(Json{{"action", "combine"}, {"element", Json::array({f.first, "m_00000000dead"})}}));
    CHECK(f.transport->requests().size() == sent);
  }

  TEST_CASE("from the conversation window: the phrases and the arguments they carry, in Polish and English") {
    Fixture f;
    for (const auto& [text, action] : std::vector<std::pair<std::string, std::string>>{
             {"Rozwiń tę wiadomość", "elaborate"}, {"Elaborate on the last message", "elaborate"},
             {"Skrytykuj ostatnią wiadomość", "critique"}, {"Oceń tę rozmowę", "critique"}, {"Critique this conversation", "critique"},
             {"Przeredaguj tę wiadomość", "rephrase"}, {"Rephrase the last message", "rephrase"}, {"Napisz inaczej tę wiadomość", "rephrase"},
             {"Wytłumacz prościej tę rozmowę", "explain_simply"}, {"Explain this conversation simply", "explain_simply"},
             {"Uprość tę wiadomość", "explain_simply"}}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(r.at("command").at("action") == action);
      CHECK(r.at("command").at("feasible") == true);
    }
    CHECK(f.route("Przeredaguj tę wiadomość w formalnym tonie").at("command").at("args") == Json{{"tone", "formalnym"}});
    CHECK(f.route("Rewrite the last message in a warm tone").at("command").at("args") == Json{{"tone", "warm"}});
    CHECK(f.route("Przeredaguj tę wiadomość na poziomie akademickim").at("command").at("args") == Json{{"level", "akademickim"}});
    CHECK(f.route("Wytłumacz prościej tę rozmowę dla początkujących").at("command").at("args") == Json{{"audience", "początkujących"}});
    CHECK(f.route("Explain this conversation simply for a beginner").at("command").at("args") == Json{{"audience", "beginner"}});
    CHECK(f.route("Explain this conversation simply like I'm five").at("command").at("args") == Json{{"audience", "like I'm five"}});
    // a question is still a message
    CHECK(f.route("How do I explain this to my boss?").at("kind") == "message");
    CHECK(f.route("Wytłumacz mi, jak działa TCP").at("kind") == "message");
    // combine from the words: every id, in the order said
    const Json combine = f.route("Połącz " + f.first + " i " + f.second + " w jeden");
    REQUIRE(combine.at("kind") == "command");
    CHECK(combine.at("command").at("action") == "combine");
    CHECK(combine.at("command").at("element").at("members") == Json::array({f.first, f.second}));
    CHECK(combine.at("command").at("feasible") == true);

    // the whole way: proposal, yes, bytes
    const auto proposal = f.input("Combine " + f.first + " and " + f.second);
    REQUIRE(proposal.handled);
    CHECK(unwrap(f.rt->db().get_msg(proposal.assistant_message_id))->text.find("2 elements") != std::string::npos);
    f.chat("Ship Friday if the tests pass.");
    REQUIRE(f.input("yes").handled);
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(content_of(f.body(0), 3) == "Combine the texts above into one: a coherent text.");
    // the parameters are shown in the proposal
    CHECK(unwrap(f.rt->db().get_msg(proposal.assistant_message_id))->text.find("shape (a coherent text)") != std::string::npos);
  }
}
