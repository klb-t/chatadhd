#include "loom/util/utf8.h"

#include <cstdint>

#include "loom/util/unicode.h"

namespace loom::utf8 {
namespace {

// Decodes one code point starting at s[i]. On success returns the code point
// and advances i past it. On error returns kReplacement and advances i past
// the maximal ill-formed subpart (at least one byte), as CPython does.
char32_t next(std::string_view s, std::size_t& i) noexcept {
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    ++i;
    return b0;
  }
  std::size_t need = 0;
  char32_t cp = 0;
  unsigned char lo = 0x80, hi = 0xBF;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    need = 1;
    cp = b0 & 0x1F;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    need = 2;
    cp = b0 & 0x0F;
    if (b0 == 0xE0) lo = 0xA0;
    if (b0 == 0xED) hi = 0x9F;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    need = 3;
    cp = b0 & 0x07;
    if (b0 == 0xF0) lo = 0x90;
    if (b0 == 0xF4) hi = 0x8F;
  } else {
    ++i;  // C0, C1, F5..FF, stray continuation byte
    return kReplacement;
  }
  std::size_t j = i + 1;
  for (std::size_t k = 0; k < need; ++k, ++j) {
    if (j >= s.size()) {
      i = j;  // truncated sequence: the whole prefix is one maximal subpart
      return kReplacement;
    }
    const auto b = static_cast<unsigned char>(s[j]);
    const unsigned char l = (k == 0) ? lo : 0x80;
    const unsigned char h = (k == 0) ? hi : 0xBF;
    if (b < l || b > h) {
      i = j;  // replace consumed prefix; resume at the offending byte
      return kReplacement;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  i = j;
  return cp;
}

bool is_final_sigma(std::u32string_view s, std::size_t pos) noexcept {
  // CPython unicodeobject.c handle_capital_sigma().
  std::size_t j = pos;
  bool before = false;
  while (j > 0) {
    char32_t c = s[j - 1];
    if (!unicode::is_case_ignorable(c)) {
      before = unicode::is_cased(c);
      break;
    }
    --j;
  }
  if (!before) return false;
  for (std::size_t k = pos + 1; k < s.size(); ++k) {
    char32_t c = s[k];
    if (!unicode::is_case_ignorable(c)) return !unicode::is_cased(c);
  }
  return true;
}

}  // namespace

bool is_valid(std::string_view s) noexcept {
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t before = i;
    char32_t cp = next(s, i);
    if (cp == kReplacement) {
      // A literal U+FFFD is encoded as EF BF BD (3 bytes).
      if (i - before != 3 || static_cast<unsigned char>(s[before]) != 0xEF) return false;
    }
  }
  return true;
}

std::u32string decode(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) out.push_back(next(s, i));
  return out;
}

void append(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    append(out, kReplacement);
  }
}

std::string encode(std::u32string_view cps) {
  std::string out;
  out.reserve(cps.size());
  for (char32_t c : cps) append(out, c);
  return out;
}

std::string encode(char32_t cp) {
  std::string out;
  append(out, cp);
  return out;
}

std::string repair(std::string_view s) {
  if (is_valid(s)) return std::string(s);
  return encode(decode(s));
}

std::size_t length(std::string_view s) noexcept {
  std::size_t n = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    // Fast path for ASCII.
    if (static_cast<unsigned char>(s[i]) < 0x80) {
      ++i;
    } else {
      next(s, i);
    }
    ++n;
  }
  return n;
}

std::size_t byte_offset(std::string_view s, std::size_t cp) noexcept {
  std::size_t i = 0;
  std::size_t n = 0;
  while (i < s.size() && n < cp) {
    if (static_cast<unsigned char>(s[i]) < 0x80) {
      ++i;
    } else {
      next(s, i);
    }
    ++n;
  }
  return i;
}

std::string_view prefix(std::string_view s, std::size_t n_codepoints) noexcept {
  return s.substr(0, byte_offset(s, n_codepoints));
}

std::string slice(std::string_view s, std::size_t start, std::size_t end) {
  std::size_t b0 = byte_offset(s, start);
  if (end == std::string::npos) return std::string(s.substr(b0));
  if (end <= start) return {};
  std::size_t b1 = b0 + byte_offset(s.substr(b0), end - start);
  return std::string(s.substr(b0, b1 - b0));
}

std::u32string to_lower(std::u32string_view s) {
  std::u32string out;
  out.reserve(s.size());
  char32_t buf[3];
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (c < 0x80) {
      out.push_back((c >= 'A' && c <= 'Z') ? c + 32 : c);
      continue;
    }
    if (c == 0x3A3) {  // GREEK CAPITAL LETTER SIGMA
      out.push_back(is_final_sigma(s, i) ? char32_t{0x3C2} : char32_t{0x3C3});
      continue;
    }
    std::size_t n = unicode::full_lower(c, buf);
    out.append(buf, n);
  }
  return out;
}

std::string to_lower(std::string_view s) {
  bool ascii = true;
  for (char c : s) {
    if (static_cast<unsigned char>(c) >= 0x80) {
      ascii = false;
      break;
    }
  }
  if (ascii) {
    std::string out(s);
    for (auto& c : out) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
  }
  return encode(to_lower(std::u32string_view(decode(s))));
}

std::string to_upper(std::string_view s) {
  std::u32string cps = decode(s);
  std::u32string out;
  out.reserve(cps.size());
  char32_t buf[3];
  for (char32_t c : cps) {
    std::size_t n = unicode::full_upper(c, buf);
    out.append(buf, n);
  }
  return encode(out);
}

namespace {
// Decode the code point ending at byte position `end` (exclusive), walking
// back over continuation bytes. Returns its start offset.
std::size_t prev_start(std::string_view s, std::size_t end) noexcept {
  std::size_t i = end - 1;
  std::size_t steps = 0;
  while (i > 0 && steps < 3 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
    --i;
    ++steps;
  }
  return i;
}
}  // namespace

std::string_view lstrip(std::string_view s) noexcept {
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t j = i;
    char32_t cp = next(s, j);
    if (!unicode::is_space(cp) || cp == kReplacement) break;
    i = j;
  }
  return s.substr(i);
}

std::string_view rstrip(std::string_view s) noexcept {
  std::size_t end = s.size();
  while (end > 0) {
    std::size_t start = prev_start(s, end);
    std::size_t j = start;
    char32_t cp = next(s, j);
    if (j != end || !unicode::is_space(cp) || cp == kReplacement) break;
    end = start;
  }
  return s.substr(0, end);
}

std::string_view strip(std::string_view s) noexcept { return rstrip(lstrip(s)); }

bool is_blank(std::string_view s) noexcept { return strip(s).empty(); }

}  // namespace loom::utf8
