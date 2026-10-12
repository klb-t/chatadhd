// loom/crypto_suite.h — pluggable crypto primitives and the "crypto suite" built on them (R51, T5b).
//
// Layers:
//   CryptoBackend  one virtual interface for every primitive. The default backend uses OpenSSL
//                  (AES-256-GCM, SHA-256/HKDF, X25519, Ed25519) when LOOM_HAVE_OPENSSL, and vendored
//                  code otherwise: Argon2id (phc-winner-argon2), X25519/Ed25519 (Monocypher),
//                  ML-KEM-768 (mlkem-native), ML-DSA-65 (mldsa-native), SHA-256/SHA3/SHAKE (portable C++).
//                  A platform (Android) may inject its own backend, e.g. AES-GCM from javax.crypto.
//   Hybrid KEM     X-Wing style X25519 + ML-KEM-768 (draft-connolly-cfrg-xwing-kem), 1216-byte public
//                  key, 32-byte secret key (seed), 1120-byte ciphertext, 32-byte shared secret.
//   Hybrid sig     Ed25519 + ML-DSA-65, container loom.sig_hybrid/1; BOTH halves must verify.
//   Sealed         loom.sealed/1 = today's {v:1,...} PBKDF2 blob (byte-for-byte unchanged);
//                  loom.sealed/2 = {v:2,...} Argon2id + HKDF object key + AES-256-GCM with AAD.
//                  open_sealed() dispatches on "v". Which one seal() writes is a SETTING:
//                  SuiteConfig is read from the storage_security pack (crypto.selected_preset,
//                  crypto.suite_presets, crypto.params.argon2id_presets); there are no parameter
//                  constants in code (only the fixed wire sizes of the standards).
//
// All byte strings are raw bytes in std::string. Nothing here logs secrets.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::cs {

using Bytes = std::string;

enum class Prim { Aes256Gcm, Sha256, HkdfSha256, Sha3_256, Shake256, Argon2id, X25519, Ed25519, MlKem768, MlDsa65 };
std::string_view prim_id(Prim p) noexcept;  // ids match crypto.algorithms in the storage_security pack
const std::vector<Prim>& all_prims() noexcept;

struct Argon2Params {
  std::uint32_t t = 0;      // passes
  std::uint32_t m_kib = 0;  // memory, KiB
  std::uint32_t p = 0;      // lanes
};

struct Keypair {
  Bytes pk;
  Bytes sk;
};
struct Encapsulated {
  Bytes ct;
  Bytes ss;
};

// Fixed sizes of the standards (not tunables).
inline constexpr std::size_t kX25519Bytes = 32;
inline constexpr std::size_t kEd25519PkBytes = 32, kEd25519SigBytes = 64;
inline constexpr std::size_t kMlKem768Pk = 1184, kMlKem768Sk = 2400, kMlKem768Ct = 1088, kMlKem768Ss = 32;
inline constexpr std::size_t kMlDsa65Pk = 1952, kMlDsa65Sk = 4032, kMlDsa65Sig = 3309;
inline constexpr std::size_t kXwingPk = 1216, kXwingSk = 32, kXwingCt = 1120, kXwingSs = 32;
inline constexpr std::size_t kHybridSigPk = 32 + 1952, kHybridSigSk = 64, kHybridSigBytes = 64 + 3309;

class CryptoBackend {
 public:
  virtual ~CryptoBackend() = default;
  virtual bool available(Prim p) const = 0;
  virtual std::string backend_name(Prim p) const = 0;  // "openssl" | "monocypher" | "argon2-ref" | "mlkem-native" | ... | "none"

  virtual Bytes random(std::size_t n) const = 0;
  // AES-256-GCM: returns ciphertext || 16-byte tag. Errc::Crypto on authentication failure.
  virtual Result<Bytes> aes256gcm_seal(std::string_view key, std::string_view nonce12, std::string_view aad,
                                       std::string_view plaintext) const = 0;
  virtual Result<Bytes> aes256gcm_open(std::string_view key, std::string_view nonce12, std::string_view aad,
                                       std::string_view ct_and_tag) const = 0;
  virtual Result<Bytes> sha256(std::string_view data) const = 0;
  virtual Result<Bytes> hkdf_sha256(std::string_view ikm, std::string_view salt, std::string_view info,
                                    std::size_t out_len) const = 0;
  // RFC 9106 Argon2id (version 0x13). `secret` and `ad` are the optional RFC inputs.
  virtual Result<Bytes> argon2id(std::string_view password, std::string_view salt, const Argon2Params& prm,
                                 std::size_t out_len, std::string_view secret = {},
                                 std::string_view ad = {}) const = 0;
  virtual Result<Bytes> x25519_public(std::string_view sk32) const = 0;
  virtual Result<Bytes> x25519(std::string_view sk32, std::string_view pk32) const = 0;  // rejects all-zero output
  virtual Result<Bytes> ed25519_public(std::string_view seed32) const = 0;
  virtual Result<Bytes> ed25519_sign(std::string_view seed32, std::string_view msg) const = 0;
  virtual bool ed25519_verify(std::string_view pk32, std::string_view msg, std::string_view sig64) const = 0;
  virtual Result<Keypair> mlkem768_keypair(std::string_view d_z64) const = 0;  // FIPS 203 d || z
  virtual Result<Encapsulated> mlkem768_encaps(std::string_view pk, std::string_view m32) const = 0;
  virtual Result<Bytes> mlkem768_decaps(std::string_view sk, std::string_view ct) const = 0;
  virtual Result<Keypair> mldsa65_keypair(std::string_view seed32) const = 0;
  // Pure ML-DSA.Sign with context; empty `rnd32` = deterministic variant, else hedged with those 32 bytes.
  virtual Result<Bytes> mldsa65_sign(std::string_view sk, std::string_view msg, std::string_view ctx,
                                     std::string_view rnd32) const = 0;
  virtual bool mldsa65_verify(std::string_view pk, std::string_view msg, std::string_view ctx,
                              std::string_view sig) const = 0;
};

// The active backend (never null). Default: OpenSSL where present, vendored otherwise.
const CryptoBackend& backend();
// Replace the active backend (platform injection, tests); nullptr restores the default.
void set_backend(std::shared_ptr<const CryptoBackend> b);
// Vendored/portable-only backend (no OpenSSL, no AES-GCM): used by tests to cross-check backends and
// by platforms that wrap it.
const CryptoBackend& portable_backend();

// SHA-3 family (portable, FIPS 202).
Bytes sha3_256(std::string_view data);
Bytes shake256(std::string_view data, std::size_t out_len);

// ── Hybrid KEM (X-Wing) ──────────────────────────────────────────────
Result<Keypair> xwing_keygen(std::string_view seed32 = {});  // sk = 32-byte seed; empty seed = random
Result<Bytes> xwing_public_from_sk(std::string_view sk32);
Result<Encapsulated> xwing_encaps(std::string_view pk1216, std::string_view eseed64 = {});
Result<Bytes> xwing_decaps(std::string_view sk32, std::string_view ct1120);

// ── Hybrid signature (Ed25519 + ML-DSA-65), container loom.sig_hybrid/1 ──
Result<Keypair> hybrid_sig_keygen(std::string_view seed64 = {});  // sk = ed25519 seed || ml-dsa seed
Result<Bytes> hybrid_sign(std::string_view sk64, std::string_view msg, std::string_view ctx = {});
Status hybrid_verify(std::string_view pk1984, std::string_view msg, std::string_view ctx, std::string_view sig3373);
// {"schema":"loom.sig_hybrid/1","alg":"ed25519+ml-dsa-65","ed25519":b64,"ml_dsa_65":b64}
Result<std::string> hybrid_sign_json(std::string_view sk64, std::string_view msg, std::string_view ctx = {});
Status hybrid_verify_json(std::string_view pk1984, std::string_view msg, std::string_view ctx, std::string_view sig_json);

// ── Suite configuration (data-driven) ────────────────────────────────
struct SuiteConfig {
  std::string preset_id = "legacy";
  std::string password_kdf = "pbkdf2-hmac-sha256";  // or "argon2id"
  std::string argon2_preset;                        // id inside crypto.params.argon2id_presets
  Argon2Params argon2;
  std::string kem;        // informational ids from the pack
  std::string signature;
  std::string handoff_envelope;
  std::vector<std::string> requires_prims;  // primitive ids the preset needs (pack: "requires")
  std::string requested_preset;   // what the setting asked for (== preset_id unless fell back)
  std::string fallback_reason;    // non-empty: effective suite is not the requested one, and why
  bool pq() const { return password_kdf == "argon2id"; }
};
// When preset_id is empty the SELECTED preset is used and, if the active backend lacks a primitive the preset
// `requires`, the effective suite becomes `legacy` with fallback_reason set (visible in crypto.status; never an
// error, existing ciphertexts stay readable). An explicit preset_id is never substituted.
// `crypto` is the "crypto" object of the storage_security pack. Empty preset_id = crypto.selected_preset.
// An unknown preset, or a preset whose Argon2id preset is missing/zero, is an error (no silent defaults).
Result<SuiteConfig> suite_config_from_json(const Json& crypto, std::string_view preset_id = {});
// Effective storage_security profile (data dir overlay included) -> SuiteConfig.
Result<SuiteConfig> suite_config_from_profile(const std::filesystem::path& data_dir, std::string_view preset_id = {});

// ── Sealed blobs: loom.sealed/1 (v:1) and loom.sealed/2 (v:2) ─────────
struct SealContext {
  std::string category;  // e.g. "conversations"; bound into the object key and the AAD
  std::string object_id;
  std::uint32_t version = 1;
};
// Writes v:1 (legacy, SealContext ignored: that format has no AAD) when cfg.password_kdf is PBKDF2,
// v:2 when it is argon2id.
Result<std::string> seal(std::string_view plaintext, std::string_view password, const SuiteConfig& cfg,
                         const SealContext& ctx = {});
// Reads either version; v:1 uses the existing PBKDF2 path unchanged. Wrong password / tamper / wrong
// context -> Errc::Crypto.
Result<std::string> open_sealed(std::string_view blob_json, std::string_view password, const SealContext& ctx = {});

// ── Credential envelope v3 (loom.credential_envelope/3) ───────────────
// Replaces RSA-OAEP v2 for credential handoff. KEM = X-Wing; key = HKDF-SHA256(ss, info = schema || binding);
// AEAD = AES-256-GCM with AAD = binding = json.dumps([purpose, session_id, programme_id, expires_at],
// separators=(",", ":"), ensure_ascii=False) (the same binding as v2).
struct EnvelopeMeta {
  std::string session_id;
  std::string purpose;
  std::string programme_id;
  std::optional<std::int64_t> expires_at;  // seconds since epoch; null = no expiry
};
inline constexpr std::string_view kEnvelopeV3Schema = "loom.credential_envelope/3";
inline constexpr std::string_view kEnvelopeV3Algorithm = "XWING-X25519-MLKEM768+HKDF-SHA256+A256GCM";
std::string envelope_binding(const EnvelopeMeta& m);
Result<std::string> envelope_v3_seal(std::string_view xwing_pk, const EnvelopeMeta& meta, std::string_view plaintext);
// `expected` must equal the envelope's own metadata (Errc::Conflict otherwise); wrong key/tamper -> Errc::Crypto.
Result<std::string> envelope_v3_open(std::string_view xwing_sk, const EnvelopeMeta& expected, std::string_view envelope_json);

// ── crypto.status ────────────────────────────────────────────────────
// {"schema":"loom.crypto_status/1","primitives":{id:{"available":bool,"backend":str}},
//  "active_preset":..., "known":true}. `active` empty -> {"known":false,"reason":...} for that field.
Json crypto_status(const SuiteConfig* active = nullptr);

// Hex helpers for tests/tools.
std::string to_hex(std::string_view raw);
std::optional<Bytes> from_hex(std::string_view hex);

}  // namespace loom::cs
