// R54 / GRAF_HUB_KOD_SILNIK §8: every chat input goes through input.route_intent: message or command; a command
// is answered in the conversation as a proposal and runs after the user's yes.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
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
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add("user", "We should ship on Friday.");
    add("assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n, const std::string& content = "A short summary.") {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json route(const std::string& text, const std::string& conv = "<current>", const Json& extra = Json::object()) {
    Json req = extra;
    req["text"] = text;
    req["conv_id"] = conv == "<current>" ? conv_id : conv;
    return unwrap(chat_route::route(*rt, req));
  }
  // The chat window's entry, as the C API calls it.
  chat_route::Outcome input(const std::string& text, const Json& request = Json::object()) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    Json req = request;
    req["message"] = text;
    return unwrap(chat_route::handle(*rt, text, opts, req, nullptr));
  }
  Json last_message(const std::string& role = "") {
    auto msgs = unwrap(rt->db().get_msgs(conv_id));
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
      if (role.empty() || it->role == role) return it->to_json();
    return Json();
  }
  std::size_t message_count() { return unwrap(rt->db().get_msgs(conv_id)).size(); }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
};

std::string action_of(const Json& routed) { return routed.at("command").at("action"); }

}  // namespace

TEST_SUITE("chat_route") {
  TEST_CASE("heuristic, English: commands name an action AND an element; everything else is a message") {
    Fixture f;
    for (const char* text : {"Summarise this conversation", "please summarize the conversation", "Could you summarise this chat?",
                             "tldr of the thread", "Summarise the last message"}) {
      INFO(text);
      const Json r = f.route(text);
      CHECK(r.at("kind") == "command");
      CHECK(r.at("method") == "heuristic");
      CHECK(r.at("lang") == "en");
      CHECK(action_of(r) == "summarise");
      CHECK(r.at("command").at("source") == "lexicon");
      CHECK(r.at("command").at("feasible") == true);
    }
    CHECK(f.route("Summarise this conversation").at("command").at("element").at("class") == "conversation");
    CHECK(f.route("Summarise the last message").at("command").at("element").at("class") == "message");
    for (const char* text : {"How does TCP work?", "Can you explain recursion to me?", "Summarise the differences between TCP and UDP",
                             "I think we should summarise this later when I am back from the long holiday",  // the verb is far from the start
                             "What is the weather like in Gdansk?", "Let's set the table for six"}) {
      INFO(text);
      CHECK(f.route(text).at("kind") == "message");
    }
  }

  TEST_CASE("heuristic, Polish: the same, with inflection and without diacritics") {
    Fixture f;
    for (const char* text : {"Streść tę rozmowę", "Podsumuj proszę tę rozmowę", "streszcz ten czat", "Streść tę rozmowe", "STREŚĆ TĘ ROZMOWĘ"}) {
      INFO(text);
      const Json r = f.route(text);
      CHECK(r.at("kind") == "command");
      CHECK(r.at("lang") == "pl");
      CHECK(action_of(r) == "summarise");
    }
    CHECK(f.route("Podsumuj wiadomość").at("command").at("element").at("class") == "message");
    for (const char* text : {"Jak działa protokół TCP?", "Streść mi, jak działa TCP", "Co sądzisz o tej rozmowie z szefem?", "Dziękuję"}) {
      INFO(text);
      CHECK(f.route(text).at("kind") == "message");
    }
  }

  TEST_CASE("any level of abstraction: make an image of this conversation, and set a setting by its key") {
    Fixture f;
    const Json image = f.route("zrób z tej rozmowy obraz");
    CHECK(image.at("kind") == "command");
    CHECK(action_of(image) == "depict");
    CHECK(image.at("command").at("element").at("class") == "conversation");
    // depict is implemented, but it paints with an image model the user has not chosen: said, not guessed.
    CHECK(image.at("command").at("feasible") == false);
    CHECK(image.at("command").at("reason").at("code") == "missing_args");
    CHECK(image.at("command").at("reason").at("args") == Json::array({"model", "provider"}));
    CHECK(image.at("command").at("reason").at("settings") == Json{{"model", "image_model"}, {"provider", "image_provider"}});
    f.rt->config().set("image_model", "image-1");
    f.rt->config().set("image_provider", "img");
    const Json ready = f.route("zrób z tej rozmowy obraz");
    CHECK(ready.at("command").at("feasible") == true);
    CHECK(ready.at("command").at("runnable") == true);
    CHECK(ready.at("command").at("plan").at("cost").at("model_calls") == 2);  // the image prompt, then the image
    CHECK(f.route("draw this conversation as an image").at("command").at("action") == "depict");
    const Json set = f.route("ustaw semantic_retry_mode=automatic");
    CHECK(set.at("kind") == "command");
    CHECK(action_of(set) == "settings.set");
    CHECK(set.at("command").at("source") == "pattern");
    CHECK(set.at("command").at("args") == Json{{"key", "semantic_retry_mode"}, {"value", "automatic"}});
    CHECK(set.at("command").at("element").at("class") == "settings_field");
    CHECK(set.at("command").at("runnable") == true);
    CHECK(f.route("semantic_retry_mode = automatic").at("command").at("args").at("value") == "automatic");
    CHECK(f.route("set max_tokens to 2048").at("command").at("args") == Json{{"key", "max_tokens"}, {"value", "2048"}});
    CHECK(f.route("show max_tokens").at("command").at("action") == "settings.get");
    // x = 5 is not a command when x is not a setting
    CHECK(f.route("x = 5").at("kind") == "message");
    CHECK(f.route("set the table = 4 plates").at("kind") == "message");
  }

  TEST_CASE("explicit syntax: /action, /set, /run with an element, ids anywhere in the text") {
    Fixture f;
    CHECK(action_of(f.route("/summarise")) == "summarise");
    CHECK(f.route("/summarise").at("command").at("source") == "explicit");
    CHECK(f.route("/summarise").at("command").at("element").at("id") == f.conv_id);  // the default element is this conversation
    const std::string mid = f.add("user", "Plan B is to ship on Monday.");
    const Json on_message = f.route("/run summarise on " + mid);
    CHECK(action_of(on_message) == "summarise");
    CHECK(on_message.at("command").at("element").at("id") == mid);
    CHECK(f.route("/summarise " + mid).at("command").at("element").at("id") == mid);
    const Json set = f.route("/set max_tokens 2048");
    CHECK(set.at("command").at("args") == Json{{"key", "max_tokens"}, {"value", "2048"}});
    CHECK(f.route("/set max_tokens=1024").at("command").at("args") == Json{{"key", "max_tokens"}, {"value", "1024"}});
    CHECK(f.route("Summarise " + mid).at("command").at("element").at("id") == mid);
    // a path or an unknown word after the slash is not a command
    CHECK(f.route("/etc/hosts is where it lives").at("kind") == "message");
    CHECK(f.route("/nothing here").at("kind") == "message");
    // a command that cannot be carried out is still a command: the answer says why
    const Json missing = f.route("/set max_tokens");
    CHECK(missing.at("kind") == "command");
    CHECK(missing.at("command").at("feasible") == false);
    CHECK(missing.at("command").at("reason").at("code") == "missing_args");
    const Json unknown_id = f.route("/summarise m_000000000000");
    CHECK(unknown_id.at("command").at("reason").at("code") == "element_not_found");
  }

  TEST_CASE("a missing trait becomes a chain, not a refusal: the element is a recording") {
    Fixture f;
    const std::string audio = unwrap(f.rt->blobs().put("RIFF....WAVE", "audio/wav")).hash;
    const Json r = f.route("summarise " + audio);
    CHECK(r.at("kind") == "command");
    CHECK(r.at("command").at("chain") == Json::array({"transcribe", "summarise"}));
    CHECK(r.at("command").at("runnable") == true);  // transcribe is implemented (P12 4): both steps can run
  }

  TEST_CASE("no conversation, no 'this conversation'; routing off or an explicit chat feature makes every input a message") {
    Fixture f;
    CHECK(f.route("Summarise this conversation", "").at("kind") == "message");
    CHECK(f.route("Summarise this conversation", "<current>", Json{{"method", "off"}}).at("kind") == "message");
    f.rt->config().set("intent_routing_method", "off");
    CHECK(f.route("Summarise this conversation").at("method") == "off");
    CHECK_FALSE(f.input("Summarise this conversation").handled);
    f.rt->config().set("intent_routing_method", "heuristic");
    CHECK(f.input("Summarise this conversation").handled);
    // attachments, a re-send with the usage confirmation, web search ... are chat features
    for (const Json& request : {Json{{"attachments", Json::array({"/tmp/x.png"})}},
                                Json{{"usage_confirmation", Json{{"approved", true}}}}, Json{{"web_search", true}}}) {
      INFO(request.dump());
      CHECK_FALSE(f.input("Summarise this conversation", request).handled);
    }
    CHECK_FALSE(chat_route::route(*f.rt, Json{{"text", "hi"}, {"method", "sometimes"}}));  // an unknown method is an error, not a silent message
  }

  TEST_CASE("a message continues on the chat path with the request bytes unchanged (golden)") {
    Fixture f;
    f.rt->config().set("system_prompt", "Golden system prompt.");
    f.script(1, "ok");
    ChatOptions opts;
    opts.stream = false;
    // The routing step first, exactly as the chat window does it ...
    CHECK_FALSE(unwrap(chat_route::handle(*f.rt, "golden question", opts, Json{{"message", "golden question"}}, nullptr)).handled);
    // ... then the unchanged send.
    REQUIRE(f.rt->chat().send("golden question", opts));
    const auto requests = f.transport->requests();
    REQUIRE(requests.size() == 1);  // routing a message sent nothing and stored nothing
    const Json expected{
        {"method", "POST"},
        {"url", "https://api.test/chat/completions"},
        {"headers", Json::array({Json::array({"Authorization", "Bearer sk-test-key"}), Json::array({"Content-Type", "application/json"}),
                                 Json::array({"HTTP-Referer", "https://github.com/chatadhd"}), Json::array({"X-Title", "ChatADHD"})})},
        {"body", "{\"model\":\"test/model\",\"messages\":[{\"role\":\"system\",\"content\":\"Golden system prompt.\"},"
                 "{\"role\":\"user\",\"content\":\"golden question\"}],\"temperature\":0.7,\"max_tokens\":4096,"
                 "\"stream\":false,\"include_reasoning\":true}"},
        {"timeout_ms", 180000},
        {"stream", false}};
    // (a fresh conversation: the golden conversation of the chat-engine tests has no earlier turns)
    CHECK(loom::test::request_json(requests[0]).at("url") == expected.at("url"));
    CHECK(loom::test::request_json(requests[0]).at("headers") == expected.at("headers"));
    CHECK(loom::test::request_json(requests[0]).at("timeout_ms") == expected.at("timeout_ms"));
    const Json body = unwrap(json::parse(requests[0].body));
    CHECK(body.at("messages").back() == Json{{"role", "user"}, {"content", "golden question"}});
    CHECK(body.at("messages").front() == Json{{"role", "system"}, {"content", "Golden system prompt."}});

    // The same input through a conversation-less runtime is byte-identical to the pinned golden of the chat engine.
    Fixture g;
    g.rt->config().set("system_prompt", "Golden system prompt.");
    g.script(1, "ok");
    ChatOptions fresh;
    fresh.stream = false;
    fresh.conv_id = unwrap(g.rt->db().create_conv("golden")).id;
    CHECK_FALSE(unwrap(chat_route::handle(*g.rt, "golden question", fresh, Json{{"message", "golden question"}}, nullptr)).handled);
    REQUIRE(g.rt->chat().send("golden question", fresh));
    CHECK(loom::test::request_json(g.transport->requests().at(0)) == expected);
  }

  TEST_CASE("a command is answered in the conversation as a proposal; nothing runs and nothing is sent until the yes") {
    Fixture f;
    const std::size_t before = f.message_count();
    const auto out = f.input("Streść tę rozmowę");
    REQUIRE(out.handled);
    CHECK(out.chunk.at("type") == "action_proposal");
    CHECK(out.chunk.at("conv_id") == f.conv_id);
    CHECK(f.message_count() == before + 2);  // the user's words and the proposal
    CHECK(f.transport->requests().empty());
    const Json user = unwrap(f.rt->db().get_msg(out.user_message_id))->to_json();
    CHECK(user.at("text") == "Streść tę rozmowę");
    CHECK(user.at("metadata").at("routed") == "command");
    const Json asst = unwrap(f.rt->db().get_msg(out.assistant_message_id))->to_json();
    CHECK(asst.at("role") == "assistant");
    const Json proposal = asst.at("metadata").at("action_proposal");
    CHECK(proposal.at("schema") == "loom.action_proposal/1");
    CHECK(proposal.at("status") == "pending");
    CHECK(proposal.at("action") == "summarise");
    CHECK(proposal.at("lang") == "pl");
    CHECK(proposal.at("run_id").get<std::string>().rfind("ar_", 0) == 0);
    CHECK(proposal.at("receipt_id").get<std::string>().rfind("arr_", 0) == 0);
    CHECK(proposal.at("cost").at("model_calls") == 1);
    CHECK(proposal.at("cost").at("input_bytes").get<int>() > 0);
    CHECK(proposal.at("effects").at("sends_off_device") == true);
    CHECK(proposal.at("element").at("class") == "conversation");
    // what it says, in Polish, from data: the plan, the estimate, how to answer
    const std::string text = asst.at("text");
    CHECK(text.find("Rozumiem to jako polecenie") != std::string::npos);
    CHECK(text.find("Wykonam:") != std::string::npos);
    CHECK(text.find("Szacunek:") != std::string::npos);
    CHECK(text.find("„tak”") != std::string::npos);
    // the run exists, waits for the user's go, and is the one the proposal points at
    const Json run = unwrap(f.rt->actions().status(Json{{"run_id", proposal.at("run_id")}}));
    CHECK(run.at("status") == "proposed");
    CHECK(run.at("request").at("context").at("origin") == "chat");
    CHECK(run.at("request").at("context").at("message_id") == out.user_message_id);
    // the semantic worker does not pick the proposal up as the user's content
    CHECK(asst.at("semantic_status") == "done");
    // English input gets English words
    const auto en = f.input("Summarise this conversation");
    CHECK(unwrap(f.rt->db().get_msg(en.assistant_message_id))->text.find("I read this as a command") != std::string::npos);
  }

  TEST_CASE("yes runs the proposal through the interpreter, with provenance; the transcript leaves out the command traffic") {
    Fixture f;
    const auto proposed = f.input("Summarise this conversation");
    REQUIRE(proposed.handled);
    f.script(1, "Ship Friday if tests pass.");
    const auto done = f.input("yes");
    REQUIRE(done.handled);
    CHECK(done.chunk.at("type") == "action_result");
    CHECK(done.chunk.at("proposal_status") == "executed");
    CHECK(done.chunk.at("text").get<std::string>().find("Ship Friday if tests pass.") != std::string::npos);
    const Json result = unwrap(f.rt->db().get_msg(done.assistant_message_id))->to_json();
    CHECK(result.at("metadata").at("action_run").at("status") == "completed");
    const std::string run_id = result.at("metadata").at("action_run").at("run_id");
    const Json run = unwrap(f.rt->actions().status(Json{{"run_id", run_id}}));
    CHECK(run.at("status") == "completed");
    CHECK(run.at("confirmations").back().at("ref").get<std::string>().rfind("chat:", 0) == 0);
    CHECK(run.at("steps").at(0).at("variants").at(0).at("provenance").at(0).at("schema") == "loom.model_use/1");
    // the proposal message now says what became of it
    CHECK(unwrap(f.rt->db().get_msg(proposed.assistant_message_id))->metadata.at("action_proposal").at("status") == "executed");
    // exactly one model call, and the text sent is the conversation, not the command chatter
    REQUIRE(f.transport->requests().size() == 1);
    const std::string sent = f.transport->requests()[0].body;
    CHECK(sent.find("We should ship on Friday.") != std::string::npos);
    CHECK(sent.find("Summarise this conversation") == std::string::npos);
    CHECK(sent.find("I read this as a command") == std::string::npos);
    // a second yes has nothing to answer: it is an ordinary message
    CHECK_FALSE(f.input("yes").handled);
  }

  TEST_CASE("no drops the proposal and runs nothing; any other reply leaves it open and goes on as a message") {
    Fixture f;
    const auto proposed = f.input("Streść tę rozmowę");
    const auto no = f.input("nie");
    REQUIRE(no.handled);
    CHECK(no.chunk.at("proposal_status") == "rejected");
    CHECK(unwrap(f.rt->actions().status(Json{{"run_id", unwrap(f.rt->db().get_msg(proposed.assistant_message_id))->metadata.at("action_proposal").at("run_id")}})).at("status") == "rejected");
    CHECK(f.transport->requests().empty());
    // the yes of a stale proposal does not run it silently: only the last thing said can be answered
    const auto second = f.input("Summarise this conversation");
    REQUIRE(second.handled);
    CHECK_FALSE(f.input("What does that mean for Friday?").handled);  // a message: the proposal stays open
    f.add("user", "What does that mean for Friday?");                  // ... the chat path answers it
    f.add("assistant", "That the tests must pass by Thursday.");
    CHECK_FALSE(f.input("yes").handled);                               // ... and the proposal is no longer the last thing said
    CHECK(unwrap(f.rt->db().get_msg(second.assistant_message_id))->metadata.at("action_proposal").at("status") == "pending");
    // it can still be confirmed explicitly, with its receipt
    const Json proposal = unwrap(f.rt->db().get_msg(second.assistant_message_id))->metadata.at("action_proposal");
    f.script(1);
    const Json run = unwrap(f.rt->actions().confirm(Json{{"run_id", proposal.at("run_id")}, {"approved", true}, {"receipt_id", proposal.at("receipt_id")}, {"ref", "web"}}));
    CHECK(run.at("status") == "completed");
  }

  TEST_CASE("a setting by chat: proposal, yes, changed; the value is the user's, the rule on credentials is the engine's") {
    Fixture f;
    const auto proposed = f.input("ustaw semantic_retry_mode=automatic");
    REQUIRE(proposed.handled);
    CHECK(proposed.chunk.at("proposal").at("action") == "settings.set");
    CHECK(f.rt->config().get("semantic_retry_mode") != "automatic");
    const auto done = f.input("tak");
    REQUIRE(done.handled);
    CHECK(f.rt->config().get("semantic_retry_mode") == "automatic");
    CHECK(done.chunk.at("text").get<std::string>().find("Gotowe") != std::string::npos);
    // a credential typed into the conversation is refused with the rule's reason; the proposal is not offered
    const auto secret = f.input("set api_key to sk-evil");
    REQUIRE(secret.handled);
    CHECK(secret.chunk.at("proposal").at("status") == "denied");
    CHECK(secret.chunk.at("text").get<std::string>().find("credential") != std::string::npos);
    CHECK(f.rt->secrets().get_string("api_key") == "sk-test-key");
    for (const auto& e : unwrap(f.rt->event_log().query(EventQuery{0, std::nullopt, std::nullopt, 100000})))
      if (e.type.rfind("action.", 0) == 0) CHECK(e.payload.dump().find("sk-evil") == std::string::npos);
  }

  TEST_CASE("auto-run with the result as a conversation message: the proposal comes first, then the result (P11)") {
    Fixture f;
    f.rt->config().set("intent_routing_run", "always");
    f.rt->config().set("action_result_target", "conversation_message");
    f.script(1);
    const auto out = f.input("Streść tę rozmowę");
    REQUIRE(out.handled);
    CHECK(out.chunk.at("type") == "action_result");
    int command = -1, proposal = -1, result = -1;
    const auto msgs = unwrap(f.rt->db().get_msgs(f.conv_id));
    for (std::size_t i = 0; i < msgs.size(); ++i) {
      const Json& m = msgs[i].metadata;
      if (m.is_object() && m.value("routed", "") == "command") command = static_cast<int>(i);
      if (m.is_object() && m.contains("action_proposal")) proposal = static_cast<int>(i);
      if (m.is_object() && m.contains("action_result")) result = static_cast<int>(i);
    }
    REQUIRE(command >= 0);
    REQUIRE(proposal >= 0);
    REQUIRE(result >= 0);
    CHECK(command < proposal);
    CHECK(proposal < result);                 // the executed proposal, then the result it produced
    CHECK(msgs[result].text == "A short summary.");
    CHECK(msgs[proposal].id == out.assistant_message_id);
    CHECK(msgs[proposal].metadata.at("action_proposal").at("status") == "executed");
    CHECK(msgs[proposal].text.find("Gotowe") != std::string::npos);
    CHECK(msgs.size() == 2 + 3);              // the two earlier messages, the command, the proposal, the result: nothing doubled
    // The same with the result as a node of the graph (the preset): the proposal message carries the report, nothing follows it
    Fixture g;
    g.rt->config().set("intent_routing_run", "always");
    g.script(1);
    const auto graph = g.input("Streść tę rozmowę");
    const auto gmsgs = unwrap(g.rt->db().get_msgs(g.conv_id));
    CHECK(gmsgs.back().id == graph.assistant_message_id);
    CHECK(gmsgs.back().text.find("A short summary.") != std::string::npos);
    CHECK(gmsgs.back().metadata.at("action_proposal").at("status") == "executed");
  }

  TEST_CASE("the execution setting: propose first (preset), read-only plans at once, or everything at once; rules still ask") {
    Fixture f;
    // preset: even a read goes through a proposal
    CHECK(f.input("show max_tokens").chunk.at("type") == "action_proposal");
    f.rt->config().set("intent_routing_run", "read_only");
    const auto read = f.input("show max_tokens");
    CHECK(read.chunk.at("type") == "action_result");
    CHECK(read.chunk.at("text").get<std::string>().find("4096") != std::string::npos);
    CHECK(read.chunk.at("proposal").at("status") == "executed");
    // a write still waits under read_only
    CHECK(f.input("set max_tokens to 1000").chunk.at("type") == "action_proposal");
    CHECK(f.rt->config().get("max_tokens") != 1000);
    f.rt->config().set("intent_routing_run", "always");
    const auto now = f.input("set max_tokens to 1000");
    CHECK(now.chunk.at("type") == "action_result");
    CHECK(f.rt->config().get("max_tokens") == 1000);
    CHECK(unwrap(f.rt->actions().status(Json{{"run_id", now.chunk.at("run").at("run_id")}})).at("confirmations").at(0).at("ref") ==
          "auto:intent_routing_run=always");
    // the rule that asks first is still asked, whatever the setting says
    f.overlay("action_rules", Json{{"rules", Json{{"ask_tokens", Json{{"id", "ask_tokens"}, {"scope", "installation"},
        {"target", Json{{"kind", "settings_field"}, {"field_scope", "config"}, {"key", "max_tokens"}}}, {"action", "edit"}, {"effect", "confirm"}, {"initiator", "owner"}}}}}});
    const auto asks = f.input("set max_tokens to 2000");
    CHECK(asks.chunk.at("type") == "action_proposal");
    CHECK(asks.chunk.at("proposal").at("needs_confirmation").at(0).at("initiator") == "owner");
    CHECK(f.rt->config().get("max_tokens") == 1000);
  }

  TEST_CASE("rules apply to what chat proposes: a denied action is not proposed, a confirm rule is listed in the proposal") {
    Fixture f;
    f.overlay("action_rules", Json{{"rules", Json{{"no_summaries", Json{{"id", "no_summaries"}, {"scope", "installation"},
        {"target", Json{{"kind", "class"}, {"id", "conversation"}}}, {"action", "summarise"}, {"effect", "deny"}, {"initiator", "owner"},
        {"reason", Json{{"en", "summaries of conversations are off"}, {"pl", "streszczenia rozmów są wyłączone"}}}}}}}});
    const auto denied = f.input("Streść tę rozmowę");
    REQUIRE(denied.handled);
    CHECK(denied.chunk.at("proposal").at("status") == "denied");
    CHECK(denied.chunk.at("text").get<std::string>().find("streszczenia rozmów są wyłączone") != std::string::npos);
    CHECK(denied.chunk.at("text").get<std::string>().find("owner") != std::string::npos);  // who set the rule
    CHECK(f.transport->requests().empty());
    CHECK_FALSE(f.input("tak").handled);  // nothing is waiting
  }

  TEST_CASE("model method: one guarded model call classifies; a failure is reported and the local method decides") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.9}")));
    const Json r = f.route("Could you boil this chat down to a paragraph?");
    CHECK(r.at("method") == "model");
    CHECK(r.at("kind") == "command");
    CHECK(action_of(r) == "summarise");
    CHECK(r.at("command").at("source") == "model");
    REQUIRE(f.transport->requests().size() == 1);
    const Json body = unwrap(json::parse(f.transport->requests()[0].body));
    CHECK(body.at("model") == "test/model");
    CHECK(body.dump().find("- summarise:") != std::string::npos);  // the catalogue is what the model chooses from
    CHECK(body.dump().find("boil this chat down") != std::string::npos);
    // a model that says message: a message
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.95}")));
    CHECK(f.route("Write me a poem about chat apps").at("kind") == "message");
    // an action that is not in the catalogue, or low confidence: a message
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"command\",\"action\":\"launch_rockets\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.99}")));
    CHECK(f.route("launch the rockets").at("kind") == "message");
    // failure: reported, the local method decides instead
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    const Json failed = f.route("Summarise this conversation");
    CHECK(failed.at("issues").at(0).at("code") == "model_route_failed");
    CHECK(failed.at("kind") == "command");
    CHECK(failed.at("command").at("source") == "lexicon");
    // the model method is the user's choice: with the heuristic no call is made
    f.rt->config().set("intent_routing_method", "heuristic");
    const std::size_t calls = f.transport->requests().size();
    f.route("Summarise this conversation");
    CHECK(f.transport->requests().size() == calls);
  }

  TEST_CASE("model routing is under the x10 guard: a held call is returned with its receipt, nothing is sent") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    for (int i = 0; i < 3; ++i) {
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
          reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.9}")));
      CHECK(f.route("hello " + std::to_string(i)).at("kind") == "message");
    }
    const Json held = f.route(std::string(200000, 'x'));
    REQUIRE(held.at("kind") == "pending");
    CHECK(held.at("usage_pending").at("stage") == "route_intent");
    CHECK(held.at("usage_pending").at("sent") == false);
    CHECK(f.transport->requests().size() == 3);
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.9}")));
    const Json confirmed = f.route(std::string(200000, 'x'), "<current>",
        Json{{"route_usage_confirmation", Json{{"approved", true}, {"receipt_id", held.at("usage_pending").at("receipt_id")},
                                                {"operation_id", held.at("usage_pending").at("operation_id")}, {"ref", "test"}}}});
    CHECK(confirmed.at("kind") == "message");
    CHECK(f.transport->requests().size() == 4);
  }
}
