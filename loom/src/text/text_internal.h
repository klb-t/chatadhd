// Internal helpers of the text area (R102 G1); not a public contract.
#pragma once

#include <cstddef>
#include <string_view>

namespace loom::text::detail {

struct Cp {
  char32_t cp;
  std::size_t len;  // bytes consumed (>= 1; an invalid byte is one replacement codepoint of length 1)
};

// Decode the codepoint at byte i (i < s.size()). Never reads past s; never returns len 0.
Cp decode_at(std::string_view s, std::size_t i) noexcept;

}  // namespace loom::text::detail
