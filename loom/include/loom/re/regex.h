// loom/re/regex.h — small backtracking regex engine with Python `re`
// semantics for the subset Loom's ported code uses.       [OWNER: wave 2 semantic]
//
// Why not std::regex: it has no Unicode \w/\d/\s/\b, no lazy-quantifier
// guarantees across implementations, no lookahead parity with Python, and is
// slow. Loom must reproduce exactly what core/semantic.py and
// engine/importer.py match, so this engine follows CPython's sre.
//
// Subject strings are UTF-32 (one element per code point) so offsets are
// Python string indices. UTF-8 overloads convert and return code-point
// offsets too.
//
// REQUIRED SYNTAX (everything used by core/semantic.py, engine/importer.py,
// core/selector.py and sklearn's default token pattern):
//   literals, escapes  \. \\ \( \) \[ \] \{ \} \| \* \+ \? \^ \$ \/ \- \' \"
//                      \t \n \r \f \v \xhh \uXXXX \UXXXXXXXX
//   classes            [abc] [a-z] [^...] with escapes inside (\w \d \s \W \D
//                      \S \\ \] \- \' \" ...) and non-ASCII members [►▶→>]
//   shorthand          \w \W \d \D \s \S  (Unicode: loom/util/unicode.h
//                      is_word / is_decimal / is_space)
//   any                .  (not '\n' unless DOTALL)
//   anchors            ^ $ (MULTILINE-aware; `$` without MULTILINE also
//                      matches before a final '\n'), \A \Z, \b \B (Unicode
//                      word boundary)
//   quantifiers        * + ? {m} {m,} {m,n} {,n} and lazy *? +? ?? {m,n}?
//   groups             (...) capturing, (?:...) non-capturing,
//                      (?=...) / (?!...) lookahead
//   alternation        a|b at any nesting level (leftmost alternative wins)
//   inline flags       (?i) (?m) (?s) (?u) at the start of the pattern
// FLAGS: IgnoreCase (sre semantics: compare simple_lower(), classes also try
//        simple_upper()), Multiline, DotAll.
// Not required: named groups, backreferences, lookbehind, possessive/atomic.
//
// Semantics that must match CPython (see tests/compat for differential tests):
//   * leftmost match, then backtracking order as in sre (greedy first).
//   * finditer(): non-overlapping; after an empty match the next search
//     starts one position later; an empty match adjacent to a previous
//     match is allowed (Python 3.7+ behaviour).
//   * Match::lastindex(): index of the capturing group that closed last
//     (Python m.lastindex), nullopt when no group participated.
//   * sub(): replacement supports \1..\99, \g<N>, and escapes \n \t \\.
//
// Safety: every search is bounded by a step budget (default 10 million
// backtracking steps). When exceeded the search reports no match and
// last_search_hit_limit() becomes true; callers may log it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"

namespace loom::re {

enum Flag : unsigned {
  kNone = 0,
  kIgnoreCase = 1u << 0,
  kMultiline = 1u << 1,
  kDotAll = 1u << 2,
};
using Flags = unsigned;

struct Span {
  std::ptrdiff_t start = -1;  // code-point offsets; -1 when the group did not participate
  std::ptrdiff_t end = -1;
  bool matched() const noexcept { return start >= 0; }
};

class Match {
 public:
  Match() = default;
  Match(std::u32string_view subject, std::vector<Span> spans, std::optional<std::size_t> lastindex)
      : subject_(subject), spans_(std::move(spans)), lastindex_(lastindex) {}

  std::size_t group_count() const noexcept { return spans_.empty() ? 0 : spans_.size() - 1; }
  Span span(std::size_t group = 0) const noexcept { return group < spans_.size() ? spans_[group] : Span{}; }
  std::ptrdiff_t start(std::size_t group = 0) const noexcept { return span(group).start; }
  std::ptrdiff_t end(std::size_t group = 0) const noexcept { return span(group).end; }
  bool matched(std::size_t group) const noexcept { return span(group).matched(); }
  std::optional<std::size_t> lastindex() const noexcept { return lastindex_; }

  // View into the subject passed to search() (must outlive the Match).
  // Empty view for groups that did not participate (Python returns None).
  std::u32string_view group(std::size_t g = 0) const noexcept;
  std::string group_utf8(std::size_t g = 0) const;

 private:
  std::u32string_view subject_;
  std::vector<Span> spans_;
  std::optional<std::size_t> lastindex_;
};

class Regex {
 public:
  // Parse + compile. Syntax errors -> Errc::Parse with the offending offset.
  static Result<Regex> compile(std::string_view pattern_utf8, Flags flags = kNone);

  Regex(const Regex&) = default;
  Regex& operator=(const Regex&) = default;
  Regex(Regex&&) noexcept = default;
  Regex& operator=(Regex&&) noexcept = default;
  ~Regex();

  // Python pattern.search(text, pos) / .match(text, pos) / .fullmatch(text, pos).
  std::optional<Match> search(std::u32string_view text, std::size_t pos = 0) const;
  std::optional<Match> match(std::u32string_view text, std::size_t pos = 0) const;
  std::optional<Match> fullmatch(std::u32string_view text, std::size_t pos = 0) const;
  std::vector<Match> finditer(std::u32string_view text) const;
  // Python re.findall for patterns with 0 or 1 capturing groups (group 0 or 1).
  std::vector<std::u32string> findall(std::u32string_view text) const;
  // Python re.sub(pattern, repl, text, count=0).
  std::u32string sub(std::u32string_view repl, std::u32string_view text, std::size_t count = 0) const;

  // UTF-8 conveniences (decode with errors="replace" first).
  std::string sub_utf8(std::string_view repl, std::string_view text, std::size_t count = 0) const;
  bool search_utf8(std::string_view text) const;  // any match?

  std::size_t group_count() const noexcept;
  const std::string& pattern() const noexcept;
  Flags flags() const noexcept;

  void set_step_limit(std::uint64_t steps) noexcept;
  bool last_search_hit_limit() const noexcept;

  struct Program;  // compiled form (src/re/)

 private:
  explicit Regex(std::shared_ptr<Program> p) : prog_(std::move(p)) {}
  std::shared_ptr<Program> prog_;  // immutable after compile; shared by copies
};

}  // namespace loom::re
