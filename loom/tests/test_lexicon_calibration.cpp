// P18 3: a sentence that merely says something ABOUT a result ("Ocena tej wiadomości jest niska") is an ordinary message, not the command "critique".
// P17 found that the lexicon (actions.pack phrases, here the stem "ocen*") reads the noun "ocena" as the verb "oceń". The calibration is data:
// intent_routing.pack heuristic.statement_guard - the nouns that double as lexicon stems, and the words that make a sentence a statement.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    rt = unwrap(Runtime::open(o));
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "We should ship on Friday. The tests must pass first.";
    unwrap(rt->db().create_msg(m));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  std::string kind(const std::string& text) { return route(text).value("kind", ""); }
  std::string action(const std::string& text) {
    const Json r = route(text);
    return r.value("kind", "") == "command" ? r.at("command").value("action", "") : std::string();
  }
};

}  // namespace

TEST_SUITE("lexicon_calibration") {
  TEST_CASE("a sentence that says something about a result is a message: the reported one and ten like it, in Polish and in English") {
    Fixture f;
    for (const char* sentence : {
             "Ocena tej wiadomości jest niska.",
             "Krytyka tej wiadomości była bardzo ostra.",
             "Podsumowanie tej rozmowy jest gotowe.",
             "Streszczenie tej wiadomości jest za długie.",
             "Tłumaczenie tej wiadomości jest błędne.",
             "Transkrypcja tej rozmowy wygląda dobrze.",
             "Narracja tej rozmowy jest chaotyczna.",
             "Ocena tego tekstu wynosi pięć.",
             "Czy ocena tej wiadomości jest wysoka?",
             "The critique of this message is harsh.",
             "The translation of this message was wrong.",
             "The evaluation of this conversation seems fair."}) {
      INFO(sentence << " -> " << f.action(sentence));
      CHECK_MESSAGE(f.kind(sentence) == "message", sentence);
    }
  }

  TEST_CASE("the commands the stems exist for still work: a verb, an infinitive, a noun that asks for the act") {
    Fixture f;
    CHECK(f.action("Oceń tę wiadomość") == "critique");
    CHECK(f.action("Skrytykuj tę wiadomość") == "critique");
    CHECK(f.action("Podsumuj tę rozmowę") == "summarise");
    CHECK(f.action("Streść tę wiadomość") == "summarise");
    CHECK(f.action("Przetłumacz tę wiadomość") == "translate");
    CHECK(f.action("Critique this message") == "critique");
    // a noun that asks for the act is a command: no statement in the sentence
    CHECK(f.action("Zrób ocenę tej wiadomości") == "critique");
    CHECK(f.action("Daj mi podsumowanie tej rozmowy") == "summarise");
    // a command with a subordinate clause that has a copula is still a command: the hit is the verb, not a noun
    CHECK(f.action("Oceń tę wiadomość, bo nie jest jasna") == "critique");
    CHECK(f.action("Przetłumacz tę wiadomość, bo jest po polsku") == "translate");
  }

  TEST_CASE("the calibration is data: the nouns and the statement words are lists of the routing profile, and an overlay that empties them restores the old reading") {
    Fixture f;
    CHECK(f.kind("Ocena tej wiadomości jest niska.") == "message");
    // a user who wants the stems to read as commands whatever the sentence says empties the lists (an overlay replaces arrays)
    std::filesystem::create_directories(f.td.path() / "profiles");
    const Json empty = Json{{"pl", Json::array()}, {"en", Json::array()}};
    std::ofstream(f.td.path() / "profiles" / "intent_routing.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "intent_routing"},
                {"overrides", Json{{"heuristic", Json{{"statement_guard", Json{{"nouns", empty}, {"statement_words", empty}}}}}}}}.dump();
    f.rt.reset();
    RuntimeOptions o;
    o.data_dir = f.td.path().string();
    o.start_workers = false;
    f.rt = unwrap(Runtime::open(o));
    f.conv_id = unwrap(f.rt->db().create_conv("Again")).id;
    NewMessage m;
    m.conv_id = f.conv_id;
    m.role = "user";
    m.text = "Something to judge.";
    unwrap(f.rt->db().create_msg(m));
    CHECK(f.action("Ocena tej wiadomości jest niska.") == "critique");
    // the verbs are not touched either way
    CHECK(f.kind("Oceń tę wiadomość") == "command");
  }
}
