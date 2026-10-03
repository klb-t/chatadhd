// kb.h: Normalizer — match keys for PL + EN text (fold, light stemming,
// stop words, glossary). Tables come from the pack (lexicons/*.json); the
// algorithm is here (documented in include/loom/kb.h).
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

bool is_ascii_vowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }

bool has_vowel(std::string_view s) {
  for (char c : s) {
    if (is_ascii_vowel(c)) return true;
  }
  // Non-ASCII letters (Polish, accented) count as vowels here: the verbal
  // repair below only ever runs on English stems.
  for (char c : s) {
    if (static_cast<unsigned char>(c) >= 0x80) return true;
  }
  return false;
}

// "stor", "hop", "creat" ... : exactly one vowel group, ending consonant-
// vowel-consonant with the last consonant not w/x/y (Porter's *o).
bool short_cvc(std::string_view s) {
  if (s.size() < 3) return false;
  int groups = 0;
  bool in_vowel = false;
  for (char c : s) {
    if (static_cast<unsigned char>(c) >= 0x80) return false;
    bool v = is_ascii_vowel(c);
    if (v && !in_vowel) ++groups;
    in_vowel = v;
  }
  if (groups != 1) return false;
  char a = s[s.size() - 3], b = s[s.size() - 2], c = s[s.size() - 1];
  return !is_ascii_vowel(a) && is_ascii_vowel(b) && !is_ascii_vowel(c) && c != 'w' && c != 'x' && c != 'y';
}

void add_words(const Json& arr, std::set<std::string, std::less<>>& out) {
  if (!arr.is_array()) return;
  for (const auto& w : arr) {
    if (w.is_string()) out.insert(utf8::to_lower(w.get<std::string>()));
  }
}

std::vector<std::string> strings_of(const Json* j) {
  std::vector<std::string> out;
  if (!j || !j->is_array()) return out;
  for (const auto& x : *j) {
    if (x.is_string()) out.push_back(x.get<std::string>());
  }
  return out;
}

bool longer_first(const std::string& a, const std::string& b) {
  return a.size() > b.size() || (a.size() == b.size() && a < b);
}

std::vector<std::string> split_spaces(const std::string& s) {
  std::vector<std::string> out;
  std::size_t p = 0;
  while (p < s.size()) {
    std::size_t e = s.find(' ', p);
    if (e == std::string::npos) e = s.size();
    if (e > p) out.push_back(s.substr(p, e - p));
    p = e + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& v, std::size_t from, std::size_t n) {
  std::string out;
  for (std::size_t i = from; i < from + n && i < v.size(); ++i) {
    if (!out.empty()) out += ' ';
    out += v[i];
  }
  return out;
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
    s.suffixes = strings_of(json::find(*j, "suffixes"));
    s.keep_endings = strings_of(json::find(*j, "keep_endings"));
    for (auto& v : strings_of(json::find(*j, "verbal"))) s.verbal.insert(std::move(v));
    s.restore_e = strings_of(json::find(*j, "restore_e"));
    s.markers = strings_of(json::find(*j, "markers"));
    if (const Json* r = json::find(*j, "exceptions"); r && r->is_object()) {
      for (auto it = r->begin(); it != r->end(); ++it) {
        if (it.value().is_string()) s.exceptions[it.key()] = it.value().get<std::string>();
      }
    }
    std::stable_sort(s.suffixes.begin(), s.suffixes.end(), longer_first);
    std::stable_sort(s.markers.begin(), s.markers.end(), longer_first);
    std::stable_sort(s.rewrite.begin(), s.rewrite.end(),
                     [](const auto& a, const auto& b) { return longer_first(a.first, b.first); });
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
      std::string en = phrase_key(p[1].get<std::string>(), false, Lang::En);
      std::string pl = phrase_key(p[0].get<std::string>(), false, Lang::Pl);
      if (en.empty()) continue;
      // First pair wins for a key (file order), so the data stays in charge.
      glossary_.emplace(en, en);
      if (!pl.empty()) glossary_.emplace(pl, en);
      glossary_max_tokens_ = std::max({glossary_max_tokens_, split_spaces(en).size(), split_spaces(pl).size()});
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

bool Normalizer::has_pl_signal(std::string_view low) const {
  if (has_polish_diacritic(low) || pl_.exceptions.count(low) || pl_stop_.count(low)) return true;
  if (utf8::length(low) < pl_.min_token) return false;
  for (const auto& m : pl_.markers) {
    if (ends_with(low, m)) return true;
  }
  return false;
}

Lang Normalizer::token_lang(std::string_view low) const { return has_pl_signal(low) ? Lang::Pl : Lang::En; }

std::string Normalizer::stem_with(const Stemmer& s, std::string_view token) const {
  if (auto it = s.exceptions.find(token); it != s.exceptions.end()) return it->second;
  if (utf8::length(token) < s.min_token) return std::string(token);
  std::string t(token);
  for (const auto& [suf, to] : s.rewrite) {
    if (ends_with(t, suf) && utf8::length(t) - utf8::length(suf) + utf8::length(to) >= s.min_stem) {
      t = t.substr(0, t.size() - suf.size()) + to;
      break;
    }
  }
  for (const auto& suf : s.suffixes) {
    if (!ends_with(t, suf) || utf8::length(t) - utf8::length(suf) < s.min_stem) continue;
    bool blocked = false;
    for (const auto& k : s.keep_endings) {
      if (ends_with(k, suf) && ends_with(t, k)) blocked = true;
    }
    if (blocked) continue;
    std::string stem = t.substr(0, t.size() - suf.size());
    if (s.verbal.count(suf)) {
      if (!has_vowel(stem)) continue;  // "string", "bring"
      std::size_t n = stem.size();
      char last = stem[n - 1];
      if (n >= 2 && stem[n - 2] == last && !is_ascii_vowel(last) && last != 'l' && last != 's' && last != 'z' &&
          static_cast<unsigned char>(last) < 0x80) {
        stem.pop_back();  // "runn" -> "run", "mapp" -> "map"
      } else {
        bool restored = false;
        for (const auto& r : s.restore_e) {
          if (ends_with(stem, r)) {
            stem += 'e';  // "creat" -> "create"
            restored = true;
            break;
          }
        }
        if (!restored && short_cvc(stem)) stem += 'e';  // "stor" -> "store"
      }
    }
    return stem;
  }
  return t;
}

std::string Normalizer::stem(std::string_view token, Lang lang) const {
  if (lang != Lang::Pl && lang != Lang::En) lang = token_lang(token);
  return stem_with(lang == Lang::Pl ? pl_ : en_, token);
}

std::string Normalizer::key_as(std::string_view low, Lang lang) const {
  // English exceptions ("data", "status") hold even inside Polish text.
  if (lang == Lang::Pl && !has_polish_diacritic(low) && !pl_.exceptions.count(low) && en_.exceptions.count(low)) {
    lang = Lang::En;
  }
  return fold(stem_with(lang == Lang::Pl ? pl_ : en_, low));
}

std::string Normalizer::match_key(std::string_view token, Lang lang) const {
  std::string low = utf8::to_lower(token);
  if (lang != Lang::Pl && lang != Lang::En) lang = token_lang(low);
  return key_as(low, lang);
}

std::string Normalizer::phrase_key(std::string_view phrase, bool map_glossary, Lang lang) const {
  std::vector<std::string> toks = tokens(phrase);
  bool phrase_pl = lang == Lang::Pl;
  if (lang != Lang::Pl && lang != Lang::En) {
    for (const auto& t : toks) phrase_pl = phrase_pl || has_pl_signal(t);
  }
  std::vector<std::string> primary, en_keys, pl_keys;
  for (const auto& t : toks) {
    if (is_stopword(t)) continue;
    Lang tl = phrase_pl ? Lang::Pl : (lang == Lang::En ? Lang::En : token_lang(t));
    // A token with a Polish signal is Polish even in English text.
    if (tl == Lang::En && lang == Lang::En && has_polish_diacritic(t)) tl = Lang::Pl;
    std::string k = key_as(t, tl);
    if (k.empty()) continue;
    primary.push_back(k);
    if (map_glossary) {
      en_keys.push_back(key_as(t, Lang::En));
      pl_keys.push_back(key_as(t, Lang::Pl));
    }
  }
  if (!map_glossary || glossary_.empty()) return join(primary, 0, primary.size());
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < primary.size()) {
    bool hit = false;
    for (std::size_t n = std::min(glossary_max_tokens_, primary.size() - i); n >= 1 && !hit; --n) {
      for (const auto* keys : {&primary, &en_keys, &pl_keys}) {
        auto it = glossary_.find(join(*keys, i, n));
        if (it != glossary_.end()) {
          for (auto& w : split_spaces(it->second)) out.push_back(std::move(w));
          i += n;
          hit = true;
          break;
        }
      }
    }
    if (!hit) out.push_back(primary[i++]);
  }
  return join(out, 0, out.size());
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
