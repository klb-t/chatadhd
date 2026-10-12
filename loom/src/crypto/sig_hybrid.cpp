// Hybrid signature loom.sig_hybrid/1: Ed25519 + ML-DSA-65, both must verify.
//   sk (64)  = ed25519 seed (32) || ml-dsa-65 keygen seed (32)
//   pk (1984)= ed25519 pk (32)   || ml-dsa-65 pk (1952)
//   sig(3373)= ed25519 sig (64)  || ml-dsa-65 sig (3309)
//   Both halves sign M' = "loom.sig_hybrid/1" 0x00 be16(len ctx) ctx msg
//   (the ML-DSA half additionally uses the label as its FIPS 204 context string), so neither half can be
//   replayed as a standalone Ed25519 or ML-DSA signature over msg.
#include "loom/crypto_suite.h"
#include "loom/util/base64.h"

namespace loom::cs {
namespace {
constexpr std::string_view kLabel = "loom.sig_hybrid/1";

Bytes message_prime(std::string_view msg, std::string_view ctx) {
  Bytes m(kLabel);
  m.push_back('\0');
  m.push_back(static_cast<char>((ctx.size() >> 8) & 0xff));
  m.push_back(static_cast<char>(ctx.size() & 0xff));
  m.append(ctx);
  m.append(msg);
  return m;
}
}  // namespace

Result<Keypair> hybrid_sig_keygen(std::string_view seed64) {
  Bytes sk = seed64.empty() ? backend().random(kHybridSigSk) : Bytes(seed64);
  if (sk.size() != kHybridSigSk) return Error(Errc::InvalidArgument, "hybrid sig: seed must be 64 bytes");
  auto ed = backend().ed25519_public(std::string_view(sk).substr(0, 32));
  if (!ed) return ed.error();
  auto ml = backend().mldsa65_keypair(std::string_view(sk).substr(32, 32));
  if (!ml) return ml.error();
  return Keypair{*ed + ml->pk, std::move(sk)};
}

Result<Bytes> hybrid_sign(std::string_view sk, std::string_view msg, std::string_view ctx) {
  if (sk.size() != kHybridSigSk) return Error(Errc::InvalidArgument, "hybrid sig: secret key must be 64 bytes");
  if (ctx.size() > 255) return Error(Errc::InvalidArgument, "hybrid sig: context longer than 255 bytes");
  Bytes mp = message_prime(msg, ctx);
  auto es = backend().ed25519_sign(sk.substr(0, 32), mp);
  if (!es) return es.error();
  auto ml = backend().mldsa65_keypair(sk.substr(32, 32));
  if (!ml) return ml.error();
  auto ms = backend().mldsa65_sign(ml->sk, mp, kLabel, backend().random(32));
  if (!ms) return ms.error();
  return *es + *ms;
}

Status hybrid_verify(std::string_view pk, std::string_view msg, std::string_view ctx, std::string_view sig) {
  if (pk.size() != kHybridSigPk || sig.size() != kHybridSigBytes || ctx.size() > 255)
    return Error(Errc::Crypto, "hybrid signature: wrong length");
  Bytes mp = message_prime(msg, ctx);
  // Evaluate both halves unconditionally, then require both.
  bool ok_ed = backend().ed25519_verify(pk.substr(0, 32), mp, sig.substr(0, 64));
  bool ok_ml = backend().mldsa65_verify(pk.substr(32), mp, kLabel, sig.substr(64));
  if (!(ok_ed && ok_ml)) return Error(Errc::Crypto, "hybrid signature: verification failed");
  return ok_status();
}

Result<std::string> hybrid_sign_json(std::string_view sk, std::string_view msg, std::string_view ctx) {
  auto s = hybrid_sign(sk, msg, ctx);
  if (!s) return s.error();
  Json j = Json{{"schema", std::string(kLabel)},
                {"alg", "ed25519+ml-dsa-65"},
                {"ed25519", base64::encode(std::string_view(*s).substr(0, 64))},
                {"ml_dsa_65", base64::encode(std::string_view(*s).substr(64))}};
  return j.dump();
}

Status hybrid_verify_json(std::string_view pk, std::string_view msg, std::string_view ctx, std::string_view sig_json) {
  Json j = Json::parse(sig_json, nullptr, false);
  if (!j.is_object() || j.value("schema", "") != kLabel || !j.contains("ed25519") || !j.contains("ml_dsa_65") ||
      !j["ed25519"].is_string() || !j["ml_dsa_65"].is_string())
    return Error(Errc::Parse, "not a loom.sig_hybrid/1 container");
  auto a = base64::decode(j["ed25519"].get<std::string>(), true);
  auto b = base64::decode(j["ml_dsa_65"].get<std::string>(), true);
  if (!a || !b) return Error(Errc::Parse, "bad base64 in signature container");
  return hybrid_verify(pk, msg, ctx, *a + *b);
}

}  // namespace loom::cs
