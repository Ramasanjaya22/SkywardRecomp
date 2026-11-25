/*
 * xxh3_128.c -- self-contained C99 port of KenjiNX's XXH3-128 texture hash.
 *
 * Copyright (c) Kenji-NX Team and Contributors
 * Licensed under the MIT License.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Ported from (MIT):
 * https://git.ryujinx.app/projects/Kenji-NX/raw/branch/master/src/Ryujinx.Common/XXHash128.cs
 * XXHash128.ComputeHash() == Xxh3128bitsInternal(input, Xxh3KSecret, 0UL).
 * Only the portable scalar paths are implemented; AVX2 is not required.
 */
#include "xxh3_128.h"
#include <string.h>   /* memcpy */

#define XXH3_STRIPE_LEN             64u
#define XXH3_ACC_NB                 8u
#define XXH3_SECRET_CONSUME_RATE    8u
#define XXH3_SECRET_LASTACC_START   7u
#define XXH3_SECRET_MERGEACCS_START 11u
#define XXH3_SECRET_SIZE_MIN        136u
#define XXH3_MIDSIZE_STARTOFFSET    3u
#define XXH3_MIDSIZE_LASTOFFSET     17u
#define XXH3_SECRET_SIZE            192u

#define PRIME32_1 0x9E3779B1u
#define PRIME32_2 0x85EBCA77u
#define PRIME32_3 0xC2B2AE3Du
#define PRIME32_4 0x27D4EB2Fu
#define PRIME32_5 0x165667B1u

#define PRIME64_1 0x9E3779B185EBCA87ULL
#define PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define PRIME64_3 0x165667B19E3779F9ULL
#define PRIME64_4 0x85EBCA77C2B2AE63ULL
#define PRIME64_5 0x27D4EB2F165667C5ULL

/* KenjiNX Xxh3KSecret / XXH3 default secret (192 bytes). */
static const uint8_t xxh3_k_secret[XXH3_SECRET_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e
};

typedef struct {
    uint64_t low;
    uint64_t high;
} xxh128_t;

static uint32_t xxh_bswap32(uint32_t x)
{
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8) |
           ((x >> 8) & 0x0000ff00u) | ((x >> 24) & 0x000000ffu);
}

static uint64_t xxh_bswap64(uint64_t x)
{
    return ((x & 0x00000000000000ffULL) << 56) |
           ((x & 0x000000000000ff00ULL) << 40) |
           ((x & 0x0000000000ff0000ULL) << 24) |
           ((x & 0x00000000ff000000ULL) << 8) |
           ((x >> 8)  & 0x00000000ff000000ULL) |
           ((x >> 24) & 0x0000000000ff0000ULL) |
           ((x >> 40) & 0x000000000000ff00ULL) |
           ((x >> 56) & 0x00000000000000ffULL);
}

static uint32_t xxh_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xxh_read64(const uint8_t *p)
{
    return (uint64_t)xxh_read32(p) | ((uint64_t)xxh_read32(p + 4) << 32);
}

static uint32_t xxh_rotl32(uint32_t x, int r)
{
    return (x << r) | (x >> (32 - r));
}

static uint64_t xxh_xorshift64(uint64_t v, int s)
{
    return v ^ (v >> s);
}

/* C# Mult32To64(ulong x, ulong y) => (uint)x * (ulong)(uint)y */
static uint64_t xxh_mult32to64(uint64_t x, uint64_t y)
{
    return (uint64_t)(uint32_t)x * (uint64_t)(uint32_t)y;
}

/* Portable 64x64 -> 128 multiply; returns high/low halves. */
static void xxh_mult64to128(uint64_t lhs, uint64_t rhs, uint64_t *hi, uint64_t *lo)
{
    uint64_t u1 = lhs & 0xffffffffULL;
    uint64_t u2 = lhs >> 32;
    uint64_t v1 = rhs & 0xffffffffULL;
    uint64_t v2 = rhs >> 32;
    uint64_t t  = u1 * v1;
    uint64_t w3 = t & 0xffffffffULL;
    uint64_t k  = t >> 32;
    uint64_t w1, w2;

    t = u2 * v1 + k;
    w1 = t & 0xffffffffULL;
    w2 = t >> 32;
    t = u1 * v2 + w1;
    *lo = (t << 32) + w3;
    *hi = u2 * v2 + w2 + (t >> 32);
}

static uint64_t xxh_mul128_fold64(uint64_t lhs, uint64_t rhs)
{
    uint64_t hi, lo;
    xxh_mult64to128(lhs, rhs, &hi, &lo);
    return lo ^ hi;
}

static uint64_t xxh3_avalanche(uint64_t h)
{
    h = xxh_xorshift64(h, 37);
    h *= 0x165667919E3779F9ULL;
    h = xxh_xorshift64(h, 32);
    return h;
}

static uint64_t xxh64_avalanche(uint64_t h)
{
    h ^= h >> 33;
    h *= PRIME64_2;
    h ^= h >> 29;
    h *= PRIME64_3;
    h ^= h >> 32;
    return h;
}

/* Xxh3Len0To16128b */
static xxh128_t xxh3_len_0to16(const uint8_t *in, size_t len,
                               const uint8_t *secret, uint64_t seed)
{
    xxh128_t h;
    uint64_t flip_l, flip_h;
    uint64_t input_lo, input_hi, keyed;
    uint32_t c1, c2, c3, combined_l, combined_h;

    if (len > 8) {
        flip_l = (xxh_read64(secret + 32) ^ xxh_read64(secret + 40)) - seed;
        flip_h = (xxh_read64(secret + 48) ^ xxh_read64(secret + 56)) + seed;
        input_lo = xxh_read64(in);
        input_hi = xxh_read64(in + len - 8);

        xxh_mult64to128(input_lo ^ input_hi ^ flip_l, PRIME64_1, &h.high, &h.low);
        h.low += ((uint64_t)len - 1) << 54;
        input_hi ^= flip_h;
        h.high += input_hi + xxh_mult32to64((uint32_t)input_hi, PRIME32_2 - 1);
        h.low ^= xxh_bswap64(h.high);

        {
            xxh128_t m = h;
            xxh_mult64to128(m.low, PRIME64_2, &h.high, &h.low);
            h.high += m.high * PRIME64_2;
        }
        h.low  = xxh3_avalanche(h.low);
        h.high = xxh3_avalanche(h.high);
        return h;
    }

    if (len >= 4) {
        /* KenjiNX seed adjustment (always 0 for ComputeHash). */
        seed ^= (uint64_t)xxh_bswap32((uint32_t)seed) << 32;
        keyed = (uint64_t)xxh_read32(in) + ((uint64_t)xxh_read32(in + len - 4) << 32);
        keyed ^= (xxh_read64(secret + 16) ^ xxh_read64(secret + 24)) + seed;
        xxh_mult64to128(keyed, PRIME64_1 + ((uint64_t)len << 2), &h.high, &h.low);
        h.high += h.low << 1;
        h.low  ^= h.high >> 3;
        h.low   = xxh_xorshift64(h.low, 35);
        h.low  *= 0x9FB21C651E98DF25ULL;
        h.low   = xxh_xorshift64(h.low, 28);
        h.high  = xxh3_avalanche(h.high);
        return h;
    }

    if (len > 0) {
        c1 = (uint32_t)in[0];
        c2 = (uint32_t)in[len >> 1];
        c3 = (uint32_t)in[len - 1];
        combined_l = (c1 << 16) | (c2 << 24) | c3 | ((uint32_t)len << 8);
        combined_h = xxh_rotl32(xxh_bswap32(combined_l), 13);
        flip_l = (uint64_t)(xxh_read32(secret) ^ xxh_read32(secret + 4)) + seed;
        flip_h = (uint64_t)(xxh_read32(secret + 8) ^ xxh_read32(secret + 12)) - seed;
        h.low  = xxh64_avalanche((uint64_t)combined_l ^ flip_l);
        h.high = xxh64_avalanche((uint64_t)combined_h ^ flip_h);
        return h;
    }

    h.low  = xxh64_avalanche(seed ^ (xxh_read64(secret + 64) ^ xxh_read64(secret + 72)));
    h.high = xxh64_avalanche(seed ^ (xxh_read64(secret + 80) ^ xxh_read64(secret + 88)));
    return h;
}

/* Xxh3Mix16b */
static uint64_t xxh3_mix16b(const uint8_t *in, const uint8_t *secret, uint64_t seed)
{
    return xxh_mul128_fold64(xxh_read64(in) ^ (xxh_read64(secret) + seed),
                             xxh_read64(in + 8) ^ (xxh_read64(secret + 8) - seed));
}

/* Xxh128Mix32b */
static xxh128_t xxh3_mix32b(xxh128_t acc, const uint8_t *in1, const uint8_t *in2,
                            const uint8_t *secret, uint64_t seed)
{
    acc.low  += xxh3_mix16b(in1, secret, seed);
    acc.low  ^= xxh_read64(in2) + xxh_read64(in2 + 8);
    acc.high += xxh3_mix16b(in2, secret + 16, seed);
    acc.high ^= xxh_read64(in1) + xxh_read64(in1 + 8);
    return acc;
}

static xxh128_t xxh3_finalize_midsize(xxh128_t acc, size_t len, uint64_t seed)
{
    xxh128_t h;
    h.low  = acc.low + acc.high;
    h.high = acc.low * PRIME64_1 + acc.high * PRIME64_4 +
             ((uint64_t)len - seed) * PRIME64_2;
    h.low  = xxh3_avalanche(h.low);
    h.high = 0ULL - xxh3_avalanche(h.high);
    return h;
}

/* Xxh3Len17To128128b */
static xxh128_t xxh3_len_17to128(const uint8_t *in, size_t len,
                                 const uint8_t *secret, uint64_t seed)
{
    xxh128_t acc;
    acc.low = (uint64_t)len * PRIME64_1;
    acc.high = 0;

    if (len > 32) {
        if (len > 64) {
            if (len > 96)
                acc = xxh3_mix32b(acc, in + 48, in + len - 64, secret + 96, seed);
            acc = xxh3_mix32b(acc, in + 32, in + len - 48, secret + 64, seed);
        }
        acc = xxh3_mix32b(acc, in + 16, in + len - 32, secret + 32, seed);
    }
    acc = xxh3_mix32b(acc, in, in + len - 16, secret, seed);
    return xxh3_finalize_midsize(acc, len, seed);
}

/* Xxh3Len129To240128b */
static xxh128_t xxh3_len_129to240(const uint8_t *in, size_t len,
                                  const uint8_t *secret, uint64_t seed)
{
    xxh128_t acc;
    unsigned int nb_rounds = (unsigned int)(len / 32);
    unsigned int i;

    acc.low = (uint64_t)len * PRIME64_1;
    acc.high = 0;
    for (i = 0; i < 4; i++)
        acc = xxh3_mix32b(acc, in + 32 * i, in + 32 * i + 16, secret + 32 * i, seed);
    acc.low  = xxh3_avalanche(acc.low);
    acc.high = xxh3_avalanche(acc.high);
    for (i = 4; i < nb_rounds; i++)
        acc = xxh3_mix32b(acc, in + 32 * i, in + 32 * i + 16,
                          secret + XXH3_MIDSIZE_STARTOFFSET + 32 * (i - 4), seed);
    acc = xxh3_mix32b(acc, in + len - 16, in + len - 32,
                      secret + XXH3_SECRET_SIZE_MIN - XXH3_MIDSIZE_LASTOFFSET - 16,
                      0ULL - seed);
    return xxh3_finalize_midsize(acc, len, seed);
}

/* Xxh3Accumulate512 (portable scalar path) */
static void xxh3_accumulate_512(uint64_t acc[XXH3_ACC_NB], const uint8_t *in,
                                const uint8_t *secret)
{
    unsigned int i;
    for (i = 0; i < XXH3_ACC_NB; i++) {
        uint64_t data_val = xxh_read64(in + 8 * i);
        uint64_t data_key = data_val ^ xxh_read64(secret + 8 * i);
        acc[i ^ 1] += data_val;
        acc[i] += xxh_mult32to64((uint32_t)data_key, data_key >> 32);
    }
}

/* Xxh3ScrambleAcc (portable scalar path) */
static void xxh3_scramble_acc(uint64_t acc[XXH3_ACC_NB], const uint8_t *secret)
{
    unsigned int i;
    for (i = 0; i < XXH3_ACC_NB; i++) {
        uint64_t acc64 = xxh_xorshift64(acc[i], 47);
        acc64 ^= xxh_read64(secret + 8 * i);
        acc64 *= PRIME32_1;
        acc[i] = acc64;
    }
}

static void xxh3_accumulate(uint64_t acc[XXH3_ACC_NB], const uint8_t *in,
                            const uint8_t *secret, unsigned int nb_stripes)
{
    unsigned int n;
    for (n = 0; n < nb_stripes; n++)
        xxh3_accumulate_512(acc, in + XXH3_STRIPE_LEN * n,
                            secret + XXH3_SECRET_CONSUME_RATE * n);
}

static uint64_t xxh3_merge_accs(const uint64_t acc[XXH3_ACC_NB],
                                const uint8_t *secret, uint64_t start)
{
    uint64_t result = start;
    unsigned int i;
    for (i = 0; i < 4; i++)
        result += xxh_mul128_fold64(acc[2 * i] ^ xxh_read64(secret + 16 * i),
                                    acc[2 * i + 1] ^ xxh_read64(secret + 16 * i + 8));
    return xxh3_avalanche(result);
}

/* Xxh3HashLong128bInternal */
static xxh128_t xxh3_hash_long(const uint8_t *in, size_t len, const uint8_t *secret)
{
    static const uint64_t init_acc[XXH3_ACC_NB] = {
        PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
        PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1
    };
    uint64_t acc[XXH3_ACC_NB];
    unsigned int stripes_per_block =
        (unsigned int)((XXH3_SECRET_SIZE - XXH3_STRIPE_LEN) / XXH3_SECRET_CONSUME_RATE);
    size_t block_len = XXH3_STRIPE_LEN * (size_t)stripes_per_block;
    size_t nb_blocks = (len - 1) / block_len;
    size_t n;
    xxh128_t h;

    memcpy(acc, init_acc, sizeof(uint64_t) * XXH3_ACC_NB);

    for (n = 0; n < nb_blocks; n++) {
        xxh3_accumulate(acc, in + block_len * n, secret, stripes_per_block);
        xxh3_scramble_acc(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
    }
    xxh3_accumulate(acc, in + block_len * nb_blocks, secret,
                    (unsigned int)((len - 1 - block_len * nb_blocks) / XXH3_STRIPE_LEN));
    xxh3_accumulate_512(acc, in + len - XXH3_STRIPE_LEN,
                        secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN - XXH3_SECRET_LASTACC_START);

    h.low = xxh3_merge_accs(acc, secret + XXH3_SECRET_MERGEACCS_START,
                            (uint64_t)len * PRIME64_1);
    h.high = xxh3_merge_accs(acc,
                             secret + XXH3_SECRET_SIZE - XXH3_ACC_NB * 8 - XXH3_SECRET_MERGEACCS_START,
                             ~((uint64_t)len * PRIME64_2));
    return h;
}

void xxh3_128(const void *data, size_t len, uint64_t out[2])
{
    const uint8_t *in = (const uint8_t *)data;
    xxh128_t h;

    if (len <= 16)
        h = xxh3_len_0to16(in, len, xxh3_k_secret, 0);
    else if (len <= 128)
        h = xxh3_len_17to128(in, len, xxh3_k_secret, 0);
    else if (len <= 240)
        h = xxh3_len_129to240(in, len, xxh3_k_secret, 0);
    else
        h = xxh3_hash_long(in, len, xxh3_k_secret);

    out[0] = h.high;
    out[1] = h.low;
}
