/* Thin C glue over the vendored mlkem-native (ML-KEM-768, FIPS 203) and
 * mldsa-native (ML-DSA-65, FIPS 204). Kept in C so the vendored headers are
 * included with their parameter-set macros only here (see CMakeLists.txt).
 * Randomness is supplied by the caller (derand APIs); `randombytes` below is
 * only required by the vendored randomized entry points that are never called. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mlkem_native.h"
#include "mldsa_native.h"
#include "pq_glue.h"

/* Defined in backend.cpp (OS entropy). */
extern int loom_os_randombytes(uint8_t* out, size_t len);
int randombytes(uint8_t* out, size_t len) { return loom_os_randombytes(out, len); }

int loom_pq_mlkem768_keypair_derand(uint8_t* pk, uint8_t* sk, const uint8_t* coins64) {
  return PQCP_MLKEM_NATIVE_MLKEM768_keypair_derand(pk, sk, coins64);
}
int loom_pq_mlkem768_enc_derand(uint8_t* ct, uint8_t* ss, const uint8_t* pk, const uint8_t* coins32) {
  return PQCP_MLKEM_NATIVE_MLKEM768_enc_derand(ct, ss, pk, coins32);
}
int loom_pq_mlkem768_dec(uint8_t* ss, const uint8_t* ct, const uint8_t* sk) {
  return PQCP_MLKEM_NATIVE_MLKEM768_dec(ss, ct, sk);
}
int loom_pq_mlkem768_check_pk(const uint8_t* pk) { return PQCP_MLKEM_NATIVE_MLKEM768_check_pk(pk); }

int loom_pq_mldsa65_keypair(uint8_t* pk, uint8_t* sk, const uint8_t* seed32) {
  return PQCP_MLDSA_NATIVE_MLDSA65_keypair_internal(pk, sk, seed32);
}
/* FIPS 204 ML-DSA.Sign (external, pure): M' = 0x00 || len(ctx) || ctx || M. */
int loom_pq_mldsa65_sign(uint8_t* sig, const uint8_t* m, size_t mlen, const uint8_t* ctx, size_t ctxlen,
                         const uint8_t* rnd32, const uint8_t* sk) {
  uint8_t pre[2 + 255];
  uint8_t zero[32];
  if (ctxlen > 255) return -1;
  pre[0] = 0;
  pre[1] = (uint8_t)ctxlen;
  if (ctxlen) memcpy(pre + 2, ctx, ctxlen);
  if (!rnd32) {
    memset(zero, 0, sizeof zero);
    rnd32 = zero;
  }
  return PQCP_MLDSA_NATIVE_MLDSA65_signature_internal(sig, m, mlen, pre, 2 + ctxlen, rnd32, sk, 0);
}
int loom_pq_mldsa65_verify(const uint8_t* sig, const uint8_t* m, size_t mlen, const uint8_t* ctx, size_t ctxlen,
                           const uint8_t* pk) {
  if (ctxlen > 255) return -1;
  return PQCP_MLDSA_NATIVE_MLDSA65_verify(sig, m, mlen, ctx, ctxlen, pk);
}
