// Independent deterministic before/after probe of the native normalizer.
// No implementation is reproduced here: every observation calls Normalizer.
// Builtin lexicon strings and synthetic boundary cases are public inputs.
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "loom/kb.h"
#include "loom/util/fs.h"

namespace {
using loom::Json;
using loom::kb::Lang;
using Strings = std::set<std::string, std::less<>>;

template <class T>
T take(loom::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

void collect_strings(const Json& value, Strings& out) {
  if (value.is_string()) out.insert(value.get<std::string>());
  else if (value.is_array()) {
    for (const auto& item : value) collect_strings(item, out);
  } else if (value.is_object()) {
    for (const auto& item : value.items()) collect_strings(item.value(), out);
  }
}

Json corpus_of(const loom::kb::Pack& pack) {
  Strings tokens, phrases;
  const Json& stemming = pack.lexicon("stemming");
  for (const char* language : {"pl", "en"}) {
    const Json& table = stemming.at(language);
    for (const char* field : {"suffixes", "rewrite", "markers", "keep_endings", "verbal", "restore_e"}) {
      if (const Json* value = loom::json::find(table, field)) collect_strings(*value, tokens);
    }
    if (const Json* exceptions = loom::json::find(table, "exceptions")) {
      for (const auto& item : exceptions->items()) {
        tokens.insert(item.key());
        collect_strings(item.value(), tokens);
      }
    }
    // Exercise suffix matching on long real-shaped inputs, rather than only
    // testing suffix strings that may be below a recipe's minimum length.
    Strings suffixes;
    if (const Json* list = loom::json::find(table, "suffixes")) collect_strings(*list, suffixes);
    if (const Json* list = loom::json::find(table, "rewrite")) {
      for (const Json& item : *list) collect_strings(item.at("suffix"), suffixes);
    }
    for (const std::string& suffix : suffixes) {
      for (const char* base : {"program", "graph", "run", "store"}) tokens.insert(base + suffix);
    }
  }
  if (const Json* fold = loom::json::find(stemming, "fold")) {
    for (const auto& item : fold->items()) {
      tokens.insert(item.key());
      collect_strings(item.value(), tokens);
    }
  }
  for (const char* lexicon : {"stopwords_base", "stopwords"}) {
    const Json& table = pack.lexicon(lexicon);
    for (const char* language : {"pl", "en", "code"}) {
      if (const Json* value = loom::json::find(table, language)) collect_strings(*value, tokens);
    }
  }
  if (const Json* pairs = loom::json::find(pack.lexicon("glossary"), "pairs")) {
    for (const Json& pair : *pairs) {
      for (const Json& phrase : pair) {
        const std::string text = phrase.get<std::string>();
        phrases.insert(text);
        phrases.insert("prefix " + text + " suffix");
        const loom::kb::Normalizer normalizer(pack);
        for (const std::string& token : normalizer.tokens(text)) tokens.insert(token);
      }
    }
  }
  for (const char* token : {
           "running", "mapping", "stored", "created", "hopping", "filing", "falling", "missing", "fizzing",
           "snowing", "boxing", "playing", "string", "bring", "class", "status", "analysis", "data",
           "graph", "graf", "grafu", "wersji", "ŻÓŁĆ", "się", "zażółć", "gęślą", "jaźń", "C++", "c#",
           "123", "abc123", "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż", "w", "x", "y", "l", "s", "z"})
    tokens.insert(token);
  for (const char* phrase : {
           "", "qxv", "qxv qxv", "się", "się qxv", "the", "the qxv", "się the", "się the qxv",
           "się the qxv qxv", "się the qxv qxv qxv", "ŻÓŁĆ", "ŻÓŁĆ qxv", "ŻÓŁĆ qxv qxv",
           "grafu wiedzy", "knowledge graph", "czat ADHD", "chat ADHD", "running mapping stored created",
           "string bring falling missing fizzing snowing boxing playing", "123 456", "C++ and c#", "grafu the knowledge"})
    phrases.insert(phrase);
  // .25 mixed, .05 single-language and <3-token boundaries, on both sides.
  for (const std::size_t count : {2u, 3u, 4u, 5u, 19u, 20u, 21u, 39u, 40u, 41u}) {
    for (const std::string& lead : {std::string("się"), std::string("the"), std::string("się the")}) {
      std::string text = lead;
      const std::size_t initial = lead == "się the" ? 2 : 1;
      for (std::size_t n = initial; n < count; ++n) text += " qxv";
      phrases.insert(std::move(text));
    }
  }
  return Json{{"schema", "loom.normalizer_snapshot_corpus/1"}, {"tokens", tokens}, {"phrases", phrases}};
}

Json observe(const loom::kb::Normalizer& normalizer, const Json& corpus) {
  if (!corpus.is_object() || loom::json::get_string(corpus, "schema") != "loom.normalizer_snapshot_corpus/1" ||
      !corpus.at("tokens").is_array() || !corpus.at("phrases").is_array())
    throw std::runtime_error("expected loom.normalizer_snapshot_corpus/1 with token and phrase arrays");
  Json tokens = Json::array(), phrases = Json::array();
  const std::array<std::pair<const char*, Lang>, 3> modes{{{"auto", Lang::Unknown}, {"pl", Lang::Pl}, {"en", Lang::En}}};
  for (const Json& input : corpus.at("tokens")) {
    const std::string token = input.get<std::string>();
    Json stems = Json::object(), keys = Json::object();
    for (const auto& [name, language] : modes) {
      stems[name] = normalizer.stem(token, language);
      keys[name] = normalizer.match_key(token, language);
    }
    tokens.push_back(Json{{"token", token}, {"token_lang", loom::kb::to_string(normalizer.token_lang(token))},
                          {"fold", normalizer.fold(token)}, {"stem", stems}, {"match_key", keys},
                          {"is_stopword", normalizer.is_stopword(token)}});
  }
  for (const Json& input : corpus.at("phrases")) {
    const std::string phrase = input.get<std::string>();
    Json keys = Json::object();
    for (const auto& [name, language] : modes) {
      keys[name] = Json{{"glossary", normalizer.phrase_key(phrase, true, language)},
                        {"without_glossary", normalizer.phrase_key(phrase, false, language)}};
    }
    phrases.push_back(Json{{"phrase", phrase}, {"tokens", normalizer.tokens(phrase)}, {"phrase_key", keys},
                           {"guess_lang", loom::kb::to_string(normalizer.guess_lang(phrase))}});
  }
  return Json{{"schema", "loom.normalizer_snapshot/1"}, {"tokens", tokens}, {"phrases", phrases}};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto pack = take(loom::kb::Pack::load_builtin());
    if (argc == 2 && std::string_view(argv[1]) == "--corpus") {
      std::cout << loom::json::canonical(corpus_of(*pack)) << '\n';
      return 0;
    }
    if (argc > 2) throw std::runtime_error("usage: normalizer_snapshot [--corpus|corpus.json]");
    const Json corpus = argc == 2 ? take(loom::json::parse(take(loom::fsutil::read_file(argv[1])))) : corpus_of(*pack);
    std::cout << loom::json::canonical(observe(loom::kb::Normalizer(*pack), corpus)) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "normalizer snapshot: " << error.what() << '\n';
    return 1;
  }
}
