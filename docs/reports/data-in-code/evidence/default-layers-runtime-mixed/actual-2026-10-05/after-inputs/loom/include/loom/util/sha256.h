// loom/util/sha256.h — self-contained SHA-256 / HMAC / PBKDF2 (no OpenSSL).
//
// Used for content addressing (blob store, input/output hashes) and as the
// PBKDF2 fallback for crypto.h when OpenSSL is not linked.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "loom/result.h"

namespace loom {

class Sha256 {
 public:
  using Digest = std::array<std::uint8_t, 32>;

  Sha256() noexcept { reset(); }
  void reset() noexcept;
  void update(const void* data, std::size_t len) noexcept;
  void update(std::string_view data) noexcept { update(data.data(), data.size()); }
  Digest finish() noexcept;     // resets the object afterwards
  std::string finish_hex();     // lowercase hex

  static Digest digest(std::string_view data) noexcept;
  static std::string hex(std::string_view data);

 private:
  void compress(const std::uint8_t* block) noexcept;
  std::array<std::uint32_t, 8> h_{};
  std::array<std::uint8_t, 64> buf_{};
  std::size_t buf_len_ = 0;
  std::uint64_t total_len_ = 0;
};

std::string to_hex(const std::uint8_t* data, std::size_t len);
template <std::size_t N>
std::string to_hex(const std::array<std::uint8_t, N>& a) { return to_hex(a.data(), N); }

// Streams the file through SHA-256 (constant memory).
Result<std::string> sha256_file_hex(const std::filesystem::path& path);

Sha256::Digest hmac_sha256(std::string_view key, std::string_view message) noexcept;

// PBKDF2-HMAC-SHA256 (RFC 8018). Writes `out_len` bytes to `out`.
void pbkdf2_hmac_sha256(std::string_view password, std::string_view salt, std::uint32_t iterations,
                        std::uint8_t* out, std::size_t out_len) noexcept;

}  // namespace loom
