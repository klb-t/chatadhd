// Compat commands for core.crypto <-> loom::CryptoEngine interop.
#include "compat_registry.h"
#include "loom/crypto.h"
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
