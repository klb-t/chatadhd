// Portable SHA-256, HMAC/HKDF-SHA256 and Keccak (SHA3-256, SHAKE256). Internal.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace loom::cs::portable {
std::string sha256(std::string_view data);
std::string hmac_sha256(std::string_view key, std::string_view data);
std::string hkdf_sha256(std::string_view ikm, std::string_view salt, std::string_view info, std::size_t len);
std::string sha3_256(std::string_view data);
std::string shake256(std::string_view data, std::size_t out_len);
}  // namespace loom::cs::portable
