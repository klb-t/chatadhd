// catalog_internal.h: AliasIndex — identity-pass matching (pass 0 of
// proposal_scale.md §5.1) with context gates and negative contexts (noise
// traps). Identities match at Unicode word boundaries; intentional word
// prefixes are data. Context cues remain substring probes because the pack
// writes them as bare stems ("aplikacj", "kernel", "wdt").
#include "catalog_internal.h"

#include <algorithm>
#include <cctype>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::catalog::internal {

namespace {

bool word_at(std::string_view text, std::size_t pos) {
  if (pos >= text.size()) return false;
  auto cp = utf8::decode(utf8::prefix(text.substr(pos), 1));
  return !cp.empty() && unicode::is_word(cp.front());
}

bool word_before(std::string_view text, std::size_t pos) {
  if (pos == 0) return false;
  std::size_t start = pos - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
  return word_at(text, start);
}

bool contains_folded(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return false;
  return haystack.find(needle) != std::string_view::npos;
}

int count_context_hits(std::string_view folded_text, const std::vector<std::string>& cues) {
  int n = 0;
  for (const auto& c : cues) {
    if (contains_folded(folded_text, c)) ++n;
  }
  return n;
}

using WordSpan = std::pair<std::size_t, std::size_t>;

std::vector<WordSpan> word_spans(std::string_view text) {
  std::vector<WordSpan> spans;
  std::size_t start = 0;
  bool in_word = false;
  for (std::size_t pos = 0; pos < text.size();) {
    bool word = word_at(text, pos);
    if (word && !in_word) start = pos;
    if (!word && in_word) spans.emplace_back(start, pos);
    in_word = word;
    pos += std::max<std::size_t>(1, utf8::prefix(text.substr(pos), 1).size());
  }
  if (in_word) spans.emplace_back(start, text.size());
  return spans;
}

std::string_view local_context(std::string_view text, const std::vector<WordSpan>& spans,
                               std::size_t begin, std::size_t end, int radius) {
  // Alias phrase tokens themselves are not charged against either side's
  // budget. Punctuation and multi-byte Unicode letters do not enlarge it.
  auto first = std::lower_bound(spans.begin(), spans.end(), begin,
                                [](const WordSpan& span, std::size_t pos) { return span.second <= pos; });
  auto after = std::lower_bound(spans.begin(), spans.end(), end,
                                [](const WordSpan& span, std::size_t pos) { return span.first < pos; });
  auto before_count = static_cast<std::size_t>(first - spans.begin());
  auto after_index = static_cast<std::size_t>(after - spans.begin());
  auto count = static_cast<std::size_t>(std::max(0, radius));
  std::size_t left = begin, right = end;
  if (count > 0 && before_count > 0) left = spans[before_count > count ? before_count - count : 0].first;
  if (count > 0 && after_index < spans.size()) {
    std::size_t take = std::min(count, spans.size() - after_index);
    right = spans[after_index + take - 1].second;
  }
  return text.substr(left, right - left);
}

std::string make_snippet(std::string_view text, std::size_t pos, std::size_t needle_len) {
  constexpr std::size_t kWindow = 60;
  std::size_t start = pos > kWindow ? pos - kWindow : 0;
  std::size_t end = std::min(text.size(), pos + needle_len + kWindow);
  return std::string(text.substr(start, end - start));
}

}  // namespace

Json Mention::to_json() const {
  return Json{{"kind", kind}, {"key", key}, {"snippet", snippet}, {"offset", offset}, {"trap", trap}, {"trap_reason", trap_reason}};
}
Mention Mention::from_json(const Json& j) {
  Mention m;
  m.kind = json::get_string(j, "kind");
  m.key = json::get_string(j, "key");
  m.snippet = json::get_string(j, "snippet");
  m.offset = json::get_int(j, "offset");
  m.trap = json::get_bool(j, "trap");
  m.trap_reason = json::get_string(j, "trap_reason");
  return m;
}

AliasIndex AliasIndex::from_profile(const SelfProfile& profile, int context_window_tokens) {
  AliasIndex idx;
  idx.context_window_tokens_ = std::max(0, context_window_tokens);
  if (!profile.terms.is_array()) return idx;
  for (const auto& t : profile.terms) {
    std::string cls = json::get_string(t, "class", "alias");
    if (cls != "alias" && cls != "principle") continue;
    AliasTerm at;
    at.project = json::get_string(t, "project");
    at.surface = json::get_string(t, "term");
    at.folded = json::get_string(t, "key");  // already folded/normalised by build_profile
    if (at.folded.empty()) at.folded = at.surface;
    at.term_class = cls;
    at.ambiguous = json::get_bool(t, "ambiguous");
    at.prefix = json::get_bool(t, "prefix", cls == "principle");
    if (const Json* rc = json::find(t, "requires_context"); rc && rc->is_object()) {
      if (const Json* any = json::find(*rc, "any"); any && any->is_array()) {
        for (const auto& c : *any) at.requires_any.push_back(c.get<std::string>());
      }
      at.requires_min = static_cast<int>(json::get_int(*rc, "min", 1));
    }
    if (const Json* neg = json::find(t, "negative_context"); neg && neg->is_array()) {
      for (const auto& c : *neg) at.negative.push_back(c.get<std::string>());
    }
    idx.terms_.push_back(std::move(at));
  }
  return idx;
}

void AliasIndex::enable_inflection(const kb::Normalizer& norm, const kb::Pack& pack, std::size_t min_chars) {
  norm_ = &norm;
  endings_.clear();
  const Json& stemming = pack.lexicon("stemming");
  for (const char* lang : {"pl", "en"}) {
    const Json* table = json::find(stemming, lang);
    const Json* suffixes = table ? json::find(*table, "suffixes") : nullptr;
    if (!suffixes || !suffixes->is_array()) continue;
    for (const auto& x : *suffixes) {
      if (x.is_string()) endings_.push_back(norm.fold(x.get<std::string>()));
    }
  }
  std::sort(endings_.begin(), endings_.end());
  endings_.erase(std::unique(endings_.begin(), endings_.end()), endings_.end());

  for (auto& at : terms_) {
    at.word_tokens.clear();
    at.word_keys.clear();
    at.word_anchors.clear();
    if (at.prefix || at.folded.empty()) continue;
    // Only plain phrases: word tokens separated by single spaces.
    std::vector<std::string> toks;
    bool plain = true;
    for (std::size_t start = 0; plain;) {
      std::size_t sp = at.folded.find(' ', start);
      std::string tok = at.folded.substr(start, sp == std::string::npos ? std::string::npos : sp - start);
      auto cps = utf8::decode(tok);
      plain = !cps.empty() && std::all_of(cps.begin(), cps.end(), [](char32_t c) { return unicode::is_word(c); });
      toks.push_back(std::move(tok));
      if (sp == std::string::npos) break;
      start = sp + 1;
    }
    if (!plain) continue;
    for (const auto& tok : toks) {
      std::string key = norm.match_key(tok, kb::Lang::Pl);
      std::string anchor;
      if (utf8::length(tok) >= min_chars) {
        // Every accepted form starts with the part the token and its stem
        // share ("kaucje"/"kaucj" -> "kaucj"), cut at a code-point boundary.
        std::size_t n = 0;
        while (n < tok.size() && n < key.size() && tok[n] == key[n]) ++n;
        while (n > 0 && n < tok.size() && (static_cast<unsigned char>(tok[n]) & 0xc0) == 0x80) --n;
        anchor = tok.substr(0, n);
        if (utf8::length(anchor) + 1 < min_chars) anchor.clear();  // too short a stem to generalise safely
      }
      at.word_keys.push_back(std::move(key));
      at.word_anchors.push_back(std::move(anchor));
    }
    at.word_tokens = std::move(toks);
  }
}

bool AliasIndex::inflected_equal(std::string_view token, const AliasTerm& at, std::size_t k) const {
  const std::string& base = at.word_tokens[k];
  if (token == base) return true;
  const std::string& anchor = at.word_anchors[k];
  if (!norm_ || anchor.empty() || token.substr(0, anchor.size()) != anchor) return false;
  if (token.size() > base.size() && token.substr(0, base.size()) == base &&
      std::binary_search(endings_.begin(), endings_.end(), token.substr(base.size()))) {
    return true;  // the alias token plus one inflectional ending
  }
  return norm_->match_key(token, kb::Lang::Pl) == at.word_keys[k];  // the same light stem
}

std::vector<Mention> AliasIndex::find(std::string_view folded_text, int max_mentions) const {
  std::vector<Mention> out;
  std::vector<WordSpan> spans;
  bool spans_ready = false;
  auto ensure_spans = [&] {
    if (!spans_ready) {
      spans = word_spans(folded_text);
      spans_ready = true;
    }
  };
  // Judges one occurrence [found, pos) of `at` and records it; false once
  // the mention cap is reached.
  auto record = [&](const AliasTerm& at, std::size_t found, std::size_t pos) {
    bool trap = false;
    std::string trap_reason;
    if (at.ambiguous) {
      ensure_spans();
      auto context = local_context(folded_text, spans, found, pos, context_window_tokens_);
      int ctx_hits = count_context_hits(context, at.requires_any);
      int neg_hits = count_context_hits(context, at.negative);
      bool ctx_ok = ctx_hits >= at.requires_min;
      bool neg_dominant = (!ctx_ok && neg_hits > 0) || (neg_hits > ctx_hits);
      if (neg_dominant) {
        trap = true;
        trap_reason = "ambiguous alias '" + at.surface + "': negative context dominates (ctx=" +
                      std::to_string(ctx_hits) + ", neg=" + std::to_string(neg_hits) + ")";
      } else if (!ctx_ok) {
        // Neither confirmed nor a clear trap: skip silently (too little
        // evidence either way), matching "counts only inside their context
        // window" (profiles/self.json header comment).
        return static_cast<int>(out.size()) < max_mentions;
      }
    }
    Mention m;
    m.kind = at.term_class == "principle" ? "principle" : "alias";
    // Principle/philosophy-probe hits carry no project: tag them "owner"
    // (a project id "owner" never exists in profiles/self.json) so
    // selection_rules.json's {"project": "owner"} rule (R2: philosophy
    // chats are relevant even without an app name) can match on `key`
    // exactly like a real project id, instead of a special case.
    m.key = at.project.empty() ? std::string("owner") : at.project;
    m.snippet = make_snippet(folded_text, found, pos - found);
    m.offset = static_cast<std::int64_t>(found);
    m.trap = trap;
    m.trap_reason = trap_reason;
    out.push_back(std::move(m));
    return static_cast<int>(out.size()) < max_mentions;
  };

  bool more = true;
  for (const auto& at : terms_) {
    if (!more) break;
    if (at.folded.empty()) continue;
    if (norm_ && !at.word_tokens.empty()) {
      // Whole-token path: consecutive word spans separated by whitespace.
      ensure_spans();
      const std::size_t n = at.word_tokens.size();
      for (std::size_t k = 0; more && k + n <= spans.size(); ++k) {
        bool ok = true;
        for (std::size_t q = 0; ok && q < n; ++q) {
          const auto [b, e] = spans[k + q];
          if (q > 0) {
            std::string_view gap = folded_text.substr(spans[k + q - 1].second, b - spans[k + q - 1].second);
            ok = !gap.empty() && std::all_of(gap.begin(), gap.end(), [](char c) {
              return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            });
          }
          ok = ok && inflected_equal(folded_text.substr(b, e - b), at, q);
        }
        if (ok) more = record(at, spans[k].first, spans[k + n - 1].second);
      }
      continue;
    }
    auto chars = utf8::decode(at.folded);
    bool begins_word = !chars.empty() && unicode::is_word(chars.front());
    bool ends_word = !chars.empty() && unicode::is_word(chars.back());
    std::size_t pos = 0;
    while (more) {
      std::size_t found = folded_text.find(at.folded, pos);
      if (found == std::string_view::npos) break;
      pos = found + std::max<std::size_t>(1, at.folded.size());
      if ((begins_word && word_before(folded_text, found)) ||
          (!at.prefix && ends_word && word_at(folded_text, pos))) {
        // A rejected occurrence may overlap a valid one ("abc-ab" in
        // "xabc-abc-ab"). Do not skip the rest of its full phrase.
        pos = found + std::max<std::size_t>(1, utf8::prefix(folded_text.substr(found), 1).size());
        continue;
      }
      more = record(at, found, pos);
    }
  }
  return out;
}

std::vector<Mention> find_version_mentions(std::string_view text, const std::vector<Mention>& alias_hits,
                                           int window_chars) {
  std::vector<Mention> out;
  auto is_version_char = [](char c) { return (c >= '0' && c <= '9') || c == '.'; };
  for (std::size_t i = 0; i < text.size();) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
      ++i;
      continue;
    }
    std::size_t j = i;
    int dots = 0;
    while (j < text.size() && is_version_char(text[j])) {
      if (text[j] == '.') ++dots;
      ++j;
    }
    if (dots >= 1 && dots <= 2 && j > i && (j - i) <= 12) {
      std::string ver(text.substr(i, j - i));
      bool near_alias = false;
      for (const auto& m : alias_hits) {
        if (m.kind != "alias" || m.trap || m.offset < 0) continue;
        auto d = m.offset > static_cast<std::int64_t>(i) ? m.offset - static_cast<std::int64_t>(i)
                                                         : static_cast<std::int64_t>(i) - m.offset;
        if (d <= window_chars) {
          near_alias = true;
          break;
        }
      }
      if (near_alias) {
        Mention m;
        m.kind = "version";
        m.key = ver;
        m.offset = static_cast<std::int64_t>(i);
        m.snippet = make_snippet(text, i, j - i);
        out.push_back(std::move(m));
      }
    }
    i = j > i ? j : i + 1;
  }
  return out;
}

}  // namespace loom::catalog::internal
