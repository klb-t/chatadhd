// loom.credential_envelope/3: X-Wing (X25519 + ML-KEM-768) key transport + AES-256-GCM.
#include "loom/crypto_suite.h"
#include "loom/util/base64.h"

namespace loom::cs {

std::string envelope_binding(const EnvelopeMeta& m) {
  Json a = Json::array({m.purpose, m.session_id, m.programme_id,
                        m.expires_at ? Json(*m.expires_at) : Json(nullptr)});
  return json::dump(a);
}

namespace {
Result<Bytes> derive_key(const Bytes& ss, const std::string& binding) {
  std::string info(kEnvelopeV3Schema);
  info += binding;
  return backend().hkdf_sha256(ss, {}, info, 32);
}
}  // namespace

Result<std::string> envelope_v3_seal(std::string_view pk, const EnvelopeMeta& meta, std::string_view plaintext) {
  const CryptoBackend& be = backend();
  if (!be.available(Prim::Aes256Gcm)) return Error(Errc::Unavailable, "AES-256-GCM is not available in this build/backend");
  if (plaintext.empty()) return Error(Errc::InvalidArgument, "empty credential");
  auto enc = xwing_encaps(pk);
  if (!enc) return enc.error();
  std::string binding = envelope_binding(meta);
  auto key = derive_key(enc->ss, binding);
  if (!key) return key.error();
  Bytes iv = be.random(12);
  auto ct = be.aes256gcm_seal(*key, iv, binding, plaintext);
  if (!ct) return ct.error();
  Json j = Json{{"schema", std::string(kEnvelopeV3Schema)},
                {"algorithm", std::string(kEnvelopeV3Algorithm)},
                {"session_id", meta.session_id},
                {"purpose", meta.purpose},
                {"programme_id", meta.programme_id},
                {"expires_at", meta.expires_at ? Json(*meta.expires_at) : Json(nullptr)},
                {"kem_ct_b64", base64::encode(enc->ct)},
                {"iv_b64", base64::encode(iv)},
                {"ciphertext_b64", base64::encode(*ct)}};
  return j.dump();
}

Result<std::string> envelope_v3_open(std::string_view sk, const EnvelopeMeta& expected, std::string_view env_json) {
  Json j = Json::parse(env_json, nullptr, false);
  if (!j.is_object()) return Error(Errc::Parse, "envelope is not a JSON object");
  if (json::get_string(j, "schema") != kEnvelopeV3Schema || json::get_string(j, "algorithm") != kEnvelopeV3Algorithm)
    return Error(Errc::Unsupported, "not a loom.credential_envelope/3 envelope");
  const Json* ex = json::find(j, "expires_at");
  bool exp_ok = ex && (expected.expires_at ? (ex->is_number_integer() && ex->get<std::int64_t>() == *expected.expires_at)
                                           : ex->is_null());
  if (json::get_string(j, "session_id") != expected.session_id || json::get_string(j, "purpose") != expected.purpose ||
      json::get_string(j, "programme_id") != expected.programme_id || !exp_ok)
    return Error(Errc::Conflict, "envelope does not match the session");
  auto kem = base64::decode(json::get_string(j, "kem_ct_b64"), true);
  auto iv = base64::decode(json::get_string(j, "iv_b64"), true);
  auto ct = base64::decode(json::get_string(j, "ciphertext_b64"), true);
  if (!kem || !iv || !ct || kem->size() != kXwingCt || iv->size() != 12 || ct->size() <= 16)
    return Error(Errc::Parse, "malformed envelope encoding");
  auto ss = xwing_decaps(sk, *kem);
  if (!ss) return ss.error();
  std::string binding = envelope_binding(expected);
  auto key = derive_key(*ss, binding);
  if (!key) return key.error();
  auto pt = backend().aes256gcm_open(*key, *iv, binding, *ct);
  if (!pt) return Error(Errc::Crypto, "envelope decryption failed");
  return pt;
}

}  // namespace loom::cs
