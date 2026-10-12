// X-Wing hybrid KEM: X25519 + ML-KEM-768 (draft-connolly-cfrg-xwing-kem).
#include "loom/crypto_suite.h"

namespace loom::cs {
namespace {
// XWingLabel = "\.//^\" (6 bytes, 5c2e2f2f5e5c)
constexpr char kLabel[6] = {0x5c, 0x2e, 0x2f, 0x2f, 0x5e, 0x5c};

struct Expanded {
  Keypair mlkem;  // pk_M, sk_M
  Bytes sk_x;
  Bytes pk_x;
};

Result<Expanded> expand(std::string_view sk32) {
  if (sk32.size() != kXwingSk) return Error(Errc::InvalidArgument, "x-wing: secret key must be 32 bytes");
  Bytes e = shake256(sk32, 96);
  Expanded x;
  auto kp = backend().mlkem768_keypair(std::string_view(e).substr(0, 64));
  if (!kp) return kp.error();
  x.mlkem = std::move(*kp);
  x.sk_x = e.substr(64, 32);
  auto px = backend().x25519_public(x.sk_x);
  if (!px) return px.error();
  x.pk_x = std::move(*px);
  return x;
}

Bytes combiner(const Bytes& ss_m, const Bytes& ss_x, const Bytes& ct_x, const Bytes& pk_x) {
  Bytes in;
  in.reserve(32 * 4 + 6);
  in += ss_m;
  in += ss_x;
  in += ct_x;
  in += pk_x;
  in.append(kLabel, 6);
  return sha3_256(in);
}
}  // namespace

Result<Keypair> xwing_keygen(std::string_view seed) {
  Bytes sk = seed.empty() ? backend().random(kXwingSk) : Bytes(seed);
  auto pk = xwing_public_from_sk(sk);
  if (!pk) return pk.error();
  return Keypair{std::move(*pk), std::move(sk)};
}

Result<Bytes> xwing_public_from_sk(std::string_view sk) {
  auto x = expand(sk);
  if (!x) return x.error();
  return x->mlkem.pk + x->pk_x;
}

Result<Encapsulated> xwing_encaps(std::string_view pk, std::string_view eseed) {
  if (pk.size() != kXwingPk) return Error(Errc::InvalidArgument, "x-wing: public key must be 1216 bytes");
  Bytes es = eseed.empty() ? backend().random(64) : Bytes(eseed);
  if (es.size() != 64) return Error(Errc::InvalidArgument, "x-wing: eseed must be 64 bytes");
  Bytes pk_m(pk.substr(0, kMlKem768Pk)), pk_x(pk.substr(kMlKem768Pk, 32));
  Bytes ek_x = es.substr(32, 32);
  auto ct_x = backend().x25519_public(ek_x);
  if (!ct_x) return ct_x.error();
  auto ss_x = backend().x25519(ek_x, pk_x);
  if (!ss_x) return ss_x.error();
  auto m = backend().mlkem768_encaps(pk_m, std::string_view(es).substr(0, 32));
  if (!m) return m.error();
  Encapsulated out;
  out.ct = m->ct + *ct_x;
  out.ss = combiner(m->ss, *ss_x, *ct_x, pk_x);
  return out;
}

Result<Bytes> xwing_decaps(std::string_view sk, std::string_view ct) {
  if (ct.size() != kXwingCt) return Error(Errc::InvalidArgument, "x-wing: ciphertext must be 1120 bytes");
  auto x = expand(sk);
  if (!x) return x.error();
  Bytes ct_m(ct.substr(0, kMlKem768Ct)), ct_x(ct.substr(kMlKem768Ct, 32));
  auto ss_m = backend().mlkem768_decaps(x->mlkem.sk, ct_m);  // implicit rejection: wrong ct -> pseudorandom ss
  if (!ss_m) return ss_m.error();
  auto ss_x = backend().x25519(x->sk_x, ct_x);
  if (!ss_x) return ss_x.error();
  return combiner(*ss_m, *ss_x, ct_x, x->pk_x);
}

}  // namespace loom::cs
