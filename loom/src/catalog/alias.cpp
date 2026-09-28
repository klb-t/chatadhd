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

AliasIndex AliasIndex::from_profile(const SelfProfile& profile) {
  AliasIndex idx;
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
    at.weight = json::get_number(t, "weight", cls == "principle" ? 1.5 : 3.0);
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

std::vector<Mention> AliasIndex::find(std::string_view folded_text, int max_mentions) const {
  std::vector<Mention> out;
  for (const auto& at : terms_) {
    if (at.folded.empty()) continue;
    auto chars = utf8::decode(at.folded);
    bool begins_word = !chars.empty() && unicode::is_word(chars.front());
    bool ends_word = !chars.empty() && unicode::is_word(chars.back());
    std::size_t pos = 0;
    while (true) {
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
      bool trap = false;
      std::string trap_reason;
      if (at.ambiguous) {
        int ctx_hits = count_context_hits(folded_text, at.requires_any);
        int neg_hits = count_context_hits(folded_text, at.negative);
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
          if (static_cast<int>(out.size()) >= max_mentions) break;
          continue;
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
      m.snippet = make_snippet(folded_text, found, at.folded.size());
      m.offset = static_cast<std::int64_t>(found);
      m.trap = trap;
      m.trap_reason = trap_reason;
      out.push_back(std::move(m));
      if (static_cast<int>(out.size()) >= max_mentions) break;
    }
    if (static_cast<int>(out.size()) >= max_mentions) break;
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
      bool near_alias = alias_hits.empty();
      for (const auto& m : alias_hits) {
        auto d = m.offset > static_cast<std::int64_t>(i) ? m.offset - static_cast<std::int64_t>(i)
                                                         : static_cast<std::int64_t>(i) - m.offset;
        if (d <= window_chars) {
          near_alias = true;
          break;
        }
      }
      if (near_alias && !alias_hits.empty()) {
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
