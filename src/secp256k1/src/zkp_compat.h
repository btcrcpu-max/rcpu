/**********************************************************************
 * zkp_compat.h — compatibility shims for the libsecp256k1-zkp
 * generator/rangeproof modules, ported onto libsecp256k1 0.4.1
 * (as vendored by Bitcoin Core 27 / RCPU).
 *
 * libsecp256k1-zkp (upstream 0.7.x) introduced several internal API
 * changes vs 0.4.1. This header re-exposes the small number of
 * functions the CT modules rely on, implemented on top of 0.4.1
 * primitives. The cryptographic semantics are identical.
 **********************************************************************/
#ifndef SECP256K1_ZKP_COMPAT_H
#define SECP256K1_ZKP_COMPAT_H

#include "field.h"
#include "group.h"
#include "scalar.h"
#include "ecmult_gen.h"
#include "eckey.h"
#include "hash.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

/* --- set_xquad: y = principal square root of x^3 + B ------------------- */
static int secp256k1_ge_set_xquad(secp256k1_ge *r, const secp256k1_fe *x) {
    secp256k1_fe x2, x3;
    int ret;
    r->x = *x;
    secp256k1_fe_sqr(&x2, x);
    secp256k1_fe_mul(&x3, x, &x2);
    r->infinity = 0;
    secp256k1_fe_add_int(&x3, SECP256K1_B);
    ret = secp256k1_fe_sqrt(&r->y, &x3);
    return ret;
}

/* --- ecmult_gen aliases -------------------------------------------------- */
static void secp256k1_ecmult_gen_gej(const secp256k1_ecmult_gen_context* ctx, secp256k1_gej *r, const secp256k1_scalar *a) {
    secp256k1_ecmult_gen(ctx, r, a);
}

static void secp256k1_ecmult_gen_ge(const secp256k1_ecmult_gen_context* ctx, secp256k1_ge *r, const secp256k1_scalar *a) {
    secp256k1_gej rj;
    secp256k1_ecmult_gen(ctx, &rj, a);
    secp256k1_ge_set_gej(r, &rj);
}

/* --- eckey serialize33 --------------------------------------------------- */
static void secp256k1_eckey_pubkey_serialize33(secp256k1_ge *elem, unsigned char *pub33) {
    size_t size = 33;
    secp256k1_eckey_pubkey_serialize(elem, pub33, &size, 1);
}

/* --- sha256_clear / memclear_explicit ------------------------------------ */
static void secp256k1_sha256_clear(secp256k1_sha256 *hash) {
    secp256k1_memczero(hash, sizeof(*hash), 1);
}

static void secp256k1_rfc6979_hmac_sha256_clear(secp256k1_rfc6979_hmac_sha256 *rng) {
    secp256k1_memczero(rng, sizeof(*rng), 1);
}

static SECP256K1_INLINE void secp256k1_memclear_explicit(void *ptr, size_t len) {
    secp256k1_memczero(ptr, len, 1);
}

/* --- scalar_set_u64 ------------------------------------------------------- */
static void secp256k1_scalar_set_u64(secp256k1_scalar *r, uint64_t v) {
    unsigned char b[32] = {0};
    int i, overflow = 0;
    for (i = 0; i < 8; i++) {
        b[31 - i] = (unsigned char)(v >> (8 * i));
    }
    secp256k1_scalar_set_b32(r, b, &overflow);
    (void)overflow; /* v < 2^64 << group order, cannot overflow */
}

/* --- clz64_var ------------------------------------------------------------ */
static SECP256K1_INLINE int secp256k1_clz64_var(uint64_t x) {
    int n = 0;
    if (x == 0) {
        return 64;
    }
    while (!(x & (1ULL << 63))) {
        x <<= 1;
        n++;
    }
    return n;
}

#endif /* SECP256K1_ZKP_COMPAT_H */
