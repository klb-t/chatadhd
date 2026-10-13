// P11 1: arguments from loose phrases. The heuristic router reads "na niemiecki", "into German", "w stylu Szekspira",
// "w rozmiarze 1024" with patterns that are data (runtime/intent_routing.pack) and fills the parameters the chosen action
// declares; a word the data does not know stays unextracted, and the proposal says what was read and what is still open.
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
  void script(const std::string& content) {
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json body(std::size_t i) { return unwrap(json::parse(transport->requests().at(i).body)); }
};

Json command(const Json& routed) { return routed.at("command"); }

}  // namespace

TEST_SUITE("action_arguments") {
  TEST_CASE("Polish and English phrases fill target_language; the value is the language by name") {
    Fixture f;
    for (const char* text : {"Przetłumacz tę wiadomość na niemiecki", "przetlumacz ta wiadomosc na jezyk niemiecki", "Tłumacz ostatnią wiadomość po niemiecku",
                             "Translate this message into German", "translate the last message to german"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(command(r).at("action") == "translate");
      CHECK(command(r).at("args") == Json{{"target_language", "German"}});
      CHECK(command(r).at("args_read").at(0).at("param") == "target_language");
      CHECK(command(r).at("feasible") == true);
    }
    // the words that were read are reported as the user wrote them
    CHECK(command(f.route("Przetłumacz tę wiadomość na Niemiecki")).at("args_read").at(0).at("matched") == "na Niemiecki");
    CHECK(command(f.route("Translate this message into French")).at("args") == Json{{"target_language", "French"}});
    CHECK(command(f.route("Przetłumacz tę wiadomość na hiszpański")).at("args") == Json{{"target_language", "Spanish"}});
  }

  TEST_CASE("unknown words are left alone: nothing is guessed, the parameter keeps its default") {
    Fixture f;
    for (const char* text : {"Przetłumacz tę wiadomość na klingoński", "Translate this message into Klingon", "Przetłumacz tę wiadomość",
                             "Przetłumacz tę wiadomość na stronie głównej"}) {
      INFO(text);
      const Json r = f.route(text);
      REQUIRE(r.at("kind") == "command");
      CHECK(command(r).at("action") == "translate");
      CHECK(command(r).at("args") == Json::object());
      CHECK(command(r).at("args_read") == Json::array());
      CHECK(command(r).at("feasible") == true);  // target_language is optional: the language of the command applies
      REQUIRE(command(r).at("params").size() == 1);
      CHECK(command(r).at("params").at(0).at("given") == false);
    }
  }

  TEST_CASE("style, tone, level, audience and size are read into the parameters the action declares, in the user's words") {
    Fixture f;
    Json r = f.route("Sparoduj tę wiadomość w stylu Szekspira");
    REQUIRE(r.at("kind") == "command");
    CHECK(command(r).at("action") == "parody");
    CHECK(command(r).at("args") == Json{{"style", "Szekspira"}});
    CHECK(command(f.route("Parodise this message in the style of Shakespeare.")).at("args") == Json{{"style", "Shakespeare"}});
    CHECK(command(f.route("Parodise this message in a pirate style")).at("args") == Json{{"style", "pirate"}});
    // a parameter the action does not declare is not touched: summarise has none
    r = f.route("Streść tę rozmowę w stylu Szekspira");
    CHECK(command(r).at("action") == "summarise");
    CHECK(command(r).at("args") == Json::object());
    // the size of an image, square when one number is given
    f.rt->config().set("image_model", "image-1");
    f.rt->config().set("image_provider", "img");
    r = f.route("Narysuj z tej rozmowy obraz w rozmiarze 1024");
    CHECK(command(r).at("action") == "depict");
    CHECK(command(r).at("args") == Json{{"size", "1024x1024"}});
    r = f.route("Draw this conversation as an image at 512x768 in the style of watercolour");
    CHECK(command(r).at("args").at("size") == "512x768");
    CHECK(command(r).at("args").at("style") == "watercolour");
    CHECK(command(r).at("args").size() == 2);
  }

  TEST_CASE("the proposal shows what was read and what is still open; the run uses it") {
    Fixture f;
    const auto proposal = f.input("Przetłumacz tę wiadomość na niemiecki");
    REQUIRE(proposal.handled);
    const Json meta = unwrap(f.rt->db().get_msg(proposal.assistant_message_id))->metadata.at("action_proposal");
    CHECK(meta.at("args") == Json{{"target_language", "German"}});
    CHECK(meta.at("args_read").at(0).at("value") == "German");
    CHECK(meta.at("params").at(0).at("from") == "phrase");
    const std::string text = unwrap(f.rt->db().get_msg(proposal.assistant_message_id))->text;
    CHECK(text.find("target_language=German") != std::string::npos);
    CHECK(text.find("Argumenty odczytane") != std::string::npos);
    // an optional parameter that was not given is listed with its default
    const auto parody = f.input("Sparoduj tę wiadomość");
    const std::string parody_text = unwrap(f.rt->db().get_msg(parody.assistant_message_id))->text;
    CHECK(parody_text.find("style (") != std::string::npos);
    CHECK(parody_text.find("Niepodane") != std::string::npos);
    // yes: the words travel to the model (the bytes are pinned)
    // (the pending proposal is the parody; answer it, then ask again for the German one)
    f.script("Parodia.");
    CHECK(f.input("tak").handled);
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(f.body(0).at("messages").back().at("content") == "Napisz parodię powyższego tekstu w tym stylu: lekka, życzliwa satyra.");
    f.script("Wir sollten am Freitag ausliefern.");
    f.input("Przetłumacz tę wiadomość na niemiecki");
    CHECK(f.input("tak").handled);
    REQUIRE(f.transport->requests().size() == 2);
    CHECK(f.body(1).at("messages").back().at("content") == "Przetłumacz powyższy tekst na język: German.");
  }

  TEST_CASE("an argument that was given explicitly is never overwritten by a phrase") {
    Fixture f;
    const Json r = f.route("/translate target_language=French");
    CHECK(command(r).at("args") == Json{{"target_language", "French"}});
    CHECK(command(r).at("source") == "explicit");
  }
}
