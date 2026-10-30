// Text segmentation and fingerprints (R102 G1). See loom/text_fragments.h.
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "loom/text_fragments.h"
#include "loom/util/sha256.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"
#include "text/text_internal.h"

namespace loom::text {

namespace {
#include "text/text_pack.inc"
}  // namespace

std::string_view builtin_pack_source() { return kTextPackSource; }

const Json& builtin_pack() {
  static const Json pack = Json::parse(kTextPack);
  return pack;
}

Json merge_pack(const Json& base, const Json& overlay) {
  if (!base.is_object() || !overlay.is_object()) return overlay;
  Json out = base;
  for (auto it = overlay.begin(); it != overlay.end(); ++it) {
    if (it.value().is_null()) {
      out.erase(it.key());
    } else if (out.contains(it.key()) && out[it.key()].is_object() && it.value().is_object()) {
      Json merged = merge_pack(out[it.key()], it.value());
      out[it.key()] = std::move(merged);
    } else {
      out[it.key()] = it.value();
    }
  }
  return out;
}

namespace detail {

Cp decode_at(std::string_view s, std::size_t i) noexcept {
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) return {b0, 1};
  std::size_t len = 0;
  char32_t cp = 0, min = 0;
  if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1F; min = 0x80; }
  else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; min = 0x800; }
  else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; min = 0x10000; }
  else return {utf8::kReplacement, 1};
  if (i + len > s.size()) return {utf8::kReplacement, 1};
  for (std::size_t k = 1; k < len; ++k) {
    const auto b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0) != 0x80) return {utf8::kReplacement, 1};
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {utf8::kReplacement, 1};
  return {cp, len};
}

}  // namespace detail

using detail::Cp;
using detail::decode_at;

namespace {

std::u32string first_cp_set(const Json& arr) {
  std::u32string out;
  if (!arr.is_array()) return out;
  for (const auto& v : arr) {
    if (!v.is_string()) continue;
    const std::string s = v.get<std::string>();
    if (s.empty()) continue;
    out.push_back(decode_at(s, 0).cp);
  }
  return out;
}

bool contains(const std::u32string& set, char32_t c) { return set.find(c) != std::u32string::npos; }

bool is_upper(char32_t c) { return unicode::is_cased(c) && unicode::simple_lower(c) != c; }
bool is_lower(char32_t c) { return unicode::is_cased(c) && unicode::simple_upper(c) != c; }
bool is_mark_like(char32_t c) {
  // combining marks, ZWJ/ZWNJ and variation selectors continue the preceding word (approximation: case-ignorable
  // codepoints from U+0300 up that are not quotation punctuation)
  return c >= 0x300 && unicode::is_case_ignorable(c) && c != U'’' && c != U'‘' && c != U'·';
}

const std::vector<std::string>& known_classes() {
  static const std::vector<std::string> k{"upper", "lower", "digit", "alpha", "uncased", "opening", "punct", "any"};
  return k;
}

bool class_match(std::string_view cls, char32_t c, const std::u32string& openers) {
  if (cls == "any") return true;
  if (cls == "upper") return is_upper(c);
  if (cls == "lower") return is_lower(c);
  if (cls == "digit") return unicode::is_decimal(c) || unicode::is_digit(c);
  if (cls == "alpha") return unicode::is_alpha(c);
  if (cls == "uncased") return unicode::is_alpha(c) && !unicode::is_cased(c);
  if (cls == "opening") return contains(openers, c);
  if (cls == "punct") return !unicode::is_alnum(c) && !unicode::is_space(c);
  return false;
}

std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
  return out;
}

const std::vector<std::string>& known_methods() {
  static const std::vector<std::string> k{"document", "separator", "sentence", "word"};
  return k;
}

// Methods return fragment START positions inside [lo, hi); the first fragment always starts at lo.
using Starts = std::vector<std::size_t>;

std::size_t skip_space(std::string_view s, std::size_t i, std::size_t hi) {
  while (i < hi) {
    const Cp c = decode_at(s, i);
    if (!unicode::is_space(c.cp)) break;
    i += c.len;
  }
  return i;
}

Starts separator_starts(std::string_view s, std::size_t lo, std::size_t hi, const Json& p) {
  const auto min_nl = p.value("min_newlines", static_cast<std::int64_t>(2));
  Starts out;
  bool content = false;
  std::size_t i = lo;
  while (i < hi) {
    const Cp c = decode_at(s, i);
    if (!unicode::is_space(c.cp)) {
      content = true;
      i += c.len;
      continue;
    }
    std::size_t e = i;
    std::int64_t nl = 0;
    while (e < hi) {
      const Cp w = decode_at(s, e);
      if (!unicode::is_space(w.cp)) break;
      if (w.cp == U'\n') ++nl;
      e += w.len;
    }
    if (content && e < hi && nl >= std::max<std::int64_t>(min_nl, 1)) out.push_back(e);
    i = e;
  }
  return out;
}

struct SentenceRules {
  std::u32string terminators, closers, openers;
  bool require_space_after = true, case_sensitive = false, initials = true, break_on_newline = false;
  std::vector<std::string> next_start;
  std::unordered_set<std::string> abbreviations;
};

SentenceRules sentence_rules(const Json& p) {
  SentenceRules r;
  r.terminators = first_cp_set(p.value("terminators", Json::array()));
  r.closers = first_cp_set(p.value("closers", Json::array()));
  r.openers = first_cp_set(p.value("openers", Json::array()));
  r.require_space_after = p.value("require_space_after", true);
  r.case_sensitive = p.value("abbreviations_case_sensitive", false);
  r.initials = p.value("initials_are_abbreviations", true);
  r.break_on_newline = p.value("break_on_newline", false);
  for (const auto& v : p.value("next_start", Json::array()))
    if (v.is_string()) r.next_start.push_back(v.get<std::string>());
  for (const auto& v : p.value("abbreviations", Json::array()))
    if (v.is_string()) r.abbreviations.insert(r.case_sensitive ? v.get<std::string>() : utf8::to_lower(v.get<std::string>()));
  return r;
}

// The token right before byte position `p` (word codepoints and inner dots), e.g. "m.in" in "m.in.".
std::string token_before(std::string_view s, std::size_t lo, std::size_t p) {
  std::size_t b = p;
  while (b > lo) {
    std::size_t q = b - 1;
    while (q > lo && (static_cast<unsigned char>(s[q]) & 0xC0) == 0x80) --q;
    const Cp c = decode_at(s, q);
    if (q + c.len != b) break;  // an invalid tail byte: stop
    if (!(unicode::is_word(c.cp) || c.cp == U'.' || is_mark_like(c.cp))) break;
    b = q;
  }
  std::string t(s.substr(b, p - b));
  while (!t.empty() && t.front() == '.') t.erase(t.begin());
  return t;
}

Starts sentence_starts(std::string_view s, std::size_t lo, std::size_t hi, const Json& p) {
  const SentenceRules r = sentence_rules(p);
  Starts out;
  bool content = false;
  std::size_t i = lo;
  while (i < hi) {
    const Cp c = decode_at(s, i);
    if (r.break_on_newline && c.cp == U'\n' && content) {
      const std::size_t e = skip_space(s, i, hi);
      if (e < hi) out.push_back(e);
      content = false;
      i = e;
      continue;
    }
    if (!contains(r.terminators, c.cp)) {
      if (!unicode::is_space(c.cp)) content = true;
      i += c.len;
      continue;
    }
    // a run of terminators, then closers
    std::size_t q = i;
    std::size_t n_term = 0;
    while (q < hi) {
      const Cp t = decode_at(s, q);
      if (!contains(r.terminators, t.cp)) break;
      ++n_term;
      q += t.len;
    }
    const bool lone_dot = n_term == 1 && c.cp == U'.';
    while (q < hi) {
      const Cp t = decode_at(s, q);
      if (!contains(r.closers, t.cp)) break;
      q += t.len;
    }
    content = true;
    if (q >= hi) break;
    if (r.require_space_after && !unicode::is_space(decode_at(s, q).cp)) {
      i = q;
      continue;
    }
    const std::size_t next = skip_space(s, q, hi);
    if (next >= hi) break;
    bool cut = true;
    if (!r.next_start.empty()) {
      const char32_t n = decode_at(s, next).cp;
      cut = std::any_of(r.next_start.begin(), r.next_start.end(),
                        [&](const std::string& cls) { return class_match(cls, n, r.openers); });
    }
    if (cut && lone_dot) {
      const std::string tok = token_before(s, lo, i);
      if (!tok.empty()) {
        if (r.abbreviations.count(r.case_sensitive ? tok : utf8::to_lower(tok))) cut = false;
        if (r.initials && utf8::length(tok) == 1 && is_upper(decode_at(tok, 0).cp)) cut = false;
      }
    }
    if (cut) {
      out.push_back(next);
      content = false;
    }
    i = next;
  }
  return out;
}

Starts word_starts(std::string_view s, std::size_t lo, std::size_t hi, const Json& p) {
  const std::u32string joiners = first_cp_set(p.value("joiners", Json::array()));
  const bool split_punct = p.value("split_punctuation", true);
  Starts tokens;
  enum { None, Word, Punct } prev = None;
  std::size_t i = lo;
  while (i < hi) {
    const Cp c = decode_at(s, i);
    if (unicode::is_space(c.cp)) {
      prev = None;
      i += c.len;
      continue;
    }
    const bool word = unicode::is_word(c.cp);
    if (word) {
      if (prev != Word) tokens.push_back(i);
      prev = Word;
    } else if (prev == Word && is_mark_like(c.cp)) {
      // continues the word
    } else if (prev == Word && contains(joiners, c.cp) && i + c.len < hi &&
               unicode::is_word(decode_at(s, i + c.len).cp)) {
      // a joiner between word codepoints stays inside the word ("don't", "biało-czerwony")
    } else {
      if (split_punct || prev != Punct) tokens.push_back(i);
      prev = Punct;
    }
    i += c.len;
  }
  Starts out;
  for (std::size_t k = 1; k < tokens.size(); ++k) out.push_back(tokens[k]);
  return out;
}

}  // namespace

Json Fragment::to_json() const {
  return Json{{"level", level}, {"begin", begin}, {"end", end}, {"core_begin", core_begin}, {"core_end", core_end},
              {"fingerprint", fingerprint}};
}

Json Segmentation::to_json() const {
  Json f = Json::array();
  for (const auto& x : fragments) f.push_back(x.to_json());
  return Json{{"level", level}, {"language", language}, {"fragments", f}, {"advisories", advisories}};
}

Result<Segmenter> Segmenter::from_pack(const Json& pack, std::string_view language) {
  if (!pack.is_object()) return Error(Errc::InvalidArgument, "text pack: an object expected");
  const Json langs = pack.value("languages", Json::object());
  if (!langs.is_object()) return Error(Errc::InvalidArgument, "text pack: languages must be an object");
  const std::string def = pack.value("default_language", std::string("*"));
  Segmenter seg;
  seg.pack_ = pack;
  seg.language_ = std::string(language);
  Json rules = langs.contains(def) ? langs[def] : Json::object();
  std::string lang(language);
  std::string used;
  if (!lang.empty() && langs.contains(lang)) {
    used = lang;
  } else if (const auto dash = lang.find_first_of("-_"); !lang.empty() && dash != std::string::npos &&
                                                          langs.contains(lang.substr(0, dash))) {
    used = lang.substr(0, dash);
    seg.advisories_.push_back("Język „" + lang + "”: użyto reguł języka „" + used + "”.");
  } else if (!lang.empty() && lang != def) {
    seg.advisories_.push_back("Język „" + lang + "” nie ma reguł w paczce podziału tekstu; użyto reguł „" + def +
                              "”. Reguły dla nowego języka dodaje się w danych (languages." + lang + ").");
  }
  if (!used.empty() && used != def) rules = merge_pack(rules, langs[used]);
  seg.rules_ = rules;
  // validate the open values the code has to understand
  const Json levels = pack.value("levels", Json::object());
  for (auto it = levels.begin(); it != levels.end(); ++it) {
    const std::string m = it.value().value("method", std::string());
    if (std::find(known_methods().begin(), known_methods().end(), m) == known_methods().end())
      return Error(Errc::InvalidArgument, "text pack: level " + it.key() + " uses method \"" + m +
                                              "\"; this build knows: " + join(known_methods()));
  }
  const Json sent = rules.value("sentence", Json::object());
  for (const auto& v : sent.value("next_start", Json::array())) {
    const std::string c = v.is_string() ? v.get<std::string>() : v.dump();
    if (std::find(known_classes().begin(), known_classes().end(), c) == known_classes().end())
      return Error(Errc::InvalidArgument, "text pack: sentence.next_start class \"" + c +
                                              "\" is unknown; this build knows: " + join(known_classes()));
  }
  const Json fp = pack.value("fingerprint", Json::object());
  if (fp.value("hash", std::string("sha256")) != "sha256")
    return Error(Errc::InvalidArgument, "text pack: fingerprint.hash \"" + fp.value("hash", std::string()) +
                                            "\"; this build knows: sha256");
  const Json norm = pack.value("normalization", Json::object());
  static const std::map<std::string, std::vector<std::string>> kNorm{
      {"case", {"lower", "upper", "keep"}}, {"whitespace", {"collapse", "drop", "keep"}}, {"punctuation", {"keep", "drop"}}};
  for (const auto& [key, allowed] : kNorm) {
    if (!norm.contains(key)) continue;
    const std::string v = norm[key].is_string() ? norm[key].get<std::string>() : norm[key].dump();
    if (std::find(allowed.begin(), allowed.end(), v) == allowed.end())
      return Error(Errc::InvalidArgument, "text pack: normalization." + key + " \"" + v + "\"; this build knows: " + join(allowed));
  }
  return seg;
}

Result<Segmentation> Segmenter::segment(std::string_view text, std::string_view level) const {
  Segmentation out;
  out.level = std::string(level);
  out.language = language_;
  out.advisories = advisories_;
  // the parent chain, root first
  const Json levels = pack_.value("levels", Json::object());
  std::vector<std::string> chain;
  std::string cur(level);
  while (cur != "document") {
    if (std::find(chain.begin(), chain.end(), cur) != chain.end())
      return Error(Errc::InvalidArgument, "text pack: the parent chain of level " + std::string(level) + " has a cycle");
    if (!levels.contains(cur) || !levels[cur].is_object())
      return Error(Errc::NotFound, "text pack: no level \"" + cur + "\"");
    chain.push_back(cur);
    cur = levels[cur].value("parent", std::string("document"));
  }
  std::reverse(chain.begin(), chain.end());
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  if (!text.empty()) ranges.emplace_back(0, text.size());
  for (const auto& lv : chain) {
    const Json def = levels[lv];
    const std::string method = def.value("method", std::string());
    Json params = rules_.value(method, Json::object());
    params = merge_pack(params, def);
    std::vector<std::pair<std::size_t, std::size_t>> next;
    for (const auto& [lo, hi] : ranges) {
      Starts st;
      if (method == "separator") st = separator_starts(text, lo, hi, params);
      else if (method == "sentence") st = sentence_starts(text, lo, hi, params);
      else if (method == "word") st = word_starts(text, lo, hi, params);
      std::size_t b = lo;
      for (std::size_t x : st) {
        if (x <= b || x >= hi) continue;
        next.emplace_back(b, x);
        b = x;
      }
      next.emplace_back(b, hi);
    }
    ranges = std::move(next);
  }
  for (const auto& [b, e] : ranges) {
    Fragment f;
    f.level = out.level;
    f.begin = b;
    f.end = e;
    f.core_begin = skip_space(text, b, e);
    std::size_t ce = f.core_begin, i = f.core_begin;
    while (i < e) {
      const Cp c = decode_at(text, i);
      i += c.len;
      if (!unicode::is_space(c.cp)) ce = i;
    }
    f.core_end = ce;
    f.fingerprint = fingerprint(text.substr(b, e - b));
    out.fragments.push_back(std::move(f));
  }
  return out;
}

std::string Segmenter::normalize(std::string_view raw) const {
  const Json norm = pack_.value("normalization", Json::object());
  const std::string cs = norm.value("case", std::string("lower"));
  const std::string ws = norm.value("whitespace", std::string("collapse"));
  const std::string pu = norm.value("punctuation", std::string("keep"));
  const bool strip = norm.value("strip", true);
  std::u32string cps = utf8::decode(utf8::repair(raw));
  if (pu == "drop") {
    std::u32string k;
    for (char32_t c : cps)
      if (unicode::is_alnum(c) || unicode::is_space(c) || c == U'_' || is_mark_like(c)) k.push_back(c);
    cps = std::move(k);
  }
  if (cs == "lower") cps = utf8::to_lower(cps);
  std::string s = utf8::encode(cps);
  if (cs == "upper") s = utf8::to_upper(s);
  if (ws != "keep") {
    std::u32string in = utf8::decode(s), k;
    bool space = false;
    for (char32_t c : in) {
      if (unicode::is_space(c)) {
        space = true;
        continue;
      }
      if (space && ws == "collapse" && !k.empty()) k.push_back(U' ');
      space = false;
      k.push_back(c);
    }
    if (space && ws == "collapse" && !strip && !k.empty()) k.push_back(U' ');
    if (ws == "collapse" && !strip && !in.empty() && unicode::is_space(in.front())) k.insert(k.begin(), U' ');
    s = utf8::encode(k);
  }
  if (strip) s = std::string(utf8::strip(s));
  return s;
}

std::string Segmenter::fingerprint(std::string_view s) const {
  const Json fp = pack_.value("fingerprint", Json::object());
  std::string hex = Sha256::hex(normalize(s));
  const auto n = fp.value("hex_chars", static_cast<std::int64_t>(0));
  if (n > 0 && static_cast<std::size_t>(n) < hex.size()) hex.resize(static_cast<std::size_t>(n));
  return "sha256:" + hex;
}

std::vector<std::string> fragment_texts(std::string_view text, const Segmentation& s) {
  std::vector<std::string> out;
  out.reserve(s.fragments.size());
  for (const auto& f : s.fragments) out.emplace_back(text.substr(f.begin, f.end - f.begin));
  return out;
}

}  // namespace loom::text
