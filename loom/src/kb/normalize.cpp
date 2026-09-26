// kb.h: Normalizer — match keys for PL + EN text (fold, light stemming,
// stop words, glossary). Tables come from the pack (lexicons/*.json); the
// algorithm is here.
#include <algorithm>

#include "loom/kb.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::kb {

namespace {

bool ends_with(std::string_view s, std::string_view suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Polish letters with diacritics (lowercase) mark a token as Polish.
bool has_polish_diacritic(std::string_view token) {
  static constexpr char32_t kPl[] = {U'ą', U'ć', U'ę', U'ł', U'ń', U'ó', U'ś', U'ź', U'ż'};
  for (char32_t c : utf8::decode(token)) {
    if (std::find(std::begin(kPl), std::end(kPl), c) != std::end(kPl)) return true;
  }
  return false;
}

void add_words(const Json& arr, std::set<std::string, std::less<>>& out) {
  if (!arr.is_array()) return;
  for (const auto& w : arr) {
    if (w.is_string()) out.insert(utf8::to_lower(w.get<std::string>()));
  }
}

}  // namespace

Normalizer::Normalizer(const Pack& pack) {
  const Json& st = pack.lexicon("stemming");
  if (const Json* f = json::find(st, "fold"); f && f->is_object()) {
    for (auto it = f->begin(); it != f->end(); ++it) {
      auto cps = utf8::decode(it.key());
      if (cps.size() == 1 && it.value().is_string()) fold_[cps[0]] = it.value().get<std::string>();
    }
  }
  auto load = [&](std::string_view lang, Stemmer& s) {
    const Json* j = json::find(st, lang);
    if (!j || !j->is_object()) return;
    s.min_token = static_cast<std::size_t>(json::get_int(*j, "min_token", 4));
    s.min_stem = static_cast<std::size_t>(json::get_int(*j, "min_stem", 3));
    if (const Json* r = json::find(*j, "rewrite"); r && r->is_array()) {
      for (const auto& x : *r) s.rewrite.emplace_back(json::get_string(x, "suffix"), json::get_string(x, "to"));
    }
    if (const Json* r = json::find(*j, "suffixes"); r && r->is_array()) {
      for (const auto& x : *r) {
        if (x.is_string()) s.suffixes.push_back(x.get<std::string>());
      }
    }
    if (const Json* r = json::find(*j, "exceptions"); r && r->is_object()) {
      for (auto it = r->begin(); it != r->end(); ++it) {
        if (it.value().is_string()) s.exceptions[it.key()] = it.value().get<std::string>();
      }
    }
    auto longer = [](const auto& a, const auto& b) { return a.size() > b.size() || (a.size() == b.size() && a < b); };
    std::stable_sort(s.suffixes.begin(), s.suffixes.end(), longer);
    std::stable_sort(s.rewrite.begin(), s.rewrite.end(),
                     [&](const auto& a, const auto& b) { return longer(a.first, b.first); });
  };
  load("pl", pl_);
  load("en", en_);
  for (const char* name : {"stopwords_base", "stopwords"}) {
    const Json& sw = pack.lexicon(name);
    for (const char* k : {"en", "pl", "code"}) add_words(sw[k], stop_);
    add_words(sw["pl"], pl_stop_);
  }
  // Folded forms are stop words too ("się" -> "sie").
  std::vector<std::string> folded;
  for (const auto& w : stop_) folded.push_back(fold(w));
  for (auto& w : folded) stop_.insert(std::move(w));
  const Json& gl = pack.lexicon("glossary");
  if (const Json* pairs = json::find(gl, "pairs"); pairs && pairs->is_array()) {
    for (const auto& p : *pairs) {
      if (!p.is_array() || p.size() != 2 || !p[0].is_string() || !p[1].is_string()) continue;
      std::string en = phrase_key(p[1].get<std::string>(), false);
      std::string pl = phrase_key(p[0].get<std::string>(), false);
      if (en.empty()) continue;
      if (!pl.empty()) glossary_.emplace(pl, en);
      glossary_.emplace(en, en);
    }
  }
}

std::string Normalizer::fold(std::string_view text) const {
  std::string out;
  out.reserve(text.size());
  for (char32_t c : utf8::to_lower(utf8::decode(text))) {
    auto it = fold_.find(c);
    if (it != fold_.end()) {
      out += it->second;
    } else {
      utf8::append(out, c);
    }
  }
  return out;
}

std::vector<std::string> Normalizer::tokens(std::string_view text) const {
  std::vector<std::string> out;
  std::u32string cur;
  bool has_alpha = false;
  auto flush = [&] {
    if (!cur.empty() && has_alpha) out.push_back(utf8::encode(cur));
    cur.clear();
    has_alpha = false;
  };
  for (char32_t c : utf8::decode(text)) {
    if (unicode::is_alnum(c)) {
      cur.push_back(unicode::simple_lower(c));
      if (unicode::is_alpha(c)) has_alpha = true;
    } else if ((c == U'+' || c == U'#') && has_alpha) {
      cur.push_back(c);  // "c++", "c#"
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool Normalizer::is_stopword(std::string_view token) const {
  if (stop_.count(token)) return true;
  return stop_.count(fold(token)) > 0;
}

std::string Normalizer::stem_with(const Stemmer& s, std::string_view token) const {
  if (auto it = s.exceptions.find(token); it != s.exceptions.end()) return it->second;
  if (utf8::length(token) < s.min_token) return std::string(token);
  std::string t(token);
  for (const auto& [suf, to] : s.rewrite) {
    if (ends_with(t, suf) && utf8::length(t) - utf8::length(suf) + utf8::length(to) >= s.min_stem) {
      t = t.substr(0, t.size() - suf.size()) + to;
      return t;
    }
  }
  for (const auto& suf : s.suffixes) {
    if (ends_with(t, suf) && utf8::length(t) - utf8::length(suf) >= s.min_stem) {
      return t.substr(0, t.size() - suf.size());
    }
  }
  return t;
}

std::string Normalizer::stem(std::string_view token, Lang lang) const {
  if (lang == Lang::Pl) return stem_with(pl_, token);
  if (lang == Lang::En) return stem_with(en_, token);
  if (has_polish_diacritic(token) || pl_.exceptions.count(token)) return stem_with(pl_, token);
  return stem_with(en_, token);
}

std::string Normalizer::match_key(std::string_view token, Lang lang) const {
  std::string low = utf8::to_lower(token);
  if (lang == Lang::Unknown || lang == Lang::Mixed) {
    lang = has_polish_diacritic(low) || pl_.exceptions.count(low) || pl_stop_.count(low) ? Lang::Pl : Lang::Unknown;
  }
  // Exceptions are keyed by surface form and already folded.
  if (lang == Lang::Pl) {
    if (auto it = pl_.exceptions.find(low); it != pl_.exceptions.end()) return it->second;
  }
  if (auto it = en_.exceptions.find(low); it != en_.exceptions.end() && lang != Lang::Pl) return it->second;
  return fold(stem(low, lang));
}

std::string Normalizer::phrase_key(std::string_view phrase, bool map_glossary) const {
  std::string out;
  for (const auto& t : tokens(phrase)) {
    if (is_stopword(t)) continue;
    std::string k = match_key(t);
    if (k.empty()) continue;
    if (!out.empty()) out += ' ';
    out += k;
  }
  if (map_glossary && !out.empty()) {
    if (auto it = glossary_.find(out); it != glossary_.end()) return it->second;
  }
  return out;
}

Lang Normalizer::guess_lang(std::string_view text) const {
  auto toks = tokens(text);
  if (toks.size() < 3) {
    bool pl = false;
    for (const auto& t : toks) pl = pl || has_polish_diacritic(t);
    return pl ? Lang::Pl : Lang::Unknown;
  }
  std::size_t pl = 0;
  std::size_t en = 0;
  for (const auto& t : toks) {
    if (pl_stop_.count(t) || has_polish_diacritic(t)) {
      ++pl;
    } else if (stop_.count(t)) {
      ++en;
    }
  }
  double n = static_cast<double>(toks.size());
  double fp = static_cast<double>(pl) / n;
  double fe = static_cast<double>(en) / n;
  if (fp >= 0.25 && fe >= 0.25) return Lang::Mixed;
  if (fp > fe && fp >= 0.05) return Lang::Pl;
  if (fe > 0.05) return Lang::En;
  return Lang::Unknown;
}

}  // namespace loom::kb
