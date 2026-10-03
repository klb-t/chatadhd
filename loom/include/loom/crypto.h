// loom/crypto.h — port of core/crypto.py + a session vault for the C API.
//                                                           [OWNER: wave 2 crypto/media/github]
// Wire format (must interoperate with Python both ways):
//   AES-256-GCM, key = PBKDF2-HMAC-SHA256(password UTF-8, salt 32 bytes,
//   iterations 600000, 32 bytes), nonce 12 bytes, no AAD; ciphertext = GCM
//   ciphertext || 16-byte tag (cryptography's AESGCM.encrypt output).
//   JSON: {"v": 1, "ct": b64, "n": b64, "s": b64, "i": 600000} written with
//   json.dumps (EncryptedBlob.to_json); from_dict defaults i=600000, v=1.
// Backend: OpenSSL EVP (AES-GCM + PKCS5_PBKDF2_HMAC). Without OpenSSL
// available() is false and every operation returns Errc::Unavailable
// (Python degraded the same way without `cryptography`).
// Wrong password / tampered data -> Errc::Crypto (Python raised InvalidTag).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

struct EncryptedBlob {
  std::string ciphertext;  // raw bytes (ct || tag)
  std::string nonce;       // 12 raw bytes
  std::string salt;        // 32 raw bytes
  std::uint32_t iterations = 600000;
  int version = 1;

  Json to_dict() const;                                // {"v","ct","n","s","i"} (Python key order)
  static Result<EncryptedBlob> from_dict(const Json& d);
  std::string to_json() const;                         // json.dumps(to_dict())
  static Result<EncryptedBlob> from_json(std::string_view s);
};

class CryptoEngine {
 public:
  static constexpr std::uint32_t kKdfIterations = 600000;
  static constexpr std::size_t kSaltBytes = 32;
  static constexpr std::size_t kKeyBytes = 32;
  static constexpr std::size_t kNonceBytes = 12;
  static constexpr std::size_t kTagBytes = 16;

  static bool available() noexcept;

  Result<std::array<std::uint8_t, kKeyBytes>> derive_key(std::string_view password, std::string_view salt,
                                                         std::uint32_t iterations = kKdfIterations) const;
  Result<EncryptedBlob> encrypt(std::string_view plaintext, std::string_view password) const;
  Result<std::string> decrypt(const EncryptedBlob& blob, std::string_view password) const;
};

// Session vault behind loom_crypto_*: a verifier blob in <data>/crypto.json
// proves the password; unlock() keeps the password in memory (wiped on lock
// and destruction) so encrypt_text/decrypt_text don't need it per call.
// Threat model unchanged from Python: protects data at rest, not a
// compromised runtime.
class CryptoVault {
 public:
  explicit CryptoVault(std::filesystem::path file);
  ~CryptoVault();
  CryptoVault(const CryptoVault&) = delete;
  CryptoVault& operator=(const CryptoVault&) = delete;

  bool configured() const;
  bool unlocked() const;
  Status setup(std::string_view password);   // Errc::AlreadyExists when configured
  Status unlock(std::string_view password);  // Errc::Crypto on a wrong password
  void lock();
  Result<std::string> encrypt_text(std::string_view plaintext);  // EncryptedBlob JSON; Errc::Auth when locked
  Result<std::string> decrypt_text(std::string_view blob_json);
  // {"available","configured","unlocked","kdf":"pbkdf2-sha256","iterations","cipher":"aes-256-gcm"}
  Json status() const;

 private:
  std::filesystem::path file_;
  CryptoEngine engine_;
  mutable std::mutex mu_;
  std::optional<std::string> password_;
};

}  // namespace loom
