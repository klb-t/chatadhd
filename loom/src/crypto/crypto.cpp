// Port of core/crypto.py: AES-256-GCM with a PBKDF2-HMAC-SHA256 derived key.
#include "loom/crypto.h"

#include <cstring>

#include "loom/log.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/random.h"

#if defined(LOOM_HAVE_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.crypto";
// Fixed plaintext encrypted with the user's password at setup() time; unlock()
// succeeds only if decrypting it with the given password reproduces this
// exact string. Never written to disk in the clear.
constexpr std::string_view kVerifyMagic = "loom-crypto-vault-v1";

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
  // Always compares the longer length so the timing does not leak how many
  // leading bytes matched, then folds in the length mismatch itself.
  std::size_t n = a.size() > b.size() ? a.size() : b.size();
  unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
  for (std::size_t i = 0; i < n; ++i) {
    unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
    unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    diff = static_cast<unsigned char>(diff | (ca ^ cb));
  }
  return diff == 0;
}

void secure_wipe(std::string& s) noexcept {
  if (s.empty()) return;
#if defined(LOOM_HAVE_OPENSSL)
  OPENSSL_cleanse(s.data(), s.size());
#else
  volatile char* p = s.data();
  for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
#endif
}

#if defined(LOOM_HAVE_OPENSSL)
void secure_wipe(std::array<std::uint8_t, CryptoEngine::kKeyBytes>& key) noexcept {
  OPENSSL_cleanse(key.data(), key.size());
}

Error openssl_error(std::string_view what) { return Error(Errc::Crypto, std::string(what)); }
#endif

}  // namespace

// ── EncryptedBlob ─────────────────────────────────────────────────────

Json EncryptedBlob::to_dict() const {
  return Json{{"v", version},
              {"ct", base64::encode(ciphertext)},
              {"n", base64::encode(nonce)},
              {"s", base64::encode(salt)},
              {"i", iterations}};
}

Result<EncryptedBlob> EncryptedBlob::from_dict(const Json& d) {
  if (!d.is_object()) return Error(Errc::Parse, "encrypted blob must be a JSON object");
  const Json* ct = json::find(d, "ct");
  const Json* n = json::find(d, "n");
  const Json* s = json::find(d, "s");
  if (!ct || !ct->is_string() || !n || !n->is_string() || !s || !s->is_string()) {
    return Error(Errc::Parse, "encrypted blob missing ct/n/s");
  }
  EncryptedBlob b;
  LOOM_TRY_ASSIGN(b.ciphertext, base64::decode(ct->get<std::string>()));
  LOOM_TRY_ASSIGN(b.nonce, base64::decode(n->get<std::string>()));
  LOOM_TRY_ASSIGN(b.salt, base64::decode(s->get<std::string>()));
  b.iterations = static_cast<std::uint32_t>(json::get_int(d, "i", CryptoEngine::kKdfIterations));
  b.version = static_cast<int>(json::get_int(d, "v", 1));
  return b;
}

std::string EncryptedBlob::to_json() const { return json::py_dumps(to_dict()); }

Result<EncryptedBlob> EncryptedBlob::from_json(std::string_view s) {
  LOOM_TRY_ASSIGN(Json j, json::parse(s));
  return from_dict(j);
}

// ── CryptoEngine ────────────────────────────────────────────────────

bool CryptoEngine::available() noexcept {
#if defined(LOOM_HAVE_OPENSSL)
  return true;
#else
  return false;
#endif
}

Result<std::array<std::uint8_t, CryptoEngine::kKeyBytes>> CryptoEngine::derive_key(std::string_view password,
                                                                                   std::string_view salt,
                                                                                   std::uint32_t iterations) const {
#if defined(LOOM_HAVE_OPENSSL)
  std::array<std::uint8_t, kKeyBytes> out{};
  int rc = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                             reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size()),
                             static_cast<int>(iterations), EVP_sha256(), static_cast<int>(kKeyBytes), out.data());
  if (rc != 1) return openssl_error("PBKDF2 key derivation failed");
  return out;
#else
  (void)password;
  (void)salt;
  (void)iterations;
  return Error(Errc::Unavailable, "cryptography backend (OpenSSL) not available - encryption disabled");
#endif
}

Result<EncryptedBlob> CryptoEngine::encrypt(std::string_view plaintext, std::string_view password) const {
#if defined(LOOM_HAVE_OPENSSL)
  std::string salt = random_bytes(kSaltBytes);
  auto key_r = derive_key(password, salt, kKdfIterations);
  if (!key_r) return key_r.error();
  auto key = *key_r;
  std::string nonce = random_bytes(kNonceBytes);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    secure_wipe(key);
    return openssl_error("failed to allocate AES-GCM context");
  }
  struct Guard {
    EVP_CIPHER_CTX* c;
    ~Guard() { EVP_CIPHER_CTX_free(c); }
  } guard{ctx};

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes), nullptr) != 1 ||
      EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), reinterpret_cast<const unsigned char*>(nonce.data())) != 1) {
    secure_wipe(key);
    return openssl_error("AES-GCM init failed");
  }

  std::string ciphertext(plaintext.size(), '\0');
  int len = 0;
  int total = 0;
  if (!plaintext.empty()) {
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
      secure_wipe(key);
      return openssl_error("AES-GCM encrypt failed");
    }
    total += len;
  }
  unsigned char final_buf[16];
  int flen = 0;
  if (EVP_EncryptFinal_ex(ctx, final_buf, &flen) != 1) {
    secure_wipe(key);
    return openssl_error("AES-GCM finalize failed");
  }
  total += flen;
  ciphertext.resize(static_cast<std::size_t>(total));

  unsigned char tag[kTagBytes];
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagBytes), tag) != 1) {
    secure_wipe(key);
    return openssl_error("AES-GCM get-tag failed");
  }
  secure_wipe(key);
  ciphertext.append(reinterpret_cast<const char*>(tag), kTagBytes);

  EncryptedBlob blob;
  blob.ciphertext = std::move(ciphertext);
  blob.nonce = std::move(nonce);
  blob.salt = std::move(salt);
  blob.iterations = kKdfIterations;
  blob.version = 1;
  return blob;
#else
  (void)plaintext;
  (void)password;
  return Error(Errc::Unavailable, "cryptography backend (OpenSSL) not available - encryption disabled");
#endif
}

Result<std::string> CryptoEngine::decrypt(const EncryptedBlob& blob, std::string_view password) const {
#if defined(LOOM_HAVE_OPENSSL)
  if (blob.nonce.size() != kNonceBytes) return Error(Errc::Crypto, "invalid nonce length");
  if (blob.ciphertext.size() < kTagBytes) return Error(Errc::Crypto, "ciphertext too short");
  auto key_r = derive_key(password, blob.salt, blob.iterations);
  if (!key_r) return key_r.error();
  auto key = *key_r;

  std::size_t ct_len = blob.ciphertext.size() - kTagBytes;
  unsigned char tag[kTagBytes];
  std::memcpy(tag, blob.ciphertext.data() + ct_len, kTagBytes);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    secure_wipe(key);
    return openssl_error("failed to allocate AES-GCM context");
  }
  struct Guard {
    EVP_CIPHER_CTX* c;
    ~Guard() { EVP_CIPHER_CTX_free(c); }
  } guard{ctx};

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes), nullptr) != 1 ||
      EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(),
                         reinterpret_cast<const unsigned char*>(blob.nonce.data())) != 1) {
    secure_wipe(key);
    return openssl_error("AES-GCM init failed");
  }

  std::string plaintext(ct_len, '\0');
  int len = 0;
  int total = 0;
  if (ct_len > 0) {
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                          reinterpret_cast<const unsigned char*>(blob.ciphertext.data()),
                          static_cast<int>(ct_len)) != 1) {
      secure_wipe(key);
      secure_wipe(plaintext);
      return Error(Errc::Crypto, "decryption failed (wrong password or tampered data)");
    }
    total += len;
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagBytes), tag) != 1) {
    secure_wipe(key);
    secure_wipe(plaintext);
    return openssl_error("failed to set AES-GCM tag");
  }
  unsigned char final_buf[16];
  int flen = 0;
  int fin_rc = EVP_DecryptFinal_ex(ctx, final_buf, &flen);
  secure_wipe(key);
  if (fin_rc != 1) {
    secure_wipe(plaintext);
    return Error(Errc::Crypto, "decryption failed (wrong password or tampered data)");
  }
  total += flen;
  plaintext.resize(static_cast<std::size_t>(total));
  return plaintext;
#else
  (void)blob;
  (void)password;
  return Error(Errc::Unavailable, "cryptography backend (OpenSSL) not available - encryption disabled");
#endif
}

// ── CryptoVault ────────────────────────────────────────────────────

CryptoVault::CryptoVault(std::filesystem::path file) : file_(std::move(file)) {}

CryptoVault::~CryptoVault() { lock(); }

bool CryptoVault::configured() const {
  std::error_code ec;
  return std::filesystem::exists(file_, ec);
}

bool CryptoVault::unlocked() const {
  std::lock_guard lk(mu_);
  return password_.has_value();
}

Status CryptoVault::setup(std::string_view password) {
  if (!CryptoEngine::available()) {
    return Error(Errc::Unavailable, "cryptography backend (OpenSSL) not available - encryption disabled");
  }
  std::lock_guard lk(mu_);
  std::error_code ec;
  if (std::filesystem::exists(file_, ec)) return Error(Errc::AlreadyExists, "crypto is already configured");
  auto blob = engine_.encrypt(kVerifyMagic, password);
  if (!blob) return blob.error();
  fsutil::AtomicWriteOptions wo;
  wo.owner_only = true;
  LOOM_TRY(fsutil::atomic_write(file_, blob->to_json(), wo));
  password_ = std::string(password);
  log::info(kLog, "crypto vault configured ({})", file_.string());
  return {};
}

Status CryptoVault::unlock(std::string_view password) {
  if (!CryptoEngine::available()) {
    return Error(Errc::Unavailable, "cryptography backend (OpenSSL) not available - encryption disabled");
  }
  std::lock_guard lk(mu_);
  auto raw = fsutil::read_file(file_);
  if (!raw) return Error(Errc::NotFound, "crypto is not configured");
  auto blob = EncryptedBlob::from_json(*raw);
  if (!blob) return Error(Errc::Crypto, "crypto verifier is corrupt: " + blob.error().message);
  auto plain = engine_.decrypt(*blob, password);
  if (!plain || !constant_time_equal(*plain, kVerifyMagic)) {
    if (plain) secure_wipe(*plain);
    log::warn(kLog, "crypto unlock failed: wrong password");
    return Error(Errc::Crypto, "wrong password");
  }
  secure_wipe(*plain);
  if (password_) secure_wipe(*password_);
  password_ = std::string(password);
  return {};
}

void CryptoVault::lock() {
  std::lock_guard lk(mu_);
  if (password_) {
    secure_wipe(*password_);
    password_.reset();
  }
}

Result<std::string> CryptoVault::encrypt_text(std::string_view plaintext) {
  std::string pw;
  {
    std::lock_guard lk(mu_);
    if (!password_) return Error(Errc::Auth, "crypto vault is locked");
    pw = *password_;
  }
  auto blob = engine_.encrypt(plaintext, pw);
  secure_wipe(pw);
  if (!blob) return blob.error();
  return blob->to_json();
}

Result<std::string> CryptoVault::decrypt_text(std::string_view blob_json) {
  std::string pw;
  {
    std::lock_guard lk(mu_);
    if (!password_) return Error(Errc::Auth, "crypto vault is locked");
    pw = *password_;
  }
  auto blob = EncryptedBlob::from_json(blob_json);
  if (!blob) {
    secure_wipe(pw);
    return blob.error();
  }
  auto r = engine_.decrypt(*blob, pw);
  secure_wipe(pw);
  return r;
}

Json CryptoVault::status() const {
  return Json{{"available", CryptoEngine::available()},
              {"configured", configured()},
              {"unlocked", unlocked()},
              {"kdf", "pbkdf2-sha256"},
              {"iterations", CryptoEngine::kKdfIterations},
              {"cipher", "aes-256-gcm"}};
}

}  // namespace loom
