// M1-K2b-2: the interview held IN THE CONVERSATION WINDOW (form mode): the conversation is marked as the interview, its messages are answered by the
// interview and never by the model, what it proposes is an ordinary action proposal of profile.confirm, and the words on the screen carry no internal
// names. A scripted transport sees every request: the interview makes none. Synthetic data only.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/onboarding_store.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json model_reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fx {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  // `chat_ops_overrides`: an overlay of the pack chat_ops written into the data directory before the runtime opens (<data>/profiles/chat_ops.pack).
  explicit Fx(const Json& chat_ops_overrides = Json(nullptr)) {
    if (!chat_ops_overrides.is_null()) {
      std::filesystem::create_directories(td.path() / "profiles");
      std::ofstream(td.path() / "profiles" / "chat_ops.pack")
          << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_ops"}, {"overrides", chat_ops_overrides}}.dump();
    }
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
  }
  Json run(const std::string& action, Json request) {
    if (!request.contains("locale")) request["locale"] = "pl";
    return unwrap(rt->actions().invoke(action, request));
  }
  std::string start(Json more = Json::object()) {
    more["op"] = "start";
    const Json r = run("profile.interview", more);
    INFO(r.dump());
    REQUIRE(r.at("status") == "completed");
    return r.at("result").at("data").at("conv_id").get<std::string>();
  }
  chat_route::Outcome say(const std::string& conv, const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  std::vector<Message> messages(const std::string& conv) { return unwrap(rt->db().get_msgs(conv)); }
  Message last(const std::string& conv) { return messages(conv).back(); }
  std::string transcript(const std::string& conv) {
    std::string out;
    for (const auto& m : messages(conv)) out += m.role + ": " + m.text + "\n";
    return out;
  }
  Json slots() { return run("profile.show", Json::object()).at("result").at("data").at("slots"); }
};

// Words that are internal names: keys with an underscore, dotted ids, true/false, key=value, the ids of operations.
bool has_internal_name(const std::string& visible, std::string* what = nullptr) {
  static const std::regex bad("[a-z]+_[a-z]+|\\b[a-z]+\\.[a-z]+\\b|\\btrue\\b|\\bfalse\\b|=|\\bop\\b");
  std::smatch m;
  if (std::regex_search(visible, m, bad)) {
    if (what) *what = m.str(0);
    return true;
  }
  return false;
}

}  // namespace

TEST_SUITE("profile interview in the conversation") {
  TEST_CASE("start opens a conversation marked as the interview; its first message is the level question with the previews, and nothing is sent") {
    Fx f;
    const std::string conv = f.start();
    const auto cv = unwrap(f.rt->db().get_conv(conv));
    REQUIRE(cv);
    CHECK(cv->metadata.at("flow") == "profile.interview");
    CHECK(cv->title == "Poznajmy się");
    const auto msgs = f.messages(conv);
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].role == "assistant");
    CHECK(msgs[0].text.find("bez modelu") != std::string::npos);
    CHECK(msgs[0].text.find("Minimalnie") != std::string::npos);
    CHECK(msgs[0].semantic_status == "done");  // the answers are private: no background worker reads them
    // an empty conversation of the client becomes the interview; one that has messages is refused and left alone
    const std::string empty = unwrap(f.rt->db().create_conv("Pusta")).id;
    CHECK(f.start(Json{{"conv_id", empty}}) == empty);
    CHECK(unwrap(f.rt->db().get_conv(empty))->metadata.at("flow") == "profile.interview");
    const std::string busy = unwrap(f.rt->db().create_conv("Zajęta")).id;
    NewMessage m;
    m.conv_id = busy;
    m.role = "user";
    m.text = "cześć";
    unwrap(f.rt->db().create_msg(m));
    const Json refused = f.run("profile.interview", Json{{"op", "start"}, {"conv_id", busy}});
    CHECK(refused.at("status") != "completed");
    CHECK(unwrap(f.rt->db().get_conv(busy))->metadata.value("flow", "") != "profile.interview");
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("a short interview: level, an answer, a remark with its proposal, the yes, the next question - and the model is never called") {
    Fx f;
    const std::string conv = f.start();
    const auto level = f.say(conv, "minimalnie");
    CHECK(level.handled);
    const std::string question = f.last(conv).text;
    CHECK(question.find("Minimalnie") != std::string::npos);  // the level is said by its name, and the next question follows
    // a remark: understood, proposed, nothing written
    const auto remark = f.say(conv, "nie pytaj o zdrowie");
    REQUIRE(remark.handled);
    const Json proposal = f.last(conv).metadata.at("action_proposal");
    CHECK(proposal.at("action") == "profile.confirm");
    CHECK(proposal.at("status") == "pending");
    CHECK_FALSE(proposal.value("run_id", "").empty());
    CHECK_FALSE(proposal.value("receipt_id", "").empty());
    for (const auto& s : f.slots()) CHECK(s.at("state") != "never");
    // while the proposal waits, other words are not guessed into an answer
    f.say(conv, "a może jednak coś innego");
    CHECK(f.last(conv).text.find("Czekam na Twoje") != std::string::npos);
    for (const auto& s : f.slots()) CHECK(s.at("state") != "never");
    // the yes: the topic is closed for good, the next question follows
    const auto yes = f.say(conv, "tak");
    CHECK(yes.handled);
    int never = 0;
    for (const auto& s : f.slots()) never += s.at("state") == "never" ? 1 : 0;
    CHECK(never >= 1);
    const auto msgs = f.messages(conv);
    CHECK(msgs.back().role == "assistant");
    CHECK(msgs.back().metadata.at("interview").at("kind") == "reply");
    MESSAGE("transcript of the short interview:\n", f.transcript(conv));
    // nothing on the screen carries an internal name
    for (const auto& m : msgs)
      if (m.role == "assistant") {
        std::string what;
        CHECK_MESSAGE(!has_internal_name(m.text, &what), "internal name \"" << what << "\" in: " << m.text);
      }
    CHECK(f.transport->requests().empty());
  }

  // K2b-3: a remark never depends on a question that waits. The emulator (S2, android-smoke) found the path minimal -> skip -> "nie pytaj o zdrowie":
  // the old code answered with the error of a missing field ("Którego pola dotyczy?") before the remark was read.
  int remaining(Fx& f) { return f.run("profile.interview", Json{{"op", "status"}}).at("result").at("data").at("remaining").get<int>(); }

  TEST_CASE("a remark is understood with no question waiting: the section skipped at the minimal level (a), proposal first, nothing written before the yes") {
    Fx f;
    const std::string conv = f.start();
    f.say(conv, "minimalnie");
    for (int i = 0; i < 20 && remaining(f) > 0; ++i) f.say(conv, "pomiń");
    REQUIRE(remaining(f) == 0);  // no question is waiting
    const Json before = f.slots();
    f.say(conv, "nie pytaj o zdrowie");
    INFO(f.transcript(conv));
    const Message last = f.last(conv);
    MESSAGE("remark with nothing waiting: ", last.text);
    CHECK(last.role == "assistant");
    CHECK(last.text.find("Którego pola") == std::string::npos);
    REQUIRE(last.metadata.contains("action_proposal"));
    CHECK(last.metadata.at("action_proposal").at("action") == "profile.confirm");
    CHECK(last.metadata.at("action_proposal").at("status") == "pending");
    CHECK(f.slots() == before);  // nothing is written before the yes
    f.say(conv, "tak");
    int never = 0;
    for (const auto& s : f.slots()) never += s.at("state") == "never" ? 1 : 0;
    CHECK(never >= 1);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("a remark is understood when every question at the level is answered (b); after the yes the category is never and no longer asked") {
    Fx f;
    const std::string conv = f.start();
    f.say(conv, "szczegółowo");
    for (int i = 0; i < 200 && remaining(f) > 0; ++i) f.run("profile.interview", Json{{"op", "answer"}, {"text", "odpowiedź testowa"}});
    REQUIRE(remaining(f) == 0);
    const Json before = f.slots();
    for (const auto& s : before) { if (s.at("slot") == "health.needs") CHECK(s.at("asked_at_level") == true); }
    f.say(conv, "nie pytaj o zdrowie");
    INFO(f.transcript(conv));
    const Message last = f.last(conv);
    REQUIRE(last.metadata.contains("action_proposal"));
    CHECK(last.metadata.at("action_proposal").at("action") == "profile.confirm");
    CHECK(last.metadata.at("action_proposal").at("status") == "pending");
    CHECK(f.slots() == before);
    f.say(conv, "tak");
    bool seen = false;
    for (const auto& s : f.slots())
      if (s.at("slot") == "health.needs") {
        seen = true;
        CHECK(s.at("state") == "never");
        CHECK(s.at("asked_at_level") == false);
        CHECK(s.at("reason") == "never");
      }
    CHECK(seen);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("words that are not a remark and not a control word, with no question waiting, get plain words - no error text, nothing written (c)") {
    for (const char* lang : {"pl", "en"}) {
      Fx f;
      const std::string conv = f.start(Json{{"locale", lang}});
      f.say(conv, std::string(lang) == "pl" ? "minimalnie" : "minimal");
      for (int i = 0; i < 20 && remaining(f) > 0; ++i) f.say(conv, std::string(lang) == "pl" ? "pomiń" : "skip");
      REQUIRE(remaining(f) == 0);
      const Json before = f.slots();
      const auto said = f.say(conv, std::string(lang) == "pl" ? "opowiem Ci coś o sobie" : "let me tell you something about me");
      CHECK(said.handled);
      const Message last = f.last(conv);
      MESSAGE(lang, ": ", last.text);
      CHECK(last.role == "assistant");
      CHECK_FALSE(last.metadata.contains("action_proposal"));
      CHECK(last.metadata.at("interview").at("kind") == "reply");
      CHECK(last.metadata.at("interview").at("question").is_null());
      CHECK(last.text.find(std::string(lang) == "pl" ? "Którego pola" : "Which field") == std::string::npos);
      CHECK(last.text.find(std::string(lang) == "pl" ? "nie czekam na Twoją odpowiedź" : "not waiting for an answer") != std::string::npos);
      CHECK(f.slots() == before);
      std::string what;
      CHECK_MESSAGE(!has_internal_name(last.text, &what), "internal name \"" << what << "\" in: " << last.text);
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("with a question waiting, an answer and a remark behave as before (d); the words promised when nothing waits really work") {
    Fx f;
    const std::string conv = f.start();
    f.say(conv, "minimalnie");
    REQUIRE(remaining(f) > 0);
    const Json before = f.slots();
    f.say(conv, "nie pytaj o zdrowie");  // remark with a question waiting: the same proposal
    REQUIRE(f.last(conv).metadata.contains("action_proposal"));
    f.say(conv, "nie");
    CHECK(f.slots() == before);
    f.say(conv, "pytaj więcej");  // promised by the words when nothing waits: one level up, proposed first
    const Message more = f.last(conv);
    REQUIRE(more.metadata.contains("action_proposal"));
    CHECK(more.metadata.at("action_proposal").at("action") == "profile.confirm");
    f.say(conv, "nie");
    f.say(conv, "podsumowanie");  // the other promised word: a control word, answered in words
    MESSAGE("podsumowanie: ", f.last(conv).text);
    CHECK(f.last(conv).role == "assistant");
    CHECK(f.last(conv).text.find("Którego pola") == std::string::npos);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("a refusal (no) writes nothing and the question comes again; 'nie teraz' declines the question asked") {
    Fx f;
    const std::string conv = f.start();
    f.say(conv, "minimalnie");
    f.say(conv, "nie pytaj o zdrowie");
    f.say(conv, "nie");
    for (const auto& s : f.slots()) CHECK(s.at("state") != "never");
    CHECK(f.last(conv).role == "assistant");
    f.say(conv, "nie teraz");
    int declined = 0;
    for (const auto& s : f.slots()) declined += s.at("state") == "declined" ? 1 : 0;
    CHECK(declined == 1);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("an ordinary conversation in the same database is untouched and reaches the model; the interview refuses the model path") {
    Fx f;
    const std::string interview = f.start();
    const std::string ordinary = unwrap(f.rt->db().create_conv("Zwykła")).id;
    ChatOptions opts;
    opts.conv_id = ordinary;
    opts.stream = false;
    const auto routed = unwrap(chat_route::handle(*f.rt, "What is the capital of France?", opts, Json{{"message", "What is the capital of France?"}}, nullptr));
    REQUIRE_FALSE(routed.handled);
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, model_reply("Paris.")));
    const auto sent = unwrap(f.rt->chat().send("What is the capital of France?", opts));
    CHECK(sent.text == "Paris.");
    CHECK(f.transport->requests().size() == 1);
    // the interview conversation: not even a direct send reaches the model
    ChatOptions into;
    into.conv_id = interview;
    into.stream = false;
    const auto refused = f.rt->chat().send("coś o mnie", into);
    CHECK_FALSE(refused);
    CHECK(f.transport->requests().size() == 1);
    // and the routing method "off" does not open a way to the model for the interview
    f.rt->config().set("intent_routing_method", "off");
    CHECK(f.say(interview, "minimalnie").handled);
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("every visible sentence of the profile words, in Polish and in English, is free of internal names") {
    const Json pack = unwrap(RuntimeProfile::builtin("chat_ops")).values();
    static const std::regex placeholder("\\{\\{[^}]*\\}\\}");
    int checked = 0;
    for (const char* lang : {"pl", "en"}) {
      const Json texts = pack.at("texts").at(lang);
      for (auto it = texts.begin(); it != texts.end(); ++it) {
        if (it.key().rfind("profile_", 0) != 0) continue;
        std::string what;
        const std::string visible = std::regex_replace(it.value().get<std::string>(), placeholder, "");
        CHECK_MESSAGE(!has_internal_name(visible, &what), lang << " " << it.key() << ": internal name \"" << what << "\" in: " << visible);
        ++checked;
      }
      // the control words exist in both languages for every operation
      for (auto w = pack.at("profile").at("words").begin(); w != pack.at("profile").at("words").end(); ++w) CHECK_MESSAGE(!w.value().at(lang).empty(), lang << " words of " << w.key());
    }
    CHECK(checked >= 100);
  }

  TEST_CASE("a scenario stored before the questions existed is refreshed only after the person's yes, and the answers stay") {
    Fx f;
    const std::string conv = f.start();
    f.say(conv, "minimalnie");
    onboarding::OnboardingStore store(f.rt->db());
    const Json state = unwrap(store.read("local"));
    Json old = state.at("scenario_definition");
    old.erase("detail_levels");
    old.erase("remark_rules");
    const auto degraded = store.update_pack("local", state.at("revision").get<std::int64_t>(), state.at("pack"), old);
    MESSAGE("old scenario stored: ", std::string(degraded ? "yes" : "no"));
    if (!degraded) {
      MESSAGE("the degraded scenario could not be stored (" << degraded.error().message << "): the old-store path is not exercised by this test");
      return;
    }
    f.say(conv, "dalej");
    const Json proposal = f.last(conv).metadata.at("action_proposal");
    CHECK(proposal.at("action") == "profile.confirm");
    CHECK(proposal.at("status") == "pending");
    CHECK(unwrap(store.read("local")).at("scenario_definition").contains("detail_levels") == false);  // nothing was changed behind the person's back
    f.say(conv, "tak");
    CHECK(unwrap(store.read("local")).at("scenario_definition").contains("detail_levels"));
    CHECK(f.transport->requests().empty());
  }

  // K2b-4 (S2, android-smoke run 39870977153, three emulator profiles): the emulator's interface language is English (the device locale is en),
  // the driver types Polish. The language of the conversation (metadata.lang, fixed at start) decides the language of the words ON THE SCREEN; what
  // the person types is understood in every language the pack has words for. Before the fix 'nie pytaj o zdrowie' in an English conversation was
  // stored as the ANSWER to the waiting question.
  std::string slot_states(Fx& f) {
    std::string out;
    for (const auto& s : f.slots()) out += s.at("slot").get<std::string>() + "=" + s.at("state").get<std::string>() + ";";
    return out;
  }
  int never_count(Fx& f) {
    int n = 0;
    for (const auto& s : f.slots()) n += s.at("state") == "never" ? 1 : 0;
    return n;
  }

  // One fixture per case (a runtime and every message cost seconds in a debug build): the remark with a question waiting, the proposal refused, the remark
  // again, the yes. With no question waiting the remark is read before any question is looked for in either language (K2b-3 above). The control words of
  // every language are tried without a runtime in test_profile_words.cpp.
  TEST_CASE("K2b-4: a remark typed in the other language is a remark - proposal first, nothing written, the lines in the language of the conversation") {
    struct Case {
      const char* conv_lang;
      const char* level;
      const char* skip;
      const char* remark;
      const char* yes;
      const char* no;
      const char* head;        // the sentence that opens the proposal, in the language of the conversation
      const char* understood;  // the rule's own line, in the language of the conversation
      const char* other_head;  // the same sentence in the OTHER language: must not appear
      const char* stored;      // the sentence of a stored answer, in the language of the conversation: must not appear
    };
    const Case cases[] = {
        {"en", "minimal", "skip", "nie pytaj o zdrowie", "yes", "no", "This is how I understood it:", "I will not ask about health", "Tak zrozumiałem", "I stored your answer"},
        {"pl", "minimalnie", "pomiń", "do not ask about health", "tak", "nie", "Tak zrozumiałem:", "Nie będę pytać o zdrowie", "This is how I understood it", "Zapisałem Twoją odpowiedź"},
    };
    for (const auto& c : cases) {
      INFO("conversation " << std::string(c.conv_lang) << ", remark \"" << std::string(c.remark) << "\"");
      Fx f;
      const std::string conv = f.start(Json{{"locale", c.conv_lang}});
      REQUIRE(unwrap(f.rt->db().get_conv(conv))->metadata.value("lang", "") == c.conv_lang);
      f.say(conv, c.level);
      for (const bool refuse : {true, false}) {
        INFO("proposal refused: " << refuse);
        const Json before = f.slots();
        const std::string states_before = slot_states(f);
        f.say(conv, c.remark);
        INFO(f.transcript(conv));
        const Message last = f.last(conv);
        MESSAGE(std::string(c.conv_lang), " conversation, remark \"", std::string(c.remark), "\" -> ", last.text);
        CHECK(last.role == "assistant");
        REQUIRE(last.metadata.contains("action_proposal"));
        CHECK(last.metadata.at("action_proposal").at("action") == "profile.confirm");
        CHECK(last.metadata.at("action_proposal").at("status") == "pending");
        CHECK(last.text.find(c.head) != std::string::npos);
        CHECK(last.text.find(c.understood) != std::string::npos);
        CHECK(last.text.find(c.other_head) == std::string::npos);
        CHECK(last.text.find(c.stored) == std::string::npos);
        CHECK(f.slots() == before);  // nothing is written before the yes
        CHECK(slot_states(f) == states_before);
        if (refuse) {
          f.say(conv, c.no);  // refused: nothing written, the question comes again
          CHECK(slot_states(f) == states_before);
        } else {
          f.say(conv, c.yes);
          CHECK(never_count(f) >= 1);
        }
      }
      CHECK(f.transport->requests().empty());
    }
  }

  TEST_CASE("K2b-4: the languages the person is understood in are a preset of the pack (profile.match_languages): 'all' by default, changeable in the overlay") {
    CHECK(unwrap(RuntimeProfile::builtin("chat_ops")).values().at("profile").at("match_languages") == "all");
    struct Case {
      Json overrides;
      bool polish_remark_understood;   // 'nie pytaj o zdrowie' in an English conversation
      bool english_remark_understood;  // 'do not ask about health' in an English conversation
    };
    const Case cases[] = {
        {Json{{"profile", Json{{"match_languages", "all"}}}}, true, true},
        {Json{{"profile", Json{{"match_languages", "conversation"}}}}, false, true},  // the old reading, chosen on purpose
        {Json{{"profile", Json{{"match_languages", Json::array({"pl"})}}}}, true, false},
    };
    for (const auto& c : cases) {
      INFO("match_languages " << c.overrides.at("profile").at("match_languages").dump());
      Fx f(c.overrides);
      const std::string conv = f.start(Json{{"locale", "en"}});
      f.say(conv, "minimal");
      REQUIRE(remaining(f) > 0);
      // a remark that is not understood is stored as the answer to the question that waits; one that is, is proposed and the proposal is refused
      for (const auto& probe : {std::make_pair("nie pytaj o zdrowie", c.polish_remark_understood), std::make_pair("do not ask about health", c.english_remark_understood)}) {
        f.say(conv, probe.first);
        const Message last = f.last(conv);
        CHECK(last.metadata.contains("action_proposal") == probe.second);
        CHECK((last.text.find("I stored your answer") != std::string::npos) == !probe.second);
        if (probe.second) f.say(conv, "no");
      }
    }
  }

  TEST_CASE("K2b-4: the sentence of a stored answer promises only what happens - the whole section is confirmed at its end - in both languages") {
    for (const char* lang : {"pl", "en"}) {
      Fx f;
      const std::string conv = f.start(Json{{"locale", lang}});
      f.say(conv, std::string(lang) == "pl" ? "szczegółowo" : "detailed");
      f.say(conv, std::string(lang) == "pl" ? "odpowiedź testowa" : "a test answer");
      const std::string text = f.last(conv).text;
      MESSAGE(std::string(lang), ": ", text);
      const std::string stored = std::string(lang) == "pl" ? "Zapisałem Twoją odpowiedź o:" : "I stored your answer about:";
      const std::string promise = std::string(lang) == "pl" ? "Potwierdzisz ją razem z całą sekcją, gdy ją skończymy." : "You will confirm it together with the whole section, when we finish it.";
      const std::string old_promise = std::string(lang) == "pl" ? "Czeka na Twoje potwierdzenie" : "waits for your confirmation";
      // the answer was taken as an answer (it names the question it was given to), and the sentence no longer says that a confirmation waits right now
      CHECK(text.find(stored) != std::string::npos);
      CHECK(text.find(promise) != std::string::npos);
      CHECK(text.find(old_promise) == std::string::npos);
      std::string what;
      CHECK_MESSAGE(!has_internal_name(text, &what), "internal name \"" << what << "\" in: " << text);
      CHECK(f.transport->requests().empty());
    }
  }
}
