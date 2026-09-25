// Unit tests for loom/crypto.h: CryptoEngine (AES-256-GCM + PBKDF2-SHA256)
// and CryptoVault (the session wrapper behind loom_crypto_*).
//
// LOOM_HAVE_OPENSSL is a private compile definition of loom_core (not
// propagated to loom_tests), so availability is branched on at runtime via
// CryptoEngine::available() rather than with #ifdef.
#include <doctest/doctest.h>

#include <string>

#include "loom/crypto.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("crypto") {
  TEST_CASE("EncryptedBlob JSON round-trip matches the Python wire format") {
    EncryptedBlob b;
    b.ciphertext = "\x01\x02\x03hello";
    b.nonce = std::string(12, '\x09');
    b.salt = std::string(32, '\x07');
    b.iterations = 600000;
    b.version = 1;

    Json d = b.to_dict();
    // Key order and names must match Python's EncryptedBlob.to_dict() exactly.
    std::vector<std::string> keys;
    for (auto it = d.begin(); it != d.end(); ++it) keys.push_back(it.key());
    CHECK(keys == std::vector<std::string>{"v", "ct", "n", "s", "i"});
    CHECK(d["v"] == 1);
    CHECK(d["i"] == 600000);

    std::string json_text = b.to_json();
    EncryptedBlob back = unwrap(EncryptedBlob::from_json(json_text));
    CHECK(back.ciphertext == b.ciphertext);
    CHECK(back.nonce == b.nonce);
    CHECK(back.salt == b.salt);
    CHECK(back.iterations == b.iterations);
    CHECK(back.version == b.version);
  }

  TEST_CASE("from_dict defaults i=600000 and v=1 when absent") {
    Json d{{"ct", base64::encode("x")}, {"n", base64::encode(std::string(12, '\0'))}, {"s", base64::encode(std::string(32, '\0'))}};
    EncryptedBlob b = unwrap(EncryptedBlob::from_dict(d));
    CHECK(b.iterations == 600000);
    CHECK(b.version == 1);
  }

  TEST_CASE("from_dict rejects a blob missing required fields") {
    CHECK_FALSE(EncryptedBlob::from_dict(Json{{"ct", "x"}}));
    CHECK_FALSE(EncryptedBlob::from_dict(Json::array()));
  }

  TEST_CASE("encrypt/decrypt: available() true -> works, false -> Unavailable everywhere") {
    CryptoEngine eng;
    if (!CryptoEngine::available()) {
      auto r = eng.encrypt("x", "pw");
      CHECK_FALSE(r);
      CHECK(r.error().code == Errc::Unavailable);
      EncryptedBlob dummy;
      dummy.nonce = std::string(12, '\0');
      dummy.salt = std::string(32, '\0');
      dummy.ciphertext = std::string(16, '\0');
      auto d = eng.decrypt(dummy, "pw");
      CHECK_FALSE(d);
      CHECK(d.error().code == Errc::Unavailable);
      auto k = eng.derive_key("pw", std::string(32, '\0'));
      CHECK_FALSE(k);
      CHECK(k.error().code == Errc::Unavailable);
      return;
    }

    for (const std::string& plain : {std::string(""), std::string("héllo wörld \xe2\x9c\x93 emoji \xf0\x9f\x98\x80"),
                                     std::string(1'000'000, 'A')}) {
      EncryptedBlob blob = unwrap(eng.encrypt(plain, "correct horse battery staple"));
      CHECK(blob.nonce.size() == CryptoEngine::kNonceBytes);
      CHECK(blob.salt.size() == CryptoEngine::kSaltBytes);
      CHECK(blob.ciphertext.size() == plain.size() + CryptoEngine::kTagBytes);
      std::string back = unwrap(eng.decrypt(blob, "correct horse battery staple"));
      CHECK(back == plain);
    }
  }

  TEST_CASE("two encryptions of the same plaintext use different salt/nonce/ciphertext" * doctest::skip(!CryptoEngine::available())) {
    CryptoEngine eng;
    EncryptedBlob a = unwrap(eng.encrypt("same text", "pw"));
    EncryptedBlob b = unwrap(eng.encrypt("same text", "pw"));
    CHECK(a.salt != b.salt);
    CHECK(a.nonce != b.nonce);
    CHECK(a.ciphertext != b.ciphertext);
  }

  TEST_CASE("wrong password fails cleanly" * doctest::skip(!CryptoEngine::available())) {
    CryptoEngine eng;
    EncryptedBlob blob = unwrap(eng.encrypt("secret", "right password"));
    auto r = eng.decrypt(blob, "wrong password");
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Crypto);
  }

  TEST_CASE("tampered ciphertext fails cleanly" * doctest::skip(!CryptoEngine::available())) {
    CryptoEngine eng;
    EncryptedBlob blob = unwrap(eng.encrypt("secret payload", "pw"));
    blob.ciphertext[0] = static_cast<char>(blob.ciphertext[0] ^ 0xFF);
    auto r = eng.decrypt(blob, "pw");
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Crypto);
  }

  TEST_CASE("tampered tag fails cleanly" * doctest::skip(!CryptoEngine::available())) {
    CryptoEngine eng;
    EncryptedBlob blob = unwrap(eng.encrypt("secret payload", "pw"));
    blob.ciphertext.back() = static_cast<char>(blob.ciphertext.back() ^ 0xFF);
    auto r = eng.decrypt(blob, "pw");
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Crypto);
  }

  TEST_CASE("derive_key is deterministic for the same password/salt/iterations" * doctest::skip(!CryptoEngine::available())) {
    CryptoEngine eng;
    std::string salt(32, '\x42');
    auto k1 = unwrap(eng.derive_key("pw", salt, 10000));
    auto k2 = unwrap(eng.derive_key("pw", salt, 10000));
    CHECK(k1 == k2);
    auto k3 = unwrap(eng.derive_key("different", salt, 10000));
    CHECK(k1 != k3);
  }

  TEST_CASE("CryptoVault: setup, unlock, lock, status") {
    fsutil::TempDir td;
    CryptoVault vault(td.path() / "crypto.json");

    Json st0 = vault.status();
    CHECK(st0["available"] == CryptoEngine::available());
    CHECK(st0["configured"] == false);
    CHECK(st0["unlocked"] == false);
    CHECK(st0["kdf"] == "pbkdf2-sha256");
    CHECK(st0["cipher"] == "aes-256-gcm");
    CHECK(st0["iterations"] == 600000);

    auto setup_r = vault.setup("hunter2");
    if (!CryptoEngine::available()) {
      CHECK_FALSE(setup_r);
      CHECK(setup_r.error().code == Errc::Unavailable);
      return;
    }
    LOOM_REQUIRE_OK(setup_r);
    CHECK(vault.configured());
    CHECK(vault.unlocked());  // setup() leaves the vault unlocked with the new password
    CHECK(std::filesystem::exists(td.path() / "crypto.json"));

    // Setting up again is refused.
    auto again = vault.setup("other");
    CHECK_FALSE(again);
    CHECK(again.error().code == Errc::AlreadyExists);

    vault.lock();
    CHECK_FALSE(vault.unlocked());
    CHECK_FALSE(vault.encrypt_text("x"));  // locked
  }

  TEST_CASE("CryptoVault: encrypt/decrypt only work while unlocked; wrong password refused" *
           doctest::skip(!CryptoEngine::available())) {
    fsutil::TempDir td;
    CryptoVault vault(td.path() / "crypto.json");
    LOOM_REQUIRE_OK(vault.setup("correct password"));

    std::string blob = unwrap(vault.encrypt_text("hello vault"));
    std::string back = unwrap(vault.decrypt_text(blob));
    CHECK(back == "hello vault");

    vault.lock();
    auto locked_r = vault.encrypt_text("x");
    CHECK_FALSE(locked_r);
    CHECK(locked_r.error().code == Errc::Auth);
    auto locked_d = vault.decrypt_text(blob);
    CHECK_FALSE(locked_d);
    CHECK(locked_d.error().code == Errc::Auth);

    auto wrong = vault.unlock("nope");
    CHECK_FALSE(wrong);
    CHECK(wrong.error().code == Errc::Crypto);
    CHECK_FALSE(vault.unlocked());

    LOOM_REQUIRE_OK(vault.unlock("correct password"));
    CHECK(vault.unlocked());
    std::string back2 = unwrap(vault.decrypt_text(blob));
    CHECK(back2 == "hello vault");
  }

  TEST_CASE("CryptoVault: a fresh vault over the same file requires unlock()" * doctest::skip(!CryptoEngine::available())) {
    fsutil::TempDir td;
    std::filesystem::path file = td.path() / "crypto.json";
    {
      CryptoVault vault(file);
      LOOM_REQUIRE_OK(vault.setup("pw"));
    }
    CryptoVault vault2(file);
    CHECK(vault2.configured());
    CHECK_FALSE(vault2.unlocked());
    LOOM_REQUIRE_OK(vault2.unlock("pw"));
    CHECK(vault2.unlocked());
  }
}
