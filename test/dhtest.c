/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/nelem.h"
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#include "testutil.h"

#ifndef OPENSSL_NO_DH
# include <openssl/dh.h>

static int cb(int p, int n, BN_GENCB *arg);

static int dh_test(void)
{
    DH *dh = NULL;
    BIGNUM *p = NULL, *q = NULL, *g = NULL;
    const BIGNUM *p2, *q2, *g2;
    BIGNUM *priv_key = NULL;
    const BIGNUM *pub_key2, *priv_key2;
    BN_GENCB *_cb = NULL;
    DH *a = NULL;
    DH *b = NULL;
    DH *c = NULL;
    const BIGNUM *ap = NULL, *ag = NULL, *apub_key = NULL;
    const BIGNUM *bpub_key = NULL, *bpriv_key = NULL;
    BIGNUM *bp = NULL, *bg = NULL, *cpriv_key = NULL;
    unsigned char *abuf = NULL;
    unsigned char *bbuf = NULL;
    unsigned char *cbuf = NULL;
    int i, alen, blen, clen, aout, bout, cout;
    int ret = 0;

    if (!TEST_ptr(dh = DH_new())
        || !TEST_ptr(p = BN_new())
        || !TEST_ptr(q = BN_new())
        || !TEST_ptr(g = BN_new())
        || !TEST_ptr(priv_key = BN_new()))
        goto err1;

    /*
     * I) basic tests
     */

    /* using a small predefined Sophie Germain DH group with generator 3 */
    if (!TEST_true(BN_set_word(p, 4079L))
        || !TEST_true(BN_set_word(q, 2039L))
        || !TEST_true(BN_set_word(g, 3L))
        || !TEST_true(DH_set0_pqg(dh, p, q, g)))
        goto err1;

    /* check fails, because p is way too small */
    if (!DH_check(dh, &i))
        goto err2;
    i ^= DH_MODULUS_TOO_SMALL;
    if (!TEST_false(i & DH_CHECK_P_NOT_PRIME)
            || !TEST_false(i & DH_CHECK_P_NOT_SAFE_PRIME)
            || !TEST_false(i & DH_UNABLE_TO_CHECK_GENERATOR)
            || !TEST_false(i & DH_NOT_SUITABLE_GENERATOR)
            || !TEST_false(i & DH_CHECK_Q_NOT_PRIME)
            || !TEST_false(i & DH_CHECK_INVALID_Q_VALUE)
            || !TEST_false(i & DH_CHECK_INVALID_J_VALUE)
            || !TEST_false(i & DH_MODULUS_TOO_SMALL)
            || !TEST_false(i & DH_MODULUS_TOO_LARGE)
            || !TEST_false(i))
        goto err2;

    /* test the combined getter for p, q, and g */
    DH_get0_pqg(dh, &p2, &q2, &g2);
    if (!TEST_ptr_eq(p2, p)
        || !TEST_ptr_eq(q2, q)
        || !TEST_ptr_eq(g2, g))
        goto err2;

    /* test the simple getters for p, q, and g */
    if (!TEST_ptr_eq(DH_get0_p(dh), p2)
        || !TEST_ptr_eq(DH_get0_q(dh), q2)
        || !TEST_ptr_eq(DH_get0_g(dh), g2))
        goto err2;

    /* set the private key only*/
    if (!TEST_true(BN_set_word(priv_key, 1234L))
        || !TEST_true(DH_set0_key(dh, NULL, priv_key)))
        goto err2;

    /* test the combined getter for pub_key and priv_key */
    DH_get0_key(dh, &pub_key2, &priv_key2);
    if (!TEST_ptr_eq(pub_key2, NULL)
        || !TEST_ptr_eq(priv_key2, priv_key))
        goto err3;

    /* test the simple getters for pub_key and priv_key */
    if (!TEST_ptr_eq(DH_get0_pub_key(dh), pub_key2)
        || !TEST_ptr_eq(DH_get0_priv_key(dh), priv_key2))
        goto err3;

    /* now generate a key pair (expect failure since modulus is too small) */
    if (!TEST_false(DH_generate_key(dh)))
        goto err3;

    /* We'll have a stale error on the queue from the above test so clear it */
    ERR_clear_error();

    /*
     * II) key generation
     */

    /* generate a DH group ... */
    if (!TEST_ptr(_cb = BN_GENCB_new()))
        goto err3;
    BN_GENCB_set(_cb, &cb, NULL);
    if (!TEST_ptr(a = DH_new())
            || !TEST_true(DH_generate_parameters_ex(a, 512,
                                                    DH_GENERATOR_5, _cb)))
        goto err3;

    /* ... and check whether it is valid */
    if (!DH_check(a, &i))
        goto err3;
    if (!TEST_false(i & DH_CHECK_P_NOT_PRIME)
            || !TEST_false(i & DH_CHECK_P_NOT_SAFE_PRIME)
            || !TEST_false(i & DH_UNABLE_TO_CHECK_GENERATOR)
            || !TEST_false(i & DH_NOT_SUITABLE_GENERATOR)
            || !TEST_false(i & DH_CHECK_Q_NOT_PRIME)
            || !TEST_false(i & DH_CHECK_INVALID_Q_VALUE)
            || !TEST_false(i & DH_CHECK_INVALID_J_VALUE)
            || !TEST_false(i & DH_MODULUS_TOO_SMALL)
            || !TEST_false(i & DH_MODULUS_TOO_LARGE)
            || !TEST_false(i))
        goto err3;

    DH_get0_pqg(a, &ap, NULL, &ag);

    /* now create another copy of the DH group for the peer */
    if (!TEST_ptr(b = DH_new()))
        goto err3;

    if (!TEST_ptr(bp = BN_dup(ap))
            || !TEST_ptr(bg = BN_dup(ag))
            || !TEST_true(DH_set0_pqg(b, bp, NULL, bg)))
        goto err3;
    bp = bg = NULL;

    /*
     * III) simulate a key exchange
     */

    if (!DH_generate_key(a))
        goto err3;
    DH_get0_key(a, &apub_key, NULL);

    if (!DH_generate_key(b))
        goto err3;
    DH_get0_key(b, &bpub_key, &bpriv_key);

    /* Also test with a private-key-only copy of |b|. */
    if (!TEST_ptr(c = DHparams_dup(b))
            || !TEST_ptr(cpriv_key = BN_dup(bpriv_key))
            || !TEST_true(DH_set0_key(c, NULL, cpriv_key)))
        goto err3;
    cpriv_key = NULL;

    alen = DH_size(a);
    if (!TEST_ptr(abuf = OPENSSL_malloc(alen))
            || !TEST_true((aout = DH_compute_key(abuf, bpub_key, a)) != -1))
        goto err3;

    blen = DH_size(b);
    if (!TEST_ptr(bbuf = OPENSSL_malloc(blen))
            || !TEST_true((bout = DH_compute_key(bbuf, apub_key, b)) != -1))
        goto err3;

    clen = DH_size(c);
    if (!TEST_ptr(cbuf = OPENSSL_malloc(clen))
            || !TEST_true((cout = DH_compute_key(cbuf, apub_key, c)) != -1))
        goto err3;

    if (!TEST_true(aout >= 20)
            || !TEST_mem_eq(abuf, aout, bbuf, bout)
            || !TEST_mem_eq(abuf, aout, cbuf, cout))
        goto err3;

    ret = 1;
    goto success;

 err1:
    /* an error occurred before p,q,g were assigned to dh */
    BN_free(p);
    BN_free(q);
    BN_free(g);
 err2:
    /* an error occurred before priv_key was assigned to dh */
    BN_free(priv_key);
 err3:
 success:
    OPENSSL_free(abuf);
    OPENSSL_free(bbuf);
    OPENSSL_free(cbuf);
    DH_free(b);
    DH_free(a);
    DH_free(c);
    BN_free(bp);
    BN_free(bg);
    BN_free(cpriv_key);
    BN_GENCB_free(_cb);
    DH_free(dh);

    return ret;
}

static int cb(int p, int n, BN_GENCB *arg)
{
    return 1;
}

/* Test data from RFC 5114 */

static const unsigned char dhtest_1024_160_xA[] = {
    0xB9, 0xA3, 0xB3, 0xAE, 0x8F, 0xEF, 0xC1, 0xA2, 0x93, 0x04, 0x96, 0x50,
    0x70, 0x86, 0xF8, 0x45, 0x5D, 0x48, 0x94, 0x3E
};

static const unsigned char dhtest_1024_160_yA[] = {
    0x2A, 0x85, 0x3B, 0x3D, 0x92, 0x19, 0x75, 0x01, 0xB9, 0x01, 0x5B, 0x2D,
    0xEB, 0x3E, 0xD8, 0x4F, 0x5E, 0x02, 0x1D, 0xCC, 0x3E, 0x52, 0xF1, 0x09,
    0xD3, 0x27, 0x3D, 0x2B, 0x75, 0x21, 0x28, 0x1C, 0xBA, 0xBE, 0x0E, 0x76,
    0xFF, 0x57, 0x27, 0xFA, 0x8A, 0xCC, 0xE2, 0x69, 0x56, 0xBA, 0x9A, 0x1F,
    0xCA, 0x26, 0xF2, 0x02, 0x28, 0xD8, 0x69, 0x3F, 0xEB, 0x10, 0x84, 0x1D,
    0x84, 0xA7, 0x36, 0x00, 0x54, 0xEC, 0xE5, 0xA7, 0xF5, 0xB7, 0xA6, 0x1A,
    0xD3, 0xDF, 0xB3, 0xC6, 0x0D, 0x2E, 0x43, 0x10, 0x6D, 0x87, 0x27, 0xDA,
    0x37, 0xDF, 0x9C, 0xCE, 0x95, 0xB4, 0x78, 0x75, 0x5D, 0x06, 0xBC, 0xEA,
    0x8F, 0x9D, 0x45, 0x96, 0x5F, 0x75, 0xA5, 0xF3, 0xD1, 0xDF, 0x37, 0x01,
    0x16, 0x5F, 0xC9, 0xE5, 0x0C, 0x42, 0x79, 0xCE, 0xB0, 0x7F, 0x98, 0x95,
    0x40, 0xAE, 0x96, 0xD5, 0xD8, 0x8E, 0xD7, 0x76
};

static const unsigned char dhtest_1024_160_xB[] = {
    0x93, 0x92, 0xC9, 0xF9, 0xEB, 0x6A, 0x7A, 0x6A, 0x90, 0x22, 0xF7, 0xD8,
    0x3E, 0x72, 0x23, 0xC6, 0x83, 0x5B, 0xBD, 0xDA
};

static const unsigned char dhtest_1024_160_yB[] = {
    0x71, 0x7A, 0x6C, 0xB0, 0x53, 0x37, 0x1F, 0xF4, 0xA3, 0xB9, 0x32, 0x94,
    0x1C, 0x1E, 0x56, 0x63, 0xF8, 0x61, 0xA1, 0xD6, 0xAD, 0x34, 0xAE, 0x66,
    0x57, 0x6D, 0xFB, 0x98, 0xF6, 0xC6, 0xCB, 0xF9, 0xDD, 0xD5, 0xA5, 0x6C,
    0x78, 0x33, 0xF6, 0xBC, 0xFD, 0xFF, 0x09, 0x55, 0x82, 0xAD, 0x86, 0x8E,
    0x44, 0x0E, 0x8D, 0x09, 0xFD, 0x76, 0x9E, 0x3C, 0xEC, 0xCD, 0xC3, 0xD3,
    0xB1, 0xE4, 0xCF, 0xA0, 0x57, 0x77, 0x6C, 0xAA, 0xF9, 0x73, 0x9B, 0x6A,
    0x9F, 0xEE, 0x8E, 0x74, 0x11, 0xF8, 0xD6, 0xDA, 0xC0, 0x9D, 0x6A, 0x4E,
    0xDB, 0x46, 0xCC, 0x2B, 0x5D, 0x52, 0x03, 0x09, 0x0E, 0xAE, 0x61, 0x26,
    0x31, 0x1E, 0x53, 0xFD, 0x2C, 0x14, 0xB5, 0x74, 0xE6, 0xA3, 0x10, 0x9A,
    0x3D, 0xA1, 0xBE, 0x41, 0xBD, 0xCE, 0xAA, 0x18, 0x6F, 0x5C, 0xE0, 0x67,
    0x16, 0xA2, 0xB6, 0xA0, 0x7B, 0x3C, 0x33, 0xFE
};

static const unsigned char dhtest_1024_160_Z[] = {
    0x5C, 0x80, 0x4F, 0x45, 0x4D, 0x30, 0xD9, 0xC4, 0xDF, 0x85, 0x27, 0x1F,
    0x93, 0x52, 0x8C, 0x91, 0xDF, 0x6B, 0x48, 0xAB, 0x5F, 0x80, 0xB3, 0xB5,
    0x9C, 0xAA, 0xC1, 0xB2, 0x8F, 0x8A, 0xCB, 0xA9, 0xCD, 0x3E, 0x39, 0xF3,
    0xCB, 0x61, 0x45, 0x25, 0xD9, 0x52, 0x1D, 0x2E, 0x64, 0x4C, 0x53, 0xB8,
    0x07, 0xB8, 0x10, 0xF3, 0x40, 0x06, 0x2F, 0x25, 0x7D, 0x7D, 0x6F, 0xBF,
    0xE8, 0xD5, 0xE8, 0xF0, 0x72, 0xE9, 0xB6, 0xE9, 0xAF, 0xDA, 0x94, 0x13,
    0xEA, 0xFB, 0x2E, 0x8B, 0x06, 0x99, 0xB1, 0xFB, 0x5A, 0x0C, 0xAC, 0xED,
    0xDE, 0xAE, 0xAD, 0x7E, 0x9C, 0xFB, 0xB3, 0x6A, 0xE2, 0xB4, 0x20, 0x83,
    0x5B, 0xD8, 0x3A, 0x19, 0xFB, 0x0B, 0x5E, 0x96, 0xBF, 0x8F, 0xA4, 0xD0,
    0x9E, 0x34, 0x55, 0x25, 0x16, 0x7E, 0xCD, 0x91, 0x55, 0x41, 0x6F, 0x46,
    0xF4, 0x08, 0xED, 0x31, 0xB6, 0x3C, 0x6E, 0x6D
};

static const unsigned char dhtest_2048_224_xA[] = {
    0x22, 0xE6, 0x26, 0x01, 0xDB, 0xFF, 0xD0, 0x67, 0x08, 0xA6, 0x80, 0xF7,
    0x47, 0xF3, 0x61, 0xF7, 0x6D, 0x8F, 0x4F, 0x72, 0x1A, 0x05, 0x48, 0xE4,
    0x83, 0x29, 0x4B, 0x0C
};

static const unsigned char dhtest_2048_224_yA[] = {
    0x1B, 0x3A, 0x63, 0x45, 0x1B, 0xD8, 0x86, 0xE6, 0x99, 0xE6, 0x7B, 0x49,
    0x4E, 0x28, 0x8B, 0xD7, 0xF8, 0xE0, 0xD3, 0x70, 0xBA, 0xDD, 0xA7, 0xA0,
    0xEF, 0xD2, 0xFD, 0xE7, 0xD8, 0xF6, 0x61, 0x45, 0xCC, 0x9F, 0x28, 0x04,
    0x19, 0x97, 0x5E, 0xB8, 0x08, 0x87, 0x7C, 0x8A, 0x4C, 0x0C, 0x8E, 0x0B,
    0xD4, 0x8D, 0x4A, 0x54, 0x01, 0xEB, 0x1E, 0x87, 0x76, 0xBF, 0xEE, 0xE1,
    0x34, 0xC0, 0x38, 0x31, 0xAC, 0x27, 0x3C, 0xD9, 0xD6, 0x35, 0xAB, 0x0C,
    0xE0, 0x06, 0xA4, 0x2A, 0x88, 0x7E, 0x3F, 0x52, 0xFB, 0x87, 0x66, 0xB6,
    0x50, 0xF3, 0x80, 0x78, 0xBC, 0x8E, 0xE8, 0x58, 0x0C, 0xEF, 0xE2, 0x43,
    0x96, 0x8C, 0xFC, 0x4F, 0x8D, 0xC3, 0xDB, 0x08, 0x45, 0x54, 0x17, 0x1D,
    0x41, 0xBF, 0x2E, 0x86, 0x1B, 0x7B, 0xB4, 0xD6, 0x9D, 0xD0, 0xE0, 0x1E,
    0xA3, 0x87, 0xCB, 0xAA, 0x5C, 0xA6, 0x72, 0xAF, 0xCB, 0xE8, 0xBD, 0xB9,
    0xD6, 0x2D, 0x4C, 0xE1, 0x5F, 0x17, 0xDD, 0x36, 0xF9, 0x1E, 0xD1, 0xEE,
    0xDD, 0x65, 0xCA, 0x4A, 0x06, 0x45, 0x5C, 0xB9, 0x4C, 0xD4, 0x0A, 0x52,
    0xEC, 0x36, 0x0E, 0x84, 0xB3, 0xC9, 0x26, 0xE2, 0x2C, 0x43, 0x80, 0xA3,
    0xBF, 0x30, 0x9D, 0x56, 0x84, 0x97, 0x68, 0xB7, 0xF5, 0x2C, 0xFD, 0xF6,
    0x55, 0xFD, 0x05, 0x3A, 0x7E, 0xF7, 0x06, 0x97, 0x9E, 0x7E, 0x58, 0x06,
    0xB1, 0x7D, 0xFA, 0xE5, 0x3A, 0xD2, 0xA5, 0xBC, 0x56, 0x8E, 0xBB, 0x52,
    0x9A, 0x7A, 0x61, 0xD6, 0x8D, 0x25, 0x6F, 0x8F, 0xC9, 0x7C, 0x07, 0x4A,
    0x86, 0x1D, 0x82, 0x7E, 0x2E, 0xBC, 0x8C, 0x61, 0x34, 0x55, 0x31, 0x15,
    0xB7, 0x0E, 0x71, 0x03, 0x92, 0x0A, 0xA1, 0x6D, 0x85, 0xE5, 0x2B, 0xCB,
    0xAB, 0x8D, 0x78, 0x6A, 0x68, 0x17, 0x8F, 0xA8, 0xFF, 0x7C, 0x2F, 0x5C,
    0x71, 0x64, 0x8D, 0x6F
};

static const unsigned char dhtest_2048_224_xB[] = {
    0x4F, 0xF3, 0xBC, 0x96, 0xC7, 0xFC, 0x6A, 0x6D, 0x71, 0xD3, 0xB3, 0x63,
    0x80, 0x0A, 0x7C, 0xDF, 0xEF, 0x6F, 0xC4, 0x1B, 0x44, 0x17, 0xEA, 0x15,
    0x35, 0x3B, 0x75, 0x90
};

static const unsigned char dhtest_2048_224_yB[] = {
    0x4D, 0xCE, 0xE9, 0x92, 0xA9, 0x76, 0x2A, 0x13, 0xF2, 0xF8, 0x38, 0x44,
    0xAD, 0x3D, 0x77, 0xEE, 0x0E, 0x31, 0xC9, 0x71, 0x8B, 0x3D, 0xB6, 0xC2,
    0x03, 0x5D, 0x39, 0x61, 0x18, 0x2C, 0x3E, 0x0B, 0xA2, 0x47, 0xEC, 0x41,
    0x82, 0xD7, 0x60, 0xCD, 0x48, 0xD9, 0x95, 0x99, 0x97, 0x06, 0x22, 0xA1,
    0x88, 0x1B, 0xBA, 0x2D, 0xC8, 0x22, 0x93, 0x9C, 0x78, 0xC3, 0x91, 0x2C,
    0x66, 0x61, 0xFA, 0x54, 0x38, 0xB2, 0x07, 0x66, 0x22, 0x2B, 0x75, 0xE2,
    0x4C, 0x2E, 0x3A, 0xD0, 0xC7, 0x28, 0x72, 0x36, 0x12, 0x95, 0x25, 0xEE,
    0x15, 0xB5, 0xDD, 0x79, 0x98, 0xAA, 0x04, 0xC4, 0xA9, 0x69, 0x6C, 0xAC,
    0xD7, 0x17, 0x20, 0x83, 0xA9, 0x7A, 0x81, 0x66, 0x4E, 0xAD, 0x2C, 0x47,
    0x9E, 0x44, 0x4E, 0x4C, 0x06, 0x54, 0xCC, 0x19, 0xE2, 0x8D, 0x77, 0x03,
    0xCE, 0xE8, 0xDA, 0xCD, 0x61, 0x26, 0xF5, 0xD6, 0x65, 0xEC, 0x52, 0xC6,
    0x72, 0x55, 0xDB, 0x92, 0x01, 0x4B, 0x03, 0x7E, 0xB6, 0x21, 0xA2, 0xAC,
    0x8E, 0x36, 0x5D, 0xE0, 0x71, 0xFF, 0xC1, 0x40, 0x0A, 0xCF, 0x07, 0x7A,
    0x12, 0x91, 0x3D, 0xD8, 0xDE, 0x89, 0x47, 0x34, 0x37, 0xAB, 0x7B, 0xA3,
    0x46, 0x74, 0x3C, 0x1B, 0x21, 0x5D, 0xD9, 0xC1, 0x21, 0x64, 0xA7, 0xE4,
    0x05, 0x31, 0x18, 0xD1, 0x99, 0xBE, 0xC8, 0xEF, 0x6F, 0xC5, 0x61, 0x17,
    0x0C, 0x84, 0xC8, 0x7D, 0x10, 0xEE, 0x9A, 0x67, 0x4A, 0x1F, 0xA8, 0xFF,
    0xE1, 0x3B, 0xDF, 0xBA, 0x1D, 0x44, 0xDE, 0x48, 0x94, 0x6D, 0x68, 0xDC,
    0x0C, 0xDD, 0x77, 0x76, 0x35, 0xA7, 0xAB, 0x5B, 0xFB, 0x1E, 0x4B, 0xB7,
    0xB8, 0x56, 0xF9, 0x68, 0x27, 0x73, 0x4C, 0x18, 0x41, 0x38, 0xE9, 0x15,
    0xD9, 0xC3, 0x00, 0x2E, 0xBC, 0xE5, 0x31, 0x20, 0x54, 0x6A, 0x7E, 0x20,
    0x02, 0x14, 0x2B, 0x6C
};

static const unsigned char dhtest_2048_224_Z[] = {
    0x34, 0xD9, 0xBD, 0xDC, 0x1B, 0x42, 0x17, 0x6C, 0x31, 0x3F, 0xEA, 0x03,
    0x4C, 0x21, 0x03, 0x4D, 0x07, 0x4A, 0x63, 0x13, 0xBB, 0x4E, 0xCD, 0xB3,
    0x70, 0x3F, 0xFF, 0x42, 0x45, 0x67, 0xA4, 0x6B, 0xDF, 0x75, 0x53, 0x0E,
    0xDE, 0x0A, 0x9D, 0xA5, 0x22, 0x9D, 0xE7, 0xD7, 0x67, 0x32, 0x28, 0x6C,
    0xBC, 0x0F, 0x91, 0xDA, 0x4C, 0x3C, 0x85, 0x2F, 0xC0, 0x99, 0xC6, 0x79,
    0x53, 0x1D, 0x94, 0xC7, 0x8A, 0xB0, 0x3D, 0x9D, 0xEC, 0xB0, 0xA4, 0xE4,
    0xCA, 0x8B, 0x2B, 0xB4, 0x59, 0x1C, 0x40, 0x21, 0xCF, 0x8C, 0xE3, 0xA2,
    0x0A, 0x54, 0x1D, 0x33, 0x99, 0x40, 0x17, 0xD0, 0x20, 0x0A, 0xE2, 0xC9,
    0x51, 0x6E, 0x2F, 0xF5, 0x14, 0x57, 0x79, 0x26, 0x9E, 0x86, 0x2B, 0x0F,
    0xB4, 0x74, 0xA2, 0xD5, 0x6D, 0xC3, 0x1E, 0xD5, 0x69, 0xA7, 0x70, 0x0B,
    0x4C, 0x4A, 0xB1, 0x6B, 0x22, 0xA4, 0x55, 0x13, 0x53, 0x1E, 0xF5, 0x23,
    0xD7, 0x12, 0x12, 0x07, 0x7B, 0x5A, 0x16, 0x9B, 0xDE, 0xFF, 0xAD, 0x7A,
    0xD9, 0x60, 0x82, 0x84, 0xC7, 0x79, 0x5B, 0x6D, 0x5A, 0x51, 0x83, 0xB8,
    0x70, 0x66, 0xDE, 0x17, 0xD8, 0xD6, 0x71, 0xC9, 0xEB, 0xD8, 0xEC, 0x89,
    0x54, 0x4D, 0x45, 0xEC, 0x06, 0x15, 0x93, 0xD4, 0x42, 0xC6, 0x2A, 0xB9,
    0xCE, 0x3B, 0x1C, 0xB9, 0x94, 0x3A, 0x1D, 0x23, 0xA5, 0xEA, 0x3B, 0xCF,
    0x21, 0xA0, 0x14, 0x71, 0xE6, 0x7E, 0x00, 0x3E, 0x7F, 0x8A, 0x69, 0xC7,
    0x28, 0xBE, 0x49, 0x0B, 0x2F, 0xC8, 0x8C, 0xFE, 0xB9, 0x2D, 0xB6, 0xA2,
    0x15, 0xE5, 0xD0, 0x3C, 0x17, 0xC4, 0x64, 0xC9, 0xAC, 0x1A, 0x46, 0xE2,
    0x03, 0xE1, 0x3F, 0x95, 0x29, 0x95, 0xFB, 0x03, 0xC6, 0x9D, 0x3C, 0xC4,
    0x7F, 0xCB, 0x51, 0x0B, 0x69, 0x98, 0xFF, 0xD3, 0xAA, 0x6D, 0xE7, 0x3C,
    0xF9, 0xF6, 0x38, 0x69
};

static const unsigned char dhtest_2048_256_xA[] = {
    0x08, 0x81, 0x38, 0x2C, 0xDB, 0x87, 0x66, 0x0C, 0x6D, 0xC1, 0x3E, 0x61,
    0x49, 0x38, 0xD5, 0xB9, 0xC8, 0xB2, 0xF2, 0x48, 0x58, 0x1C, 0xC5, 0xE3,
    0x1B, 0x35, 0x45, 0x43, 0x97, 0xFC, 0xE5, 0x0E
};

static const unsigned char dhtest_2048_256_yA[] = {
    0x2E, 0x93, 0x80, 0xC8, 0x32, 0x3A, 0xF9, 0x75, 0x45, 0xBC, 0x49, 0x41,
    0xDE, 0xB0, 0xEC, 0x37, 0x42, 0xC6, 0x2F, 0xE0, 0xEC, 0xE8, 0x24, 0xA6,
    0xAB, 0xDB, 0xE6, 0x6C, 0x59, 0xBE, 0xE0, 0x24, 0x29, 0x11, 0xBF, 0xB9,
    0x67, 0x23, 0x5C, 0xEB, 0xA3, 0x5A, 0xE1, 0x3E, 0x4E, 0xC7, 0x52, 0xBE,
    0x63, 0x0B, 0x92, 0xDC, 0x4B, 0xDE, 0x28, 0x47, 0xA9, 0xC6, 0x2C, 0xB8,
    0x15, 0x27, 0x45, 0x42, 0x1F, 0xB7, 0xEB, 0x60, 0xA6, 0x3C, 0x0F, 0xE9,
    0x15, 0x9F, 0xCC, 0xE7, 0x26, 0xCE, 0x7C, 0xD8, 0x52, 0x3D, 0x74, 0x50,
    0x66, 0x7E, 0xF8, 0x40, 0xE4, 0x91, 0x91, 0x21, 0xEB, 0x5F, 0x01, 0xC8,
    0xC9, 0xB0, 0xD3, 0xD6, 0x48, 0xA9, 0x3B, 0xFB, 0x75, 0x68, 0x9E, 0x82,
    0x44, 0xAC, 0x13, 0x4A, 0xF5, 0x44, 0x71, 0x1C, 0xE7, 0x9A, 0x02, 0xDC,
    0xC3, 0x42, 0x26, 0x68, 0x47, 0x80, 0xDD, 0xDC, 0xB4, 0x98, 0x59, 0x41,
    0x06, 0xC3, 0x7F, 0x5B, 0xC7, 0x98, 0x56, 0x48, 0x7A, 0xF5, 0xAB, 0x02,
    0x2A, 0x2E, 0x5E, 0x42, 0xF0, 0x98, 0x97, 0xC1, 0xA8, 0x5A, 0x11, 0xEA,
    0x02, 0x12, 0xAF, 0x04, 0xD9, 0xB4, 0xCE, 0xBC, 0x93, 0x7C, 0x3C, 0x1A,
    0x3E, 0x15, 0xA8, 0xA0, 0x34, 0x2E, 0x33, 0x76, 0x15, 0xC8, 0x4E, 0x7F,
    0xE3, 0xB8, 0xB9, 0xB8, 0x7F, 0xB1, 0xE7, 0x3A, 0x15, 0xAF, 0x12, 0xA3,
    0x0D, 0x74, 0x6E, 0x06, 0xDF, 0xC3, 0x4F, 0x29, 0x0D, 0x79, 0x7C, 0xE5,
    0x1A, 0xA1, 0x3A, 0xA7, 0x85, 0xBF, 0x66, 0x58, 0xAF, 0xF5, 0xE4, 0xB0,
    0x93, 0x00, 0x3C, 0xBE, 0xAF, 0x66, 0x5B, 0x3C, 0x2E, 0x11, 0x3A, 0x3A,
    0x4E, 0x90, 0x52, 0x69, 0x34, 0x1D, 0xC0, 0x71, 0x14, 0x26, 0x68, 0x5F,
    0x4E, 0xF3, 0x7E, 0x86, 0x8A, 0x81, 0x26, 0xFF, 0x3F, 0x22, 0x79, 0xB5,
    0x7C, 0xA6, 0x7E, 0x29
};

static const unsigned char dhtest_2048_256_xB[] = {
    0x7D, 0x62, 0xA7, 0xE3, 0xEF, 0x36, 0xDE, 0x61, 0x7B, 0x13, 0xD1, 0xAF,
    0xB8, 0x2C, 0x78, 0x0D, 0x83, 0xA2, 0x3B, 0xD4, 0xEE, 0x67, 0x05, 0x64,
    0x51, 0x21, 0xF3, 0x71, 0xF5, 0x46, 0xA5, 0x3D
};

static const unsigned char dhtest_2048_256_yB[] = {
    0x57, 0x5F, 0x03, 0x51, 0xBD, 0x2B, 0x1B, 0x81, 0x74, 0x48, 0xBD, 0xF8,
    0x7A, 0x6C, 0x36, 0x2C, 0x1E, 0x28, 0x9D, 0x39, 0x03, 0xA3, 0x0B, 0x98,
    0x32, 0xC5, 0x74, 0x1F, 0xA2, 0x50, 0x36, 0x3E, 0x7A, 0xCB, 0xC7, 0xF7,
    0x7F, 0x3D, 0xAC, 0xBC, 0x1F, 0x13, 0x1A, 0xDD, 0x8E, 0x03, 0x36, 0x7E,
    0xFF, 0x8F, 0xBB, 0xB3, 0xE1, 0xC5, 0x78, 0x44, 0x24, 0x80, 0x9B, 0x25,
    0xAF, 0xE4, 0xD2, 0x26, 0x2A, 0x1A, 0x6F, 0xD2, 0xFA, 0xB6, 0x41, 0x05,
    0xCA, 0x30, 0xA6, 0x74, 0xE0, 0x7F, 0x78, 0x09, 0x85, 0x20, 0x88, 0x63,
    0x2F, 0xC0, 0x49, 0x23, 0x37, 0x91, 0xAD, 0x4E, 0xDD, 0x08, 0x3A, 0x97,
    0x8B, 0x88, 0x3E, 0xE6, 0x18, 0xBC, 0x5E, 0x0D, 0xD0, 0x47, 0x41, 0x5F,
    0x2D, 0x95, 0xE6, 0x83, 0xCF, 0x14, 0x82, 0x6B, 0x5F, 0xBE, 0x10, 0xD3,
    0xCE, 0x41, 0xC6, 0xC1, 0x20, 0xC7, 0x8A, 0xB2, 0x00, 0x08, 0xC6, 0x98,
    0xBF, 0x7F, 0x0B, 0xCA, 0xB9, 0xD7, 0xF4, 0x07, 0xBE, 0xD0, 0xF4, 0x3A,
    0xFB, 0x29, 0x70, 0xF5, 0x7F, 0x8D, 0x12, 0x04, 0x39, 0x63, 0xE6, 0x6D,
    0xDD, 0x32, 0x0D, 0x59, 0x9A, 0xD9, 0x93, 0x6C, 0x8F, 0x44, 0x13, 0x7C,
    0x08, 0xB1, 0x80, 0xEC, 0x5E, 0x98, 0x5C, 0xEB, 0xE1, 0x86, 0xF3, 0xD5,
    0x49, 0x67, 0x7E, 0x80, 0x60, 0x73, 0x31, 0xEE, 0x17, 0xAF, 0x33, 0x80,
    0xA7, 0x25, 0xB0, 0x78, 0x23, 0x17, 0xD7, 0xDD, 0x43, 0xF5, 0x9D, 0x7A,
    0xF9, 0x56, 0x8A, 0x9B, 0xB6, 0x3A, 0x84, 0xD3, 0x65, 0xF9, 0x22, 0x44,
    0xED, 0x12, 0x09, 0x88, 0x21, 0x93, 0x02, 0xF4, 0x29, 0x24, 0xC7, 0xCA,
    0x90, 0xB8, 0x9D, 0x24, 0xF7, 0x1B, 0x0A, 0xB6, 0x97, 0x82, 0x3D, 0x7D,
    0xEB, 0x1A, 0xFF, 0x5B, 0x0E, 0x8E, 0x4A, 0x45, 0xD4, 0x9F, 0x7F, 0x53,
    0x75, 0x7E, 0x19, 0x13
};

static const unsigned char dhtest_2048_256_Z[] = {
    0x86, 0xC7, 0x0B, 0xF8, 0xD0, 0xBB, 0x81, 0xBB, 0x01, 0x07, 0x8A, 0x17,
    0x21, 0x9C, 0xB7, 0xD2, 0x72, 0x03, 0xDB, 0x2A, 0x19, 0xC8, 0x77, 0xF1,
    0xD1, 0xF1, 0x9F, 0xD7, 0xD7, 0x7E, 0xF2, 0x25, 0x46, 0xA6, 0x8F, 0x00,
    0x5A, 0xD5, 0x2D, 0xC8, 0x45, 0x53, 0xB7, 0x8F, 0xC6, 0x03, 0x30, 0xBE,
    0x51, 0xEA, 0x7C, 0x06, 0x72, 0xCA, 0xC1, 0x51, 0x5E, 0x4B, 0x35, 0xC0,
    0x47, 0xB9, 0xA5, 0x51, 0xB8, 0x8F, 0x39, 0xDC, 0x26, 0xDA, 0x14, 0xA0,
    0x9E, 0xF7, 0x47, 0x74, 0xD4, 0x7C, 0x76, 0x2D, 0xD1, 0x77, 0xF9, 0xED,
    0x5B, 0xC2, 0xF1, 0x1E, 0x52, 0xC8, 0x79, 0xBD, 0x95, 0x09, 0x85, 0x04,
    0xCD, 0x9E, 0xEC, 0xD8, 0xA8, 0xF9, 0xB3, 0xEF, 0xBD, 0x1F, 0x00, 0x8A,
    0xC5, 0x85, 0x30, 0x97, 0xD9, 0xD1, 0x83, 0x7F, 0x2B, 0x18, 0xF7, 0x7C,
    0xD7, 0xBE, 0x01, 0xAF, 0x80, 0xA7, 0xC7, 0xB5, 0xEA, 0x3C, 0xA5, 0x4C,
    0xC0, 0x2D, 0x0C, 0x11, 0x6F, 0xEE, 0x3F, 0x95, 0xBB, 0x87, 0x39, 0x93,
    0x85, 0x87, 0x5D, 0x7E, 0x86, 0x74, 0x7E, 0x67, 0x6E, 0x72, 0x89, 0x38,
    0xAC, 0xBF, 0xF7, 0x09, 0x8E, 0x05, 0xBE, 0x4D, 0xCF, 0xB2, 0x40, 0x52,
    0xB8, 0x3A, 0xEF, 0xFB, 0x14, 0x78, 0x3F, 0x02, 0x9A, 0xDB, 0xDE, 0x7F,
    0x53, 0xFA, 0xE9, 0x20, 0x84, 0x22, 0x40, 0x90, 0xE0, 0x07, 0xCE, 0xE9,
    0x4D, 0x4B, 0xF2, 0xBA, 0xCE, 0x9F, 0xFD, 0x4B, 0x57, 0xD2, 0xAF, 0x7C,
    0x72, 0x4D, 0x0C, 0xAA, 0x19, 0xBF, 0x05, 0x01, 0xF6, 0xF1, 0x7B, 0x4A,
    0xA1, 0x0F, 0x42, 0x5E, 0x3E, 0xA7, 0x60, 0x80, 0xB4, 0xB9, 0xD6, 0xB3,
    0xCE, 0xFE, 0xA1, 0x15, 0xB2, 0xCE, 0xB8, 0x78, 0x9B, 0xB8, 0xA3, 0xB0,
    0xEA, 0x87, 0xFE, 0xBE, 0x63, 0xB6, 0xC8, 0xF8, 0x46, 0xEC, 0x6D, 0xB0,
    0xC2, 0x6C, 0x5D, 0x7C
};

static const unsigned char dhtest_rfc5114_2048_224_bad_y[] = {
    0x45, 0x32, 0x5F, 0x51, 0x07, 0xE5, 0xDF, 0x1C, 0xD6, 0x02, 0x82, 0xB3,
    0x32, 0x8F, 0xA4, 0x0F, 0x87, 0xB8, 0x41, 0xFE, 0xB9, 0x35, 0xDE, 0xAD,
    0xC6, 0x26, 0x85, 0xB4, 0xFF, 0x94, 0x8C, 0x12, 0x4C, 0xBF, 0x5B, 0x20,
    0xC4, 0x46, 0xA3, 0x26, 0xEB, 0xA4, 0x25, 0xB7, 0x68, 0x8E, 0xCC, 0x67,
    0xBA, 0xEA, 0x58, 0xD0, 0xF2, 0xE9, 0xD2, 0x24, 0x72, 0x60, 0xDA, 0x88,
    0x18, 0x9C, 0xE0, 0x31, 0x6A, 0xAD, 0x50, 0x6D, 0x94, 0x35, 0x8B, 0x83,
    0x4A, 0x6E, 0xFA, 0x48, 0x73, 0x0F, 0x83, 0x87, 0xFF, 0x6B, 0x66, 0x1F,
    0xA8, 0x82, 0xC6, 0x01, 0xE5, 0x80, 0xB5, 0xB0, 0x52, 0xD0, 0xE9, 0xD8,
    0x72, 0xF9, 0x7D, 0x5B, 0x8B, 0xA5, 0x4C, 0xA5, 0x25, 0x95, 0x74, 0xE2,
    0x7A, 0x61, 0x4E, 0xA7, 0x8F, 0x12, 0xE2, 0xD2, 0x9D, 0x8C, 0x02, 0x70,
    0x34, 0x44, 0x32, 0xC7, 0xB2, 0xF3, 0xB9, 0xFE, 0x17, 0x2B, 0xD6, 0x1F,
    0x8B, 0x7E, 0x4A, 0xFA, 0xA3, 0xB5, 0x3E, 0x7A, 0x81, 0x9A, 0x33, 0x66,
    0x62, 0xA4, 0x50, 0x18, 0x3E, 0xA2, 0x5F, 0x00, 0x07, 0xD8, 0x9B, 0x22,
    0xE4, 0xEC, 0x84, 0xD5, 0xEB, 0x5A, 0xF3, 0x2A, 0x31, 0x23, 0xD8, 0x44,
    0x22, 0x2A, 0x8B, 0x37, 0x44, 0xCC, 0xC6, 0x87, 0x4B, 0xBE, 0x50, 0x9D,
    0x4A, 0xC4, 0x8E, 0x45, 0xCF, 0x72, 0x4D, 0xC0, 0x89, 0xB3, 0x72, 0xED,
    0x33, 0x2C, 0xBC, 0x7F, 0x16, 0x39, 0x3B, 0xEB, 0xD2, 0xDD, 0xA8, 0x01,
    0x73, 0x84, 0x62, 0xB9, 0x29, 0xD2, 0xC9, 0x51, 0x32, 0x9E, 0x7A, 0x6A,
    0xCF, 0xC1, 0x0A, 0xDB, 0x0E, 0xE0, 0x62, 0x77, 0x6F, 0x59, 0x62, 0x72,
    0x5A, 0x69, 0xA6, 0x5B, 0x70, 0xCA, 0x65, 0xC4, 0x95, 0x6F, 0x9A, 0xC2,
    0xDF, 0x72, 0x6D, 0xB1, 0x1E, 0x54, 0x7B, 0x51, 0xB4, 0xEF, 0x7F, 0x89,
    0x93, 0x74, 0x89, 0x59
};

typedef struct {
    DH *(*get_param) (void);
    const unsigned char *xA;
    size_t xA_len;
    const unsigned char *yA;
    size_t yA_len;
    const unsigned char *xB;
    size_t xB_len;
    const unsigned char *yB;
    size_t yB_len;
    const unsigned char *Z;
    size_t Z_len;
} rfc5114_td;

# define make_rfc5114_td(pre) { \
        DH_get_##pre, \
        dhtest_##pre##_xA, sizeof(dhtest_##pre##_xA), \
        dhtest_##pre##_yA, sizeof(dhtest_##pre##_yA), \
        dhtest_##pre##_xB, sizeof(dhtest_##pre##_xB), \
        dhtest_##pre##_yB, sizeof(dhtest_##pre##_yB), \
        dhtest_##pre##_Z, sizeof(dhtest_##pre##_Z) \
        }

static const rfc5114_td rfctd[] = {
        make_rfc5114_td(1024_160),
        make_rfc5114_td(2048_224),
        make_rfc5114_td(2048_256)
};

static int rfc5114_test(void)
{
    int i;
    DH *dhA = NULL;
    DH *dhB = NULL;
    unsigned char *Z1 = NULL;
    unsigned char *Z2 = NULL;
    const rfc5114_td *td = NULL;
    BIGNUM *bady = NULL, *priv_key = NULL, *pub_key = NULL;
    const BIGNUM *pub_key_tmp;

    for (i = 0; i < (int)OSSL_NELEM(rfctd); i++) {
        td = rfctd + i;
        /* Set up DH structures setting key components */
        if (!TEST_ptr(dhA = td->get_param())
                || !TEST_ptr(dhB = td->get_param()))
            goto bad_err;

        if (!TEST_ptr(priv_key = BN_bin2bn(td->xA, td->xA_len, NULL))
                || !TEST_ptr(pub_key = BN_bin2bn(td->yA, td->yA_len, NULL))
                || !TEST_true(DH_set0_key(dhA, pub_key, priv_key)))
            goto bad_err;

        if (!TEST_ptr(priv_key = BN_bin2bn(td->xB, td->xB_len, NULL))
                || !TEST_ptr(pub_key = BN_bin2bn(td->yB, td->yB_len, NULL))
                || !TEST_true( DH_set0_key(dhB, pub_key, priv_key)))
            goto bad_err;
        priv_key = pub_key = NULL;

        if (!TEST_uint_eq(td->Z_len, (size_t)DH_size(dhA))
            || !TEST_uint_eq(td->Z_len, (size_t)DH_size(dhB)))
            goto err;

        if (!TEST_ptr(Z1 = OPENSSL_malloc(DH_size(dhA)))
                || !TEST_ptr(Z2 = OPENSSL_malloc(DH_size(dhB))))
            goto bad_err;
        /*
         * Work out shared secrets using both sides and compare with expected
         * values.
         */
        DH_get0_key(dhB, &pub_key_tmp, NULL);
        if (!TEST_int_ne(DH_compute_key(Z1, pub_key_tmp, dhA), -1))
            goto bad_err;

        DH_get0_key(dhA, &pub_key_tmp, NULL);
        if (!TEST_int_ne(DH_compute_key(Z2, pub_key_tmp, dhB), -1))
            goto bad_err;

        if (!TEST_mem_eq(Z1, td->Z_len, td->Z, td->Z_len)
                || !TEST_mem_eq(Z2, td->Z_len, td->Z, td->Z_len))
            goto err;

        DH_free(dhA);
        dhA = NULL;
        DH_free(dhB);
        dhB = NULL;
        OPENSSL_free(Z1);
        Z1 = NULL;
        OPENSSL_free(Z2);
        Z2 = NULL;
    }

    /* Now i == OSSL_NELEM(rfctd) */
    /* RFC5114 uses unsafe primes, so now test an invalid y value */
    if (!TEST_ptr(dhA = DH_get_2048_224())
            || !TEST_ptr(Z1 = OPENSSL_malloc(DH_size(dhA))))
        goto bad_err;

    if (!TEST_ptr(bady = BN_bin2bn(dhtest_rfc5114_2048_224_bad_y,
                                   sizeof(dhtest_rfc5114_2048_224_bad_y),
                                   NULL)))
        goto bad_err;

    if (!DH_generate_key(dhA))
        goto bad_err;

    if (DH_compute_key(Z1, bady, dhA) != -1) {
        /*
         * DH_compute_key should fail with -1. If we get here we unexpectedly
         * allowed an invalid y value
         */
        goto err;
    }
    /* We'll have a stale error on the queue from the above test so clear it */
    ERR_clear_error();
    BN_free(bady);
    DH_free(dhA);
    OPENSSL_free(Z1);
    return 1;

 bad_err:
    BN_free(bady);
    DH_free(dhA);
    DH_free(dhB);
    BN_free(pub_key);
    BN_free(priv_key);
    OPENSSL_free(Z1);
    OPENSSL_free(Z2);
    TEST_error("Initialisation error RFC5114 set %d\n", i + 1);
    return 0;

 err:
    BN_free(bady);
    DH_free(dhA);
    DH_free(dhB);
    OPENSSL_free(Z1);
    OPENSSL_free(Z2);
    TEST_error("Test failed RFC5114 set %d\n", i + 1);
    return 0;
}

static int rfc7919_test(void)
{
    DH *a = NULL, *b = NULL;
    const BIGNUM *apub_key = NULL, *bpub_key = NULL;
    unsigned char *abuf = NULL;
    unsigned char *bbuf = NULL;
    int i, alen, blen, aout, bout;
    int ret = 0;

    if (!TEST_ptr(a = DH_new_by_nid(NID_ffdhe2048)))
         goto err;

    if (!DH_check(a, &i))
        goto err;
    if (!TEST_false(i & DH_CHECK_P_NOT_PRIME)
            || !TEST_false(i & DH_CHECK_P_NOT_SAFE_PRIME)
            || !TEST_false(i & DH_UNABLE_TO_CHECK_GENERATOR)
            || !TEST_false(i & DH_NOT_SUITABLE_GENERATOR)
            || !TEST_false(i))
        goto err;

    if (!DH_generate_key(a))
        goto err;
    DH_get0_key(a, &apub_key, NULL);

    /* now create another copy of the DH group for the peer */
    if (!TEST_ptr(b = DH_new_by_nid(NID_ffdhe2048)))
        goto err;

    if (!DH_generate_key(b))
        goto err;
    DH_get0_key(b, &bpub_key, NULL);

    alen = DH_size(a);
    if (!TEST_ptr(abuf = OPENSSL_malloc(alen))
            || !TEST_true((aout = DH_compute_key(abuf, bpub_key, a)) != -1))
        goto err;

    blen = DH_size(b);
    if (!TEST_ptr(bbuf = OPENSSL_malloc(blen))
            || !TEST_true((bout = DH_compute_key(bbuf, apub_key, b)) != -1))
        goto err;

    if (!TEST_true(aout >= 20)
            || !TEST_mem_eq(abuf, aout, bbuf, bout))
        goto err;

    ret = 1;

 err:
    OPENSSL_free(abuf);
    OPENSSL_free(bbuf);
    DH_free(a);
    DH_free(b);
    return ret;
}

static int prime_groups[] = {
    NID_ffdhe2048,
    NID_ffdhe3072,
    NID_ffdhe4096,
    NID_ffdhe6144,
    NID_ffdhe8192,
    NID_modp_2048,
    NID_modp_3072,
    NID_modp_4096,
    NID_modp_6144,
};

static int dh_test_prime_groups(int index)
{
    int ok = 0;
    DH *dh = NULL;
    const BIGNUM *p, *q, *g;

    if (!TEST_ptr(dh = DH_new_by_nid(prime_groups[index])))
        goto err;
    DH_get0_pqg(dh, &p, &q, &g);
    if (!TEST_ptr(p) || !TEST_ptr(q) || !TEST_ptr(g))
        goto err;

    if (!TEST_int_eq(DH_get_nid(dh), prime_groups[index]))
        goto err;
    ok = 1;
err:
    DH_free(dh);
    return ok;
}
#endif


int setup_tests(void)
{
#ifdef OPENSSL_NO_DH
    TEST_note("No DH support");
#else
    ADD_TEST(dh_test);
    ADD_TEST(rfc5114_test);
    ADD_TEST(rfc7919_test);
    ADD_ALL_TESTS(dh_test_prime_groups, OSSL_NELEM(prime_groups));
#endif
    return 1;
}
