// P12 1: a "no" to a proposal from the conversation window never loses the user's sentence. The reply offers, in the same
// proposal block, to send the sentence to the model as an ordinary message (words, button, or at once, by the setting
// intent_routing_on_reject: offer | send_as_message | drop). What is sent has exactly the request bytes of an ordinary message.
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
    rt->config().set("system_prompt", "Golden system prompt.");
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
  void script(int n = 1) {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("The model's answer.")));
  }
  ChatOptions options() {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return opts;
  }
  chat_route::Outcome input(const std::string& text, const ChatOptions* opts = nullptr) {
    ChatOptions o = opts ? *opts : options();
    return unwrap(chat_route::handle(*rt, text, o, Json{{"message", text}}, nullptr));
  }
  Message message(const std::string& id) { return *unwrap(rt->db().get_msg(id)); }
  Json proposal(const std::string& id) { return message(id).metadata.at("action_proposal"); }
  Message last() {
    auto msgs = unwrap(rt->db().get_msgs(conv_id));
    return msgs.back();
  }
  // What the chat path does with an outcome that asks for the sentence to be sent (capi_chat.cpp).
  Result<ChatResult> send(const chat_route::Resend& resend) {
    ChatOptions o = options();
    o.history_skip = resend.history_skip;
    return rt->chat().send(resend.text, o, ChatCallbacks{}, nullptr);
  }
  std::string body(std::size_t i) { return transport->requests().at(i).body; }
};

// The bytes an ordinary message makes on a conversation with the same earlier turns.
std::string ordinary_body(const std::string& sentence) {
  Fixture ref;
  ref.script();
  REQUIRE(ref.rt->chat().send(sentence, ref.options(), ChatCallbacks{}, nullptr));
  REQUIRE(ref.transport->requests().size() == 1);
  return ref.body(0);
}

}  // namespace

TEST_SUITE("reject_offer") {
  TEST_CASE("a no offers the sentence in the same proposal; the words take it; the request is exactly an ordinary message's (golden)") {
    const std::string sentence = "Streść tę rozmowę";
    const std::string golden = ordinary_body(sentence);
    CHECK(golden.find("Golden system prompt.") != std::string::npos);
    CHECK(golden.find("Friday works") != std::string::npos);

    Fixture f;
    const auto asked = f.input(sentence);
    REQUIRE(asked.handled);
    const Json p = f.proposal(asked.assistant_message_id);
    CHECK(p.at("status") == "pending");
    CHECK(p.at("sentence").at("message_id") == asked.user_message_id);

    const auto no = f.input("nie");
    REQUIRE(no.handled);
    CHECK(no.chunk.at("proposal_status") == "rejected");
    CHECK(no.chunk.at("offer").at("status") == "open");
    const std::string said = no.chunk.at("text");
    CHECK(said.find("wyślij jako wiadomość") != std::string::npos);
    CHECK(said.find("nie poszło do modelu") != std::string::npos);
    CHECK(f.transport->requests().empty());  // nothing was sent: the offer is not a send
    // the offer is in the same proposal block, and the reply carries the key that finds it
    const Json offer = f.proposal(asked.assistant_message_id).at("offer");
    CHECK(offer == Json{{"action", "send_as_message"}, {"status", "open"}, {"message_id", asked.user_message_id}, {"mode", "offer"}});
    CHECK(f.last().metadata.at("action_offer").at("proposal_message_id") == asked.assistant_message_id);

    const auto take = f.input("wyślij jako wiadomość");
    CHECK_FALSE(take.handled);  // the chat path sends it
    REQUIRE(take.resend.has_value());
    CHECK(take.resend->text == sentence);
    CHECK(take.resend->source_message_id == asked.user_message_id);
    // the exchange of the proposal: the sentence as a command, the proposal, the no, the reply, the words that took it
    CHECK(take.resend->history_skip.size() == 5);
    CHECK(f.proposal(asked.assistant_message_id).at("offer").at("status") == "sent");

    f.script();
    const auto sent = unwrap(f.send(*take.resend));
    CHECK(sent.text == "The model's answer.");
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(f.body(0) == golden);  // byte for byte the request of an ordinary message
    // the sentence is sent as the user's message; the command exchange stays visible in the conversation
    CHECK(f.message(sent.user_message_id).text == sentence);

    // the offer is consumed: the same words now are an ordinary message, not a second send
    CHECK_FALSE(f.input("wyślij jako wiadomość").resend.has_value());
  }

  TEST_CASE("the English words, the yes words and the no words of the offer; anything else leaves it open") {
    Fixture f;
    const auto asked = f.input("Summarise this conversation");
    REQUIRE(asked.handled);
    CHECK(f.proposal(asked.assistant_message_id).at("lang") == "en");
    const auto no = f.input("no");
    CHECK(no.chunk.at("text").get<std::string>().find("send as message") != std::string::npos);
    // a question is a message: the chat path takes it and the offer stays open on the proposal
    CHECK_FALSE(f.input("What does that mean for Friday?").handled);
    CHECK(f.proposal(asked.assistant_message_id).at("offer").at("status") == "open");
    f.add("user", "What does that mean for Friday?");
    f.add("assistant", "That the tests must pass by Thursday.");
    CHECK_FALSE(f.input("send as message").resend.has_value());  // no longer the last thing said: the button still works (below)

    Fixture g;
    REQUIRE(g.input("Summarise this conversation").handled);
    g.input("no");
    const auto yes = g.input("yes");
    REQUIRE(yes.resend.has_value());
    CHECK(yes.resend->text == "Summarise this conversation");

    Fixture h;
    const auto asked_h = h.input("Summarise this conversation");
    h.input("no");
    const auto closed = h.input("no");  // a no to the offer closes it
    REQUIRE(closed.handled);
    CHECK(closed.chunk.at("proposal_status") == "rejected");
    CHECK(closed.chunk.at("offer").at("status") == "declined");
    CHECK(h.proposal(asked_h.assistant_message_id).at("offer").at("status") == "declined");
    CHECK(h.transport->requests().empty());
    CHECK_FALSE(h.input("send as message").resend.has_value());
  }

  TEST_CASE("presets of intent_routing_on_reject: offer (default), send_as_message, drop; an unknown name is the default") {
    const std::string sentence = "Streść tę rozmowę";
    const std::string golden = ordinary_body(sentence);
    {  // drop: the command is dropped and so is the offer; the sentence stays in the conversation
      Fixture f;
      f.rt->config().set("intent_routing_on_reject", "drop");
      const auto asked = f.input(sentence);
      const auto no = f.input("nie");
      REQUIRE(no.handled);
      CHECK_FALSE(no.chunk.contains("offer"));
      CHECK(no.chunk.at("text") == "Odrzucone. Nic nie wykonano.");
      CHECK_FALSE(f.proposal(asked.assistant_message_id).contains("offer"));
      CHECK_FALSE(f.last().metadata.contains("action_offer"));
      CHECK_FALSE(f.input("wyślij jako wiadomość").resend.has_value());
      CHECK(f.transport->requests().empty());
    }
    {  // send_as_message: the no is answered and the sentence goes to the model at once, as an ordinary message
      Fixture f;
      f.rt->config().set("intent_routing_on_reject", "send_as_message");
      const auto asked = f.input(sentence);
      const auto no = f.input("nie");
      CHECK_FALSE(no.handled);
      REQUIRE(no.resend.has_value());
      CHECK(no.resend->text == sentence);
      CHECK(no.resend->history_skip.size() == 4);  // the sentence, the proposal, the no, the reply
      CHECK(f.proposal(asked.assistant_message_id).at("offer").at("status") == "sent");
      CHECK(f.proposal(asked.assistant_message_id).at("status") == "rejected");
      f.script();
      unwrap(f.send(*no.resend));
      REQUIRE(f.transport->requests().size() == 1);
      CHECK(f.body(0) == golden);
    }
    for (const char* name : {"offer", "banana"}) {  // offer, and what a name the profile does not list is
      INFO(name);
      Fixture f;
      f.rt->config().set("intent_routing_on_reject", name);
      const auto asked = f.input(sentence);
      const auto no = f.input("nie");
      REQUIRE(no.handled);
      CHECK(no.chunk.at("offer").at("status") == "open");
      CHECK(f.proposal(asked.assistant_message_id).at("offer").at("mode") == "offer");
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("the button: confirm settles the proposal of the conversation, send_as_message takes the offer; one click sends once") {
    const std::string sentence = "Streść tę rozmowę";
    const std::string golden = ordinary_body(sentence);
    Fixture f;
    const auto asked = f.input(sentence);
    const Json p = f.proposal(asked.assistant_message_id);
    const std::size_t before = unwrap(f.rt->db().get_msgs(f.conv_id)).size();

    // a wrong receipt does not settle anything
    CHECK_FALSE(chat_route::confirm(*f.rt, Json{{"run_id", p.at("run_id")}, {"approved", false}, {"receipt_id", "arr_wrong"}, {"ref", "web"}}));
    CHECK(f.proposal(asked.assistant_message_id).at("status") == "pending");

    const Json answered = unwrap(chat_route::confirm(*f.rt, Json{{"run_id", p.at("run_id")}, {"approved", false}, {"receipt_id", p.at("receipt_id")}, {"ref", "web"}}));
    CHECK(answered.at("status") == "rejected");
    CHECK(answered.at("proposal_message_id") == asked.assistant_message_id);
    CHECK(answered.at("proposal_status") == "rejected");
    CHECK(answered.at("offer").at("status") == "open");
    CHECK_FALSE(answered.contains("resend"));
    CHECK(f.proposal(asked.assistant_message_id).at("status") == "rejected");
    CHECK(f.proposal(asked.assistant_message_id).at("offer").at("status") == "open");
    CHECK(unwrap(f.rt->db().get_msgs(f.conv_id)).size() == before + 1);  // the reply only: a click is not a typed message
    CHECK(f.message(answered.at("message_id")).metadata.at("action_offer").at("proposal_message_id") == asked.assistant_message_id);

    // the button: a chat request carrying send_as_message
    ChatOptions click = f.options();
    click.send_as_message = asked.assistant_message_id;
    const auto routed = f.input("ignored: the sentence is the one in the conversation", &click);
    CHECK_FALSE(routed.handled);
    REQUIRE(routed.resend.has_value());
    CHECK(routed.resend->text == sentence);
    CHECK(routed.resend->history_skip.size() == 3);  // the sentence, the proposal, the reply
    f.script();
    unwrap(f.send(*routed.resend));
    CHECK(f.body(0) == golden);

    // a second click does not send it twice ...
    const auto again = f.input("x", &click);
    CHECK(again.handled);
    CHECK(again.chunk.at("type") == "error");
    CHECK(again.chunk.at("code") == "already_exists");
    // ... except as the re-send of the same request the usage guard held
    ChatOptions held = click;
    held.usage_confirmation = Json{{"approved", true}};
    REQUIRE(f.input("x", &held).resend.has_value());
    // an unknown proposal is an error of the click, not a send
    ChatOptions bad = f.options();
    bad.send_as_message = "m_000000000000";
    CHECK(f.input("x", &bad).chunk.at("type") == "error");
  }

  TEST_CASE("send_as_message under the button: the setting asks the client to send; confirm says so") {
    Fixture f;
    f.rt->config().set("intent_routing_on_reject", "send_as_message");
    const auto asked = f.input("Streść tę rozmowę");
    const Json p = f.proposal(asked.assistant_message_id);
    const Json answered = unwrap(chat_route::confirm(*f.rt, Json{{"run_id", p.at("run_id")}, {"approved", false}, {"receipt_id", p.at("receipt_id")}, {"ref", "web"}}));
    CHECK(answered.at("resend") == Json{{"proposal_message_id", asked.assistant_message_id}});
    CHECK(answered.at("offer").at("status") == "open");  // the client's chat request takes it; the call itself sent nothing
    CHECK(f.transport->requests().empty());
    ChatOptions click = f.options();
    click.send_as_message = asked.assistant_message_id;
    CHECK(f.input("x", &click).resend.has_value());
  }

  TEST_CASE("a yes through confirm settles the proposal as executed and posts the result; a plain run without a conversation is untouched") {
    Fixture f;
    f.script();
    const auto asked = f.input("Streść tę rozmowę");
    const Json p = f.proposal(asked.assistant_message_id);
    const Json done = unwrap(chat_route::confirm(*f.rt, Json{{"run_id", p.at("run_id")}, {"approved", true}, {"receipt_id", p.at("receipt_id")}, {"ref", "web"}}));
    CHECK(done.at("status") == "completed");
    CHECK(done.at("proposal_status") == "executed");
    CHECK_FALSE(done.contains("offer"));
    CHECK(f.proposal(asked.assistant_message_id).at("status") == "executed");
    CHECK(f.message(done.at("message_id")).text.find("The model's answer.") != std::string::npos);
    // a run that no proposal waits for is the plain action
    const Json run = unwrap(f.rt->actions().run(Json{{"action", "summarise"}, {"element", Json{{"id", f.conv_id}}}, {"propose", true}, {"ref", "api"}}));
    f.script();
    const Json plain = unwrap(chat_route::confirm(*f.rt, Json{{"run_id", run.at("id")}, {"approved", true}, {"receipt_id", run.at("pending").at("receipt_id")}, {"ref", "api"}}));
    CHECK(plain.at("status") == "completed");
    CHECK_FALSE(plain.contains("proposal_message_id"));
  }

  TEST_CASE("a definition proposed for an unknown command is rejected without losing the sentence") {
    const std::string sentence = "Rozpisz tę wiadomość na kroki";
    const std::string golden = ordinary_body(sentence);
    Fixture f;
    const auto asked = f.input(sentence);
    REQUIRE(asked.handled);
    CHECK(f.proposal(asked.assistant_message_id).at("kind") == "define_action");
    const auto no = f.input("nie");
    REQUIRE(no.handled);
    CHECK(no.chunk.at("offer").at("status") == "open");
    CHECK(f.proposal(asked.assistant_message_id).at("definition").at("id") == "rozpisz");
    const auto take = f.input("wyślij jako wiadomość");
    REQUIRE(take.resend.has_value());
    CHECK(take.resend->text == sentence);
    f.script();
    unwrap(f.send(*take.resend));
    CHECK(f.body(0) == golden);
  }

  TEST_CASE("a proposal that grew out of a run (no sentence behind it) offers nothing") {
    Fixture f;
    // (the growth offered after a run is made without a sentence; the cheapest check is a proposal with none recorded)
    const auto asked = f.input("Streść tę rozmowę");
    MsgPatch patch;
    Json meta = f.message(asked.assistant_message_id).metadata;
    meta["action_proposal"].erase("sentence");
    patch.metadata = meta;
    REQUIRE(f.rt->db().update_msg(asked.assistant_message_id, patch));
    const auto no = f.input("nie");
    REQUIRE(no.handled);
    CHECK_FALSE(no.chunk.contains("offer"));
    CHECK(no.chunk.at("text") == "Odrzucone. Nic nie wykonano.");
  }
}
