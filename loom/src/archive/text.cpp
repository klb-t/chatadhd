// Text utilities for the archive pipeline: tokenisation, stop words (PL+EN),
// sentence splitting, dates, identifiers. Pure functions.
#include <cctype>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "archive/profile.h"
#include "loom/util/sha256.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::archive {
namespace {

bool all_digits(std::u32string_view s) {
  for (char32_t c : s) {
    if (!unicode::is_decimal(c) && !unicode::is_digit(c)) return false;
  }
  return !s.empty();
}

bool is_upper_cp(char32_t c) { return unicode::simple_lower(c) != c; }

}  // namespace

std::vector<std::string> tokenize(std::string_view text) {
  std::vector<std::string> out;
  std::u32string cur;
  auto flush = [&] {
    if (!cur.empty() && !all_digits(cur)) out.push_back(utf8::encode(cur));
    cur.clear();
  };
  for (char32_t c : utf8::decode(text)) {
    if (unicode::is_alnum(c)) {
      cur.push_back(unicode::simple_lower(c));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool is_stopword(std::string_view lower_token, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  return scope.get().stopword(lower_token);
}

std::vector<std::string> content_tokens(std::string_view text, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  std::vector<std::string> out;
  for (auto& t : tokenize(text)) {
    if (utf8::length(t) < static_cast<std::size_t>(policy.integer("/text/min_token_codepoints")) || is_stopword(t, profile)) continue;
    out.push_back(std::move(t));
  }
  return out;
}

std::vector<std::string> candidate_terms(std::string_view text, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  // Unigrams plus bigrams of adjacent content tokens separated only by
  // spaces or a hyphen ("knowledge graph", "append-only"); paths, "::",
  // punctuation and line breaks end a phrase.
  std::vector<std::string> out;
  const auto& orders = policy.value("/text_item_closure/phrase_orders");
  const auto separators = utf8::decode(policy.text("/text_item_closure/phrase_separators"));
  const auto separator = policy.text("/text_item_closure/phrase_output_separator");
  const auto distinct = policy.value("/text_item_closure/phrase_distinct_adjacent").get<bool>();
  std::size_t max_order = 0;
  for (const auto& order : orders) max_order = std::max(max_order, order.get<std::size_t>());
  std::deque<std::string> previous;
  bool joinable = false;  // only spaces/hyphen since the previous token
  std::u32string cur;
  auto flush = [&] {
    if (cur.empty()) return;
    std::string t = utf8::encode(cur);
    cur.clear();
    bool content = !all_digits(utf8::decode(t)) && utf8::length(t) >= static_cast<std::size_t>(policy.integer("/text/min_token_codepoints")) && !is_stopword(t, profile);
    if (!content) {
      previous.clear();
      joinable = false;
      return;
    }
    if (!joinable) previous.clear();
    previous.push_back(t);
    while (previous.size() > max_order) previous.pop_front();
    for (const auto& order : orders) {
      const auto count = order.get<std::size_t>();
      if (count > previous.size()) continue;
      const auto start = previous.size() - count;
      bool eligible = true;
      std::string phrase;
      for (std::size_t k = start; k < previous.size(); ++k) {
        if (distinct && k > start && previous[k] == previous[k - 1]) eligible = false;
        if (k > start) phrase += separator;
        phrase += previous[k];
      }
      if (eligible) out.push_back(std::move(phrase));
    }
    joinable = true;
  };
  for (char32_t c : utf8::decode(text)) {
    if (unicode::is_alnum(c)) {
      cur.push_back(unicode::simple_lower(c));
      continue;
    }
    flush();
    if (separators.find(c) == std::u32string::npos) joinable = false;
  }
  flush();
  return out;
}

namespace {

std::string strip_line_marker(std::string_view line, bool* is_bullet, bool* is_heading, const ArchiveProfile& policy) {
  std::string_view s = utf8::strip(line);
  *is_bullet = false;
  *is_heading = false;
  // headings (section context, not sentences)
  std::size_t h = 0;
  while (h < s.size() && s[h] == '#') ++h;
  if (h > 0 && h < s.size() && s[h] == ' ') {
    *is_heading = true;
    return std::string(utf8::strip(s.substr(h)));
  }
  while (!s.empty() && s.front() == '>') {
    s.remove_prefix(1);
    s = utf8::lstrip(s);
  }
  if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
    *is_bullet = true;
    s.remove_prefix(2);
  } else {
    std::size_t d = 0;
    while (d < s.size() && d < policy.value("/text_item_closure/bullet_max_digits").get<std::size_t>() && s[d] >= '0' && s[d] <= '9') ++d;
    if (d > 0 && d + 1 < s.size() && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') {
      *is_bullet = true;
      s.remove_prefix(d + 2);
    }
  }
  if (!s.empty() && s.front() == '|') *is_bullet = true;  // table row: its own unit
  return std::string(utf8::strip(s));
}

bool is_abbrev_before(const std::u32string& s, std::size_t dot, const ArchiveProfile& policy) {
  // word immediately before the dot
  std::size_t b = dot;
  while (b > 0 && unicode::is_alpha(s[b - 1])) --b;
  std::u32string w = s.substr(b, dot - b);
  for (auto& c : w) c = unicode::simple_lower(c);

  if (w.size() <= 1 && b > 0 && s[b - 1] == '.') return true;  // "e.g." / "m.in."
  return policy.contains("/text/abbreviations", utf8::encode(w));
}

void split_paragraph(std::string_view para, std::vector<std::string>& out, const ArchiveProfile& policy) {
  std::u32string s = utf8::decode(para);
  const auto terminals = utf8::decode(policy.text("/text_item_closure/sentence_terminals"));
  const auto closers = utf8::decode(policy.text("/text_item_closure/sentence_closers"));
  const auto openers = utf8::decode(policy.text("/text_item_closure/sentence_openers"));
  const auto abbreviation_terminals = utf8::decode(policy.text("/text_item_closure/abbreviation_terminals"));
  auto opener_class = [&](char32_t cp) {
    for (const auto& cls : policy.value("/text_item_closure/sentence_opener_classes")) {
      if (cls == "uppercase" && is_upper_cp(cp)) return true;
      if (cls == "lowercase" && unicode::simple_upper(cp) != cp) return true;
      if (cls == "decimal" && unicode::is_decimal(cp)) return true;
      if (cls == "alpha" && unicode::is_alpha(cp)) return true;
      if (cls == "alnum" && unicode::is_alnum(cp)) return true;
      if (cls == "space" && unicode::is_space(cp)) return true;
      if (cls == "any") return true;
    }
    return false;
  };
  std::size_t start = 0;
  auto emit = [&](std::size_t end) {
    std::string piece(utf8::strip(utf8::encode(std::u32string_view(s).substr(start, end - start))));
    if (utf8::length(piece) >= static_cast<std::size_t>(policy.integer("/text/min_sentence_codepoints")) && tokenize(piece).size() >= static_cast<std::size_t>(policy.integer("/text/min_sentence_tokens"))) out.push_back(std::move(piece));
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (terminals.find(c) == std::u32string::npos) continue;
    // consume closing punctuation
    std::size_t j = i + 1;
    while (j < s.size() && closers.find(s[j]) != std::u32string::npos) {
      ++j;
    }
    if (j >= s.size()) break;
    if (!unicode::is_space(s[j])) continue;
    std::size_t k = j;
    while (k < s.size() && unicode::is_space(s[k])) ++k;
    if (k >= s.size()) break;
    char32_t n = s[k];
    bool starts = opener_class(n) || openers.find(n) != std::u32string::npos;
    if (!starts) continue;
    if (abbreviation_terminals.find(c) != std::u32string::npos && is_abbrev_before(s, i, policy)) continue;
    emit(j);
    start = k;
    i = k - 1;
  }
  emit(s.size());
}

}  // namespace

std::vector<std::string> split_sentences(std::string_view text, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  std::vector<std::string> out;
  std::string para;
  bool in_fence = false;
  auto flush = [&] {
    if (!para.empty()) split_paragraph(para, out, policy);
    para.clear();
  };
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    pos = nl + 1;
    std::string_view st = utf8::strip(line);
    if (st.substr(0, 3) == "```" || st.substr(0, 3) == "~~~") {
      flush();
      in_fence = !in_fence;
      if (pos > text.size()) break;
      continue;
    }
    if (in_fence) {
      if (pos > text.size()) break;
      continue;
    }
    if (st.empty() || st == "---" || st == "***" || st.find_first_not_of("-|: ") == std::string_view::npos) {
      flush();
    } else {
      bool bullet = false;
      bool heading = false;
      std::string body = strip_line_marker(line, &bullet, &heading, policy);
      if (heading) {
        flush();
      } else if (!body.empty() && body.front() == '|') {
        flush();
        // table row: skip header rows (followed by a |---| separator), join cells
        std::size_t nl2 = text.find('\n', pos);
        std::string_view next = pos <= text.size() ? utf8::strip(text.substr(pos, nl2 == std::string_view::npos ? std::string_view::npos : nl2 - pos)) : std::string_view();
        bool header = !next.empty() && next.front() == '|' && next.find_first_not_of("-|: ") == std::string_view::npos;
        if (!header) {
          std::string row;
          std::size_t b = 1;
          while (b < body.size()) {
            std::size_t e = body.find('|', b);
            if (e == std::string::npos) e = body.size();
            std::string cell(utf8::strip(std::string_view(body).substr(b, e - b)));
            if (!cell.empty()) row += (row.empty() ? "" : " — ") + cell;
            b = e + 1;
          }
          para = row;
          flush();
        }
      } else if (bullet) {
        flush();
        para = body;
      } else {
        if (!para.empty()) para += ' ';
        para += body;
      }
    }
    if (pos > text.size()) break;
  }
  flush();
  return out;
}

std::string first_date(std::string_view t, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  const auto& min_year = policy.value("/text_item_closure/year_min");
  const auto& max_year = policy.value("/text_item_closure/year_max");
  auto dig = [&](std::size_t i) { return i < t.size() && t[i] >= '0' && t[i] <= '9'; };
  for (std::size_t i = 0; i + 10 <= t.size(); ++i) {
    if (!(dig(i) && dig(i + 1) && dig(i + 2) && dig(i + 3) && t[i + 4] == '-' && dig(i + 5) && dig(i + 6) &&
          t[i + 7] == '-' && dig(i + 8) && dig(i + 9))) {
      continue;
    }
    if (i > 0 && (dig(i - 1) || t[i - 1] == '_' || t[i - 1] == '-')) continue;  // part of a file name / id
    if (dig(i + 10)) continue;
    if (i + 10 < t.size() && t[i + 10] == '.' && i + 11 < t.size() && std::isalpha(static_cast<unsigned char>(t[i + 11]))) {
      continue;  // "..._2026-09-16.md"
    }
    int y = std::stoi(std::string(t.substr(i, 4)));
    int m = std::stoi(std::string(t.substr(i + 5, 2)));
    int d = std::stoi(std::string(t.substr(i + 8, 2)));
    if ((!min_year.is_null() && y < min_year.get<int>()) || (!max_year.is_null() && y > max_year.get<int>()) ||
        m < 1 || m > 12 || d < 1 || d > 31) continue;
    return std::string(t.substr(i, 10));
  }
  return "";
}

std::string first_date(std::string_view t) { return first_date(t, nullptr); }

std::string iso_from_epoch(double seconds, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  if (!std::isfinite(seconds) ||
      (!scope.get().value("/text_item_closure/allow_nonpositive_epoch").get<bool>() && seconds <= 0)) return "";
  const auto floored = std::floor(static_cast<long double>(seconds));
  if constexpr (std::numeric_limits<std::time_t>::is_integer) {
    // Exclusive power-of-two upper bound remains exact even when long double
    // cannot represent the largest time_t integer without rounding upward.
    const auto upper = std::ldexp(1.0L, std::numeric_limits<std::time_t>::digits);
    const auto lower = std::numeric_limits<std::time_t>::is_signed ? -upper : 0.0L;
    if (floored < lower || floored >= upper) return "";
  } else {
    if (floored < static_cast<long double>(std::numeric_limits<std::time_t>::lowest()) ||
        floored > static_cast<long double>(std::numeric_limits<std::time_t>::max())) return "";
  }
  std::time_t tt = static_cast<std::time_t>(floored);
  std::tm tm{};
  if (!gmtime_r(&tt, &tm)) return "";
  char buf[32];
  if (std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) return "";
  return buf;
}

std::string iso_from_epoch(double seconds) { return iso_from_epoch(seconds, nullptr); }

std::string normalize_date(std::string_view s, const ArchiveProfile* profile) {
  s = utf8::strip(s);
  if (s.size() < 10) return "";
  std::string d = first_date(s.substr(0, 10), profile);
  if (d.empty()) return "";
  if (s.size() < 19 || (s[10] != 'T' && s[10] != ' ')) return d;
  auto num = [&](std::size_t i, std::size_t n) -> int {
    int v = 0;
    for (std::size_t k = i; k < i + n; ++k) {
      if (k >= s.size() || s[k] < '0' || s[k] > '9') return -1;
      v = v * 10 + (s[k] - '0');
    }
    return v;
  };
  std::tm tm{};
  tm.tm_year = num(0, 4) - 1900;
  tm.tm_mon = num(5, 2) - 1;
  tm.tm_mday = num(8, 2);
  tm.tm_hour = num(11, 2);
  tm.tm_min = num(14, 2);
  tm.tm_sec = num(17, 2);
  if (tm.tm_hour < 0 || tm.tm_min < 0 || tm.tm_sec < 0) return d;
  std::size_t i = 19;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
  }
  long offset = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    int sign = s[i] == '-' ? -1 : 1;
    int hh = num(i + 1, 2);
    int mm = (i + 3 < s.size() && s[i + 3] == ':') ? num(i + 4, 2) : num(i + 3, 2);
    if (hh >= 0 && mm >= 0) offset = sign * (hh * 3600L + mm * 60L);
  }
  std::time_t t = timegm(&tm) - offset;
  return iso_from_epoch(static_cast<double>(t), profile);
}

std::string normalize_date(std::string_view s) { return normalize_date(s, nullptr); }

std::string date_only(std::string_view iso) { return iso.size() >= 10 ? std::string(iso.substr(0, 10)) : ""; }

std::string hash_prefix(std::string_view s, std::size_t n) { return Sha256::hex(s).substr(0, n); }

std::string clip(std::string_view s, std::size_t max_cp) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  if (utf8::length(out) <= max_cp) return out;
  std::string cut(utf8::prefix(out, max_cp > 1 ? max_cp - 1 : 0));
  while (!cut.empty() && cut.back() == ' ') cut.pop_back();
  return cut + "…";
}

std::vector<std::string> split_identifier(std::string_view ident) {
  std::vector<std::string> out;
  std::u32string s = utf8::decode(ident);
  std::u32string cur;
  auto flush = [&] {
    if (!cur.empty()) {
      for (auto& c : cur) c = unicode::simple_lower(c);
      out.push_back(utf8::encode(cur));
    }
    cur.clear();
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (!unicode::is_alnum(c)) {
      flush();
      continue;
    }
    if (!cur.empty() && is_upper_cp(c)) {
      bool prev_lower = !is_upper_cp(cur.back()) && unicode::is_alpha(cur.back());
      bool next_lower = i + 1 < s.size() && unicode::is_alpha(s[i + 1]) && !is_upper_cp(s[i + 1]);
      bool prev_upper = is_upper_cp(cur.back());
      if (prev_lower || (prev_upper && next_lower) || unicode::is_decimal(cur.back())) flush();
    }
    cur.push_back(c);
  }
  flush();
  return out;
}

std::string stem(std::string_view t, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  std::string s(t);
  auto ends = [&](std::string_view suf) {
    return s.size() > suf.size() + static_cast<std::size_t>(policy.integer("/text/stem_min_extra_bytes")) &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
  };
  for (const auto& rule : policy.value("/text/stem_rules")) {
    bool matched = false, excluded = false;
    for (const auto& suffix : rule.at("suffixes")) matched = matched || ends(suffix.get_ref<const std::string&>());
    for (const auto& suffix : rule.at("exclude")) excluded = excluded || ends(suffix.get_ref<const std::string&>());
    const auto remove = rule.at("remove").get<std::size_t>();
    if (matched && !excluded && remove <= s.size()) return s.substr(0, s.size() - remove) + rule.at("replacement").get<std::string>();
  }
  return s;
}

std::string gloss(std::string_view t, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& words = scope.get().value("/text/glossary");
  auto it = words.find(std::string(t));
  return it == words.end() ? std::string(t) : it->get<std::string>();
}

}  // namespace loom::archive
