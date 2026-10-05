#include "loom/util/unicode.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace loom::unicode {
namespace {

struct CpRange {
  char32_t lo;
  char32_t hi;
};
struct CpMap {
  char32_t from;
  char32_t to;
};
struct CpSpecial {
  char32_t from;
  std::uint8_t n;
  char32_t seq[3];
};

#include "unicode_tables.inc"

template <std::size_t N>
bool in_ranges(const CpRange (&table)[N], char32_t cp) noexcept {
  const CpRange* end = table + N;
  const CpRange* it = std::upper_bound(table, end, cp, [](char32_t v, const CpRange& r) { return v < r.lo; });
  if (it == table) return false;
  --it;
  return cp >= it->lo && cp <= it->hi;
}

template <std::size_t N>
const CpMap* find_map(const CpMap (&table)[N], char32_t cp) noexcept {
  const CpMap* end = table + N;
  const CpMap* it = std::lower_bound(table, end, cp, [](const CpMap& m, char32_t v) { return m.from < v; });
  return (it != end && it->from == cp) ? it : nullptr;
}

template <std::size_t N>
const CpSpecial* find_special(const CpSpecial (&table)[N], char32_t cp) noexcept {
  const CpSpecial* end = table + N;
  const CpSpecial* it =
      std::lower_bound(table, end, cp, [](const CpSpecial& m, char32_t v) { return m.from < v; });
  return (it != end && it->from == cp && it->n > 0) ? it : nullptr;
}

}  // namespace

bool is_alpha(char32_t cp) noexcept {
  if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
  return in_ranges(kAlpha, cp);
}
bool is_decimal(char32_t cp) noexcept {
  if (cp < 0x80) return cp >= '0' && cp <= '9';
  return in_ranges(kDecimal, cp);
}
bool is_digit(char32_t cp) noexcept { return in_ranges(kDigit, cp); }
bool is_numeric(char32_t cp) noexcept { return in_ranges(kNumeric, cp); }
bool is_alnum(char32_t cp) noexcept {
  if (cp < 0x80) return is_alpha(cp) || is_decimal(cp);
  return is_alpha(cp) || is_decimal(cp) || is_digit(cp) || is_numeric(cp);
}
bool is_space(char32_t cp) noexcept {
  if (cp < 0x80) return cp == ' ' || (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x1F);
  return in_ranges(kSpace, cp);
}
bool is_word(char32_t cp) noexcept { return cp == U'_' || is_alnum(cp); }

char32_t simple_lower(char32_t cp) noexcept {
  if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
  const CpMap* m = find_map(kSimpleLower, cp);
  return m ? m->to : cp;
}
char32_t simple_upper(char32_t cp) noexcept {
  if (cp < 0x80) return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
  const CpMap* m = find_map(kSimpleUpper, cp);
  return m ? m->to : cp;
}

std::size_t full_lower(char32_t cp, char32_t out[3]) noexcept {
  if (const CpSpecial* s = find_special(kFullLower, cp)) {
    for (std::size_t i = 0; i < s->n; ++i) out[i] = s->seq[i];
    return s->n;
  }
  out[0] = simple_lower(cp);
  return 1;
}
std::size_t full_upper(char32_t cp, char32_t out[3]) noexcept {
  if (const CpSpecial* s = find_special(kFullUpper, cp)) {
    for (std::size_t i = 0; i < s->n; ++i) out[i] = s->seq[i];
    return s->n;
  }
  out[0] = simple_upper(cp);
  return 1;
}

bool is_cased(char32_t cp) noexcept { return in_ranges(kCased, cp); }
bool is_case_ignorable(char32_t cp) noexcept { return in_ranges(kCaseIgnorable, cp); }

std::string_view database_version() noexcept { return kUnicodeVersion; }

}  // namespace loom::unicode
