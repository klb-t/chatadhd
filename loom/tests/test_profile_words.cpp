// M1-K2b-4: the control words of the interview (pack chat_ops, profile.words) are understood in every language the pack has words for, whatever the
// language of the conversation; the language of the conversation decides only the language of what is SAID. The interview is driven without a runtime
// (profile::Env over a database), so every word of every language can be tried in a conversation of every language, one profile per trial.
// Synthetic data only.
#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "actions/profile_ops.h"
#include "loom/db.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Words {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  Json pack;
  std::map<std::string, Json> settings;
  int users = 0;
  Words() {
    DbOptions options;
    options.enable_fts = false;
    db = loom::test::open_db(td.path() / "words.db", options);
    pack = unwrap(RuntimeProfile::builtin("chat_ops")).values();
  }
  // The environment of one call: sentences are named by their id and what fills them, so two calls can be compared without any language in the way.
  actions::profile::Env env(const std::string& lang, const Json& match = Json(nullptr)) {
    const Json fold_map = pack.at("fold");
    actions::profile::Env e{*db,
                            lang,
                            "local",
                            [lang](const std::string& id, const Json& vars) { return lang + ":" + id + json::dump(vars); },
                            [fold_map](const std::string& t) {
                              std::string out;
                              for (unsigned char c : t) out += static_cast<char>(c < 128 ? std::tolower(c) : c);
                              for (auto it = fold_map.begin(); it != fold_map.end(); ++it) {
                                const std::string from = it.key(), to = it.value().get<std::string>();
                                for (std::size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + to.size())) out.replace(at, from.size(), to);
                              }
                              return out;
                            },
                            [this](const std::string& key) { auto it = settings.find(key); return it == settings.end() ? Json(nullptr) : it->second; },
                            [this](const std::string& key, const Json& value) -> Status { settings[key] = value; return {}; },
                            pack.at("profile").at("words"),
                            nullptr};
    e.match_languages = match;
    return e;
  }
  // What the person gets for one word typed in a new profile with the level chosen and a question waiting: the sentence (named by its id and its
  // fillers) and the facts that say what happened, with no ids, times or revisions in them.
  Json said(const std::string& conv_lang, const std::string& word, const Json& match = Json(nullptr)) {
    const std::string user = "synthetic/words-" + std::to_string(++users);
    settings["profile_detail_level"] = "normal";  // the default of the configuration
    const auto setup = env(conv_lang);  // the preset under test is for the word only (a preset that cannot be read refuses every free message)
    unwrap(actions::profile::interview(setup, Json{{"op", "start"}, {"user_id", user}, {"in_conversation", false}}));
    unwrap(actions::profile::interview(setup, Json{{"op", "say"}, {"user_id", user}, {"text", "minimal"}}));
    auto r = actions::profile::interview(env(conv_lang, match), Json{{"op", "say"}, {"user_id", user}, {"text", word}});
    if (!r) return Json{{"error", r.error().message}};
    const Json& d = r->data;
    return Json{{"text", r->text},
                {"written", d.value("written", Json(nullptr))},
                {"remaining", d.value("remaining", Json(nullptr))},
                {"proposal", d.contains("proposal") && d.at("proposal").is_object()},
                {"answered", d.contains("answered")}};
  }
};

}  // namespace

TEST_SUITE("profile_words") {
  TEST_CASE("K2b-4: every control word of the pack means its operation in a conversation of any language of the pack - the whole possibility space, no runtime") {
    Words w;
    const Json words = w.pack.at("profile").at("words");
    std::vector<std::string> langs;
    for (auto op = words.begin(); op != words.end(); ++op)
      for (auto l = op.value().begin(); l != op.value().end(); ++l)
        if (std::find(langs.begin(), langs.end(), l.key()) == langs.end()) langs.push_back(l.key());
    REQUIRE(langs.size() >= 2);
    int tried = 0, crossed = 0;
    for (const char* preset : {"", "all"}) {
      for (const auto& conv_lang : langs) {
        const auto e = w.env(conv_lang, *preset ? Json(preset) : Json(nullptr));
        for (auto op = words.begin(); op != words.end(); ++op)
          for (auto l = op.value().begin(); l != op.value().end(); ++l) {
            const Json spoken = l.value();
            for (const auto& word : spoken) {
              INFO("operation " << op.key() << ": \"" << word.get<std::string>() << "\" (" << l.key() << ") in a " << conv_lang << " conversation, preset \"" << std::string(preset) << "\"");
              CHECK(actions::profile::control_op(e, word.get<std::string>()) == op.key());
              CHECK(actions::profile::control_op(e, "  " + word.get<std::string>() + " !") == op.key());  // as typed: spaces and a mark around it
              ++tried;
              crossed += l.key() == conv_lang ? 0 : 1;
            }
          }
      }
    }
    CHECK(tried >= 100);
    CHECK(crossed >= 40);
    // not a control word in any language: no operation (a word is the whole message, not a part of it)
    const auto e = w.env("en");
    for (const char* text : {"", "   ", "skip this section please", "pomiń to proszę", "opowiem Ci coś o sobie", "nigdy w życiu nie pytaj"})
      CHECK(actions::profile::control_op(e, text).empty());
  }

  TEST_CASE("K2b-4: the languages the words are understood in are a preset: 'conversation' keeps the old reading, a list names the languages, a bad value reads none") {
    Words w;
    using actions::profile::control_op;
    const std::vector<std::pair<const char*, const char*>> crossed = {{"en", "pomiń"}, {"pl", "skip"}, {"en", "nigdy"}, {"pl", "not now"}};
    for (const auto& [conv_lang, word] : crossed) {
      INFO("\"" << word << "\" in a " << conv_lang << " conversation");
      CHECK_FALSE(control_op(w.env(conv_lang), word).empty());
      CHECK_FALSE(control_op(w.env(conv_lang, Json("all")), word).empty());
      CHECK(control_op(w.env(conv_lang, Json("conversation")), word).empty());                   // the old reading, chosen on purpose
      CHECK(control_op(w.env("", Json("conversation")), word) == control_op(w.env(conv_lang), word));  // no conversation named: nothing to keep to
      CHECK_FALSE(control_op(w.env(conv_lang, Json::array({"pl", "en"})), word).empty());
      CHECK(control_op(w.env(conv_lang, Json::array({conv_lang})), word).empty());
      CHECK(control_op(w.env(conv_lang, Json::array()), word).empty());                         // an empty list reads nothing
      CHECK(control_op(w.env(conv_lang, Json("bogus")), word).empty());                         // never turned into a guess; the remark path says why
    }
    for (const auto& conv_lang : {"en", "pl"}) {
      const std::string own = conv_lang == std::string("pl") ? "pomiń" : "skip";
      CHECK(control_op(w.env(conv_lang, Json("conversation")), own) == "skip");
      CHECK(control_op(w.env(conv_lang, Json::array({conv_lang})), own) == "skip");
    }
  }

  // Four trials with the interview itself (a call costs seconds in a debug build): the word of the other language does what the word of the conversation's
  // language does, the preset 'conversation' stores it as the answer (the old reading), and a preset that cannot be read is named, not guessed.
  TEST_CASE("K2b-4: through the interview, 'pomiń' in an English conversation is 'skip'; with the preset 'conversation' it is the answer; a bad preset is an error") {
    Words w;
    const Json skip = w.said("en", "skip");
    CHECK(skip.at("answered") == false);
    CHECK(w.said("en", "pomiń") == skip);
    CHECK(w.said("en", "pomiń", Json("conversation")).at("answered") == true);
    const Json bad = w.said("en", "pomiń", Json("bogus"));
    CHECK(bad.contains("error"));
  }
}
