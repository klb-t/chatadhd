// Compat commands for core.crypto <-> loom::CryptoEngine interop.
#include "compat_registry.h"
#include "loom/crypto.h"
#include "loom/crypto_suite.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"

using namespace loom;
using namespace loom::compat;

namespace {
// Args are base64 text, either inline or (for large payloads, to stay under
// the OS argv size limit) in a file referenced as "@path" (see arg_text()).
std::string b64_arg(const std::string& s) {
  auto text = arg_text(s);
  if (!text) return std::string();
  return base64::decode(*text, true).value_or(std::string());
}
}  // namespace

LOOM_COMPAT_COMMAND(cmd_crypto_available, "crypto-available", ": CryptoEngine::available()") {
  print_json(Json{{"available", CryptoEngine::available()}});
  return 0;
}

// crypto-encrypt <password_b64> <plaintext_b64> : encrypt, print EncryptedBlob JSON
LOOM_COMPAT_COMMAND(cmd_crypto_encrypt, "crypto-encrypt", "<password_b64> <plaintext_b64> : EncryptedBlob.to_json()") {
  if (args.size() < 2) return fail("usage: crypto-encrypt <password_b64> <plaintext_b64>");
  CryptoEngine eng;
  auto r = eng.encrypt(b64_arg(args[1]), b64_arg(args[0]));
  if (!r) {
    print_json(Json{{"error", true}, {"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}});
    return 0;
  }
  print_json(Json{{"blob", r->to_json()}});
  return 0;
}

// crypto-decrypt <password_b64> <blob_json> : decrypt an EncryptedBlob JSON string
LOOM_COMPAT_COMMAND(cmd_crypto_decrypt, "crypto-decrypt", "<password_b64> @blob.json : decrypted plaintext (base64) or error") {
  if (args.size() < 2) return fail("usage: crypto-decrypt <password_b64> @blob.json");
  auto blob_text = arg_text(args[1]);
  if (!blob_text) return fail(blob_text.error().to_string());
  auto blob = EncryptedBlob::from_json(*blob_text);
  if (!blob) {
    print_json(Json{{"error", true}, {"code", std::string(errc_name(blob.error().code))}, {"message", blob.error().message}});
    return 0;
  }
  CryptoEngine eng;
  auto r = eng.decrypt(*blob, b64_arg(args[0]));
  if (!r) {
    print_json(Json{{"error", true}, {"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}});
    return 0;
  }
  print_json(Json{{"plaintext_b64", base64::encode(*r)}});
  return 0;
}

// crypto-derive <password_b64> <salt_b64> <iterations> : PBKDF2 key as hex
LOOM_COMPAT_COMMAND(cmd_crypto_derive, "crypto-derive", "<password_b64> <salt_b64> <iterations> : derived key hex") {
  if (args.size() < 3) return fail("usage: crypto-derive <password_b64> <salt_b64> <iterations>");
  CryptoEngine eng;
  auto r = eng.derive_key(b64_arg(args[0]), b64_arg(args[1]), static_cast<std::uint32_t>(std::stoul(args[2])));
  if (!r) {
    print_json(Json{{"error", true}, {"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}});
    return 0;
  }
  print_json(Json{{"key_hex", to_hex(r->data(), r->size())}});
  return 0;
}

// ── T5b: sealed/2 and credential envelope v3 interop ───────────────────
namespace {
int print_err(const Error& e) {
  print_json(Json{{"error", true}, {"code", std::string(errc_name(e.code))}, {"message", e.message}});
  return 0;
}
cs::EnvelopeMeta meta_from(const Json& j) {
  cs::EnvelopeMeta m;
  m.session_id = json::get_string(j, "session_id");
  m.purpose = json::get_string(j, "purpose");
  m.programme_id = json::get_string(j, "programme_id");
  const Json* e = json::find(j, "expires_at");
  if (e && e->is_number_integer()) m.expires_at = e->get<std::int64_t>();
  return m;
}
}  // namespace

// sealed2-seal <password_b64> <plaintext_b64> <t> <m_kib> <p> <category> <id> : loom.sealed/2 blob
LOOM_COMPAT_COMMAND(cmd_sealed2_seal, "sealed2-seal", "<pw_b64> <pt_b64> <t> <m_kib> <p> <category> <id> : sealed/2 JSON") {
  if (args.size() < 7) return fail("usage: sealed2-seal <pw_b64> <pt_b64> <t> <m_kib> <p> <category> <id>");
  cs::SuiteConfig cfg;
  cfg.preset_id = "pq_hybrid";
  cfg.password_kdf = "argon2id";
  cfg.argon2 = {static_cast<std::uint32_t>(std::stoul(args[2])), static_cast<std::uint32_t>(std::stoul(args[3])),
                static_cast<std::uint32_t>(std::stoul(args[4]))};
  auto r = cs::seal(b64_arg(args[1]), b64_arg(args[0]), cfg, cs::SealContext{args[5], args[6], 1});
  if (!r) return print_err(r.error());
  print_json(Json{{"blob", *r}});
  return 0;
}

// sealed-open <password_b64> @blob.json <category> <id> : v1 or v2 plaintext (base64)
LOOM_COMPAT_COMMAND(cmd_sealed_open, "sealed-open", "<pw_b64> @blob.json <category> <id> : plaintext_b64") {
  if (args.size() < 4) return fail("usage: sealed-open <pw_b64> @blob.json <category> <id>");
  auto blob = arg_text(args[1]);
  if (!blob) return fail(blob.error().to_string());
  auto r = cs::open_sealed(*blob, b64_arg(args[0]), cs::SealContext{args[2], args[3], 1});
  if (!r) return print_err(r.error());
  print_json(Json{{"plaintext_b64", base64::encode(*r)}});
  return 0;
}

// envelope3-seal <xwing_pk_b64> @meta.json <plaintext_b64> : loom.credential_envelope/3
LOOM_COMPAT_COMMAND(cmd_envelope3_seal, "envelope3-seal", "<xwing_pk_b64> @meta.json <pt_b64> : envelope JSON") {
  if (args.size() < 3) return fail("usage: envelope3-seal <xwing_pk_b64> @meta.json <pt_b64>");
  auto meta = arg_text(args[1]);
  if (!meta) return fail(meta.error().to_string());
  auto r = cs::envelope_v3_seal(b64_arg(args[0]), meta_from(Json::parse(*meta, nullptr, false)), b64_arg(args[2]));
  if (!r) return print_err(r.error());
  print_json(Json{{"envelope", *r}});
  return 0;
}

// envelope3-open <xwing_sk_b64> @meta.json @envelope.json : plaintext (base64)
LOOM_COMPAT_COMMAND(cmd_envelope3_open, "envelope3-open", "<xwing_sk_b64> @meta.json @envelope.json : plaintext_b64") {
  if (args.size() < 3) return fail("usage: envelope3-open <xwing_sk_b64> @meta.json @envelope.json");
  auto meta = arg_text(args[1]);
  auto env = arg_text(args[2]);
  if (!meta || !env) return fail("cannot read arguments");
  auto r = cs::envelope_v3_open(b64_arg(args[0]), meta_from(Json::parse(*meta, nullptr, false)), *env);
  if (!r) return print_err(r.error());
  print_json(Json{{"plaintext_b64", base64::encode(*r)}});
  return 0;
}

// xwing-keygen <seed_b64> : public key (base64) for a 32-byte secret seed
LOOM_COMPAT_COMMAND(cmd_xwing_pub, "xwing-public", "<seed_b64> : X-Wing public key (base64)") {
  if (args.empty()) return fail("usage: xwing-public <seed_b64>");
  auto r = cs::xwing_public_from_sk(b64_arg(args[0]));
  if (!r) return print_err(r.error());
  print_json(Json{{"pk_b64", base64::encode(*r)}});
  return 0;
}
