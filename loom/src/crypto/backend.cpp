// CryptoBackend: OpenSSL where available, vendored portable C otherwise. See loom/crypto_suite.h.
#include <atomic>
#include <cstring>
#include <mutex>

#include "crypto/hash_portable.h"
#include "crypto/vendor/pq_glue.h"
#include "loom/crypto_suite.h"
#include "loom/util/random.h"

extern "C" {
#include "argon2.h"
#include "monocypher-ed25519.h"
#include "monocypher.h"
}

#if defined(LOOM_HAVE_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif

extern "C" int loom_os_randombytes(std::uint8_t* out, std::size_t len) {
  loom::random_bytes(out, len);
  return 0;
}

namespace loom::cs {
namespace {

const unsigned char* u8(std::string_view s) { return reinterpret_cast<const unsigned char*>(s.data()); }
unsigned char* u8(std::string& s) { return reinterpret_cast<unsigned char*>(s.data()); }

void wipe(void* p, std::size_t n) noexcept {
#if defined(LOOM_HAVE_OPENSSL)
  OPENSSL_cleanse(p, n);
#else
  volatile unsigned char* v = static_cast<volatile unsigned char*>(p);
  for (std::size_t i = 0; i < n; ++i) v[i] = 0;
#endif
}
void wipe(std::string& s) noexcept {
  if (!s.empty()) wipe(s.data(), s.size());
}

Error bad_size(std::string_view what) { return Error(Errc::InvalidArgument, std::string(what) + ": wrong input length"); }
Error unavailable(std::string_view what) {
  return Error(Errc::Unavailable, std::string(what) + " is not available in this build/backend");
}

bool all_zero(std::string_view s) {
  unsigned char acc = 0;
  for (unsigned char c : s) acc = static_cast<unsigned char>(acc | c);
  return acc == 0;
}

// Portable: vendored code + portable hashes. AES-GCM is not provided here.
class PortableBackend : public CryptoBackend {
 public:
  bool available(Prim p) const override { return p != Prim::Aes256Gcm; }
  std::string backend_name(Prim p) const override {
    switch (p) {
      case Prim::Aes256Gcm: return "none";
      case Prim::Sha256: case Prim::HkdfSha256: case Prim::Sha3_256: case Prim::Shake256: return "portable";
      case Prim::Argon2id: return "argon2-ref";
      case Prim::X25519: case Prim::Ed25519: return "monocypher";
      case Prim::MlKem768: return "mlkem-native";
      case Prim::MlDsa65: return "mldsa-native";
    }
    return "none";
  }
  Bytes random(std::size_t n) const override { return loom::random_bytes(n); }
  Result<Bytes> aes256gcm_seal(std::string_view, std::string_view, std::string_view, std::string_view) const override {
    return unavailable("aes-256-gcm");
  }
  Result<Bytes> aes256gcm_open(std::string_view, std::string_view, std::string_view, std::string_view) const override {
    return unavailable("aes-256-gcm");
  }
  Result<Bytes> sha256(std::string_view d) const override { return portable::sha256(d); }
  Result<Bytes> hkdf_sha256(std::string_view ikm, std::string_view salt, std::string_view info,
                            std::size_t n) const override {
    if (n > 255 * 32) return Error(Errc::InvalidArgument, "hkdf: output too long");
    return portable::hkdf_sha256(ikm, salt, info, n);
  }
  Result<Bytes> argon2id(std::string_view pw, std::string_view salt, const Argon2Params& prm, std::size_t out_len,
                         std::string_view secret, std::string_view ad) const override {
    if (prm.t == 0 || prm.p == 0 || prm.m_kib == 0)
      return Error(Errc::InvalidArgument, "argon2id: t, m_kib and p must be set (from the crypto preset)");
    Bytes out(out_len, '\0');
    argon2_context ctx{};
    ctx.out = u8(out);
    ctx.outlen = static_cast<uint32_t>(out_len);
    ctx.pwd = const_cast<uint8_t*>(u8(pw));
    ctx.pwdlen = static_cast<uint32_t>(pw.size());
    ctx.salt = const_cast<uint8_t*>(u8(salt));
    ctx.saltlen = static_cast<uint32_t>(salt.size());
    ctx.secret = secret.empty() ? nullptr : const_cast<uint8_t*>(u8(secret));
    ctx.secretlen = static_cast<uint32_t>(secret.size());
    ctx.ad = ad.empty() ? nullptr : const_cast<uint8_t*>(u8(ad));
    ctx.adlen = static_cast<uint32_t>(ad.size());
    ctx.t_cost = prm.t;
    ctx.m_cost = prm.m_kib;
    ctx.lanes = prm.p;
    ctx.threads = prm.p;
    ctx.version = ARGON2_VERSION_NUMBER;
    ctx.flags = ARGON2_DEFAULT_FLAGS;
    int rc = argon2_ctx(&ctx, Argon2_id);
    if (rc != ARGON2_OK) {
      wipe(out);
      // Never silently lower the cost: report (e.g. memory allocation failure) to the caller.
      return Error(rc == ARGON2_MEMORY_ALLOCATION_ERROR ? Errc::Unavailable : Errc::InvalidArgument,
                   std::string("argon2id failed: ") + argon2_error_message(rc));
    }
    return out;
  }
  Result<Bytes> x25519_public(std::string_view sk) const override {
    if (sk.size() != 32) return bad_size("x25519 secret key");
    Bytes out(32, '\0');
    crypto_x25519_public_key(u8(out), u8(sk));
    return out;
  }
  Result<Bytes> x25519(std::string_view sk, std::string_view pk) const override {
    if (sk.size() != 32 || pk.size() != 32) return bad_size("x25519");
    Bytes out(32, '\0');
    crypto_x25519(u8(out), u8(sk), u8(pk));
    if (all_zero(out)) return Error(Errc::Crypto, "x25519: low-order point");
    return out;
  }
  Result<Bytes> ed25519_public(std::string_view seed) const override {
    if (seed.size() != 32) return bad_size("ed25519 seed");
    uint8_t sk[64], pk[32];
    uint8_t s[32];
    std::memcpy(s, seed.data(), 32);
    crypto_ed25519_key_pair(sk, pk, s);  // wipes s
    wipe(sk, sizeof sk);
    return Bytes(reinterpret_cast<char*>(pk), 32);
  }
  Result<Bytes> ed25519_sign(std::string_view seed, std::string_view msg) const override {
    if (seed.size() != 32) return bad_size("ed25519 seed");
    uint8_t sk[64], pk[32];
    uint8_t s[32];
    std::memcpy(s, seed.data(), 32);
    crypto_ed25519_key_pair(sk, pk, s);
    Bytes sig(64, '\0');
    crypto_ed25519_sign(u8(sig), sk, u8(msg), msg.size());
    wipe(sk, sizeof sk);
    return sig;
  }
  bool ed25519_verify(std::string_view pk, std::string_view msg, std::string_view sig) const override {
    if (pk.size() != 32 || sig.size() != 64) return false;
    return crypto_ed25519_check(u8(sig), u8(pk), u8(msg), msg.size()) == 0;
  }
  Result<Keypair> mlkem768_keypair(std::string_view dz) const override {
    if (dz.size() != 64) return bad_size("ml-kem-768 keygen seed");
    Keypair kp;
    kp.pk.assign(kMlKem768Pk, '\0');
    kp.sk.assign(kMlKem768Sk, '\0');
    if (loom_pq_mlkem768_keypair_derand(u8(kp.pk), u8(kp.sk), u8(dz)) != 0)
      return Error(Errc::Crypto, "ml-kem-768 key generation failed");
    return kp;
  }
  Result<Encapsulated> mlkem768_encaps(std::string_view pk, std::string_view m) const override {
    if (pk.size() != kMlKem768Pk || m.size() != 32) return bad_size("ml-kem-768 encaps");
    Encapsulated e;
    e.ct.assign(kMlKem768Ct, '\0');
    e.ss.assign(kMlKem768Ss, '\0');
    // FIPS 203 7.2: the encapsulation key must pass the modulus check.
    if (loom_pq_mlkem768_check_pk(u8(pk)) != 0) return Error(Errc::InvalidArgument, "ml-kem-768: invalid public key");
    if (loom_pq_mlkem768_enc_derand(u8(e.ct), u8(e.ss), u8(pk), u8(m)) != 0)
      return Error(Errc::Crypto, "ml-kem-768 encapsulation failed");
    return e;
  }
  Result<Bytes> mlkem768_decaps(std::string_view sk, std::string_view ct) const override {
    if (sk.size() != kMlKem768Sk || ct.size() != kMlKem768Ct) return bad_size("ml-kem-768 decaps");
    Bytes ss(kMlKem768Ss, '\0');
    if (loom_pq_mlkem768_dec(u8(ss), u8(ct), u8(sk)) != 0) return Error(Errc::Crypto, "ml-kem-768 decapsulation failed");
    return ss;
  }
  Result<Keypair> mldsa65_keypair(std::string_view seed) const override {
    if (seed.size() != 32) return bad_size("ml-dsa-65 keygen seed");
    Keypair kp;
    kp.pk.assign(kMlDsa65Pk, '\0');
    kp.sk.assign(kMlDsa65Sk, '\0');
    if (loom_pq_mldsa65_keypair(u8(kp.pk), u8(kp.sk), u8(seed)) != 0)
      return Error(Errc::Crypto, "ml-dsa-65 key generation failed");
    return kp;
  }
  Result<Bytes> mldsa65_sign(std::string_view sk, std::string_view msg, std::string_view ctx,
                             std::string_view rnd) const override {
    if (sk.size() != kMlDsa65Sk || (!rnd.empty() && rnd.size() != 32) || ctx.size() > 255)
      return bad_size("ml-dsa-65 sign");
    Bytes sig(kMlDsa65Sig, '\0');
    if (loom_pq_mldsa65_sign(u8(sig), u8(msg), msg.size(), u8(ctx), ctx.size(), rnd.empty() ? nullptr : u8(rnd),
                             u8(sk)) != 0)
      return Error(Errc::Crypto, "ml-dsa-65 signing failed");
    return sig;
  }
  bool mldsa65_verify(std::string_view pk, std::string_view msg, std::string_view ctx,
                      std::string_view sig) const override {
    if (pk.size() != kMlDsa65Pk || sig.size() != kMlDsa65Sig || ctx.size() > 255) return false;
    return loom_pq_mldsa65_verify(u8(sig), u8(msg), msg.size(), u8(ctx), ctx.size(), u8(pk)) == 0;
  }
};

#if defined(LOOM_HAVE_OPENSSL)
struct PkeyDel { void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); } };
using Pkey = std::unique_ptr<EVP_PKEY, PkeyDel>;
struct CtxDel { void operator()(EVP_CIPHER_CTX* c) const { EVP_CIPHER_CTX_free(c); } };
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CtxDel>;
struct MdDel { void operator()(EVP_MD_CTX* c) const { EVP_MD_CTX_free(c); } };
using MdCtx = std::unique_ptr<EVP_MD_CTX, MdDel>;
struct PctxDel { void operator()(EVP_PKEY_CTX* c) const { EVP_PKEY_CTX_free(c); } };
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, PctxDel>;

class OpenSslBackend : public PortableBackend {
 public:
  bool available(Prim) const override { return true; }
  std::string backend_name(Prim p) const override {
    switch (p) {
      case Prim::Aes256Gcm: case Prim::Sha256: case Prim::X25519: case Prim::Ed25519:
        return "openssl";
      default: return PortableBackend::backend_name(p);
    }
  }
  Result<Bytes> aes256gcm_seal(std::string_view key, std::string_view nonce, std::string_view aad,
                               std::string_view pt) const override {
    if (key.size() != 32 || nonce.size() != 12) return bad_size("aes-256-gcm");
    CipherCtx c(EVP_CIPHER_CTX_new());
    if (!c) return Error(Errc::Crypto, "AES-GCM context allocation failed");
    if (EVP_EncryptInit_ex(c.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_EncryptInit_ex(c.get(), nullptr, nullptr, u8(key), u8(nonce)) != 1)
      return Error(Errc::Crypto, "AES-GCM init failed");
    int len = 0;
    if (!aad.empty() && EVP_EncryptUpdate(c.get(), nullptr, &len, u8(aad), static_cast<int>(aad.size())) != 1)
      return Error(Errc::Crypto, "AES-GCM aad failed");
    Bytes out(pt.size() + 16, '\0');
    int total = 0;
    if (!pt.empty()) {
      if (EVP_EncryptUpdate(c.get(), u8(out), &len, u8(pt), static_cast<int>(pt.size())) != 1)
        return Error(Errc::Crypto, "AES-GCM encrypt failed");
      total = len;
    }
    unsigned char fin[16];
    if (EVP_EncryptFinal_ex(c.get(), fin, &len) != 1) return Error(Errc::Crypto, "AES-GCM finalize failed");
    total += len;
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_GET_TAG, 16, out.data() + total) != 1)
      return Error(Errc::Crypto, "AES-GCM tag failed");
    out.resize(static_cast<std::size_t>(total) + 16);
    return out;
  }
  Result<Bytes> aes256gcm_open(std::string_view key, std::string_view nonce, std::string_view aad,
                               std::string_view ct) const override {
    if (key.size() != 32 || nonce.size() != 12) return bad_size("aes-256-gcm");
    if (ct.size() < 16) return Error(Errc::Crypto, "ciphertext too short");
    std::size_t n = ct.size() - 16;
    CipherCtx c(EVP_CIPHER_CTX_new());
    if (!c) return Error(Errc::Crypto, "AES-GCM context allocation failed");
    if (EVP_DecryptInit_ex(c.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_DecryptInit_ex(c.get(), nullptr, nullptr, u8(key), u8(nonce)) != 1)
      return Error(Errc::Crypto, "AES-GCM init failed");
    int len = 0;
    if (!aad.empty() && EVP_DecryptUpdate(c.get(), nullptr, &len, u8(aad), static_cast<int>(aad.size())) != 1)
      return Error(Errc::Crypto, "AES-GCM aad failed");
    Bytes out(n, '\0');
    int total = 0;
    if (n > 0) {
      if (EVP_DecryptUpdate(c.get(), u8(out), &len, u8(ct), static_cast<int>(n)) != 1) {
        wipe(out);
        return Error(Errc::Crypto, "decryption failed (wrong key or tampered data)");
      }
      total = len;
    }
    unsigned char tag[16];
    std::memcpy(tag, ct.data() + n, 16);
    unsigned char fin[16];
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_TAG, 16, tag) != 1 || EVP_DecryptFinal_ex(c.get(), fin, &len) != 1) {
      wipe(out);
      return Error(Errc::Crypto, "decryption failed (wrong key or tampered data)");
    }
    out.resize(static_cast<std::size_t>(total) + static_cast<std::size_t>(len));
    return out;
  }
  Result<Bytes> sha256(std::string_view d) const override {
    Bytes out(32, '\0');
    unsigned int n = 0;
    if (EVP_Digest(d.data(), d.size(), u8(out), &n, EVP_sha256(), nullptr) != 1) return Error(Errc::Crypto, "sha256 failed");
    return out;
  }
  Result<Bytes> hkdf_sha256(std::string_view ikm, std::string_view salt, std::string_view info,
                            std::size_t n) const override {
    if (n > 255 * 32) return Error(Errc::InvalidArgument, "hkdf: output too long");
    // Portable HMAC-SHA256 extract/expand (RFC 5869); bit-identical to any other HKDF (RFC vector in tests)
    return portable::hkdf_sha256(ikm, salt, info, n);
  }
  Result<Bytes> x25519_public(std::string_view sk) const override {
    if (sk.size() != 32) return bad_size("x25519 secret key");
    Pkey k(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, u8(sk), 32));
    if (!k) return Error(Errc::Crypto, "x25519 key load failed");
    Bytes out(32, '\0');
    std::size_t n = 32;
    if (EVP_PKEY_get_raw_public_key(k.get(), u8(out), &n) != 1 || n != 32) return Error(Errc::Crypto, "x25519 public failed");
    return out;
  }
  Result<Bytes> x25519(std::string_view sk, std::string_view pk) const override {
    if (sk.size() != 32 || pk.size() != 32) return bad_size("x25519");
    Pkey a(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, u8(sk), 32));
    Pkey b(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, u8(pk), 32));
    if (!a || !b) return Error(Errc::Crypto, "x25519 key load failed");
    PkeyCtx d(EVP_PKEY_CTX_new(a.get(), nullptr));
    if (!d || EVP_PKEY_derive_init(d.get()) != 1 || EVP_PKEY_derive_set_peer(d.get(), b.get()) != 1)
      return Error(Errc::Crypto, "x25519 derive init failed");
    Bytes out(32, '\0');
    std::size_t n = 32;
    if (EVP_PKEY_derive(d.get(), u8(out), &n) != 1 || n != 32 || all_zero(out))
      return Error(Errc::Crypto, "x25519: low-order point");
    return out;
  }
  Result<Bytes> ed25519_public(std::string_view seed) const override {
    if (seed.size() != 32) return bad_size("ed25519 seed");
    Pkey k(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, u8(seed), 32));
    if (!k) return Error(Errc::Crypto, "ed25519 key load failed");
    Bytes out(32, '\0');
    std::size_t n = 32;
    if (EVP_PKEY_get_raw_public_key(k.get(), u8(out), &n) != 1 || n != 32) return Error(Errc::Crypto, "ed25519 public failed");
    return out;
  }
  Result<Bytes> ed25519_sign(std::string_view seed, std::string_view msg) const override {
    if (seed.size() != 32) return bad_size("ed25519 seed");
    Pkey k(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, u8(seed), 32));
    MdCtx m(EVP_MD_CTX_new());
    if (!k || !m || EVP_DigestSignInit(m.get(), nullptr, nullptr, nullptr, k.get()) != 1)
      return Error(Errc::Crypto, "ed25519 sign init failed");
    Bytes sig(64, '\0');
    std::size_t n = 64;
    if (EVP_DigestSign(m.get(), u8(sig), &n, u8(msg), msg.size()) != 1 || n != 64)
      return Error(Errc::Crypto, "ed25519 sign failed");
    return sig;
  }
  bool ed25519_verify(std::string_view pk, std::string_view msg, std::string_view sig) const override {
    if (pk.size() != 32 || sig.size() != 64) return false;
    Pkey k(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, u8(pk), 32));
    MdCtx m(EVP_MD_CTX_new());
    if (!k || !m || EVP_DigestVerifyInit(m.get(), nullptr, nullptr, nullptr, k.get()) != 1) return false;
    return EVP_DigestVerify(m.get(), u8(sig), 64, u8(msg), msg.size()) == 1;
  }
};
#endif

std::mutex g_mu;
std::shared_ptr<const CryptoBackend> g_override;

const CryptoBackend& default_backend() {
#if defined(LOOM_HAVE_OPENSSL)
  static const OpenSslBackend b;
#else
  static const PortableBackend b;
#endif
  return b;
}

}  // namespace

const CryptoBackend& backend() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_override ? *g_override : default_backend();
}
void set_backend(std::shared_ptr<const CryptoBackend> b) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_override = std::move(b);
}
const CryptoBackend& portable_backend() {
  static const PortableBackend b;
  return b;
}

std::string_view prim_id(Prim p) noexcept {
  switch (p) {
    case Prim::Aes256Gcm: return "aes-256-gcm";
    case Prim::Sha256: return "sha-256";
    case Prim::HkdfSha256: return "hkdf-sha256";
    case Prim::Sha3_256: return "sha3-256";
    case Prim::Shake256: return "shake256";
    case Prim::Argon2id: return "argon2id";
    case Prim::X25519: return "x25519";
    case Prim::Ed25519: return "ed25519";
    case Prim::MlKem768: return "ml-kem-768";
    case Prim::MlDsa65: return "ml-dsa-65";
  }
  return "";
}
const std::vector<Prim>& all_prims() noexcept {
  static const std::vector<Prim> v = {Prim::Aes256Gcm, Prim::Sha256, Prim::HkdfSha256, Prim::Sha3_256, Prim::Shake256,
                                      Prim::Argon2id, Prim::X25519, Prim::Ed25519, Prim::MlKem768, Prim::MlDsa65};
  return v;
}

Bytes sha3_256(std::string_view d) { return portable::sha3_256(d); }
Bytes shake256(std::string_view d, std::size_t n) { return portable::shake256(d, n); }

std::string to_hex(std::string_view raw) {
  static const char* k = "0123456789abcdef";
  std::string o;
  o.reserve(raw.size() * 2);
  for (unsigned char c : raw) { o.push_back(k[c >> 4]); o.push_back(k[c & 15]); }
  return o;
}
std::optional<Bytes> from_hex(std::string_view h) {
  if (h.size() % 2) return std::nullopt;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  Bytes o;
  o.reserve(h.size() / 2);
  for (std::size_t i = 0; i < h.size(); i += 2) {
    int a = nib(h[i]), b = nib(h[i + 1]);
    if (a < 0 || b < 0) return std::nullopt;
    o.push_back(static_cast<char>(a * 16 + b));
  }
  return o;
}

}  // namespace loom::cs
