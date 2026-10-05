#include "loom/util/sha256.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace loom {
namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

}  // namespace

void Sha256::reset() noexcept {
  h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  buf_len_ = 0;
  total_len_ = 0;
}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (std::uint32_t{block[i * 4]} << 24) | (std::uint32_t{block[i * 4 + 1]} << 16) |
           (std::uint32_t{block[i * 4 + 2]} << 8) | std::uint32_t{block[i * 4 + 3]};
  }
  for (int i = 16; i < 64; ++i) {
    std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    std::uint32_t ch = (e & f) ^ (~e & g);
    std::uint32_t t1 = h + S1 + ch + kK[i] + w[i];
    std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    std::uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) noexcept {
  const auto* p = static_cast<const std::uint8_t*>(data);
  total_len_ += len;
  if (buf_len_ > 0) {
    std::size_t take = std::min(len, buf_.size() - buf_len_);
    std::memcpy(buf_.data() + buf_len_, p, take);
    buf_len_ += take;
    p += take;
    len -= take;
    if (buf_len_ == buf_.size()) {
      compress(buf_.data());
      buf_len_ = 0;
    }
  }
  while (len >= 64) {
    compress(p);
    p += 64;
    len -= 64;
  }
  if (len > 0) {
    std::memcpy(buf_.data(), p, len);
    buf_len_ = len;
  }
}

Sha256::Digest Sha256::finish() noexcept {
  std::uint64_t bit_len = total_len_ * 8;
  std::uint8_t pad = 0x80;
  update(&pad, 1);
  std::uint8_t zero = 0;
  while (buf_len_ != 56) update(&zero, 1);
  std::uint8_t len_be[8];
  for (int i = 0; i < 8; ++i) len_be[i] = static_cast<std::uint8_t>(bit_len >> (56 - 8 * i));
  update(len_be, 8);
  Digest out{};
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<std::uint8_t>(h_[i] >> 24);
    out[i * 4 + 1] = static_cast<std::uint8_t>(h_[i] >> 16);
    out[i * 4 + 2] = static_cast<std::uint8_t>(h_[i] >> 8);
    out[i * 4 + 3] = static_cast<std::uint8_t>(h_[i]);
  }
  reset();
  return out;
}

std::string Sha256::finish_hex() { return to_hex(finish()); }

Sha256::Digest Sha256::digest(std::string_view data) noexcept {
  Sha256 s;
  s.update(data);
  return s.finish();
}

std::string Sha256::hex(std::string_view data) { return to_hex(digest(data)); }

std::string to_hex(const std::uint8_t* data, std::size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out[i * 2] = kHex[data[i] >> 4];
    out[i * 2 + 1] = kHex[data[i] & 0xF];
  }
  return out;
}

Result<std::string> sha256_file_hex(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open " + path.string());
  Sha256 s;
  std::vector<char> buf(1 << 16);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize got = in.gcount();
    if (got > 0) s.update(buf.data(), static_cast<std::size_t>(got));
  }
  if (in.bad()) return Error(Errc::Io, "read failed: " + path.string());
  return s.finish_hex();
}

Sha256::Digest hmac_sha256(std::string_view key, std::string_view message) noexcept {
  std::uint8_t k[64] = {};
  if (key.size() > 64) {
    auto d = Sha256::digest(key);
    std::memcpy(k, d.data(), d.size());
  } else {
    std::memcpy(k, key.data(), key.size());
  }
  std::uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  Sha256 inner;
  inner.update(ipad, 64);
  inner.update(message);
  auto ih = inner.finish();
  Sha256 outer;
  outer.update(opad, 64);
  outer.update(ih.data(), ih.size());
  return outer.finish();
}

void pbkdf2_hmac_sha256(std::string_view password, std::string_view salt, std::uint32_t iterations,
                        std::uint8_t* out, std::size_t out_len) noexcept {
  // Precompute the keyed inner/outer states once; each iteration then costs
  // two compressions instead of four.
  std::uint8_t k[64] = {};
  if (password.size() > 64) {
    auto d = Sha256::digest(password);
    std::memcpy(k, d.data(), d.size());
  } else {
    std::memcpy(k, password.data(), password.size());
  }
  std::uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  Sha256 inner_base, outer_base;
  inner_base.update(ipad, 64);
  outer_base.update(opad, 64);

  auto prf = [&](const std::uint8_t* msg, std::size_t len) {
    Sha256 in = inner_base;
    in.update(msg, len);
    auto ih = in.finish();
    Sha256 o = outer_base;
    o.update(ih.data(), ih.size());
    return o.finish();
  };

  std::uint32_t block = 1;
  std::size_t written = 0;
  std::vector<std::uint8_t> first(salt.size() + 4);
  std::memcpy(first.data(), salt.data(), salt.size());
  while (written < out_len) {
    first[salt.size()] = static_cast<std::uint8_t>(block >> 24);
    first[salt.size() + 1] = static_cast<std::uint8_t>(block >> 16);
    first[salt.size() + 2] = static_cast<std::uint8_t>(block >> 8);
    first[salt.size() + 3] = static_cast<std::uint8_t>(block);
    auto u = prf(first.data(), first.size());
    auto t = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
      u = prf(u.data(), u.size());
      for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    std::size_t take = std::min(t.size(), out_len - written);
    std::memcpy(out + written, t.data(), take);
    written += take;
    ++block;
  }
}

}  // namespace loom
