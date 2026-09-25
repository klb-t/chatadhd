// OWNER: wave 2 semantic. Everything below except Match helpers is a stub.
#include "loom/re/regex.h"

#include "loom/util/utf8.h"
#include "stub.h"

namespace loom::re {

struct Regex::Program {
  std::string pattern;
  Flags flags = kNone;
  std::size_t groups = 0;
  std::uint64_t step_limit = 10'000'000;
  bool hit_limit = false;
};

std::u32string_view Match::group(std::size_t g) const noexcept {
  Span s = span(g);
  if (!s.matched() || static_cast<std::size_t>(s.end) > subject_.size()) return {};
  return subject_.substr(static_cast<std::size_t>(s.start), static_cast<std::size_t>(s.end - s.start));
}

std::string Match::group_utf8(std::size_t g) const { return utf8::encode(group(g)); }

Regex::~Regex() = default;

Result<Regex> Regex::compile(std::string_view pattern_utf8, Flags flags) {
  // STUB: wave2
  (void)pattern_utf8;
  (void)flags;
  return LOOM_NOT_IMPLEMENTED("re::Regex::compile");
}

std::optional<Match> Regex::search(std::u32string_view, std::size_t) const { return std::nullopt; }     // STUB: wave2
std::optional<Match> Regex::match(std::u32string_view, std::size_t) const { return std::nullopt; }      // STUB: wave2
std::optional<Match> Regex::fullmatch(std::u32string_view, std::size_t) const { return std::nullopt; }  // STUB: wave2
std::vector<Match> Regex::finditer(std::u32string_view) const { return {}; }                          // STUB: wave2
std::vector<std::u32string> Regex::findall(std::u32string_view) const { return {}; }                  // STUB: wave2
std::u32string Regex::sub(std::u32string_view, std::u32string_view text, std::size_t) const {
  return std::u32string(text);  // STUB: wave2
}
std::string Regex::sub_utf8(std::string_view, std::string_view text, std::size_t) const {
  return std::string(text);  // STUB: wave2
}
bool Regex::search_utf8(std::string_view) const { return false; }  // STUB: wave2

std::size_t Regex::group_count() const noexcept { return prog_ ? prog_->groups : 0; }
const std::string& Regex::pattern() const noexcept {
  static const std::string kEmpty;
  return prog_ ? prog_->pattern : kEmpty;
}
Flags Regex::flags() const noexcept { return prog_ ? prog_->flags : kNone; }
void Regex::set_step_limit(std::uint64_t steps) noexcept {
  if (prog_) prog_->step_limit = steps;
}
bool Regex::last_search_hit_limit() const noexcept { return prog_ && prog_->hit_limit; }

}  // namespace loom::re
