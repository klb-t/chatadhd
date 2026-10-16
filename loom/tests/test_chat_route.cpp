// R54 / GRAF_HUB_KOD_SILNIK §8: every chat input goes through input.route_intent: message or command; a command
// is answered in the conversation as a proposal and runs after the user's yes.
#include <doctest/doctest.h>

#include <fstream>
#include <set>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"total_tokens", 7}}}};
}

Json reply_with_usage(const std::string& content, const Json& usage) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", usage}};
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
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests; fetching the model list for a price is tested in test_estimate_registry
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

  // ── P16 1: the routing preset hybrid ──────────────────────────────────

  TEST_CASE("hybrid: a command the local reading is sure of is never overridden by the model; the model is not even asked") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "hybrid");
    const std::string message_verdict = "{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.99}";
    for (const char* text : {"Summarise this conversation", "Streść tę rozmowę", "/summarise"}) {
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
      const Json r = f.route(text);
      INFO(text);
      CHECK(r.at("method") == "hybrid");
      REQUIRE(r.at("kind") == "command");
      CHECK(action_of(r) == "summarise");
      CHECK(r.at("command").at("source") != "model");
      CHECK(r.at("hybrid").at("confident") == true);
      CHECK(r.at("hybrid").at("asked_model") == false);
    }
    CHECK(f.transport->requests().empty());  // three confident commands, a model that would have said "message": no call made
    // a rule said in the conversation is a command the local reading is sure of too
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
    const Json rule = f.route("For me always ask before summarise");
    CHECK(rule.at("kind") == "command");
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("hybrid: an input the local reading is unsure of asks the model, and the model's verdict decides") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "hybrid");
    const Json weak = f.route("Hey, could you summarise this conversation", "<current>", Json{{"method", "heuristic"}});
    REQUIRE(weak.at("kind") == "command");  // the local reading alone finds it, with a score below confident_score
    CHECK(weak.at("command").at("score").get<double>() < 0.7);
    CHECK(weak.at("command").at("score").get<double>() >= 0.5);
    const std::string message_verdict = "{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.95}";
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
    const Json said_message = f.route("Hey, could you summarise this conversation");
    CHECK(said_message.at("hybrid").at("confident") == false);
    CHECK(said_message.at("hybrid").at("asked_model") == true);
    CHECK(said_message.at("kind") == "message");  // the model arbitrates what the local reading was unsure of
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(unwrap(json::parse(f.transport->requests()[0].body)).dump().find("could you summarise this conversation") != std::string::npos);
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.9}")));
    const Json said_command = f.route("Hey, could you summarise this conversation");
    REQUIRE(said_command.at("kind") == "command");
    CHECK(action_of(said_command) == "summarise");
    CHECK(said_command.at("command").at("source") == "model");
    CHECK(f.transport->requests().size() == 2);
    // a failed call: said, and the local reading stands
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    const Json failed = f.route("Hey, could you summarise this conversation");
    CHECK(failed.at("issues").at(0).at("code") == "model_route_failed");
    CHECK(failed.at("kind") == "command");
    CHECK(failed.at("command").at("source") == "lexicon");
  }

  TEST_CASE("hybrid: an ordinary message stays a message and costs no routing call; the bytes sent are the golden ones") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "hybrid");
    f.rt->config().set("system_prompt", "Golden system prompt.");
    for (const char* text : {"What is the weather like in Paris?", "Dzień dobry, jak się masz?", "golden question"}) {
      const Json r = f.route(text);
      INFO(text);
      CHECK(r.at("kind") == "message");
      CHECK(r.at("hybrid").at("asked_model") == false);
    }
    CHECK(f.transport->requests().empty());
    f.script(1, "ok");
    ChatOptions opts;
    opts.stream = false;
    CHECK_FALSE(unwrap(chat_route::handle(*f.rt, "golden question", opts, Json{{"message", "golden question"}}, nullptr)).handled);
    REQUIRE(f.rt->chat().send("golden question", opts));
    REQUIRE(f.transport->requests().size() == 1);
    // the same bytes as with the heuristic method (test "a message continues on the chat path ... (golden)")
    Fixture g;
    g.rt->config().set("system_prompt", "Golden system prompt.");
    g.script(1, "ok");
    CHECK_FALSE(unwrap(chat_route::handle(*g.rt, "golden question", opts, Json{{"message", "golden question"}}, nullptr)).handled);
    REQUIRE(g.rt->chat().send("golden question", opts));
    const Json hybrid_bytes = loom::test::request_json(f.transport->requests().at(0));
    const Json heuristic_bytes = loom::test::request_json(g.transport->requests().at(0));
    CHECK(hybrid_bytes == heuristic_bytes);
    CHECK(hybrid_bytes.at("body") == "{\"model\":\"test/model\",\"messages\":[{\"role\":\"system\",\"content\":\"Golden system prompt.\"},"
                                    "{\"role\":\"user\",\"content\":\"golden question\"}],\"temperature\":0.7,\"max_tokens\":4096,"
                                    "\"stream\":false,\"include_reasoning\":true}");
  }

  TEST_CASE("hybrid: an unknown command shape still gets the definition offer, whatever the model says; ask_when no_candidate is a setting") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "hybrid");
    const Json offered = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(offered.at("kind") == "command");
    CHECK(offered.at("command").at("source") == "emergent");
    CHECK(f.transport->requests().empty());  // nothing matched locally: not a case for the model by default
    // the person who wants every unrecognised input put to the model says so in the overlay; the offer survives the verdict
    f.overlay("intent_routing", Json{{"method", Json{{"presets", Json{{"hybrid", Json{{"ask_when", Json::array({"unsure", "no_candidate"})}}}}}}}});
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.95}")));
    const Json again = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(again.at("kind") == "command");
    CHECK(again.at("command").at("source") == "emergent");
    CHECK(again.at("hybrid").at("asked_model") == true);
    CHECK(f.transport->requests().size() == 1);
  }

  // ── P16 3: the default element of an action ───────────────────────────

  TEST_CASE("a bare command acts on the element its action names by default; a sentence with its own text stays a message (PL)") {
    Fixture f;
    const std::string last = f.last_message().at("id");
    // no element named: translate acts on the last message, summarise on the conversation
    const Json bare = f.route("przetłumacz na niemiecki");
    REQUIRE(bare.at("kind") == "command");
    CHECK(action_of(bare) == "translate");
    CHECK(bare.at("command").at("element").at("id") == last);
    CHECK(bare.at("command").at("element").at("class") == "message");
    CHECK(bare.at("command").at("args").at("target_language") == "German");
    const Json plain = f.route("przetłumacz to na niemiecki, proszę");  // filler words are not content
    REQUIRE(plain.at("kind") == "command");
    CHECK(action_of(plain) == "translate");
    const Json sum = f.route("streść");
    REQUIRE(sum.at("kind") == "command");
    CHECK(action_of(sum) == "summarise");
    CHECK(sum.at("command").at("element").at("id") == f.conv_id);
    for (const char* text : {"przeredaguj", "wytłumacz prościej", "skrytykuj", "rozwiń"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(r.at("command").at("element").at("id") == last);
    }
    // the sentence carries its own text: after a colon, quoted, on a second line, or as more content words than the data allows
    for (const char* text : {"przetłumacz na niemiecki: dzień dobry", "przetłumacz na niemiecki:dzień dobry", "przetłumacz \"dzień dobry\" na niemiecki",
                             "przetłumacz „dzień dobry” na niemiecki", "przetłumacz na niemiecki\ndzień dobry", "przetłumacz dzień dobry na niemiecki",
                             "streść artykuł o grzybach", "Streść poniższy tekst: Ala ma kota"}) {
      INFO(text);
      CHECK(f.route(text).at("kind") == "message");
    }
    // a named element is as before
    const Json named = f.route("przetłumacz tę rozmowę na niemiecki");
    REQUIRE(named.at("kind") == "command");
    CHECK(named.at("command").at("element").at("id") == f.conv_id);
    // "przetłumacz na niemiecki: dzień dobry" is answered by the model: the chat window does not handle it, the bytes are the message's
    ChatOptions opts;
    opts.stream = false;
    CHECK_FALSE(unwrap(chat_route::handle(*f.rt, "przetłumacz na niemiecki: dzień dobry", opts, Json{{"message", "x"}}, nullptr)).handled);
    // and the bare one is proposed in the conversation
    const auto proposed = f.input("przetłumacz na niemiecki");
    CHECK(proposed.handled);
  }

  TEST_CASE("a bare command acts on the element its action names by default; a sentence with its own text stays a message (EN)") {
    Fixture f;
    const std::string last = f.last_message().at("id");
    const Json bare = f.route("translate to German");
    REQUIRE(bare.at("kind") == "command");
    CHECK(action_of(bare) == "translate");
    CHECK(bare.at("command").at("element").at("id") == last);
    CHECK(bare.at("command").at("args").at("target_language") == "German");
    CHECK(action_of(f.route("Please translate it to German")) == "translate");
    CHECK(action_of(f.route("summarise")) == "summarise");
    for (const char* text : {"rephrase", "explain simply", "critique", "elaborate"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(r.at("command").at("element").at("id") == last);
    }
    for (const char* text : {"translate to German: good morning", "translate \"good morning\" to German", "translate good morning to German",
                             "Summarise the plot of Hamlet", "translate to German\ngood morning"}) {
      INFO(text);
      CHECK(f.route(text).at("kind") == "message");
    }
    // the allowed number of content words is data: with two allowed, "good morning" is still the command
    f.overlay("intent_routing", Json{{"heuristic", Json{{"payload", Json{{"max_words", 2}}}}}});
    const Json loose = f.route("translate good morning to German");
    REQUIRE(loose.at("kind") == "command");
    CHECK(action_of(loose) == "translate");
    CHECK(f.route("translate to German: good morning").at("kind") == "message");  // a colon is a payload whatever the number
    // /translate with no element: the action's own default
    const Json explicit_ = f.route("/translate target_language=German");
    REQUIRE(explicit_.at("kind") == "command");
    CHECK(explicit_.at("command").at("element").at("id") == last);
    // a conversation without a message has nothing to translate: the words are a message
    Fixture empty;
    empty.conv_id = unwrap(empty.rt->db().create_conv("Empty")).id;
    CHECK(empty.route("translate to German").at("kind") == "message");
  }

  TEST_CASE("image phrases mean depict, in a command and in a rule; never a media action that is only planned") {
    Fixture f;
    for (const char* text : {"zrób obraz tej rozmowy", "zrób obraz", "narysuj tę rozmowę", "make an image of this conversation", "draw this conversation", "make image"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(action_of(r) == "depict");
      CHECK(r.at("command").at("element").at("id") == f.conv_id);
      for (const auto& c : r.at("command").at("candidates")) CHECK(c.at("action").get<std::string>().rfind("media.", 0) != 0);
    }
    // a sentence about a subject is the model's to answer
    CHECK(f.route("zrób obraz kota").at("kind") == "message");
    for (const char* text : {"pytaj przed obrazem", "zawsze pytaj przed obrazem", "dla mnie zawsze pytaj przed obrazem", "w tej rozmowie pytaj przed obrazem",
                             "ask before image", "for me always ask before image", "for this conversation never run image without asking", "ask before picture",
                             "zabroń obrazu", "never run image without asking"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(action_of(r) == "rules.set");
      CHECK(r.at("command").at("args").at("action") == "depict");
    }
  }

  // ── P16 2: one language per answer ────────────────────────────────────

  TEST_CASE("data: every action has a title and a result text in both languages") {
    Fixture f;
    const Json actions = unwrap(RuntimeProfile::load("actions", f.rt->paths().root)).values().at("actions");
    REQUIRE(actions.size() > 100);
    std::size_t data_results = 0;
    for (auto it = actions.begin(); it != actions.end(); ++it) {
      const Json& a = it.value();
      INFO("action " << it.key());
      CHECK_FALSE(a.value("title", "").empty());                                 // English title
      CHECK_FALSE(a.value("labels", Json::object()).value("pl", "").empty());   // Polish title
      const auto outputs = a.value("outputs", Json::array());
      if (std::find(outputs.begin(), outputs.end(), "text") != outputs.end()) continue;  // words: in the language of the text it works on
      // not words: the sentence that says what came out, in both languages, naming only arguments the action requires
      ++data_results;
      const Json texts = a.value("result_text", Json::object());
      for (const char* lang : {"en", "pl"}) {
        const std::string text = texts.value(lang, "");
        INFO("result_text." << lang);
        REQUIRE_FALSE(text.empty());
        for (std::size_t at = text.find("{{"); at != std::string::npos; at = text.find("{{", at + 2)) {
          const std::string name = text.substr(at + 2, text.find("}}", at) - at - 2);
          bool declared = name == "title";
          for (const auto& p : a.value("params", Json::array()))
            if (p.value("name", "") == name && p.value("required", false)) declared = true;
          INFO("placeholder " << name);
          CHECK(declared);
        }
      }
    }
    CHECK(data_results >= 40);
    // the texts of the router and of the operations: every sentence in both languages
    for (const char* domain : {"intent_routing", "chat_ops"}) {
      const Json texts = unwrap(RuntimeProfile::load(domain, f.rt->paths().root)).values().at("texts");
      INFO(domain);
      for (auto it = texts.at("en").begin(); it != texts.at("en").end(); ++it) CHECK_MESSAGE(texts.at("pl").contains(it.key()), "pl lacks " << it.key());
      for (auto it = texts.at("pl").begin(); it != texts.at("pl").end(); ++it) CHECK_MESSAGE(texts.at("en").contains(it.key()), "en lacks " << it.key());
    }
  }

  TEST_CASE("a Polish conversation is answered in Polish only: proposal, what asks first, result; an input that shows no language follows the conversation") {
    Fixture f;
    f.add("user", "Dzień dobry, mam pytanie o plan wydania.");
    f.overlay("action_rules", Json{{"rules", Json{{"ask_summaries", Json{{"id", "ask_summaries"}, {"scope", "installation"},
        {"target", Json{{"kind", "class"}, {"id", "conversation"}}}, {"action", "summarise"}, {"effect", "confirm"}, {"initiator", "owner"},
        {"reason", Json{{"en", "summaries are asked about first"}, {"pl", "o streszczenia pytam najpierw"}}}}}}}});
    const auto asked = f.input("Streść tę rozmowę");
    REQUIRE(asked.handled);
    const std::string proposal = asked.chunk.at("text");
    CHECK(proposal.find("Rozumiem to jako polecenie: Streść.") != std::string::npos);
    CHECK(proposal.find("o streszczenia pytam najpierw") != std::string::npos);
    CHECK(proposal.find("Streść - o streszczenia") != std::string::npos);  // the title of what asks first is Polish too
    CHECK(proposal.find("Summarise") == std::string::npos);
    CHECK(proposal.find("Use a model") == std::string::npos);
    f.script(1, "Krótkie streszczenie.");
    const auto done = f.input("tak");
    REQUIRE(done.handled);
    CHECK(done.chunk.at("text").get<std::string>().find("Gotowe: Streść.") != std::string::npos);
    CHECK(done.chunk.at("text").get<std::string>().find("Done:") == std::string::npos);
    // an input whose words show no language: the answer follows the conversation, not the default
    const auto bare = f.input("/rules.list");
    REQUIRE(bare.handled);
    CHECK(bare.chunk.at("proposal").at("lang") == "pl");
    const std::string asked_list = bare.chunk.at("text");
    CHECK(asked_list.find("Rozumiem to jako polecenie: Reguły w mocy.") != std::string::npos);
    CHECK(asked_list.find("I read this as a command") == std::string::npos);
    const auto listed = f.input("tak");
    REQUIRE(listed.handled);
    const std::string result = listed.chunk.at("text");
    CHECK(result.find("Gotowe: Reguły w mocy.") != std::string::npos);
    CHECK(result.find("Reguły w mocy:") != std::string::npos);  // the sentence that says what the data is
    CHECK(result.find("Done:") == std::string::npos);
    CHECK(result.find("Rules in force") == std::string::npos);
    // the rule written from the conversation: Polish title and Polish sentence
    const auto rule = f.input("zawsze pytaj przed obrazem");
    REQUIRE(rule.handled);
    CHECK(rule.chunk.at("proposal").at("lang") == "pl");
    CHECK(rule.chunk.at("proposal").at("title") == "Zapisz regułę");
    const auto saved = f.input("tak");
    REQUIRE(saved.handled);
    CHECK(saved.chunk.at("text").get<std::string>().find("Gotowe: Zapisz regułę.") != std::string::npos);
    CHECK(saved.chunk.at("text").get<std::string>().find("Reguła została zapisana:") != std::string::npos);
    CHECK(saved.chunk.at("text").get<std::string>().find("Set a rule") == std::string::npos);
    // the message that holds the result says its language, for the clients
    CHECK(unwrap(f.rt->db().get_msg(saved.assistant_message_id))->metadata.at("action_run").at("lang") == "pl");
  }

  TEST_CASE("an English conversation is answered in English only, and a Polish input in it gets Polish (the words win over the conversation)") {
    Fixture f;
    const auto bare = f.input("/rules.list");
    REQUIRE(bare.handled);
    CHECK(bare.chunk.at("proposal").at("lang") == "en");
    const auto listed = f.input("yes");
    REQUIRE(listed.handled);
    const std::string result = listed.chunk.at("text");
    CHECK(result.find("Done: Rules in force.") != std::string::npos);
    CHECK(result.find("Rules in force:") != std::string::npos);
    CHECK(result.find("Gotowe") == std::string::npos);
    const auto polish = f.input("Streść tę rozmowę");
    REQUIRE(polish.handled);
    CHECK(polish.chunk.at("proposal").at("lang") == "pl");
    // and the conversation has now spoken Polish last: the input that shows no language follows it
    const auto after = f.input("/rules.list");
    REQUIRE(after.handled);
    CHECK(after.chunk.at("proposal").at("lang") == "pl");
    // the feature is data
    f.overlay("intent_routing", Json{{"heuristic", Json{{"conversation_language", Json{{"enabled", false}}}}}});
    const auto plain = f.input("/rules.list");
    REQUIRE(plain.handled);
    CHECK(plain.chunk.at("proposal").at("lang") == "en");
  }

  TEST_CASE("P16: an ordinary message never waits for the model registry; only a proposal's estimate may fetch it, once, and unknown stays unknown") {
    Fixture f;
    f.rt->config().set("model_registry_autorefresh", "on_demand");  // the preset
    // an ordinary message (heuristic routing, and hybrid with a confident or no local reading) sends and fetches nothing while it is routed
    for (const char* method : {"heuristic", "hybrid"}) {
      f.rt->config().set("intent_routing_method", method);
      ChatOptions opts;
      opts.stream = false;
      CHECK_FALSE(unwrap(chat_route::handle(*f.rt, "What is the weather like in Paris?", opts, Json{{"message", "What is the weather like in Paris?"}}, nullptr)).handled);
      CHECK(f.transport->requests().empty());
    }
    // a proposal needs a price: the public list is fetched once; a provider that lists nothing leaves the money unknown, with the reason
    f.rt->config().set("intent_routing_method", "heuristic");
    f.transport->expect("GET", "https://api.test/models", net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array()}}));
    const auto out = f.input("Summarise this conversation");
    REQUIRE(out.handled);
    CHECK(out.chunk.at("proposal").at("cost").at("money").at("known") == false);
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(f.transport->requests()[0].url == "https://api.test/models");
    // the next proposal inside the retry window does not fetch again
    f.input("Streść tę rozmowę");
    CHECK(f.transport->requests().size() == 1);
  }
  // P30: the Polish "Streść tę rozmowę" was routed to define_action of a new verb "stresc" when the routing model (a cheap one) called the
  // sentence a message: the emergent shape check ran without asking the lexicon. The local reading is made first now, and a verb an existing
  // action answers to is never offered as a new one. Every route carries its trace.
  TEST_CASE("P30: a model that finds no command never turns a known verb into define_action; the route trace says why") {
    const char* message_verdict = "{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.9}";
    for (const char* method : {"model", "hybrid", "heuristic"}) {
      for (const char* sentence : {"Streść tę rozmowę", "streść rozmowę", "podsumuj tę rozmowę", "summarise this conversation"}) {
        CAPTURE(method);
        CAPTURE(sentence);
        Fixture f;
        f.rt->config().set("intent_routing_method", method);
        for (int i = 0; i < 2; ++i)
          f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
        const Json r = f.route(sentence);
        REQUIRE(r.at("kind") == "command");
        CHECK(action_of(r) == "summarise");
        CHECK(r.at("command").value("emergent", Json(nullptr)).is_null());
        const Json trace = r.at("trace");
        CHECK(trace.at("schema") == "loom.route_trace/1");
        CHECK(trace.at("chosen") == "summarise");
        CHECK(trace.at("decided_by").is_string());
        CHECK(trace.at("why").is_string());
        CHECK(trace.at("considered").size() >= 1);
        CHECK(r.at("command").at("route_trace").at("chosen") == "summarise");
        if (std::string(method) == "model") {
          const Json st = trace.at("stages").at(0);
          CHECK(st.at("stage") == "model");
          CHECK(st.at("asked") == true);
          CHECK(st.at("model") == "test/model");
          CHECK(st.at("raw").get<std::string>().find("\"message\"") != std::string::npos);
          CHECK(st.at("verdict").at("why") == "model_said_message");
          CHECK(trace.at("decided_by") == "local_recheck");
        }
      }
    }
  }

  TEST_CASE("P30: a genuinely unknown verb is still offered as a new action, and the proposal message keeps the trace") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.9}")));
    const Json r = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(r.at("kind") == "command");
    CHECK(r.at("command").at("emergent").at("kind") == "define_action");
    CHECK(r.at("trace").at("decided_by") == "emergent");
    CHECK(r.at("trace").at("why").get<std::string>().find("define_action") == 0);
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.9}")));
    const auto out = f.input("Rozpisz tę wiadomość na kroki");
    REQUIRE(out.handled);
    const Json proposal_msg = f.last_message("assistant");
    CHECK(proposal_msg.at("metadata").at("route_trace").at("decided_by") == "emergent");
    CHECK(f.last_message("user").at("metadata").at("route_trace").at("schema") == "loom.route_trace/1");
  }

  TEST_CASE("P30: a model answer that names the action is recorded with its raw output and accepted") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        reply("{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.9}")));
    const Json r = f.route("Could you boil this chat down to a paragraph?");
    const Json st = r.at("trace").at("stages").at(0);
    CHECK(r.at("trace").at("decided_by") == "model");
    CHECK(st.at("verdict").at("accepted") == true);
    CHECK(st.at("raw").get<std::string>().find("summarise") != std::string::npos);
    CHECK(r.dump().find("sk-test-key") == std::string::npos);
  }

  // ── P31: the findings of the real-model run (R19) ─────────────────────

  TEST_CASE("P31: an answer that names an existing action is that command even when it says kind=message; the trace says why") {
    const char* named = "{\"kind\":\"message\",\"action\":\"explain_simply\",\"reference\":\"message\",\"args\":{},\"confidence\":1.0}";
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(named)));
    const Json r = f.route("Wyjaśnij to prościej");
    REQUIRE(r.at("kind") == "command");
    CHECK(action_of(r) == "explain_simply");
    CHECK(r.at("command").at("source") == "model");
    const Json st = r.at("trace").at("stages").at(0);
    CHECK(st.at("verdict").at("accepted") == true);
    CHECK(st.at("verdict").at("why") == "named_action_despite_kind_message");
    CHECK(r.at("trace").at("decided_by") == "model");
    // below the threshold, an unknown action or no action at all: still a message
    for (const char* weak : {"{\"kind\":\"message\",\"action\":\"explain_simply\",\"confidence\":0.5}", "{\"kind\":\"message\",\"action\":\"no_such_action\",\"confidence\":1.0}",
                            "{\"kind\":\"message\",\"action\":null,\"confidence\":1.0}"}) {
      Fixture g;
      g.rt->config().set("intent_routing_method", "model");
      g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(weak)));
      CAPTURE(weak);
      CHECK(g.route("Opowiedz mi o kotach").at("kind") == "message");
    }
    // the threshold is data: above 1 it is switched off
    Fixture off;
    off.rt->config().set("intent_routing_method", "model");
    off.overlay("intent_routing", Json{{"model", Json{{"named_action_min_confidence", 2}}}});
    off.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(named)));
    CHECK(off.route("Opowiedz mi o kotach").at("kind") == "message");
  }

  TEST_CASE("P31: the routing prompt is Polish-aware: examples in the language of the input, from the data; no empty needs part") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    const auto prompt_of = [&](const std::string& text) {
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("{\"kind\":\"message\",\"action\":null,\"confidence\":0.9}")));
      f.route(text);
      const Json body = unwrap(json::parse(f.transport->requests().back().body));
      std::string all;
      for (const auto& msg : body.at("messages")) all += msg.at("content").get<std::string>() + "\n";
      return std::make_pair(body.at("messages").at(0).at("content").get<std::string>(), all);
    };
    const auto pl = prompt_of("Opowiedz mi coś o kotach, proszę");
    CHECK(pl.first.find("Streść tę rozmowę\" -> command summarise conversation") != std::string::npos);
    CHECK(pl.first.find("\"wytłumacz prościej\" -> command explain_simply message") != std::string::npos);  // the first phrase of the lexicon
    CHECK(pl.first.find("Co to jest DNS?\" -> message") != std::string::npos);
    CHECK(pl.first.find("Summarise this conversation") == std::string::npos);
    CHECK(pl.second.find("(needs: )") == std::string::npos);
    CHECK(pl.second.find("- summarise: ") != std::string::npos);
    const auto en = prompt_of("Tell me something about cats, please");
    CHECK(en.first.find("Summarise this conversation\" -> command summarise conversation") != std::string::npos);
    CHECK(en.first.find("Streść tę rozmowę") == std::string::npos);
    // data: examples switched off leave only the instruction
    f.overlay("intent_routing", Json{{"model", Json{{"examples", Json{{"enabled", false}}}}}});
    const auto none = prompt_of("Opowiedz mi coś o kotach, proszę");
    CHECK(none.first.find("command summarise") == std::string::npos);
    CHECK(none.first.size() < pl.first.size());
    // the size of the whole prompt, in characters (the real call was ~2780 tokens; the catalogue is most of it)
    std::size_t lines = 0, with_needs = 0;
    for (std::size_t at = pl.second.find("\n- "); at != std::string::npos; at = pl.second.find("\n- ", at + 1)) ++lines;
    for (std::size_t at = pl.second.find("(needs: "); at != std::string::npos; at = pl.second.find("(needs: ", at + 1)) ++with_needs;
    MESSAGE("routing prompt: ", pl.second.size(), " chars with examples, ", none.second.size(), " without; catalogue lines ", lines, ", with needs ", with_needs);
  }

  TEST_CASE("P31: the routing call is already spent: the proposal shows it apart from the estimate, and the ledger counts it") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply_with_usage(
        "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.9}",
        Json{{"prompt_tokens", 2780}, {"completion_tokens", 40}, {"total_tokens", 2820}, {"cost", 0.00029}})));
    const auto out = f.input("Could you boil this chat down to a paragraph?");
    REQUIRE(out.handled);
    const Json proposal = f.last_message("assistant").at("metadata").at("action_proposal");
    REQUIRE(proposal.contains("routing"));
    CHECK(proposal.at("routing").at("usd") == 0.00029);
    CHECK(proposal.at("routing").at("usd_source") == "provider");
    CHECK(proposal.at("routing").at("tokens").at("input") == 2780);
    CHECK(proposal.at("routing").at("tokens").at("output") == 40);
    CHECK(proposal.at("routing").at("model") == "test/model");
    const std::string text = f.last_message("assistant").at("text");
    CHECK(text.find("Already spent reading your sentence") != std::string::npos);
    CHECK(text.find("0.00029 USD") != std::string::npos);
    CHECK(text.find("2820 tokens") != std::string::npos);
    CHECK(text.find("not in the estimate above") != std::string::npos);
    // the estimate of the action is separate and does not include it
    CHECK(proposal.at("cost").at("model_calls") == 1);
    // the call went through use_model: the ledger of the guard recorded it, with its own cohort
    auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", Json::object()));
    CHECK(unwrap(policy->observed("input.route_intent", "test/model", "output_tokens")).at("samples") == 1);
    // a provider that reports no cost and no tokens: unknown, said so - never a zero
    Fixture g;
    g.rt->config().set("intent_routing_method", "model");
    g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply_with_usage(
        "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":0.9}", Json::object())));
    REQUIRE(g.input("Could you boil this chat down to a paragraph?").handled);
    const std::string unknown = g.last_message("assistant").at("text");
    CHECK(unknown.find("the cost is not known") != std::string::npos);
    CHECK(g.last_message("assistant").at("metadata").at("action_proposal").at("routing").at("usd").is_null());
    // a Polish sentence is answered in Polish
    Fixture p;
    p.rt->config().set("intent_routing_method", "model");
    p.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply_with_usage(
        "{\"kind\":\"message\",\"action\":\"summarise\",\"reference\":\"conversation\",\"args\":{},\"confidence\":1.0}",
        Json{{"prompt_tokens", 100}, {"completion_tokens", 10}, {"cost", 0.000025}})));
    REQUIRE(p.input("Zrób z tego krótkie podsumowanie").handled);
    const std::string pl = p.last_message("assistant").at("text");
    CHECK(pl.find("Już wydane na odczytanie zdania") != std::string::npos);
    CHECK(pl.find("0.000025 USD") != std::string::npos);
  }

  TEST_CASE("P31: the model method with local_first asks the model only when the local reading is unsure (a preset, off by default)") {
    const std::string message_verdict = "{\"kind\":\"message\",\"action\":null,\"reference\":null,\"args\":{},\"confidence\":0.99}";
    {
      Fixture f;  // the preset: one model call per input, a confident command included
      f.rt->config().set("intent_routing_method", "model");
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
      f.route("Streść tę rozmowę");
      CHECK(f.transport->requests().size() == 1);
    }
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.overlay("intent_routing", Json{{"method", Json{{"presets", Json{{"model", Json{{"local_first", true}}}}}}}});
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(message_verdict)));
    for (const char* text : {"Summarise this conversation", "Streść tę rozmowę", "/summarise"}) {
      const Json r = f.route(text);
      INFO(text);
      CHECK(r.at("method") == "model");
      REQUIRE(r.at("kind") == "command");
      CHECK(action_of(r) == "summarise");
      CHECK(r.at("hybrid").at("asked_model") == false);
    }
    CHECK(f.transport->requests().empty());  // the local reading was sure: no routing call
    // nothing found locally: the model is asked, as the preset's ask_when says
    CHECK(f.route("Opowiedz mi o kotach").at("kind") == "message");
    CHECK(f.transport->requests().size() == 1);
  }

  // ── P32: the router's catalogue and the verb guard ─────────────────────

  TEST_CASE("P32: the nearest action at full confidence is not accepted for a verb it does not answer to; the trace shows the verb guard") {
    const char* nearest = "{\"kind\":\"command\",\"action\":\"explain_simply\",\"reference\":\"message\",\"args\":{},\"confidence\":1.0}";
    for (const char* sentence : {"Rozpisz tę wiadomość na kroki", "rozpisz tę rozmowę na punkty"}) {
      Fixture f;
      f.rt->config().set("intent_routing_method", "model");
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(nearest)));
      const Json r = f.route(sentence);
      INFO(sentence);
      REQUIRE(r.at("kind") == "command");
      CHECK(r.at("command").at("emergent").at("kind") == "define_action");
      CHECK(r.at("trace").at("decided_by") == "emergent");
      const Json stages = r.at("trace").at("stages");
      CHECK(stages.at(0).at("stage") == "model");
      CHECK(stages.at(0).at("verdict").at("guard").at("verb") == "rozpisz");
      CHECK(stages.at(0).at("verdict").at("guard").at("action") == "explain_simply");
      CHECK(stages.back().at("stage") == "emergent");
      CHECK(stages.back().at("why") == "verb_guard");
    }
    // data: the guard off leaves the model's pick (the old behaviour)
    Fixture off;
    off.rt->config().set("intent_routing_method", "model");
    off.overlay("intent_routing", Json{{"model", Json{{"verb_guard", Json{{"enabled", false}}}}}});
    off.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(nearest)));
    const Json r = off.route("Rozpisz tę wiadomość na kroki");
    CHECK(action_of(r) == "explain_simply");
    CHECK(r.at("trace").at("decided_by") == "model");
    // nothing can be defined (no element near the verb): the model's action stands, and the trace says so
    Fixture g;
    g.rt->config().set("intent_routing_method", "model");
    g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(nearest)));
    const Json k = g.route("Rozpisz to dokładniej");
    if (k.at("kind") == "command" && k.at("command").value("emergent", Json(nullptr)).is_null()) {
      CHECK(action_of(k) == "explain_simply");
      CHECK(k.at("trace").at("stages").at(1).at("why") == "kept_model_action");
    }
  }

  TEST_CASE("P32: kind=define from the model is the definition offer, traced; a known verb still reaches its action") {
    Fixture f;
    f.rt->config().set("intent_routing_method", "model");
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("{\"kind\":\"define\",\"action\":null,\"confidence\":0.9}")));
    const Json r = f.route("Rozpisz tę wiadomość na kroki");
    REQUIRE(r.at("kind") == "command");
    CHECK(r.at("command").at("emergent").at("kind") == "define_action");
    CHECK(r.at("trace").at("stages").at(0).at("verdict").at("why") == "model_said_define");
    CHECK(r.at("trace").at("stages").back().at("stage") == "emergent");
    CHECK(r.at("trace").at("stages").back().at("why") == "model_said_define");
    // a model that says define about a sentence the lexicon knows: the local recheck wins, no new action
    Fixture g;
    g.rt->config().set("intent_routing_method", "model");
    g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("{\"kind\":\"define\",\"action\":null,\"confidence\":0.9}")));
    const Json k = g.route("Streść tę rozmowę");
    REQUIRE(k.at("kind") == "command");
    CHECK(action_of(k) == "summarise");
    // the prompt teaches the answer, in both languages, from the data
    const Json body = unwrap(json::parse(f.transport->requests().back().body));
    const std::string system = body.at("messages").at(0).at("content").get<std::string>();
    CHECK(system.find("\"kind\": \"define\"") != std::string::npos);
    CHECK(system.find("\"Rozpisz to na kroki\" -> define") != std::string::npos);
  }

  TEST_CASE("P32: the model's right answers still route (summarise, translate, explain simply, rules.set)") {
    struct Case { const char* text; const char* answer; const char* action; };
    const Case cases[] = {
        {"Streść tę rozmowę", "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"confidence\":0.95}", "summarise"},
        {"streść rozmowę", "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"confidence\":0.95}", "summarise"},
        {"podsumuj tę rozmowę", "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"confidence\":0.95}", "summarise"},
        {"summarise this conversation", "{\"kind\":\"command\",\"action\":\"summarise\",\"reference\":\"conversation\",\"confidence\":0.95}", "summarise"},
        {"Przetłumacz tę wiadomość na niemiecki", "{\"kind\":\"command\",\"action\":\"translate\",\"reference\":\"message\",\"args\":{\"to\":\"niemiecki\"},\"confidence\":0.95}", "translate"},
        {"Wyjaśnij to prościej", "{\"kind\":\"command\",\"action\":\"explain_simply\",\"reference\":\"message\",\"confidence\":0.95}", "explain_simply"},
        {"pytaj przed obrazem", "{\"kind\":\"command\",\"action\":\"rules.set\",\"confidence\":0.9}", "rules.set"},
    };
    for (const auto& c : cases) {
      Fixture f;
      f.rt->config().set("intent_routing_method", "model");
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(c.answer)));
      const Json r = f.route(c.text);
      INFO(c.text);
      REQUIRE(r.at("kind") == "command");
      CHECK(action_of(r) == c.action);
      CHECK(r.at("command").value("emergent", Json(nullptr)).is_null());
    }
  }

  TEST_CASE("P32: the routing prompt carries the top-K actions closest to the message plus what the local reading scored; K=0 is the whole catalogue") {
    const auto catalogue_of = [](Fixture& f, const std::string& text) {
      f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("{\"kind\":\"message\",\"action\":null,\"confidence\":0.9}")));
      const Json r = f.route(text);
      const Json body = unwrap(json::parse(f.transport->requests().back().body));
      const std::string user = body.at("messages").back().at("content").get<std::string>();
      std::set<std::string> ids;
      for (std::size_t at = user.find("\n- "); at != std::string::npos; at = user.find("\n- ", at + 1)) {
        const std::size_t colon = user.find(':', at);
        ids.insert(user.substr(at + 3, colon - at - 3));
      }
      return std::make_pair(ids, user.size());
    };
    struct Case { const char* text; const char* action; };
    const Case cases[] = {{"Streść tę rozmowę", "summarise"}, {"podsumuj tę rozmowę", "summarise"}, {"summarise this conversation", "summarise"},
                          {"Przetłumacz tę wiadomość na niemiecki", "translate"}, {"Wyjaśnij to prościej", "explain_simply"}, {"pytaj przed obrazem", "rules.set"}};
    Fixture whole;
    whole.rt->config().set("intent_routing_method", "model");
    whole.overlay("intent_routing", Json{{"model", Json{{"catalogue_retrieval", Json{{"top_k", 0}}}}}});
    const auto full = catalogue_of(whole, "Streść tę rozmowę");
    CHECK(full.first.size() > 100);
    for (const auto& c : cases) {
      Fixture f;
      f.rt->config().set("intent_routing_method", "model");
      const auto got = catalogue_of(f, c.text);
      INFO(c.text);
      CHECK(got.first.count(c.action) == 1);
      CHECK(got.first.size() <= 20 + 4 + 8);  // K, the always list, the actions the local reading scored
      CHECK(got.first.size() < full.first.size());
      CHECK(got.second < full.second);
      if (std::string(c.text) == "Streść tę rozmowę")
        MESSAGE("routing prompt (user part): ", full.second, " chars whole catalogue (", full.first.size(), " actions), ", got.second, " chars with K=20 (", got.first.size(), " actions); ~", static_cast<int>(full.second / 3.9), " -> ~", static_cast<int>(got.second / 3.9), " tokens (estimate: chars/3.9)");
    }
    // the trace carries the sizes
    Fixture t;
    t.rt->config().set("intent_routing_method", "model");
    t.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("{\"kind\":\"message\",\"action\":null,\"confidence\":0.9}")));
    const Json cat = t.route("Opowiedz mi o kotach").at("trace").at("stages").at(0).at("catalogue");
    CHECK(cat.at("why") == "top_k");
    CHECK(cat.at("tokens_estimate_after").get<int>() < cat.at("tokens_estimate_before").get<int>());
  }
}
