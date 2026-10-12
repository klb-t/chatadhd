// loom.sealed/1 + /2 dispatch, suite configuration from the storage_security pack, crypto.status.
#include <cstring>

#include "loom/crypto.h"
#include "loom/crypto_suite.h"
#include "loom/runtime_profile.h"
#include "loom/util/base64.h"

namespace loom::cs {
namespace {
constexpr std::string_view kSealed2 = "loom.sealed/2";
constexpr std::size_t kSaltBytes = 32, kNonceBytes = 12, kKeyBytes = 32;

std::string aad_for(std::string_view suite, const SealContext& c) {
  return std::string(kSealed2) + "|" + std::string(suite) + "|" + c.category + "|" + c.object_id + "|" +
         std::to_string(c.version);
}
std::string info_for(const SealContext& c) {
  return std::string(kSealed2) + "|key|" + c.category + "|" + c.object_id;
}

bool pos_uint(const Json& j, std::uint32_t& out) {
  if (!j.is_number_integer()) return false;
  auto v = j.get<std::int64_t>();
  if (v <= 0 || v > 0xffffffffLL) return false;
  out = static_cast<std::uint32_t>(v);
  return true;
}
Error unavailable_aead() {
  return Error(Errc::Unavailable, "AES-256-GCM is not available in this build/backend (inject one with set_backend)");
}
void wipe_s(std::string& s) {
  volatile char* p = s.data();
  for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
}
}  // namespace

Result<SuiteConfig> suite_config_from_json(const Json& crypto, std::string_view preset_id) {
  if (!crypto.is_object()) return Error(Errc::InvalidArgument, "crypto section missing");
  std::string id(preset_id);
  if (id.empty()) id = json::get_string(crypto, "selected_preset");
  const Json* presets = json::find(crypto, "suite_presets");
  const Json* p = presets && presets->is_object() ? json::find(*presets, id) : nullptr;
  if (id.empty() || !p || !p->is_object()) return Error(Errc::NotFound, "unknown crypto suite preset: " + id);
  SuiteConfig c;
  c.preset_id = id;
  c.password_kdf = json::get_string(*p, "password_kdf");
  c.kem = json::get_string(*p, "kem");
  c.signature = json::get_string(*p, "signature");
  c.handoff_envelope = json::get_string(*p, "handoff_envelope");
  if (c.password_kdf == "argon2id") {
    c.argon2_preset = json::get_string(*p, "argon2id_preset");
    const Json* ap = json::find(crypto, "params");
    ap = ap ? json::find(*ap, "argon2id_presets") : nullptr;
    ap = ap ? json::find(*ap, c.argon2_preset) : nullptr;
    if (!ap || !ap->is_object())
      return Error(Errc::NotFound, "argon2id preset not found: " + c.argon2_preset);
    if (!pos_uint(ap->value("t", Json()), c.argon2.t) || !pos_uint(ap->value("m_kib", Json()), c.argon2.m_kib) ||
        !pos_uint(ap->value("p", Json()), c.argon2.p))
      return Error(Errc::InvalidArgument,
                   "argon2id preset '" + c.argon2_preset + "' has no concrete t/m_kib/p (calibrate it first)");
  } else if (c.password_kdf != "pbkdf2-hmac-sha256") {
    return Error(Errc::Unsupported, "unsupported password_kdf: " + c.password_kdf);
  }
  return c;
}

Result<SuiteConfig> suite_config_from_profile(const std::filesystem::path& data_dir, std::string_view preset_id) {
  auto prof = RuntimeProfile::load("storage_security", data_dir);
  if (!prof) return prof.error();
  const Json* c = json::find(prof->values(), "crypto");
  if (!c) return Error(Errc::NotFound, "storage_security profile has no crypto section");
  return suite_config_from_json(*c, preset_id);
}

Result<std::string> seal(std::string_view plaintext, std::string_view password, const SuiteConfig& cfg,
                         const SealContext& ctx) {
  if (!cfg.pq()) {
    CryptoEngine e;
    auto b = e.encrypt(plaintext, password);
    if (!b) return b.error();
    return b->to_json();
  }
  const CryptoBackend& be = backend();
  if (!be.available(Prim::Aes256Gcm)) return unavailable_aead();
  Bytes salt = be.random(kSaltBytes), nonce = be.random(kNonceBytes);
  auto dek = be.argon2id(password, salt, cfg.argon2, kKeyBytes);
  if (!dek) return dek.error();
  auto okey = be.hkdf_sha256(*dek, {}, info_for(ctx), kKeyBytes);
  wipe_s(*dek);
  if (!okey) return okey.error();
  auto ct = be.aes256gcm_seal(*okey, nonce, aad_for(cfg.preset_id, ctx), plaintext);
  wipe_s(*okey);
  if (!ct) return ct.error();
  Json j = Json{{"v", 2},
                {"fmt", std::string(kSealed2)},
                {"suite", cfg.preset_id},
                {"kdf", Json{{"alg", "argon2id"}, {"t", cfg.argon2.t}, {"m", cfg.argon2.m_kib}, {"p", cfg.argon2.p}}},
                {"s", base64::encode(salt)},
                {"n", base64::encode(nonce)},
                {"ct", base64::encode(*ct)}};
  return j.dump();
}

Result<std::string> open_sealed(std::string_view blob_json, std::string_view password, const SealContext& ctx) {
  Json j = Json::parse(blob_json, nullptr, false);
  if (!j.is_object()) return Error(Errc::Parse, "sealed blob is not a JSON object");
  std::int64_t v = j.contains("v") && j["v"].is_number_integer() ? j["v"].get<std::int64_t>() : 1;
  if (v == 1) {
    auto blob = EncryptedBlob::from_dict(j);
    if (!blob) return blob.error();
    CryptoEngine e;
    return e.decrypt(*blob, password);
  }
  if (v != 2) return Error(Errc::Unsupported, "unknown sealed format version " + std::to_string(v));
  const Json* kdf = json::find(j, "kdf");
  if (!kdf || json::get_string(*kdf, "alg") != "argon2id") return Error(Errc::Unsupported, "unsupported kdf in sealed/2");
  Argon2Params prm;
  if (!pos_uint(kdf->value("t", Json()), prm.t) || !pos_uint(kdf->value("m", Json()), prm.m_kib) ||
      !pos_uint(kdf->value("p", Json()), prm.p))
    return Error(Errc::Parse, "bad argon2id parameters in sealed/2");
  auto salt = base64::decode(json::get_string(j, "s"), true);
  auto nonce = base64::decode(json::get_string(j, "n"), true);
  auto ct = base64::decode(json::get_string(j, "ct"), true);
  if (!salt || !nonce || !ct || nonce->size() != kNonceBytes || ct->size() < 16 || salt->size() < 8)
    return Error(Errc::Parse, "malformed sealed/2 blob");
  const CryptoBackend& be = backend();
  if (!be.available(Prim::Aes256Gcm)) return unavailable_aead();
  auto dek = be.argon2id(password, *salt, prm, kKeyBytes);
  if (!dek) return dek.error();
  auto okey = be.hkdf_sha256(*dek, {}, info_for(ctx), kKeyBytes);
  wipe_s(*dek);
  if (!okey) return okey.error();
  auto pt = be.aes256gcm_open(*okey, *nonce, aad_for(json::get_string(j, "suite"), ctx), *ct);
  wipe_s(*okey);
  if (!pt) return Error(Errc::Crypto, "decryption failed (wrong password, wrong context or tampered data)");
  return pt;
}

Json crypto_status(const SuiteConfig* active) {
  const CryptoBackend& be = backend();
  Json prims = Json::object();
  for (Prim p : all_prims())
    prims[std::string(prim_id(p))] = Json{{"available", be.available(p)}, {"backend", be.backend_name(p)}};
  Json out = Json{{"schema", "loom.crypto_status/1"}, {"known", true}, {"primitives", std::move(prims)}};
  if (active) {
    out["active"] = Json{{"known", true},
                         {"preset", active->preset_id},
                         {"password_kdf", active->password_kdf},
                         {"argon2id_preset", active->argon2_preset},
                         {"kem", active->kem},
                         {"signature", active->signature},
                         {"handoff_envelope", active->handoff_envelope}};
  } else {
    out["active"] = Json{{"known", false}, {"reason", "suite_not_resolved"}};
  }
  return out;
}

}  // namespace loom::cs
