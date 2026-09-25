// OWNER: wave 2 crypto/media/github. Stub.
#include "loom/crypto.h"

#include "stub.h"

namespace loom {

Json EncryptedBlob::to_dict() const {
  return Json{{"v", version}, {"ct", ""}, {"n", ""}, {"s", ""}, {"i", iterations}};  // STUB: wave2 (base64 fields)
}
Result<EncryptedBlob> EncryptedBlob::from_dict(const Json&) {
  return LOOM_NOT_IMPLEMENTED("EncryptedBlob::from_dict");  // STUB: wave2
}
std::string EncryptedBlob::to_json() const { return json::py_dumps(to_dict()); }
Result<EncryptedBlob> EncryptedBlob::from_json(std::string_view s) {
  LOOM_TRY_ASSIGN(Json j, json::parse(s));
  return from_dict(j);
}

bool CryptoEngine::available() noexcept { return false; }  // STUB: wave2

Result<std::array<std::uint8_t, CryptoEngine::kKeyBytes>> CryptoEngine::derive_key(std::string_view, std::string_view,
                                                                                   std::uint32_t) const {
  return LOOM_NOT_IMPLEMENTED("CryptoEngine::derive_key");  // STUB: wave2
}
Result<EncryptedBlob> CryptoEngine::encrypt(std::string_view, std::string_view) const {
  return LOOM_NOT_IMPLEMENTED("CryptoEngine::encrypt");  // STUB: wave2
}
Result<std::string> CryptoEngine::decrypt(const EncryptedBlob&, std::string_view) const {
  return LOOM_NOT_IMPLEMENTED("CryptoEngine::decrypt");  // STUB: wave2
}

CryptoVault::CryptoVault(std::filesystem::path file) : file_(std::move(file)) {}
CryptoVault::~CryptoVault() { lock(); }
bool CryptoVault::configured() const { return false; }  // STUB: wave2
bool CryptoVault::unlocked() const {
  std::lock_guard lk(mu_);
  return password_.has_value();
}
Status CryptoVault::setup(std::string_view) { return LOOM_NOT_IMPLEMENTED("CryptoVault::setup"); }    // STUB: wave2
Status CryptoVault::unlock(std::string_view) { return LOOM_NOT_IMPLEMENTED("CryptoVault::unlock"); }  // STUB: wave2
void CryptoVault::lock() {
  std::lock_guard lk(mu_);
  if (password_) {
    volatile char* p = password_->data();
    for (std::size_t i = 0; i < password_->size(); ++i) p[i] = 0;
    password_.reset();
  }
}
Result<std::string> CryptoVault::encrypt_text(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("CryptoVault::encrypt_text");  // STUB: wave2
}
Result<std::string> CryptoVault::decrypt_text(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("CryptoVault::decrypt_text");  // STUB: wave2
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
