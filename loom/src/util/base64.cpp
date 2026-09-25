#include "loom/util/base64.h"

#include <array>
#include <cstdint>

namespace loom::base64 {
namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::array<int, 256> make_rev() {
  std::array<int, 256> r{};
  for (auto& v : r) v = -1;
  for (int i = 0; i < 64; ++i) r[static_cast<unsigned char>(kAlphabet[i])] = i;
  return r;
}
constexpr auto kRev = make_rev();
}  // namespace

std::string encode(std::string_view bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  while (i + 3 <= bytes.size()) {
    std::uint32_t v = (std::uint32_t{static_cast<unsigned char>(bytes[i])} << 16) |
                      (std::uint32_t{static_cast<unsigned char>(bytes[i + 1])} << 8) |
                      std::uint32_t{static_cast<unsigned char>(bytes[i + 2])};
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(kAlphabet[(v >> 6) & 63]);
    out.push_back(kAlphabet[v & 63]);
    i += 3;
  }
  std::size_t rem = bytes.size() - i;
  if (rem == 1) {
    std::uint32_t v = std::uint32_t{static_cast<unsigned char>(bytes[i])} << 16;
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out += "==";
  } else if (rem == 2) {
    std::uint32_t v = (std::uint32_t{static_cast<unsigned char>(bytes[i])} << 16) |
                      (std::uint32_t{static_cast<unsigned char>(bytes[i + 1])} << 8);
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(kAlphabet[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

Result<std::string> decode(std::string_view text, bool strict) {
  std::string clean;
  clean.reserve(text.size());
  for (char c : text) {
    if (kRev[static_cast<unsigned char>(c)] >= 0 || c == '=') {
      clean.push_back(c);
    } else if (strict) {
      return Error(Errc::Parse, "invalid base64 character");
    }
  }
  // Python (binascii.a2b_base64): data after the first complete pad group is
  // ignored in non-strict mode; we require canonical padding here.
  std::string out;
  out.reserve(clean.size() / 4 * 3);
  std::uint32_t acc = 0;
  int bits = 0;
  std::size_t quad = 0;
  std::size_t pads = 0;
  for (char c : clean) {
    if (c == '=') {
      ++pads;
      ++quad;
      if (quad % 4 == 0) break;
      continue;
    }
    if (pads > 0) return Error(Errc::Parse, "invalid base64 padding");
    acc = (acc << 6) | static_cast<std::uint32_t>(kRev[static_cast<unsigned char>(c)]);
    bits += 6;
    ++quad;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  if (quad % 4 != 0) return Error(Errc::Parse, "Incorrect padding");
  return out;
}

}  // namespace loom::base64
