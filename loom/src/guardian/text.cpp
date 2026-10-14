#include "text.h"

#include <algorithm>
#include <map>
#include <set>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::guardian::text {
namespace {

char32_t fold_cp(char32_t cp, const std::map<char32_t, char32_t>& map) {
  const char32_t lower = unicode::simple_lower(cp);
  auto it = map.find(lower);
  return it == map.end() ? lower : it->second;
}

std::map<char32_t, char32_t> fold_table(const Json& fold_map) {
  std::map<char32_t, char32_t> out;
  if (!fold_map.is_object()) return out;
  for (auto it = fold_map.begin(); it != fold_map.end(); ++it) {
    if (!it.value().is_string()) continue;
    const auto from = utf8::decode(it.key());
    const auto to = utf8::decode(it.value().get<std::string>());
    if (from.size() == 1 && to.size() == 1) out[from[0]] = to[0];
  }
  return out;
}

bool boundary_char(char32_t c) { return c == ',' || c == ';' || c == '.' || c == '!' || c == '?' || c == ':' || c == 0x2014 || c == 0x2013 || c == '\n' || c == '(' || c == ')'; }

}  // namespace

std::string fold(std::string_view text, const Json& fold_map) {
  const auto table = fold_table(fold_map);
  std::u32string out;
  for (char32_t cp : utf8::decode(text)) out.push_back(fold_cp(cp, table));
  return utf8::encode(out);
}

std::vector<Tok> tokenize(const std::u32string& text, const Json& fold_map) {
  const auto table = fold_table(fold_map);
  std::vector<Tok> out;
  std::u32string cur;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size() && unicode::is_word(text[i])) {
      if (cur.empty()) start = i;
      cur.push_back(fold_cp(text[i], table));
    } else if (!cur.empty()) {
      out.push_back(Tok{utf8::encode(cur), start, i});
      cur.clear();
    }
  }
  return out;
}

std::vector<Phrase> compile(const std::vector<std::string>& phrases, const Json& fold_map) {
  std::vector<Phrase> out;
  const auto table = fold_table(fold_map);
  for (const auto& p : phrases) {
    Phrase ph;
    ph.source = p;
    std::u32string folded;
    for (char32_t cp : utf8::decode(p)) folded.push_back(fold_cp(cp, table));
    std::u32string cur;
    bool star = false;
    bool any_word = false;
    auto flush = [&] {
      if (cur.empty()) return;
      ph.words.push_back(utf8::encode(cur));
      ph.prefix.push_back(star);
      cur.clear();
      star = false;
      any_word = true;
    };
    for (std::size_t i = 0; i < folded.size(); ++i) {
      if (unicode::is_word(folded[i])) cur.push_back(folded[i]);
      else if (folded[i] == U'*' && !cur.empty()) {
        star = true;
        flush();
      } else flush();
    }
    flush();
    ph.raw = !any_word;
    if (ph.raw) ph.words = {utf8::encode(folded)};
    if (!ph.words.empty() && !ph.words[0].empty()) out.push_back(std::move(ph));
  }
  // The longest phrases first, so that "tak jakby" wins over "tak".
  std::stable_sort(out.begin(), out.end(), [](const Phrase& a, const Phrase& b) { return a.words.size() > b.words.size(); });
  return out;
}

std::vector<Hit> find(const std::vector<Tok>& toks, const std::vector<Phrase>& phrases) {
  std::vector<Hit> out;
  std::size_t i = 0;
  while (i < toks.size()) {
    bool matched = false;
    for (const auto& ph : phrases) {
      if (ph.raw || ph.words.empty() || i + ph.words.size() > toks.size()) continue;
      bool ok = true;
      for (std::size_t k = 0; k < ph.words.size() && ok; ++k) {
        const std::string& w = toks[i + k].folded;
        ok = ph.prefix[k] ? w.rfind(ph.words[k], 0) == 0 : w == ph.words[k];
      }
      if (!ok) continue;
      out.push_back(Hit{i, i + ph.words.size() - 1, ph.source});
      i += ph.words.size();
      matched = true;
      break;
    }
    if (!matched) ++i;
  }
  return out;
}

bool contains_raw(std::string_view lowered_text, const std::vector<Phrase>& phrases) {
  for (const auto& ph : phrases)
    if (ph.raw && !ph.words.empty() && lowered_text.find(ph.words[0]) != std::string_view::npos) return true;
  return false;
}

std::vector<std::string> strings_of(const Json* arr) {
  std::vector<std::string> out;
  if (arr && arr->is_array())
    for (const auto& v : *arr)
      if (v.is_string()) out.push_back(v.get<std::string>());
  return out;
}

std::vector<Span> noise_spans(const std::u32string& text, const Json& noise, const Json& fold_map) {
  std::vector<Span> out;
  const auto toks = tokenize(text, fold_map);
  if (toks.empty()) return out;
  auto taken = std::vector<bool>(toks.size(), false);
  auto add = [&](const std::string& category, std::size_t first, std::size_t last, std::size_t from_char, std::size_t to_char) {
    for (std::size_t k = first; k <= last; ++k) taken[k] = true;
    Span s;
    s.category = category;
    s.start = from_char;
    s.end = to_char;
    s.matched = utf8::encode(std::u32string_view(text).substr(from_char, to_char - from_char));
    out.push_back(std::move(s));
  };
  // Fillers: both languages at once (the language of a message is not known and a filler of the other language is still a filler).
  std::vector<std::string> fillers;
  if (const Json* f = json::find(noise, "fillers"); f && f->is_object())
    for (auto it = f->begin(); it != f->end(); ++it)
      for (const auto& p : strings_of(&it.value())) fillers.push_back(p);
  for (const auto& hit : find(toks, compile(fillers, fold_map))) add("filler", hit.first, hit.last, toks[hit.first].start, toks[hit.last].end);
  // Retractions: "scratch that" drops the clause before it and itself (the clause is short and non-empty; nothing else is guessed).
  if (const Json* r = json::find(noise, "restart"); r && r->is_object()) {
    const std::size_t max_words = static_cast<std::size_t>(json::get_int(*r, "max_clause_words", 12));
    std::vector<std::string> markers;
    if (const Json* m = json::find(*r, "markers"); m && m->is_object())
      for (auto it = m->begin(); it != m->end(); ++it)
        for (const auto& p : strings_of(&it.value())) markers.push_back(p);
    for (const auto& hit : find(toks, compile(markers, fold_map))) {
      if (taken[hit.first]) continue;
      std::size_t first = hit.first;
      // walk back over the words of the clause: stop at a boundary character between tokens
      while (first > 0) {
        bool boundary = false;
        for (std::size_t c = toks[first - 1].end; c < toks[first].start; ++c)
          if (boundary_char(text[c])) boundary = true;
        if (boundary) break;
        --first;
      }
      const std::size_t words = hit.first - first;
      if (words == 0 || words > max_words) continue;
      bool free = true;
      for (std::size_t k = first; k <= hit.last; ++k) free = free && !taken[k];
      if (!free) continue;
      add("restart", first, hit.last, toks[first].start, toks[hit.last].end);
    }
  }
  // Repetitions: an n-gram repeated at once (longest first); the second copy is the noise.
  const Json* rep = json::find(noise, "repetition");
  const std::size_t max_n = rep ? static_cast<std::size_t>(json::get_int(*rep, "max_ngram", 3)) : 3;
  for (std::size_t n = max_n; n >= 1; --n) {
    for (std::size_t i = 0; i + 2 * n <= toks.size(); ++i) {
      bool same = true, free = true, digits = true;
      for (std::size_t k = 0; k < n && same; ++k) {
        same = toks[i + k].folded == toks[i + n + k].folded;
        free = free && !taken[i + n + k];
        for (char32_t cp : utf8::decode(toks[i + k].folded)) digits = digits && unicode::is_decimal(cp);
      }
      if (!same || !free || digits) continue;
      add("repetition", i + n, i + 2 * n - 1, toks[i + n].start, toks[i + 2 * n - 1].end);
      i += 2 * n - 1;
    }
    if (n == 1) break;
  }
  std::sort(out.begin(), out.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
  return out;
}

std::string without(const std::u32string& text, std::vector<std::pair<std::size_t, std::size_t>> spans) {
  std::sort(spans.begin(), spans.end());
  std::u32string out;
  std::size_t pos = 0;
  for (const auto& [a, b] : spans) {
    if (b <= pos) continue;
    const std::size_t from = std::max(a, pos);
    out.append(text, pos, from - pos);
    pos = std::max(pos, std::min(b, text.size()));
  }
  if (pos < text.size()) out.append(text, pos, std::u32string::npos);
  // Tidy: collapse blanks, no blank before punctuation, no punctuation run left at the start, no doubled commas.
  std::u32string tidy;
  for (char32_t c : out) {
    const bool blank = c == U' ' || c == U'\t';
    if (blank && !tidy.empty() && tidy.back() == U' ') continue;
    if ((c == U',' || c == U'.' || c == U';' || c == U':' || c == U'!' || c == U'?') && !tidy.empty() && tidy.back() == U' ') tidy.pop_back();
    if (c == U',' && !tidy.empty() && tidy.back() == U',') continue;
    tidy.push_back(blank ? U' ' : c);
  }
  std::size_t b = 0;
  while (b < tidy.size() && (tidy[b] == U' ' || tidy[b] == U',' || tidy[b] == U';' || tidy[b] == U'\n')) ++b;
  std::size_t e = tidy.size();
  while (e > b && (tidy[e - 1] == U' ' || tidy[e - 1] == U',')) --e;
  return utf8::encode(std::u32string_view(tidy).substr(b, e - b));
}

}  // namespace loom::guardian::text
