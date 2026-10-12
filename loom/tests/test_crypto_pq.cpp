// T5b: crypto suite primitives, KATs, hybrid KEM/signature, sealed v1/v2, status.
#include <filesystem>

#include "doctest/doctest.h"
#include "loom/crypto.h"
#include "loom/crypto_suite.h"
#include "loom/graph_entry.h"
#include "loom/runtime_profile.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"

namespace fs = std::filesystem;
using namespace loom;
using namespace loom::cs;

namespace {
Bytes hx(std::string_view h) {
  auto b = from_hex(h);
  REQUIRE(b.has_value());
  return *b;
}
Json fixture(const char* name) {
  auto t = fsutil::read_file(fs::path(LOOM_TEST_FIXTURES) / "crypto" / name);
  REQUIRE(t.has_value());
  Json j = Json::parse(*t, nullptr, false);
  REQUIRE(j.is_object());
  return j;
}
Bytes jh(const Json& j, const char* k) { return hx(j.at(k).get<std::string>()); }

// Runs `body` against the active backend and the portable (vendored) one.
template <class F>
void each_backend(F body) {
  body(backend());
  body(portable_backend());
}
}  // namespace

TEST_CASE("crypto_pq: SHA-256, SHA3-256, SHAKE256, HKDF known answers") {
  each_backend([](const CryptoBackend& b) {
    CHECK(to_hex(*b.sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(to_hex(*b.sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // RFC 5869 test case 1
    auto okm = b.hkdf_sha256(hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), hx("000102030405060708090a0b0c"),
                             hx("f0f1f2f3f4f5f6f7f8f9"), 42);
    REQUIRE(okm);
    CHECK(to_hex(*okm) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
    // RFC 5869 test case 3 (empty salt, empty info)
    auto o3 = b.hkdf_sha256(hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), {}, {}, 42);
    REQUIRE(o3);
    CHECK(to_hex(*o3) == "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
    // long input spans several blocks
    CHECK(to_hex(*b.sha256(std::string(1000, 'a'))) ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
  });
  CHECK(to_hex(sha3_256("")) == "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
  CHECK(to_hex(sha3_256("abc")) == "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
  CHECK(to_hex(shake256("", 32)) == "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f");
  // multi-block absorb + multi-block squeeze agree with themselves across lengths
  Bytes long_in(500, 'x');
  CHECK(shake256(long_in, 300).substr(0, 32) == shake256(long_in, 32));
}

TEST_CASE("crypto_pq: AES-256-GCM seal/open, AAD binding, tamper") {
  const CryptoBackend& b = backend();
  if (!b.available(Prim::Aes256Gcm)) return;
  Bytes key(32, 'k'), nonce(12, 'n');
  auto ct = b.aes256gcm_seal(key, nonce, "aad", "hello");
  REQUIRE(ct);
  CHECK(ct->size() == 5 + 16);
  auto pt = b.aes256gcm_open(key, nonce, "aad", *ct);
  REQUIRE(pt);
  CHECK(*pt == "hello");
  CHECK_FALSE(b.aes256gcm_open(key, nonce, "other", *ct));
  Bytes bad = *ct;
  bad[0] ^= 1;
  CHECK_FALSE(b.aes256gcm_open(key, nonce, "aad", bad));
  // NIST SP 800-38D / McGrew-Viega test case 15 (AES-256, 96-bit IV, no AAD, 64-byte plaintext)
  auto k15 = hx("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
  auto iv15 = hx("cafebabefacedbaddecaf888");
  auto p15 = hx("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255");
  auto c15 = b.aes256gcm_seal(k15, iv15, {}, p15);
  REQUIRE(c15);
  CHECK(to_hex(*c15) ==
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad"
        "b094dac5d93471bdec1a502270e3cc6c");
}

TEST_CASE("crypto_pq: X25519 RFC 7748") {
  each_backend([](const CryptoBackend& b) {
    // §5.2 vector 1
    auto r = b.x25519(hx("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
                      hx("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"));
    REQUIRE(r);
    CHECK(to_hex(*r) == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    // §6.1 Diffie-Hellman
    auto a_sk = hx("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto b_sk = hx("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    auto a_pk = b.x25519_public(a_sk), b_pk = b.x25519_public(b_sk);
    REQUIRE(a_pk);
    REQUIRE(b_pk);
    CHECK(to_hex(*a_pk) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CHECK(to_hex(*b_pk) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    auto s1 = b.x25519(a_sk, *b_pk), s2 = b.x25519(b_sk, *a_pk);
    REQUIRE(s1);
    REQUIRE(s2);
    CHECK(*s1 == *s2);
    CHECK(to_hex(*s1) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    // low-order point (all zero u) is rejected
    CHECK_FALSE(b.x25519(a_sk, Bytes(32, '\0')));
  });
}

TEST_CASE("crypto_pq: Ed25519 RFC 8032 section 7.1") {
  struct V { const char *sk, *pk, *msg, *sig; };
  const V vs[] = {
      {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
       "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
       "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
      {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
       "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
       "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
  };
  each_backend([&](const CryptoBackend& b) {
    for (const auto& v : vs) {
      auto pk = b.ed25519_public(hx(v.sk));
      REQUIRE(pk);
      CHECK(to_hex(*pk) == v.pk);
      auto sig = b.ed25519_sign(hx(v.sk), hx(v.msg));
      REQUIRE(sig);
      CHECK(to_hex(*sig) == v.sig);
      CHECK(b.ed25519_verify(hx(v.pk), hx(v.msg), hx(v.sig)));
      Bytes bad = hx(v.sig);
      bad[10] ^= 1;
      CHECK_FALSE(b.ed25519_verify(hx(v.pk), hx(v.msg), bad));
      CHECK_FALSE(b.ed25519_verify(hx(v.pk), "tampered", hx(v.sig)));
    }
  });
}

TEST_CASE("crypto_pq: Argon2id RFC 9106 section 5.3") {
  each_backend([](const CryptoBackend& b) {
    auto tag = b.argon2id(Bytes(32, '\x01'), Bytes(16, '\x02'), Argon2Params{3, 32, 4}, 32, Bytes(8, '\x03'),
                          Bytes(12, '\x04'));
    REQUIRE(tag);
    CHECK(to_hex(*tag) == "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659");
    // unset parameters are an error, never silently defaulted
    CHECK_FALSE(b.argon2id("pw", Bytes(16, 's'), Argon2Params{}, 32));
    // memory below the standard's minimum (8*p KiB) is reported, not raised
    CHECK_FALSE(b.argon2id("pw", Bytes(16, 's'), Argon2Params{1, 8, 4}, 32));
  });
}

TEST_CASE("crypto_pq: ML-KEM-768 NIST ACVP (FIPS 203)") {
  Json f = fixture("acvp_mlkem768.json");
  const CryptoBackend& b = backend();
  for (const auto& t : f.at("mlkem768_keygen")) {
    auto kp = b.mlkem768_keypair(jh(t, "d") + jh(t, "z"));
    REQUIRE(kp);
    CHECK(kp->pk == jh(t, "ek"));
    CHECK(kp->sk == jh(t, "dk"));
  }
  for (const auto& t : f.at("mlkem768_encap")) {
    auto e = b.mlkem768_encaps(jh(t, "ek"), jh(t, "m"));
    REQUIRE(e);
    CHECK(e->ct == jh(t, "c"));
    CHECK(e->ss == jh(t, "k"));
  }
  for (const auto& t : f.at("mlkem768_decap")) {
    auto ss = b.mlkem768_decaps(jh(t, "dk"), jh(t, "c"));
    REQUIRE(ss);
    CHECK(*ss == jh(t, "k"));
  }
  for (const auto& t : f.at("mlkem768_ekcheck")) {
    auto e = b.mlkem768_encaps(jh(t, "ek"), Bytes(32, 'm'));
    CHECK(e.has_value() == t.at("testPassed").get<bool>());
  }
}

TEST_CASE("crypto_pq: ML-DSA-65 NIST ACVP (FIPS 204)") {
  Json f = fixture("acvp_mldsa65.json");
  const CryptoBackend& b = backend();
  for (const auto& t : f.at("mldsa65_keygen")) {
    auto kp = b.mldsa65_keypair(jh(t, "seed"));
    REQUIRE(kp);
    CHECK(kp->pk == jh(t, "pk"));
    CHECK(kp->sk == jh(t, "sk"));
  }
  for (const auto& t : f.at("mldsa65_siggen_det_pure")) {
    auto sig = b.mldsa65_sign(jh(t, "sk"), jh(t, "message"), jh(t, "context"), {});
    REQUIRE(sig);
    CHECK(*sig == jh(t, "signature"));
  }
  for (const auto& t : f.at("mldsa65_sigver_pure")) {
    bool ok = b.mldsa65_verify(jh(t, "pk"), jh(t, "message"), jh(t, "context"), jh(t, "signature"));
    CHECK_MESSAGE(ok == t.at("testPassed").get<bool>(), t.at("reason").get<std::string>());
  }
}

TEST_CASE("crypto_pq: X-Wing draft vectors") {
  Json f = fixture("xwing_draft_vectors.json");
  REQUIRE(f.is_object());  // wrapper object, see fixture README
  for (const auto& t : f.at("vectors")) {
    auto kp = xwing_keygen(jh(t, "seed"));
    REQUIRE(kp);
    CHECK(kp->sk == jh(t, "sk"));
    CHECK(kp->pk == jh(t, "pk"));
    auto e = xwing_encaps(jh(t, "pk"), jh(t, "eseed"));
    REQUIRE(e);
    CHECK(e->ct == jh(t, "ct"));
    CHECK(e->ss == jh(t, "ss"));
    auto ss = xwing_decaps(jh(t, "sk"), jh(t, "ct"));
    REQUIRE(ss);
    CHECK(*ss == jh(t, "ss"));
  }
}

TEST_CASE("crypto_pq: hybrid KEM round trip and tamper behaviour") {
  auto kp = xwing_keygen();
  REQUIRE(kp);
  CHECK(kp->pk.size() == kXwingPk);
  CHECK(kp->sk.size() == kXwingSk);
  auto e = xwing_encaps(kp->pk);
  REQUIRE(e);
  CHECK(e->ct.size() == kXwingCt);
  auto ss = xwing_decaps(kp->sk, e->ct);
  REQUIRE(ss);
  CHECK(*ss == e->ss);
  // Changing any byte of either half yields a different secret (ML-KEM implicit rejection / X25519 mismatch).
  for (std::size_t pos : {std::size_t(0), std::size_t(700), kMlKem768Ct - 1, kMlKem768Ct, kXwingCt - 1}) {
    Bytes ct = e->ct;
    ct[pos] ^= 0x01;
    auto r = xwing_decaps(kp->sk, ct);
    if (r) CHECK(*r != e->ss);
  }
  // A different key pair derives a different secret.
  auto other = xwing_keygen();
  REQUIRE(other);
  auto wrong = xwing_decaps(other->sk, e->ct);
  REQUIRE(wrong);
  CHECK(*wrong != e->ss);
  // Wrong lengths are errors.
  CHECK_FALSE(xwing_encaps(kp->pk.substr(1)));
  CHECK_FALSE(xwing_decaps(kp->sk, e->ct.substr(1)));
  // Malformed ML-KEM public key (coefficient >= q) is rejected at encapsulation.
  Bytes bad_pk = kp->pk;
  bad_pk[0] = '\xff';
  bad_pk[1] = '\xff';
  CHECK_FALSE(xwing_encaps(bad_pk));
  // Low-order X25519 key half is rejected.
  Bytes low = kp->pk;
  std::fill(low.begin() + static_cast<std::ptrdiff_t>(kMlKem768Pk), low.end(), '\0');
  CHECK_FALSE(xwing_encaps(low));
}

TEST_CASE("crypto_pq: hybrid signature needs both halves") {
  auto kp = hybrid_sig_keygen();
  REQUIRE(kp);
  CHECK(kp->pk.size() == kHybridSigPk);
  auto sig = hybrid_sign(kp->sk, "decision 42", "ctx-a");
  REQUIRE(sig);
  CHECK(sig->size() == kHybridSigBytes);
  CHECK(hybrid_verify(kp->pk, "decision 42", "ctx-a", *sig));
  // altered message / context
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 43", "ctx-a", *sig));
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-b", *sig));
  // forged Ed25519 half only
  Bytes s1 = *sig;
  s1[5] ^= 1;
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", s1));
  // forged ML-DSA half only
  Bytes s2 = *sig;
  s2[64 + 100] ^= 1;
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", s2));
  // each half valid for the other key, other half swapped in
  auto kp2 = hybrid_sig_keygen();
  REQUIRE(kp2);
  auto sig2 = hybrid_sign(kp2->sk, "decision 42", "ctx-a");
  REQUIRE(sig2);
  Bytes mixed_a = sig->substr(0, 64) + sig2->substr(64);
  Bytes mixed_b = sig2->substr(0, 64) + sig->substr(64);
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", mixed_a));
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", mixed_b));
  CHECK_FALSE(hybrid_verify(kp2->pk, "decision 42", "ctx-a", *sig));
  // a bare Ed25519 or bare ML-DSA signature over the message (no label binding) does not verify as hybrid
  auto bare_ed = backend().ed25519_sign(kp->sk.substr(0, 32), "decision 42");
  REQUIRE(bare_ed);
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", *bare_ed + sig->substr(64)));
  // wrong length
  CHECK_FALSE(hybrid_verify(kp->pk, "decision 42", "ctx-a", sig->substr(1)));
  // JSON container
  auto js = hybrid_sign_json(kp->sk, "m");
  REQUIRE(js);
  CHECK(hybrid_verify_json(kp->pk, "m", {}, *js));
  CHECK_FALSE(hybrid_verify_json(kp->pk, "m2", {}, *js));
  CHECK_FALSE(hybrid_verify_json(kp->pk, "m", {}, "{\"schema\":\"x\"}"));
}

namespace {
Json test_crypto_section() {
  // Cheap Argon2id test parameters supplied as data (the shipped presets are exercised in the pack test).
  return Json::parse(R"({
    "selected_preset":"legacy",
    "suite_presets":{
      "legacy":{"password_kdf":"pbkdf2-hmac-sha256","kem":"rsa-oaep-3072-sha256","handoff_envelope":"credential_envelope/2"},
      "pq_hybrid":{"password_kdf":"argon2id","argon2id_preset":"t_small","kem":"xwing-x25519-mlkem768",
                   "signature":"hybrid-ed25519-mldsa65","handoff_envelope":"credential_envelope/3"},
      "pq_other":{"password_kdf":"argon2id","argon2id_preset":"t_other"},
      "pq_calibrated":{"password_kdf":"argon2id","argon2id_preset":"calibrated"}},
    "params":{"argon2id_presets":{"t_small":{"t":2,"m_kib":64,"p":2},"t_other":{"t":1,"m_kib":128,"p":1},
                                  "calibrated":{"t":"chosen","m_kib":"chosen","p":"chosen"}}}})");
}
}  // namespace

TEST_CASE("crypto_pq: suite config comes from data") {
  Json c = test_crypto_section();
  auto legacy = suite_config_from_json(c);
  REQUIRE(legacy);
  CHECK_FALSE(legacy->pq());
  auto pq = suite_config_from_json(c, "pq_hybrid");
  REQUIRE(pq);
  CHECK(pq->pq());
  CHECK(pq->argon2.t == 2);
  CHECK(pq->argon2.m_kib == 64);
  CHECK(pq->argon2.p == 2);
  CHECK_FALSE(suite_config_from_json(c, "nope"));
  CHECK_FALSE(suite_config_from_json(c, "pq_calibrated"));  // unresolved calibration is an error
  c["selected_preset"] = "pq_other";
  auto sel = suite_config_from_json(c);
  REQUIRE(sel);
  CHECK(sel->preset_id == "pq_other");
}

TEST_CASE("crypto_pq: shipped storage_security pack resolves both presets") {
  auto prof = RuntimeProfile::builtin("storage_security");
  REQUIRE(prof);
  const Json& c = prof->values().at("crypto");
  auto legacy = suite_config_from_json(c, "legacy");
  REQUIRE(legacy);
  CHECK_FALSE(legacy->pq());
  auto pq = suite_config_from_json(c, "pq_hybrid");
  REQUIRE(pq);
  CHECK(pq->argon2.t == 3);        // RFC 9106 second recommended option, read from the pack
  CHECK(pq->argon2.m_kib == 65536);
  CHECK(pq->argon2.p == 4);
}

TEST_CASE("crypto_pq: legacy v1 ciphertext from core/crypto.py still decrypts") {
  if (!CryptoEngine::available()) return;
  Json f = fixture("legacy_v1_blob.json");
  auto pt = open_sealed(f.at("blob_json").get<std::string>(), f.at("password").get<std::string>());
  REQUIRE(pt);
  CHECK(*pt == f.at("plaintext").get<std::string>());
  CHECK_FALSE(open_sealed(f.at("blob_json").get<std::string>(), "wrong password"));
}

TEST_CASE("crypto_pq: sealed v2 round trip, context binding, wrong password, tamper") {
  if (!backend().available(Prim::Aes256Gcm)) return;
  Json c = test_crypto_section();
  auto pq = suite_config_from_json(c, "pq_hybrid");
  auto legacy = suite_config_from_json(c, "legacy");
  REQUIRE(pq);
  REQUIRE(legacy);
  SealContext ctx{"conversations", "c_123", 3};
  auto blob = seal("secret ąę text", "pw-1", *pq, ctx);
  REQUIRE(blob);
  Json j = Json::parse(*blob);
  CHECK(j.at("v") == 2);
  CHECK(j.at("suite") == "pq_hybrid");
  CHECK(j.at("kdf").at("alg") == "argon2id");
  auto pt = open_sealed(*blob, "pw-1", ctx);
  REQUIRE(pt);
  CHECK(*pt == "secret ąę text");
  CHECK_FALSE(open_sealed(*blob, "pw-2", ctx));
  CHECK_FALSE(open_sealed(*blob, "pw-1", SealContext{"conversations", "c_124", 3}));
  CHECK_FALSE(open_sealed(*blob, "pw-1", SealContext{"conversations", "c_123", 4}));
  CHECK_FALSE(open_sealed(*blob, "pw-1", SealContext{}));
  Json t = j;
  t["suite"] = "legacy";
  CHECK_FALSE(open_sealed(t.dump(), "pw-1", ctx));  // suite id is in the AAD
  t = j;
  std::string ct = t["ct"].get<std::string>();
  ct[4] = ct[4] == 'A' ? 'B' : 'A';
  t["ct"] = ct;
  CHECK_FALSE(open_sealed(t.dump(), "pw-1", ctx));
  // two seals of the same data differ (random salt and nonce)
  auto blob2 = seal("secret ąę text", "pw-1", *pq, ctx);
  REQUIRE(blob2);
  CHECK(*blob2 != *blob);
  // Changing the selected preset affects only NEW writes; the old blob keeps its own parameters.
  auto other = suite_config_from_json(c, "pq_other");
  REQUIRE(other);
  auto blob3 = seal("x", "pw-1", *other, ctx);
  REQUIRE(blob3);
  CHECK(Json::parse(*blob3).at("kdf").at("m") == 128);
  CHECK(Json::parse(*blob).at("kdf").at("m") == 64);
  CHECK(open_sealed(*blob, "pw-1", ctx));
  // Legacy preset writes the unchanged v:1 format and it is readable through the same entry point.
  if (CryptoEngine::available()) {
    auto v1 = seal("old", "pw", *legacy);
    REQUIRE(v1);
    Json j1 = Json::parse(*v1);
    CHECK(j1.at("v") == 1);
    CHECK(j1.contains("i"));
    auto back = open_sealed(*v1, "pw");
    REQUIRE(back);
    CHECK(*back == "old");
  }
  CHECK_FALSE(open_sealed("{\"v\":9}", "pw"));
  CHECK_FALSE(open_sealed("not json", "pw"));
}

TEST_CASE("crypto_pq: crypto.status lists primitives, backends and the active suite") {
  auto c = test_crypto_section();
  auto pq = suite_config_from_json(c, "pq_hybrid");
  REQUIRE(pq);
  Json st = crypto_status(&*pq);
  CHECK(st.at("known") == true);
  for (const char* id : {"aes-256-gcm", "sha-256", "hkdf-sha256", "sha3-256", "shake256", "argon2id", "x25519",
                         "ed25519", "ml-kem-768", "ml-dsa-65"})
    CHECK(st.at("primitives").contains(id));
  for (const char* id : {"argon2id", "ml-kem-768", "ml-dsa-65", "x25519", "ed25519"})
    CHECK(st.at("primitives").at(id).at("available") == true);
  CHECK(st.at("primitives").at("ml-kem-768").at("backend") == "mlkem-native");
  CHECK(st.at("active").at("preset") == "pq_hybrid");
  Json unk = crypto_status(nullptr);
  CHECK(unk.at("active").at("known") == false);
}

TEST_CASE("crypto_pq: injected backend replaces AES-GCM (platform injection)") {
  struct Fake : CryptoBackend {
    const CryptoBackend& base = portable_backend();
    bool available(Prim p) const override { return p == Prim::Aes256Gcm ? true : base.available(p); }
    std::string backend_name(Prim p) const override { return p == Prim::Aes256Gcm ? "test-injected" : base.backend_name(p); }
    Bytes random(std::size_t n) const override { return base.random(n); }
    Result<Bytes> aes256gcm_seal(std::string_view, std::string_view, std::string_view, std::string_view pt) const override { return Bytes(pt) + Bytes(16, 't'); }
    Result<Bytes> aes256gcm_open(std::string_view, std::string_view, std::string_view, std::string_view ct) const override { return Bytes(ct.substr(0, ct.size() - 16)); }
    Result<Bytes> sha256(std::string_view d) const override { return base.sha256(d); }
    Result<Bytes> hkdf_sha256(std::string_view a, std::string_view b, std::string_view c, std::size_t n) const override { return base.hkdf_sha256(a, b, c, n); }
    Result<Bytes> argon2id(std::string_view a, std::string_view b, const Argon2Params& p, std::size_t n, std::string_view s, std::string_view d) const override { return base.argon2id(a, b, p, n, s, d); }
    Result<Bytes> x25519_public(std::string_view a) const override { return base.x25519_public(a); }
    Result<Bytes> x25519(std::string_view a, std::string_view b) const override { return base.x25519(a, b); }
    Result<Bytes> ed25519_public(std::string_view a) const override { return base.ed25519_public(a); }
    Result<Bytes> ed25519_sign(std::string_view a, std::string_view b) const override { return base.ed25519_sign(a, b); }
    bool ed25519_verify(std::string_view a, std::string_view b, std::string_view c) const override { return base.ed25519_verify(a, b, c); }
    Result<Keypair> mlkem768_keypair(std::string_view a) const override { return base.mlkem768_keypair(a); }
    Result<Encapsulated> mlkem768_encaps(std::string_view a, std::string_view b) const override { return base.mlkem768_encaps(a, b); }
    Result<Bytes> mlkem768_decaps(std::string_view a, std::string_view b) const override { return base.mlkem768_decaps(a, b); }
    Result<Keypair> mldsa65_keypair(std::string_view a) const override { return base.mldsa65_keypair(a); }
    Result<Bytes> mldsa65_sign(std::string_view a, std::string_view b, std::string_view c, std::string_view d) const override { return base.mldsa65_sign(a, b, c, d); }
    bool mldsa65_verify(std::string_view a, std::string_view b, std::string_view c, std::string_view d) const override { return base.mldsa65_verify(a, b, c, d); }
  };
  set_backend(std::make_shared<Fake>());
  CHECK(crypto_status()["primitives"]["aes-256-gcm"]["backend"] == "test-injected");
  auto pq = suite_config_from_json(test_crypto_section(), "pq_hybrid");
  REQUIRE(pq);
  auto blob = seal("hi", "pw", *pq, {});
  REQUIRE(blob);
  auto back = open_sealed(*blob, "pw", {});
  REQUIRE(back);
  CHECK(*back == "hi");
  set_backend(nullptr);
}

TEST_CASE("crypto_pq: envelope v3 round trip, binding, tamper") {
  if (!backend().available(Prim::Aes256Gcm)) return;
  auto kp = xwing_keygen();
  REQUIRE(kp);
  EnvelopeMeta m{"0123456789abcdef0123456789abcdef", "loom.test/3", "programme-ż", 4600};
  auto env = envelope_v3_seal(kp->pk, m, "fabricated-credential");
  REQUIRE(env);
  auto pt = envelope_v3_open(kp->sk, m, *env);
  REQUIRE(pt);
  CHECK(*pt == "fabricated-credential");
  // wrong session metadata, wrong key, tamper
  EnvelopeMeta other = m;
  other.programme_id = "x";
  CHECK_FALSE(envelope_v3_open(kp->sk, other, *env));
  other = m;
  other.expires_at.reset();
  CHECK_FALSE(envelope_v3_open(kp->sk, other, *env));
  auto kp2 = xwing_keygen();
  REQUIRE(kp2);
  CHECK_FALSE(envelope_v3_open(kp2->sk, m, *env));
  Json j = Json::parse(*env);
  CHECK(j.at("schema") == "loom.credential_envelope/3");
  CHECK(j.at("algorithm") == "XWING-X25519-MLKEM768+HKDF-SHA256+A256GCM");
  CHECK(base64::decode(j.at("kem_ct_b64").get<std::string>(), true)->size() == kXwingCt);
  for (const char* f : {"kem_ct_b64", "iv_b64", "ciphertext_b64"}) {
    Json t = j;
    std::string v = t[f].get<std::string>();
    v[6] = v[6] == 'A' ? 'B' : 'A';
    t[f] = v;
    CHECK_FALSE(envelope_v3_open(kp->sk, m, t.dump()));
  }
  // The Python-style binding: json.dumps([...], separators=(",", ":"), ensure_ascii=False)
  CHECK(envelope_binding(m) == "[\"loom.test/3\",\"0123456789abcdef0123456789abcdef\",\"programme-ż\",4600]");
  EnvelopeMeta none{"s", "p", "g", std::nullopt};
  CHECK(envelope_binding(none) == "[\"p\",\"s\",\"g\",null]");
}

TEST_CASE("crypto_pq: vault writes the selected suite to new data only and reads both") {
  if (!backend().available(Prim::Aes256Gcm)) return;
  fsutil::TempDir td;
  CryptoVault legacy_vault(td.path() / "crypto.json");
  // no provider: legacy, byte-identical v:1
  REQUIRE(legacy_vault.setup("pw"));
  Json st = legacy_vault.status();
  CHECK(st.at("kdf") == "pbkdf2-sha256");
  CHECK(st.at("iterations") == 600000);
  CHECK(st.at("crypto").at("known") == true);
  auto old_blob = legacy_vault.encrypt_text("old data");
  REQUIRE(old_blob);
  CHECK(Json::parse(*old_blob).at("v") == 1);

  // same vault, setting flips to pq: new writes are v:2, old blob still opens
  Json section = test_crypto_section();
  std::string sel = "legacy";
  legacy_vault.set_suite_provider([&]() { return suite_config_from_json(section, sel); });
  sel = "pq_hybrid";
  auto new_blob = legacy_vault.encrypt_text("new data");
  REQUIRE(new_blob);
  CHECK(Json::parse(*new_blob).at("v") == 2);
  auto a = legacy_vault.decrypt_text(*old_blob);
  auto b = legacy_vault.decrypt_text(*new_blob);
  REQUIRE(a);
  REQUIRE(b);
  CHECK(*a == "old data");
  CHECK(*b == "new data");
  CHECK(legacy_vault.status().at("kdf") == "argon2id");

  // fresh vault configured under pq_hybrid has a v:2 verifier; wrong password rejected, right one unlocks
  fsutil::TempDir td2;
  CryptoVault pq_vault(td2.path() / "crypto.json");
  pq_vault.set_suite_provider([&]() { return suite_config_from_json(section, "pq_hybrid"); });
  REQUIRE(pq_vault.setup("pw2"));
  auto verifier = fsutil::read_file(td2.path() / "crypto.json");
  REQUIRE(verifier);
  CHECK(Json::parse(*verifier).at("v") == 2);
  pq_vault.lock();
  CHECK_FALSE(pq_vault.unlock("nope"));
  CHECK(pq_vault.unlock("pw2"));
  // an unresolvable preset fails the write instead of silently downgrading
  pq_vault.set_suite_provider([&]() { return suite_config_from_json(section, "missing"); });
  CHECK_FALSE(pq_vault.encrypt_text("x"));
}

TEST_CASE("crypto_pq: graph entry reports crypto primitives and the active suite") {
  fsutil::TempDir td;
  graph_entry::Env env;
  env.data_dir = td.path();
  auto view = graph_entry::compute(env);
  REQUIRE(view);
  const Json* area = nullptr;
  for (const auto& a : (*view)["areas"])
    if (a.value("id", "") == "storage_security") area = &a;
  REQUIRE(area);
  const Json& counts = (*area)["counts"];
  REQUIRE(counts.contains("crypto_primitives"));
  CHECK(counts["crypto_primitives"]["known"] == true);
  CHECK(counts["crypto_primitives"]["by"].contains("ml-kem-768@mlkem-native"));
  CHECK(counts["crypto_active_suite"]["known"] == true);
  CHECK(counts["crypto_active_suite"]["by"].contains("legacy"));
}
