/* C ABI of the glue over the vendored ML-KEM-768 / ML-DSA-65 (internal). */
#ifndef LOOM_PQ_GLUE_H
#define LOOM_PQ_GLUE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { LOOM_MLKEM768_PK = 1184, LOOM_MLKEM768_SK = 2400, LOOM_MLKEM768_CT = 1088, LOOM_MLKEM768_SS = 32,
       LOOM_MLDSA65_PK = 1952, LOOM_MLDSA65_SK = 4032, LOOM_MLDSA65_SIG = 3309 };
int loom_pq_mlkem768_keypair_derand(uint8_t* pk, uint8_t* sk, const uint8_t* coins64);
int loom_pq_mlkem768_enc_derand(uint8_t* ct, uint8_t* ss, const uint8_t* pk, const uint8_t* coins32);
int loom_pq_mlkem768_dec(uint8_t* ss, const uint8_t* ct, const uint8_t* sk);
int loom_pq_mlkem768_check_pk(const uint8_t* pk);
int loom_pq_mldsa65_keypair(uint8_t* pk, uint8_t* sk, const uint8_t* seed32);
int loom_pq_mldsa65_sign(uint8_t* sig, const uint8_t* m, size_t mlen, const uint8_t* ctx, size_t ctxlen,
                         const uint8_t* rnd32 /* NULL = deterministic */, const uint8_t* sk);
int loom_pq_mldsa65_verify(const uint8_t* sig, const uint8_t* m, size_t mlen, const uint8_t* ctx, size_t ctxlen,
                           const uint8_t* pk);
#ifdef __cplusplus
}
#endif
#endif
