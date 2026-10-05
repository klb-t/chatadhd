// loom/util/utf8.h — UTF-8 helpers with Python str semantics.
//
// Python slices and len() count code points; Loom stores UTF-8. These helpers
// give the Python meaning (`text[:25]`, `len(text) >= 20`, `s.lower()`,
// `s.strip()`, `bytes.decode("utf-8", errors="replace")`) on UTF-8 strings.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace loom::utf8 {

inline constexpr char32_t kReplacement = U'�';

bool is_valid(std::string_view s) noexcept;

// Decode with Python's errors="replace": every maximal ill-formed subpart is
// replaced by one U+FFFD (Unicode "best practice", which CPython follows).
std::u32string decode(std::string_view s);
// Same as decode() but re-encoded to UTF-8 (valid input is returned unchanged).
std::string repair(std::string_view s);

void append(std::string& out, char32_t cp);
std::string encode(std::u32string_view cps);
std::string encode(char32_t cp);

// Number of code points (Python len()). Invalid bytes count as decode() would.
std::size_t length(std::string_view s) noexcept;

// Python s[:n] on code points. Returns a view into `s` (valid UTF-8 input).
std::string_view prefix(std::string_view s, std::size_t n_codepoints) noexcept;
// Python s[start:end] on code points (end clamped; npos = to the end).
std::string slice(std::string_view s, std::size_t start, std::size_t end = std::string::npos);
// Byte offset of code point index `cp` (clamped to s.size()).
std::size_t byte_offset(std::string_view s, std::size_t cp) noexcept;

// Python str.lower() / str.upper() (full case mapping incl. final sigma for
// lower). Tables generated from CPython's unicodedata (see unicode.h).
std::string to_lower(std::string_view s);
std::string to_upper(std::string_view s);
std::u32string to_lower(std::u32string_view s);

// Python str.strip()/lstrip()/rstrip() with no argument (Unicode whitespace).
std::string_view strip(std::string_view s) noexcept;
std::string_view lstrip(std::string_view s) noexcept;
std::string_view rstrip(std::string_view s) noexcept;
// Python `not s.strip()`.
bool is_blank(std::string_view s) noexcept;

}  // namespace loom::utf8
