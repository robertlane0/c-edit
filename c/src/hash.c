#include "edit/hash.h"

#include <assert.h>
#include <string.h>

static uint64_t wyr3(const uint8_t *p, size_t k) {
    // k in 1..4; caller bounds-checks.
    uint64_t p0 = (uint64_t)p[0];
    uint64_t p1 = (uint64_t)p[k >> 1U];
    uint64_t p2 = (uint64_t)p[k - 1U];
    return (p0 << 16U) | (p1 << 8U) | p2;
}

static uint64_t wyr4(const uint8_t *p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof v);
    return (uint64_t)v;
}

static uint64_t wyr8(const uint8_t *p) {
    uint64_t v = 0;
    memcpy(&v, p, sizeof v);
    return v;
}

uint64_t edit_wymix(uint64_t lhs, uint64_t rhs) {
    __uint128_t r = (__uint128_t)lhs * (__uint128_t)rhs;
    return (uint64_t)(r >> 64) ^ (uint64_t)r;
}

uint64_t edit_hash(uint64_t seed, const uint8_t *data, size_t len) {
    static const uint64_t S0 = UINT64_C(0xa0761d6478bd642f);
    static const uint64_t S1 = UINT64_C(0xe7037ed1a0b428db);
    static const uint64_t S2 = UINT64_C(0x8ebc6af09c88c6e3);
    static const uint64_t S3 = UINT64_C(0x589965cc75374cc3);

    if (len > 0 && data == NULL) {
        return 0; // invalid input, defined output
    }
    if (len == 0) {
        // Generic path below would call wyr3 with k=0; avoid it.
        return edit_wymix(S1 ^ (uint64_t)0, edit_wymix(S1, (S0 ^ seed)));
    }

    uint64_t a = 0;
    uint64_t b = 0;
    seed ^= S0;

    if (len <= 16) {
        if (len >= 4) {
            size_t mid = (len >> 3U) << 2U;
            a = (wyr4(data) << 32U) | wyr4(data + mid);
            b = (wyr4(data + len - 4U) << 32U) | wyr4(data + len - 4U - mid);
        } else {
            a = wyr3(data, len);
            b = 0;
        }
    } else {
        size_t off = 0;
        size_t i = len;
        if (i > 48) {
            uint64_t seed1 = seed;
            uint64_t seed2 = seed;
            do {
                seed = edit_wymix(wyr8(data + off) ^ S1, wyr8(data + off + 8U) ^ seed);
                seed1 = edit_wymix(wyr8(data + off + 16U) ^ S2, wyr8(data + off + 24U) ^ seed1);
                seed2 = edit_wymix(wyr8(data + off + 32U) ^ S3, wyr8(data + off + 40U) ^ seed2);
                off += 48;
                i -= 48;
            } while (i > 48);
            seed ^= seed1 ^ seed2;
        }
        while (i > 16) {
            seed = edit_wymix(wyr8(data + off) ^ S1, wyr8(data + off + 8U) ^ seed);
            off += 16;
            i -= 16;
        }
        a = wyr8(data + len - 16U);
        b = wyr8(data + len - 8U);
    }

    return edit_wymix(S1 ^ (uint64_t)len, edit_wymix(a ^ S1, b ^ seed));
}

uint64_t edit_hash_str(uint64_t seed, const char *s) {
    if (s == NULL) {
        return edit_hash(seed, NULL, 0);
    }
    return edit_hash(seed, (const uint8_t *)s, strlen(s));
}
