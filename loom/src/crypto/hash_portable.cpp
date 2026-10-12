// Portable FIPS 180-4 SHA-256 and FIPS 202 SHA3-256 / SHAKE256, RFC 2104 HMAC, RFC 5869 HKDF.
// Used where OpenSSL is absent (Android) and for X-Wing's SHA3/SHAKE everywhere.
#include "crypto/hash_portable.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace loom::cs::portable {
namespace {

// ── SHA-256 ──
constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256_block(std::array<std::uint32_t, 8>& h, const unsigned char* p) {
  std::array<std::uint32_t, 64> w{};
  for (int i = 0; i < 16; ++i)
    w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
           (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
  for (int i = 16; i < 64; ++i) {
    std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    std::uint32_t ch = (e & f) ^ (~e & g);
    std::uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
    std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    std::uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// ── Keccak-f[1600] ──
constexpr std::uint64_t kRC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
constexpr int kRot[24] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
constexpr int kPi[24] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

inline std::uint64_t rotl64(std::uint64_t x, int n) { return (x << n) | (x >> (64 - n)); }

void keccak_f(std::uint64_t st[25]) {
  for (int round = 0; round < 24; ++round) {
    std::uint64_t bc[5];
    for (int i = 0; i < 5; ++i) bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
    for (int i = 0; i < 5; ++i) {
      std::uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
      for (int j = 0; j < 25; j += 5) st[j + i] ^= t;
    }
    std::uint64_t t = st[1];
    for (int i = 0; i < 24; ++i) {
      int j = kPi[i];
      std::uint64_t b = st[j];
      st[j] = rotl64(t, kRot[i]);
      t = b;
    }
    for (int j = 0; j < 25; j += 5) {
      for (int i = 0; i < 5; ++i) bc[i] = st[j + i];
      for (int i = 0; i < 5; ++i) st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
    }
    st[0] ^= kRC[round];
  }
}

std::string keccak(std::string_view in, std::size_t rate, unsigned char dsuffix, std::size_t out_len) {
  std::uint64_t st[25] = {};
  auto absorb_byte = [&](std::size_t pos, unsigned char b) { st[pos / 8] ^= std::uint64_t(b) << (8 * (pos % 8)); };
  std::size_t pos = 0;
  for (unsigned char c : in) {
    absorb_byte(pos++, c);
    if (pos == rate) { keccak_f(st); pos = 0; }
  }
  absorb_byte(pos, dsuffix);
  absorb_byte(rate - 1, 0x80);
  keccak_f(st);
  std::string out(out_len, '\0');
  std::size_t off = 0;
  while (off < out_len) {
    std::size_t n = out_len - off < rate ? out_len - off : rate;
    for (std::size_t i = 0; i < n; ++i) out[off + i] = static_cast<char>((st[i / 8] >> (8 * (i % 8))) & 0xff);
    off += n;
    if (off < out_len) keccak_f(st);
  }
  return out;
}

}  // namespace

std::string sha256(std::string_view data) {
  std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto* p = reinterpret_cast<const unsigned char*>(data.data());
  std::size_t n = data.size();
  std::size_t full = n / 64;
  for (std::size_t i = 0; i < full; ++i) sha256_block(h, p + 64 * i);
  unsigned char tail[128] = {};
  std::size_t rem = n - full * 64;
  if (rem) std::memcpy(tail, p + full * 64, rem);
  tail[rem] = 0x80;
  std::size_t tl = rem < 56 ? 64 : 128;
  std::uint64_t bits = static_cast<std::uint64_t>(n) * 8;
  for (int i = 0; i < 8; ++i) tail[tl - 1 - i] = static_cast<unsigned char>(bits >> (8 * i));
  for (std::size_t i = 0; i < tl; i += 64) sha256_block(h, tail + i);
  std::string out(32, '\0');
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<char>(h[i] >> (24 - 8 * j));
  return out;
}

std::string hmac_sha256(std::string_view key, std::string_view data) {
  std::string k(key);
  if (k.size() > 64) k = sha256(k);
  k.resize(64, '\0');
  std::string ipad(64, '\0'), opad(64, '\0');
  for (int i = 0; i < 64; ++i) {
    ipad[i] = static_cast<char>(k[i] ^ 0x36);
    opad[i] = static_cast<char>(k[i] ^ 0x5c);
  }
  return sha256(opad + sha256(ipad + std::string(data)));
}

std::string hkdf_sha256(std::string_view ikm, std::string_view salt, std::string_view info, std::size_t len) {
  std::string zero_salt(32, '\0');
  std::string prk = hmac_sha256(salt.empty() ? std::string_view(zero_salt) : salt, ikm);
  std::string okm, t;
  for (unsigned char i = 1; okm.size() < len; ++i) {
    t = hmac_sha256(prk, t + std::string(info) + static_cast<char>(i));
    okm += t;
  }
  okm.resize(len);
  return okm;
}

std::string sha3_256(std::string_view data) { return keccak(data, 136, 0x06, 32); }
std::string shake256(std::string_view data, std::size_t out_len) { return keccak(data, 136, 0x1f, out_len); }

}  // namespace loom::cs::portable
